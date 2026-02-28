/*	$NetBSD: clock.c,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $ */

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
 * Clock/timer driver for CMMC68 - minimal version
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/time.h>
#include <sys/device.h>

#include <m68k/m68k.h>
#include <m68k/frame.h>
#include <machine/param.h>
#include <machine/vectors.h>
#include <machine/mfpreg.h>

/* Forward declarations */
int clock_intr(void *);
uint32_t getclocktick(void);
void clock_handler(struct clockframe *frame);

/*
 * Clock interrupt frequency
 */
#define	CLOCK_HZ	100		/* 100 Hz */
#define	CLOCK_TICK	(1000000 / CLOCK_HZ)

/*
 * Clock state
 */
static volatile uint32_t clock_count;

/*
 * Initialize clock
 */
void
cpu_initclocks(void)
{
	volatile uint8_t *mfp = MFP_REGS;
	extern char intrhand_autovec[];
	int i;

	/* Set MFP vector base to 0x40, S bit = 0 (auto end-of-interrupt) */
	mfp[MFP_VR] = 0x40;

	/* Install MFP vectored interrupt handler for all 16 MFP vectors.
	 * MFP VR base = 0x40, so vectors 0x40-0x4F (64-79).
	 * Timer A is channel 13 = vector 0x4D (77). */
	for (i = 0; i < 16; i++)
		vec_set_entry(0x40 + i, intrhand_autovec);

	/* Enable and unmask Timer A interrupt (bit 5 in IERA/IMRA) */
	mfp[MFP_IERA] |= 0x20;	/* Enable Timer A interrupt */
	mfp[MFP_IMRA] |= 0x20;	/* Unmask Timer A interrupt */

	/* Configure MFP Timer A for 100 Hz */
	mfp[MFP_TADR] = 200;		/* Timer countdown value */
	mfp[MFP_TACR] = TIMER_DELAY_200;	/* Start timer, prescaler /200 */

	clock_count = 0;

	printf("Clock initialized: %d Hz\n", CLOCK_HZ);
}

/*
 * Clock interrupt handler
 */
int
clock_intr(void *arg)
{
	clock_count++;

	/* Call hardclock */
	hardclock((struct clockframe *)arg);

	return 1;
}

/*
 * Clock handler (called from assembly)
 */
void
clock_handler(struct clockframe *frame)
{
	clock_intr(frame);
}

/*
 * Start clock
 */
void
setstatclockrate(int newhz)
{
	/* Nothing to do for now */
	(void)newhz;
}

/*
 * Delay for n microseconds
 */
void
_delay(u_int n)
{
	volatile uint32_t count;

	/* Simple delay loop calibrated for ~1us per iteration */
	/* This needs to be calibrated based on CPU speed */
	while (n--) {
		count = 10;
		while (count--) {
			__asm volatile("nop");
		}
	}
}

/*
 * Get current time tick
 */
uint32_t
getclocktick(void)
{
	return clock_count;
}

/*
 * Clock config attachment declaration
 */
CFATTACH_DECL_NEW(clock, 0,
    NULL, NULL, NULL, NULL);
