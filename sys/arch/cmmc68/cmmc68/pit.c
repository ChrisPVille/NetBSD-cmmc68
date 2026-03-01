/*	$NetBSD$	*/

/*
 * MC68230 PI/T (Parallel Interface / Timer) driver for CMMC68.
 *
 * Timer: 24-bit countdown at 8 MHz / 32 = 250 kHz, generates
 * 100 Hz interrupts (vector 0x50) for the system clock.
 *
 * Parallel port: Port A output, Port B input (printer stub).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>

#include <m68k/m68k.h>
#include <m68k/frame.h>
#include <machine/cpu.h>
#include <machine/vectors.h>
#include <machine/pitreg.h>

void pit_timer_init(void);
void pit_timer_start(void);
void pit_timer_intr(struct clockframe *);

static int pit_match(device_t, cfdata_t, void *);
static void pit_attach(device_t, device_t, void *);

CFATTACH_DECL_NEW(pit, 0,
    pit_match, pit_attach, NULL, NULL);

static int pit_attached;

static int
pit_match(device_t parent, cfdata_t cf, void *aux)
{
	return !pit_attached;
}

static void
pit_attach(device_t self, device_t parent, void *aux)
{
	volatile uint8_t *pit = PIT_REGS;
	extern char intrhand_autovec[];

	pit_attached = 1;

	/* Set Timer Interrupt Vector Register */
	pit[PIT_TIVR] = PIT_TIMER_VEC;

	/* Install interrupt handler at vector 0x50 */
	vec_set_entry(PIT_TIMER_VEC, intrhand_autovec);

	/* Configure parallel port: Port A = output, Port B = input */
	pit[PIT_PGCR] = 0x00;	/* Unidirectional mode */
	pit[PIT_PADDR] = 0xFF;	/* Port A all outputs */
	pit[PIT_PBDDR] = 0x00;	/* Port B all inputs */
	pit[PIT_PCDDR] = 0x00;	/* Port C all inputs */

	aprint_normal(": MC68230 PI/T, timer + parallel\n");
}

/*
 * Initialize PIT hardware and install interrupt vector.
 * Called early during boot, before cpu_initclocks().
 */
void
pit_timer_init(void)
{
	volatile uint8_t *pit = PIT_REGS;
	extern char intrhand_autovec[];

	/* Set Timer Interrupt Vector Register */
	pit[PIT_TIVR] = PIT_TIMER_VEC;

	/* Install interrupt handler at vector 0x50 */
	vec_set_entry(PIT_TIMER_VEC, intrhand_autovec);

	/* Configure parallel port: Port A = output, Port B = input */
	pit[PIT_PGCR] = 0x00;
	pit[PIT_PADDR] = 0xFF;
	pit[PIT_PBDDR] = 0x00;
	pit[PIT_PCDDR] = 0x00;

	printf("pit0: MC68230 PI/T, timer + parallel\n");
}

/*
 * Start the PIT timer at 100 Hz.
 * Called from cpu_initclocks().
 */
void
pit_timer_start(void)
{
	volatile uint8_t *pit = PIT_REGS;

	/* Load 24-bit preload value: 2500 = 0x0009C4 */
	pit[PIT_CPRH] = (PIT_PRELOAD >> 16) & 0xFF;
	pit[PIT_CPRM] = (PIT_PRELOAD >> 8) & 0xFF;
	pit[PIT_CPRL] = PIT_PRELOAD & 0xFF;

	/* Enable timer: CLK/32, zero detect interrupt, timer enable */
	pit[PIT_TCR] = TCR_ENABLE | TCR_CLK_32 | TCR_ZD_INT;
}

/*
 * PIT timer interrupt handler.
 * Called from intr_dispatch() when vector 0x50 is received.
 */
void
pit_timer_intr(struct clockframe *cf)
{
	volatile uint8_t *pit = PIT_REGS;
	static int pit_tick;

	/* Clear zero detect status (write 1 to clear) */
	pit[PIT_TSR] = TSR_ZDS;

	pit_tick++;

	/* hardclock handles timekeeping, softints, callouts */

	/* Call hardclock */
	hardclock(cf);
}
