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
 * Clock/timer driver for CMMC68.
 *
 * The system clock is driven by the MC68230 PIT timer at 100 Hz.
 * MFP Timer A is kept as a fallback for MFP-vectored interrupts.
 * cpu_initclocks() sets up MFP vectors and then starts the PIT.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/time.h>
#include <sys/device.h>
#include <sys/cpu.h>
#include <sys/rndsource.h>

#include <m68k/m68k.h>
#include <m68k/frame.h>
#include <machine/param.h>
#include <machine/vectors.h>
#include <machine/mfpreg.h>

/* Forward declarations */
int clock_intr(void *);
uint32_t getclocktick(void);
void clock_handler(struct clockframe *frame);
void mfp_timer_a_start(void);
extern void pit_timer_start(void);
extern int pit_present;

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
 * Start MFP Timer A as fallback system clock (~100 Hz).
 * Used when the PIT expansion card is not present.
 * Timer A has its own control register (TACR) so it doesn't
 * interfere with Timer C/D which are used for UART baud rate.
 *
 * This is a temporary clock source until PIT hardware is available.
 */
void
mfp_timer_a_start(void)
{
	volatile uint8_t *mfp = MFP_REGS;

	/* Stop timer before reconfiguring */
	mfp[MFP_TACR] = TIMER_STOPPED;

	/* Load Timer A data register */
	mfp[MFP_TADR] = MFP_TIMER_COUNT;

	/* Start Timer A in delay mode with /200 prescaler */
	mfp[MFP_TACR] = TIMER_DELAY_200;

	/* Enable Timer A interrupt (IERA bit 5 = channel 13) */
	mfp[MFP_IERA] |= MFP_IERA_TIMER_A;
	mfp[MFP_IMRA] |= MFP_IERA_TIMER_A;
}

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
	 * These are needed for MFP serial receive interrupts. */
	for (i = 0; i < 16; i++)
		vec_set_entry(0x40 + i, intrhand_autovec);

	/* Enable MFP serial receive interrupt.
	 * Must be done AFTER vectors are installed above.
	 * MC68901 IERA bit 4 = Receive Buffer Full (channel 12, vector 0x4C).
	 * RSR bit 0 = Receiver Enable, TSR bit 0 = Transmitter Enable.
	 * IMRA/IMRB = 0xFF to unmask all MFP interrupt sources. */
	mfp[MFP_IMRA] = 0xFF;
	mfp[MFP_IMRB] = 0xFF;
	mfp[MFP_RSR] = 0x01;		/* Enable receiver */
	mfp[MFP_TSR] = 0x01;		/* Enable transmitter */
	if (mfp[MFP_RSR] & RSR_CHAR_AVAILABLE) {
		/* Save stale character to mfpcon ring buffer instead
		 * of discarding.  It will be delivered to the tty
		 * when mfpconopen() drains the ring buffer. */
		extern uint8_t mfpcon_rbuf[];
		extern volatile u_int mfpcon_rbput;
		u_int put = mfpcon_rbput;
		mfpcon_rbuf[put & 63] = mfp[MFP_UDR];
		mfpcon_rbput = put + 1;
	}
	mfp[MFP_IERA] = 0x10;		/* Rcv Buffer Full interrupt */

	clock_count = 0;

	/* Start the system clock timer */
	if (pit_present) {
		pit_timer_start();
		printf("Clock initialized: 100 Hz (PIT)\n");
	} else {
		/* Fallback: MFP Timer A as system clock */
		mfp_timer_a_start();
		printf("Clock initialized: ~100 Hz (MFP Timer A)\n");
	}

	/*
	 * Seed the entropy pool.
	 *
	 * CMMC68 has no hardware RNG, no persistent storage for saved
	 * entropy, and the clockinterrupt timecounter produces perfectly
	 * predictable deltas (constant 10ms ticks), so rnd_delta_estimate()
	 * never credits any entropy samples.  Without seeding, the first
	 * getrandom() call blocks indefinitely waiting for entropy.
	 *
	 * Provide a boot-time seed from available system state.  This is
	 * NOT cryptographically strong entropy, but it's the best we can
	 * do on an embedded system with no HWRNG.  Still while cold, so
	 * entropy_enter_early() credits bits directly to E->bitsneeded.
	 */
	{
		static struct krndsource boot_rndsource;
		uint32_t seed[8]; /* 32 bytes = 256 bits */
		extern paddr_t kernel_pa_offset;

		seed[0] = (uint32_t)&seed;		/* stack address */
		seed[1] = (uint32_t)kernel_pa_offset;
		seed[2] = physmem;
		seed[3] = clock_count;
		seed[4] = (uint32_t)mfp[MFP_RSR];	/* MFP state */
		seed[5] = (uint32_t)curcpu();
		seed[6] = (uint32_t)&boot_rndsource;	/* BSS address */
		seed[7] = 0xCCCC6810;			/* platform tag */

		rnd_attach_source(&boot_rndsource, "cmmc68boot",
		    RND_TYPE_UNKNOWN, RND_FLAG_COLLECT_VALUE);
		rnd_add_data(&boot_rndsource, seed, sizeof(seed), 256);
	}

}

/*
 * Clock interrupt handler
 */
int
clock_intr(void *arg)
{
	struct clockframe *cf = (struct clockframe *)arg;

	clock_count++;

	/* Call hardclock */
	hardclock(cf);

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
 * Clock autoconfig
 */
static int clock_attached;

static int
clock_match(device_t parent, cfdata_t cf, void *aux)
{
	return !clock_attached;
}

static void
clock_attach(device_t self, device_t parent, void *aux)
{
	clock_attached = 1;
	aprint_normal(": system clock\n");
}

CFATTACH_DECL_NEW(clock, 0,
    clock_match, clock_attach, NULL, NULL);
