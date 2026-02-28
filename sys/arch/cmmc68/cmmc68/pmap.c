/*	$NetBSD: pmap.c,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $	*/

/*
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * pmap module for CMMC68 with custom MMU.
 *
 * The CMMC68 MMU uses a single flat page table with 4096 × 16-bit PTEs
 * covering the 16MB address space.  The page table is accessed through
 * a hardware window at MMU_PAGETABLE_WIN (0xFD2000).
 *
 * The bootloader sets up a static mapping of kernel VA 0x0-0x3FFFFF to
 * PA 0x400000-0x7FFFFF.  pmap manages dynamic mappings for UBC windows,
 * kmem, and user pages by writing PTEs directly to the page table window.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/pool.h>
#include <sys/kmem.h>

#include <uvm/uvm.h>

#include <machine/param.h>
#include <machine/vmparam.h>
#include <machine/mmu.h>
#include <machine/pmap.h>
#include <machine/cpu.h>

#include <m68k/fcode.h>
#include <m68k/pmap_motorola.h>

/* Forward declarations */
paddr_t pmap_bootstrap1(paddr_t, paddr_t);
void pmap_bootstrap(paddr_t, paddr_t);
void pmap_unwire(pmap_t, vaddr_t);
void pmap_zero_page_area(paddr_t, off_t, size_t);
bool pmap_pageidlezero(paddr_t);
long pmap_physaddr(vaddr_t);
void pmap_tlb_flush(void);

/*
 * MMU segment table physical address
 * Used by locore.s during MMU initialization
 */
paddr_t Sysseg_pa = 0;

/*
 * Protection codes - used to convert VM protection to MMU protection bits
 * For our custom MMU, we use the standard m68k PG_RW and PG_RO
 */
int protection_codes[8];

/*
 * Kernel pmap
 */
struct pmap kernel_pmap_store;
struct pmap *const kernel_pmap_ptr = &kernel_pmap_store;

/*
 * Kernel PA offset: the physical address that VA 0x0 maps to.
 * Set dynamically from the MMU hardware PTE for VPN 0 during bootstrap.
 * In ROM mode (kernel loaded separately): 0x400000
 * In RAM mode (packed with 4K bootloader): 0x401000
 */
paddr_t kernel_pa_offset;

/*
 * lwp0 u-area virtual address
 * Allocated in pmap_bootstrap1, used in pmap_bootstrap2
 */
vaddr_t lwp0uarea;

/*
 * Virtual address range available to the kernel
 * Used by pmap_virtual_space and pmap_steal_memory
 * Declared extern in pmap_motorola.h
 */
vaddr_t virtual_avail;
vaddr_t virtual_end;

/*
 * Scratch VAs for pmap_zero_page and pmap_copy_page.
 * These are reserved kernel VAs with no permanent backing — they
 * are temporarily mapped to target PAs during page operations.
 *
 * CRITICAL: We cannot use va = pa - kernel_pa_offset because that
 * VA may already be in use by kernel data structures (kmem, softint,
 * etc.) that were mapped to different PAs via pmap_kenter_pa.
 * Temporarily remapping such a VA would cause the wrong physical page
 * to be visible to interrupt handlers that access the data structure.
 */
static vaddr_t pmap_scr1_va;	/* for pmap_zero_page dest */
static vaddr_t pmap_scr2_va;	/* for pmap_copy_page src */

/*
 * Software bitmap tracking which pages were explicitly mapped by pmap.
 * The bootloader maps all of kernel VA (0x0-0x3FFFFF), but UVM needs
 * pmap_extract to return false for pages not explicitly managed by pmap.
 * 1024 pages (4MB / 4KB) = 128 bytes bitmap.
 */
#define PMAP_NKPAGES	1024	/* kernel VA 0x0-0x3FFFFF at 4KB pages */
static uint8_t pmap_mapped[PMAP_NKPAGES / 8];

static inline void
pmap_bitmap_set(vaddr_t va)
{
	unsigned int vpn = (va >> PAGE_SHIFT) & 0x3FF;
	pmap_mapped[vpn >> 3] |= (1 << (vpn & 7));
}

static inline void
pmap_bitmap_clear(vaddr_t va)
{
	unsigned int vpn = (va >> PAGE_SHIFT) & 0x3FF;
	pmap_mapped[vpn >> 3] &= ~(1 << (vpn & 7));
}

static inline bool
pmap_bitmap_test(vaddr_t va)
{
	unsigned int vpn = (va >> PAGE_SHIFT) & 0x3FF;
	return (pmap_mapped[vpn >> 3] & (1 << (vpn & 7))) != 0;
}

/*
 * Write a PTE to the MMU page table window.
 * The window at 0xFD2000 has 4096 × 2-byte entries covering 16MB VA space.
 */
static inline void
pmap_write_pte(vaddr_t va, uint16_t pte)
{
	unsigned int vpn = (va >> PAGE_SHIFT) & 0xFFF;
	((volatile uint16_t *)MMU_PAGETABLE_WIN)[vpn] = pte;
}

static inline uint16_t
pmap_read_pte(vaddr_t va)
{
	unsigned int vpn = (va >> PAGE_SHIFT) & 0xFFF;
	return ((volatile uint16_t *)MMU_PAGETABLE_WIN)[vpn];
}


/*
 * Bootstrap pmap initialization
 * Called very early in boot before VM is initialized
 * For CMMC68, this is called as pmap_bootstrap1 from locore.s
 * 
 * IMPORTANT: The bootloader has already set up MMU mappings.
 * We just initialize the kernel pmap structure here and allocate
 * the lwp0 u-area.
 */
paddr_t
pmap_bootstrap1(paddr_t nextpa, paddr_t reloff)
{
	paddr_t lwp0upa;

	/*
	 * Auto-detect kernel PA offset from the MMU hardware.
	 * The bootloader wrote PTEs mapping VPN 0 → some PPN.
	 * Read PTE for VPN 0 to determine the PA offset.
	 *   ROM mode:  VPN 0 → PPN 0x400 → offset 0x400000
	 *   RAM mode:  VPN 0 → PPN 0x401 → offset 0x401000
	 */
	{
		volatile uint16_t *pte_window =
		    (volatile uint16_t *)MMU_PAGETABLE_WIN;
		uint16_t pte0 = pte_window[0];
		uint16_t ppn0 = pte0 & PTE_PPN;
		kernel_pa_offset = (paddr_t)ppn0 << PAGE_SHIFT;
	}

	/* Allocate lwp0 u-area (USPACE bytes, typically 8KB) */
	lwp0upa = m68k_round_page(nextpa);
	nextpa = lwp0upa + USPACE;

	/*
	 * Store lwp0 u-area virtual address.
	 * VA = PA - kernel_pa_offset
	 */
	lwp0uarea = (vaddr_t)(lwp0upa - kernel_pa_offset);

	/* Initialize kernel pmap */
	memset(&kernel_pmap_store, 0, sizeof(kernel_pmap_store));
	kernel_pmap_store.pm_count = 1;

	/* Initialize virtual_avail/end for pmap_virtual_space */
	virtual_avail = (vaddr_t)(nextpa - kernel_pa_offset);

	/*
	 * Reserve two scratch VAs for pmap_zero_page/pmap_copy_page.
	 * These VAs have no backing PA — they are temporarily mapped
	 * on demand and then invalidated.
	 */
	pmap_scr1_va = virtual_avail;
	virtual_avail += PAGE_SIZE;
	pmap_scr2_va = virtual_avail;
	virtual_avail += PAGE_SIZE;

	virtual_end = VM_MAX_KERNEL_ADDRESS;

	/* Return next physical address */
	return nextpa;
}

/*
 * Second-stage pmap bootstrap
 * Called after MMU is enabled to finish initialization
 */
void *
pmap_bootstrap2(void)
{
	extern vaddr_t lwp0uarea;
	paddr_t avail_start, avail_end;

	/* Initialize UVM page size */
	uvmexp.pagesize = NBPG;

	uvm_md_init();

	/*
	 * Register physical memory with UVM.
	 * CMMC68 has RAM at PA 0x400000-0x800000 (4MB).
	 * The kernel is loaded at PA 0x400000, so we need to skip
	 * the kernel and reserve space for the lwp0 uarea.
	 * virtual_avail was set in pmap_bootstrap1 to the VA after kernel.
	 */
	avail_start = (paddr_t)virtual_avail + kernel_pa_offset;
	avail_end = 0x800000;  /* End of 4MB RAM */

	/* Register available physical memory with UVM */
	uvm_page_physload(atop(avail_start), atop(avail_end),
			   atop(avail_start), atop(avail_end),
			   VM_FREELIST_DEFAULT);

	/* Initialize protection codes (using Motorola-compatible values) */
	protection_codes[VM_PROT_NONE|VM_PROT_NONE|VM_PROT_NONE] = 0;
	protection_codes[VM_PROT_READ|VM_PROT_NONE|VM_PROT_NONE] = PG_RO;
	protection_codes[VM_PROT_READ|VM_PROT_NONE|VM_PROT_EXECUTE] = PG_RO;
	protection_codes[VM_PROT_NONE|VM_PROT_NONE|VM_PROT_EXECUTE] = PG_RO;
	protection_codes[VM_PROT_NONE|VM_PROT_WRITE|VM_PROT_NONE] = PG_RW;
	protection_codes[VM_PROT_NONE|VM_PROT_WRITE|VM_PROT_EXECUTE] = PG_RW;
	protection_codes[VM_PROT_READ|VM_PROT_WRITE|VM_PROT_NONE] = PG_RW;
	protection_codes[VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE] = PG_RW;

	/*
	 * Mark all kernel pages below virtual_avail as "mapped" in our
	 * software bitmap. These are the bootloader-mapped kernel text,
	 * data, bss, and uarea pages. Pages above virtual_avail are
	 * left unset so pmap_extract returns false for free KVA.
	 */
	{
		vaddr_t va;
		for (va = 0; va < virtual_avail; va += PAGE_SIZE)
			pmap_bitmap_set(va);
	}

	/*
	 * Clear bootloader PTEs for the scratch VAs.
	 * These are reserved for pmap_zero_page/pmap_copy_page and
	 * must not have stale mappings.
	 */
	pmap_write_pte(pmap_scr1_va, PTE_INVALID);
	pmap_write_pte(pmap_scr2_va, PTE_INVALID);

	/*
	 * Initialize lwp0 uarea, curlwp, and curpcb.
	 * This is critical for main() to run properly.
	 */
	memset((void *)lwp0uarea, 0, USPACE);
	uvm_lwp_setuarea(&lwp0, lwp0uarea);
	curlwp = &lwp0;
	curpcb = lwp_getpcb(&lwp0);

	/* Return lwp0 uarea pointer as required by the interface */
	return (void *)lwp0uarea;
}

/*
 * Initialize pmap module
 */
void
pmap_init(void)
{
	/*
	 * Set SFC and DFC to FC_USERD so that copyin/copyout can use
	 * the "moves" instruction to access user space.  The DIAGNOSTIC
	 * checks in copy.s verify these are set correctly.
	 */
	setsfc(FC_USERD);
	setdfc(FC_USERD);

}

/*
 * Create a new pmap
 */
pmap_t
pmap_create(void)
{
	struct pmap *pm;
	
	pm = kmem_zalloc(sizeof(*pm), KM_SLEEP);
	if (pm == NULL)
		return NULL;
	
	pm->pm_count = 1;
	return pm;
}

/*
 * Destroy a pmap
 */
void
pmap_destroy(pmap_t pm)
{
	if (pm == NULL)
		return;
	
	if (atomic_dec_uint_nv(&pm->pm_count) == 0) {
		kmem_free(pm, sizeof(*pm));
	}
}

/*
 * Reference a pmap
 */
void
pmap_reference(pmap_t pm)
{
	if (pm != NULL)
		atomic_inc_uint(&pm->pm_count);
}

/*
 * Build a PTE from PA and protection bits for our custom MMU.
 */
static uint16_t
pmap_prot_to_pte(vm_prot_t prot, bool user)
{
	uint16_t pte = 0;

	if (prot & VM_PROT_WRITE)
		pte |= PTE_RW;
	if (prot & VM_PROT_EXECUTE)
		pte |= PTE_EX;
	if (user)
		pte |= PTE_U;
	return pte;
}

/*
 * Enter a mapping
 */
int
pmap_enter(pmap_t pm, vaddr_t va, paddr_t pa, vm_prot_t prot, u_int flags)
{
	uint16_t pte;
	unsigned int ppn;
	bool user = (va >= VM_MIN_USER_ADDRESS);

	ppn = (pa >> PAGE_SHIFT) & PTE_PPN;
	pte = PTE_M | ppn | pmap_prot_to_pte(prot, user);

	pmap_write_pte(va, pte);

	/* Track kernel VA in bitmap for pmap_extract */
	if (!user && va < VM_MAX_KERNEL_ADDRESS)
		pmap_bitmap_set(va);

	return 0;
}

/*
 * Remove mappings in the given range.
 * Clear PTEs for both user VA and kernel VA.  For kernel VA, this is
 * needed by UBC which calls pmap_remove(pmap_kernel()) to unmap window
 * pages when recycling windows for different file data.
 */
void
pmap_remove(pmap_t pm, vaddr_t sva, vaddr_t eva)
{
	vaddr_t va;

	for (va = sva; va < eva; va += PAGE_SIZE) {
		pmap_write_pte(va, PTE_INVALID);
		if (va < VM_MAX_KERNEL_ADDRESS)
			pmap_bitmap_clear(va);
	}
}

/*
 * Extract physical address.
 * For kernel VA, use the bootloader's known VA→PA mapping.
 * For user VA, read the hardware PTE.
 */
bool
pmap_extract(pmap_t pm, vaddr_t va, paddr_t *pap)
{
	if (pm == pmap_kernel()) {
		/*
		 * Kernel VA: use bitmap to distinguish pages explicitly
		 * mapped via pmap_kenter_pa from the bootloader's static
		 * mapping. UVM needs pmap_extract to return false for
		 * unallocated KVA.
		 */
		if (!pmap_bitmap_test(va))
			return false;
		/* Read actual PA from hardware PTE */
		uint16_t pte = pmap_read_pte(va);
		if (!(pte & PTE_M))
			return false;
		if (pap != NULL)
			*pap = ((paddr_t)(pte & PTE_PPN) << PAGE_SHIFT) |
			    (va & PAGE_MASK);
		return true;
	}

	/* User VA: read hardware PTE directly */
	{
		uint16_t pte = pmap_read_pte(va);
		if (!(pte & PTE_M))
			return false;
		if (pap != NULL)
			*pap = ((paddr_t)(pte & PTE_PPN) << PAGE_SHIFT) |
			    (va & PAGE_MASK);
		return true;
	}
}

/*
 * Protect a range.
 * Only modify hardware PTEs for user VA.
 */
void
pmap_protect(pmap_t pm, vaddr_t sva, vaddr_t eva, vm_prot_t prot)
{
	vaddr_t va;
	uint16_t pte;

	if (prot == VM_PROT_NONE) {
		pmap_remove(pm, sva, eva);
		return;
	}

	for (va = sva; va < eva; va += PAGE_SIZE) {
		if (va < VM_MIN_USER_ADDRESS)
			continue;
		pte = pmap_read_pte(va);
		if (!(pte & PTE_M))
			continue;
		pte = PTE_M | (pte & PTE_PPN) | pmap_prot_to_pte(prot, true);
		pmap_write_pte(va, pte);
	}
}

/*
 * Activate a pmap (switch to it)
 */
void
pmap_activate(struct lwp *l)
{
	/* TODO: Switch MMU context */
}

/*
 * Deactivate a pmap
 */
void
pmap_deactivate(struct lwp *l)
{
	/* Nothing to do */
}

/*
 * Unwire a page
 */
void
pmap_unwire(pmap_t pm, vaddr_t va)
{
	/* Nothing to do */
}

/*
 * Clear modify bits - takes vm_page now
 */
bool
pmap_clear_modify(struct vm_page *pg)
{
	/* TODO: Implement */
	return false;
}

/*
 * Clear reference bits - takes vm_page now
 */
bool
pmap_clear_reference(struct vm_page *pg)
{
	/* TODO: Implement */
	return false;
}

/*
 * Is page modified? - takes vm_page now
 */
bool
pmap_is_modified(struct vm_page *pg)
{
	/* TODO: Implement */
	return false;
}

/*
 * Is page referenced? - takes vm_page now
 */
bool
pmap_is_referenced(struct vm_page *pg)
{
	/* TODO: Implement */
	return false;
}

/*
 * Copy a page using dedicated scratch VAs.
 *
 * We must NOT use va = pa - kernel_pa_offset because that VA may have
 * been remapped by pmap_kenter_pa to a different PA (e.g., for kmem
 * or softint structures).  Temporarily remapping it would cause
 * interrupt handlers to access the wrong physical page.
 */
void
pmap_copy_page(paddr_t src, paddr_t dst)
{
	unsigned int src_ppn = (src >> PAGE_SHIFT) & PTE_PPN;
	unsigned int dst_ppn = (dst >> PAGE_SHIFT) & PTE_PPN;

	pmap_write_pte(pmap_scr2_va,
	    PTE_EX | PTE_RW | PTE_M | src_ppn);
	pmap_write_pte(pmap_scr1_va,
	    PTE_EX | PTE_RW | PTE_M | dst_ppn);

	memcpy((void *)pmap_scr1_va, (void *)pmap_scr2_va, PAGE_SIZE);

	pmap_write_pte(pmap_scr1_va, PTE_INVALID);
	pmap_write_pte(pmap_scr2_va, PTE_INVALID);
}

/*
 * Zero a page using a dedicated scratch VA.
 */
void
pmap_zero_page(paddr_t pa)
{
	unsigned int ppn = (pa >> PAGE_SHIFT) & PTE_PPN;

	pmap_write_pte(pmap_scr1_va, PTE_EX | PTE_RW | PTE_M | ppn);

	memset((void *)pmap_scr1_va, 0, PAGE_SIZE);

	pmap_write_pte(pmap_scr1_va, PTE_INVALID);
}

/*
 * Zero an area of a page using a dedicated scratch VA.
 */
void
pmap_zero_page_area(paddr_t pa, off_t off, size_t size)
{
	unsigned int ppn = (pa >> PAGE_SHIFT) & PTE_PPN;

	pmap_write_pte(pmap_scr1_va, PTE_EX | PTE_RW | PTE_M | ppn);

	memset((char *)pmap_scr1_va + off, 0, size);

	pmap_write_pte(pmap_scr1_va, PTE_INVALID);
}

/*
 * Page idle zero - no-op for now
 */
bool
pmap_pageidlezero(paddr_t pa)
{
	pmap_zero_page(pa);
	return true;
}

/*
 * Protect a physical page - change protection in all mappings.
 * With the bootloader's static mapping covering all kernel VA, we must
 * not scan/clear hardware PTEs blindly. Stub for now.
 */
void
pmap_page_protect(struct vm_page *pg, vm_prot_t prot)
{
	/* No-op: bootloader mappings must not be disturbed */
}


/*
 * Kernel virtual enter (no PTE_U — kernel only)
 *
 * Write a hardware PTE to map VA → PA for kernel use.
 * The bootloader's static mapping (VA+0x400000) is only valid for
 * kernel text/data. Dynamic KVA (UBC, kmem, etc.) needs real PTEs
 * because the PA may differ from the static offset.
 */
void
pmap_kenter_pa(vaddr_t va, paddr_t pa, vm_prot_t prot, u_int flags)
{
	uint16_t pte;
	unsigned int ppn;

	ppn = (pa >> PAGE_SHIFT) & PTE_PPN;
	pte = PTE_M | PTE_RW | PTE_EX | ppn;	/* kernel: no PTE_U */

	pmap_write_pte(va, pte);

	if (va < VM_MAX_KERNEL_ADDRESS)
		pmap_bitmap_set(va);
}

/*
 * Kernel virtual remove
 *
 * Clear the PTE to PTE_INVALID so subsequent accesses fault.
 * This is critical for UBC: the read path relies on page faults
 * to trigger genfs_getpages when a file page needs loading.
 * If we restore the bootloader's static mapping instead, the CPU
 * reads from the wrong PA (static offset) without faulting.
 */
void
pmap_kremove(vaddr_t va, vsize_t size)
{
	vaddr_t end = va + size;

	for (; va < end; va += PAGE_SIZE) {
		pmap_write_pte(va, PTE_INVALID);

		if (va < VM_MAX_KERNEL_ADDRESS)
			pmap_bitmap_clear(va);
	}
}

/*
 * Get physical address - deprecated, may not be needed
 */
long
pmap_physaddr(vaddr_t va)
{
	/* TODO: Implement */
	return 0;
}

/*
 * Flush TLB - no-op for now
 */
void
pmap_tlb_flush(void)
{
	/* TODO: Implement TLB flush */
}

/*
 * Update pmap - no-op for now (macro in pmap_motorola.h)
 */
/* pmap_update is a macro, don't define it here */

/*
 * Get virtual space range
 */
void
pmap_virtual_space(vaddr_t *startp, vaddr_t *endp)
{
	/*
	 * Return kernel virtual address range.
	 * The bootloader maps VA 0x0-0x3FFFFF to PA 0x400000 (kernel RAM).
	 * virtual_avail is set by pmap_bootstrap to the end of the kernel
	 * image plus any early allocations.
	 */
	*startp = virtual_avail;
	*endp = VM_MAX_KERNEL_ADDRESS;
}

/*
 * Copy page mappings from one pmap to another
 */
void
pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, vaddr_t dst_addr, vsize_t len, vaddr_t src_addr)
{
	/* TODO: Implement if needed for fork() */
}

/*
 * Get physical address from virtual
 * Used by uvm_device
 */
paddr_t
pmap_phys_address(vaddr_t va)
{
	/* TODO: Extract PA from page table */
	/* For now, assume identity mapping in kernel space */
	if (va >= VM_MIN_KERNEL_ADDRESS && va < VM_MAX_KERNEL_ADDRESS) {
		return va - VM_MIN_KERNEL_ADDRESS;
	}
	return 0;
}

