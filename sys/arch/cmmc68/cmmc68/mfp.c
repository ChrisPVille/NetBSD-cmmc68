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
 * Serial receive buffer
 */
#define	SERIAL_BUFFER_SIZE	64

static uint8_t rx_buffer[SERIAL_BUFFER_SIZE];
static int rx_head = 0, rx_tail = 0;

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
int mfp_getc_wait(void);
int mfp_peek(void);

/*
 * Initialize the MFP
 */
void
mfp_init(void)
{
	/* Reset the MFP */
	MFP_REGS[MFP_VR] = 0x40;  /* Software reset, vector base = 0x40 */

	/* Disable all interrupts */
	MFP_REGS[MFP_IERA] = 0x00;
	MFP_REGS[MFP_IERB] = 0x00;

	/* Unmask all interrupt sources (IMRA/IMRB control masking) */
	MFP_REGS[MFP_IMRA] = 0xFF;
	MFP_REGS[MFP_IMRB] = 0xFF;

	/* Initialize UART */
	MFP_REGS[MFP_UCR] = UCR_RCV_ENABLE | UCR_XMIT_ENABLE |
	                    UCR_PARITY_NONE | UCR_8BIT | UCR_1STOP;

	/* Clear receive/transmit status */
	(void)MFP_REGS[MFP_RSR];
	(void)MFP_REGS[MFP_TSR];

	/* Enable serial interrupts */
	MFP_REGS[MFP_IERA] |= MFP_IRQ_RCV;   /* Receive interrupt */
	MFP_REGS[MFP_IERA] |= MFP_IRQ_XMIT;  /* Transmit interrupt */

	/* Initialize timer for system clock */
	MFP_REGS[MFP_TACR] = TIMER_STOPPED;
	MFP_REGS[MFP_TADR] = 0x00;  /* Timer count */

	/* Set timer to generate 100 Hz interrupts */
	/* Assuming 2.4576 MHz crystal */
	/* Divisor = 2,457,600 / (100 * 4) = 6144 = 0x1800 */
	MFP_REGS[MFP_TACR] = TIMER_DELAY_4;
	MFP_REGS[MFP_TADR] = 0x80;  /* High byte */
	MFP_REGS[MFP_TADR] = 0x00;  /* Low byte */

	/* Enable timer interrupt */
	MFP_REGS[MFP_IERA] |= MFP_IRQ_TIMER_A;
}

/*
 * MFP interrupt handler
 * Called from trap.c for each interrupt level
 */
void
mfp_intr_handler(int level)
{
	uint8_t ipra;

	/* Read interrupt pending register */
	ipra = MFP_REGS[MFP_IPRA];

	/* Handle IRQA interrupts */
	if (ipra & MFP_IRQ_RCV) {
		mfp_rcv_intr();
	}

	if (ipra & MFP_IRQ_GPIP4) {
		/* Timer A interrupt - system clock */
		extern void clock_handler(void);
		clock_handler();
	}
}

/*
 * Receive interrupt handler
 */
void
mfp_rcv_intr(void)
{
	uint8_t rsr, data;
	int next_head;

	/* Read status */
	rsr = MFP_REGS[MFP_RSR];

	/* Check if character is available */
	if (!(rsr & RSR_CHAR_AVAILABLE)) {
		return;
	}

	/* Read data */
	data = MFP_REGS[MFP_UDR];

	/* Check for errors */
	if (rsr & (RSR_OVERRUN_ERROR | RSR_PARITY_ERROR | RSR_FRAMING_ERROR)) {
		/* Discard character with errors */
		return;
	}

	/* Add to receive buffer */
	next_head = (rx_head + 1) % SERIAL_BUFFER_SIZE;
	if (next_head != rx_tail) {
		rx_buffer[rx_head] = data;
		rx_head = next_head;
	}
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
 * Get a character from the serial port (non-blocking)
 * Returns -1 if no character available
 */
int
mfp_getc(void)
{
	uint8_t data;

	if (rx_head == rx_tail) {
		return -1;  /* No data available */
	}

	data = rx_buffer[rx_tail];
	rx_tail = (rx_tail + 1) % SERIAL_BUFFER_SIZE;

	return data;
}

/*
 * Get a character from the serial port (blocking)
 */
int
mfp_getc_wait(void)
{
	int c;

	while ((c = mfp_getc()) == -1) {
		/* Wait for character */
	}

	return c;
}

/*
 * Check if character is available
 */
int
mfp_peek(void)
{
	return (rx_head != rx_tail) ? 1 : 0;
}

/*
 * MFP config attachment declaration
 */
CFATTACH_DECL_NEW(mfp, 0,
    NULL, NULL, NULL, NULL);
