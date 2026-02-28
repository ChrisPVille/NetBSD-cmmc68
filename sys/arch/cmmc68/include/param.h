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
#define	KERNBASE	0x00000000	/* start of kernel virtual */

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
 * Number of pages - 4MB physical memory
 */
#define	LOWPAGES	0x400		/* 4MB / 4KB = 1024 pages */

#define	NPTEPG		(NBPG/(sizeof (pt_entry_t)))

/*
 * Concurrent exec limit - minimize exec_map size for small VA space.
 * exec_map size = MAXEXEC * NCARGS = MAXEXEC * 256KB
 * For 4MB VA space, keep this small (1 = 256KB)
 */
#define	MAXEXEC		1

/*
 * Minimum and maximum sizes of the kernel malloc arena in PAGE_SIZE-sized
 * logical pages.
 * CMMC68 has limited VA space (4MB total).
 * Kernel image is ~2.35MB (kvm_start=0x23C000), leaving ~1.77MB for
 * kmem + submaps. Budget:
 *   kmem_va_arena: 512KB (128 pages) - handles pool pages + large allocs
 *   pager_map:     256KB
 *   exec_map:      256KB (MAXEXEC=1 * NCARGS=256KB)
 *   UBC (ubc_init): 32 wins * 8KB = 256KB
 *   other maps:    ~250KB remaining
 * Physical pages are allocated on demand, so kmem VA size doesn't
 * pre-consume physical RAM.
 */
#define	NKMEMPAGES_MIN_DEFAULT	((32 * 1024) >> PAGE_SHIFT)   /* 32KB min */
#define	NKMEMPAGES_MAX_DEFAULT	((512 * 1024) >> PAGE_SHIFT)  /* 512KB max */

/*
 * Unified Buffer Cache (UBC) configuration.
 * Default UBC_NWINS=1024 with 8KB windows = 8MB of kernel VA - way too
 * much for our 4MB KVA space.  Use 8 windows to conserve KVA.
 * 8 * 8KB = 64KB of KVA for UBC.
 */
#define	UBC_NWINS	8

#if defined(_KERNEL) && !defined(_LOCORE)
#include <machine/intr.h>

#define	delay(us)	_delay(us)
#define DELAY(us)	delay(us)

void	_delay(u_int);
#endif /* _KERNEL && !_LOCORE */

#endif	/* !_CMMC68_PARAM_H_ */
