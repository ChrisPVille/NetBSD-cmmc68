/*	$NetBSD$	*/

/*
 * MC68681 DUART (Dual Universal Asynchronous Receiver/Transmitter)
 * driver for CMMC68.
 *
 * Two independent serial channels with tty integration.
 * Interrupt-driven RX with ring buffer (hard intr fills,
 * soft intr drains to line discipline).
 *
 * Based on sgimips scn.c patterns.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/tty.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/poll.h>
#include <sys/kauth.h>

#include <m68k/m68k.h>
#include <machine/cpu.h>
#include <machine/vectors.h>
#include <machine/duartreg.h>

/*
 * Per-channel state
 */
struct duart_chan {
	struct tty *tty;
	int channel;			/* 0 = A, 1 = B */
	uint8_t tx_int;			/* IMR bit for TX */
	uint8_t rx_int;			/* IMR bit for RX */
	/* RX ring buffer: filled in hard intr, drained in soft intr */
	uint16_t rbuf[DUART_RING_SIZE];	/* (status << 8) | data */
	volatile u_int rbget;
	volatile u_int rbput;
};

/*
 * Softc for the DUART (one device, two channels)
 */
struct duart_softc {
	device_t sc_dev;
	struct duart_chan sc_chan[2];
	uint8_t sc_imr;			/* shadow of IMR */
	void *sc_si;			/* softint cookie */
};

/* There is only one DUART on CMMC68 */
static struct duart_softc *duart_sc;

static int duart_match(device_t, cfdata_t, void *);
static void duart_attach(device_t, device_t, void *);
static void duart_soft(void *);
void duart_intr(void);
void duart_hw_init(void);

CFATTACH_DECL_NEW(duart, sizeof(struct duart_softc),
    duart_match, duart_attach, NULL, NULL);

extern struct cfdriver duart_cd;

/*
 * cdevsw functions
 */
dev_type_open(duartopen);
dev_type_close(duartclose);
dev_type_read(duartread);
dev_type_write(duartwrite);
dev_type_ioctl(duartioctl);
dev_type_stop(duartstop);
dev_type_tty(duarttty);
dev_type_poll(duartpoll);

const struct cdevsw duart_cdevsw = {
	.d_open = duartopen,
	.d_close = duartclose,
	.d_read = duartread,
	.d_write = duartwrite,
	.d_ioctl = duartioctl,
	.d_stop = duartstop,
	.d_tty = duarttty,
	.d_poll = duartpoll,
	.d_mmap = nommap,
	.d_kqfilter = ttykqfilter,
	.d_discard = nodiscard,
	.d_flag = D_TTY
};

static void duartstart(struct tty *);
static int duartparam(struct tty *, struct termios *);

/*
 * Channel register access helpers.
 * Channel A: MR at 0x01, SR at 0x03, CR at 0x05, DAT at 0x07
 * Channel B: MR at 0x11, SR at 0x13, CR at 0x15, DAT at 0x17
 */
static inline volatile uint8_t *
duart_chbase(int channel)
{
	return DUART_REGS + (channel ? 0x10 : 0x00);
}

#define CH_MR	0x01	/* Mode Register (MR1/MR2 auto-pointer) */
#define CH_SR	0x03	/* Status Register (read) */
#define CH_CSR	0x03	/* Clock Select Register (write) */
#define CH_CR	0x05	/* Command Register (write) */
#define CH_RHR	0x07	/* RX Holding Register (read) */
#define CH_THR	0x07	/* TX Holding Register (write) */

/*
 * Early hardware initialization (called before autoconfig).
 * Installs the interrupt vector and does basic chip reset.
 */
void
duart_hw_init(void)
{
	volatile uint8_t *du = DUART_REGS;
	volatile uint8_t *ch;
	extern char intrhand_autovec[];
	int i;

	/* Reset both channels */
	for (i = 0; i < 2; i++) {
		ch = duart_chbase(i);
		ch[CH_CR] = CR_RST_MR;
		ch[CH_CR] = CR_RST_RX;
		ch[CH_CR] = CR_RST_TX;
		ch[CH_CR] = CR_RST_ERR;
	}

	/* Disable all interrupts */
	du[DU_IMR] = 0;

	/* Set Interrupt Vector Register */
	du[DU_IVR] = DUART_VEC;

	/* Install interrupt handler at vector 0x51 */
	vec_set_entry(DUART_VEC, intrhand_autovec);

	printf("duart0: MC68681 DUART, 2 channels\n");
}

/* ------------------------------------------------------------------ */

static int
duart_match(device_t parent, cfdata_t cf, void *aux)
{
	return (duart_sc == NULL);
}

static void
duart_attach(device_t self, device_t parent, void *aux)
{
	struct duart_softc *sc = device_private(self);
	volatile uint8_t *du = DUART_REGS;
	volatile uint8_t *ch;
	extern char intrhand_autovec[];
	int i;

	sc->sc_dev = self;
	duart_sc = sc;

	/* Reset both channels */
	for (i = 0; i < 2; i++) {
		struct duart_chan *dc = &sc->sc_chan[i];
		dc->channel = i;
		dc->rbget = dc->rbput = 0;
		dc->tx_int = (i == 0) ? INT_TXA : INT_TXB;
		dc->rx_int = (i == 0) ? INT_RXA : INT_RXB;

		ch = duart_chbase(i);
		ch[CH_CR] = CR_RST_MR;		/* Reset MR pointer */
		ch[CH_CR] = CR_RST_RX;		/* Reset receiver */
		ch[CH_CR] = CR_RST_TX;		/* Reset transmitter */
		ch[CH_CR] = CR_RST_ERR;	/* Reset error status */

		/* Configure 19200 8N1 */
		ch[CH_CR] = CR_RST_MR;		/* Point to MR1 */
		ch[CH_MR] = MR1_CS8 | MR1_PNONE; /* MR1: 8-bit, no parity */
		ch[CH_MR] = MR2_STOP1;		/* MR2: 1 stop bit */
	}

	/* ACR: baud rate set 2 */
	du[DU_ACR] = ACR_SET2;

	/* Set baud rate 19200 for both channels */
	du[DU_CSRA] = CSR_19200;
	du[DU_CSRB] = CSR_19200;

	/* Set Interrupt Vector Register */
	du[DU_IVR] = DUART_VEC;

	/* Install interrupt handler at vector 0x51 */
	vec_set_entry(DUART_VEC, intrhand_autovec);

	/* Enable RX on both channels, TX will be enabled on open */
	ch = duart_chbase(0);
	ch[CH_CR] = CR_ENA_RX;
	ch = duart_chbase(1);
	ch[CH_CR] = CR_ENA_RX;

	/* Start with no interrupts enabled */
	sc->sc_imr = 0;
	du[DU_IMR] = 0;

	/* Set up softint for RX processing */
	sc->sc_si = softint_establish(SOFTINT_SERIAL, duart_soft, sc);

	aprint_normal(": MC68681 DUART, 2 channels\n");
}

/* ------------------------------------------------------------------ */

/*
 * Get softc and channel from minor device number.
 * minor 0 = channel A, minor 1 = channel B.
 */
static inline struct duart_chan *
duart_getchan(dev_t dev)
{
	int unit = minor(dev);
	if (duart_sc == NULL || unit > 1)
		return NULL;
	return &duart_sc->sc_chan[unit];
}

int
duartopen(dev_t dev, int flags, int mode, struct lwp *l)
{
	struct duart_chan *dc = duart_getchan(dev);
	struct duart_softc *sc = duart_sc;
	volatile uint8_t *du = DUART_REGS;
	volatile uint8_t *ch;
	struct tty *tp;
	int error;

	if (dc == NULL)
		return ENXIO;

	tp = dc->tty;
	if (tp == NULL) {
		tp = tty_alloc();
		dc->tty = tp;
		tty_attach(tp);
	}

	tp->t_oproc = duartstart;
	tp->t_param = duartparam;
	tp->t_dev = dev;

	if (kauth_authorize_device_tty(l->l_cred, KAUTH_DEVICE_TTY_OPEN, tp))
		return EBUSY;

	ttylock(tp);
	if ((tp->t_state & TS_ISOPEN) == 0 && tp->t_wopen == 0) {
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = CS8 | CREAD | CLOCAL | HUPCL;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed = 19200;
		tp->t_winsize.ws_row = 24;
		tp->t_winsize.ws_col = 80;

		duartparam(tp, &tp->t_termios);
		ttsetwater(tp);

		/* Enable TX and RX on this channel */
		ch = duart_chbase(dc->channel);
		ch[CH_CR] = CR_ENA_TX | CR_ENA_RX;

		/* Enable RX interrupt */
		sc->sc_imr |= dc->rx_int;
		du[DU_IMR] = sc->sc_imr;

		/* No modem control — always assert carrier */
		tp->t_state |= TS_CARR_ON;
	}
	ttyunlock(tp);

	error = ttyopen(tp, 0, flags & O_NONBLOCK);
	if (error)
		return error;

	error = (*tp->t_linesw->l_open)(dev, tp);
	if (error) {
		if ((tp->t_state & TS_ISOPEN) == 0 && tp->t_wopen == 0) {
			/* Disable RX interrupt if nobody else has it open */
			sc->sc_imr &= ~dc->rx_int;
			du[DU_IMR] = sc->sc_imr;
		}
		return error;
	}

	return 0;
}

int
duartclose(dev_t dev, int flags, int mode, struct lwp *l)
{
	struct duart_chan *dc = duart_getchan(dev);
	struct duart_softc *sc = duart_sc;
	volatile uint8_t *du = DUART_REGS;
	struct tty *tp;

	if (dc == NULL)
		return ENXIO;

	tp = dc->tty;
	if (tp == NULL)
		return 0;

	(*tp->t_linesw->l_close)(tp, flags);

	/* Disable TX interrupt */
	sc->sc_imr &= ~dc->tx_int;
	du[DU_IMR] = sc->sc_imr;

	ttyclose(tp);

	return 0;
}

int
duartread(dev_t dev, struct uio *uio, int flags)
{
	struct duart_chan *dc = duart_getchan(dev);
	struct tty *tp;

	if (dc == NULL || dc->tty == NULL)
		return ENXIO;
	tp = dc->tty;
	return (*tp->t_linesw->l_read)(tp, uio, flags);
}

int
duartwrite(dev_t dev, struct uio *uio, int flags)
{
	struct duart_chan *dc = duart_getchan(dev);
	struct tty *tp;

	if (dc == NULL || dc->tty == NULL)
		return ENXIO;
	tp = dc->tty;
	return (*tp->t_linesw->l_write)(tp, uio, flags);
}

int
duartioctl(dev_t dev, u_long cmd, void *data, int flags, struct lwp *l)
{
	struct duart_chan *dc = duart_getchan(dev);
	struct tty *tp;
	int error;

	if (dc == NULL || dc->tty == NULL)
		return ENXIO;
	tp = dc->tty;

	error = (*tp->t_linesw->l_ioctl)(tp, cmd, data, flags, l);
	if (error != EPASSTHROUGH)
		return error;

	error = ttioctl(tp, cmd, data, flags, l);
	if (error != EPASSTHROUGH)
		return error;

	return EPASSTHROUGH;
}

void
duartstop(struct tty *tp, int flags)
{
	int s;

	s = spltty();
	if (tp->t_state & TS_BUSY) {
		if ((tp->t_state & TS_TTSTOP) == 0)
			tp->t_state |= TS_FLUSH;
	}
	splx(s);
}

struct tty *
duarttty(dev_t dev)
{
	struct duart_chan *dc = duart_getchan(dev);

	if (dc == NULL)
		return NULL;
	return dc->tty;
}

int
duartpoll(dev_t dev, int events, struct lwp *l)
{
	struct duart_chan *dc = duart_getchan(dev);
	struct tty *tp;

	if (dc == NULL || dc->tty == NULL)
		return POLLHUP;
	tp = dc->tty;
	return (*tp->t_linesw->l_poll)(tp, events, l);
}

/* ------------------------------------------------------------------ */

/*
 * Set serial parameters (baud rate, data bits, parity, stop bits).
 * Called from tty layer when termios changes.
 */
static int
duartparam(struct tty *tp, struct termios *t)
{
	/* For now we only support 19200 8N1.  Accept any request silently. */
	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;
	return 0;
}

/*
 * Start output on a channel.
 * Called by the tty layer when there is data to send.
 */
static void
duartstart(struct tty *tp)
{
	struct duart_chan *dc;
	struct duart_softc *sc = duart_sc;
	volatile uint8_t *du = DUART_REGS;
	volatile uint8_t *ch;
	int s, c;

	if (sc == NULL)
		return;

	dc = &sc->sc_chan[minor(tp->t_dev)];
	ch = duart_chbase(dc->channel);

	s = spltty();

	if (tp->t_state & (TS_BUSY | TS_TIMEOUT | TS_TTSTOP))
		goto out;

	if (!ttypull(tp))
		goto out;

	tp->t_state |= TS_BUSY;

	/* Fill TX register (MC68681 has a 1-byte THR, no FIFO) */
	if (ch[CH_SR] & SR_TXRDY) {
		c = getc(&tp->t_outq);
		if (c != -1)
			ch[CH_THR] = c;
	}

	/* Enable TX interrupt to continue sending */
	sc->sc_imr |= dc->tx_int;
	du[DU_IMR] = sc->sc_imr;

out:
	splx(s);
}

/* ------------------------------------------------------------------ */

/*
 * Hardware interrupt handler.
 * Called from intr_dispatch() when DUART vector (0x51) is received.
 *
 * Reads ISR, handles TX ready and RX ready for both channels.
 * RX data is stored in ring buffer; softint processes it later.
 */
void
duart_intr(void)
{
	struct duart_softc *sc = duart_sc;
	volatile uint8_t *du = DUART_REGS;
	uint8_t isr_val;
	int did_rx = 0;

	if (sc == NULL)
		return;

	isr_val = du[DU_ISR];

	/* RX ready channel A */
	if (isr_val & INT_RXA) {
		struct duart_chan *dc = &sc->sc_chan[0];
		volatile uint8_t *ch = duart_chbase(0);

		while (ch[CH_SR] & SR_RXRDY) {
			uint8_t sr = ch[CH_SR];
			uint8_t data = ch[CH_RHR];
			u_int put = dc->rbput;
			dc->rbuf[put & DUART_RING_MASK] =
			    ((uint16_t)sr << 8) | data;
			dc->rbput = put + 1;
			did_rx = 1;
		}
	}

	/* RX ready channel B */
	if (isr_val & INT_RXB) {
		struct duart_chan *dc = &sc->sc_chan[1];
		volatile uint8_t *ch = duart_chbase(1);

		while (ch[CH_SR] & SR_RXRDY) {
			uint8_t sr = ch[CH_SR];
			uint8_t data = ch[CH_RHR];
			u_int put = dc->rbput;
			dc->rbuf[put & DUART_RING_MASK] =
			    ((uint16_t)sr << 8) | data;
			dc->rbput = put + 1;
			did_rx = 1;
		}
	}

	/* TX ready channel A */
	if (isr_val & INT_TXA) {
		struct duart_chan *dc = &sc->sc_chan[0];
		struct tty *tp = dc->tty;

		if (tp != NULL && (tp->t_state & TS_BUSY)) {
			volatile uint8_t *ch = duart_chbase(0);
			int c;

			tp->t_state &= ~(TS_BUSY | TS_FLUSH);

			/* Send next character if available */
			c = getc(&tp->t_outq);
			if (c != -1) {
				tp->t_state |= TS_BUSY;
				ch[CH_THR] = c;
			} else {
				/* No more data — disable TX interrupt */
				sc->sc_imr &= ~dc->tx_int;
				du[DU_IMR] = sc->sc_imr;
			}

			(*tp->t_linesw->l_start)(tp);
		} else {
			/* Nobody waiting — disable TX interrupt */
			sc->sc_imr &= ~INT_TXA;
			du[DU_IMR] = sc->sc_imr;
		}
	}

	/* TX ready channel B */
	if (isr_val & INT_TXB) {
		struct duart_chan *dc = &sc->sc_chan[1];
		struct tty *tp = dc->tty;

		if (tp != NULL && (tp->t_state & TS_BUSY)) {
			volatile uint8_t *ch = duart_chbase(1);
			int c;

			tp->t_state &= ~(TS_BUSY | TS_FLUSH);

			c = getc(&tp->t_outq);
			if (c != -1) {
				tp->t_state |= TS_BUSY;
				ch[CH_THR] = c;
			} else {
				sc->sc_imr &= ~dc->tx_int;
				du[DU_IMR] = sc->sc_imr;
			}

			(*tp->t_linesw->l_start)(tp);
		} else {
			sc->sc_imr &= ~INT_TXB;
			du[DU_IMR] = sc->sc_imr;
		}
	}

	/* Schedule softint to drain RX ring buffers */
	if (did_rx)
		softint_schedule(sc->sc_si);
}

/*
 * Software interrupt handler.
 * Drains RX ring buffers and feeds characters to line discipline.
 */
static void
duart_soft(void *arg)
{
	struct duart_softc *sc = arg;
	int i;

	for (i = 0; i < 2; i++) {
		struct duart_chan *dc = &sc->sc_chan[i];
		struct tty *tp = dc->tty;

		if (tp == NULL)
			continue;

		while (dc->rbget != dc->rbput) {
			uint16_t raw = dc->rbuf[dc->rbget & DUART_RING_MASK];
			dc->rbget++;

			int c = raw & 0xFF;
			int sr = (raw >> 8) & 0xFF;

			/* Map hardware errors to tty error flags */
			if (sr & SR_PE)
				c |= TTY_PE;
			if (sr & SR_FE)
				c |= TTY_FE;

			(*tp->t_linesw->l_rint)(c, tp);
		}
	}
}
