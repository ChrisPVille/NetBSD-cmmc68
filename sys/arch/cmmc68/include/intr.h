/*	$NetBSD: intr.h,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $	*/

/*-
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _CMMC68_INTR_H_
#define _CMMC68_INTR_H_

#ifdef _KERNEL

#include <m68k/psl.h>

#define	MACHINE_PSL_IPL_SOFTCLOCK	PSL_IPL1
#define	MACHINE_PSL_IPL_SOFTBIO		PSL_IPL1
#define	MACHINE_PSL_IPL_SOFTNET		PSL_IPL1
#define	MACHINE_PSL_IPL_SOFTSERIAL	PSL_IPL1
#define	MACHINE_PSL_IPL_VM		PSL_IPL5
#define	MACHINE_PSL_IPL_SCHED		PSL_IPL6

#define	MACHINE_INTREVCNT_NAMES						\
	{ "spurious", "lev1", "lev2", "lev3", "lev4", "lev5", "clock", "nmi" }

#endif /* _KERNEL */

#include <m68k/intr.h>

#ifdef _KERNEL

/* These spl calls are _not_ to be used by machine-independent code. */
#define	splhil()	splraise1()
#define	splkbd()	splhil()

/*
 * Machine-dependent interrupt functions
 */

struct clockframe;

void	intr_init(void);
void	intr_dispatch(struct clockframe *);
void	intr_enable_level(int);
void	intr_disable_level(int);
int	intr_save(void);
void	intr_restore(int);
void	intr_block_all(void);
void	intr_unblock(void);
void	intr_count(int);
void	intr_print_stats(void);

/*
 * Interface wrappers.
 */

static inline void *
intr_establish(int (*func)(void *), void *arg, int ipl, int isrpri)
{
	return m68k_intr_establish(func, arg, (void *)0, 0, ipl, isrpri, 0);
}

static inline void
intr_disestablish(void *ih)
{
	m68k_intr_disestablish(ih);
}

#endif /* _KERNEL */

#endif	/* _CMMC68_INTR_H */
