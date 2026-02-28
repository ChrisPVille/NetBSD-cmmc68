/*	$NetBSD: stubs.c,v 1.3 2025/02/16 00:00:00 cmmc68 Exp $	*/

/*
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * CMMC68 kernel stubs for missing symbols
 * 
 * Only functions that don't have declarations in system headers
 * and aren't implemented elsewhere should be here.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <machine/cpu.h>
#include <machine/vectors.h>

/*
 * BUS SPACE FUNCTIONS - Direct mapping for CMMC68
 */

int
bus_space_map(bus_space_tag_t t, bus_addr_t bpa, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	*bshp = (bus_space_handle_t)bpa;
	return 0;
}

void
bus_space_unmap(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t size)
{
}

/*
 * Console table (needed by com.c)
 * Note: cn_tab is also defined in dev/cons.c, so we don't define it here
 */
extern struct consdev *cn_tab;

/*
 * Note: kernel_text is defined in locore.s
 * Note: _delay is implemented in clock.c
 * Note: vec_init is now provided by m68k/m68k/vectors.c
 */

