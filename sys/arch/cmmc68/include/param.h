/*	$NetBSD: param.h,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $	*/

/*
 * Machine dependent constants for CMMC68 (MC68010-based system).
 * Based on HP300 implementation.
 */

#ifndef	_CMMC68_PARAM_H_
#define	_CMMC68_PARAM_H_

/*
 * Machine identification
 */
#define	_MACHINE	cmmc68
#define	MACHINE		"cmmc68"
#define	_MACHINE_ARCH	m68000
#define	MACHINE_ARCH	"m68000"

/*
 * Page shift and kernel base must be defined before including m68k/param.h
 */
#define	PGSHIFT		12		/* LOG2(NBPG) */
#define	KERNBASE	0x00C00000	/* start of kernel virtual */

#define	UPAGES		2		/* pages of u-area */

#include <m68k/param.h>

/*
 * Page size definitions for UVM (compile-time constant)
 */
#define	PAGE_SIZE	NBPG
#define	PAGE_SHIFT	PGSHIFT
#define	PAGE_MASK	(PAGE_SIZE - 1)

/*
 * CPU model - MC68010
 */
#define	M68010
#define	M68K_MMU_CMMC68

/*
 * Number of pages - 8MB physical memory
 */
#define	LOWPAGES	0x800		/* 8MB / 4KB = 2048 pages */

#define	NPTEPG		(NBPG/(sizeof (pt_entry_t)))

/*
 * Concurrent exec limit.
 * exec_map size = MAXEXEC * NCARGS (256KB). Default MAXEXEC=16 → 4MB, which
 * exceeds our ~1.7MB free KVA.  Keep at 1 for 256KB exec_map.
 */
#define	MAXEXEC		1

/*
 * Minimum and maximum sizes of the kernel malloc arena in PAGE_SIZE-sized
 * logical pages.
 * CMMC68 kernel VA: 0xC00000-0xEFFFFF (3MB).
 * Kernel image is ~1.3MB, virtual_avail ≈ 0xD4xxxx,
 * leaving ~1.7MB free KVA. vmem qcache is disabled for arenas < 2MB
 * (see uvm_km.c) to avoid 64KB pool page allocations.
 */
#define	NKMEMPAGES_MIN_DEFAULT	((32 * 1024) >> PAGE_SHIFT)   /* 32KB min */
#define	NKMEMPAGES_MAX_DEFAULT	((512 * 1024) >> PAGE_SHIFT)  /* 512KB max */

/*
 * Unified Buffer Cache (UBC) configuration.
 * With 1.7MB free KVA, we can afford 16 windows (128KB).
 */
#define	UBC_NWINS	8

#if defined(_KERNEL) && !defined(_LOCORE)
#include <machine/intr.h>

#define	delay(us)	_delay(us)
#define DELAY(us)	delay(us)

void	_delay(u_int);
#endif /* _KERNEL && !_LOCORE */

#endif	/* !_CMMC68_PARAM_H_ */
