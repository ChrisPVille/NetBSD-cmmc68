/*	$NetBSD: com_mfp.c,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $ */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

void com_mfp_attach(device_t, device_t, void *);

CFATTACH_DECL_NEW(com_mfp, 0, NULL, com_mfp_attach, NULL, NULL);

void
com_mfp_attach(device_t parent, device_t self, void *aux)
{
	aprint_normal("\n");
}
