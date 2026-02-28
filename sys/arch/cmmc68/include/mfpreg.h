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
 * Interrupt source definitions
 * IERA/IPRA/ISRA/IMRA bits
 */
#define	MFP_IRQ_GPIP7	0x80	/* Timer D */
#define	MFP_IRQ_GPIP6	0x40	/* Timer C */
#define	MFP_IRQ_GPIP5	0x20	/* Timer B */
#define	MFP_IRQ_GPIP4	0x10	/* Timer A */
#define	MFP_IRQ_GPIP3	0x08	/* Link */
#define	MFP_IRQ_GPIP2	0x04	/* XMIT */
#define	MFP_IRQ_GPIP1	0x02	/* RCV */
#define	MFP_IRQ_GPIP0	0x01	/* GPIO0 */

/*
 * IERB/IPRB/ISRB/IMRB bits
 */
#define	MFP_IRQ_TIMER_D	0x80
#define	MFP_IRQ_TIMER_C	0x40
#define	MFP_IRQ_TIMER_B	0x20
#define	MFP_IRQ_TIMER_A	0x10
#define	MFP_IRQ_LINK	0x08
#define	MFP_IRQ_XMIT	0x02
#define	MFP_IRQ_RCV	0x01

#endif /* _CMMC68_MFPREG_H_ */
