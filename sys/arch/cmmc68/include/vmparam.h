/*	$NetBSD: vmparam.h,v 1.3 2024/01/01 00:00:00 cmmc68 Exp $ */

/*
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 */

#ifndef _CMMC68_VMPARAM_H_
#define _CMMC68_VMPARAM_H_

/*
 * CMMC68 Virtual Address Space Layout (MC68010, 24-bit, custom MMU)
 * 12:3:1 split — User : Kernel : I/O
 *
 * VA 0x000000-0xBFFFFF  User space (12MB, per-process dynamic mapping)
 * VA 0xC00000-0xEFFFFF  Kernel (3MB text/data/bss + free KVA)
 * VA 0xF00000-0xFFFFFF  I/O hardware (1MB, identity mapped, supervisor-only)
 *
 * Ramdisk is mapped by bootloader at VA 0x800000 (supervisor-only,
 * no PTE_U) and accessed by md(4) through that pointer.
 */

/*
 * User and kernel address space boundaries
 */
#define	VM_MIN_USER_ADDRESS	((vaddr_t)0x00000000)
#define	VM_MAX_USER_ADDRESS	((vaddr_t)0x00C00000)
#define	VM_MAXUSER_ADDRESS	((vaddr_t)0x00C00000)
#define	VM_MIN_ADDRESS		((vaddr_t)0x00000000)

#define	VM_MIN_KERNEL_ADDRESS	((vaddr_t)0x00C00000)
#define	VM_MAX_KERNEL_ADDRESS	((vaddr_t)0x00F00000)

/*
 * MMU and device address space (identity mapped, supervisor-only)
 */
#define	MMU_CTRL_PHYS		((paddr_t)0x00FD0000)
#define	MMU_CTRL_REGION_SIZE	0x2000		/* 8 KB */
#define	MMU_PAGETABLE_PHYS	((paddr_t)0x00FD2000)
#define	MMU_PAGETABLE_REGION_SIZE	0x2000	/* 8 KB */
#define	MMU_CTRL_VIRT		((vaddr_t)0x00FD0000)
#define	MMU_PAGETABLE_VIRT	((vaddr_t)0x00FD2000)

#define	VM_MIN_MMU_ADDRESS	((vaddr_t)0x00FD0000)
#define	VM_MAX_MMU_ADDRESS	((vaddr_t)0x00FD3FFF)
#define	VM_MIN_DEVICE_ADDRESS	((vaddr_t)0x00FD0000)
#define	VM_MAX_ADDRESS		((vaddr_t)0x00FFFFFF)

/*
 * User address space layout
 * USRSTACK at top of user VA (0xC00000), stack grows down.
 * Programs link at VA 0x1000 (page 1); page 0 is null guard.
 * Low placement enables m68k short addressing modes.
 *
 * Data (heap) limits set to full user VA — brk() grows freely
 * until it collides with the stack region in uvm_map().
 *
 * Stack limits must be explicit because exec reserves the full
 * MAXSSIZ as address space upfront (PROT_NONE for demand-fault
 * growth).  USRSTACK - MAXSSIZ must stay above the highest
 * plausible program end.
 */
#define	USRSTACK		0x00C00000	/* Top of user stack */
#define	MAXSSIZ			(2*1024*1024)	/* 2 MB — noaccess base at 0xA00000 */
#define	DFLSSIZ			(512*1024)	/* 512 KB initial accessible stack */
#define	MAXDSIZ			VM_MAXUSER_ADDRESS  /* uncapped — uvm_map enforces */
#define	DFLDSIZ			VM_MAXUSER_ADDRESS  /* uncapped — uvm_map enforces */

/*
 * mmap hint address override.
 *
 * The default VM_DEFAULT_ADDRESS_BOTTOMUP computes:
 *   round_page(data_addr + maxdmap)
 * On a 24-bit address space (12 MB user VA), maxdmap = MAXDSIZ can push
 * the hint past VM_MAXUSER_ADDRESS, causing mmap(0,...) to fail with
 * ENOMEM.  Override to hint right after the data segment and let the
 * uvm allocator find free space.
 */
#define	VM_DEFAULT_ADDRESS_BOTTOMUP(da, sz) \
	round_page((vaddr_t)(da))

/*
 * Kernel map sizing
 */
#define	VM_PHYSSEG_MAX		16
#define	VM_PHYSSEG_STRAT	VM_PSTRAT_BSEARCH
#define	VM_NFREEPOOL		1
#define	VM_NFREELIST		1
#define	VM_FREELIST_DEFAULT	0

/*
 * UVM parameters
 */
#define	USRIOSIZE		512
#define	VM_MMAP_SIZE		64
#define	VM_MAXGUEST_KMEM	((vsize_t)0)

/*
 * Address translation macros
 */
#define	VA_TO_VPN(va)		(((va) >> PGSHIFT) & 0xFFF)
#define	PA_TO_PPN(pa)		(((pa) >> PGSHIFT) & 0xFFF)
#define	VPN_TO_PA(vpn)		((vpn) << PGSHIFT)
#define	PTE_TO_PA(pte)		(((pte) & PTE_PPN) << PGSHIFT)

/*
 * Kernel map
 */
#define	VM_MAP_SIZE(vmsize)	round_page((vmsize) / 4)

/*
 * Kernel PA offset: the physical address that VA KERNBASE maps to.
 *   PA = (VA - KERNBASE) + kernel_pa_offset
 *   VA = (PA - kernel_pa_offset) + KERNBASE
 * Auto-detected from MMU hardware during bootstrap.
 */
#ifdef _KERNEL
extern paddr_t kernel_pa_offset;
#endif

/*
 * Page size range for userspace (jemalloc).
 * CMMC68 always uses 4KB pages.
 */
#if !defined(_KERNEL)
#define	MIN_PAGE_SHIFT	12
#define	MAX_PAGE_SHIFT	12
#define	MIN_PAGE_SIZE	(1 << MIN_PAGE_SHIFT)
#define	MAX_PAGE_SIZE	(1 << MAX_PAGE_SHIFT)
#endif /* !_KERNEL */

#endif /* _CMMC68_VMPARAM_H_ */
