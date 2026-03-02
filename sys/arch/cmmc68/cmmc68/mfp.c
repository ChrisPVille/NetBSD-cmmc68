/*	$NetBSD: mfp.c,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $ */

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
 * MC68901 Multi-Function Peripheral driver for CMMC68
 *
 * The MFP provides:
 * - UART (serial I/O) - polled mode
 * - Interrupt controller with 16 sources
 * - 4 programmable timers
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/tty.h>
#include <sys/device.h>

#include <m68k/m68k.h>
#include <machine/mfpreg.h>

/*
 * MFP console ring buffer and softint (defined in mfpcon.c).
 * The receive interrupt fills this ring buffer; the softint
 * in mfpcon.c drains it to the tty line discipline.
 */
extern uint8_t mfpcon_rbuf[];
extern volatile u_int mfpcon_rbget, mfpcon_rbput;
extern void *mfpcon_si_cookie(void);

#define MFPCON_RING_SIZE	64
#define MFPCON_RING_MASK	(MFPCON_RING_SIZE - 1)

/*
 * MFP interrupt handlers
 */
extern void (*mfp_handlers[])(void);

/* Forward declarations */
void mfp_init(void);
void mfp_intr_handler(int);
void mfp_rcv_intr(void);
int mfp_putc(int);
int mfp_getc(void);

/*
 * MFP serial receive interrupt setup is done in cpu_initclocks()
 * (clock.c) after the MFP vectors are installed.  The transmitter
 * works with polled I/O from boot via mfp_putc() and needs no init.
 */

/*
 * MFP interrupt handler (legacy, not used).
 * Interrupt dispatch is handled by intr_dispatch() in intr.c
 * which routes individual MFP vectors directly.
 */
void
mfp_intr_handler(int level)
{
}

/*
 * Receive interrupt handler.
 * Reads character from MFP UDR, stores in mfpcon ring buffer,
 * and schedules softint to feed the tty line discipline.
 */
void
mfp_rcv_intr(void)
{
	uint8_t rsr, data;
	u_int put;
	void *si;

	/* Read status */
	rsr = MFP_REGS[MFP_RSR];

	/* Check if character is available */
	if (!(rsr & RSR_CHAR_AVAILABLE))
		return;

	/* Read data (must read UDR to clear interrupt) */
	data = MFP_REGS[MFP_UDR];

	/* Discard characters with errors */
	if (rsr & (RSR_OVERRUN_ERROR | RSR_PARITY_ERROR | RSR_FRAMING_ERROR))
		return;

	/* Store in mfpcon ring buffer */
	put = mfpcon_rbput;
	mfpcon_rbuf[put & MFPCON_RING_MASK] = data;
	mfpcon_rbput = put + 1;

	/* Schedule softint to drain ring to tty */
	si = mfpcon_si_cookie();
	if (si != NULL)
		softint_schedule(si);
}

/*
 * Put a character to the serial port (polled)
 */
int
mfp_putc(int c)
{
	/* Wait for transmitter to be ready */
	while (!(MFP_REGS[MFP_TSR] & TSR_BUFFER_EMPTY)) {
		/* Poll wait */
	}
	/* Send character directly */
	MFP_REGS[MFP_UDR] = (uint8_t)c;
	return c;
}

/*
 * Get a character from the serial port (polled, non-blocking).
 * Reads hardware directly — used by cn_tab (kernel console)
 * during early boot, panic, and debugger when interrupts are off.
 * Returns -1 if no character available.
 */
int
mfp_getc(void)
{
	if (!(MFP_REGS[MFP_RSR] & RSR_CHAR_AVAILABLE))
		return -1;
	return MFP_REGS[MFP_UDR];
}

/*
 * MFP autoconfig
 */
static int mfp_attached;

static int
mfp_match(device_t parent, cfdata_t cf, void *aux)
{
	return !mfp_attached;
}

static void
mfp_attach(device_t self, device_t parent, void *aux)
{
	mfp_attached = 1;
	aprint_normal(": MC68901 MFP\n");

	/* Attach children (clock) */
	config_search(self, NULL,
	    CFARGS(.search = config_stdsubmatch));
}

CFATTACH_DECL_NEW(mfp, 0,
    mfp_match, mfp_attach, NULL, NULL);
