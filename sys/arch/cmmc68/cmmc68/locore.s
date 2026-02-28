/*      $NetBSD: locore.s,v 1.2 2024/01/01 00:00:00 cmmc68 Exp $      */

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1980, 1990, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * from: Utah $Hdr: locore.s 1.66 92/12/22$
 *
 *      @(#)locore.s    8.6 (Berkeley) 5/27/94
 */

/*
 * Low-level startup code for CMMC68 (MC68010 with custom MMU)
 * Adapted from virt68k locore.s
 */

#include "opt_compat_netbsd.h"
#include "opt_compat_sunos.h"
#include "opt_ddb.h"
#include "opt_kgdb.h"
#include "opt_lockdebug.h"
#include "opt_m68k_arch.h"

#include "assym.h"
#include <machine/asm.h>
#include <machine/trap.h>

/*
 * CMMC68 Memory Map:
 *   VA 0x000000-0x3FFFFF  Kernel (mapped to PA 0x400000 by bootloader)
 *   VA 0x400000-0xDFFFFF  User space
 *   VA 0xFD0000-0xFFFFFF  MMU/devices/ROM (identity mapped)
 */

/*
 * Temporary stack for a variety of purposes.
 * Try and make this the first thing in the data segment so it
 * is page aligned.
 */
	.data
	.space	PAGE_SIZE
ASLOCAL(tmpstk)

/*
 * Macro to relocate a symbol, used before MMU is enabled.
 */
#define IMMEDIATE		#
#define _RELOC(var, ar)			\
	movl	IMMEDIATE var,ar;	\
	addl	%a5,ar

#define RELOC(var, ar)		_RELOC(_C_LABEL(var), ar)
#define ASRELOC(var, ar)	_RELOC(_ASM_LABEL(var), ar)

BSS(esym,4)

	.globl	_C_LABEL(edata)
	.globl	_C_LABEL(etext),_C_LABEL(end)

/*
 * This is for kvm_mkdb, and should be the address of the beginning
 * of the kernel text segment (not necessarily the same as kernbase).
 */
	.text
GLOBAL(kernel_text)

/*
 * start of kernel and .text!
 */
ASENTRY_NOPROFILE(start)
	movw	#PSL_HIGHIPL,%sr	| no interrupts

	/*
	 * On CMMC68, the kernel is running at virtual 0x0 with MMU already enabled
	 * by bootloader. The kernel is loaded at physical 0x400000.
	 * Since bootloader mapped VA 0x0 -> PA 0x400000, no relocation needed.
	 * The relocation offset is 0.
	 */
	movl	#0, %a5		| relocation offset is 0 (running at linked address)

	ASRELOC(tmpstk, %a0)
	movl	%a0,%sp			| give ourselves a temporary stack

	RELOC(edata,%a0)		| clear out BSS
	movl	#_C_LABEL(end) - 4, %d0 | (must be <= 256 kB)
	subl	#_C_LABEL(edata), %d0
	lsrl	#2,%d0
1:	clrl	%a0@+
	dbra	%d0,1b

	/*
	 * Zero all free RAM from end of kernel to end of VA space.
	 * Pool/UVM page allocations come from this memory and expect
	 * it to be zeroed (no pmap_zero_page in pool_page_alloc path).
	 * Can't use dbra — count exceeds 16 bits.
	 */
	movl	#_C_LABEL(end),%a0	| start at end of kernel
	movl	#0x003FFFFC,%d0		| last long in kernel VA space
	subl	#_C_LABEL(end),%d0	| bytes to zero
	lsrl	#2,%d0			| convert to longs
2:	clrl	%a0@+
	subql	#1,%d0
	bne	2b

	/*
	 * Save the end of loaded kernel for pmap bootstrap.
	 * Read the MMU PTE for VPN 0 to determine kernel PA offset
	 * dynamically.  This supports both ROM mode (offset 0x400000)
	 * and packed RAM mode (offset 0x401000).
	 */
	movl	#_C_LABEL(end),%d7	| virtual address of end of kernel
	clrl	%d6
	movw	0xFD2000,%d6		| read PTE[0] — PPN in bits 11:0
	andl	#0x0FFF,%d6		| mask to PPN only
	lsll	#8,%d6			| shift left 8
	lsll	#4,%d6			| shift left 4 more (total << 12)
	addl	%d6,%d7			| PA = VA + kernel_pa_offset

	/* NOTE: %d7 is now off-limits!! */

	/*
	 * Initialize the custom MMU for identity mapping.
	 * On CMMC68, the MMU is a simple custom design.
	 */
	/* Call pmap_bootstrap1 to set up the MMU */
	pea	%a5@			| reloff (should be 0)
	movl	%d7,%sp@-		| nextpa
	RELOC(pmap_bootstrap1,%a0)
	jbsr	%a0@			| pmap_bootstrap1(nextpa, reloff)
	addql	#8,%sp

	/* Updated nextpa returned in %d0 */
	movl	%d0, %d7

/*
 * Enable the MMU.
 * On CMMC68, MMU is already enabled by bootloader, skip the standard enable code.
 * The bootloader has already set up VA 0x0 -> PA 0x400000 mapping.
 */
	| Skip MMU enable since bootloader already did it

/*
 * Should be running mapped from this point on
 */
	Lmmuenabled:
	ASRELOC(tmpstk,%a0)
	movl	%a0,%sp			| re-load the temporary stack

	RELOC(vec_init,%a0)
	jbsr	%a0@			| initialize the vector table

/* phase 2 of pmap setup, returns pointer to lwp0 uarea in %a0 */
	RELOC(pmap_bootstrap2,%a0)
	jbsr	%a0@

/* set kernel stack, user SP */
	lea	%a0@(USPACE-4),%sp	| set kernel stack to end of area
	movl	#USRSTACK-4,%a2
	movl	%a2,%usp		| init user SP

	tstl	_C_LABEL(fputype)	| Have an FPU?
	jeq	1f			| No, skip.
	clrl	%a0@(PCB_FPCTX)		| ensure null FP context
	pea	%a0@(PCB_FPCTX)
	RELOC(m68881_restore,%a1)	| restore it (does not kill %a0)
	jbsr	%a1@
	addql	#4,%sp
1:
	/* CMMC68 uses 68010, no cache control needed */
	| 	movl	#CACHE_ON,%d0
	| 	movc	%d0,%cacr		| clear cache(s)
/* final setup for C code */
	movl	%d7,%sp@-		| push nextpa saved above
	RELOC(machine_init,%a0)
	jbsr	%a0@			| additional pre-main initialization
	addql	#4,%sp
/*
 * Create a fake exception frame so that cpu_lwp_fork() can copy it.
 * main() never returns; we exit to user mode from a forked process
 * later on.
 */
	clrw	%sp@-			| vector offset/frame type
	clrl	%sp@-			| PC - filled in by "execve"
	movw	#PSL_USER,%sp@-		| in user mode
	clrl	%sp@-			| stack adjust count and padding
	lea	%sp@(-64),%sp		| construct space for D0-D7/A0-A7
	lea	_C_LABEL(lwp0),%a0	| save pointer to frame
	movl	%sp,%a0@(L_MD_REGS)	|   in lwp0.l_md.md_regs

	jra	_C_LABEL(main)		| main()

/*
 * MC68010-specific bus error and address error handlers.
 * These are required for vec_init() to work properly.
 * Based on sun2 implementation (also MC68010).
 */
 
/*
 * Bus Error Handler (Vector 2)
 * MC68010 uses long format exception stack frame (29 words).
 */
ENTRY_NOPROFILE(buserr)
	clrl	%sp@-			| stack adjust count
	moveml	#0xFFFF,%sp@-		| save user registers
	movl	%usp,%a0		| save the user SP
	movl	%a0,%sp@(FR_SP)		|   in the savearea
	lea	%sp@(FR_HW),%a1		| grab base of HW berr frame
	moveq	#0,%d0
	movw	%a1@(8),%d0		| grab SSW for fault processing
	movl	%a1@(10),%d1		| fault address is as given in frame
	movl	%d1,%sp@-		| push fault VA
	movl	%d0,%sp@-		| and padded SSW
	movw	%a1@(6),%d0		| get frame format/vector offset
	andw	#0x0FFF,%d0		| clear out frame format
	cmpw	#12,%d0			| address error vector?
	jeq	Lisaerr			| yes, go to it

	/*
	 * CMMC68 specific: Check if this is an MMU fault or bus error.
	 * For now, treat all as bus errors until MMU driver is complete.
	 */
Lisberr:
	movl	#T_BUSERR,%sp@-		| mark bus error
	jra	_ASM_LABEL(faultstkadj)	| and deal with it

/*
 * Address Error Handler (Vector 3)
 */
ENTRY_NOPROFILE(addrerr)
	clrl	%sp@-			| stack adjust count
	moveml	#0xFFFF,%sp@-		| save user registers
	movl	%usp,%a0		| save the user SP
	movl	%a0,%sp@(FR_SP)		|   in the savearea
	lea	%sp@(FR_HW),%a1		| grab base of HW berr frame
	moveq	#0,%d0
	movw	%a1@(8),%d0		| grab SSW for fault processing
	movl	%a1@(10),%d1		| fault address is as given in frame
	movl	%d1,%sp@-		| push fault VA
	movl	%d0,%sp@-		| and padded SSW
Lisaerr:
	movl	#T_ADDRERR,%sp@-	| mark address error
	jra	_ASM_LABEL(faultstkadj)	| and deal with it

/*
 * Trap/interrupt vector routines
 */
#include <m68k/m68k/trap_subr.s>

/*
 * Don't use common m68k bus error handlers - they don't support MC68010.
 * We've provided our own above.
 */

/*
 * FP exceptions.
 * For MC68010 with external FPU (68881/68882), we use simple handlers.
 */
ENTRY_NOPROFILE(fpfline)
	jra	_C_LABEL(illinst)

ENTRY_NOPROFILE(fpunsupp)
	jra	_C_LABEL(illinst)

/*
 * Handles all other FP coprocessor exceptions.
 */
ENTRY_NOPROFILE(fpfault)
	clrl	%sp@-			| stack adjust count
	moveml	#0xFFFF,%sp@-		| save user registers
	movl	%usp,%a0		| and save
	movl	%a0,%sp@(FR_SP)		|   the user stack pointer
	clrl	%sp@-			| no VA arg
	movl	_C_LABEL(curpcb),%a0	| current pcb
	lea	%a0@(PCB_FPCTX),%a0	| address of FP savearea
	| NOTE: MC68010 has no FPU, using soft-float
	| fsave/fmovem/frestore not available on 68010
	clrw	%sp@-			| push dummy fpsr as code argument
	movl	#T_FPERR,%sp@-		| push type arg
	jra	_ASM_LABEL(faultstkadj) | call trap and deal with stack cleanup


/*
 * Other exceptions only cause four and six word stack frame and require
 * no post-trap stack adjustment.
 */

ENTRY_NOPROFILE(badtrap)
	moveml	#0xC0C0,%sp@-		| save scratch regs
	movw	%sp@(22),%sp@-		| push exception vector info
	clrw	%sp@-
	movl	%sp@(22),%sp@-		| and PC
	jbsr	_C_LABEL(straytrap)	| report
	addql	#8,%sp			| pop args
	moveml	%sp@+,#0x0303		| restore regs
	jra	_ASM_LABEL(rei)		| all done

ENTRY_NOPROFILE(trap0)
	clrl	%sp@-			| stack adjust count
	moveml	#0xFFFF,%sp@-		| save user registers
	movl	%usp,%a0		| save the user SP
	movl	%a0,%sp@(FR_SP)		|   in the savearea
	movl	%d0,%sp@-		| push syscall number
	jbsr	_C_LABEL(syscall)	| handle it
	addql	#4,%sp			| pop syscall arg
	tstl	_C_LABEL(astpending)	| AST pending?
	jne	Lrei1			| Yup, go deal with it.
	movl	%sp@(FR_SP),%a0		| grab and restore
	movl	%a0,%usp		|   user SP
	moveml	%sp@+,#0x7FFF		| restore most registers
	addql	#8,%sp			| pop SP and stack adjust
	rte

/*
 * Trap 12 is the entry point for the cachectl "syscall" (both HPUX & BSD)
 *	cachectl(command, addr, length)
 * command in d0, addr in a1, length in d1
 */
ENTRY_NOPROFILE(trap12)
	movl	_C_LABEL(curlwp),%a0
	movl	%a0@(L_PROC),%sp@-	| push current proc pointer
	movl	%d1,%sp@-		| push length
	movl	%a1,%sp@-		| push addr
	movl	%d0,%sp@-		| push command
	jbsr	_C_LABEL(cachectl1)	| do it
	lea	%sp@(16),%sp		| pop args
	jra	_ASM_LABEL(rei)		| all done

/*
 * Trace (single-step) trap.  Kernel-mode is special.
 * User mode traps are simply passed on to trap().
 */
ENTRY_NOPROFILE(trace)
	clrl	%sp@-			| stack adjust count
	moveml	#0xFFFF,%sp@-
	moveq	#T_TRACE,%d0

	| Check PSW and see what happen.
	|   T=0 S=0     (should not happen)
	|   T=1 S=0     trace trap from user mode
	|   T=0 S=1     trace trap on a trap instruction
	|   T=1 S=1     trace trap from system mode (kernel breakpoint)

	movw	%sp@(FR_HW),%d1		| get PSW
	notw	%d1			| XXX no support for T0 on 680[234]0
	andw	#PSL_TS,%d1		| from system mode (T=1, S=1)?
	jeq	Lkbrkpt			| yes, kernel breakpoint
	jra	_ASM_LABEL(fault)	| no, user-mode fault

/*
 * Trap 15 is used for:
 *	- GDB breakpoints (in user programs)
 *	- KGDB breakpoints (in the kernel)
 *	- trace traps for SUN binaries (not fully supported yet)
 * User mode traps are simply passed to trap().
 */
ENTRY_NOPROFILE(trap15)
	clrl	%sp@-			| stack adjust count
	moveml	#0xFFFF,%sp@-
	moveq	#T_TRAP15,%d0
	movw	%sp@(FR_HW),%d1		| get PSW
	andw	#PSL_S,%d1		| from system mode?
	jne	Lkbrkpt			| yes, kernel breakpoint
	jra	_ASM_LABEL(fault)	| no, user-mode fault

Lkbrkpt: | Kernel-mode breakpoint or trace trap. (d0=trap_type)
	| Save the system sp rather than the user sp.
	movw	#PSL_HIGHIPL,%sr	| lock out interrupts
	lea	%sp@(FR_SIZE),%a6	| Save stack pointer
	movl	%a6,%sp@(FR_SP)		|  from before trap

	| If were are not on tmpstk switch to it.
	| (so debugger can change the stack pointer)
	movl	%a6,%d1
	cmpl	#_ASM_LABEL(tmpstk),%d1
	jls	Lbrkpt2			| already on tmpstk
	| Copy frame to the temporary stack
	movl	%sp,%a0			| a0=src
	lea	_ASM_LABEL(tmpstk)-96,%a1 | a1=dst
	movl	%a1,%sp			| sp=new frame
	movql	#FR_SIZE,%d1
Lbrkpt1:
	movl	%a0@+,%a1@+
	subql	#4,%d1
	jbgt	Lbrkpt1

Lbrkpt2:
	| Call the trap handler for the kernel debugger.
	| Do not call trap() to do it, so that we can
	| set breakpoints in trap() if we want.  We know
	| the trap type is either T_TRACE or T_BREAKPOINT.
	| If we have both DDB and KGDB, let KGDB see it first,
	| because KGDB will just return 0 if not connected.
	| Save args in d2, a2
	movl	%d0,%d2			| trap type
	movl	%sp,%a2			| frame ptr
#ifdef KGDB
	| Let KGDB handle it (if connected)
	movl	%a2,%sp@-		| push frame ptr
	movl	%d2,%sp@-		| push trap type
	jbsr	_C_LABEL(kgdb_trap)	| handle the trap
	addql	#8,%sp			| pop args
	cmpl	#0,%d0			| did kgdb handle it?
	jne	Lbrkpt3			| yes, done
#endif
#ifdef DDB
	| Let DDB handle it
	movl	%a2,%sp@-		| push frame ptr
	movl	%d2,%sp@-		| push trap type
	jbsr	_C_LABEL(kdb_trap)	| handle the trap
	addql	#8,%sp			| pop args
#endif
Lbrkpt3:
	| The stack pointer may have been modified, or
	| data below it modified (by kgdb push call),
	| so push the hardware frame at the current sp
	| before restoring registers and returning.

	movl	%sp@(FR_SP),%a0		| modified sp
	lea	%sp@(FR_SIZE),%a1	| end of our frame
	movl	%a1@-,%a0@-		| copy 2 longs with
	movl	%a1@-,%a0@-		| ... predecrement
	movl	%a0,%sp@(FR_SP)		| sp = h/w frame
	moveml	%sp@+,#0x7FFF		| restore all but sp
	movl	%sp@,%sp		| ... and sp
	rte				| all done

/*
 * Interrupt handlers.
 *
 * For auto-vectored interrupts, the CPU provides the
 * vector 0x18+level.
 *
 * intrhand_autovec is the entry point for auto-vectored
 * interrupts.
 */

ENTRY_NOPROFILE(intrhand_autovec)
	addql	#1,_C_LABEL(intr_depth)
	INTERRUPT_SAVEREG
	jbsr	_C_LABEL(intr_dispatch)	| call dispatcher
	INTERRUPT_RESTOREREG
	subql	#1,_C_LABEL(intr_depth)

	/* FALLTHROUGH to rei */

/*
 * Emulation of VAX REI instruction.
 *
 * This code deals with checking for and servicing ASTs
 * (profiling, scheduling).
 * After identifying that we need an AST we drop the IPL to allow device
 * interrupts.
 *
 * This code is complicated by the fact that sendsig may have been called
 * necessitating a stack cleanup.
 */
ASENTRY_NOPROFILE(rei)
	tstl	_C_LABEL(astpending)	| AST pending?
	jeq	Ldorte			| Nope. Just return.
	btst	#5,%sp@			| Returning to kernel mode?
	jne	Ldorte			| Yup. Can't do ASTs
	movw	#PSL_LOWIPL,%sr		| lower SPL
	clrl	%sp@-			| stack adjust
	moveml	#0xFFFF,%sp@-		| save all registers
	movl	%usp,%a1		| including
	movl	%a1,%sp@(FR_SP)		|    the users SP
Lrei1:	clrl	%sp@-			| VA == none
	clrl	%sp@-			| code == none
	movl	#T_ASTFLT,%sp@-		| type == async system trap
	pea	%sp@(12)		| fp == address of trap frame
	jbsr	_C_LABEL(trap)		| go handle it
	lea	%sp@(16),%sp		| pop value args
	movl	%sp@(FR_SP),%a0		| restore user SP
	movl	%a0,%usp		|   from save area
	movw	%sp@(FR_ADJ),%d0	| need to adjust stack?
	jne	Laststkadj		| yes, go to it
	moveml	%sp@+,#0x7FFF		| no, restore most user regs
	addql	#8,%sp			| toss SP and stack adjust
Ldorte:	rte				| and do real RTE

Laststkadj:
	lea	%sp@(FR_HW),%a1		| pointer to HW frame
	addql	#8,%a1			| source pointer
	movl	%a1,%a0			| source
	addw	%d0,%a0			|  + hole size = dest pointer
	movl	%a1@-,%a0@-		| copy
	movl	%a1@-,%a0@-		|  8 bytes
	movl	%a0,%sp@(FR_SP)		| new SSP
	moveml	%sp@+,#0x7FFF		| restore user registers
	movl	%sp@,%sp		| and our SP
	rte				| and do real RTE

/*
 * Primitives
 */

/*
 * Use common m68k process/lwp switch and context save subroutines.
 * NOTE: MC68010 has no FPU, so don't define FPCOPROC
 */
#include <m68k/m68k/switch_subr.s>

ENTRY(ecacheon)
	rts

ENTRY(ecacheoff)
	rts

/*
 * MC68010 has no FPU - provide stub functions
 */
ENTRY(m68881_save)
	rts

ENTRY(m68881_restore)
	rts

/*
 * Misc. global variables.
 */
	.data

GLOBAL(mmutype)
	.long	MMU_68851	| MC68010 with external MMU

GLOBAL(cputype)
	.long	CPU_68010	| MC68010

GLOBAL(fputype)
	.long	0	| FPU_NONE - No FPU, 68010 has no coprocessor interface
