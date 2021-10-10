/*
 * Copyright (c) 1989, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * UNIX shell
 *
 * S. R. Bourne
 * Rewritten by David Korn
 * AT&T Bell Laboratories
 *
 */

#include	<errno.h>
#include	<string.h>
#include	"defs.h"
#include	"jobs.h"
#include	"sym.h"
#include	"builtins.h"
#include	"history.h"
#include	<sys/auxv.h>	/* for AT_SUN_EXECNAME */

#define	MAXDEPTH (32*sizeof (int))	/* maximum levels of recursion */

extern int		gscan_some();
#ifdef SUID_EXEC
    extern char		*utos();
#endif /* SUID_EXEC */

static char		*prune();
static char		*execs();
static int		canexecute();
#ifndef VFORK
    static void		 exscript();
#endif /* VFORK */
#ifdef VPIX
    static int		suffix_offset;
    extern char		*suffix_list[];
    static int		 dospath();
#endif /* VPIX */

static struct namnod	*tracknod;
const char  *xecmsg = (char *)NULL;
static char		**xecenv;
static int		pruned;
static char		cwd[PATH_MAX+1];

/* make sure PWD is set up correctly */

/*
 * Return the present working directory
 * Invokes /bin/pwd if flag==0 and if necessary
 * Sets the PWD variable to this value
 */

/* CSI assumption1(ascii) made here. See csi.h. */
char *
path_pwd(flag)
int flag;
{
	char *cp;
	char *dfault = (char *)e_dot;
	int count = 0;
	extern MSG	e_crondir;


	while (1)
	{
		/* try from lowest to highest */
		switch (count++)
		{
			case 0:
				cp = nam_strval(PWDNOD);
				break;
			case 1:
				cp = nam_strval(HOME);
				break;
			case 2:
				cp = "/";
				break;
			case 3:
				cp = (char *)e_crondir;
				/* skip next case when non-zero flag */
				if (flag)
					++count;
				break;
			case 4:
				cp = getcwd(cwd, sizeof (cwd));
				if (cp && *cp == '/')
					dfault = cp;
				break;
			case 5:
				return (dfault);
		}
		if (cp && *cp == '/' && test_inode(cp, (char *)e_dot))
			break;
	}
	if (count > 1)
		nam_fputval(PWDNOD, cp);
	nam_ontype(PWDNOD, N_FREE|N_EXPORT);
	sh.pwd = PWDNOD->value.namval.cp;
	return (cp);
}

/*
 * given <s> return a colon separated list of directories to search on the stack
 * This routine adds names to the tracked alias list, if possible, and returns
 * a reduced path string for tracked aliases
 */

/* CSI assumption5(slash) made here. See csi.h. */
char *
path_get(s)
const char *s;
/*
 *	assume s!=NULL;
 *	return path satisfying path!=NULL;
 */
{
	register char *path;
	register char *sp = sh.lastpath;
	if (strchr(s, '/'))
		return (NULLSTR);
	path = nam_fstrval(PATHNOD);
	if (path == NULL)
		path = (char *)e_defpath;
	path = stakcopy(path);
	if (sp || ((tracknod = nam_search(s, sh.track_tree, 0)) &&
		nam_istype(tracknod, NO_ALIAS) == 0 &&
		(sp = nam_strval(tracknod))))
	{
		path = prune(path, sp);
		pruned++;
	}
	return (path);
}

/* CSI assumption5(slash) made here. See csi.h. */
int
path_open(name, path)
register const char *name;
register char *path;
/*
 *	assume name!=NULL;
 */
{
	register int n;
	struct stat statb;
	sigset_t omask;

	if (strchr(name, '/'))
	{
		if (is_option(RSHFLG))
			sh_fail(name, e_restricted);
	}
	else
	{
		if (path == NULL)
			path = (char *)e_defpath;
		path = stakcopy(path);
	}

	(void) sigprocmask(SIG_BLOCK, &childmask, &omask);
	do
	{
		path = path_join(path, name);
		if ((n = open(path_relative(stakptr(OPATH)), O_RDONLY)) >= 0)
		{
			if (fstat(n, &statb) < 0 || S_ISDIR(statb.st_mode))
			{
				close(n);
				n = -1;
			}
		}
	} while (n < 0 && path);
	(void) sigprocmask(SIG_SETMASK, &omask, NULL);

	/* opened file must have file descriptor >=2 */
	n = io_movefd(n);
	return (n);
}

#ifdef VPIX
/*
 * This routine returns 1 if first directory in path is also in
 * the DOSPATH variable, 0 otherwise
 */

static int dospath(path)
char *path;
{
	register char *dp = nam_fstrval(DOSPATHNOD);
	register char *sp = path;
	register int c;
	int match = 1;
	int pwd = 0;	/* set for in preset working directory */
	if (dp == 0 || *sp == 0)
		return (0);
	if (sp == 0)
		return (0);
	if (*sp == ':')
	{
		sp = path = path_pwd(1);
		pwd++;
	}
	if (pwd && *dp == ':')
		return (1);
	while (1)
	{
		if ((c = *dp++) == 0 || c == ':')
		{
			if (match == 1 && (*sp == 0 || *sp == ':'))
				return (1);
			if (c == 0)
				return (0);
			if (pwd && (*dp == ':' || *dp == 0))
				return (1);
			match = 1;
			sp = path;
		} else if (match)
		{
			if (*sp++ != c)
				match = 0;
		}
	}
	/* NOTREACHED */
}
#endif /* VPIX */

/*
 *  set tracked alias node <np> to value <sp>
 */

void
path_alias(np, sp)
register struct namnod *np;
register char *sp;
/*
 *	assume np!=NULL;
 */
{
	if (sp == NIL)
		nam_free(np);
	else
	{
		char *vp = np->value.namval.cp;
		int n = 1;
		int nofree = nam_istype(np, N_FREE);
		nam_offtype(np, ~NO_PRINT);
		if (vp == 0 || (n = strcmp(sp, vp)) != 0)
			nam_putval(np, sp);
		nam_typeset(np, T_FLAG|N_EXPORT);
		if (nofree && n == 0)
			nam_ontype(np, N_FREE);
	}
}


/*
 * given a pathname return the base name
 */

/* CSI assumption1(ascii) made here. See csi.h. */
char *
path_basename(name)
const char *name;
/*
 *	assume name!=NULL;
 *	return x satisfying x>=name && *x!='/';
 */
{
	register const char *start = name;
	while (*name) {
		int	len;
		if ((*name == '/') && *(name + 1)) /* don't trim trailing / */
			start = name + 1;
		(void) mb_nextc(&name);
	}
	return ((char *)start);
}

/*
 * do a path search and track alias if requested
 * if flag is 0, or if name not found, then try autoloading function
 * if flag==2, returns 1 if name found on FPATH
 * returns 1, if function was autoloaded.
 */

int
path_search(name, flag)
register const char *name;
/*
 *	assume name!=NULL;
 *	assume flag==0 || flag==1 || flag==2;
 */
{
	struct namnod *np;
	int fno;
	if (flag)
	{
		/* if not found on pruned path, rehash and try again */
		while ((sh.lastpath = path_absolute(name)) == 0 && pruned)
			nam_ontype(tracknod, NO_ALIAS);
	}
	if (flag == 0 || sh.lastpath == 0)
	{
		register char *path;
		int savestates;
		path = nam_fstrval(FPATHNOD);
		if (path && (fno = path_open(name, path)) >= 0)
		{
			if (flag == 2)
			{
				close(fno);
				return (1);
			}
			sh.un.fd = fno;
			st.exec_flag--;
			savestates = st.states;
			st.states |= NOLOG;
			sh_eval(NIL);
			if ((st.states & RM_TMP) != 0)
				savestates |= RM_TMP;
			st.states = savestates;
			st.exec_flag++;
			if ((np = nam_search(name, sh.fun_tree, 0))&&
			    np->value.namval.ip)
				return (1);
		}
		return (0);
	} else
	{
		if ((np = tracknod) || (is_option(HASHALL) &&
		    (np = nam_search(name, sh.track_tree, N_ADD))))
			path_alias(np, sh.lastpath);
	}
	return (0);
}


/*
 * do a path search and find the full pathname of file name
 */

char *
path_absolute(name)
register const char *name;
/*
 *	assume name!=NULL;
 *	return x satisfying x && *x=='/';
 */
{
	int	f;
	char *path;
#ifdef VPIX
	char **suffix = 0;
	char *top;
#endif /* VPIX */
	pruned = 0;
	path = path_get(name);
	do
	{
		if (sh.trapnote & SIGSET)
			sh_exit(SIGFAIL);
#ifdef VPIX
		if (suffix == 0)
		{
			if (dospath(path))
				suffix = suffix_list;
			path = path_join(path, name);
			if (suffix)
				top = stakptr(suffix_offset);
		}
		if (suffix)
		{
			sh_copy(*suffix, top);
			if (**suffix == 0)
				suffix = 0;
			else
				suffix++;
		}
#else
		path = path_join(path, name);
#endif /* VPIX */
		f = canexecute(stakptr(OPATH));
	}
#ifdef VPIX
	while (f < 0 && (path || suffix));
#else
	while (f < 0 && path);
#endif /* VPIX */
	if (f < 0)
		return (0);
	/* check for relative pathname */
	if (*stakptr(OPATH) != '/')
		(void) path_join(path_pwd(1), (char *)stakfreeze(1) + OPATH);
	return ((char *)stakfreeze(1) + OPATH);
}

/*
 * returns 0 if path can execute
 * sets xecmsg to e_exec if file is found but can't be executable
 */
#undef S_IXALL
#ifdef S_IXUSR
#define	S_IXALL	(S_IXUSR|S_IXGRP|S_IXOTH)
#else
#ifdef S_IEXEC
#define	S_IXALL	(S_IEXEC|(S_IEXEC>>3)|(S_IEXEC>>6))
#else
#define	S_IXALL	0111
#endif /* S_EXEC */
#endif /* S_IXUSR */

static int
canexecute(path)
register char *path;
/*
 *	assume path!=NULL;
 */
{
	struct stat statb;
	int ret = -1;
	sigset_t omask;

	path = path_relative(path);
	xecmsg = e_found;
	(void) sigprocmask(SIG_BLOCK, &childmask, &omask);
	if (stat(path, &statb) < 0)
	{
		if (errno != ENOENT)
			xecmsg = e_exec;
		goto out;
	}
	xecmsg = e_exec;
	if (!S_ISDIR(statb.st_mode))
	{
		if ((statb.st_mode&S_IXALL) == S_IXALL)
			ret = 0;
		else
			ret = sh_access(path, X_OK);
	}
out:	(void) sigprocmask(SIG_SETMASK, &omask, NULL);
	return (ret);
}

#ifndef INT16
/*
 * Return path relative to present working directory
 */

/* CSI assumption1(ascii) made here. See csi.h. */
char *
path_relative(file)
register const char *file;
/*
 *	assume file!=NULL;
 *	return x satisfying x!=NULL;
 */
{
	char *pwd;
	char *fp = (char *)file;
	/* can't relpath when sh.pwd not set */
	if (!(pwd = sh.pwd))
		return (fp);
	while (*pwd == *fp)
	{
		if (*pwd++ == 0)
			return ((char *)e_dot);
		fp++;
	}
	if (*pwd == 0 && *fp == '/')
	{
		while (*++fp == '/');
		if (*fp)
			return (fp);
		return ((char *)e_dot);
	}
	return ((char *)file);
}
#endif /* INT16 */

/* CSI assumptions1(ascii),5(slash) made here. See csi.h. */
char *
path_join(path, name)
register char *path;
const char *name;
/*
 *	assume path!=NULL;
 *	assume name!=NULL;
 */
{
	/* leaves result on top of stack */
	char *scanp = path;
	register int c;
	wchar_t w;
	stakseek(OPATH);
	/* eliminate . and ./ */
	if (*scanp == '.')
	{
		if ((c = *++scanp) == 0 || c == ':')
			path = scanp;
		else if (c == '/')
			path = ++scanp;
		else
			scanp--;
	}
	while ((w = mb_peekc((const char *)scanp)) != L'\0' && w != L':') {
		stakputwc(w);
		(void) mb_nextc((const char **)&scanp);
	}
	if (scanp != path)
	{
		if (scanp[-1] != '/')
			stakputascii('/');
		/* position past ":" unless a trailing colon after pathname */
		if (*scanp && *++scanp == 0)
			scanp--;
	}
	else
		while (*scanp == ':')
			scanp++;
	path = (*scanp ? scanp : 0);
	stakputs(name);
#ifdef VPIX
	/* make sure that there is room for suffixes */
	suffix_offset = staktell();
	stakputs(*suffix_list);
	*stakptr(suffix_offset) = 0;
#endif /* VPIX */
	return (path);
}

/* CSI assumption5(slash) made here. See csi.h. */
void
path_exec(at, local)
char *at[];
struct argnod *local;		/* local environment modification */
/*
 * assume at!=NULL && *at!=NULL;
 */
{
	register const char *path = e_nullstr;
	register char **t = at;
	register int erret;

	xecmsg = e_found;
#ifdef VFORK
	if (local)
		nam_scope(local);
	xecenv = env_gen();
#else
	env_setlist(local, N_EXPORT);
	xecenv = env_gen();
#endif	/* VFORK */
	if (strchr(t[0], '/'))
	{
		/* name containing / not allowed for restricted shell */
		if (is_option(RSHFLG))
			sh_fail(t[0], e_restricted);
	}
	else
		path = path_get(*t);
#ifdef VFORK
	if (local)
		nam_unscope();
#endif	/* VFORK */
	/* leave room for inserting _= pathname in environment */
	xecenv--;
	while (path = execs((char *)path, t));
	/* XPG4: exit status must conform XPG4 standard */
	if (xecmsg == e_found)
		erret = ENOTFOUND;
	else if (xecmsg == e_exec)
		erret = ECANTEXEC;
	else
		erret = ERROR;
	cmd_shfail(*t, xecmsg, erret);
}

/*
 * This routine constructs a short path consisting of all
 * Relative directories up to the directory of fullname <name>
 */
/* CSI assumption1(ascii) made here. See csi.h. */
static char *
prune(path, fullname)
register char *path;
const char *fullname;
/*
 *	assume path!=NULL;
 *	return x satisfying x!=NULL && strlen(x)<=strlen(in path);
 */
{
	register char *p = path;
	register char *s;
	int n = 1;
	const char *base;
	char *inpath = path;
	if (fullname == NULL || *fullname != '/' || *path == 0)
		return (path);
	base = path_basename(fullname);
	do
	{
		/* a null path means current directory */
		if (*path == ':')
		{
			*p++ = ':';
			path++;
			continue;
		}
		s = path;
		path = path_join(path, base);
		if (*s != '/' || (n = strcmp(stakptr(OPATH), fullname)) == 0)
		{
			/* position p past end of path */
			while (*s && *s != ':')
				*p++ = *s++;
			if (n == 0)
			{
				*p = 0;
				return (inpath);
			}
			*p++ = ':';
		}
	}
	while (path);
	/* if there is no match just return path */
	path = nam_fstrval(PATHNOD);
	if (path == NULL)
		path = (char *)e_defpath;
	strcpy(inpath, path);
	return (inpath);
}

#ifdef XENIX
/*
 *  This code takes care of a bug in the XENIX exec routine
 *  Contributed by Pat Wood
 */
static ex_xenix(file)
char *file;
{
	struct stat stats;
	register int fd;
	unsigned short magic;
	/* can't read, so can't be shell prog */
	if ((fd = open(file, O_RDONLY)) == -1)
		return (1);
	read(fd, &magic, sizeof (magic));
	if (magic == 01006) /* magic for xenix executable */
	{
		close(fd);
		return (1);
	}
	fstat(fd, &stats);
	close(fd);
	errno = ENOEXEC;
	if (!geteuid())
	{
		if (!(stats.st_mode & 0111))
			errno = EACCES;
		return (0);
	}
	if ((geteuid() == stats.st_uid))
	{
		if (!(stats.st_mode & 0100))
			errno = EACCES;
		return (0);
	}
	if ((getegid() == stats.st_gid))
	{
		if (!(stats.st_mode & 0010))
			errno = EACCES;
		return (0);
	}
	if (!(stats.st_mode & 0001))
		errno = EACCES;
	return (0);
}
#endif	/* XENIX */

/* CSI assumption5(slash) made here. See csi.h. */
static char *
execs(ap, t)
char *ap;
register char **t;
/*
 *	assume ap!=NULL;
 *	assume t!=NULL && *t!=NULL;
 */
{
	int space = 0;
	char *p, *prefix;
	prefix = path_join(ap, t[0]);
	xecenv[0] =  stakptr(0);
	*stakptr(0) = '_';
	*stakptr(1) = '=';
	p = stakptr(OPATH);
	p_flush();

#ifdef VPIX
	if (dospath(ap))
	{
		char **suffix;
		char *savet = t[0];
		t[0] = p;
		t[-2] = (char *)e_vpix+1;
		t[-1] = "-c";
		suffix = suffix_list;
		while (**suffix)
		{
			char *vp;
			sh_copy(*suffix++, stakptr(suffix_offset));
			if (canexecute(p) >= 0)
			{
				stakfreeze(1);
				if (p = nam_fstrval(VPIXNOD))
					stakputs(p);
				else
					stakputs(e_vpixdir);
				stakputs(e_vpix);
				execve(stakptr(0), &t[-2], xecenv);

				switch (errno)
				{
					case ENOENT:
						sh_fail(vp, e_found);
					default:
						sh_fail(vp, e_exec);
				}
			}
		}
		t[0] = savet;
		*stakptr(suffix_offset) = 0;
	}
#endif /* VPIX */
	if (sh.trapnote&SIGSET)
		sh_exit(SIGFAIL);
#ifndef AT_SUN_EXECNAME	/* pruning the path hurts the execname feature */
	p = path_relative(p);
#endif
#ifdef XENIX
	if (ex_xenix(p))
#endif	/* XENIX */
#ifdef SHELLMAGIC
	if (*p != '/' && p != stakptr(OPATH) && strchr(p, '/') == (char *)NULL)
	{
		/*
		 * The following code because execv(foo,) and execv(./foo,)
		 * may not yield the same resulst
		 */
		char *sp = malloc(strlen(p)+3);
		sp[0] = '.';
		sp[1] = '/';
		strcpy(sp+2, p);
		p = sp;
		space++;
	}
#endif /* SHELLMAGIC */
	(void) execve(p, &t[0], xecenv);
#ifdef SHELLMAGIC
	if (*p == '.' && p != stakptr(OPATH) && space)
	{
		free(p);
		p = path_relative(stakptr(OPATH));
		space = 0;
	}
#endif /* SHELLMAGIC */
	switch (errno)
	{
#ifdef apollo
		/*
		 * On apollo's execve will fail with eacces when
		 * file has execute but not read permissions. So,
		 * for now we will pretend that EACCES and ENOEXEC
		 * mean the same thing.
		 */
		case EACCES:
#endif /* apollo */
		case ENOEXEC:
#ifdef VFORK
		{
	/* this code handles the !# interpreter name convention */
			char iname[PATH_MAX];
#ifdef SUID_EXEC
	/* check if file cannot open for read or script is setuid/setgid  */
			static char name[] = "/tmp/euidXXXXXXXXXXX";
			register int n;
			register uid_t euserid;
			struct stat statb;
			if ((n = open(p, O_RDONLY)) >= 0)
			{
				if (fstat(n, &statb) == 0)
				{
					if ((statb.st_mode &
					    (S_ISUID|S_ISGID)) == 0)
						goto openok;
				}
				close(n);
			}
			if ((euserid = geteuid()) != sh.userid)
			{
				strncpy(name+9, utos((long)getpid(),
				    10), sizeof (name)-10);
		/* create a suid open file with owner equal effective uid */
				if ((n = creat(name, 04100)) < 0)
					goto fail;
				unlink(name);
				/* make sure that file has right owner */
				if (fstat(n, &statb) < 0 ||
				    statb.st_uid != euserid)
					goto fail;
				if (n != 10)
				{
					close(10);
					fcntl(n, F_DUPFD, 10);
					close(n);
				}
			}
			*--t = p;
			execve(e_suidexec, t, xecenv);
	fail:
			sh_fail(p, e_open);
	openok:
			close(n);
#endif /* SUID_EXEC */
			/* get name returns the interpreter name */
			if (get_shell(p, iname) < 0)
				sh_fail(p, e_exec);
			t--;
			t[0] = iname;
			execve(iname, t, xecenv);
			if (sh_access(iname, F_OK) == 0)
				xecmsg = e_exec;
			sh_fail(iname, xecmsg);
		}
#else
			exscript(p, t);
#endif	/* VFORK */
#ifdef ENAMETOOLONG
		case ENAMETOOLONG:
			xecmsg = e_longname;
			return (prefix);
#endif /* ENAMETOOLONG */
		case ENOMEM:
			sh_fail(p, e_toobig);

		case E2BIG:
			sh_fail(p, e_arglist);

#ifdef ETXTBSY
		case ETXTBSY:
			sh_fail(p, e_txtbsy);
#endif /* ETXTBSY */
#ifdef ELIBACC
		case ELIBACC:
			sh_fail(p, e_libacc);

		case ELIBBAD:
			sh_fail(p, e_libbad);

		case ELIBSCN:
			sh_fail(p, e_libscn);

		case ELIBMAX:
			sh_fail(p, e_libmax);
#endif /* ELIBACC */

		default:
			if (sh_access(p, F_OK) == 0)
				xecmsg = e_exec;
		case ENOENT:
			return (prefix);
	}
}

/*
 * File is executable but not machine code.
 * Assume file is a Shell script and execute it.
 */


static void
exscript(p, t)
register char *p;
register char *t[];
/*
 *	assume p!=NULL;
 *	assume t!=NULL && *t!=NULL;
 */
{
#ifdef _OPTIM_
	st.flags.i[_LOW_] = 0;
	st.flags.i[_HIGH_] &= (HASHALL|EMACS|GMACS|VIRAW|EDITVI)>>16;
#else
	off_option(~(HASHALL|EMACS|GMACS|VIRAW|EDITVI));
#endif /* _OPTIM_ */
	sh.comdiv = 0;
	sh.bckpid = 0;
	st.ioset = 0;
	/* clean up any cooperating processes */
	if (sh.cpipe[INPIPE] > 0)
		io_pclose(sh.cpipe);
	if (sh.cpid)
		io_fclose(COTPIPE);
	arg_clear(); /* remove for loop junk */
	io_clear((struct fileblk *)0); /* remove open files */
	job_clear();
	if (input > 0 && input != F_STRING)
		io_fclose(input);
	st.states = 0;
	p_flush();
	st.standout = 1;
#ifdef SUID_EXEC
	/* check if file cannot open for read or script is setuid/setgid  */
	{
		static char name[] = "/tmp/euidXXXXXXXXXX";
		int n;
		uid_t euserid;
		char *savet;
		struct stat statb;
		sigset_t omask;

		(void) sigprocmask(SIG_BLOCK, &childmask, &omask);
		n = open(p, O_RDONLY);
		if (n >= 0)
		{
			if (fstat(n, &statb) == 0)
			{
				if ((statb.st_mode & (S_ISUID|S_ISGID)) == 0)
					goto openok;
			}
			close(n);
		}
		if ((euserid = geteuid()) != sh.userid)
		{
			(void) strncpy(name+9, utos((unsigned long)getpid(),
			    10), sizeof (name)-10);
		/* create a suid open file with owner equal effective uid */
			if ((n = creat(name, 04100)) < 0)
				goto fail;
			unlink(name);
			/* make sure that file has right owner */
			if (fstat(n, &statb) < 0 || statb.st_uid != euserid)
				goto fail;
			if (n != 10)
			{
				close(10);
				fcntl(n, F_DUPFD, 10);
				close(n);
			}
		}
		(void) sigprocmask(SIG_SETMASK, &omask, NULL);
		savet = *--t;
		*t = p;
		(void) execve(e_suidexec, t, xecenv);
	fail:
		/*
		 *  The following code is just for compatibility
		 *  It should be replaced with the line sh_fail(p,e_exec);
		 */
		if ((n = open(p, O_RDONLY)) < 0) {
			(void) sigprocmask(SIG_SETMASK, &omask, NULL);
			sh_fail(p, e_open);
		}
		*t++ = savet;
		close(10);

	openok:
		(void) sigprocmask(SIG_SETMASK, &omask, NULL);
		input = n;
	}
#else
	input = io_safe_fopen(p);
#endif /* SUID_EXEC */
	hist_close();
#ifdef ACCT
	preacct(p);  /* reset accounting */
#endif	/* ACCT */
	/* remove locals */
	gscan_some(env_nolocal, sh.var_tree, N_EXPORT, 0); /* local variables */
	gscan_some(env_nolocal, sh.alias_tree, N_EXPORT, 0); /* local aliases */
	gscan_some(env_nolocal, sh.fun_tree, N_EXPORT, 0); /* local functions */
	/* set up new args */
	arg_set(t);
	nam_ontype(L_ARGNOD, N_INDIRECT);
	nam_offtype(SHELLNOD, ~N_RESTRICT);
	nam_offtype(PATHNOD, ~N_RESTRICT);
	sh.lastarg = sh_heap(p);
	/* save name of calling command */
	sh.readscript = st.cmdadr;
	st.cmdadr = sh_heap(t[0]);
	st.fn_depth = st.dot_depth = 0;
	LONGJMP(sh.subshell, 1);
}

/*
 * The following routine is used to execute shell functions and command subs
 * when com!=NULL $* is saved and restored
 */

void
sh_funct(t, com, execflg, envlist)
union anynode *t;
register char *com[];
register int execflg;
struct argnod *envlist;
/*
 *	assume t!=NULL;
 */
{
	/* execute user defined function */
	register char *trap;
	jmp_buf retbuf;
	jmp_buf *savreturn = sh.freturn;
	int savop_char;
	longlong_t	savop_index;
	struct dolnod	*argsav = 0;
	int mode;
	struct dolnod *savargfor;
	struct fileblk *savstandin = st.standin;
	struct sh_scoped savst;
	savst = st;
	savop_index = opt_index;
	savop_char = opt_char;
	opt_char = opt_index = 0;
	st.loopcnt = 0;
	if (com)
	{
		nam_scope(envlist);
		if (execflg & EXECPR)
			on_option(EXECPR);
		else
			off_option(EXECPR);
		execflg &= ~EXECPR;
		/* P.55, section 2.9.5 XPG4 manual: Preserve $0 */
		sig_funset(0);
		argsav = arg_new(com, &savargfor);
	}
	sh.freturn = (jmp_buf *)retbuf;
	mode = SETJMP(retbuf);
	if (mode == 0)
	{
		st.states |= FUNCTION;
		if (st.fn_depth++ > MAXDEPTH)
			LONGJMP(*sh.freturn, 3);
		else
			sh_exec(t, execflg);
	}
	if (--st.fn_depth == 1 && mode == 3)
		sh_fail(com[0], e_recursive);
	sh.freturn = savreturn;
	if (com)
	{
		nam_unscope();
		arg_reset(argsav, savargfor);
		trap = st.trapcom[0];
		st.trapcom[0] = 0;
		sig_funset(1);
	}
	else
	{
		/* remember signals that occur for processing later */
		savst.trapflg[SIGINT] = st.trapflg[SIGINT];
		savst.trapflg[SIGTERM] = st.trapflg[SIGTERM];
		savst.trapflg[SIGHUP] = st.trapflg[SIGHUP];
	}
	io_clear(savstandin);

	/*
	 * Temporary files for here documents might have been created
	 * through the function call. RM_TMP should be preserved, so that
	 * the temp files will be removed at sh_done().
	 */
	if ((st.states & RM_TMP) != 0)
		savst.states |= RM_TMP;

	st = savst;
	opt_index = savop_index;
	opt_char = savop_char;
	if (com)
	{
		if (sh.exitval > SIGFAIL)
			sh_fault(sh.exitval-SIGFAIL);
		if (trap)
		{
			if (sh.posixfunction)
			{
				/* Latest EXIT trap is used */
				if (st.trapcom[0])
					free(st.trapcom[0]);
				st.trapcom[0] = trap;
			}
			else
			{
				int savexit = sh.exitval;
				sh_eval(trap);
				sh.exitval = savexit;
				free(trap);
			}
		}
		/* do a debug trap on return unless in a debug trap handler */
		if (st.trapcom[DEBUGTRAP] && sh.intrap != DEBUGTRAP + 1)
		{
			st.trapflg[DEBUGTRAP] |= TRAPSET;
			sh.trapnote |= TRAPSET;
		}
	}
	if (mode > 2)
	{
		/*
		 * mode == 4 for exit command - see b_ret_exit().
		 * Exit immediately if we are on the base level. If this
		 * function call was made from a function, we can't exit
		 * since we'll have to examine both EXIT/ERR traps defined in
		 * each functions and also the global EXIT trap. Once we
		 * reached the base level and executed global EXIT trap, then
		 * we are ready to go.
		 */
		if (mode == 4 && st.fn_depth == 0)
		{
			st.states &=
			    ~(PROMPT|PROFILE|BUILTIN|FUNCTION|LASTPIPE);
			st.dot_depth = 0;
			sh_exit(sh.exitval);
		}
		LONGJMP(*sh.freturn, mode);
	}
}

#ifdef LSTAT
/*
 * Given an absolute pathname, find physical pathname by resolving links
 * path_phys returns 1, if successful, 0 for recursive link or overflow
 * path must be an array of at least PATH_MAX characters
 * The resulting path is canonicalized
 * Coded by David Korn
 *          ulysses!dgk
 */

/* CSI assumptions1(ascii),5(slash) made here. See csi.h. */
int
path_physical(char *path)
/*
 *	assume path!=NULL;
 */
{
#ifdef apollo
	/*
	 * This code has been added to figure out where we
	 * really are instead performing operations on the
	 * path string. This is much faster the walking through
	 * the path doing readlink(s). Note: code has been added in
	 * SYSCD(builtin.c) to look at the errno value in case getwd() fails.
	 */
	extern char *getwd();
	char realpath[PATH_MAX+1];

	if (!getwd(realpath))
		return (0);
	strcpy(path, realpath);
	return (1);
#else /* apollo */
	register char *cp = path;
	char buffer[PATH_MAX];
	register char *savecp;
	int depth = 0;
	int c;
	int n;
	while (*cp)
	{
		/* skip over '/' */
		savecp = cp+1;
		while (*cp == '/')
			cp++;
		/* eliminate multiple slashes */
		if (cp > savecp)
			cp = strcpy(savecp, cp);
		/* check for .. */
		if (*cp == '.')
		{
			switch (cp[1])
			{
			case 0: case '/':
				/* eliminate /. */
				cp--;
				strcpy(cp, cp+2);
				continue;
			case '.':
				if (cp[2] == '/' || cp[2] == 0)
				{
					/* backup, but not past root */
					savecp = cp+2;
					cp--;
					while (cp > path && *--cp != '/');
					if (cp == path && *cp == '/')
						cp++;
					strcpy(cp, savecp);
					continue;
				}
				break;
			}
		}
		savecp = cp;
		/* go to end of component */
		while (*cp && *cp != '/')
			cp++;
		c = *cp;
		*cp = 0;
		n = readlink(path, buffer, PATH_MAX);
		*cp = c;
		if (n > 0)
		{
			if (++depth > MAXDEPTH)
				return (0);
			strcpy(buffer+n, cp);
			if (*buffer == '/')
				cp = strcpy(path, buffer);
			else
			{
				/* check for path overflow */
				cp = savecp;
				if ((strlen(buffer)+(cp-path)) >= PATH_MAX)
					return (0);
				strcpy(cp, buffer);
			}
		}
	}
	if (cp == path)
		*++cp = 0;
	else while (--cp > path && *cp == '/')
		/* eliminate trailing slashes */
		*cp = 0;
	return (1);
#endif /* apollo */
}
#endif /* LSTAT */

#ifdef ACCT
#include <sys/acct.h>

static int compress();

static struct acct sabuf;
static struct tms buffer;
static clock_t	before;

/*
 *	0 environment variable SHACCT not set so never acct
 *	ptr to SHACCT value if set, so acct if shell procedure
 */
static char *SHACCT;

/*
 *	0 implies do not write record on exit
 *	1 implies write acct record on exit
 */
static shaccton;

/*
 *	initialize accounting, i.e., see if SHACCT variable set
 */
void
initacct()
{

	SHACCT = nam_strval(ACCTNOD);
}
/*
 * suspend accounting unitl turned on by preacct()
 */
void
suspacct()
{
	shaccton = 0;
}

int
preacct(cmdname)
char	*cmdname;
{
	if (SHACCT)
	{
		sabuf.ac_btime = time((time_t *)0);
		before = times(&buffer);
		sabuf.ac_uid = getuid();
		sabuf.ac_gid = getgid();
		strncpy(sabuf.ac_comm, (char *)path_basename(cmdname),
			sizeof (sabuf.ac_comm));
		shaccton = 1;
	}
}

void
doacct()
{
	int	fd;
	clock_t	after;

	if (shaccton) {
		after = times(&buffer);
		sabuf.ac_utime = compress(buffer.tms_utime + buffer.tms_cutime);
		sabuf.ac_stime = compress(buffer.tms_stime + buffer.tms_cstime);
		sabuf.ac_etime = compress((time_t)(after-before));
		fd = open(SHACCT, O_WRONLY | O_APPEND | O_CREAT, RW_ALL);
		write(fd, &sabuf, sizeof (sabuf));
		close(fd);
	}
}

/*
 * Produce a pseudo-floating point representation
 * with 3 bits base-8 exponent, 13 bits fraction.
 */
static int compress(t)
register time_t t;
{
	register int exp = 0, rund = 0;

	while (t >= 8192)
	{
		exp++;
		rund = t&04;
		t >>= 3;
	}
	if (rund)
	{
		t++;
		if (t >= 8192)
		{
			t >>= 3;
			exp++;
		}
	}
	return ((exp<<13) + t);
}
#endif	/* ACCT */
