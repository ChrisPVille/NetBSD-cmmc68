/*$NetBSD: trap.c,v 1.3 2024/01/01 00:00:00 cmmc68 Exp $*/

/*
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Trap handling for CMMC68 (MC68010)
 * Based on sun2/trap.c
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/cpu.h>
#include <sys/errno.h>
#include <sys/userret.h>

#include <machine/frame.h>
#include <machine/trap.h>
#include <machine/pcb.h>
#include <machine/psl.h>
#include <machine/vmparam.h>

#include <m68k/frame.h>
#include <m68k/fcode.h>

#include <uvm/uvm_extern.h>

/*
 * nofault — set by badaddr()/badbaddr() to recover from bus errors.
 * When non-NULL, the bus error handler longjmps here instead of panicking.
 */
int *nofault;

/*
 * Function prototypes
 */
/* buserr and addrerr are defined in locore.s */
/* illinst is defined in m68k/m68k/trap_subr.s */
void diverr(struct trapframe *);
void chkerr(struct trapframe *);
void trapverr(struct trapframe *);
void priverr(struct trapframe *);
void trace_handler(struct trapframe *);
void linea(struct trapframe *);
void linef(struct trapframe *);
void fmt(struct trapframe *);
void uninitint(struct trapframe *);
void spuriousint(struct trapframe *);
void nmi(struct trapframe *);

/*
 * Main trap function — called from faultstkadj in trap_subr.s.
 * Signature: trap(struct frame *fp, int type, u_int code, u_int v)
 */
void trap(struct frame *, int, u_int, u_int);

/*
 * MC68010 SSW helpers
 */
#define	KDFAULT(c)	(((c) & (SSW1_IF|SSW1_FCMASK)) == (FC_SUPERD))
/*
 * WRFAULT: true if the SSW indicates a data write fault.
 * MC68010 SSW: RW=0 means write, RW=1 means read.
 * DF (data fault) is always set for data accesses, so we must NOT
 * include it in the check (the sun2 original used reformatted code).
 */
#define	WRFAULT(c)	(((c) & (SSW1_IF|SSW1_RW)) == 0)

/*
 * Exception frame sizes for MC68010
 * This array is indexed by the exception frame format field.
 */
short exframesize[] = {
	0,		/* type 0 - normal (4 words) */
	0,		/* type 1 - throwaway (68020+) */
	0,		/* type 2 - normal 6-word (68020+) */
	0,		/* type 3 - FP post-instruction (68040) */
	0,		/* type 4 - access error (68060) */
	-1, -1,		/* type 5-6 - undefined */
	0,		/* type 7 - access error (68040) */
	58,		/* type 8 - bus fault (68010) */
	0,		/* type 9 - coprocessor mid-instruction (68020) */
	0,		/* type A - short bus fault (68020) */
	0,		/* type B - long bus fault (68020) */
	-1, -1, -1, -1	/* type C-F - undefined */
};

/*
 * Trap handling stubs for CMMC68
 */

/* void illinst(struct trapframe *tf); */

void
diverr(struct trapframe *tf)
{
	printf("Divide error at PC=%08X\n", tf->tf_pc);
	panic("divide error");
}

void
chkerr(struct trapframe *tf)
{
	printf("CHK error at PC=%08X\n", tf->tf_pc);
	panic("CHK error");
}

void
trapverr(struct trapframe *tf)
{
	printf("TRAPV error at PC=%08X\n", tf->tf_pc);
	panic("TRAPV error");
}

void
priverr(struct trapframe *tf)
{
	printf("Privilege violation at PC=%08X\n", tf->tf_pc);
	panic("privilege violation");
}

void
trace_handler(struct trapframe *tf)
{
	printf("Trace at PC=%08X\n", tf->tf_pc);
}

void
linea(struct trapframe *tf)
{
	printf("Line A at PC=%08X\n", tf->tf_pc);
	panic("Line A");
}

void
linef(struct trapframe *tf)
{
	printf("Line F at PC=%08X\n", tf->tf_pc);
	panic("Line F");
}

void
fmt(struct trapframe *tf)
{
	printf("Format error at PC=%08X\n", tf->tf_pc);
	panic("format error");
}

void
uninitint(struct trapframe *tf)
{
	printf("Uninitialized interrupt\n");
}

void
spuriousint(struct trapframe *tf)
{
	printf("Spurious interrupt\n");
}

void
nmi(struct trapframe *tf)
{
	printf("NMI at PC=%08X\n", tf->tf_pc);
	panic("NMI");
}


/*
 * Main trap function (called from faultstkadj in trap_subr.s)
 *
 * Called with: trap(fp, type, code, v)
 *   fp   - pointer to struct frame on stack
 *   type - trap type (T_BUSERR, T_ADDRERR, etc.)
 *   code - SSW (Special Status Word) or 0
 *   v    - fault virtual address or 0
 */
void
trap(struct frame *fp, int type, u_int code, u_int v)
{
	struct trapframe *tf = &fp->F_t;
	struct lwp *l;
	struct proc *p;
	struct pcb *pcb;
	void *onfault;
	int rv;
	ksiginfo_t ksi;

	curcpu()->ci_data.cpu_ntrap++;
	l = curlwp;
	p = l->l_proc;
	pcb = lwp_getpcb(l);
	onfault = pcb->pcb_onfault;

	if (USERMODE(tf->tf_sr))
		type |= T_USER;

	KSI_INIT_TRAP(&ksi);
	ksi.ksi_trap = type & ~T_USER;

	switch (type) {
	default:
	dopanic:
		printf("trap type %d, code = 0x%x, v = 0x%x\n", type, code, v);
		printf("%s program counter = 0x%x\n",
		    (type & T_USER) ? "user" : "kernel", tf->tf_pc);
		printf("curlwp=%p onfault=%p\n", (void *)curlwp, onfault);
		{
			/* Walk frame pointer chain for stack trace */
			u_int *cfp;
			__asm__ volatile("movl %%a6,%0" : "=r"(cfp));
			int depth;
			for (depth = 0; depth < 15 && cfp != NULL; depth++) {
				u_int *nextfp = (u_int *)*cfp;
				u_int retaddr = cfp[1];
				printf("  frame[%d]: fp=%p ret=0x%x\n",
				    depth, (void *)cfp, retaddr);
				if ((vaddr_t)nextfp < 0x1000 ||
				    (vaddr_t)nextfp > 0x3FFFFF)
					break;
				cfp = nextfp;
			}
		}
		panic("trap");
		/* NOTREACHED */

	case T_BUSERR:		/* kernel bus error */
		/*
		 * Check for badaddr()/badbaddr() probe.
		 * If nofault is set, longjmp back to the probe caller.
		 */
		if (nofault) {
			longjmp((label_t *)nofault);
			/* NOTREACHED */
		}

		/*
		 * If the fault address is in user space, try uvm_fault
		 * to demand-page the mapping (e.g. during copyout).
		 */
		if (v >= VM_MIN_USER_ADDRESS && v < VM_MAX_USER_ADDRESS) {
			vaddr_t va = trunc_page(v);
			struct vmspace *vm = p->p_vmspace;
			struct vm_map *map = &vm->vm_map;
			vm_prot_t ftype;

			if (WRFAULT(code))
				ftype = VM_PROT_WRITE;
			else
				ftype = VM_PROT_READ;

			pcb->pcb_onfault = NULL;
			rv = uvm_fault(map, va, ftype);
			pcb->pcb_onfault = onfault;

			if (rv == 0) {
				return;	/* retry the faulting instruction */
			}
		}

		/*
		 * Try kernel-space fault (e.g., UBC window page-in).
		 * The UBC read path relies on page faults to trigger
		 * genfs_getpages when file data needs loading.
		 */
		if (v < VM_MAX_KERNEL_ADDRESS) {
			vaddr_t va = trunc_page(v);
			vm_prot_t ftype;

			if (WRFAULT(code))
				ftype = VM_PROT_WRITE;
			else
				ftype = VM_PROT_READ;

			pcb->pcb_onfault = NULL;
			rv = uvm_fault(kernel_map, va, ftype);
			pcb->pcb_onfault = onfault;

			if (rv == 0) {
				return;	/* retry the faulting instruction */
			}
			/* Fall through to onfault if uvm_fault fails */
		}

		if (onfault == NULL)
			goto dopanic;
		rv = EFAULT;
		/* FALLTHROUGH */

	copyfault:
		/*
		 * Recover from a fault during copyin/copyout.
		 * Adjust the stack frame to discard the bus error
		 * frame and redirect PC to the onfault handler.
		 */
		tf->tf_stackadj = exframesize[tf->tf_format];
		tf->tf_format = 0;
		tf->tf_vector = 0;
		tf->tf_pc = (int)onfault;
		tf->tf_regs[0] = rv;	/* D0 = error code */
		return;

	case T_BUSERR|T_USER:	/* user bus error */
	case T_ADDRERR|T_USER:	/* user address error */
		/*
		 * On CMMC68, the custom MMU generates bus errors for
		 * unmapped pages (no separate MMU fault vector).
		 * Handle as demand-paging faults for user addresses.
		 */
		if (v >= VM_MIN_USER_ADDRESS && v < VM_MAX_USER_ADDRESS) {
			vaddr_t va = trunc_page(v);
			struct vmspace *vm = p->p_vmspace;
			struct vm_map *map = &vm->vm_map;
			vm_prot_t ftype;

			if (WRFAULT(code))
				ftype = VM_PROT_WRITE;
			else
				ftype = VM_PROT_READ;

			pcb->pcb_onfault = NULL;
			rv = uvm_fault(map, va, ftype);
			pcb->pcb_onfault = onfault;

			if (rv == 0) {
				/*
				 * If this was a stack growth fault,
				 * tell UVM to update the stack size.
				 */
				if ((void *)va >= vm->vm_maxsaddr)
					uvm_grow(p, va);
				goto finish;
			}

			if (rv == EACCES) {
				ksi.ksi_code = SEGV_ACCERR;
			} else {
				ksi.ksi_code = SEGV_MAPERR;
			}
			ksi.ksi_addr = (void *)v;

			switch (rv) {
			case ENOMEM:
				printf("UVM: pid %d (%s) killed: "
				    "out of swap\n",
				    p->p_pid, p->p_comm);
				ksi.ksi_signo = SIGKILL;
				break;
			case EINVAL:
				ksi.ksi_signo = SIGBUS;
				ksi.ksi_code = BUS_ADRERR;
				break;
			case EACCES:
				ksi.ksi_signo = SIGSEGV;
				break;
			default:
				ksi.ksi_signo = SIGSEGV;
				break;
			}
			break;
		}
		/* Fault outside user VA range */
		ksi.ksi_signo = SIGBUS;
		ksi.ksi_code = BUS_OBJERR;
		ksi.ksi_addr = (void *)v;
		break;

	case T_ADDRERR:		/* kernel address error */
		if (onfault != NULL) {
			rv = EFAULT;
			goto copyfault;
		}
		goto dopanic;

	case T_MMUFLT:		/* kernel page fault */
		/*
		 * Page fault in kernel mode.
		 * Try uvm_fault first; if that fails, check onfault.
		 */
		{
			vaddr_t va = trunc_page(v);
			vm_prot_t ftype;

			if (WRFAULT(code))
				ftype = VM_PROT_WRITE;
			else
				ftype = VM_PROT_READ;

			pcb->pcb_onfault = NULL;
			rv = uvm_fault(kernel_map, va, ftype);
			pcb->pcb_onfault = onfault;

			if (rv == 0)
				return;

			if (onfault != NULL)
				goto copyfault;
		}
		goto dopanic;

	case T_MMUFLT|T_USER:	/* user page fault */
		{
			vaddr_t va = trunc_page(v);
			struct vmspace *vm = p->p_vmspace;
			struct vm_map *map = &vm->vm_map;
			vm_prot_t ftype;

			if (WRFAULT(code))
				ftype = VM_PROT_WRITE;
			else
				ftype = VM_PROT_READ;

			pcb->pcb_onfault = NULL;
			rv = uvm_fault(map, va, ftype);
			pcb->pcb_onfault = onfault;

			if (rv == 0) {
				if ((void *)va >= vm->vm_maxsaddr)
					uvm_grow(p, va);
				goto finish;
			}

			ksi.ksi_addr = (void *)v;
			if (rv == EACCES) {
				ksi.ksi_signo = SIGSEGV;
				ksi.ksi_code = SEGV_ACCERR;
			} else {
				ksi.ksi_signo = SIGSEGV;
				ksi.ksi_code = SEGV_MAPERR;
			}
		}
		break;

	case T_ILLINST|T_USER:		/* illegal instruction */
		ksi.ksi_signo = SIGILL;
		ksi.ksi_code = ILL_ILLOPC;
		ksi.ksi_addr = (void *)(uintptr_t)tf->tf_pc;
		break;

	case T_PRIVINST|T_USER:		/* privileged instruction */
		ksi.ksi_signo = SIGILL;
		ksi.ksi_code = ILL_PRVOPC;
		ksi.ksi_addr = (void *)(uintptr_t)tf->tf_pc;
		break;

	case T_ZERODIV|T_USER:		/* divide by zero */
		ksi.ksi_signo = SIGFPE;
		ksi.ksi_code = FPE_INTDIV;
		ksi.ksi_addr = (void *)(uintptr_t)tf->tf_pc;
		break;

	case T_TRACE:		/* kernel trace trap */
	case T_TRACE|T_USER:	/* user trace trap */
		tf->tf_sr &= ~PSL_T;
		if ((type & T_USER) == 0)
			return;
		ksi.ksi_signo = SIGTRAP;
		ksi.ksi_code = TRAP_TRACE;
		break;

	case T_ASTFLT|T_USER:	/* user AST */
		break;
	}

finish:
	/* If trap was from supervisor mode, just return. */
	if ((type & T_USER) == 0)
		return;

	/* Post a signal if necessary. */
	if (ksi.ksi_signo)
		trapsignal(l, &ksi);

	mi_userret(l);
}
