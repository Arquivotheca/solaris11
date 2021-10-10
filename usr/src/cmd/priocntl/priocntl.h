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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

#ifndef	_PRIOCNTL_H
#define	_PRIOCNTL_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	NPIDS	1024	/* number of additional pids allocated by realloc(). */
#define	NIDS	1024	/* max number of id arguments we handle */

#define	BASENMSZ	16
#define	CSOPTSLN	128	/* max length of class specific opts string */
#define	PC_IDTYPNMSZ	12	/* max length of an idtype name */

/*
 * The command string for the sub-command must be big enough for the
 * path, the class specific options, and plenty of space for arguments.
 */
#define	SUBCMDSZ	512

#define	BADPID	(-2)

/*
 * CLP_PIDLIST_SZ indicates the amount of characters needed to store a
 * 'PID/LWP' string.
 */
#define	CLP_ID_LENGTH	(10)
#define	CLP_PIDLIST_SZ	(((2 * CLP_ID_LENGTH) + 1) * sizeof (char))

/*
 * Used to tie a PID and a range of LWPs specified by the user.
 */
typedef struct plwp {
	id_t	pid;
	id_t	lwp;
	char	clname[PC_CLNMSZ];
} plwp_t;

typedef	char	pid_lwp_t[CLP_PIDLIST_SZ];

extern void		fatalerr(const char *, ...);
extern int		str2idtyp(char *, idtype_t *);
extern int		idtyp2str(idtype_t, char *);
extern int		idcompar(id_t *, id_t *);
extern int		plwpcompar(plwp_t *, plwp_t *);
extern id_t		clname2cid(char *);
extern int		getmyid(idtype_t, id_t *);
extern int		getmyidstr(idtype_t, char *);
extern int		verifyupri(idtype_t, id_t, char *, int, pri_t, char *);
extern pid_lwp_t	*read_pidlist(size_t *, FILE *);
extern void		free_pidlist(pid_lwp_t *);
extern long		str2num(char *, long, long);
extern long		str2pid(char *, long, long, plwp_t *, int *, char **);
extern void		itoa(long, char *);

/*
 * The following is an excerpt from <sys/hrtcntl.h>. HRT timers are not
 * supported by SunOS (which will support the POSIX definition). Priocntl
 * uses the hrt routine _hrtnewres because it coincidentally does the
 * right thing. These defines allow this routine to be locally included
 * in priocntl (rather than exported in libc). This should be improved in
 * the long term.
 */
#define	HRT_TRUNC	0	/* Round results down.	*/
#define	HRT_RND		1	/* Round results (rnd up if fractional	*/
				/*   part >= .5 otherwise round down).	*/
#define	HRT_RNDUP	2	/* Always round results up.	*/

/*
 * Structure used to represent a high-resolution time-of-day
 * or interval.
 */
typedef struct hrtimer {
	ulong_t	hrt_secs;	/* Seconds.				*/
	long	hrt_rem;	/* A value less than a second.		*/
	ulong_t	hrt_res;	/* The resolution of hrt_rem.		*/
} hrtimer_t;

extern int _hrtnewres(hrtimer_t *, ulong_t, long);

#ifdef	__cplusplus
}
#endif

#endif	/* _PRIOCNTL_H */
