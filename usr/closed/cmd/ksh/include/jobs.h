/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 *	UNIX shell
 *	S. R. Bourne
 *	rewritten by David Korn
 *
 */

#ifndef	___KSH_JOBS_H
#define	___KSH_JOBS_H

#define LOBYTE	0377
#define MAXMSG	25
extern int JOBTTY;

#include	"sh_config.h"
#ifdef JOBS
#   include	"terminal.h"
#   undef ESCAPE

#   ifdef FIOLOOKLD
	/* Ninth edition */
	extern int tty_ld, ntty_ld;
#	define OTTYDISC	tty_ld
#	define NTTYDISC	ntty_ld
#	define ksh_killpg(pgrp,sig)		kill(-(pgrp),sig)
#   endif	/* FIOLOOKLD */
#   define job_nonstop()	(st.states &= ~MONITOR)
#else
#   undef SIGTSTP
#   undef SIGCHLD
#   undef SIGCLD
#   undef MONITOR
#   define MONITOR	0
#   define job_nonstop()
#   define job_set(x)
#   define job_reset(x)
#endif	/* JOBS */

#if defined(CHILD_MAX) && (CHILD_MAX>4096)
#   define MAXJ	(CHILD_MAX-1)
#else
#   define MAXJ	4096		/* maximum number of jobs, must be < 32K */
#endif /* CHILD_MAX */

/* JBYTES is the number of char's needed for MAXJ bits */
#define JBYTES		(1+((MAXJ-1)/(8)))

struct process
{
	struct process *p_nxtjob;	/* next job structure */
	struct process *p_nxtproc;	/* next process in current job */
	pid_t		p_pid;		/* process id */
	pid_t		p_pgrp;		/* process group */
	pid_t		p_fgrp;		/* process group when stopped */
	short		p_job;		/* job number of process */
	unsigned short	p_exit;		/* exit value or signal number */
	unsigned short	p_flag;		/* flags - see below */
#ifdef JOBS
	off_t		p_name;		/* history file offset for command */
	struct termios	p_stty;		/* terminal state for job */
#endif /* JOBS */
};

/* Process states */

#define P_RUNNING	0x0001
#define P_STOPPED	0x0002
#define P_NOTIFY	0x0004
#define P_SIGNALLED	0x0008
#define P_STTY		0x0010
#define P_DONE		0x0020
#define P_COREDUMP	0x0040
#define P_BYNUM		0x0080
#define P_EXITSAVE	0x0100
#define P_KNOWNPID	0x0200
#define	P_ASYNCPROC	0x0400		/* has ever run in background */

struct jobs
{
	struct process	*pwlist;	/* head of process list */
	pid_t		curpgid;	/* current process gid id */
	pid_t		parent;		/* set by fork() */
	pid_t		mypid;		/* process id of shell */
	pid_t		mypgid;		/* process group id of shell */
	pid_t		mytgid;		/* terminal group id of shell */
	int		numpost;	/* number of posted jobs */
#ifdef JOBS
	int		suspend;	/* suspend character */
	int		linedisc;	/* line dicipline */
	char		jobcontrol;	/* turned on for real job control */
#endif /* JOBS */
	char		pipeflag;	/* set for pipelines */
	char		waitall;	/* wait for all pids of pipeline */
	char		waitsafe;	/* wait will not block */
	unsigned char	freejobs[JBYTES];	/* free jobs numbers */
};

extern struct jobs job;

#ifdef JOBS
extern const char	e_jobusage[];
extern const char	e_kill[];
extern const char	e_Done[];
extern const char	e_Running[];
extern const char	e_coredump[];
extern const char	e_killcolon[];
extern const char	e_no_proc[];
extern const char	e_no_job[];
extern const char	e_running[];
extern const char	e_nlspace[];
extern const char	e_ambiguous[];
#ifdef SIGTSTP
   extern const char	e_no_start[];
   extern const char	e_terminate[];
   extern const char	e_no_jctl[];
#endif /* SIGTSTP */
#ifdef NTTYDISC
   extern const char	e_newtty[];
   extern const char	e_oldtty[];
#endif /* NTTYDISC */
#endif	/* JOBS */

/*
 * The following are defined in jobs.c
 */

#ifdef PROTO
    extern void job_clear(void);
    extern int	job_post(pid_t, int);
    extern void job_bwait(char*[]);
    extern int	job_walk(int(*)(),int,char*[]);
    extern int	job_kill(struct process*,int);
    extern void	job_mark_save(pid_t);
    extern void	job_discard_save(pid_t);
    extern void	job_wait(pid_t);
#else
    extern void job_clear();
    extern int	job_post();
    extern void job_bwait();
    extern int	job_walk();
    extern int	job_kill();
    extern void	job_mark_save();
    extern void	job_discard_save();
    extern void	job_wait();
#endif /* PROTO */

#ifdef JOBS
#   ifdef PROTO
	extern void	job_init(int);
	extern int	job_close(void);
	extern int	job_list(struct process*,int);
#   else
	extern void	job_init();
	extern int	job_close();
	extern int	job_list();
#   endif /* PROTO */
#else
#	define job_init(flag)
#	define job_close()	(0)
#endif	/* JOBS */

#ifdef SIGTSTP
#   ifdef PROTO
	extern int	job_switch(struct process*,int);
#   else
	extern int	job_switch();
#   endif /* PROTO */
#endif /* SIGTSTP */

#endif /* !___KSH_JOBS_H */
