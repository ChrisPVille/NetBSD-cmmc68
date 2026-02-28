/*	$NetBSD: pmap.h,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $	*/

#ifndef _CMMC68_PMAP_H_
#define _CMMC68_PMAP_H_

#include <machine/mmu.h>
#include <machine/vmparam.h>

/*
 * Use the common m68k pmap implementation.
 * The CMMC68 has a custom MMU, but we'll start with the Motorola pmap
 * and adapt as needed.
 */
#include <m68k/pmap_motorola.h>

#endif /* _CMMC68_PMAP_H_ */
