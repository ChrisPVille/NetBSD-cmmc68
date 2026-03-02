/*	$NetBSD: machdep.c,v 1.1 2024/01/01 00:00:00 cmmc68 Exp $	*/

/*
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Machine-dependent code for CMMC68 (MC68010 with custom MMU)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/reboot.h>
#include <sys/exec.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/device.h>
#include <sys/conf.h>

#include <uvm/uvm_extern.h>

#include <machine/bootinfo.h>
#include <machine/vmparam.h>
#include <machine/frame.h>

#include <machine/cpu.h>

#include <dev/cons.h>

/*
 * Global symbols for kernel
 */
char machine[] = "cmmc68";
/* machine_arch is defined in m68k/m68k/m68k_machdep.c */

/*
 * CPU/MMU/FPU types (m68k standard)
 * These are also defined in locore.s, so we use 'extern' here
 */
extern int cputype;
extern int mmutype;
extern int fputype;

/*
 * VM map for physical memory
 */
struct vm_map *phys_map = NULL;

/*
 * CPU information for primary CPU
 */
struct cpu_info cpu_info_store;

/*
 * Fault information (used by trap handler)
 */
vaddr_t fault_addr;
int fault_code;

/* Console device wrappers for cn_tab */
extern int mfp_putc(int);
extern int mfp_getc(void);

static void cmmc68_cnputc(dev_t dev, int c) { mfp_putc(c); }
static int cmmc68_cngetc(dev_t dev) { return mfp_getc(); }
static void cmmc68_cnpollc(dev_t dev, int on) { }

static struct consdev cmmc68_consdev = {
	.cn_putc = cmmc68_cnputc,
	.cn_getc = cmmc68_cngetc,
	.cn_pollc = cmmc68_cnpollc,
	.cn_dev = 0,	/* Initialized to makedev(7,0) in consinit */
	.cn_pri = CN_REMOTE,
};

/*
 * Function prototypes
 */
psize_t physmem_size(void);
void initcpu(void);
void identifycpu(void);
void cpu_halt(void);
void cpu_reboot(int, char *);
void setregs(struct lwp *, struct exec_package *, vaddr_t);
u_int cpu_frequency(void);
int mm_md_physacc(paddr_t, vm_prot_t);
int mm_md_kernacc(void *, vm_prot_t, bool *);
void cpu_startup(void);
void consinit(void);
void cpu_dumpconf(void);
int cpu_exec_aout_makecmds(struct lwp *, struct exec_package *);
void machine_userret(struct lwp *);
int sys_sysarch(struct lwp *, void *, register_t *);
void straytrap(int, u_short);
void machine_init(paddr_t);

/*
 * badaddr — probe a word (16-bit) address, return 1 if bus error.
 * Used to detect optional hardware before accessing registers.
 */
int
badaddr(void *addr)
{
	int i;
	label_t faultbuf;

	nofault = (int *)&faultbuf;
	if (setjmp((label_t *)nofault)) {
		nofault = NULL;
		return 1;
	}
	i = *(volatile short *)addr;
	__USE(i);
	nofault = NULL;
	return 0;
}

/*
 * badbaddr — probe a byte (8-bit) address, return 1 if bus error.
 */
int
badbaddr(void *addr)
{
	int i;
	label_t faultbuf;

	nofault = (int *)&faultbuf;
	if (setjmp((label_t *)nofault)) {
		nofault = NULL;
		return 1;
	}
	i = *(volatile char *)addr;
	__USE(i);
	nofault = NULL;
	return 0;
}

/*
 * Boot information
 */
struct bootinfo bootinfo;

/*
 * CPU identification
 */
const char cpu_model[] = "MC68010";

/*
 * Startup data  
 */
extern char kernel_text[];
extern char etext[];
extern char edata[];
extern char end[];

/*
 * Available memory range
 */
int avail_start, avail_end;

/*
 * Initialize boot information
 */
void
bootinfo_init(void)
{
	bootinfo.bi_memsize = 0x400000;	/* 4 MB */
	bootinfo.bi_kernelstart = (uint32_t)kernel_text;
	bootinfo.bi_kernelend = (uint32_t)end;
}

/*
 * Stray trap handler
 */
void
straytrap(int pc, u_short evec)
{
        printf("unexpected trap (vector offset %x) from %x\n",
            evec & 0xFFF, pc);
}

/*
 * Identify CPU
 */
void
identifycpu(void)
{
        printf("CPU: %s\n", cpu_model);
        printf("  Clock: 8 MHz\n");
        printf("  MMU: Custom CMMC68 MMU\n");
}

/*
 * Halt the system
 */
void
cpu_halt(void)
{
        printf("System halted.\n");

        /* Disable interrupts and halt */
        __asm__ volatile("oriw #0x0700, %sr");
        for (;;)
                __asm__ volatile("stop #0x2700");
}

/*
 * System reboot
 *
 * The 'reset' instruction clears the MMU context register, putting
 * the MMU into bypass mode (VA = PA).  After that, code at kernel VA
 * (0x000XXXXX mapped to PA 0x400000+) is no longer reachable.
 *
 * Solution: write a small trampoline to monitor SRAM at 0xFF0000,
 * which is identity-mapped (VA = PA both with and without MMU).
 * The trampoline executes 'reset' then jumps to the bootloader
 * entry at PA 0xFE0200.
 */
void
cpu_reboot(int howto, char *bootstr)
{
        volatile uint16_t *tramp;

        if (howto & RB_HALT) {
                cpu_halt();
        }

        printf("Rebooting...\n");

        /* Disable interrupts */
        __asm__ volatile("oriw #0x0700, %%sr" ::: "cc");

        /*
         * Write trampoline to identity-mapped SRAM at 0xFF0000.
         *   0x4E70       reset
         *   0x4EF9       jmp abs.l
         *   0x00FE 0200  0x00FE0200 (bootloader entry)
         */
        tramp = (volatile uint16_t *)0xFF0000;
        tramp[0] = 0x4E70;     /* reset */
        tramp[1] = 0x4EF9;     /* jmp abs.l */
        tramp[2] = 0x00FE;     /* high word of 0x00FE0200 */
        tramp[3] = 0x0200;     /* low word of 0x00FE0200 */

        /* Jump to trampoline */
        __asm__ volatile("jmp 0xFF0000");

        /* NOTREACHED */
        for (;;);
}

/*
 * Set up registers for exec
 * Note: This is implemented in m68k/m68k/m68k_machdep.c
 */

/*
 * CPU frequency
 */
u_int
cpu_frequency(void)
{
        return 8000000;  /* 8 MHz */
}

/*
 * Check physical memory access
 */
int
mm_md_physacc(paddr_t pa, vm_prot_t prot)
{
        /* Allow access to all physical memory for now */
        /* TODO: Add proper bounds checking */
        return 0;
}

/*
 * Check kernel memory access
 */
int
mm_md_kernacc(void *ptr, vm_prot_t prot, bool *handled)
{
        /* Allow access to all kernel memory for now */
        /* TODO: Add proper bounds checking */
        *handled = true;
        return 0;
}

/*
 * Write to process memory (for ptrace)
 */
void
pmap_procwr(struct proc *p, vaddr_t va, size_t len)
{
        /* TODO: Implement cache flush after write */
}

/*
 * CPU startup (called from init_main.c)
 */
void
cpu_startup(void)
{
	/*
	 * Reduce softint memory from 32KB to 8KB.
	 * Safe because cpu_startup() runs before softint_init().
	 */
	extern u_int softint_bytes;
	softint_bytes = 8192;

        printf("cpu_startup: CMMC68 kernel starting\n");
        printf("  kernel_pa_offset=0x%lx virtual_avail=0x%lx virtual_end=0x%lx\n",
            (unsigned long)kernel_pa_offset,
            (unsigned long)virtual_avail,
            (unsigned long)virtual_end);
        printf("  physmem=%d avail=%d\n", (int)physmem, (int)uvmexp.free);

        identifycpu();

        /* HACK: MMU rev1 EX bit bug workaround active */
        printf("  HACK: MMU rev1 — PTE_EX forced on all pages (no NX)\n");

        printf("  text=%lu data=%lu bss=%lu\n",
            (unsigned long)(etext - kernel_text),
            (unsigned long)(edata - etext),
            (unsigned long)(end - edata));
        printf("Initializing kernel subsystems...\n");
}

/*
 * Initialize console (called from init_main.c)
 */
void
consinit(void)
{
	/* com0 is at cdevsw index 7, unit 0 */
	cmmc68_consdev.cn_dev = makedev(7, 0);
	cn_set_tab(&cmmc68_consdev);
}

/*
 * Configure dump device
 */
void
cpu_dumpconf(void)
{
        /* TODO: Configure dump device */
}

/*
 * Execute a.out binary (legacy)
 */
int
cpu_exec_aout_makecmds(struct lwp *l, struct exec_package *e)
{
        /* Not supported */
        return ENOEXEC;
}

/*
 * Return to user mode
 */
void
machine_userret(struct lwp *l)
{
        /* Nothing needed for CMMC68 */
}

/*
 * Machine-specific initialization called from locore.s after MMU enabled
 * This is called with the next physical address after kernel
 */
void
machine_init(paddr_t nextpa)
{
	/* Initialize boot information */
	bootinfo_init();
}
