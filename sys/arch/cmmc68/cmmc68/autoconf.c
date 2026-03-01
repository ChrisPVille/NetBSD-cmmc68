/*	$NetBSD: autoconf.c,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $	*/

/*
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 */

/*
 * Device autoconfiguration for CMMC68 - minimal version
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/conf.h>

#include <machine/cpu.h>
#include <m68k/m68k.h>

/*
 * Configure all devices on system
 */
void
cpu_configure(void)
{
	/* Run device autoconfiguration */
	printf("Device autoconfiguration...\n");

	if (config_rootfound("mainbus", NULL) == NULL)
		panic("autoconfig failed, no root");

	/* Enable interrupts */
	spl0();
	printf("Interrupts enabled.\n");
}

/*
 * Root device setup
 */
void
cpu_rootconf(void)
{
	printf("Mounting root filesystem...\n");
	rootconf();
}


