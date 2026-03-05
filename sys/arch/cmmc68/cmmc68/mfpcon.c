/*	$NetBSD: mfpcon.c,v 1.2 2024/01/01 00:00:00 cmmc68 Exp $	*/

/*
 * MFP serial console character device for CMMC68.
 *
 * Provides /dev/console via cn_dev = makedev(7,0).
 * Full tty integration with interrupt-driven RX:
 *   hard interrupt fills ring buffer (mfp_rcv_intr in mfp.c),
 *   soft interrupt drains to line discipline (echo, canonical).
 *
 * Polled I/O (cn_tab callbacks) bypasses the tty layer and
 * is used by kernel printf and panic/debugger.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <sys/proc.h>
#include <sys/poll.h>
#include <sys/fcntl.h>
#include <sys/kauth.h>
#include <sys/intr.h>

#include <machine/mfpreg.h>

extern int mfp_putc(int);

/*
 * TTY state
 */
static struct tty *mfpcon_tp;
static void *mfpcon_si;		/* softint cookie */

/*
 * Ring buffer for hard→soft interrupt handoff.
 * Filled by mfp_rcv_intr() in mfp.c, drained by mfpcon_soft().
 */
#define MFPCON_RING_SIZE	64
#define MFPCON_RING_MASK	(MFPCON_RING_SIZE - 1)
uint8_t mfpcon_rbuf[MFPCON_RING_SIZE];
volatile u_int mfpcon_rbget, mfpcon_rbput;

/* Exported for mfp.c interrupt handler */
void *mfpcon_si_cookie(void);

void *
mfpcon_si_cookie(void)
{
	return mfpcon_si;
}

/*
 * Forward declarations
 */
static dev_type_open(mfpconopen);
static dev_type_close(mfpconclose);
static dev_type_read(mfpconread);
static dev_type_write(mfpconwrite);
static dev_type_ioctl(mfpconioctl);
static dev_type_poll(mfpconpoll);
static dev_type_stop(mfpconstop);
static dev_type_tty(mfpcontty);

static void mfpconstart(struct tty *);
static int mfpconparam(struct tty *, struct termios *);
static void mfpcon_soft(void *);

const struct cdevsw mfpcon_cdevsw = {
	.d_open = mfpconopen,
	.d_close = mfpconclose,
	.d_read = mfpconread,
	.d_write = mfpconwrite,
	.d_ioctl = mfpconioctl,
	.d_stop = mfpconstop,
	.d_tty = mfpcontty,
	.d_poll = mfpconpoll,
	.d_mmap = nommap,
	.d_kqfilter = ttykqfilter,
	.d_discard = nodiscard,
	.d_flag = D_TTY
};

static int
mfpconopen(dev_t dev, int flag, int mode, struct lwp *l)
{
	struct tty *tp;
	int error;

	tp = mfpcon_tp;
	if (tp == NULL) {
		tp = tty_alloc();
		mfpcon_tp = tp;
		tty_attach(tp);
	}

	tp->t_oproc = mfpconstart;
	tp->t_param = mfpconparam;
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

		mfpconparam(tp, &tp->t_termios);
		ttsetwater(tp);

		/* Establish softint for RX processing (once) */
		if (mfpcon_si == NULL)
			mfpcon_si = softint_establish(SOFTINT_SERIAL,
			    mfpcon_soft, NULL);

		/* No modem control — always assert carrier */
		tp->t_state |= TS_CARR_ON;
	}
	ttyunlock(tp);

	error = ttyopen(tp, 0, flag & O_NONBLOCK);
	if (error)
		return error;

	error = (*tp->t_linesw->l_open)(dev, tp);
	if (error)
		return error;

	/*
	 * Drain any characters that arrived before the tty was open.
	 * While mfpcon_si was NULL, mfp_rcv_intr() stored characters
	 * in the ring buffer but could not schedule the softint.
	 */
	if (mfpcon_rbget != mfpcon_rbput && mfpcon_si != NULL)
		softint_schedule(mfpcon_si);

	return 0;
}

static int
mfpconclose(dev_t dev, int flag, int mode, struct lwp *l)
{
	struct tty *tp = mfpcon_tp;

	if (tp == NULL)
		return 0;

	(*tp->t_linesw->l_close)(tp, flag);
	ttyclose(tp);

	return 0;
}

static int
mfpconread(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tp = mfpcon_tp;

	if (tp == NULL)
		return ENXIO;
	return (*tp->t_linesw->l_read)(tp, uio, flag);
}

static int
mfpconwrite(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tp = mfpcon_tp;

	if (tp == NULL)
		return ENXIO;
	return (*tp->t_linesw->l_write)(tp, uio, flag);
}

static int
mfpconioctl(dev_t dev, u_long cmd, void *data, int flag, struct lwp *l)
{
	struct tty *tp = mfpcon_tp;
	int error;

	if (tp == NULL)
		return ENXIO;

	error = (*tp->t_linesw->l_ioctl)(tp, cmd, data, flag, l);
	if (error != EPASSTHROUGH)
		return error;

	error = ttioctl(tp, cmd, data, flag, l);
	if (error != EPASSTHROUGH)
		return error;

	return EPASSTHROUGH;
}

static void
mfpconstop(struct tty *tp, int flags)
{
	int s;

	s = spltty();
	if (tp->t_state & TS_BUSY) {
		if ((tp->t_state & TS_TTSTOP) == 0)
			tp->t_state |= TS_FLUSH;
	}
	splx(s);
}

static struct tty *
mfpcontty(dev_t dev)
{
	return mfpcon_tp;
}

static int
mfpconpoll(dev_t dev, int events, struct lwp *l)
{
	struct tty *tp = mfpcon_tp;

	if (tp == NULL)
		return POLLHUP;
	return (*tp->t_linesw->l_poll)(tp, events, l);
}

/*
 * Set serial parameters.
 * MFP only supports 19200 8N1 — accept any request silently.
 */
static int
mfpconparam(struct tty *tp, struct termios *t)
{
	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;
	return 0;
}

/*
 * Start output.
 * Called by tty layer when there is data to send.
 * Dequeues from t_outq and calls mfp_putc() (polled TX).
 */
static void
mfpconstart(struct tty *tp)
{
	int s, c;

	s = spltty();

	if (tp->t_state & (TS_BUSY | TS_TIMEOUT | TS_TTSTOP))
		goto out;

	if (!ttypull(tp))
		goto out;

	tp->t_state |= TS_BUSY;

	while ((c = getc(&tp->t_outq)) != -1)
		mfp_putc(c);

	tp->t_state &= ~TS_BUSY;

out:
	splx(s);
}

/*
 * Software interrupt handler.
 * Drains the ring buffer and feeds characters to line discipline.
 * The line discipline handles echo, canonical processing, etc.
 */
static void
mfpcon_soft(void *arg)
{
	struct tty *tp = mfpcon_tp;

	if (tp == NULL)
		return;

	while (mfpcon_rbget != mfpcon_rbput) {
		int c = mfpcon_rbuf[mfpcon_rbget & MFPCON_RING_MASK];
		mfpcon_rbget++;
		(*tp->t_linesw->l_rint)(c, tp);
	}
}
