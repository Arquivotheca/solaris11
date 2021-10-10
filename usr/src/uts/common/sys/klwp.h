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
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_KLWP_H
#define	_SYS_KLWP_H

#include <sys/types.h>
#include <sys/condvar.h>
#include <sys/thread.h>
#include <sys/signal.h>
#include <sys/siginfo.h>
#include <sys/pcb.h>
#include <sys/time.h>
#include <sys/msacct.h>
#include <sys/ucontext.h>
#include <sys/lwp.h>
#include <sys/contract.h>

#if (defined(_KERNEL) || defined(_KMEMUSER)) && defined(_MACHDEP)
#include <sys/machparam.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The light-weight process object and the methods by which it
 * is accessed.
 */

#define	MAXSYSARGS	8	/* Maximum # of arguments passed to a syscall */

/* lwp_eosys values */
#define	NORMALRETURN	0	/* normal return; adjusts PC, registers */
#define	JUSTRETURN	1	/* just return, leave registers alone */

/*
 * Resource usage, per-lwp plus per-process (sum over defunct lwps).
 */
struct lrusage {
	u_longlong_t	minflt;		/* minor page faults */
	u_longlong_t	majflt;		/* major page faults */
	u_longlong_t	nswap;		/* swaps */
	u_longlong_t	inblock;	/* input blocks */
	u_longlong_t	oublock;	/* output blocks */
	u_longlong_t	msgsnd;		/* messages sent */
	u_longlong_t	msgrcv;		/* messages received */
	u_longlong_t	nsignals;	/* signals received */
	u_longlong_t	nvcsw;		/* voluntary context switches */
	u_longlong_t	nivcsw;		/* involuntary context switches */
	u_longlong_t	sysc;		/* system calls */
	u_longlong_t	ioch;		/* chars read and written */
};

typedef struct _klwp	*klwp_id_t;

/*
 * ac_clock
 *	Every time an LWP is charged with a tick of CPU time, this is
 *	incremented. This is basically a per-LWP clock based on CPU time.
 * ac_uclock
 *	This is a per-LWP clock based on user CPU time.
 * ac_bclock
 *	This is a per-LWP buffered clock incremented every N ticks of
 *	CPU time. Some of the LWP accounting clients only need to be
 *	updated once in every N ticks.
 * ac_lbolt
 *	Copy of the lbolt used during an LWP accounting tick cycle.
 * ac_flags
 *	LWP accounting state flags.
 */
typedef struct lwp_ac {
	uint64_t	ac_clock;		/* LWP CPU time clock */
	uint64_t	ac_uclock;		/* LWP user CPU time clock */
	uint64_t	ac_bclock;		/* Buffered CPU time clock */
	clock_t		ac_lbolt;		/* copy of the lbolt */
	int		ac_flags;		/* flags */
} lwp_ac_t;

/*
 * This flag is set to indicate to the LWP accounting handlers that an
 * LWP is exiting and should no longer be accounted.
 */
#define	LWP_AC_EXIT	0x1

/*
 * Macro to get the value of the LWP CPU time clock. Used by modules to base
 * CPU time-based timers on. E.g., thread time quantum, interval timers
 * ITIMER_VIRTUAL and ITIMER_PROF.
 */
#define	LWP_AC_CLOCK(lwp, user, clock)					\
{									\
	lwp_ac_t	*lac;						\
									\
	lac = &(lwp)->lwp_ac;						\
	clock = (user) ? lac->ac_uclock : lac->ac_clock;		\
}

typedef struct lwp_ac_timer {
	uint64_t	lac_clock;	/* snapshot of the LWP ac clock */
	uint64_t	lac_timer;	/* interval timer based on the clock */
} lwp_ac_timer_t;

typedef struct _klwp {
	/*
	 * user-mode context
	 */
	struct pcb	lwp_pcb;		/* user regs save pcb */
	uintptr_t	lwp_oldcontext;		/* previous user context */

	/*
	 * system-call interface
	 */
	long	*lwp_ap;	/* pointer to arglist */
	int	lwp_errno;	/* error for current syscall (private) */
	/*
	 * support for I/O
	 */
	char	lwp_error;	/* return error code */
	char	lwp_eosys;	/* special action on end of syscall */
	char	lwp_argsaved;	/* are all args in lwp_arg */
	char	lwp_watchtrap;	/* lwp undergoing watchpoint single-step */
	long	lwp_arg[MAXSYSARGS];	/* args to current syscall */
	void	*lwp_regs;	/* pointer to saved regs on stack */
	void	*lwp_fpu;	/* pointer to fpu regs */
	label_t	lwp_qsav;	/* longjmp label for quits and interrupts */

	/*
	 * signal handling and debugger (/proc) interface
	 */
	uchar_t	lwp_cursig;		/* current signal */
	uchar_t	lwp_curflt;		/* current fault */
	uchar_t	lwp_sysabort;		/* if set, abort syscall */
	uchar_t	lwp_asleep;		/* lwp asleep in syscall */
	uchar_t lwp_extsig;		/* cursig sent from another contract */
	stack_t lwp_sigaltstack;	/* alternate signal stack */
	struct sigqueue *lwp_curinfo;	/* siginfo for current signal */
	k_siginfo_t	lwp_siginfo;	/* siginfo for stop-on-fault */
	k_sigset_t	lwp_sigoldmask;	/* for sigsuspend */
	struct lwp_watch {		/* used in watchpoint single-stepping */
		caddr_t	wpaddr;
		size_t	wpsize;
		int	wpcode;
		int	wpmapped;
		greg_t	wppc;
	} lwp_watch[4];		/* one for each of exec/write/read/read */

	uint32_t lwp_oweupc;		/* profil(2) ticks owed to this lwp */

	/*
	 * Microstate accounting.  Timestamps are made at the start and the
	 * end of each microstate (see <sys/msacct.h> for state definitions)
	 * and the corresponding accounting info is updated.  The current
	 * microstate is kept in the thread struct, since there are cases
	 * when one thread must update another thread's state (a no-no
	 * for an lwp since it may be swapped/paged out).  The rest of the
	 * microstate stuff is kept here to avoid wasting space on things
	 * like kernel threads that don't have an associated lwp.
	 */
	struct mstate {
		int ms_prev;			/* previous running mstate */
		hrtime_t ms_start;		/* lwp creation time */
		hrtime_t ms_term;		/* lwp termination time */
		hrtime_t ms_state_start;	/* start time of this mstate */
		hrtime_t ms_acct[NMSTATES];	/* per mstate accounting */
	} lwp_mstate;

	/*
	 * Per-lwp resource usage.
	 */
	struct lrusage lwp_ru;

	/*
	 * Things to keep for real-time (SIGPROF) profiling.
	 */
	int	lwp_lastfault;
	caddr_t	lwp_lastfaddr;

	/*
	 * timers. Protected by lwp->procp->p_lock
	 */
	struct itimerval lwp_timer[3];

	/*
	 * used to stop/alert lwps
	 */
	char	lwp_unused;
	char	lwp_state;	/* Running in User/Kernel mode (no lock req) */
	ushort_t lwp_nostop;	/* Don't stop this lwp (no lock required) */
	ushort_t lwp_pad;	/* Reserved for future use */

	/*
	 * Last failed privilege.
	 */
	short	lwp_badpriv;

	/*
	 * linkage
	 */
	struct _kthread	*lwp_thread;
	struct proc	*lwp_procp;

	size_t lwp_childstksz;	/* kernel stksize for this lwp's descendants */

	uintptr_t	lwp_ustack;		/* current stack bounds */
	size_t		lwp_old_stk_ctl;	/* old stack limit */

	/*
	 * Contracts
	 */
	struct ct_template *lwp_ct_active[CTT_MAXTYPE]; /* active templates */
	struct contract	*lwp_ct_latest[CTT_MAXTYPE]; /* last created contract */

	void	*lwp_brand;		/* per-lwp brand data */

	lwp_ac_t	lwp_ac;			/* LWP accounting structure */
	lwp_ac_timer_t	lwp_ac_timer[3];	/* virtual and prof timers */
} klwp_t;

/* lwp states */
#define	LWP_USER	0x01		/* Running in user mode */
#define	LWP_SYS		0x02		/* Running in kernel mode */

#if	defined(_KERNEL)

/*
 * Initialize LWP accounting lock.
 */
#define	LWP_AC_LOCK_INIT(tp)						\
	mutex_init(&(tp)->t_ac_lock, 0, MUTEX_DEFAULT, 0)

/*
 * The LWP accounting lock is used to protect the CPU time accounting state
 * of an LWP. The accounting handlers acquire the lock to prevent the
 * state from changing during CPU time accounting. Code that changes the state
 * acquires the lock to prevent CPU time accounting during the state changes.
 * The accounting state comprises the following pieces:
 *
 * LWP exit state
 *	When an LWP exits, this lock is taken to set an exit flag. The
 *	accounting handler does not perform accounting for an LWP after this
 *	flag is set.
 *
 * Scheduling class-specific structures
 *	When CL_FORK() or CL_ENTERCLASS() are called for an LWP, its scheduling
 *	class-specific data structures are set up. E.g., tsproc_t for a TS
 *	thread. The accounting handler performs time quantum processing using
 *	these data structures. So, entering a scheduling class is done under
 *	the lock. Similarly, when scheduling class-specific parameters are set
 *	for an LWP, this lock is acquired.
 *
 * LWP transition
 *	No accounting is performed for threads that do not have LWPs. So,
 *	the transition of a thread to an LWP (and vice-versa) is protected
 *	by this lock.
 *
 * Task
 *	The accounting handler processes the task.max-cpu-time resource
 *	control The task for a thread's process cannot be allowed to change
 *	during accounting. So, updates to this kthread_t field is protected by
 *	this lock - t_task.
 *
 * Project, Zone
 *	The accounting handler processes CPU caps for projects and zones. The
 *	project and zone for a thread's process cannot be allowed to change
 *	during accounting. So, updates to these kthread_t fields are protected
 *	by this lock - t_proj, t_zone.
 *
 * Interval Timers
 *	The interval timers ITIMER_VIRTUAL and ITIMER_PROF are processed by
 *	the accounting handler. Setting and cancelling of these timers is
 *	protected by the lock.
 *
 * Process
 *	The accounting handler processes the process.max-cpu-time resource
 *	control. The resource control is implemented differently for a
 *	single-threaded process and a multi-threaded process. The transition
 *	of a process from single to multi-threaded is protected by this lock.
 *
 * The locking order is as follows:
 *	p->p_lock --> LWP accounting lock
 *	thread_free_lock --> LWP accounting lock
 *	thread_free_lock --> p->p_lock
 */
#define	LWP_AC_LOCK(tp)			mutex_enter(&(tp)->t_ac_lock)

/*
 * Unblock LWP accounting.
 */
#define	LWP_AC_UNLOCK(tp)		mutex_exit(&(tp)->t_ac_lock)

/*
 * Test for the LWP accounting lock.
 */
#define	LWP_AC_LOCK_HELD(tp)		MUTEX_HELD(&(tp)->t_ac_lock)

/*
 * Some of the thread accounting clients do not need to update every tick.
 * They only need to update every N ticks. E.g., the process.max-cpu-time
 * resource control. The following specifies the amount of CPU time
 * that is buffered each time before an update is done for these clients.
 */
#define	LWP_AC_BTIME			10000000

extern	int	lwp_default_stksize;
extern	int	lwp_reapcnt;

extern	uint64_t	lwp_ac_bticks;

extern	struct _kthread *lwp_deathrow;
extern	kmutex_t	reaplock;
extern	struct kmem_cache *lwp_cache;
extern	void		*segkp_lwp;
extern	klwp_t		lwp0;

/* where newly-created lwps normally start */
extern	void	lwp_rtt(void);

extern void		lwp_ac_init(void);
extern void		lwp_ac_start(klwp_t *);
extern void		lwp_ac_end(klwp_t *);
extern int		ithandler(klwp_t *, uint_t);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_KLWP_H */
