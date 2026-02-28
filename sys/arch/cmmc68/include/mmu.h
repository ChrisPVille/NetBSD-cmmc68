/*$NetBSD: mmu.h,v 1.2 2024/01/01 00:00:00 cmmc68 Exp $ */

/*
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Custom MMU definitions for CMMC68 (fully custom design)
 */

#ifndef _CMMC68_MMU_H_
#define _CMMC68_MMU_H_

/*
 * MMU Control Register address (byte at low byte of word, big-endian)
 */
#define	MMU_CTRL_REG		0xFD0001
#define	MMU_CTRL_REG_SIZE	8

/*
 * MMU Control Register bits
 */
#define	MMU_CTRL_ENABLE		0x80	/* Bit 7: MMU enable */
#define	MMU_CTRL_CTX_MASK	0x7F	/* Bits 0-6: Context ID */

/*
 * MMU Page Table Window address
 */
#define	MMU_PAGETABLE_WIN	0xFD2000
#define	MMU_WINDOW_SIZE		0x2000	/* 8KB = 4096 entries × 2 bytes */

/*
 * Page size
 */
#define	MMU_PAGESIZE		4096
#define	MMU_PAGESHIFT		12
#define	MMU_PAGEOFFSET		(MMU_PAGESIZE - 1)
#define	MMU_PFN_MASK		(~MMU_PAGEOFFSET)

/*
 * Page Table Entry (PTE) format - 16 bits
 * Bit 15: Execute (EX) - 1 = allowed
 * Bit 14: Read-Write (RW) - 1 = read-write
 * Bit 13: User (U) - 1 = user mode allowed
 * Bit 12: Mapped (M) - 1 = mapped
 * Bits 11-0: Physical Page Number (PPN)
 */
#define	PTE_EX		0x8000	/* Execute allowed */
#define	PTE_RW		0x4000	/* Read-write */
#define	PTE_U		0x2000	/* User accessible */
#define	PTE_M		0x1000	/* Mapped */
#define	PTE_PPN		0x0FFF	/* Physical page number mask */
#define	PTE_PPN_SHIFT	12

/* PTE protection combinations */
#define	PTE_PROT_MASK	(PTE_EX | PTE_RW | PTE_U)
#define	PTE_INVALID	0x0000

/*
 * Number of contexts (address spaces)
 */
#define	MMU_NCONTEXTS	128

#endif /* _CMMC68_MMU_H_ */
