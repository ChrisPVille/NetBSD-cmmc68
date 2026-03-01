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
 * Software reference and modify bitmaps.
 *
 * The CMMC68 MMU has no hardware reference/modify bits in its PTEs
 * (format: EX/RW/U/M/PPN — M is "mapped/valid", not "modified").
 * Without software tracking, pmap_is_referenced() returns false for
 * ALL pages, which causes the page daemon's clock algorithm to treat
 * every page as unreferenced → immediate eviction → catastrophic
 * thrashing when demand-paging brings in new code pages (e.g. first
 * invocation of ls loads ~50 pages of fts/sort/format code that were
 * never touched by sh or chmod, pushing free below freemin, triggering
 * the page daemon which evicts those same pages in a tight loop).
 *
 * Fix: set reference bit in pmap_enter (page was just mapped, so it's
 * being used).  The page daemon's clock hand clears it; if the page
 * is re-entered before the next scan, it survives.  This gives each
 * page at least one clock-hand reprieve, breaking the thrashing cycle.
 *
 * Physical RAM: PA 0x400000-0x800000 → PPN 0x400-0x7FF (1024 pages).
 * Bitmaps: 128 bytes each.
 */
#define PMAP_PHYS_BASE_PPN	0x400
#define PMAP_PHYS_NPAGES	1024	/* 4MB / 4KB */

static uint8_t pmap_refbits[PMAP_PHYS_NPAGES / 8];
static uint8_t pmap_modbits[PMAP_PHYS_NPAGES / 8];

static inline void
pmap_set_refbit(paddr_t pa)
{
	unsigned int idx = (pa >> PAGE_SHIFT) - PMAP_PHYS_BASE_PPN;
	if (idx < PMAP_PHYS_NPAGES)
		pmap_refbits[idx >> 3] |= (1 << (idx & 7));
}

static inline bool
pmap_test_refbit(paddr_t pa)
{
	unsigned int idx = (pa >> PAGE_SHIFT) - PMAP_PHYS_BASE_PPN;
	if (idx < PMAP_PHYS_NPAGES)
		return (pmap_refbits[idx >> 3] & (1 << (idx & 7))) != 0;
	return false;
}

static inline bool
pmap_test_and_clear_refbit(paddr_t pa)
{
	unsigned int idx = (pa >> PAGE_SHIFT) - PMAP_PHYS_BASE_PPN;
	if (idx < PMAP_PHYS_NPAGES) {
		uint8_t mask = 1 << (idx & 7);
		bool was = (pmap_refbits[idx >> 3] & mask) != 0;
		pmap_refbits[idx >> 3] &= ~mask;
		return was;
	}
	return false;
}

static inline void
pmap_set_modbit(paddr_t pa)
{
	unsigned int idx = (pa >> PAGE_SHIFT) - PMAP_PHYS_BASE_PPN;
	if (idx < PMAP_PHYS_NPAGES)
		pmap_modbits[idx >> 3] |= (1 << (idx & 7));
}

static inline bool
pmap_test_modbit(paddr_t pa)
{
	unsigned int idx = (pa >> PAGE_SHIFT) - PMAP_PHYS_BASE_PPN;
	if (idx < PMAP_PHYS_NPAGES)
		return (pmap_modbits[idx >> 3] & (1 << (idx & 7))) != 0;
	return false;
}

static inline bool
pmap_test_and_clear_modbit(paddr_t pa)
{
	unsigned int idx = (pa >> PAGE_SHIFT) - PMAP_PHYS_BASE_PPN;
	if (idx < PMAP_PHYS_NPAGES) {
		uint8_t mask = 1 << (idx & 7);
		bool was = (pmap_modbits[idx >> 3] & mask) != 0;
		pmap_modbits[idx >> 3] &= ~mask;
		return was;
	}
	return false;
}

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
 * Per-process user PTE backing store.
 *
 * Each user pmap has a software copy of its user-space PTEs.
 * User VA range: 0x400000-0xDFFFFF → VPN 0x400-0xDFF = 2560 entries.
 * On context switch, pmap_activate() loads the new process's PTEs
 * into the hardware page table window.
 *
 * We repurpose existing struct pmap fields (from pmap_motorola.h):
 *   pm_ptab  → pointer to uint16_t[USER_NPTES] backing store
 *   pm_stab  → next pointer in global pmap list
 */
#define USER_VPN_BASE	0x400
#define USER_VPN_END	0xE00				/* exclusive */
#define USER_NPTES	(USER_VPN_END - USER_VPN_BASE)	/* 2560 */
#define USER_PTE_BYTES	(USER_NPTES * sizeof(uint16_t))	/* 5120 */

#define PM_USER_PTES(pm)   ((uint16_t *)(pm)->pm_ptab)
#define PM_SET_PTES(pm, p) ((pm)->pm_ptab = (pt_entry_t *)(p))
#define PM_NEXT(pm)        ((struct pmap *)(pm)->pm_stab)
#define PM_SET_NEXT(pm, n) ((pm)->pm_stab = (st_entry_t *)(n))

/*
 * Static pool of PTE backing stores.
 * Using static allocation avoids runtime kmem_zalloc which can cause
 * memory pressure during early boot (only ~496 pages of RAM).
 * 4 slots supports up to 4 concurrent user processes.
 */
#define PMAP_MAX_USER	4
static uint16_t pmap_pte_pool[PMAP_MAX_USER][USER_NPTES];
static bool pmap_pte_inuse[PMAP_MAX_USER];

/*
 * Global list of all user pmaps.
 * Used by pmap_page_protect() to find all mappings of a physical page.
 */
static struct pmap *pmap_allpmaps;

/*
 * Currently active user pmap (whose PTEs are loaded in hardware).
 * NULL during early boot before any user process runs.
 */
static struct pmap *pmap_active;

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
 * Create a new pmap.
 * Allocates a per-process user PTE backing store and adds
 * the pmap to the global list for pmap_page_protect() scanning.
 */
pmap_t
pmap_create(void)
{
	struct pmap *pm;
	int i;

	pm = kmem_zalloc(sizeof(*pm), KM_SLEEP);
	if (pm == NULL)
		return NULL;

	/* Allocate a PTE backing store from the static pool */
	for (i = 0; i < PMAP_MAX_USER; i++) {
		if (!pmap_pte_inuse[i]) {
			pmap_pte_inuse[i] = true;
			memset(pmap_pte_pool[i], 0, USER_PTE_BYTES);
			PM_SET_PTES(pm, pmap_pte_pool[i]);
			break;
		}
	}
	if (i == PMAP_MAX_USER) {
		printf("pmap_create: no free PTE slots\n");
		kmem_free(pm, sizeof(*pm));
		return NULL;
	}

	pm->pm_count = 1;

	/* Add to global pmap list */
	PM_SET_NEXT(pm, pmap_allpmaps);
	pmap_allpmaps = pm;

	return pm;
}

/*
 * Destroy a pmap.
 * Frees the per-process PTE backing store and removes from global list.
 */
void
pmap_destroy(pmap_t pm)
{
	struct pmap **pp;

	if (pm == NULL)
		return;

	if (atomic_dec_uint_nv(&pm->pm_count) == 0) {
		/* Remove from global pmap list */
		for (pp = &pmap_allpmaps; *pp != NULL;
		    pp = (struct pmap **)&((*pp)->pm_stab)) {
			if (*pp == pm) {
				*pp = PM_NEXT(pm);
				break;
			}
		}

		/* Clear active pointer if this pmap was active */
		if (pmap_active == pm)
			pmap_active = NULL;

		/* Return PTE backing store to static pool */
		if (PM_USER_PTES(pm) != NULL) {
			int i;
			for (i = 0; i < PMAP_MAX_USER; i++) {
				if (PM_USER_PTES(pm) == pmap_pte_pool[i]) {
					pmap_pte_inuse[i] = false;
					break;
				}
			}
		}

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
 * Enter a mapping.
 * For user VA on a user pmap: write to per-process backing store,
 * and also to hardware if this pmap is currently active.
 * For kernel VA: write directly to hardware.
 */
int
pmap_enter(pmap_t pm, vaddr_t va, paddr_t pa, vm_prot_t prot, u_int flags)
{
	uint16_t pte;
	unsigned int ppn;
	bool user = (va >= VM_MIN_USER_ADDRESS);

	ppn = (pa >> PAGE_SHIFT) & PTE_PPN;
	pte = PTE_M | ppn | pmap_prot_to_pte(prot, user);

	/* Software reference/modify tracking for page daemon */
	pmap_set_refbit(pa);
	if (prot & VM_PROT_WRITE)
		pmap_set_modbit(pa);

	if (user && pm != pmap_kernel() && PM_USER_PTES(pm) != NULL) {
		/* User page: write to per-process backing store */
		unsigned int vpn = (va >> PAGE_SHIFT) & 0xFFF;
		unsigned int idx = vpn - USER_VPN_BASE;
		if (idx < USER_NPTES) {
			PM_USER_PTES(pm)[idx] = pte;
			if (pm == pmap_active)
				pmap_write_pte(va, pte);
		}
	} else {
		/* Kernel VA or no backing store: write to hardware */
		pmap_write_pte(va, pte);
		if (!user && va < VM_MAX_KERNEL_ADDRESS)
			pmap_bitmap_set(va);
	}

	return 0;
}

/*
 * Remove mappings in the given range.
 * For user VA on a user pmap: clear per-process backing store,
 * and also hardware if this pmap is currently active.
 * For kernel VA: clear hardware directly (needed by UBC).
 */
void
pmap_remove(pmap_t pm, vaddr_t sva, vaddr_t eva)
{
	vaddr_t va;

	for (va = sva; va < eva; va += PAGE_SIZE) {
		if (va >= VM_MIN_USER_ADDRESS &&
		    pm != pmap_kernel() && PM_USER_PTES(pm) != NULL) {
			unsigned int vpn = (va >> PAGE_SHIFT) & 0xFFF;
			unsigned int idx = vpn - USER_VPN_BASE;
			if (idx < USER_NPTES) {
				PM_USER_PTES(pm)[idx] = PTE_INVALID;
				if (pm == pmap_active)
					pmap_write_pte(va, PTE_INVALID);
			}
		} else {
			pmap_write_pte(va, PTE_INVALID);
			if (va < VM_MAX_KERNEL_ADDRESS)
				pmap_bitmap_clear(va);
		}
	}
}

/*
 * Extract physical address.
 * For kernel VA, use the bitmap + hardware PTE.
 * For user VA, read from the per-process backing store so that
 * we return the correct mapping even for non-active pmaps.
 */
bool
pmap_extract(pmap_t pm, vaddr_t va, paddr_t *pap)
{
	uint16_t pte;

	if (pm == pmap_kernel()) {
		/*
		 * Kernel VA: use bitmap to distinguish pages explicitly
		 * mapped via pmap_kenter_pa from the bootloader's static
		 * mapping. UVM needs pmap_extract to return false for
		 * unallocated KVA.
		 */
		if (!pmap_bitmap_test(va))
			return false;
		pte = pmap_read_pte(va);
		if (!(pte & PTE_M))
			return false;
		if (pap != NULL)
			*pap = ((paddr_t)(pte & PTE_PPN) << PAGE_SHIFT) |
			    (va & PAGE_MASK);
		return true;
	}

	/* User VA: read hardware if active, backing store otherwise */
	if (va >= VM_MIN_USER_ADDRESS) {
		if (pm == pmap_active) {
			/* Active pmap: read hardware directly */
			pte = pmap_read_pte(va);
		} else if (PM_USER_PTES(pm) != NULL) {
			unsigned int vpn = (va >> PAGE_SHIFT) & 0xFFF;
			unsigned int idx = vpn - USER_VPN_BASE;
			if (idx >= USER_NPTES)
				return false;
			pte = PM_USER_PTES(pm)[idx];
		} else {
			return false;
		}
		if (!(pte & PTE_M))
			return false;
		if (pap != NULL)
			*pap = ((paddr_t)(pte & PTE_PPN) <<
			    PAGE_SHIFT) | (va & PAGE_MASK);
		return true;
	}

	return false;
}

/*
 * Protect a range.
 * For user VA on a user pmap: update per-process backing store,
 * and also hardware if active.  This is called during fork to
 * remove write permission for COW.
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

		if (pm != pmap_kernel() && PM_USER_PTES(pm) != NULL) {
			unsigned int vpn = (va >> PAGE_SHIFT) & 0xFFF;
			unsigned int idx = vpn - USER_VPN_BASE;
			if (idx >= USER_NPTES)
				continue;
			pte = PM_USER_PTES(pm)[idx];
			if (!(pte & PTE_M))
				continue;
			pte = PTE_M | (pte & PTE_PPN) |
			    pmap_prot_to_pte(prot, true);
			PM_USER_PTES(pm)[idx] = pte;
			if (pm == pmap_active)
				pmap_write_pte(va, pte);
		} else {
			pte = pmap_read_pte(va);
			if (!(pte & PTE_M))
				continue;
			pte = PTE_M | (pte & PTE_PPN) |
			    pmap_prot_to_pte(prot, true);
			pmap_write_pte(va, pte);
		}
	}
}

/*
 * Activate a pmap (switch to it).
 * Called from mi_switch() and lwp_startup() during context switch.
 * Loads the new process's user PTEs into the hardware page table window.
 */
void
pmap_activate(struct lwp *l)
{
	struct pmap *pm = l->l_proc->p_vmspace->vm_map.pmap;
	volatile uint16_t *hw = (volatile uint16_t *)MMU_PAGETABLE_WIN;
	int i;

	/* If same pmap is already active, nothing to do */
	if (pm == pmap_active)
		return;

	if (pm == pmap_kernel() || PM_USER_PTES(pm) == NULL) {
		/* Kernel thread: clear all user PTEs in hardware */
		for (i = USER_VPN_BASE; i < USER_VPN_END; i++)
			hw[i] = PTE_INVALID;
	} else {
		/* User process: load its user PTEs into hardware */
		uint16_t *ptes = PM_USER_PTES(pm);
		for (i = 0; i < USER_NPTES; i++)
			hw[USER_VPN_BASE + i] = ptes[i];
	}

	pmap_active = pm;
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
 * Clear modify bit — return old value.
 * Called by page daemon before pageout to check if writeback is needed.
 */
bool
pmap_clear_modify(struct vm_page *pg)
{
	return pmap_test_and_clear_modbit(VM_PAGE_TO_PHYS(pg));
}

/*
 * Clear reference bit — return old value.
 * Called by page daemon clock algorithm.  If true, page gets another
 * chance (moved back to active queue).  If false, page is an eviction
 * candidate.  Without this, ALL pages appear unreferenced and the
 * page daemon evicts everything on sight, causing thrashing.
 */
bool
pmap_clear_reference(struct vm_page *pg)
{
	return pmap_test_and_clear_refbit(VM_PAGE_TO_PHYS(pg));
}

/*
 * Is page modified?
 */
bool
pmap_is_modified(struct vm_page *pg)
{
	return pmap_test_modbit(VM_PAGE_TO_PHYS(pg));
}

/*
 * Is page referenced?
 * The page daemon uses this to decide whether to rescue a page from
 * the inactive queue.  Returning false for all pages (the old stub)
 * meant no page was ever rescued → immediate eviction → thrashing.
 */
bool
pmap_is_referenced(struct vm_page *pg)
{
	return pmap_test_refbit(VM_PAGE_TO_PHYS(pg));
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
 * Protect a physical page — change or remove all mappings of a given
 * physical page across ALL user pmaps.
 *
 * Called by UVM for:
 *   VM_PROT_READ — downgrade all mappings to read-only (for COW setup)
 *   VM_PROT_NONE — remove all mappings (before freeing the page)
 *
 * Scans all user pmaps' backing stores for entries mapping this PPN.
 * Always reads from the backing store (contiguous RAM array) rather
 * than the hardware PTE window — the backing store is kept in sync by
 * pmap_enter/pmap_remove/pmap_protect, and scanning RAM is much faster
 * than 2560 volatile device reads through the MMU window.  Hardware is
 * only touched on match (0-1 writes per call) for the active pmap.
 */
void
pmap_page_protect(struct vm_page *pg, vm_prot_t prot)
{
	paddr_t pa = VM_PAGE_TO_PHYS(pg);
	uint16_t ppn = (pa >> PAGE_SHIFT) & PTE_PPN;
	struct pmap *pm;
	uint16_t *ptes;
	int i;

	/* If full access requested, nothing to restrict */
	if (prot & VM_PROT_WRITE)
		return;

	/* Scan all user pmaps' backing stores */
	for (pm = pmap_allpmaps; pm != NULL; pm = PM_NEXT(pm)) {
		ptes = PM_USER_PTES(pm);
		if (ptes == NULL)
			continue;
		for (i = 0; i < USER_NPTES; i++) {
			if ((ptes[i] & (PTE_M | PTE_PPN)) !=
			    (PTE_M | ppn))
				continue;
			if (prot == VM_PROT_NONE)
				ptes[i] = PTE_INVALID;
			else
				ptes[i] &= ~PTE_RW;
			/* Sync hardware if this is the active pmap */
			if (pm == pmap_active)
				pmap_write_pte(
				    (vaddr_t)(USER_VPN_BASE + i)
					<< PAGE_SHIFT,
				    ptes[i]);
		}
	}
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
 * Copy page mappings from one pmap to another.
 * No-op: UVM handles fork() via COW with pmap_protect() to downgrade
 * parent pages to read-only, and the child faults in pages on demand.
 */
void
pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, vaddr_t dst_addr, vsize_t len, vaddr_t src_addr)
{
	/* Intentionally empty — COW handles this via fault-on-demand */
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

