/*	$NetBSD: types.h,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $	*/

#ifndef _CMMC68_TYPES_H_
#define _CMMC68_TYPES_H_

#include <m68k/types.h>

#define __HAVE_DEVICE_REGISTER
#define __HAVE_MM_MD_KERNACC

/*
 * MC68010-based machines don't do indivisible R-M-W cycles
 * (used by CAS, CAS2, and TAS) correctly, so we need to avoid them.
 */
#define __HAVE_M68K_BROKEN_RMC		1

#endif /* !_CMMC68_TYPES_H_ */
