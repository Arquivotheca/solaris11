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
 * Copyright (c) 1988, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * du -- summarize disk usage
 *	du [-dorx] [-a|-s] [-h|-k|-m] [-H|-L] [file...]
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/avl.h>
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <libcmdutils.h>


static int		aflg = 0;
static int		rflg = 0;
static int		sflg = 0;
static int		kflg = 0;
static int		mflg = 0;
static int		oflg = 0;
static int		dflg = 0;
static int		hflg = 0;
static int		Hflg = 0;
static int		Lflg = 0;
static int		cmdarg = 0;	/* Command line argument */
static char		*dot = ".";
static int		level = 0;	/* Level of recursion */

static char		*base;
static char		*name;
static size_t		base_len = PATH_MAX + 1;    /* # of chars for base */
static size_t		name_len = PATH_MAX + 1;    /* # of chars for name */

#define	NUMBER_WIDTH	64
typedef char		numbuf_t[NUMBER_WIDTH];

/*
 * Output formats.  Solaris uses a tab as separator, XPG4 a space.
 */
#ifdef XPG4
#define	FORMAT1	"%s %s\n"
#define	FORMAT2	"%lld %s\n"
#else
#define	FORMAT1	"%s\t%s\n"
#define	FORMAT2	"%lld\t%s\n"
#endif

/*
 * convert DEV_BSIZE blocks to K blocks
 */
#define	DEV_BSIZE	512
#define	DEV_KSHIFT	1
#define	DEV_MSHIFT	11
#define	kb(n)		(((u_longlong_t)(n)) >> DEV_KSHIFT)
#define	mb(n)		(((u_longlong_t)(n)) >> DEV_MSHIFT)

long	wait();
static u_longlong_t 	descend(char *curname, int curfd, int *retcode,
			    dev_t device);
static void		printsize(blkcnt_t blocks, char *path);
static void		exitdu(int exitcode);

static avl_tree_t	*tree = NULL;

/*
 * Macro and structures which allow descend() to use a loop instead of
 * doing recursion.
 */

/* Specify location to where EXIT_DIR returns */
enum	location	{ loc1 = 1, loc2 };

/*
 * Structure to save local variables when going to the top of the loop.
 */
typedef	struct	local_vars {
	struct	local_vars	*next;

	enum	location	where;

	char			*ebase0;
	char			*ebase;
	struct	stat		stb;
	struct	stat		stb1;
	int			fd;
	int			tmpflg;
	int			follow_symlinks;
	blkcnt_t		blocks;
	off_t			curoff;
	ptrdiff_t		offset;
	ptrdiff_t		offset0;
	struct dirent		*dp;
	char			dirbuf[PATH_MAX + 1];

	char			*curname;
	int			curfd;
	dev_t			device;
}	local_vars_t;

#define	SAVE_VARS() {							\
		v->ebase0 = ebase0;					\
		v->ebase = ebase;					\
		v->stb = stb;						\
		v->stb1 = stb1;						\
		v->fd = fd;						\
		v->tmpflg = tmpflg;					\
		v->follow_symlinks = follow_symlinks;			\
		v->blocks = blocks;					\
		v->curoff = curoff;					\
		v->offset = offset;					\
		v->offset0 = offset0;					\
		v->dp = dp;						\
		(void) strcpy(v->dirbuf, dirbuf);			\
		v->curname = curname;					\
		v->curfd = curfd;					\
		v->device = device;					\
	}

#define	RESTORE_VARS() {						\
		ebase0 = v->ebase0;					\
		ebase = v->ebase;					\
		stb = v->stb;						\
		stb1 = v->stb1;						\
		fd = v->fd;						\
		tmpflg = v->tmpflg;					\
		follow_symlinks = v->follow_symlinks;			\
		blocks = v->blocks;					\
		curoff = v->curoff;					\
		offset = v->offset;					\
		offset0 = v->offset0;					\
		dp = v->dp;						\
		(void) strncpy(dirbuf, v->dirbuf, sizeof (dirbuf));	\
		curname = v->curname;					\
		curfd = v->curfd;					\
		device = v->device;					\
	}

#define	INIT_VARS(c_name, c_fd, dev) {					\
		blocks = 0;						\
		curoff = 0;						\
		dirbuf[0] = '\0';					\
		curname = c_name;					\
		curfd = c_fd;						\
		device = dev;						\
	}

#define	DIR_ENTRY(loc, c_name, c_fd, dev) {				\
		v = (local_vars_t *)malloc(sizeof (local_vars_t));	\
		if (v == NULL) {					\
			perror("du: ");					\
			exitdu(1);					\
		}							\
		v->where = loc;						\
		/* Push save area on stack */				\
		v->next = stack;					\
		stack = v;						\
		SAVE_VARS();						\
		INIT_VARS(c_name, c_fd, dev);				\
		/* Enter directory */					\
		goto enter_dir;						\
loc :		/* Return to here from EXIT_DIR */			\
		/* Pop v */						\
		v = stack;						\
		stack = v->next;					\
		RESTORE_VARS();						\
		free(v);						\
	}

#define	DIR_RETURN(rc) {						\
		retval = rc;						\
		if (stack == NULL) {					\
			/* At top level again, all done */		\
			goto done;					\
		}							\
		if (stack->where == loc1)				\
			goto loc1;					\
		else if (stack->where == loc2)				\
			goto loc2;					\
		else {							\
			/* unknow location */				\
			(void) fprintf(stderr,				\
			    gettext("du: unknow directory location\n")); \
			exitdu(1);					\
		}							\
	}

int
main(int argc, char **argv)
{
	blkcnt_t	blocks = 0;
	int		c;
	extern int	optind;
	char		*np;
	pid_t		pid, wpid;
	int		status, retcode = 0;
	setbuf(stderr, NULL);
	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

#ifdef XPG4
	rflg++;		/* "-r" is not an option but ON always */
#endif

	while ((c = getopt(argc, argv, "adhHkLmorsx")) != EOF)
		switch (c) {

		case 'a':
			aflg++;
			continue;

		case 'h':
			hflg++;
			kflg = 0;
			mflg = 0;
			continue;

		case 'r':
			rflg++;
			continue;

		case 's':
			sflg++;
			continue;

		case 'k':
			kflg++;
			hflg = 0;
			mflg = 0;
			continue;

		case 'm':
			mflg++;
			hflg = 0;
			kflg = 0;
			continue;

		case 'o':
			oflg++;
			continue;

		case 'd':
			dflg++;
			continue;

		case 'x':
			dflg++;
			continue;

		case 'H':
			Hflg++;
			/* -H and -L are mutually exclusive */
			Lflg = 0;
			cmdarg++;
			continue;

		case 'L':
			Lflg++;
			/* -H and -L are mutually exclusive */
			Hflg = 0;
			cmdarg = 0;
			continue;
		case '?':
			(void) fprintf(stderr, gettext(
			    "usage: du [-dorx] [-a|-s] [-h|-k|-m] [-H|-L] "
			    "[file...]\n"));
			exit(2);
		}
	if (optind == argc) {
		argv = &dot;
		argc = 1;
		optind = 0;
	}

	/* "-o" and "-s" don't make any sense together. */
	if (oflg && sflg)
		oflg = 0;

	if ((base = (char *)calloc(base_len, sizeof (char))) == NULL) {
		perror("du");
		exit(1);
	}
	if ((name = (char *)calloc(name_len, sizeof (char))) == NULL) {
		perror("du");
		free(base);
		exit(1);
	}
	do {
		if (optind < argc - 1) {
			pid = fork();
			if (pid == (pid_t)-1) {
				perror(gettext("du: No more processes"));
				exitdu(1);
			}
			if (pid != 0) {
				while ((wpid = wait(&status)) != pid &&
				    wpid != (pid_t)-1)
					;
				if (pid != (pid_t)-1 && status != 0)
					retcode = 1;
			}
		}
		if (optind == argc - 1 || pid == 0) {
			while (base_len < (strlen(argv[optind]) + 1)) {
				base_len = base_len * 2;
				if ((base = (char *)realloc(base, base_len *
				    sizeof (char))) == NULL) {
					if (rflg) {
						(void) fprintf(stderr, gettext(
						    "du: can't process %s"),
						    argv[optind]);
						perror("");
					}
					exitdu(1);
				}
			}
			if (base_len > name_len) {
				name_len = base_len;
				if ((name = (char *)realloc(name, name_len *
				    sizeof (char))) == NULL) {
					if (rflg) {
						(void) fprintf(stderr, gettext(
						    "du: can't process %s"),
						    argv[optind]);
						perror("");
					}
					exitdu(1);
				}
			}
			(void) strcpy(base, argv[optind]);
			(void) strcpy(name, argv[optind]);
			if (np = strrchr(name, '/')) {
				*np++ = '\0';
				if (chdir(*name ? name : "/") < 0) {
					if (rflg) {
						(void) fprintf(stderr, "du: ");
						perror(*name ? name : "/");
						exitdu(1);
					}
					exitdu(0);
				}
			} else
				np = base;
			blocks = descend(*np ? np : ".", 0, &retcode,
			    (dev_t)0);
			if (sflg)
				printsize(blocks, base);
			if (optind < argc - 1)
				exitdu(retcode);
		}
		optind++;
	} while (optind < argc);
	exitdu(retcode);

	return (retcode);
}


/*
 * descend directories, adding up the allocated blocks.
 * If curname is NULL, curfd is used.
 */
static u_longlong_t
descend(char *a_curname, int a_curfd, int *retcode, dev_t a_device)
{
	static DIR		*dirp = NULL;
	int			i;
	int			j;
	int			ret;

	char			*ebase0;
	char			*ebase;
	struct	stat		stb;
	struct	stat		stb1;
	int			fd;
	int			tmpflg;
	int			follow_symlinks;
	blkcnt_t		blocks = 0;
	off_t			curoff = 0;
	ptrdiff_t		offset;
	ptrdiff_t		offset0;
	struct dirent		*dp = NULL;
	char			dirbuf[PATH_MAX + 1] = "";

	char			*curname = a_curname;
	int			curfd = a_curfd;
	dev_t			device = a_device;

	local_vars_t		*stack = NULL;
	local_vars_t		*v;
	u_longlong_t		retval;

enter_dir :
	ebase0 = ebase = strchr(base, 0);
	if (ebase > base && ebase[-1] == '/')
		ebase--;
	offset = ebase - base;
	offset0 = ebase0 - base;

	if (curname)
		curfd = AT_FDCWD;

	/*
	 * If neither a -L or a -H was specified, don't follow symlinks.
	 * If a -H was specified, don't follow symlinks if the file is
	 * not a command line argument.
	 */
	follow_symlinks = (Lflg || (Hflg && cmdarg));
	if (follow_symlinks) {
		i = fstatat(curfd, curname, &stb, 0);
		j = fstatat(curfd, curname, &stb1, AT_SYMLINK_NOFOLLOW);

		/*
		 * Make sure any files encountered while traversing the
		 * hierarchy are not considered command line arguments.
		 */
		if (Hflg) {
			cmdarg = 0;
		}
	} else {
		i = fstatat(curfd, curname, &stb, AT_SYMLINK_NOFOLLOW);
		j = 0;
	}

	if ((i < 0) || (j < 0)) {
		if (rflg) {
			(void) fprintf(stderr, "du: ");
			perror(base);
		}

		/*
		 * POSIX states that non-zero status codes are only set
		 * when an error message is printed out on stderr
		 */
		*retcode = (rflg ? 1 : 0);
		*ebase0 = 0;
		DIR_RETURN(0);
	}
	if (device) {
		if (dflg && stb.st_dev != device) {
			*ebase0 = 0;
			DIR_RETURN(0);
		}
	}
	else
		device = stb.st_dev;

	/*
	 * If following links (-L) we need to keep track of all inodes
	 * visited so they are only visited/reported once and cycles
	 * are avoided.  Otherwise, only keep track of files which are
	 * hard links so they only get reported once, and of directories
	 * so we don't report a directory and its hierarchy more than
	 * once in the special case in which it lies under the
	 * hierarchy of a directory which is a hard link.
	 * Note:  Files with multiple links should only be counted
	 * once.  Since each inode could possibly be referenced by a
	 * symbolic link, we need to keep track of all inodes when -L
	 * is specified.
	 */
	if (Lflg || ((stb.st_mode & S_IFMT) == S_IFDIR) ||
	    (stb.st_nlink > 1)) {
		int rc;
		if ((rc = add_tnode(&tree, stb.st_dev, stb.st_ino)) != 1) {
			if (rc == 0) {
				/*
				 * This hierarchy, or file with multiple
				 * links, has already been visited/reported.
				 */
				DIR_RETURN(0);
			} else {
				/*
				 * An error occurred while trying to add the
				 * node to the tree.
				 */
				if (rflg) {
					perror("du");
				}
				exitdu(1);
			}
		}
	}
	blocks = stb.st_blocks;
	/*
	 * If there are extended attributes on the current file, add their
	 * block usage onto the block count.  Note: Since pathconf() always
	 * follows symlinks, only test for extended attributes using pathconf()
	 * if we are following symlinks or the current file is not a symlink.
	 */
	if (curname && (follow_symlinks ||
	    ((stb.st_mode & S_IFMT) != S_IFLNK)) &&
	    pathconf(curname, _PC_XATTR_EXISTS) == 1) {
		if ((fd = attropen(curname, ".", O_RDONLY)) < 0) {
			if (rflg)
				perror(gettext(
				    "du: can't access extended attributes"));
		}
		else
		{
			tmpflg = sflg;
			sflg = 1;
			DIR_ENTRY(loc1, NULL, fd, device);
			blocks += retval;
			sflg = tmpflg;
		}
	}
	if ((stb.st_mode & S_IFMT) != S_IFDIR) {
		/*
		 * Don't print twice: if sflg, file will get printed in main().
		 * Otherwise, level == 0 means this file is listed on the
		 * command line, so print here; aflg means print all files.
		 */
		if (sflg == 0 && (aflg || level == 0))
			printsize(blocks, base);
		DIR_RETURN(blocks);
	}
	if (dirp != NULL)
		/*
		 * Close the parent directory descriptor, we will reopen
		 * the directory when we pop up from this level of the
		 * recursion.
		 */
		(void) closedir(dirp);
	if (curname == NULL)
		dirp = fdopendir(curfd);
	else
		dirp = opendir(curname);
	if (dirp == NULL) {
		if (rflg) {
			(void) fprintf(stderr, "du: ");
			perror(base);
		}
		*retcode = 1;
		*ebase0 = 0;
		DIR_RETURN(0);
	}
	level++;
	if (curname == NULL || (Lflg && S_ISLNK(stb1.st_mode))) {
		if (getcwd(dirbuf, PATH_MAX) == NULL) {
			if (rflg) {
				(void) fprintf(stderr, "du: ");
				perror(base);
			}
			exitdu(1);
		}
	}
	if ((curname ? (chdir(curname) < 0) : (fchdir(curfd) < 0))) {
		if (rflg) {
			(void) fprintf(stderr, "du: ");
			perror(base);
		}
		*retcode = 1;
		*ebase0 = 0;
		(void) closedir(dirp);
		dirp = NULL;
		level--;
		DIR_RETURN(0);
	}
	while (dp = readdir(dirp)) {
		if ((strcmp(dp->d_name, ".") == 0) ||
		    (strcmp(dp->d_name, "..") == 0))
			continue;
		/*
		 * we're about to append "/" + dp->d_name
		 * onto end of base; make sure there's enough
		 * space
		 */
		while ((offset + strlen(dp->d_name) + 2) > base_len) {
			base_len = base_len * 2;
			if ((base = (char *)realloc(base,
			    base_len * sizeof (char))) == NULL) {
				if (rflg) {
					perror("du");
				}
				exitdu(1);
			}
			ebase = base + offset;
			ebase0 = base + offset0;
		}
		/* LINTED - unbounded string specifier */
		(void) sprintf(ebase, "/%s", dp->d_name);
		curoff = telldir(dirp);
		DIR_ENTRY(loc2, ebase + 1, 0, device);
		/* base may have been moved via realloc in lower directory */
		ebase = base + offset;
		ebase0 = base + offset0;
		*ebase = 0;
		blocks += retval;
		if (dirp == NULL) {
			if ((dirp = opendir(".")) == NULL) {
				if (rflg) {
					(void) fprintf(stderr,
					    gettext("du: Can't reopen in "));
					perror(base);
				}
				*retcode = 1;
				level--;
				DIR_RETURN(0);
			}
			seekdir(dirp, curoff);
		}
	}
	(void) closedir(dirp);
	level--;
	dirp = NULL;
	if (sflg == 0)
		printsize(blocks, base);
	if (curname == NULL || (Lflg && S_ISLNK(stb1.st_mode)))
		ret = chdir(dirbuf);
	else
		ret = chdir("..");
	if (ret < 0) {
		if (rflg) {
			(void) sprintf(strchr(base, '\0'), "/..");
			(void) fprintf(stderr,
			    gettext("du: Can't change dir to '..' in "));
			perror(base);
		}
		exitdu(1);
	}
	*ebase0 = 0;
	if (oflg) {
		DIR_RETURN(0);
	} else {
		DIR_RETURN(blocks);
	}
done:
	return (retval);
}

/*
 * Convert an unsigned long long to a string representation and place the
 * result in the caller-supplied buffer.
 * The given number is in units of "unit_from" size,
 * this will first be converted to a number in 1024 or 1000 byte size,
 * depending on the scaling factor.
 * Then the number is scaled down until it is small enough to be in a good
 * human readable format i.e. in the range 0 thru scale-1.
 * If it's smaller than 10 there's room enough to provide one decimal place.
 * The value "(unsigned long long)-1" is a special case and is always
 * converted to "-1".
 * Returns a pointer to the caller-supplied buffer.
 */
static char *
number_to_scaled_string(
	numbuf_t buf,			/* put the result here */
	unsigned long long number,	/* convert this number */
	unsigned long long unit_from,	/* number of bytes per input unit */
	unsigned long long scale)	/* 1024 (-h)  or 1000 (-H) */
{
	unsigned long long save = 0;
	char *M = "KMGTPE"; /* Measurement: kilo, mega, giga, tera, peta, exa */
	char *uom = M;    /* unit of measurement, initially 'K' (=M[0]) */

	if ((long long)number == (long long)-1) {
		(void) strcpy(buf, "-1");
		return (buf);
	}

	/*
	 * Convert number from unit_from to given scale (1024 or 1000)
	 * This means multiply number with unit_from and divide by scale.
	 * if number is large enough, we first divide and then multiply
	 * 	to avoid an overflow
	 * 	(large enough here means 100 (rather arbitrary value)
	 *	times scale in order to reduce rounding errors)
	 * otherwise, we first multiply and then divide
	 * 	to avoid an underflow
	 */
	if (number >= 100L * scale) {
		number = number / scale;
		number = number * unit_from;
	} else {
		number = number * unit_from;
		number = number / scale;
	}

	/*
	 * Now we have number as a count of scale units.
	 * Stop scaling when we reached exa bytes, then something is
	 * probably wrong with our number.
	 */
	while ((number >= scale) && (*uom != 'E')) {
		uom++; /* next unit of measurement */
		save = number;
		number = (number + (scale / 2)) / scale;
	}

	/* check if we should output a decimal place after the point */
	if (save && ((save / scale) < 10)) {
		/* sprintf() will round for us */
		float fnum = (float)save / scale;
		(void) sprintf(buf, "%4.1f%c", fnum, *uom);
	} else {
		(void) sprintf(buf, "%4llu%c", number, *uom);
	}
	return (buf);
}

static void
printsize(blkcnt_t blocks, char *path)
{
	if (hflg) {
		numbuf_t numbuf;
		unsigned long long scale = 1024L;
		(void) printf(FORMAT1,
		    number_to_scaled_string(numbuf, blocks, DEV_BSIZE, scale),
		    path);
	} else if (kflg) {
		(void) printf(FORMAT2, (long long)kb(blocks), path);
	} else if (mflg) {
		(void) printf(FORMAT2, (long long)mb(blocks), path);
	} else {
		(void) printf(FORMAT2, (long long)blocks, path);
	}
}

static void
exitdu(int exitcode)
{
	free(base);
	free(name);
	exit(exitcode);
}
