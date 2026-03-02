/*	$NetBSD$	*/

/*
 * MC68901 Multi-Function Peripheral register definitions for CMMC68.
 */

#ifndef _CMMC68_MFPREG_H_
#define _CMMC68_MFPREG_H_

/*
 * MFP register base address
 */
#define	MFP_BASE	0xFFFFC0
#define	MFP_REGS	((volatile uint8_t *)MFP_BASE)

/*
 * MFP register offsets
 */
#define	MFP_GPIP	0x01	/* General Purpose I/O Pins */
#define	MFP_AER		0x03	/* Active Edge Register */
#define	MFP_DDR		0x05	/* Data Direction Register */
#define	MFP_IERA	0x07	/* Interrupt Enable Register A */
#define	MFP_IERB	0x09	/* Interrupt Enable Register B */
#define	MFP_IPRA	0x0B	/* Interrupt Pending Register A */
#define	MFP_IPRB	0x0D	/* Interrupt Pending Register B */
#define	MFP_ISRA	0x0F	/* Interrupt In-Service Register A */
#define	MFP_ISRB	0x11	/* Interrupt In-Service Register B */
#define	MFP_IMRA	0x13	/* Interrupt Mask Register A */
#define	MFP_IMRB	0x15	/* Interrupt Mask Register B */
#define	MFP_VR		0x17	/* Vector Register */
#define	MFP_TACR	0x19	/* Timer A Control Register */
#define	MFP_TBCR	0x1B	/* Timer B Control Register */
#define	MFP_TCDCR	0x1D	/* Timer C/D Control Register */
#define	MFP_TADR	0x1F	/* Timer A Data Register */
#define	MFP_TBDR	0x21	/* Timer B Data Register */
#define	MFP_TCDR	0x23	/* Timer C Data Register */
#define	MFP_TDDR	0x25	/* Timer D Data Register */
#define	MFP_SCR		0x27	/* Synchronous Character Register */
#define	MFP_UCR		0x29	/* UART Control Register */
#define	MFP_RSR		0x2B	/* Receiver Status Register */
#define	MFP_TSR		0x2D	/* Transmitter Status Register */
#define	MFP_UDR		0x2F	/* UART Data Register */

/*
 * Timer control register values
 */
#define	TIMER_STOPPED	0x00
#define	TIMER_DELAY_4	0x01	/* Divisor 4 */
#define	TIMER_DELAY_10	0x02	/* Divisor 10 */
#define	TIMER_DELAY_16	0x03	/* Divisor 16 */
#define	TIMER_DELAY_50	0x04	/* Divisor 50 */
#define	TIMER_DELAY_64	0x05	/* Divisor 64 */
#define	TIMER_DELAY_100	0x06	/* Divisor 100 */
#define	TIMER_DELAY_200	0x07	/* Divisor 200 */

/*
 * UART control register bits
 */
#define	UCR_RCV_ENABLE	0x01
#define	UCR_XMIT_ENABLE	0x02
#define	UCR_PARITY_EVEN	0x00
#define	UCR_PARITY_ODD	0x04
#define	UCR_PARITY_NONE	0x10
#define	UCR_8BIT	0x00
#define	UCR_7BIT	0x20
#define	UCR_1STOP	0x00
#define	UCR_1_5STOP	0x40
#define	UCR_2STOP	0x80

/*
 * Receiver status register bits
 */
#define	RSR_CHAR_AVAILABLE	0x80
#define	RSR_OVERRUN_ERROR	0x40
#define	RSR_PARITY_ERROR	0x20
#define	RSR_FRAMING_ERROR	0x10
#define	RSR_BREAK		0x08
#define	RSR_END_OF_BREAK	0x04

/*
 * Transmitter status register bits
 */
#define	TSR_BUFFER_EMPTY	0x80
#define	TSR_UNDERRUN		0x40
#define	TSR_COLLISION		0x20

/*
 * MC68901 interrupt channels.
 * Vector = VR_base + channel_number.
 * Channels 0-7 use IERB/IPRB/ISRB/IMRB (bit = channel).
 * Channels 8-15 use IERA/IPRA/ISRA/IMRA (bit = channel - 8).
 */

/* IERA/IPRA/ISRA/IMRA bits (channels 8-15) */
#define	MFP_IERA_GPIP7		0x80	/* Channel 15: GPI 7 */
#define	MFP_IERA_GPIP6		0x40	/* Channel 14: GPI 6 */
#define	MFP_IERA_TIMER_A	0x20	/* Channel 13: Timer A */
#define	MFP_IERA_RCV_FULL	0x10	/* Channel 12: Receive Buffer Full */
#define	MFP_IERA_RCV_ERR	0x08	/* Channel 11: Receive Error */
#define	MFP_IERA_XMT_EMPTY	0x04	/* Channel 10: Transmit Buffer Empty */
#define	MFP_IERA_XMT_ERR	0x02	/* Channel 9: Transmit Error */
#define	MFP_IERA_TIMER_B	0x01	/* Channel 8: Timer B */

/* IERB/IPRB/ISRB/IMRB bits (channels 0-7) */
#define	MFP_IERB_GPIP5		0x80	/* Channel 7: GPIO 5 */
#define	MFP_IERB_GPIP4		0x40	/* Channel 6: GPIO 4 */
#define	MFP_IERB_TIMER_C	0x20	/* Channel 5: Timer C */
#define	MFP_IERB_TIMER_D	0x10	/* Channel 4: Timer D */
#define	MFP_IERB_GPIP3		0x08	/* Channel 3: GPIO 3 */
#define	MFP_IERB_GPIP2		0x04	/* Channel 2: GPIO 2 */
#define	MFP_IERB_GPIP1		0x02	/* Channel 1: GPIO 1 */
#define	MFP_IERB_GPIP0		0x01	/* Channel 0: GPIO 0 */

/* MFP vector numbers (VR base 0x40) */
#define	MFP_VEC_TIMER_A		0x4D	/* Channel 13 */
#define	MFP_VEC_RCV_FULL	0x4C	/* Channel 12 */
#define	MFP_VEC_TIMER_B		0x48	/* Channel 8 */
#define	MFP_VEC_TIMER_C		0x45	/* Channel 5 */
#define	MFP_VEC_TIMER_D		0x44	/* Channel 4 */

/*
 * MFP timer clock for fallback system clock (Timer A).
 * The MC68901 timers are clocked from the XTAL input.
 * Adjust MFP_XTAL_HZ to match the crystal on your board.
 * Timer C/D must NOT be used — they provide UART baud rate.
 */
#define	MFP_XTAL_HZ		2457600	/* 2.4576 MHz (common for MC68901) */
#define	MFP_TIMER_HZ		100	/* desired interrupt frequency */
#define	MFP_TIMER_PRESCALER	200	/* /200 divisor */
#define	MFP_TIMER_COUNT		(MFP_XTAL_HZ / (MFP_TIMER_PRESCALER * MFP_TIMER_HZ))

#endif /* _CMMC68_MFPREG_H_ */
