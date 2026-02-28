/*	$NetBSD: mainbus.c,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $ */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

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
	aprint_normal("\n");
	
	/* Attach children */
	config_search(self, NULL,
	    CFARGS(.search = config_stdsubmatch));
}
