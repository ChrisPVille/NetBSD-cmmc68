/*	$NetBSD: mfpcon.c,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $	*/

/*
 * Minimal character device for CMMC68 MFP serial console.
 * Uses polled I/O — no interrupts.
 * Provides cdevsw for /dev/console delegation via cn_dev.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <sys/proc.h>

#include <machine/mfpreg.h>

extern int mfp_putc(int);
extern int mfp_getc(void);

static dev_type_open(mfpconopen);
static dev_type_close(mfpconclose);
static dev_type_read(mfpconread);
static dev_type_write(mfpconwrite);
static dev_type_ioctl(mfpconioctl);
static dev_type_poll(mfpconpoll);

const struct cdevsw mfpcon_cdevsw = {
	.d_open = mfpconopen,
	.d_close = mfpconclose,
	.d_read = mfpconread,
	.d_write = mfpconwrite,
	.d_ioctl = mfpconioctl,
	.d_stop = nostop,
	.d_tty = notty,
	.d_poll = mfpconpoll,
	.d_mmap = nommap,
	.d_kqfilter = nokqfilter,
	.d_discard = nodiscard,
	.d_flag = D_TTY
};

static int
mfpconopen(dev_t dev, int flag, int mode, struct lwp *l)
{
	return 0;
}

static int
mfpconclose(dev_t dev, int flag, int mode, struct lwp *l)
{
	return 0;
}

static int
mfpconread(dev_t dev, struct uio *uio, int flag)
{
	int c;
	int error = 0;

	while (uio->uio_resid > 0) {
		c = mfp_getc();
		if (c < 0) {
			/* No data available — for now, spin-wait */
			continue;
		}
		error = ureadc(c, uio);
		if (error)
			break;
		/* Return after each character for line buffering */
		break;
	}
	return error;
}

static int
mfpconwrite(dev_t dev, struct uio *uio, int flag)
{
	int error = 0;
	char buf[64];

	while (uio->uio_resid > 0) {
		size_t n = uio->uio_resid;
		if (n > sizeof(buf))
			n = sizeof(buf);
		error = uiomove(buf, n, uio);
		if (error)
			break;
		for (size_t i = 0; i < n; i++)
			mfp_putc(buf[i]);
	}
	return error;
}

static int
mfpconioctl(dev_t dev, u_long cmd, void *data, int flag, struct lwp *l)
{
	return EPASSTHROUGH;
}

static int
mfpconpoll(dev_t dev, int events, struct lwp *l)
{
	return events;	/* Always ready */
}
