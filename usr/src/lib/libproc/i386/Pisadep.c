/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/stack.h>
#include <sys/regset.h>
#include <sys/frame.h>
#include <sys/sysmacros.h>
#include <sys/trap.h>
#include <sys/machelf.h>

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>

#include "Pcontrol.h"
#include "Pstack.h"

static uchar_t int_syscall_instr[] = { 0xCD, T_SYSCALLINT };

const char *
Ppltdest(struct ps_prochandle *P, uintptr_t pltaddr)
{
	map_info_t *mp = Paddr2mptr(P, pltaddr);

	uintptr_t r_addr;
	file_info_t *fp;
	Elf32_Rel r;
	size_t i;

	if (mp == NULL || (fp = mp->map_file) == NULL ||
	    fp->file_plt_base == 0 ||
	    pltaddr - fp->file_plt_base >= fp->file_plt_size) {
		errno = EINVAL;
		return (NULL);
	}

	i = (pltaddr - fp->file_plt_base) / M_PLT_ENTSIZE - M_PLT_XNumber;

	r_addr = fp->file_jmp_rel + i * sizeof (r);

	if (Pread(P, &r, sizeof (r), r_addr) == sizeof (r) &&
	    (i = ELF32_R_SYM(r.r_info)) < fp->file_dynsym.sym_symn) {

		Elf_Data *data = fp->file_dynsym.sym_data_pri;
		Elf32_Sym *symp = &(((Elf32_Sym *)data->d_buf)[i]);

		return (fp->file_dynsym.sym_strs + symp->st_name);
	}

	return (NULL);
}

int
Pissyscall(struct ps_prochandle *P, uintptr_t addr)
{
	uchar_t instr[16];

	if (Pread(P, instr, sizeof (int_syscall_instr), addr) !=
	    sizeof (int_syscall_instr))
		return (0);

	if (memcmp(instr, int_syscall_instr, sizeof (int_syscall_instr)) == 0)
		return (1);

	return (0);
}

int
Pissyscall_prev(struct ps_prochandle *P, uintptr_t addr, uintptr_t *dst)
{
	int ret;

	if (ret = Pissyscall(P, addr - sizeof (int_syscall_instr))) {
		if (dst)
			*dst = addr - sizeof (int_syscall_instr);
		return (ret);
	}

	return (0);
}

/* ARGSUSED */
int
Pissyscall_text(struct ps_prochandle *P, const void *buf, size_t buflen)
{
	if (buflen < sizeof (int_syscall_instr))
		return (0);

	if (memcmp(buf, int_syscall_instr, sizeof (int_syscall_instr)) == 0)
		return (1);

	return (0);
}

#define	TR_ARG_MAX 6	/* Max args to print, same as SPARC */

/*
 * Given a return address, determine the likely number of arguments
 * that were pushed on the stack prior to its execution.  We do this by
 * expecting that a typical call sequence consists of pushing arguments on
 * the stack, executing a call instruction, and then performing an add
 * on %esp to restore it to the value prior to pushing the arguments for
 * the call.  We attempt to detect such an add, and divide the addend
 * by the size of a word to determine the number of pushed arguments.
 *
 * If we do not find such an add, this does not necessarily imply that the
 * function took no arguments. It is not possible to reliably detect such a
 * void function because hand-coded assembler does not always perform an add
 * to %esp immediately after the "call" instruction (eg. _sys_call()).
 * Because of this, we default to returning MIN(sz, TR_ARG_MAX) instead of 0
 * in the absence of an add to %esp.
 */
static ulong_t
argcount(struct ps_prochandle *P, long pc, ssize_t sz)
{
	uchar_t instr[6];
	ulong_t count, max;

	max = MIN(sz / sizeof (long), TR_ARG_MAX);

	/*
	 * Read the instruction at the return location.
	 */
	if (Pread(P, instr, sizeof (instr), pc) != sizeof (instr) ||
	    instr[1] != 0xc4)
		return (max);

	switch (instr[0]) {
	case 0x81:	/* count is a longword */
		count = instr[2]+(instr[3]<<8)+(instr[4]<<16)+(instr[5]<<24);
		break;
	case 0x83:	/* count is a byte */
		count = instr[2];
		break;
	default:
		return (max);
	}

	count /= sizeof (long);
	return (MIN(count, max));
}

static void
ucontext_n_to_prgregs(const ucontext_t *src, prgregset_t dst)
{
	(void) memcpy(dst, src->uc_mcontext.gregs, sizeof (gregset_t));
}

int
Pstack_iter(struct ps_prochandle *P, const prgregset_t regs,
	proc_stack_f *func, void *arg)
{
	prgreg_t *prevfp = NULL;
	uint_t pfpsize = 0;
	int nfp = 0;
	struct {
		long	fp;
		long	pc;
		long	args[32];
	} frame;
	uint_t argc;
	ssize_t sz;
	prgregset_t gregs;
	prgreg_t fp;
	prgreg_t pc;
	int rv;
	int frame_flags = 0;
	int sig;	/* ignored unless (frame_flags & PR_FOUND_SIGNAL) */

	/*
	 * Type definition for a structure corresponding to an IA32
	 * signal frame.  Refer to the comments in Pstack.c for more info
	 */
	typedef struct {
		long fp;
		long pc;
		int signo;
		ucontext_t *ucp;
		siginfo_t *sip;
	} sf_t;

	uclist_t ucl;
	ucontext_t uc;
	uintptr_t uc_addr;

	init_uclist(&ucl, P);
	(void) memcpy(gregs, regs, sizeof (gregs));

	fp = regs[R_FP];
	pc = regs[R_PC];

	while (fp != 0 || pc != 0) {
		if (stack_loop(fp, &prevfp, &nfp, &pfpsize))
			break;

		if (fp != 0 &&
		    (sz = Pread(P, &frame, sizeof (frame), (uintptr_t)fp)
		    >= (ssize_t)(2* sizeof (long)))) {

			/*
			 * A return PC of -1 on x86 denotes a signal frame. We
			 * continue to look for a ucontext as a modest
			 * precaution against stack corruption.
			 */
			if (frame.pc == -1 &&
			    find_uclink(&ucl, fp + sizeof (sf_t))) {

				uc_addr = fp + sizeof (sf_t);
				frame_flags = PR_SIGNAL_FRAME;
				if (Pread(P, &sig, sizeof (int),
				    (uintptr_t)(fp + sizeof (struct frame)))
				    == sizeof (int))
					frame_flags |= PR_FOUND_SIGNAL;
				/*
				 * The bogus return PC prevents us from calling
				 * argcount() but we know that the signal
				 * handler takes three arguments (the signal
				 * number and pointers to a siginfo_t and a
				 * ucontext_t).
				 */
				argc = 3;
			} else {
				uc_addr = NULL;
				sz -= 2* sizeof (long);
				argc = argcount(P, (long)frame.pc, sz);
			}
		} else {
			(void) memset(&frame, 0, sizeof (frame));
			uc_addr = NULL;
			argc = 0;
		}

		gregs[R_FP] = fp;
		gregs[R_PC] = pc;

		if ((rv = func(arg, gregs, argc, frame.args, frame_flags,
		    sig)) != 0)
			break;

		if (frame_flags & PR_SIGNAL_FRAME)
			frame_flags = 0;

		/* Locate the next frame. */
		if (gregs[R_FP] != fp || gregs[R_PC] != pc) {
			/*
			 * In order to allow iteration over java frames (which
			 * can have their own frame pointers), we allow the
			 * iterator to change the contents of gregs. If we
			 * detect a change, then we assume that the new values
			 * point to the next frame.
			 */
			fp = gregs[R_FP];
			pc = gregs[R_PC];
		} else if (uc_addr != NULL &&
		    Pread(P, &uc, sizeof (uc), uc_addr) == sizeof (uc)) {
			/*
			 * If this is a signal frame then we extract the new
			 * registers from the saved context, thereby allowing us
			 * to display the interrupted frame.
			 */
			ucontext_n_to_prgregs(&uc, gregs);
			fp = gregs[R_FP];
			pc = gregs[R_PC];
		} else {
			fp = frame.fp;
			pc = frame.pc;
		}
	}

	if (prevfp)
		free(prevfp);

	free_uclist(&ucl);
	return (rv);
}

uintptr_t
Psyscall_setup(struct ps_prochandle *P, int nargs, int sysindex, uintptr_t sp)
{
	sp -= sizeof (int) * (nargs+2);	/* space for arg list + CALL parms */

	P->status.pr_lwp.pr_reg[EAX] = sysindex;
	P->status.pr_lwp.pr_reg[R_SP] = sp;
	P->status.pr_lwp.pr_reg[R_PC] = P->sysaddr;

	return (sp);
}

int
Psyscall_copyinargs(struct ps_prochandle *P, int nargs, argdes_t *argp,
    uintptr_t ap)
{
	int32_t arglist[MAXARGS+2];
	int i;
	argdes_t *adp;

	for (i = 0, adp = argp; i < nargs; i++, adp++)
		arglist[1 + i] = (int32_t)adp->arg_value;

	arglist[0] = P->status.pr_lwp.pr_reg[R_PC];
	if (Pwrite(P, &arglist[0], sizeof (int) * (nargs+1),
	    (uintptr_t)ap) != sizeof (int) * (nargs+1))
		return (-1);

	return (0);
}

int
Psyscall_copyoutargs(struct ps_prochandle *P, int nargs, argdes_t *argp,
    uintptr_t ap)
{
	uint32_t arglist[MAXARGS + 2];
	int i;
	argdes_t *adp;

	if (Pread(P, &arglist[0], sizeof (int) * (nargs+1), (uintptr_t)ap)
	    != sizeof (int) * (nargs+1))
		return (-1);

	for (i = 0, adp = argp; i < nargs; i++, adp++)
		adp->arg_value = arglist[i];

	return (0);
}
