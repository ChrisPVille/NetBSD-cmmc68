/*	$NetBSD$	*/

/*
 * MC68230 PI/T (Parallel Interface / Timer) register definitions.
 *
 * Active on LDS (odd bytes): register N is at base + N*2 + 1.
 * The PIT is at physical address 0xFDC000 on CMMC68.
 */

#ifndef _CMMC68_PITREG_H_
#define _CMMC68_PITREG_H_

#define PIT_BASE	0xFDC000
#define PIT_REGS	((volatile uint8_t *)PIT_BASE)

/* Port registers */
#define PIT_PGCR	0x01	/* Port General Control */
#define PIT_PSRR	0x03	/* Port Service Request */
#define PIT_PADDR	0x05	/* Port A Data Direction */
#define PIT_PBDDR	0x07	/* Port B Data Direction */
#define PIT_PCDDR	0x09	/* Port C Data Direction */
#define PIT_PIVR	0x0B	/* Port Interrupt Vector */
#define PIT_PACR	0x0D	/* Port A Control */
#define PIT_PBCR	0x0F	/* Port B Control */
#define PIT_PADR	0x11	/* Port A Data */
#define PIT_PBDR	0x13	/* Port B Data */
#define PIT_PCDR	0x19	/* Port C Data */
#define PIT_PSR		0x1B	/* Port Status */

/* Timer registers */
#define PIT_TCR		0x21	/* Timer Control */
#define PIT_TIVR	0x23	/* Timer Interrupt Vector */
#define PIT_CPRH	0x27	/* Counter Preload High */
#define PIT_CPRM	0x29	/* Counter Preload Mid */
#define PIT_CPRL	0x2B	/* Counter Preload Low */
#define PIT_CNTH	0x2F	/* Counter High (read-only) */
#define PIT_CNTM	0x31	/* Counter Mid (read-only) */
#define PIT_CNTL	0x33	/* Counter Low (read-only) */
#define PIT_TSR		0x35	/* Timer Status */

/* TCR bits */
#define TCR_ENABLE	0x01	/* Timer enable */
#define TCR_CLK_32	0x00	/* CLK/32 prescaler (bits 2:1 = 00) */
#define TCR_CLK_1	0x02	/* CLK/1 direct (bits 2:1 = 01) */
#define TCR_ZD_INT	0x20	/* Zero detect interrupt enable (bit 5) */

/* TSR bits */
#define TSR_ZDS		0x01	/* Zero Detect Status */

/* Timer vector and preload for 100 Hz */
#define PIT_TIMER_VEC	0x50
#define PIT_CLK_HZ	8000000
#define PIT_PRESCALER	32
#define PIT_PRELOAD	(PIT_CLK_HZ / PIT_PRESCALER / 100)  /* 2500 = 0x9C4 */

#endif /* _CMMC68_PITREG_H_ */
