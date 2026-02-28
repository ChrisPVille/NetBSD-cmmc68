/*
 * Minimal init for CMMC68
 *
 * Opens /dev/console, prints a banner, and execs /bin/sh.
 *
 * NetBSD m68k syscall convention:
 *   - D0 = syscall number
 *   - Arguments on user stack (after return address slot)
 *   - TRAP #0
 */

#include <sys/syscall.h>

/* open() flags from fcntl.h */
#define O_RDWR	0x0002

	.text
	.globl	_start
	.type	_start, @function
_start:
	/* open("/dev/console", O_RDWR) -> fd 0 (stdin) */
	move.l	#O_RDWR, %sp@-		/* arg2: flags */
	pea	devcon			/* arg1: path */
	clr.l	%sp@-			/* dummy return address slot */
	move.l	#SYS_open, %d0
	trap	#0
	lea	%sp@(12), %sp		/* pop 3 longs */

	/* dup(0) -> fd 1 (stdout) */
	clr.l	%sp@-			/* arg1: fd = 0 */
	clr.l	%sp@-			/* dummy return address slot */
	move.l	#SYS_dup, %d0
	trap	#0
	lea	%sp@(8), %sp		/* pop 2 longs */

	/* dup(0) -> fd 2 (stderr) */
	clr.l	%sp@-			/* arg1: fd = 0 */
	clr.l	%sp@-			/* dummy return address slot */
	move.l	#SYS_dup, %d0
	trap	#0
	lea	%sp@(8), %sp		/* pop 2 longs */

	/* write(1, banner, bannerlen) */
	move.l	#bannerend-banner, %sp@-	/* arg3: length */
	pea	banner				/* arg2: buffer */
	move.l	#1, %sp@-			/* arg1: fd (stdout) */
	clr.l	%sp@-				/* dummy return address slot */
	move.l	#SYS_write, %d0
	trap	#0
	lea	%sp@(16), %sp		/* pop 4 longs */

	/* execve("/bin/sh", argv, envp) */
	pea	envp			/* arg3: envp */
	pea	argv			/* arg2: argv */
	pea	sh_path			/* arg1: path */
	clr.l	%sp@-			/* dummy return address slot */
	move.l	#SYS_execve, %d0
	trap	#0
	lea	%sp@(16), %sp		/* pop 4 longs */

	/* If execve fails, loop forever */
halt_loop:
	/* write(2, errmsg, errmsglen) */
	move.l	#errmsgend-errmsg, %sp@-
	pea	errmsg
	move.l	#2, %sp@-
	clr.l	%sp@-
	move.l	#SYS_write, %d0
	trap	#0
	lea	%sp@(16), %sp

	/* exit(1) */
	move.l	#1, %sp@-
	clr.l	%sp@-
	move.l	#SYS_exit, %d0
	trap	#0
	bra.w	halt_loop

	.even
devcon:
	.asciz	"/dev/console"
	.even
sh_path:
	.asciz	"/bin/sh"
	.even
banner:
	.ascii	"\r\nNetBSD/cmmc68 init running\r\n"
bannerend:
	.even
errmsg:
	.ascii	"init: execve /bin/sh failed\r\n"
errmsgend:
	.even

/* argv for sh: {"sh", "-i", NULL} to force interactive mode */
	.even
argv:
	.long	sh_name
	.long	sh_iflag
	.long	0
sh_name:
	.asciz	"sh"
	.even
sh_iflag:
	.asciz	"-i"
	.even
/* envp: {"PATH=/bin:/sbin", "HOME=/", "TERM=vt100", NULL} */
envp:
	.long	envstr_path
	.long	envstr_home
	.long	envstr_term
	.long	0
envstr_path:
	.asciz	"PATH=/bin:/sbin"
	.even
envstr_home:
	.asciz	"HOME=/"
	.even
envstr_term:
	.asciz	"TERM=vt100"
	.even

/*
 * NetBSD ELF identification note.
 * Required for exec_elf to recognize this as a NetBSD binary.
 */
	.section ".note.netbsd.ident", "a"
	.p2align 2
	.long	7		/* namesz: "NetBSD\0" = 7 bytes */
	.long	4		/* descsz: 4 bytes version */
	.long	1		/* type: NT_NETBSD_IDENT */
	.ascii	"NetBSD\0"	/* name */
	.byte	0		/* pad to 4-byte alignment */
	.long	1100000000	/* desc: version 11.00.00.00 */
