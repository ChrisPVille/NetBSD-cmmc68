/*	$NetBSD: mainbus.c,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $ */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/cpu.h>
#include <machine/pitreg.h>
#include <machine/duartreg.h>

static int mainbus_match(device_t, cfdata_t, void *);
static void mainbus_attach(device_t, device_t, void *);

CFATTACH_DECL_NEW(mainbus, 0,
    mainbus_match, mainbus_attach, NULL, NULL);

static int
mainbus_match(device_t parent, cfdata_t cf, void *aux)
{
	return 1;
}

static void
mainbus_attach(device_t self, device_t parent, void *aux)
{
	extern void pit_timer_init(void);
	extern void duart_hw_init(void);

	aprint_normal("\n");

	/* Probe for optional PIT (MC68230) at PA 0xFDC000 */
	if (badbaddr((void *)(PIT_BASE + PIT_TIVR)) == 0) {
		pit_timer_init();
	} else {
		aprint_normal("mainbus: PIT not present\n");
	}

	/* Probe for optional DUART (MC68681) at PA 0xFD9000 */
	if (badbaddr((void *)(DUART_BASE + DU_SRA)) == 0) {
		duart_hw_init();
	} else {
		aprint_normal("mainbus: DUART not present\n");
	}
}
