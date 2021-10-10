/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef ___KSH_BRKINCR_H
#define	___KSH_BRKINCR_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 *	UNIX shell
 *	S. R. Bourne
 *	Rewritten by David Korn
 *
 */

#define ERRTRAP		(MAXTRAP-2)
#define DEBUGTRAP	(MAXTRAP-1)

#ifndef SIGCHLD
#   ifdef SIGCLD
#	define SIGCHLD SIGCLD
#   endif
#endif

#define SIGBITS		8

#undef SIGIGNORE
#define SIGIGNORE	1	/* signal handler set to ignore */
#define TRAPSET		2	/* signal received and trap is set */
#define SIGSET		4	/* signal received and trap not set */
#define SIGMOD		8	/* modified signal handler */
#undef SIGCAUGHT
#define SIGCAUGHT	16	/* default action is to catch signal */
#undef SIGOFF
#define SIGOFF		32	/* signal ignored by parent */
#undef SIGDONE
#define SIGDONE		64	/* default action is clean up and exit */
#undef SIGFAULT
#define SIGFAULT	128	/* signal handler set to sh_fault */
#undef SIGFAIL
#define SIGSLOW		256	/* slow signal received */
#define SIGBEGIN	512	/* set during signal initialization */
#define SIGFAIL		0600
#undef SIGFLG
#define SIGFLG		0200
#define EXITMASK	0377

#ifdef PROTO
    extern void 	sh_fault(int);
    extern void 	sh_done(int)
	__NORETURN;
    extern void 	sig_sh_done(int);
    extern void 	sh_chktrap(void);
    extern void 	sh_exit(int)
	__NORETURN;
    extern void 	sig_clear(int);
    extern int		sig_ignore(int);
    extern void 	sig_init(void);
    extern void 	sig_ontrap(int);
    extern void 	sig_reset(int);
#else
    extern void 	sh_fault();
    extern void 	sh_done() __NORETURN;
    extern void 	sig_sh_done();
    extern void 	sh_chktrap();
    extern void 	sh_exit() __NORETURN;
    extern void 	sig_clear();
    extern int		sig_ignore();
    extern void 	sig_init();
    extern void 	sig_ontrap();
    extern void 	sig_reset();
    extern void 	sig_funset();
#endif /* PROTO */

extern const char		e_space[];
#ifndef	SIGSTRINGS
extern const struct sysnod	sig_messages[];
#endif	/* SIGSTRINGS */
extern const struct sysnod	sig_names[];

#endif /* !___KSH_BRKINCR_H */
