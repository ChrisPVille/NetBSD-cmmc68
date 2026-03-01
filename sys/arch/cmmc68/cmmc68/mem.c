/*	$NetBSD: mem.c,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $ */

/* Memory device stub */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

static int mem_attached;

static int
mem_match(device_t parent, cfdata_t cf, void *aux)
{
	return !mem_attached;
}

static void
mem_attach(device_t self, device_t parent, void *aux)
{
	mem_attached = 1;
	aprint_normal("\n");
}

CFATTACH_DECL_NEW(mem, 0, mem_match, mem_attach, NULL, NULL);
