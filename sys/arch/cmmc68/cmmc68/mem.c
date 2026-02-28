/*	$NetBSD: mem.c,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $ */

/* Memory device stub */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

void mem_attach(device_t, device_t, void *);

CFATTACH_DECL_NEW(mem, 0, NULL, mem_attach, NULL, NULL);

void
mem_attach(device_t parent, device_t self, void *aux)
{
	aprint_normal("\n");
}
