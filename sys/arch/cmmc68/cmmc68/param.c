/*	$NetBSD: param.c,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $	*/

/*
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * CMMC68-specific system parameter definitions.
 * This provides the global variables that the NetBSD kernel expects.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/callout.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/utsname.h>
#include <sys/lwp.h>
#include <machine/vmparam.h>

/*
 * Clock parameters - CMMC68 uses 64Hz for MFP timer
 */
#ifndef HZ
#define HZ 64
#endif

int	hz = HZ;
int	tick = 1000000 / HZ;	/* microseconds per tick */
int	tickadj = 1;		/* tick adjust value */
int	rtc_offset = 0;		/* RTC offset from GMT */

/*
 * Process/thread limits
 * Use hardcoded values since MAXUSERS might not be defined yet
 * For small memory systems (4MB), reduce MAXEXEC to avoid exec_map
 * being too large (MAXEXEC * NCARGS = MAXEXEC * 256KB)
 */
#ifndef MAXEXEC
#define MAXEXEC 1	/* Only 1 concurrent exec to minimize exec_map size */
#endif

int	maxproc = 128;		/* max processes */
int	maxlwp = 512;		/* max LWPs */
u_int	maxfiles = 512;		/* max open files */
int	maxexec = MAXEXEC;	/* max concurrent exec() calls */

/*
 * For single processor
 */
u_int	maxcpus = 1;
size_t	coherency_unit = ALIGNBYTES + 1;

/*
 * Vnode cache
 */
int	desiredvnodes = 256;

/*
 * Fscale for load average calculations
 */
int	fscale = FSCALE;

/*
 * Network mbuf parameters
 */
int	nmbclusters = 0;

#ifndef MBLOWAT
#define MBLOWAT 16
#endif
int	mblowat = MBLOWAT;

#ifndef MCLLOWAT
#define MCLLOWAT 8
#endif
int	mcllowat = MCLLOWAT;

/*
 * Actual network mbuf sizes (read-only), for netstat.
 */
const int msize = MSIZE;
const int mclbytes = MCLBYTES;

/*
 * Socket buffer limits
 */
#ifndef SB_MAX
#define SB_MAX (256 * 1024)	/* default max socket buffer */
#endif
u_long	sb_max = SB_MAX;

/*
 * Machine and architecture names
 * These are declared in kern/kern_uname.c, just reference them
 */
