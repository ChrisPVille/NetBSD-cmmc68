/*$NetBSD: intr.c,v 1.2 2024/01/01 00:00:00 cmmc68 Exp $ */

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

/*
 * Interrupt management for CMMC68
 * Platform-specific initialization and control
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/intr.h>
#include <sys/cpu.h>
#include <sys/kernel.h>

#include <m68k/m68k.h>
#include <m68k/frame.h>
#include <machine/param.h>
#include <machine/intr.h>
#include <machine/cpu.h>
#include <machine/mfpreg.h>
#include <machine/pitreg.h>
#include <machine/duartreg.h>

/*
 * CPU info structure for the primary CPU
 * Required by the scheduler and other kernel subsystems
 * Declaration is in m68k/cpu.h, we just provide the storage
 */
extern struct cpu_info cpu_info_store;

/*
 * AST pending flag
 * Set when a context switch or signal delivery is needed
 * Definition is in m68k/m68k/m68k_trap.c, we just use extern here
 */
extern volatile int astpending;

/*
 * Initialize interrupt system
 * Called from machdep.c during boot
 */
void
intr_init(void)
{
	/* Enable interrupts in CPU */
	/* Interrupts are enabled by clearing the interrupt mask in SR */
	__asm__ volatile("andw #0xF8FF,%%sr" ::: "cc");  /* Clear IPL bits */
}

/*
 * Dispatch interrupt
 * Called from intrhand_autovec in locore.s with clockframe pointer.
 *
 * The clockframe contains saved d0/d1/a0/a1, then the MC68010
 * hardware exception frame: SR, PC, and format/vector word.
 * We extract the vector number from cf_vo to dispatch to the
 * correct handler.
 *
 * Vector assignments:
 *   0x40-0x4F  MFP vectors (Timer A = 0x4D)
 *   0x50       PIT timer
 *   0x51       DUART
 */
void
intr_dispatch(struct clockframe *cf)
{
	extern int clock_intr(void *);
	extern void pit_timer_intr(struct clockframe *);
	extern void duart_intr(void);
	extern void mfp_rcv_intr(void);

	int vecnum = (cf->cf_vo & 0x0FFF) >> 2;

	if (vecnum == (PIT_TIMER_VEC)) {
		pit_timer_intr(cf);
	} else if (vecnum == (DUART_VEC)) {
		duart_intr();
	} else if (vecnum == 0x4C) {
		/* MFP vector 0x4C: Receive Buffer Full (channel 12) */
		mfp_rcv_intr();
	} else if (vecnum >= 0x40 && vecnum <= 0x4F) {
		/* Other MFP vectors (timer, transmit, etc.) */
		clock_intr(cf);
	}
}

/*
 * Enable interrupt at given level in interrupt controller
 */
void
intr_enable_level(int level)
{
	/* TODO: Write to interrupt controller enable register */
}

/*
 * Disable interrupt at given level in interrupt controller
 */
void
intr_disable_level(int level)
{
	/* TODO: Write to interrupt controller disable register */
}

/*
 * Save current interrupt state
 */
int
intr_save(void)
{
	int ipl;

	/* Read current IPL from SR */
	__asm__ volatile("movew %%sr,%0" : "=d"(ipl));
	ipl = (ipl >> 8) & 7;

	return ipl;
}

/*
 * Restore interrupt state
 */
void
intr_restore(int ipl)
{
	/* Set IPL in SR */
	__asm__ volatile("movew %0,%%sr" :: "d"(ipl << 8));
}

/*
 * Block all interrupts
 */
void
intr_block_all(void)
{
	/* Set IPL to 7 (highest) */
	__asm__ volatile("oriw #0x0700,%%sr" ::: "cc");
}

/*
 * Unblock all interrupts
 */
void
intr_unblock(void)
{
	/* Set IPL to 0 (lowest) */
	__asm__ volatile("andw #0xF8FF,%%sr" ::: "cc");
}

/*
 * Count interrupt at given level (for statistics)
 */
void
intr_count(int level)
{
	/* TODO: Increment interrupt counter for this level */
}

/*
 * Print interrupt statistics
 */
void
intr_print_stats(void)
{
}
