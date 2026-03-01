/*	$NetBSD$	*/

/*
 * MC68681 DUART (Dual Universal Asynchronous Receiver/Transmitter)
 * register definitions for CMMC68.
 *
 * Active on LDS (odd bytes): register N is at base + N*2 + 1.
 * The DUART is at physical address 0xFD9000 on CMMC68.
 *
 * Read/Write aliasing: same offset may have different functions
 * depending on read vs. write.
 */

#ifndef _CMMC68_DUARTREG_H_
#define _CMMC68_DUARTREG_H_

#define DUART_BASE	0xFD9000
#define DUART_REGS	((volatile uint8_t *)DUART_BASE)

/*
 * Register offsets (odd byte offsets from base).
 * Chip register N is at offset N*2 + 1.
 */

/* Channel A registers */
#define DU_MRA		0x01	/* MR1A/MR2A (auto-pointer) */
#define DU_SRA		0x03	/* Status Register A (read) */
#define DU_CSRA		0x03	/* Clock Select Register A (write) */
#define DU_CRA		0x05	/* Command Register A (write) */
#define DU_RHRA		0x07	/* RX Holding Register A (read) */
#define DU_THRA		0x07	/* TX Holding Register A (write) */

/* DUART-wide registers */
#define DU_IPCR		0x09	/* Input Port Change (read) */
#define DU_ACR		0x09	/* Aux Control Register (write) */
#define DU_ISR		0x0B	/* Interrupt Status (read) */
#define DU_IMR		0x0B	/* Interrupt Mask (write) */
#define DU_CTUR		0x0D	/* Counter/Timer Upper (write) */
#define DU_CTLR		0x0F	/* Counter/Timer Lower (write) */

/* Channel B registers */
#define DU_MRB		0x11	/* MR1B/MR2B (auto-pointer) */
#define DU_SRB		0x13	/* Status Register B (read) */
#define DU_CSRB		0x13	/* Clock Select Register B (write) */
#define DU_CRB		0x15	/* Command Register B (write) */
#define DU_RHRB		0x17	/* RX Holding Register B (read) */
#define DU_THRB		0x17	/* TX Holding Register B (write) */

/* More DUART-wide registers */
#define DU_IVR		0x19	/* Interrupt Vector Register */
#define DU_IP		0x1B	/* Input Port (read) */
#define DU_OPCR		0x1B	/* Output Port Config (write) */
#define DU_SOPBC	0x1D	/* Set Output Port Bits (write) */
#define DU_ROPBC	0x1F	/* Reset Output Port Bits (write) */

/* Status Register bits (SRA/SRB) */
#define SR_RXRDY	0x01	/* Receiver ready */
#define SR_FFULL	0x02	/* FIFO full */
#define SR_TXRDY	0x04	/* Transmitter ready */
#define SR_TXEMT	0x08	/* Transmitter empty */
#define SR_OE		0x10	/* Overrun error */
#define SR_PE		0x20	/* Parity error */
#define SR_FE		0x40	/* Framing error */
#define SR_BRK		0x80	/* Received break */

/* ISR/IMR bits */
#define INT_TXA		0x01	/* Channel A TX ready */
#define INT_RXA		0x02	/* Channel A RX ready */
#define INT_TXB		0x10	/* Channel B TX ready */
#define INT_RXB		0x20	/* Channel B RX ready */

/* Command Register (CRA/CRB) */
#define CR_DIS_TX	0x08	/* Disable transmitter */
#define CR_ENA_TX	0x04	/* Enable transmitter */
#define CR_DIS_RX	0x02	/* Disable receiver */
#define CR_ENA_RX	0x01	/* Enable receiver */
#define CR_RST_MR	0x10	/* Reset MR pointer */
#define CR_RST_RX	0x20	/* Reset receiver */
#define CR_RST_TX	0x30	/* Reset transmitter */
#define CR_RST_ERR	0x40	/* Reset error status */
#define CR_RST_BRK	0x50	/* Reset break change */

/* MR1 bits */
#define MR1_CS5		0x00	/* 5 data bits */
#define MR1_CS6		0x01	/* 6 data bits */
#define MR1_CS7		0x02	/* 7 data bits */
#define MR1_CS8		0x03	/* 8 data bits */
#define MR1_PNONE	0x10	/* No parity */
#define MR1_PEVEN	0x00	/* Even parity */
#define MR1_PODD	0x04	/* Odd parity */
#define MR1_RXRTS	0x80	/* RX RTS flow control */

/* MR2 bits */
#define MR2_STOP1	0x07	/* 1 stop bit */
#define MR2_STOP2	0x0F	/* 2 stop bits */
#define MR2_TXCTS	0x10	/* TX CTS flow control */

/* Baud rates with ACR[7]=1 (Baud Rate Set 2) */
#define CSR_9600	0xBB
#define CSR_19200	0xCC
#define ACR_SET2	0x80

/* Interrupt vector */
#define DUART_VEC	0x51

/* RX ring buffer size (must be power of 2) */
#define DUART_RING_SIZE	256
#define DUART_RING_MASK	(DUART_RING_SIZE - 1)

#endif /* _CMMC68_DUARTREG_H_ */
