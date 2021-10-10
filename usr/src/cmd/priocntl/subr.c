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

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <limits.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/procset.h>
#include <sys/priocntl.h>
#include <sys/task.h>
#include <procfs.h>
#include <project.h>
#include <errno.h>
#include <zone.h>
#include <libcontract_priv.h>
#include <sys/varargs.h>
#include <search.h>
#include <libproc.h>
#include <sys/proc.h>
#include "priocntl.h"

/*
 * Utility functions for priocntl command.
 */
#define	MAX_PROCFS_PATH	(80)

static char *procdir = "/proc";

void
fatalerr(const char *format, ...)
{
	va_list ap;

	(void) va_start(ap, format);
	(void) vfprintf(stderr, format, ap);
	va_end(ap);
	exit(1);
}

/*
 * Structure defining idtypes known to the priocntl command
 * along with the corresponding names and a liberal guess
 * of the max number of procs sharing any given ID of that type.
 * The idtype values themselves are defined in <sys/procset.h>.
 */
static struct idtypes {
	idtype_t	idtype;
	char		*idtypnm;
} idtypes [] = {
	{ P_PID,	"pid"	},
	{ P_PPID,	"ppid"	},
	{ P_PGID,	"pgid"	},
	{ P_SID,	"sid"	},
	{ P_CID,	"class"	},
	{ P_UID,	"uid"	},
	{ P_GID,	"gid"	},
	{ P_PROJID,	"projid"},
	{ P_TASKID,	"taskid"},
	{ P_ZONEID,	"zoneid"},
	{ P_CTID,	"ctid"	},
	{ P_ALL,	"all"	}
};

#define	IDCNT (sizeof (idtypes) / sizeof (struct idtypes))

/*
 * Verify that the given idtype is valid, returning the id type name through
 * the 'idtypep' pointer according to the 'idtypes' table.
 */
int
str2idtyp(char *idtypnm, idtype_t *idtypep)
{
	register struct idtypes	*curp;
	register struct idtypes	*endp;

	for (curp = idtypes, endp = &idtypes[IDCNT]; curp < endp; curp++) {
		if (strcmp(curp->idtypnm, idtypnm) == 0) {
			*idtypep = curp->idtype;
			return (0);
		}
	}

	return (-1);
}

int
idtyp2str(idtype_t idtype, char *idtypnm)
{
	register struct idtypes	*curp;
	register struct idtypes	*endp;

	for (curp = idtypes, endp = &idtypes[IDCNT]; curp < endp; curp++) {
		if (idtype == curp->idtype) {
			(void) strncpy(idtypnm, curp->idtypnm, PC_IDTYPNMSZ);
			return (0);
		}
	}

	return (-1);
}

/*
 * Compare two IDs for equality, used when populating lists of id's.
 */
int
idcompar(id_t *id1p, id_t *id2p)
{
	if (*id1p == *id2p)
		return (0);
	else
		return (-1);
}

/*
 * Compare two plwp_t's.
 */
int
plwpcompar(plwp_t *id1p, plwp_t *id2p)
{
	if (id1p->pid == id2p->pid &&
	    id1p->lwp == id2p->lwp)
		return (0);
	else
		return (-1);
}

/*
 * Validate a given class name, returning it's id or -1 if invalid.
 */
id_t
clname2cid(char	*clname)
{
	pcinfo_t pcinfo;

	(void) strncpy(pcinfo.pc_clname, clname, PC_CLNMSZ);
	if (priocntl(0, 0, PC_GETCID, (caddr_t)&pcinfo) == -1)
		return ((id_t)-1);

	return (pcinfo.pc_cid);
}

int
getmyid(idtype_t idtype, id_t *idptr)
{
	pcinfo_t pcinfo;

	switch (idtype) {
	case P_PID:
		*idptr = (id_t)getpid();
		break;

	case P_PPID:
		*idptr = (id_t)getppid();
		break;

	case P_PGID:
		*idptr = (id_t)getpgrp();
		break;

	case P_SID:
		*idptr = (id_t)getsid(getpid());
		break;

	case P_CID:
		if (priocntl(P_PID, P_MYID, PC_GETXPARMS, NULL,
		    PC_KY_CLNAME, pcinfo.pc_clname, 0) == -1 ||
		    priocntl(0, 0, PC_GETCID, (caddr_t)&pcinfo) == -1)
			return (-1);

		*idptr = pcinfo.pc_cid;
		break;

	case P_UID:
		*idptr = (id_t)getuid();
		break;

	case P_GID:
		*idptr = (id_t)getgid();
		break;

	case P_PROJID:
		*idptr = (id_t)getprojid();
		break;

	case P_TASKID:
		*idptr = (id_t)gettaskid();
		break;

	case P_ZONEID:
		*idptr = (id_t)getzoneid();
		break;

	case P_CTID: {
		ctid_t id = getctid();
		if (id == -1)
			return (-1);
		*idptr = id;
		break;
	}

	default:
		return (-1);
	}

	return (0);
}

int
getmyidstr(idtype_t idtype, char *idstr)
{
	char clname[PC_CLNMSZ];

	switch (idtype) {
	case P_PID:
		itoa((long)getpid(), idstr);
		break;

	case P_PPID:
		itoa((long)getppid(), idstr);
		break;

	case P_PGID:
		itoa((long)getpgrp(), idstr);
		break;
	case P_SID:
		itoa((long)getsid(getpid()), idstr);
		break;

	case P_CID:
		if (priocntl(P_PID, P_MYID, PC_GETXPARMS, NULL,
		    PC_KY_CLNAME, clname, 0) == -1)
			return (-1);
		(void) strncpy(idstr, clname, PC_CLNMSZ);
		break;

	case P_UID:
		itoa((long)getuid(), idstr);
		break;

	case P_GID:
		itoa((long)getgid(), idstr);
		break;

	case P_PROJID:
		itoa((long)getprojid(), idstr);
		break;

	case P_TASKID:
		itoa((long)gettaskid(), idstr);
		break;

	case P_ZONEID:
		itoa((long)getzoneid(), idstr);
		break;

	case P_CTID: {
		id_t id;
		if ((id = getctid()) == -1)
			return (-1);
		itoa((long)id, idstr);
		break;
	}

	default:
		return (-1);
	}

	return (0);
}

/*
 * Look for pids with "upri > uprilim" in the set specified by idtype/id.
 * If upri exceeds uprilim then print a warning.
 */
int
verifyupri(idtype_t idtype, id_t id, char *clname, int key,
    pri_t upri, char *basenm)
{
	psinfo_t		prinfo;
	prcred_t		prcred;
	DIR			*dirp;
	struct dirent		*dentp;
	char			pname[MAXNAMLEN];
	char			*fname;
	int			procfd;
	int			saverr;
	pri_t			uprilim;
	int			verify;
	int			error = 0;

	if (idtype == P_PID) {
		if (priocntl(P_PID, id, PC_GETXPARMS, clname, key,
		    &uprilim, 0) == -1)
			error = -1;
		else if (upri > uprilim)
			(void) fprintf(stderr,
			    "%s: Specified user priority %d exceeds"
			    " limit %d; set to %d (pid %d)\n",
			    basenm, upri, uprilim, uprilim, (int)id);

		return (error);
	}

	/*
	 * Look for the processes in the set specified by idtype/id.
	 * We read the /proc/<pid>/psinfo file to get the necessary
	 * process information.
	 */

	if ((dirp = opendir(procdir)) == NULL)
		fatalerr("%s: Can't open PROC directory %s\n",
		    basenm, procdir);

	while ((dentp = readdir(dirp)) != NULL) {
		if (dentp->d_name[0] == '.')	/* skip . and .. */
			continue;

		(void) snprintf(pname, MAXNAMLEN, "%s/%s/",
		    procdir, dentp->d_name);
		fname = pname + strlen(pname);
retry:
		(void) strncpy(fname, "psinfo", strlen("psinfo") + 1);
		if ((procfd = open(pname, O_RDONLY)) < 0)
			continue;
		if (read(procfd, &prinfo, sizeof (prinfo)) != sizeof (prinfo)) {
			saverr = errno;
			(void) close(procfd);
			if (saverr == EAGAIN)
				goto retry;
			continue;
		}
		(void) close(procfd);

		if (idtype == P_UID || idtype == P_GID) {
			(void) strncpy(fname, "cred", strlen("cred") + 1);
			if ((procfd = open(pname, O_RDONLY)) < 0 ||
			    read(procfd, &prcred, sizeof (prcred)) !=
			    sizeof (prcred)) {
				saverr = errno;
				(void) close(procfd);
				if (saverr == EAGAIN)
					goto retry;
				continue;
			}
			(void) close(procfd);
		}

		if (prinfo.pr_lwp.pr_state == 0 || prinfo.pr_nlwp == 0)
			continue;

		/*
		 * The lwp must be in the correct class.
		 */
		if (strncmp(clname, prinfo.pr_lwp.pr_clname, PC_CLNMSZ) != 0)
			continue;

		verify = 0;
		switch (idtype) {
		case P_PPID:
			if (id == (id_t)prinfo.pr_ppid)
				verify++;
			break;

		case P_PGID:
			if (id == (id_t)prinfo.pr_pgid)
				verify++;
			break;

		case P_SID:
			if (id == (id_t)prinfo.pr_sid)
				verify++;
			break;

		case P_UID:
			if (id == (id_t)prcred.pr_euid)
				verify++;
			break;

		case P_GID:
			if (id == (id_t)prcred.pr_egid)
				verify++;
			break;

		case P_PROJID:
			if (id == (id_t)prinfo.pr_projid)
				verify++;
			break;

		case P_TASKID:
			if (id == (id_t)prinfo.pr_taskid)
				verify++;
			break;

		case P_ZONEID:
			if (id == (id_t)prinfo.pr_zoneid)
				verify++;
			break;

		case P_CTID:
			if (id == (id_t)prinfo.pr_contract)
				verify++;
			break;

		case P_CID:
		case P_ALL:
			verify++;
			break;

		default:
			fatalerr("%s: Bad idtype %d in verifyupri()\n",
			    basenm, idtype);
		}

		if (verify) {
			if (priocntl(P_PID, prinfo.pr_pid, PC_GETXPARMS,
			    clname, key, &uprilim, 0) == -1)
				error = -1;
			else if (upri > uprilim)
				(void) fprintf(stderr,
				    "%s: Specified user priority %d exceeds"
				    " limit %d; set to %d (pid %d)\n",
				    basenm, upri, uprilim, uprilim,
				    (int)prinfo.pr_pid);
		}
	}

	(void) closedir(dirp);

	return (error);
}

/*
 * Read a list of pids from a stream.
 */
pid_lwp_t *
read_pidlist(size_t *npidsp, FILE *filep)
{
	size_t nitems;
	pid_lwp_t *pidlist = NULL;

	*npidsp = 0;

	do {
		if ((pidlist = realloc(pidlist,
		    (*npidsp + NPIDS) * sizeof (pid_lwp_t))) == NULL)
			return (NULL);

		nitems = fread(pidlist + *npidsp, sizeof (pid_lwp_t),
		    NPIDS, filep);

		if (ferror(filep)) {
			free(pidlist);
			return (NULL);
		}

		*npidsp += nitems;
	} while (nitems == NPIDS);

	return (pidlist);
}

void
free_pidlist(pid_lwp_t *pidlist)
{
	free(pidlist);
}

long
str2num(char *p, long min, long max)
{
	long val;
	char *q;
	errno = 0;

	val = strtol(p, &q, 10);
	if (errno != 0 || q == p || *q != '\0' || val < min || val > max)
		errno = EINVAL;

	return (val);
}

/*
 * Check if the passed in PID contains one or more LWPs, and if all of the
 * process' LWPs (specified or not) are in the same scheduling class. We
 * return the name of the scheduling class through 'cln' if so.
 */
long
str2pid(char *p, long min, long max, plwp_t *plwplist, int *nplwps, char **cln)
{
	char *q, *range = NULL, *lpsinfo, *name1 = NULL, *name2 = NULL;
	char procfile[MAX_PROCFS_PATH];
	struct prheader hdr;
	struct lwpsinfo *lwpinfo;
	int i, fd, nent, sz;
	boolean_t mult_classes = B_FALSE, mult_classes_range = B_FALSE;
	long pid;
	plwp_t plwp;

	pid = strtol(p, &q, 10);

	/*
	 * Check if 'pid' is valid.
	 */
	if (errno != 0 || q == p || (*q != '\0' && *q != '/') ||
	    pid < min || pid > max) {
		errno = EINVAL;
		return (-1);
	}

	/*
	 * Check if 'pid' is followed by one or more LWPs.
	 */
	if (errno == 0 && *q == '/') {
		range = ++q;

		if (*range == '\0' || proc_lwp_range_valid(range) != 0) {
			errno = EINVAL;
			return (-1);
		}
	}

	/*
	 * Open the process' lpsinfo so we can look at its LWPs.
	 */
	(void) snprintf(procfile, MAX_PROCFS_PATH, "/proc/%d/lpsinfo", pid);

	if ((fd = open(procfile, O_RDONLY)) < 0)
		fatalerr("priocntl: Couldn't find process %d\n", pid);

	if (pread(fd, &hdr, sizeof (struct prheader), 0) !=
	    sizeof (struct prheader))
		fatalerr("priocntl: Failed to read process %d\n", pid);

	nent = hdr.pr_nent;
	sz = hdr.pr_entsize * nent;

	if ((lpsinfo = malloc(sz * sizeof (char))) == NULL)
		fatalerr("priocntl: Failed to allocate memory\n");

	if (pread(fd, lpsinfo, sz, sizeof (struct prheader)) != sz)
		fatalerr("priocntl: Failed to read data for process %d\n", pid);

	/*
	 * Walk the process' LWPs.
	 */
	for (i = 0, q = lpsinfo; i < nent && *nplwps < NIDS;
	    i++, q += hdr.pr_entsize) {

		/* LINTED ALIGNMENT */
		lwpinfo = (lwpsinfo_t *)q;

		/*
		 * Skip zombie threads.
		 */
		if (lwpinfo->pr_state == SZOMB)
			continue;

		/*
		 * Check if this LWP was specified by the user, adding it
		 * to the plwplist if so.
		 */
		if (range != NULL &&
		    proc_lwp_in_set(range, lwpinfo->pr_lwpid)) {

			plwp.pid = pid;
			plwp.lwp = lwpinfo->pr_lwpid;
			(void) strncpy(plwp.clname, lwpinfo->pr_clname,
			    PC_CLNMSZ);

			/*
			 * Add to the list skipping duplicates.
			 */
			(void) lsearch((void *)&plwp, (void *)plwplist,
			    (size_t *)nplwps, sizeof (plwp_t),
			    (int (*)())plwpcompar);

			/*
			 * Make sure that the specified LWPs are in the same
			 * scheduling class.
			 */
			if (name2 == NULL) {
				if ((name2 = calloc(PC_CLNMSZ,
				    sizeof (char))) == NULL)
					fatalerr("priocntl: Failed to allocate"
					    "memory.\n");
			} else {
				if (strncmp(name2, lwpinfo->pr_clname,
				    PC_CLNMSZ) != 0)
					mult_classes_range = B_TRUE;
			}

			(void) strncpy(name2, lwpinfo->pr_clname, PC_CLNMSZ);
		}

		/*
		 * Check if this process has LWPs in different scheduling
		 * classes.
		 */
		if (name1 == NULL) {
			if ((name1 = calloc(PC_CLNMSZ, sizeof (char))) == NULL)
				fatalerr("priocntl: Failed to allocate"
				    "memory.\n");
		} else {
			if (strncmp(name1, lwpinfo->pr_clname, PC_CLNMSZ) != 0)
				mult_classes = B_TRUE;
		}

		(void) strncpy(name1, lwpinfo->pr_clname, PC_CLNMSZ);
	}

	free(lpsinfo);
	(void) close(fd);

	/*
	 * If the user didn't specify a class already..
	 */
	if (*cln == NULL) {
		/*
		 * If we got this far but haven't taken note of a scheduling
		 * class, it means that the process doesn't have any LWPs or
		 * they were all zombies. Either way, an invalid PID.
		 */
		if (name1 == NULL) {
			errno = EINVAL;
		} else {
			/*
			 * Don't return a class name if:
			 *  - the user specified a range with LWPs in more
			 *    than one scheduling class;
			 *  - the user didn't specify a range and the process
			 *    has LWPs in more than one class.
			 * The caller can interpret this as an error other
			 * than EINVAL.
			 */
			if (mult_classes == B_FALSE ||
			    (range != NULL && mult_classes_range == B_FALSE)) {
				if (name2 != NULL) {
					*cln = name2;
					free(name1);
				} else {
					*cln = name1;
					free(name2);
				}
			}
		}
	} else {
		/*
		 * User specified class, free our variables..
		 */
		free(name1);
		free(name2);
	}

	return (pid);
}

/*
 * itoa() and reverse() taken almost verbatim from K & R Chapter 3.
 */
static void reverse();

/*
 * itoa(): Convert n to characters in s.
 */
void
itoa(long n, char *s)
{
	long i, sign;

	if ((sign = n) < 0)	/* record sign */
		n = -n;		/* make sign positive */

	i = 0;

	do {	/* generate digits in reverse order */
		s[i++] = n % 10 + '0';	/* get next digit */
	} while ((n /= 10) > 0);	/* delete it */

	if (sign < 0)
		s[i++] = '-';

	s[i] = '\0';
	reverse(s);
}

/*
 * reverse(): Reverse string s in place.
 */
static void
reverse(char *s)
{
	int c, i, j;

	for (i = 0, j = strlen(s) - 1; i < j; i++, j--) {
		c = s[i];
		s[i] = s[j];
		s[j] = (char)c;
	}
}

/*
 * The following routine was removed from libc (libc/port/gen/hrtnewres.c).
 * It has also been added to dispadmin, so if you fix it here, you should
 * also probably fix it there. In the long term, this should be recoded to
 * not be hrt'ish.
 */

/*
 * Convert interval expressed in htp->hrt_res to new_res.
 *
 * Calculate: (interval * new_res) / htp->hrt_res  rounding off as
 * specified by round.
 *
 * Note: All args are assumed to be positive. If
 * the last divide results in something bigger than
 * a long, then -1 is returned instead.
 */
int
_hrtnewres(hrtimer_t *htp, ulong_t new_res, long round)
{
	long		interval;
	longlong_t	dint;
	longlong_t	dto_res;
	longlong_t	drem;
	longlong_t	dfrom_res;
	longlong_t	prod;
	longlong_t	quot;
	long		numerator;
	long		result;
	ulong_t		modulus;
	ulong_t		twomodulus;
	long		temp;

	if (htp->hrt_res == 0 || new_res == 0 ||
	    new_res > NANOSEC || htp->hrt_rem < 0)
		return (-1);

	if (htp->hrt_rem >= htp->hrt_res) {
		htp->hrt_secs += htp->hrt_rem / htp->hrt_res;
		htp->hrt_rem = htp->hrt_rem % htp->hrt_res;
	}

	interval = htp->hrt_rem;
	if (interval == 0) {
		htp->hrt_res = new_res;
		return (0);
	}

	/*
	 * Try to do the calculations in single precision first
	 * (for speed).  If they overflow, use double precision.
	 * What we want to compute is:
	 *
	 *	(interval * new_res) / hrt->hrt_res
	 */
	numerator = interval * new_res;

	if (numerator / new_res  ==  interval) {

		/*
		 * The above multiply didn't give overflow since
		 * the division got back the original number.  Go
		 * ahead and compute the result.
		 */
		result = numerator / htp->hrt_res;

		/*
		 * For HRT_RND, compute the value of:
		 *
		 *	(interval * new_res) % htp->hrt_res
		 *
		 * If it is greater than half of the htp->hrt_res,
		 * then rounding increases the result by 1.
		 *
		 * For HRT_RNDUP, we increase the result by 1 if:
		 *
		 *	result * htp->hrt_res != numerator
		 *
		 * because this tells us we truncated when calculating
		 * result above.
		 *
		 * We also check for overflow when incrementing result
		 * although this is extremely rare.
		 */

		if (round == HRT_RND) {
			modulus = numerator - result * htp->hrt_res;
			if ((twomodulus = 2 * modulus) / 2 == modulus) {
				/*
				 * No overflow (if we overflow in calculation
				 * of twomodulus we fall through and use
				 * double precision).
				 */
				if (twomodulus >= htp->hrt_res) {
					temp = result + 1;
					if (temp - 1 == result)
						result++;
					else
						return (-1);
				}

				htp->hrt_res = new_res;
				htp->hrt_rem = result;
				return (0);
			}
		} else {
			if (round == HRT_RNDUP) {
				if (result * htp->hrt_res != numerator) {
					temp = result + 1;
					if (temp - 1 == result)
						result++;
					else
						return (-1);
				}
				htp->hrt_res = new_res;
				htp->hrt_rem = result;
				return (0);
			} else {	/* round == HRT_TRUNC */
				htp->hrt_res = new_res;
				htp->hrt_rem = result;
				return (0);
			}
		}
	}

	/*
	 * We would get overflow doing the calculation is
	 * single precision so do it the slow but careful way.
	 *
	 * Compute the interval times the resolution we are
	 * going to.
	 */
	dint = interval;
	dto_res = new_res;
	prod = dint * dto_res;

	/*
	 * For HRT_RND the result will be equal to:
	 *
	 *	((interval * new_res) + htp->hrt_res / 2) / htp->hrt_res
	 *
	 * and for HRT_RNDUP we use:
	 *
	 *	((interval * new_res) + htp->hrt_res - 1) / htp->hrt_res
	 *
	 * This is a different but equivalent way of rounding.
	 */
	if (round == HRT_RND) {
		drem = htp->hrt_res / 2;
		prod = prod + drem;
	} else {
		if (round == HRT_RNDUP) {
			drem = htp->hrt_res - 1;
			prod = prod + drem;
		}
	}

	dfrom_res = htp->hrt_res;
	quot = prod / dfrom_res;

	/*
	 * If the quotient won't fit in a long, then we have
	 * overflow.  Otherwise, return the result.
	 */
	if (quot > UINT_MAX) {
		return (-1);
	} else {
		htp->hrt_res = new_res;
		htp->hrt_rem = (int)quot;
		return (0);
	}
}
