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
 *
 * VA 0x000000-0x3FFFFF  Kernel (4MB, mapped to PA 0x400000 by bootloader)
 * VA 0x400000-0xDFFFFF  User space (10MB, per-process dynamic mapping)
 * VA 0xFD0000-0xFD3FFF  MMU registers (supervisor-only, bypasses translation)
 * VA 0xFE0000-0xFFFFFF  ROM/IO/MFP (identity mapped)
 */

/*
 * User and kernel address space boundaries
 */
#define	VM_MIN_USER_ADDRESS	((vaddr_t)0x00400000)
#define	VM_MAX_USER_ADDRESS	((vaddr_t)0x00E00000)
#define	VM_MAXUSER_ADDRESS	((vaddr_t)0x00E00000)
#define	VM_MIN_ADDRESS		((vaddr_t)0x00000000)

#define	VM_MIN_KERNEL_ADDRESS	((vaddr_t)0x00000000)
#define	VM_MAX_KERNEL_ADDRESS	((vaddr_t)0x003FFFFF)

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
 */
#define	USRSTACK		0x00E00000	/* Top of user stack */
#define	MAXSSIZ			(2*1024*1024)	/* Max stack size (2MB) */
#define	DFLSSIZ			(2*1024*1024)	/* Default stack size (2MB) */
#define	MAXDSIZ			(4*1024*1024)	/* Max data segment size (4MB) */
#define	DFLDSIZ			(1*1024*1024)	/* Default data segment size (1MB) */

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
 * Kernel PA offset: the physical address that VA 0x0 maps to.
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
