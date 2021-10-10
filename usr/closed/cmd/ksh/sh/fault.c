/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * UNIX shell
 *
 * S. R. Bourne
 * Rewritten by David Korn
 * AT&T Bell Laboratories
 *
 */

#include	<libintl.h>
#include	"defs.h"
#include	"jobs.h"
#include	"sym.h"
#include	"timeout.h"

/* ========	fault handling routines	   ======== */

#ifndef JOBS
#   undef SIGCHLD
#endif /* JOBS */

/*
 * Stop messages for jobs(1) under XPG4
 */
/* TRANSLATION_NOTE
 * To be printed to show a job is stopped by SIGSTOP.
 * (e.g. "sleep 10 &", "kill -STOP %1", then type Return) */
static char *xpg4_sigstop = "Stopped (SIGSTOP)";

/* TRANSLATION_NOTE
 * To be printed to show a job is stopped by SIGTSTP.
 * (e.g. "sleep 10", then type ^Z) */
static char *xpg4_sigtstp = "Stopped (SIGTSTP)";

/* TRANSLATION_NOTE
 * To be printed to show a job is stopped by SIGTTIN.
 * (e.g. run this script in background, then type return
 *	--- cut here ---
 *	#!/usr/bin/ksh
 *	read foo
 *	--- cut here ---
 * ) */
static char *xpg4_sigttin = "Stopped (SIGTTIN)";

/* TRANSLATION_NOTE
 * To be printed to show a job is stopped by SIGTTOU.
 * (e.g. "stty tostop", "echo foo &", then type return) */
static char *xpg4_sigttou = "Stopped (SIGTTOU)";

void	sh_fault(sig)
register int 	sig;
{
	register int 	flag;
#ifdef OLDTERMIO
	/* This .1 sec delay eliminates break key problems on 3b consoles */
#   ifdef _poll_
	if(sig==2)
		poll("",0,100);
#   endif /* _poll_ */
#endif /* OLDTERMIO */
#ifdef apollo
	/*
	 * Since this routine only handles SIGCHLD, make SIGCLD look like
	 * a SIGCHLD. Since both signals are defined on an apollo.
	 */
	if(sig==SIGCLD)
		sig = SIGCHLD;
#endif /* apollo */
#ifdef	SIGCHLD
	if(sig==SIGCHLD)
	{
		job.waitsafe++;
		if(st.trapcom[SIGCHLD])
		{
			sh.trapnote |= SIGSLOW;
#   ifndef SIG_NORESTART
			if(st.intfn)
			{
				sigrelease(sig);
				(*st.intfn)();
			}
#   endif	/* SIG_NORESTART */
		}
		if (is_option(NOTIFY)) {
			int save_outp = sh.curout;	/* Save output stream */
			p_setout(ERRIO);
			job_walk(job_list,N_FLAG,(char**)0);
			p_setout(save_outp);    /* Restore the output stream */
		}
		return;
	}
#endif	/* SIGCHLD */
	signal(sig, sh_fault);
	if(sig==SIGALRM)
	{
		if((st.states&WAITING) && sh_timeout>0)
		{
			if(st.states&GRACE)
			{
				/* force exit */
					st.states &= ~GRACE;
					st.states |= FORKED;
					sh_fail((const char *)
						gettext(e_timeout), NIL);
			}
			else
			{
				st.states |= GRACE;
				alarm((unsigned)TGRACE);
				p_str((const char *)gettext(e_timewarn), NL);
				p_flush();
			}
		}
	}
	else
	{
		if(st.trapcom[sig])
			flag = TRAPSET;
		else
		{
			sh.lastsig = sig;
			flag = SIGSET;
		}
		sh.trapnote |= flag;
		st.trapflg[sig] |= flag;
		if(sig <= SIGQUIT)
			sh.trapnote |= SIGSLOW;
	}
#ifndef SIG_NORESTART
	/* This is needed because interrupted reads automatically restart */
	if(st.intfn)
	{
		sigrelease(sig);
		(*st.intfn)();
	}
#endif	/* SIG_NORESTART */
}


void sig_init()
{
	register int i;
	register int n;
	register const struct sysnod	*syscan = sig_names;
#ifdef	SIGSTRINGS
	register char *sd;
#endif	/* SIGSTRINGS */
	sig_begin();
	while(*syscan->sysnam)
	{
		n = syscan->sysval;
		i = n&((1<<SIGBITS)-1);
		n >>= SIGBITS;
		st.trapflg[--i] = (n&~SIGIGNORE);
		if(n&SIGFAULT)
			signal(i,(VOID(*)())sh_fault);
		else if(n&SIGIGNORE)
			sig_ignore(i);
		else if(n&SIGCAUGHT)
			sig_ontrap(i);
		else if(n&SIGDONE)
		{
			sh.trapnote |= SIGBEGIN;
			if (signal(i, (void(*)())sig_sh_done) == SIG_IGN) {
				sig_ignore(i);
				st.trapflg[i] = SIGOFF;
			}
			else
				st.trapflg[i] = SIGMOD|SIGDONE;
			sh.trapnote &= ~SIGBEGIN;
		}
		syscan++;
	}
#ifndef	SIGSTRINGS
	for(syscan=sig_messages; n=syscan->sysval; syscan++)
	{
		if(n > NSIG+1)
			continue;
		if(*syscan->sysnam)
			sh.sigmsg[n-1] = (char*)syscan->sysnam;
	}
#else	/* SIGSTRINGS */
	/* XPG4: Job stop messages */
	sh.sigmsg[SIGSTOP] = xpg4_sigstop;
	sh.sigmsg[SIGTSTP] = xpg4_sigtstp;
	sh.sigmsg[SIGTTIN] = xpg4_sigttin;
	sh.sigmsg[SIGTTOU] = xpg4_sigttou;
#endif	/* SIGSTRINGS */
}

/*
 * set signal n to ignore
 * returns 1 if signal was already ignored, 0 otherwise
 */
int	sig_ignore(n)
register int n;
{
	if(n < MAXTRAP-1 && !(st.trapflg[n]&SIGIGNORE))
	{
		if(signal(n,SIG_IGN) != SIG_IGN)
		{
			st.trapflg[n] |= SIGIGNORE;
			st.trapflg[n] &= ~SIGFAULT;
			return(0);
		}
		st.trapflg[n] = SIGOFF;
	}
	return(1);
}

/*
 * Turn on trap handler for signal <n>
 */

void	sig_ontrap(n)
register int n;
{
	register int flag;
	if(n==DEBUGTRAP)
		sh.trapnote |= TRAPSET;
	/* don't do anything if already set or off by parent */
	else if(!(st.trapflg[n]&(SIGFAULT|SIGOFF)))
	{
		flag = st.trapflg[n];
		if(signal(n,(VOID(*)())sh_fault)==SIG_IGN) 
		{
			/* has it been set to ignore by shell */
			if(flag&SIGIGNORE)
				flag |= SIGFAULT;
			else
			{
				/* It ignored already, keep it ignored */ 
				sig_ignore(n);
				flag = SIGOFF;
			}
		}
		else
			flag |= SIGFAULT;
		flag &= ~(SIGSET|TRAPSET|SIGIGNORE|SIGMOD);
		st.trapflg[n] = flag;
	}
}

/*
 * Restore to default signals
 * Do not free the trap strings if flag is non-zero
 */

void	sig_reset(flag)
{
	register int 	i;
	register char *t;
	i=MAXTRAP;
	while(i--)
	{
		t=st.trapcom[i];
		if(t==0 || *t)
		{
			if(flag)
				st.trapcom[i] = 0; /* don't free the traps */
			sig_clear(i);
		}
		st.trapflg[i] &= ~(TRAPSET|SIGSET);
	}
	sh.trapnote=0;
}

/*
 * reset traps at start of function execution
 * keep track of which traps are caught by caller in case they are modified
 * flag==0 before function, flag==1 after function
 */

void	sig_funset(flag)
{
	register int 	i;
	register char *tp;
	i=MAXTRAP;
	while(i--)
	{
		tp = st.trapcom[i];
		if(flag==0)
		{
			if(tp && *tp==0)
				st.trapflg[i] = SIGOFF;
			else
			{
				if(tp)
					st.trapflg[i] |= SIGCAUGHT;
				st.trapflg[i] &= ~(TRAPSET|SIGSET);
			}
			st.trapcom[i] = 0;
		}
		else if(tp)
			sig_clear(i);
	}
	sh.trapnote = 0;
}

/*
 * free up trap if set and restore signal handler if modified
 */

void	sig_clear(n)
register int 	n;
{
	register int flag = st.trapflg[n];
	register char *t;
	if(t=st.trapcom[n])
	{
		free(t);
		st.trapcom[n]=0;
		flag &= ~(TRAPSET|SIGSET);
	}
	if(flag&(SIGFAULT|SIGMOD|SIGIGNORE))
	{
		if(flag&SIGCAUGHT)
		{
			if(flag&(SIGMOD|SIGIGNORE))
				signal(n, sh_fault);
		}
		else if((flag&SIGDONE))
		{
			if(t || (flag&SIGIGNORE))
				signal(n, sig_sh_done);
		}
		else
			 signal(n, SIG_DFL);
		flag &= ~(SIGMOD|SIGFAULT|SIGIGNORE);
		if(flag&SIGCAUGHT)
			flag |= SIGFAULT;
		else if(flag&SIGDONE)
			flag |= SIGMOD;
	}
	st.trapflg[n] = flag;
	if(n==SIGTERM && (st.states&INTFLG) && !(st.states&FORKED))
		sig_ignore(SIGTERM);
}


/*
 * check for traps
 */

void	sh_chktrap()
{
	register int 	i=MAXTRAP;
	register char *t;
#ifdef JOBS
	if(job.waitsafe)
		job_wait((pid_t)0);
#endif /* JOBS */
	/* process later if doing command substitution */
	if(st.subflag)
		return;
	sh.trapnote &= ~(TRAPSET|SIGSLOW);
	if((st.states&ERRFLG) && sh.exitval)
	{
		if(st.trapcom[ERRTRAP])
			st.trapflg[ERRTRAP] = TRAPSET;
		if(is_option(ERRFLG))
			sh_exit(sh.exitval);
	}
	while(--i)
	{
		if(st.trapflg[i]&TRAPSET)
		{
			st.trapflg[i] &= ~TRAPSET;
			if(t=st.trapcom[i])
			{
				int savxit=sh.exitval;
				int sav_trapnote = sh.trapnote;
				int savintrap = sh.intrap;
				if(i==ERRTRAP)
					sh.trapnote &= ~SIGSET;
				sh.intrap = i + 1;
				sh_eval(t);
				sh.intrap = savintrap;
				p_flush();
				sh.exitval=savxit;
				exitset();
				sh.trapnote = sav_trapnote;
			}
		}
	}
	if(st.trapcom[DEBUGTRAP])
	{
		st.trapflg[DEBUGTRAP] |= TRAPSET;
		sh.trapnote |= TRAPSET;
	}
}

#ifdef signal
void
(*nsignal(sig, act))()
	int sig;
	void (*act)();
{
	struct sigaction sa, osa;

	sa.sa_handler = act;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sig == SIGCHLD) {
		sa.sa_flags |= SA_NOCLDSTOP;
		if (act == SIG_IGN)
			sa.sa_flags |= SA_NOCLDWAIT;
	}

	if (sig != SIGCHLD)
		sa.sa_flags |= SA_RESTART;

	if (sigaction(sig, &sa, &osa) < 0)
		return ((void (*)())-1);
	return (osa.sa_handler);
}
#endif
