/*
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *  builtin routines for the shell
 *
 *   David Korn
 *   AT&T Bell Laboratories
 *   Room 3C-526B
 *   Murray Hill, N. J. 07974
 *   Tel. x7975
 *
 */

#include	<limits.h>
#include	<wait.h>
#include	<errno.h>
#include	<string.h>
#include	<libintl.h>
#include	"defs.h"
#include	"history.h"
#include	"builtins.h"
#include	"jobs.h"
#include	"sym.h"

#define	NOT_USED(x)	(&x, 1)	/* prevents not used messages */

#ifdef _sys_resource_

#ifndef included_sys_time_
#include <sys/time.h>
#endif

#include <sys/resource.h>	/* needed for ulimit */

#define	LIM_FSIZE	RLIMIT_FSIZE
#define	LIM_DATA	RLIMIT_DATA
#define	LIM_STACK	RLIMIT_STACK
#define	LIM_CORE	RLIMIT_CORE
#define	LIM_CPU		RLIMIT_CPU
#define	INFINITY	RLIM_INFINITY

#ifdef RLIMIT_RSS
#define	LIM_MAXRSS	RLIMIT_RSS
#endif /* RLIMIT_RSS */

#else

#ifdef VLIMIT
#include	<sys/vlimit.h>
#endif /* VLIMIT */

#endif	/* _sys_resource_ */

#ifdef UNIVERSE
static int att_univ = -1;

#ifdef _sys_universe_
#include <sys/universe.h>

#ifdef sequent
#define	NUMUNIV 2
static char *univ_name[] = { "ucb", "att", 0};
#define	getuniverse(x) (flag = universe(U_GET),\
	(flag > -1 ? strncpy(x, univ_name[flag], TMPSIZ) : 0),\
	flag)
#define	univ_index(n)	(n)
#define	setuniverse(x) universe(x)

#else /* !sequent */

#define	getuniverse(x)	(setuniverse(flag = setuniverse(0)),\
	(--flag >= 0 ? strncpy(x, univ_name[flag], TMPSIZ) : 0),\
	flag)
#define	univ_index(n)	((n)+1)
#endif /* sequent */

#else

#define	univ_number(x)	(x)
#endif /* _sys_universe_ */

#endif /* UNIVERSE */

#ifdef ECHOPRINT
#undef ECHO_RAW
#undef ECHO_N
#endif /* ECHOPRINT */

#define	DOTMAX	32	/* maximum level of . nesting */
#define	FCMAX	20	/* maximum level of fc recursion */
#define	PHYS_MODE	H_FLAG
#define	LOG_MODE	N_LJUST

/* determine order of -L or -P flags as to last one */

static int flg_precedence; /* last -P or -L flag found is used */

/* This module references these external routines */

#ifdef ECHO_RAW
extern char		*echo_mode();
#endif	/* ECHO_RAW */

extern int		gscan_all();
extern char		*utos();
extern char		*ulltos();
extern void		ltou();

static int	b_common();
static int	b_unall();
static int	command_popt(int, char **);
static int	getnum(char *, int *);
static int	flagset();
static int	sig_number();
static int	scanargs();

#ifdef JOBS
static void	sig_list();
#endif	/* JOBS */

static int	argnum;
static int 	aflag;
static int	newflag;
static int	echon;
static int	scoped;

void rehash();
extern const char    *xecmsg;
#define	READ	S_IRUSR | S_IRGRP | S_IROTH
#define	WRITE	S_IWUSR | S_IWGRP | S_IWOTH
#define	EXEC	S_IXUSR | S_IXGRP | S_IXOTH

static void unalias_nam();
static void unalias_all();
static void umask_sprint();
extern int optind;
extern char *optarg;
extern int _sp;

int
b_exec(int argn, char **com)
{
	st.ioset = 0;
	if (*++com) {
		b_login(argn, com);
	}
	return (0);
}

int
b_login(int argn, char **com)
{
	NOT_USED(argn);
	if (is_option(RSHFLG)) {
		sh_cfail(e_restricted);
	} else {
#ifdef JOBS
		if (job_close() < 0) {
			return (1);
		}
#endif /* JOBS */
		/* force bad exec to terminate shell */
		st.states &= ~(PROFILE | PROMPT | BUILTIN | LASTPIPE);
		sig_reset(0);
		hist_close();
		sh_freeup();
		if (st.states & RM_TMP) {
			/* clean up all temp files */
			rm_files(io_tmpname);
		}
		path_exec(com, (struct argnod *)0);
		sh_done(0);
	}
	return (1);
}

/* CSI assumption5(slash) made here. See csi.h. */
int
b_pwd(int argn, char **com)
{
	int flag = 0;
	char *a1 = com[1];
	NOT_USED(argn);
#if defined(LSTAT) || defined(FS_3D)
	while (a1 && *a1 == '-' && (flag = flagset(a1,
	    ~(PHYS_MODE | LOG_MODE)))) {
		com++;
		a1 = com[1];
	}
	if (flg_precedence == LOG_MODE) {
		flag = 0;
	}

#endif /* LSTAT || FS_3D */
	if (*(a1 = path_pwd(0)) != '/') {
		sh_cfail(e_pwd);
	}
#if defined(LSTAT) || defined(FS_3D)
	if (flag) {
		char *path = a1;
		/* reserve PATH_MAX bytes on stack */
		int offset = staktell();
		stakseek(offset + PATH_MAX);
		a1 = stakseek(offset) + offset;
#ifdef FS_3D
		if (umask(flag = umask(0)), flag & 01000) {
			mount(".", a1, 10|(PATH_MAX)<<4);
		} else {
#endif /* FS_3D */
			a1 = strncpy(stakseek(offset), path, PATH_MAX);
#ifdef FS_3D
		}
#endif
#ifdef LSTAT
		path_physical(a1);
		stakseek(offset);
#endif /* LSTAT */
	}
#endif /* LSTAT || FS_3D */
	p_setout(st.standout);
	p_str(a1, NL);
	return (0);
}

#ifndef ECHOPRINT
int
b_echo(int argn, char **com)
{
	int r;
	char *save = *com;
#ifdef ECHO_RAW
	/* This mess is because /bin/echo on BSD is archaic */
#ifdef UNIVERSE
	if (att_univ < 0) {
		int flag;
		char Xuniverse[TMPSIZ];
		if (getuniverse(Xuniverse) >= 0) {
			att_univ = (strcmp(Xuniverse, "att") == 0);
		}
	}
	if (att_univ > 0) {
		*com = (char *)e_minus;
	} else {
#endif /* UNIVERSE */
		*com = echo_mode();
#ifdef UNIVERSE
	}
#endif /* UNIVERSE */
	r = b_print(argn + 1, com - 1);
	*com = save;
	return (r);
#else	/* ECHO_RAW */
#ifdef ECHO_N
	/* same as echo except -n special */
	echon = 1;
	return (b_print(argn, com));
#else	/* ECHO_N */
	/* equivalent to print - */
	*com = (char *)e_minus;
	r = b_print(argn + 1, com - 1);
	*com = save;
	return (r);
#endif /* ECHO_N */
#endif	/* ECHO_RAW */
}
#endif /* ECHOPRINT */

/* CSI assumption1(ascii) made here. See csi.h. */
int
b_print(int argn, char **com)
{
	int fd;
	char *a1 = com[1];
	int flag = 0;
	int r = 0;
	const char *msg = e_file;
	int raw = 0;
	NOT_USED(argn);
	argnum =  -1;

	while (a1 && *a1 == '-') {
		int c = *(a1 + 1);
		com++;
		/* echon set when only -n is legal */
		if (echon) {
			if (strcmp(a1, "-n") == 0) {
				c = 0;
			} else {
				com--;
				break;
			}
		}
		newflag = flagset(a1,
		    ~(N_FLAG | R_FLAG | P_FLAG | U_FLAG | S_FLAG | N_RJUST));
		flag |= newflag;
		/* handle the -R flag for BSD style echo */
		if (flag & N_RJUST) {
			echon = 1;
		}
		if ((flag & U_FLAG) && argnum < 0) {
			/*
			 * Comes here in case of "print -u [n] arg" is
			 * invoked.
			 */
			a1 = com[1];
			while (*a1) {
				if (!isdigit(*a1)) {
					/*
					 * If the argument contains non-digits
					 * then use the default descriptor
					 * to write onto. Also this argument
					 * would be written to the default
					 * output.
					 */
					argnum = 1;
					break;
				}
				a1++;
			}
			if (argnum < 0) {
				/*
				 * if argnum is still -1, then com[1] contains
				 * the file  descriptor  unit number on which
				 * the output should be placed. Also increment
				 * "com" to make com[1] point to the next
				 * argument (See the last line of the outer
				 * while-loop).
				 */
				argnum = atoi(*++com);
			}
		}
		if (c == 0 || newflag == 0) {
			break;
		}
		a1 = com[1];
	}
	echon = 0;
	argnum %= 10;
	if (flag & (R_FLAG | N_RJUST)) {
		raw = 1;
	}
	if (flag & S_FLAG) {
		/* print to history file */
		if (!hist_open()) {
			sh_cfail(e_history);
		}
		fd = hist_ptr->fixfd;
		st.states |= FIXFLG;
		goto skip;
	} else if (flag & P_FLAG) {
		fd = COTPIPE;
		msg = e_query;
	} else if (flag & U_FLAG) {
		fd = argnum;
	} else {
		fd = st.standout;
	}
	if (r = !fiswrite(fd)) {
		if (fd == st.standout) {
			return (r);
		}
		sh_cfail(msg);
	}
	if (fd == input) {
		sh_cfail(msg);
	}
skip:
	p_setout(fd);
	if (echo_list(raw, com + 1) && (flag & N_FLAG) == 0) {
#ifdef WEXP
		if (opt_flags & WEXP_E) {
			p_char(0);
		} else {
			newline();
		}
#else
		newline();
#endif /* WEXP */
	}

	if (flag & S_FLAG) {
		hist_flush();
	}
	return (r);
}

int
b_let(int argn, char **com)
{
	int r;
	if (argn < 2) {
		sh_cfail(e_nargs);
	}
	while (--argn) {
		r = !sh_arith_expr(*++com);
	}
	return (r);
}

/*
 * The following few builtins are provided to set, print,
 * and test attributes and variables for shell variables,
 * aliases, and functions.
 * In addition, typeset -f can be used to test whether a
 * function has been defined or to list all defined functions
 * Note readonly is same as typeset -r.
 * Note export is same as typeset -x.
 */

int
b_readonly(int argn, char **com)
{
	NOT_USED(argn);

	if (com[1] && *com[1] == '-') {
		com += scanargs(com, ~P_FLAG);
	}

	aflag = '-';
	argnum = scoped = 0;
	return (b_common(com, R_FLAG, sh.var_tree));
}

int
b_export(int argn, char **com)
{
	NOT_USED(argn);
	if (com[1] && *com[1] == '-') {
		com += scanargs(com, ~P_FLAG);
	}

	aflag = '-';
	argnum = scoped = 0;
	return (b_common(com, X_FLAG, sh.var_tree));
}


int
b_alias(int argn, char **com)
{
	int ch;
	int flag = 0;
	struct Amemory *troot;
	longlong_t save_index = opt_index;
	int save_char = opt_char;
	NOT_USED(argn);
	argnum = scoped = 0;

	if (com[1]) {
		aflag = *com[1];
	} else {
		aflag = 0;
	}
	opt_char = 0;
	opt_index = 1;
	opt_plus = 1;
	while ((ch = optget(com, ":tx"))) {
		switch (ch) {
		case 't':
			flag |= T_FLAG;
			break;
		case 'x':
			flag |= N_EXPORT;
			break;
		default:
			sh_cfail(e_option);
			break;
		}
	}
	com += opt_index - 1;
	opt_index = save_index;
	opt_char = save_char;

	if (flag & T_FLAG) {
		troot = sh.track_tree;
	} else {
		troot = sh.alias_tree;
	}
	return (b_common(com, flag, troot));
}


int
b_typeset(int argn, char **com)
{
	int flag = 0;
	struct Amemory *troot;
	NOT_USED(argn);
	argnum = scoped = 0;

	if (com[1]) {
		if ((aflag = *com[1]) == '-' || aflag == '+') {
			com += scanargs(com, ~(N_LJUST | N_RJUST | N_ZFILL
			    | N_INTGER | N_LTOU | N_UTOL | X_FLAG | R_FLAG
			    | F_FLAG  | T_FLAG | N_HOST
			    | N_DOUBLE | N_EXPNOTE));
			flag = newflag;
		}
	} else {
		aflag = 0;
	}
	/* G_FLAG forces name to be in newest scope */
	if (st.fn_depth) {
		scoped = G_FLAG;
	}
	if ((flag & N_INTGER) && (flag & (N_LJUST | N_RJUST | N_ZFILL |
	    F_FLAG))) {
		sh_cfail(e_option);
	} else if (flag&F_FLAG) {
		if (flag & ~(N_EXPORT | F_FLAG | T_FLAG | U_FLAG)) {
			sh_cfail(e_option);
		}
		troot = sh.fun_tree;
		flag &= ~F_FLAG;
	} else {
		troot = sh.var_tree;
	}
	return (b_common(com, flag, troot));
}

static
int
b_common(char **com, int flag, struct Amemory *troot)
{
	int fd;
	char *a1;
	int type = 0;
	int r = 0;
	fd = st.standout;
	p_setout(fd);
	if (troot == sh.alias_tree) {
		/* env_namset treats this value specially */
		type = (V_FLAG|G_FLAG);
	}
	if (aflag == 0) {
		if (type) {
			env_scan(fd, 0, troot, 0);
		} else {
			gscan_all(env_prattr, troot);
		}
		return (0);
	}
	if (com[1]) {
		while (a1 = *++com) {
			unsigned newflag;
			struct namnod *np;
			unsigned curflag;
			if (st.subflag && (flag || mbschr(a1, '='))) {
				continue;
			}
			if (troot == sh.fun_tree) {
				/*
				 * functions can be exported or
				 * traced but not set
				 */
				if (flag & U_FLAG) {
					np = env_namset(a1, sh.fun_tree,
					    P_FLAG | V_FLAG);
				} else {
					np = nam_search(a1, sh.fun_tree, 0);
				}
				if (np && is_abuiltin(np)) {
					np = 0;
				}
				if (np && ((flag & U_FLAG) || !isnull(np) ||
				    nam_istype(np, U_FLAG))) {
					if (flag == 0) {
						env_prnamval(np, 0);
						continue;
					}
					if (aflag == '-') {
						nam_ontype(np,
						    flag | N_FUNCTION);
					} else if (aflag == '+') {
						nam_offtype(np, ~flag);
					}
				} else {
					r++;
				}
				continue;
			}
			np = env_namset(a1, troot, (type|scoped));
			/* tracked alias */
			if (troot == sh.track_tree && aflag == '-') {
				nam_ontype(np, NO_ALIAS);
				path_alias(np, path_absolute(np->namid));
				continue;
			}
			if (flag == 0 && aflag != '-' &&
			    mbschr(a1, '=') == NULL) {
				/* type == 0 for TYPESET */
				if (type && (isnull(np) ||
				    !env_prnamval(np, 0))) {
					p_setout(ERRIO);
					p_str(a1, ':');
					p_str((const char *)gettext(e_alias),
					    NL);
					r++;
					p_setout(st.standout);
				}
				continue;
			}
			curflag = namflag(np);
			if (aflag == '-') {
				newflag = curflag;
				if (flag & ~NO_CHANGE) {
					newflag &= NO_CHANGE;
				}
				newflag |= flag;
				if (flag & (N_LJUST|N_RJUST)) {
					if (flag & N_LJUST) {
						newflag &= ~N_RJUST;
					} else {
						newflag &= ~N_LJUST;
					}
				}
				if (flag & N_UTOL) {
					newflag &= ~N_LTOU;
				} else if (flag & N_LTOU) {
					newflag &= ~N_UTOL;
				}
			} else {
				if ((flag & R_FLAG) && (curflag & R_FLAG)) {
					sh_fail(np->namid, e_readonly);
				}
				newflag = curflag & ~flag;
			}
			if (aflag && (argnum > 0 || (curflag != newflag))) {
				if (type) {
					namflag(np) = newflag;
				} else {
					nam_newtype(np, newflag, argnum);
				}
				nam_newtype(np, newflag, argnum);
			}
		}
	} else {
		env_scan(fd, flag, troot, aflag == '+');
	}
	return (r);
}

/*
 * The removing of Shell variable names, aliases, and functions
 * is performed here.
 * Unset functions with unset -f
 * Non-existent items being deleted give non-zero exit status
 */

int
b_unalias(int argn, char **com)
{
	int ch;
	int aflg = 0;
	longlong_t save_index = opt_index;
	int save_char = opt_char;

	NOT_USED(argn);

	if (com[1]) {
		aflag = (*com[1] == '-' ? '-' : 1);
	} else {
		aflag = 0;
	}

	opt_char = 0;
	opt_index = 1;
	opt_plus = 0;
	while ((ch = optget(com, ":a"))) {
		switch (ch) {
		case 'a':
			aflg++;
			break;
		default:
			sh_cfail(e_option);
			break;
		}
	}

	com += opt_index -1;
	argn -= opt_index -1;
	opt_index = save_index;
	opt_char = save_char;

	if (aflg) {
		unalias_all(sh.alias_tree);
		return (0);
	} else {
		return (b_unall(argn, com, sh.alias_tree));
	}
}

int
b_unset(int argn, char **com)
{
	int ch;
	struct Amemory *troot;
	longlong_t save_index = opt_index;
	int save_char = opt_char;

	troot = sh.var_tree;
	if (com[1]) {
		aflag = (*com[1] == '-' ? '-' : 1);
	} else {
		aflag = 0;
	}

	opt_char = 0;
	opt_index = 1;
	opt_plus = 0;
	while ((ch = optget(com, ":fv"))) {
		switch (ch) {
		case 'f':
			troot = sh.fun_tree;
			break;
		case 'v':
			troot = sh.var_tree;
			break;
		default:
			sh_cfail(e_option);
			break;
		}
	}

	com += opt_index - 1;
	argn -= opt_index - 1;
	opt_index = save_index;
	opt_char = save_char;
	return (b_unall(argn, com, troot));
}

static
int
b_unall(int argn, char **com, struct Amemory *troot)
{
	char *a1;
	struct namnod *np;
	struct slnod *slp;
	int r = 0;
	if (st.subflag) {
		return (0);
	}
	if (argn < 2) {
		sh_cfail(e_nargs);
	}
	while (a1 = *++com) {
		np = env_namset(a1, troot, P_FLAG);
		if (np && !isnull(np)) {
			if (troot == sh.var_tree) {
				if (nam_istype(np, N_RDONLY)) {
					sh_fail(np->namid, e_readonly);
				} else if (nam_istype(np, N_RESTRICT)) {
					sh_fail(np->namid, e_restricted);
				}
#ifdef apollo
				{
					short namlen;
					namlen = strlen(np->namid);
					ev_$delete_var(np->namid, &namlen);
				}
#endif /* apollo */
			} else if (is_abuiltin(np)) {
				r = 1;
				continue;
			} else if (slp = (struct slnod *)(np->value.namenv)) {
				/* free function definition */
				stakdelete(slp->slptr);
				np->value.namenv = 0;
			}
			nam_free(np);
		} else {
			r = 1;
		}
	}
	return (r);
}

int
b_dot(int argn, char **com)
{
	char *a1 = com[1];
	st.states &= ~MONITOR;
	if (a1) {
		int flag;
		jmp_buf retbuf;
		jmp_buf *savreturn = sh.freturn;
#ifdef notdef
		/* check for function first */
		struct namnod *np;
		np = nam_search(a1, sh.fun_tree, 0);
		if (np && !np->value.namval.ip) {
			if (!nam_istype(np, N_FUNCTION)) {
				np = 0;
			} else {
				path_search(a1, 0);
				if (np->value.namval.ip == 0) {
					sh_fail(a1, e_found);
				}
			}
		}
		if (!np) {
#endif
			if ((sh.un.fd = path_open(a1, path_get(a1))) < 0) {
				sh_fail(a1, e_found);
			} else {
				if (st.dot_depth++ > DOTMAX) {
					sh_cfail(e_recursive);
				}
				if (argn > 2) {
					arg_set(com + 1);
				}
				st.states |= BUILTIN;
				sh.freturn = (jmp_buf *)retbuf;
				flag = SETJMP(retbuf);
				if (flag == 0) {
#ifdef notdef
					if (np) {
						sh_exec((union anynode *)
						    (funtree(np)),
						    (int)(st.states &
						    (ERRFLG | MONITOR)));
					} else {
#endif
						sh_eval((char *)0);
#ifdef notdef
					}
#endif
				}
				st.states &= ~BUILTIN;
				sh.freturn = savreturn;
				st.dot_depth--;
				if (sh.exitval > SIGFAIL) {
					sh_fault(sh.exitval - SIGFAIL);
				}
				if (flag && flag != 2) {
					/* exit command */
					if (flag == 4) {
						sh_exit(sh.exitval);
					} else {
						LONGJMP(*sh.freturn, flag);
					}
				}
			}
#ifdef notdef
		}
#endif
	} else {
		sh_cfail(e_argexp);
	}
	return (sh.exitval);
}

int
b_times()
{
	struct tms tt;
	times(&tt);
	p_setout(st.standout);
	p_time(tt.tms_utime, ' ');
	p_time(tt.tms_stime, NL);
	p_time(tt.tms_cutime, ' ');
	p_time(tt.tms_cstime, NL);
	return (0);
}


/*
 * return and exit
 */

int
b_ret_exit(int argn, char **com)
{
	int flag;
	int isexit = (**com == 'e');
	NOT_USED(argn);
	if (st.subflag) {
		return (0);
	}
	flag = ((com[1] ? atoi(com[1]) : sh.oldexit) & EXITMASK);
	if (st.fn_depth > 0 || (!isexit &&
	    (st.dot_depth > 0 || (st.states & PROFILE)))) {
		sh.exitval = flag;
		LONGJMP(*sh.freturn, isexit ? 4 : 2);
	}
	/* force exit */
	st.states &= ~(PROMPT | PROFILE | BUILTIN | FUNCTION | LASTPIPE);
	st.dot_depth = 0;
	sh_exit(flag);
	return (1);
}

/*
 * null command
 */
int
b_null(int argn, char **com)
{
	NOT_USED(argn);
	return (**com == 'f');
}

int
b_continue(int argn, char **com)
{
	NOT_USED(argn);
	if (!st.subflag && st.loopcnt) {
		st.execbrk = st.breakcnt = 1;
		if (com[1]) {

			if (getnum(com[1], &st.breakcnt) == 0) {
				if (st.breakcnt < 1) {
					return (1);
				}
			} else {
				/* Serious syntax error ? */
				sh_cfail(e_number);
			}
		}
		if (st.breakcnt > st.loopcnt) {
			st.breakcnt = st.loopcnt;
		} else {
			st.breakcnt = -st.breakcnt;
		}
	}
	return (0);
}

int
b_break(int argn, char **com)
{
	NOT_USED(argn);
	if (!st.subflag && st.loopcnt) {
		st.execbrk = st.breakcnt = 1;
		if (com[1]) {
			if (getnum(com[1], &st.breakcnt) == 0) {
				if (st.breakcnt < 1) {
					return (1);
				}
			} else {
				/* Serious syntax error ? */
					sh_cfail(e_number);
			}
		}
		if (st.breakcnt > st.loopcnt) {
			st.breakcnt = st.loopcnt;
		}
	}
	return (0);
}

/* CSI assumption1(ascii) made here. See csi.h. */
int
b_trap(int argn, char **com)
{
	char *a1;
	int sig;
	int r = 0;

	NOT_USED(argn);

	a1 = com[1];
	while (a1 != NULL && strcmp(a1, "--") == 0) {
		com++;
		a1 = com[1];
	}

	if (a1) {
		int	clear;
		char *action = a1;

		if (st.subflag) {
			return (0);
		}
		/* first argument all digits or - means clear */
		while (sh_iswdigit(mb_peekc(a1))) {
			mb_nextc((const char **)&a1);
		}
		clear = (a1 != action && *a1 == 0);
		if (!clear) {
			++com;
			if (*action == '-' && action[1] == 0) {
				clear++;
			}
		}

		while (a1 = *++com) {
			sig = sig_number(a1);
			if (sig >= MAXTRAP || sig < MINTRAP) {
				/* XPG4: shell does not abort */
				r = ERROR;
			} else if (clear) {
				sig_clear(sig);
			} else {
				if (a1 = st.trapcom[sig]) {
					free(a1);
				}
				st.trapcom[sig] = sh_heap(action);
				if (*action) {
					sig_ontrap(sig);
				} else {
					sig_ignore(sig);
				}
			}
		}
	} else { /* print out current traps */
#ifdef POSIX
		sig_list(-1);
#else
		p_setout(st.standout);
		for (sig = 0; sig < MAXTRAP; sig++) {
			if (st.trapcom[sig]) {
				p_num(sig, ':');
				p_str(st.trapcom[sig], NL);
			}
		}
#endif /* POSIX */
	}

	return (r);
}

/* CSI assumption1(ascii) made here. See csi.h. */
int
b_chdir(int argn, char **com)
{
	char *a1 = com[1];
	const char *dp;
	char *cdpath = NULLSTR;
	int flag = 0;
	int rval = -1;
	char *oldpwd;
	/* if cdpath does not contain null directory add */
	char *c_colon = {":"};
	/* flag for no null directory existed in cdpath - try again */
	int nonulldir = 0;


	if (st.subflag) {
		return (0);
	}
	if (is_option(RSHFLG)) {
		sh_cfail(e_restricted);
	}
#ifdef LSTAT
#ifdef apollo
	/*
	 * support for the apollo "set -o physical" feature.
	 */
	flag = is_option(PHYSICAL);
#endif /* apollo */
	while (a1 && *a1 == '-' && a1[1]) {
		flag = flagset(a1, ~(PHYS_MODE | LOG_MODE));
		com++;
		argn--;
		a1 =  com[1];
	}
	if (flg_precedence == LOG_MODE) {
		flag = 0;
	}
#endif /* LSTAT */
	if (argn > 3) {
		sh_cfail(e_nargs);
	}
	oldpwd = sh.pwd;
	if (argn == 3) {
		a1 = sh_substitute((oldpwd ? oldpwd : a1), a1, com[2]);
	} else if (a1 == 0 || *a1 == 0) {
		a1 = nam_strval(HOME);
	} else if (*a1 == '-' && *(a1 + 1) == 0) {
		a1 = nam_strval(OLDPWDNOD);
	}
	if (a1 == 0 || *a1 == 0) {
		sh_cfail(argn == 3 ? e_subst : e_direct);
	}
	if (*a1 != '/') {
		cdpath = nam_fstrval(CDPNOD);
	}
	if (cdpath == 0) {
		cdpath = NULLSTR;
	}
	if (*a1 == '.') {
		/* test for pathname . ./ .. or ../ */
		if (*(dp = a1 + 1) == '.') {
			dp++;
		}
		if (*dp == 0 || *dp == '/') {
			cdpath = NULLSTR;
		}
	}
	do {
		dp = cdpath;
		cdpath = path_join((char *)dp, a1);
		if (*stakptr(OPATH) != '/') {
			char *last = (char *)stakfreeze(1);
			if (!oldpwd) {
				oldpwd = path_pwd(0);
			}
			stakseek(OPATH);
			stakputs(oldpwd);
			stakputascii('/');
			stakputs(last + OPATH);
			stakputascii(0);
		}
#ifdef LSTAT
		if (!flag)
#endif /* LSTAT */
		{
			char *cp;
#ifdef FS_3D
			if (!(cp = pathcanon(stakptr(OPATH)))) {
				continue;
			}
			/* eliminate trailing '/' */
			while (*--cp == '/' && cp > stakptr(OPATH)) {
				*cp = 0;
			}
#else
			if (*(cp = stakptr(OPATH)) == '/') {
				if (!pathcanon(cp)) {
					continue;
				}
			}
#endif /* FS_3D */
		}
		rval = chdir(path_relative(stakptr(OPATH)));
		if (rval < 0 && cdpath == 0 && nonulldir == 0) {
			cdpath = c_colon;
			nonulldir = 1;
		}
	}
	while (rval < 0 && cdpath)
		;
	/* use absolute chdir() if relative chdir() fails */
	if (rval < 0 && *a1 == '/' && *(path_relative(stakptr(OPATH))) != '/') {
		rval = chdir(a1);
	}
#ifdef apollo
	/*
	 * The label is used to display the error message if path_physical()
	 * routine fails.(See below)
	 */
unavoidable_goto:
#endif /* apollo */
	if (rval < 0) {
		switch (errno) {
#ifdef ENAMETOOLONG
		case ENAMETOOLONG:
			dp = e_longname;
			break;
#endif /* ENAMETOOLONG */
#ifdef EMULTIHOP
		case EMULTIHOP:
			dp = e_multihop;
			break;
#endif /* EMULTIHOP */
		case ENOTDIR:
			dp = e_notdir;
			break;

		case ENOENT:
			dp = e_found;
			break;

		case EACCES:
			dp = e_access;
			break;
		case ELOOP:
			dp = e_loop;
			break;
#ifdef ENOLINK
		case ENOLINK:
			dp = e_link;
			break;
#endif /* ENOLINK */
		default:
			dp = e_direct;
			break;
		}
		sh_fail(a1, dp);
	}
	if (a1 == nam_strval(OLDPWDNOD) || argn == 3) {
		dp = a1;	/* print out directory for cd - */
	}
#ifdef LSTAT
	if (flag) {
		/* make sure at least PATH_MAX bytes on stack */
		if ((flag = staktell()) < OPATH + PATH_MAX) {
			stakseek(OPATH + PATH_MAX);
		}
		a1 = stakseek(OPATH) + OPATH;
#ifdef apollo
		/*
		 * check the return status of path_physical().
		 * if the return status is 0 then the getwd() has
		 * failed, so print an error message.
		 */
		if (!path_physical(a1)) {
			rval = -1;
			goto unavoidable_goto;
		}
#else
		path_physical(a1);
#endif /* apollo */
		stakseek(OPATH + strlen(a1));
		a1 = (char *)stakfreeze(1) + OPATH;
	} else {
#endif /* LSTAT */
		a1 = (char *)stakfreeze(1) + OPATH;
#ifdef LSTAT
	}
#endif
	/*
	 * ksh was used to suppress the output if
	 * it was not run interactively. However,
	 * XPG4 standard requires to see the output
	 * all the times.
	 */
	if (*dp && *dp != ':' && mbschr(a1, '/')) {
		p_setout(st.standout);
		p_str(a1, NL);
	}
	if (*a1 != '/') {
		return (0);
	}
	nam_fputval(OLDPWDNOD, oldpwd);
	if (oldpwd) {
		free(oldpwd);
	}
	nam_free(PWDNOD);
	nam_fputval(PWDNOD, a1);
	nam_ontype(PWDNOD, N_FREE | N_EXPORT);
	sh.pwd = PWDNOD->value.namval.cp;
	return (0);
}

int
b_shift(int argn, char **com)
{
	int flag = (com[1] ? (int)sh_arith(com[1]) : 1);
	NOT_USED(argn);

	if (flag < 0 || st.dolc < flag) {
		sh_cfail(e_number);
	} else {
		if (st.subflag) {
			return (0);
		}
		st.dolv += flag;
		st.dolc -= flag;
	}
	return (0);
}

int
b_wait(int argn, char **com)
{
	NOT_USED(argn);
	st.states &= ~MONITOR;
	if (!st.subflag) {
		job_bwait(com + 1);
	}
	return (sh.exitval);
}

int
b_read(int argn, char **com)
{
	char	*a1;
	int	r, fd, flag;
	int	c, unum;
	longlong_t save_index = opt_index;
	int	save_char = opt_char;

	flag = 0;
	opt_char = 0;
	opt_index = 1;
	opt_plus = 0;
	while ((c = optget(com, "prsu:")) != 0) {
		switch (c) {
		case 'p':
			flag |= P_FLAG;
			break;
		case 'r':
			flag |= R_FLAG;
			break;
		case 's':
			flag |= S_FLAG;
			break;
		case 'u':
			flag |= U_FLAG;
			unum = atoi(opt_arg);
			break;
		default:
			sh_cfail(e_option);
		}
	}
	com += opt_index;
	argn -= opt_index;
	opt_index = save_index;
	opt_char = save_char;

	if (flag & P_FLAG) {
		if ((fd = sh.cpipe[INPIPE]) <= 0) {
			sh_cfail(e_query);
		}
	} else if (flag & U_FLAG) {
		fd = unum;
	} else {
		fd = 0;
	}
	if (fd && !fisread(fd)) {
		sh_cfail(e_file);
	}

	a1 = com[0];
	/* look for prompt */
	if (a1 && (a1 = mbschr(a1, '?')) && tty_check(fd)) {
		p_setout(ERRIO);
		p_str(a1 + 1, 0);
	}
	env_readline(&com[0], fd, flag & (R_FLAG | S_FLAG));
	if (r = (fiseof(io_get_ftbl(fd)) != 0)) {
		if (flag & P_FLAG) {
			io_pclose(sh.cpipe);
			return (1);
		}
	}
	io_clearerr(io_get_ftbl(fd));
	return (r);
}

int
b_set(int argn, char **com)
{
	if (com[1]) {
		arg_opts(argn, com, 1);
		st.states &= ~(READPR | MONITOR);
		st.states |= is_option(READPR | MONITOR);
	} else {
		/* scan name chain and print */
		env_scan(st.standout, 0, sh.var_tree, 0);
	}
	return (sh.exitval);
}

int
b_eval(int argn, char **com)
{
	NOT_USED(argn);
	st.states &= ~MONITOR;
	if (com[1]) {
		sh.un.com = com + 2;
		sh_eval(com[1]);
	}
	return (sh.exitval);
}

/* CSI assumption1(ascii) made here. See csi.h. */
int
b_fc(int argn, char **com)
{
	char *a1;
	int flag;
	struct history *fp;
#ifdef notdef
	struct stat statb;
	time_t	before = 0;
#endif
	int fdo;
	char *argv[2];
	char fname[TMPSIZ];
	int index2;
	int indx = -1; /* used as subscript for range */
	char *edit = NULL;		/* name of editor */
	char *replace = NULL;		/* replace old=new */
	int incr;
	int range[2];	/* upper and lower range of commands */
	int lflag = 0;
	int nflag = 0;
	int rflag = 0;
	int sflag = 0;
	histloc location;
	NOT_USED(argn);

	/* Check for excessive recursion */
	if (st.exec_flag < -FCMAX) {
		sh_fail(sh.cmdname, e_ulimit);
	}
	if (!hist_open()) {
		sh_cfail(e_history);
	}
	fp = hist_ptr;
	while ((a1 = com[1]) && *a1 == '-') {
		argnum = -1;
		flag = flagset(a1, ~(S_FLAG | E_FLAG | L_FLAG | N_FLAG |
		    R_FLAG));
		if (flag == 0) {
			if (argnum < 0) {
				com++;
				break;
			}
			flag = fp->fixind - argnum - 1;
			if (flag <= 0) {
				flag = 1;
			}
			range[++indx] = flag;
			argnum = 0;
			if (indx == 1) {
				break;
			}
		} else {
			if (flag & E_FLAG) {
				/* name of editor specified */
				com++;
				if ((edit = com[1]) == NULL) {
					sh_cfail(e_argexp);
				}
			}
			if (flag & N_FLAG) {
				nflag++;
			}
			if (flag & L_FLAG) {
				lflag++;
			}
			if (flag & R_FLAG) {
				rflag++;
			}
			if (flag & S_FLAG) {
				sflag++;
			}
		}
		com++;
	}
	/* XPG4: Do some error checkings for input options */
	if (sflag && edit != NULL) {
		sh_cfail(e_option);
	}

	/* XPG4: Set edit to skip -e handler */
	if (sflag) {
		edit = "-";
	}

	flag = indx;
	while (flag < 1 && (a1 = com[1])) {
		/* look for old=new argument */
		if (replace == NULL && mbschr(a1 + 1, '=')) {
			replace = a1;
			com++;
			continue;
		} else if (isdigit(*a1) || *a1 == '-') {
			/* see if completely numeric */
			do	a1++;
			while (isdigit(*a1))
				;
			if (*a1 == 0) {
				a1 = com[1];
				range[++flag] = atoi(a1);
				if (*a1 == '-') {
					range[flag] += (fp->fixind - 1);
				} else if (range[flag] >= fp->fixind - 1) {
					sh_fail(com[1], e_found);
				}
				com++;
				continue;
			}
		}
		/* search for last line starting with string */
		location = hist_find(com[1], fp->fixind - 1, 0, -1);
		if ((range[++flag] = location.his_command) < 0) {
			sh_fail(com[1], e_found);
		}
		com++;
	}
	if (flag < 0) {
		/* set default starting range */
		if (lflag) {
			flag = fp->fixind - 16;
			if (flag < 1) {
				flag = 1;
			}
		} else {
			flag = fp->fixind - 2;
		}
		range[0] = flag;
		flag = 0;
	}
	if (flag == 0) {
		/* set default termination range */
		range[1] = (lflag ? fp->fixind - 1 : range[0]);
	}
	if ((index2 = fp->fixind - fp->fixmax) <= 0) {
		index2 = 1;
	}
	/*
	 * There is no invalid range for
	 * XPG4.
	 * So, check for valid ranges
	 */
	for (flag = 0; flag < 2; flag++) {
		if (range[flag] < index2) {
			range[flag] = index2;
		} else if (range[flag] >= (fp->fixind - (lflag == 0))) {
			range[flag] = fp->fixind - 1;
		}
	}

	if (edit && *edit == '-' && range[0] != range[1]) {
		sh_cfail(e_number);
	}
	/* now list commands from range[rflag] to range[1-rflag] */
	incr = 1;
	flag = rflag > 0;
	if (range[1 - flag] < range[flag]) {
		incr = -1;
	}
	if (lflag) {
		fdo = st.standout;
		a1 = "\n\t";
	} else {
		fdo = io_mktmp(fname);
		a1 = "\n";
		nflag++;
	}
	p_setout(fdo);
	while (1) {
		if (nflag == 0) {
			p_num(range[flag], '\t');
		} else if (lflag) {
			p_char('\t');
		}
		hist_list(hist_position(range[flag]), 0, a1);
		if (lflag && (sh.trapnote & SIGSET)) {
			sh_exit(SIGFAIL);
		}
		if (range[flag] == range[1 - flag]) {
			break;
		}
		range[flag] += incr;
	}
	if (lflag) {
		return (0);
	}
#ifdef notdef
	if (fstat(fdo, &statb) >= 0) {
		before = statb.st_mtime;
	}
#endif

	io_fclose(fdo);
	hist_eof();
	p_setout(ERRIO);
	a1 = edit;
	if (a1 == NULL && (a1 = nam_strval(FCEDNOD)) == NULL) {
		a1 = (char *)e_defedit;
	}
#ifdef apollo
	/*
	 * Code to support the FC using the pad editor.
	 * Exampled of how to use: FCEDIT=pad
	 */
	if (strcmp(a1, "pad") == 0) {
		int pad_create();
		io_fclose(fdo);
		fdo = pad_create(fname);
		pad_wait(fdo);
		unlink(fname);
		strcat(fname, ".bak");
		unlink(fname);
		io_seek(fdo, (off_t)0, SEEK_SET);
	} else {
#endif /* apollo */
		if (*a1 != '-') {
			sh.un.com = argv;
			argv[0] =  fname;
			argv[1] = NULL;
			sh_eval(a1);
		}
		fdo = io_safe_fopen(fname);
#ifdef notdef
		/*
		 * Korn says this is the right thing to do, but our customers,
		 * the ksh man page, the ksh book, and the POSIX shell spec
		 * all disagree with Korn.  None of the docs make any mention
		 * of checking for modification of the file.  And besides, the
		 * mod check fails if you modify the file *very* quickly.
		 */
		/* if the file hasn't changed, treat this as a error */
		if (*a1 != '-' && fstat(fdo, &statb) >= 0 &&
		    before == statb.st_mtime) {
			sh.exitval = 1;
		}
#endif	/* notdef */
		unlink(fname);
#ifdef apollo
	}
#endif /* apollo */
	/* don't history fc itself unless forked */
	if (!(st.states & FORKED)) {
		hist_cancel();
	}
	st.states |= (READPR|FIXFLG);	/* echo lines as read */
	st.exec_flag--;  /* needed for command numbering */
	if (replace != NULL) {
		hist_subst(sh.cmdname, fdo, replace);
	} else if (sh.exitval == 0) {
		/* read in and run the command */
		st.states &= ~BUILTIN;
		sh.un.fd = fdo;
		sh_eval((char *)0);
	} else {
		io_fclose(fdo);
		if (!is_option(READPR)) {
			st.states &= ~(READPR|FIXFLG);
		}
	}
	st.exec_flag++;
	return (sh.exitval);
}

/*
 * Implementation of getopts builtin for ksh. The function optget()
 * in ../shlib/optget.c does the parsing work, and this function does
 * all output if errors occur.
 *
 * CSI assumption1(ascii) made here. See csi.h.
 */
int
b_getopts(int argn, char **com)
{
	char *a1 = com[1];
	int flag;
	struct namnod *name_node;
	int return_val = 0;
	extern char opt_option[];	/* contains whole option, long or */
					/* short, including dashes, but no */
					/* argument */
	extern char *opt_arg;
	static char value[2];
	const char *message = e_argexp;
	char *opt_name = NULL;

	if (argn < 3) {
		sh_cfail(e_argexp);
	}
	name_node = env_namset(com[2], sh.var_tree, P_FLAG);
	if (argn > 3) {
		com += 2;
		argn -= 2;
	} else {
		com = st.dolv;
		argn = st.dolc;
	}
	opt_plus = 1;	/* allow + to be used for options */
	flag = (opt_index <= argn ? optget(com, a1) : 0);
	switch (flag) {
	case '?':			/* unrecognized option */
		message = e_option;
		/* fall through */
	case ':':			/* required arg. missing */
		/* option requires an argument that was not found */

		/* get the option name, without leading - or + */
		opt_name = opt_option+1;
		if (((*opt_name) == '-') ||
		    (opt_plus && ((*opt_name) == '+'))) {
			++opt_name;	/* long option (++ or --) */
		}

		if (*a1 == ':') {
			/*
			 * client specified no output from us
			 * Return bad argument in OPTARG
			 */
			opt_arg = opt_name;
		} else {
			/* output an error message (arg. required) */
			p_setout(ERRIO);
			p_prp(sh.cmdname);
			p_str(e_colon, 0);
			p_str(opt_name, 0);
			p_char(' ');
			p_str(message, NL);
			flag = '?';
		}
		*(a1 = value) = flag;
		break;

	case 0:
		a1 = "?";  /* XPG4: End of option char */
		return_val = ERROR;
		opt_char = 0;
		break;

	default:
		a1 = opt_option + (*opt_option == '-');
	} /* switch flag */
	nam_putval(name_node, a1);
	name_node = nam_search(OPTARG->namid, sh.var_tree, N_ADD|G_FLAG);
	nam_fputval(name_node, opt_arg);
	return (return_val);
} /* b_getopts() */

int
b_whence(int argn, char **com)
{
	NOT_USED(argn);
	com += scanargs(com, ~(V_FLAG|P_FLAG));
	if (com[1] == 0) {
		sh_cfail(e_nargs);
	}
	p_setout(st.standout);
	return (sh_whence(com, newflag));
}


int
b_umask(int argn, char **com)
{
	int ch;
	int sflag = 0;
	longlong_t save_index = opt_index;
	int save_char = opt_char;
	char *a1;
	int flag = 0;

	NOT_USED(argn);
	opt_char = 0;
	opt_index = 1;
	opt_plus = 0;
	while ((ch = optget(com, ":S"))) {
		switch (ch) {
		case 'S':
			sflag++;
			argn--;
			break;
		default:
			sh_cfail(e_option);
			break;
		}
	}

	com += opt_index;
	opt_index = save_index;
	opt_char = save_char;
	NOT_USED(argn);

	if (a1 = com[0]) {
		int i;
		wchar_t c;

		if (st.subflag) {
			return (0);
		}
		if (sh_iswdigit(mb_peekc(a1))) {
			while (c = mb_nextc((const char **) & a1)) {
				if (c >= '0' && c <= '7') {
					flag = (flag << 3) + (c - '0');
				} else {
					sh_cfail(e_number);
				}
			}
		} else {
			char **cp = com + 1;
			flag = umask(0);
			i = strperm(a1, cp, ~flag);
			if (**cp) {
				umask(flag);
				sh_cfail(e_format);
			}
			flag = (~i&0777);
		}
		umask(flag);
	} else {

		flag = umask(0);		/* Original Mask */
		(void) umask(flag);		/* Restore it */
		p_setout(st.standout);

		if (sflag) {
			umask_sprint(flag);
		} else {
			a1 = utos((ulong_t)flag, 8);
			*++a1 = '0';
			p_str(a1, NL);
		}

	}
	return (0);
} /* b_umask() */

#ifdef LIM_CPU
#define	HARD	1
#define	SOFT	2
/* BSD style ulimit */
int
b_ulimit(int argn, char **com)
{
	char *a1;
	int flag = 0;
#ifdef RLIMIT_CPU
	struct rlimit rlp;
#endif /* RLIMIT_CPU */
	const struct sysnod *sp;
	rlim_t i, tmp;
	int label;
	int n;
	int mode = 0;
	int unit;
	int noargs;
	longlong_t save_index = opt_index;
	int save_char = opt_char;
	NOT_USED(argn);
	opt_char = 0;
	opt_index = 1;
	opt_plus = 0;

	while ((n = optget(com, ":HSacdfmnstv"))) {
		switch (n) {
		case 'H':
			mode |= HARD;
			continue;
		case 'S':
			mode |= SOFT;
			continue;
		case 'a':
			flag = (0x2f
#ifdef LIM_MAXRSS
			    |(1 << 4)
#endif /* LIM_MAXRSS */
#ifdef RLIMIT_NOFILE
			    |(1 << 6)
#endif /* RLIMIT_NOFILE */
#ifdef RLIMIT_VMEM
			    |(1 << 7)
#endif /* RLIMIT_VMEM */
			    );	/* indentation modified to pass cstyle -Pp */
			break;
		case 't':
			flag |= 1;
			break;
#ifdef LIM_MAXRSS
		case 'm':
			flag |= (1 << 4);
			break;
#endif /* LIM_MAXRSS */
		case 'd':
			flag |= (1 << 2);
			break;
		case 's':
			flag |= (1 << 3);
			break;
		case 'f':
			flag |= (1 << 1);
			break;
		case 'c':
			flag |= (1 << 5);
			break;
#ifdef RLIMIT_NOFILE
		case 'n':
			flag |= (1 << 6);
			break;
#endif /* RLIMIT_NOFILE */
#ifdef RLIMIT_VMEM
		case 'v':
			flag |= (1 << 7);
			break;
#endif /* RLIMIT_VMEM */
		default:
			sh_cfail(e_option);
		}
	}
	com += opt_index;
	a1 = *com;
	opt_index = save_index;
	opt_char = save_char;
	/* default to -f */
	if (noargs = (flag == 0)) {
		flag |= (1 << 1);
	}
	/* only one option at a time for setting */
	label = (flag & (flag - 1));
	if (a1) {
		if (label) {
			sh_cfail(e_option);
		}
		if (com[1]) {
			sh_cfail(e_nargs);
		}
	}
	sp = limit_names;
	if (mode == 0) {
		mode = (HARD|SOFT);
	}
	for (; flag; sp++, flag >>= 1) {
		if (!(flag & 1)) {
			continue;
		}
		n = sp->sysval >> 11;
		unit = sp->sysval & 0x7ff;
		if (a1) {
			if (st.subflag) {
				return (0);
			}
			if (strcmp(a1, e_unlimited) == 0) {
				i = INFINITY;
			} else {
				if ((longlong_t)(i=
				    (u_longlong_t)sh_arith(a1)) < 0) {
					sh_cfail(e_number);
				}
				/*
				 * try to catch overflow values b/c
				 * sh_arith won't
				 */
				tmp = i * unit;
				if (tmp < i) {
					sh_cfail(e_number);
				}
				i = tmp;
			}
#ifdef RLIMIT_CPU
			if (getrlimit(n, &rlp) < 0) {
				sh_cfail(e_number);
			}
			if (mode & HARD) {
				rlp.rlim_max = i;
			}
			if (mode & SOFT) {
				rlp.rlim_cur = i;
			}
			if (setrlimit(n, &rlp) < 0) {
				sh_cfail(e_ulimit);
			}
#endif /* RLIMIT_CPU */
		} else {
#ifdef  RLIMIT_CPU
			if (getrlimit(n, &rlp) < 0) {
				sh_cfail(e_number);
			}
			if (mode & HARD) {
				i = rlp.rlim_max;
			}
			if (mode & SOFT) {
				i = rlp.rlim_cur;
			}
#else
			i = -1;
		}
		if ((i = vlimit(n, i)) < 0) {
			sh_cfail(e_number);
		}
		if (a1 == 0) {
#endif /* RLIMIT_CPU */
			p_setout(st.standout);
			if (label) {
				p_str((const char *)gettext(sp->sysnam), SP);
			}
			/* ulimit without args gives "unlimited" */
			if (i != INFINITY) {
				i /= unit;
				p_str(ulltos((u_longlong_t)i, 10), NL);
			} else {
				p_str((const char *)gettext(e_unlimited), NL);
			}
		}
	}
	return (0);
}
#else
int
b_ulimit(int argn, char **com)
{
	char *a1 = com[1];
	int flag = 0;
#ifndef VENIX
	rlim_t i;
	long ulimit();
	int mode = 2;
	NOT_USED(argn);
	if (a1 && *a1 == '-') {
#ifdef RT
		flag = flagset(a1, ~(F_FLAG|P_FLAG));
#else
		flag = flagset(a1, ~F_FLAG);
#endif /* RT */
		a1 = com[2];
	}
	if (flag & P_FLAG) {
		mode = 5;
	}
	if (a1) {
		if (st.subflag) {
			return (0);
		}
		if ((i = sh_arith(a1)) < 0) {
			sh_cfail(e_number);
		}
		if (flag & LOG_MODE) {
			flag = 0;
		}
	} else {
		mode--;
		i = -1;
	}
	if ((i = ulimit(mode, i)) < 0) {
		sh_cfail(e_number);
	}
	if (a1 == 0) {
		p_setout(st.standout);
		p_str(ulltos(i, 10), NL);
	}
#endif /* VENIX */
	return (0);
}
#endif /* LIM_CPU */

#ifdef JOBS
#ifdef SIGTSTP
/* CSI assumption1(ascii) made here. See csi.h. */
int
b_bgfg(int argn, char **com)
{
	int flag = (**com == 'b');
	char *a1 = com[1];

	NOT_USED(argn);

	if (a1 && (*a1++ == '-')) {
		if (*a1 == '-') {
			com++;
		} else {
			sh_cfail(e_option);
		}
	}

	if (!(st.states&MONITOR) && job.jobcontrol) {
		sh_cfail(e_no_jctl);
	}
	if (job_walk(job_switch, flag, com + 1)) {
		sh_cfail(e_no_job);
	}
	return (sh.exitval);
}
#endif /* SIGTSTP */

int
b_jobs(int argn, char **com)
{
	NOT_USED(argn);
	com += scanargs(com, ~(N_FLAG | L_FLAG | P_FLAG));
	if (*++com == 0) {
		com = 0;
	}
	p_setout(st.standout);
	if (job_walk(job_list, newflag, com)) {
		sh_cfail(e_no_job);
	}
	return (sh.exitval);
}

/* CSI assumption1(ascii) made here. See csi.h. */
int
b_kill(int argn, char **com)
{
	int flag;
	int sflg = 0;
	int rslt = 0;
	char *a1 = com[1];
	if (argn < 2) {
		sh_cfail(e_nargs);
	}
	/* just in case we send a kill -9 $$ */
	p_flush();
	flag = SIGTERM;

	if ((*a1 == '-') && (a1[1] != '-')) {
		a1++;
		if (*a1 == 'l') {
			int n;
#ifdef POSIX
			if (argn > 2) {
				com++;
				while (a1 = *++com) {
					if (isdigit(*a1)) {
						/* Validate the input sig no. */
						rslt = getnum(a1, &n);
						n = n % 128;
						if ((rslt != 0) ||
						    (n < 0 || n >= NSIG)) {
							/*
							 * Serious syntax
							 * error ?
							 */
							sh_cfail(e_badarg);
						}

						sig_list(n + 1);
					} else {
						if ((flag = sig_number(a1)) <
						    0) {
							sh_cfail(e_option);
						}
						p_num(flag, NL);
					}
				}
			} else {
#endif /* POSIX */
				sig_list(0);
#ifdef POSIX
			}
#endif
			return (0);
		} else if (*a1 == 's') {
			sflg++;
			com++;
			a1 = com[1];
		}

		if (!a1 || (flag = sig_number(a1)) < 0 || flag >= NSIG) {
			sh_cfail(e_option);
		}
		com++;
	}
	if (*++com == 0) {
		sh_cfail(e_nargs);
	}
	/* Check for -- for bad mix options */
	a1 = com[0];
	if (*a1 == '-') {
		if (*++a1 == '-') {
			com++;				/* -- */
			a1 = com[0];
			if (a1 == NULL) 	/* no pid, pgid, jid, ... */
				sh_cfail(e_nargs);
		} else if (sflg) {
			sh_cfail(e_option);  /* Something like kill -s sig -l */
		}
	}
	if (job_walk(job_kill, flag, com)) {
		sh.exitval = 2;
	}
	return (sh.exitval);
}
#endif	/* JOBS */

#ifdef LDYNAMIC
#ifdef apollo
/*
 *  Apollo system support library loads into the virtual address space
 */

int
b_inlib(int argn, char **com)
{
	char *a1 = com[1];
	int status;
	short len;

	std_$call void loader_$inlib();
	if (!st.subflag && a1) {
		len = strlen(a1);
		loader_$inlib(*a1, len, status);
		if (status != 0) {
			sh_fail(a1, e_badinlib);
		}
	}
	return (0);
}
#else	/* apollo */
/*
 * dynamic library loader from Ted Kowalski
 */

int
b_inlib(int argn, char **com)
{
	char *a1;
	if (!st.subflag) {
		ldinit();
		addfunc(ldname("nam_putval", 0), (int (*)())nam_putval);
		addfunc(ldname("nam_strval", 0), (int (*)())nam_strval);
		addfunc(ldname("p_setout", 0), (int (*)())p_setout);
		addfunc(ldname("p_str", 0), (int (*)())p_str);
		addfunc(ldname("p_flush", 0), (int (*)())p_flush);
		while (a1 = *++com) {
			if (!ldfile(a1)) {
				sh_fail(a1, e_badinlib);
			}
		}
		loadend();
		if (undefined() != 0) {
			sh_cfail("undefined symbols");
		}
	}
}

/*
 * bind a built-in name to the function that implements it
 * uses Ted Kowalski's run-time loader
 */
int
b_builtin(int argn, char **com)
{
	struct namnod *np;
	int (*fn)();
	int (*ret_func())();
	if (argn != 3) {
		sh_cfail(e_nargs);
	}
	if (!(np = nam_search(com[1], sh.fun_tree, N_ADD))) {
		sh_fail(com[1], e_create);
	}
	if (!isnull(np)) {
		sh_fail(com[1], is_builtin);
	}
	if (!(fn = ret_func(ldname(com[2], 0)))) {
		sh_fail(com[2], e_found);
	}
	funptr(np) = fn;
	nam_ontype(np, N_BLTIN);
}
#endif	/* !apollo */
#endif /* LDYNAMIC */

/* CSI assumption1(ascii) made here. See csi.h. */
int
b_command(int argn, char **com)
{
	int ch;
	char *rpath;
	int r = 0;
	int flag = 0;
	int pflag = 0;
	int vflag = 0;
	int Vflag = 0;
	longlong_t save_index = opt_index;
	int save_char = opt_char;

	if (argn < 2) {
		sh_cfail(e_option);
	}

	opt_char = 0;
	opt_index = 1;
	opt_plus = 0;
	/*
	 * Check on -p | -v | -V mutual exclusive
	 */
	while (ch = optget(com, ":vVp")) {
		switch (ch) {
		case 'p':
			if (vflag || Vflag) {
				sh_cfail(e_option);
			}
			pflag++;
			break;
		case 'v':
			if (pflag || Vflag) {
				sh_cfail(e_option);
			}
			vflag++;
			break;
		case 'V':
			if (pflag || vflag) {
				sh_cfail(e_option);
			}
			Vflag++;
			break;
		default:
			sh_cfail(e_option);
			break;
		} /* switch */
	}
	com += opt_index;
	argn -= opt_index;
	opt_index = save_index;
	opt_char = save_char;

	p_setout(st.standout);
	if (vflag) {
		if (com[0] == (char *)NULL) {
			sh_cfail(e_option);
		}
		com--;
		r = sh_whence(com, 0);
	} else if (Vflag) {
		if (com[0] == (char *)NULL) {
			sh_cfail(e_option);
		}
		com--;
		r = sh_whence(com, V_FLAG);
	} else if (pflag || (argn > 0)) {
		if (com[0] == (char *)NULL) {
			sh_cfail(e_option);
		}
		r = command_popt(argn, com);
		if (r == ENOTFOUND) {
			cmd_shcfail(e_found, ENOTFOUND);
		}

	}

	return (r);
}

#ifdef FS_3D
#define	VLEN	14
int
b_vpath_map(int argn, char **com)
{
	int flag = (com[0][1] == 'p' ? 2 : 4);
	char *a1 = com[1];
	char version[VLEN + 1];
	char *vend;
	int n;

	switch (argn) {
	case 1:
	case 2:
		flag |= 8;
		p_setout(st.standout);
		if ((n = mount(a1, version, flag)) >= 0) {
			vend = stakalloc(++n);
			n = mount(a1, vend, flag | (n << 4));
		}
		if (n < 0) {
			if (flag == 2) {
				sh_cfail("cannot get mapping");
			} else {
				sh_cfail("cannot get versions");
			}
		}
		if (argn == 2) {
			p_str(vend, NL);
			break;
		}
		n = 0;
		while (flag = *vend++) {
			if (flag == ' ') {
				flag = e_sptbnl[n + 1];
				n = !n;
			}
			p_char(flag);
		}
		if (n) {
			newline();
		}
		break;
	default:
		if (!(argn & 1)) {
			sh_cfail(e_nargs);
		}
		/*FALLTHROUGH*/
	case 3:
		if (st.subflag) {
			break;
		}
		for (n = 1; n < argn; n += 2) {
			if (mount(com[n + 1], com[n], flag) < 0) {
				if (flag == 2) {
					sh_cfail("cannot set mapping");
				} else {
					sh_cfail("cannot set vpath");
				}
			}
		}
	}
	return (0);
}
#endif /* FS_3D */

#ifdef UNIVERSE
/*
 * there are three styles of universe
 * Pyramid and Sequent universes have <sys/universe.h> file
 * Masscomp universes do not
 */

int
b_universe(int argn, char **com)
{
	char *a1 = com[1];
	if (a1) {
		if (setuniverse(univ_number(a1)) < 0) {
			sh_cfail("invalid name");
		}
		att_univ = (strcmp(a1, "att") == 0);
		/* set directory in new universe */
		if (*(a1 = path_pwd(0)) == '/') {
			chdir(a1);
		}
		/* clear out old tracked alias */
		stakseek(0);
		stakputs((PATHNOD)->namid);
		stakputc('=');
		stakputs(nam_strval(PATHNOD));
		a1 = stakfreeze(1);
		env_namset(a1, sh.var_tree, nam_istype(PATHNOD, ~0));
	} else {
		int flag;
		char Xuniverse[TMPSIZ];
		if (getuniverse(Xuniverse) < 0) {
			sh_cfail("not accessible");
		} else {
			p_str(Xuniverse, NL);
		}
	}
	return (0);
}
#endif /* UNIVERSE */


#ifdef SYSSLEEP
/* fine granularity sleep builtin someday */
int
b_sleep(int argn, char **com)
{
	extern double atof();
	char *a1 = com[1];
	if (a1) {
		if (strmatch(a1, "*([0-9])?(.)*([0-9])")) {
			sh_delay(atof(a1));
		} else {
			sh_cfail(e_number);
		}
	} else {
		sh_cfail(e_argexp);
	}
	return (0);
}
#endif /* SYSSLEEP */

static const char flgchar[] = "efgilmnprstuvxEFHLPRZ";
static const int flgval[] = {E_FLAG, F_FLAG, G_FLAG, I_FLAG, L_FLAG, M_FLAG,
			N_FLAG, P_FLAG, R_FLAG, S_FLAG, T_FLAG, U_FLAG, V_FLAG,
			X_FLAG, N_DOUBLE|N_INTGER|N_EXPNOTE, N_DOUBLE|N_INTGER,
			N_HOST, N_LJUST, H_FLAG, N_RJUST, N_RJUST|N_ZFILL};
/*
 * process option flags for built-ins
 * flagmask are the invalid options
 */

static
int
flagset(char *flaglist, int flagmask)
{
	int flag = 0;
	int c;
	char *cp, *sp;
	int numset = 0;

	for (cp = flaglist + 1; c = *cp; cp++) {
		if (isdigit(c)) {
			if (argnum < 0) {
				argnum = 0;
				numset = -100;
			} else {
				numset++;
			}
			argnum = 10 * argnum + (c - '0');
		} else if (sp = strchr(flgchar, c)) {
			/* mbschr() not needed here */
			flag |= flgval[sp - flgchar];
	/* the following "if" is for the routines b_chdir and b_pwd only */
			if (flgval[sp - flgchar] & (PHYS_MODE|LOG_MODE)) {
				flg_precedence = flgval[sp - flgchar];
			}
		} else if (c != *flaglist) {
			goto badoption;
		}
	}
	if (numset > 0 && flag == 0) {
		goto badoption;
	}
	if ((flag & flagmask) == 0) {
		return (flag);
	}
badoption:
	sh_cfail(e_option);
	/* NOTREACHED */
}

/*
 * process command line options and store into newflag
 */

/* CSI assumption1(ascii) made here. See csi.h. */
static
int
scanargs(char *com[], int flags)
{
	char **argv;
	int flag;
	char *a1;
	newflag = 0;

	argv = ++com;
	if (*argv) {
		aflag = **argv;
	} else {
		aflag = 0;
	}
	if (aflag != '+' && aflag != '-') {
		return (0);
	}
	while ((a1 = *argv) && *a1 == aflag) {
		if (a1[1] && a1[1] != aflag) {
			flag = flagset(a1, flags);
		} else {
			flag = 0;
		}
		argv++;
		if (flag == 0) {
			break;
		}
		newflag |= flag;
	}
	return (argv-com);
}

/*
 * evaluate the string <s> or the contents of file <un.fd> as shell script
 * If <s> is not null, un is interpreted as an argv[] list instead of a file
 */

void
sh_eval(char *s)
{
	struct fileblk	fb;
	union anynode *t;
	char inbuf[IOBSIZE + 1];
	struct ionod *saviotemp = st.iotemp;
	struct slnod *saveslp = st.staklist;
	io_push(&fb);
	if (s) {
		io_sopen(s);
		if (sh.un.com) {
			fb.feval = sh.un.com;
			if (*fb.feval) {
				fb.ftype = F_ISEVAL;
			}
		}
	} else if (sh.un.fd >= 0) {
		io_init(input = sh.un.fd, &fb, inbuf);
	}
	sh.un.com = 0;
	st.exec_flag++;
	t = sh_parse(NL, NLFLG|MTFLG);
	st.exec_flag--;
	if (is_option(READPR) == 0) {
		st.states &= ~READPR;
	}
	if (s == NULL && hist_ptr) {
		hist_flush();
	}
	p_setout(ERRIO);
	io_pop(0);
	sh_exec(t, (int)(st.states & (ERRFLG | MONITOR)));
	sh_freeup();
	st.iotemp = saviotemp;
	st.staklist = saveslp;
}


/*
 * Given the name or number of a signal return the signal number
 */

static
int
sig_number(char *string)
{
	int n;
	if (isdigit(*string)) {
		n = atoi(string);
	} else {
		ltou(string, string);
		n = sh_lookup(string, sig_names);
		n &= (1 << SIGBITS) - 1;
		n--;
#ifdef	SIGSTRINGS
		if (n >= 0) {
			return (n);
		}
		if (str2sig(string, &n) < 0) {
			return (-1);
		}
#endif	/* SIGSTRINGS */
	}
	return (n);
}

#ifdef JOBS
/*
 * list all the possible signals
 * If flag is 1, then the current trap settings are displayed
 */
static
void
sig_list(int flag)
{
	const struct sysnod	*syscan;
	int n = MAXTRAP;
	const char *names[MAXTRAP+3];
#ifdef	SIGSTRINGS
	char signames[MAXTRAP*10], *signamealloc;
#endif	/* SIGSTRINGS */
	syscan = sig_names;
	p_setout(st.standout);
	/* not all signals may be defined */

#ifdef POSIX
	if (flag < 0) {
		n += 2;
	}
#else
	NOT_USED(flag);
#endif /* POSIX */
	while (--n >= 0) {
		names[n] = e_trap;
	}
	while (*syscan->sysnam) {
		n = syscan->sysval;
		n &= ((1 << SIGBITS) - 1);
		names[n] = syscan->sysnam;
		syscan++;
	}
#ifdef	SIGSTRINGS
	signamealloc = signames;
	for (n = 1; n < NSIG; n++) {
		if (names[n + 1] == e_trap) {
			if (sig2str(n, signamealloc) >= 0) {
				names[n + 1] = signamealloc;
				signamealloc += strlen(signamealloc) + 1;
			}
		}
	}
#endif	/* SIGSTRINGS */
	n = MAXTRAP - 1;

#ifdef POSIX
	if (flag < 0) {
		n += 2;
	}
#endif /* POSIX */
	while (names[--n] == e_trap)
		;
	names[n + 1] = NULL;
#ifdef POSIX
	if (flag < 0) {
		int sig;

		for (sig = 0; sig < MAXTRAP; sig++) {
			if (st.trapcom[sig]) {
				char	sigtstr[SIG2STR_MAX];

				if (sig == NSIG) {
					continue;
				}
				p_str("trap -- ", 0);
				p_qstr(st.trapcom[sig], ' ');
				if (sig < NSIG) {
					strcpy(sigtstr, names[sig + 1]);
				} else {
					switch (sig) {
					case NSIG + 1:
						strcpy(sigtstr, "ERR");
						break;
					case NSIG + 2:
						strcpy(sigtstr, "DEBUG");
						break;

					}
				}
				p_str(sigtstr, NL);
			}
		}
	} else if (flag) {
		if (flag <= n && names[flag]) {
			p_str(names[flag], NL);
		} else {
			p_num(flag - 1, NL);
		}
	} else
#endif /* POSIX */
	{
		int n;
		char	tmpstr[SIG2STR_MAX];
		/*
		 * XPG4: sig_list(0) does not print the
		 * correct output format for kill -l.
		 * The original ksh sig_list(0) does
		 * not print all the signals upto NSIG.
		 * Only kill -l will get here.
		 */
		for (n = 0; n < NSIG; n++) {
			if (sig2str(n, tmpstr) == -1) {
				if (n > SIGTHAW) {
					continue;
				}
				sh_cfail(e_sigtrans);
			}
			p_str(tmpstr, SP);
		}
		p_str("", NL);
	}
}
#endif	/* JOBS */

#ifdef SYSSLEEP
/*
 * delay execution for time <t>
 */

#ifdef _poll_
#include	<poll.h>
#endif /* _poll_ */
#ifndef TIC_SEC
#ifdef HZ
#define	TIC_SEC	HZ	/* number of ticks per second */
#else
#define	TIC_SEC	60	/* number of ticks per second */
#endif /* HZ */
#endif /* TIC_SEC */


void
sh_delay(double t)
{
	int n = t;
#ifdef _poll_
	struct pollfd fd;
	if (t <= 0) {
		return;
	} else if (n > 30) {
		sleep(n);
		t -= n;
	}
	if (n = 1000 * t) {
		poll(&fd, 0, n);
	}
#else		/* _poll_ */
#ifdef _SELECT5_
	struct timeval timeloc;
	if (t <= 0) {
		return;
	}
	timeloc.tv_sec = n;
	timeloc.tv_usec = 1000000 * (t - (double)n);
	select(0, (fd_set*)0, (fd_set*)0, (fd_set*)0, &timeloc);
#else		/* _SELECT5_ */
#ifdef _SELECT4_
	/* for 9th edition machines */
	if (t <= 0) {
		return;
	}
	if (n > 30) {
		sleep(n);
		t -= n;
	}
	/* IS THIS WHAT WE REALLY MEAN, SHOULD IT BE == ??? */
	if (n = 1000 * t) {
		select(0, (fd_set*)0, (fd_set*)0, n);
	}
#else		/* _SELECT4_ */
	struct tms tt;
	if (t <= 0) {
		return;
	}
	sleep(n);
	t -= n;
	if (t) {
		clock_t begin = times(&tt);
		if (begin == 0) {
			return;
		}
		t *= TIC_SEC;
		n += (t + .5);
		while ((times(&tt) - begin) < n)
			;
	}
#endif /* _SELECT4_ */
#endif /* _SELECT5_ */
#endif /* _poll_ */
}
#endif /* SYSSLEEP */

#ifdef UNIVERSE
#ifdef _sys_universe_
int
univ_number(char *str)
{
	int n = 0;
	while (n < NUMUNIV) {
		if (strcmp(str, univ_name[n]) == 0) {
			return (univ_index(n));
		}
		n++;
	}
	return (-1);
}
#endif /* _sys_universe_ */
#endif /* UNIVERSE */

static
char **
ddash_handler(char **cmdline)
{
	char	*p = cmdline[0];
	if (p && *p == '-' && p[1] == '-') {
		cmdline++;
	}
	return (cmdline);
}

static
int
command_opt(int argn, char **com, char **cmdp)
{
	struct namnod *np;
	struct namnod *np1;
	char *a1, *a2, *a3;
	int sh_resword;		/* Shell reserved word */
	int sh_bltin;		/* Shell builtin */
	int sh_func;		/* Shell function */
	int sh_alias;		/* Shell alias - non-tracked */
	int u_func;		/* User function */
	int t_alias;		/* Tracked alias */
	int notrack;
	int q;
	int nopath;

	com -= 1;
	a1 = a2 = *++com;

	sh_resword = sh_bltin = sh_func = sh_alias = u_func = 0;
	q = 0;		/* Error flag for each command */
	nopath = 1;
	t_alias = 1;


	sh_resword = sh_lookup(a1, tab_reserved);
	np = nam_search(a1, sh.fun_tree, N_NULL);
	np1 = nam_search(a1, sh.alias_tree, N_NULL);
	/*
	 * Absolute path name for:
	 *		- Utilities
	 *		- Regular built-in utilities
	 *		- Command name with slash character
	 *		- Functions
	 *	found using PATH variable
	 *
	 * Name itself for:
	 *		- Shell functions
	 *		- Special built-in utilities
	 *		- Regular built-in utilities
	 *	 not associated with a PATH search
	 *		- Shell reserved words
	 * Alias definition
	 *		- aliases
	 *
	 * command [-p] overrides only user and shell functions
	 *
	 */
	/*
	 * XPG4: Buffer the output as we don't know
	 * whether there will be an error until
	 * after search and XPG4 requires the error
	 * output goes to STDERR.
	 */
	a3 = (char *)NULL;
	if (!sh_resword || np == NULL) {
		if (np1 && (t_alias = (nam_istype(np1, T_FLAG) == 0)) &&
		    (a3 = nam_strval(np1))) {
			sh_alias++;		/* non-tracked alias */
		} else if (np) {
			/* built-ins and functions next */
			if (is_abuiltin(np)) {
				sh_bltin++;
			} else {
				if (!isnull(np) && is_afunction(np)) {
					u_func++;
				} else if (isnull(np) && !is_afunction(np)) {
					goto search;
				} else {
					sh_func++;
				}
				if (u_func|sh_func) {
					/* Override user functions ? */
					goto search;
				}
			}
		} else {
			/*
			 * The output needs a path search
			 */
search:
			if (path_search(a1, 2) == 0) {
				a2 = sh.lastpath;
				nopath = (a2 == (char *)NULL);
			}
			sh.lastpath = 0;
			if (!a2) {
				q = ((xecmsg == e_exec) ? ECANTEXEC: ENOTFOUND);
			}

		}
	}

	if (q) {
		sh.cmdname = a1;
		sh.exitval = q;
	} else {
		*cmdp = 0;	/* No need to exec later */
		if ((sh_resword && np != NULL) | sh_alias | sh_bltin) {
			sh.exitval = (*funptr(np))(argn, com);
		} else if (nopath && (u_func | sh_func)) {
			struct slnod *slp;
			/* increase refcnt for unset */
			slp = (struct slnod *)np->value.namenv;
			sh_funstaks(slp->slchild, 1);
			staklink(slp->slptr);
			sh_funct((union anynode *)(funtree(np)), com,
			    (int)(nam_istype(np, T_FLAG) ? EXECPR : 0), 0);
			sh_funstaks(slp->slchild, -1);
			stakdelete(slp->slptr);
		} else {
			*cmdp = a2;
		}
	}

	return (q);
}

static
int
command_popt(int argn, char **com)
{
	char	*cmdp = (char *)NULL;
	int r = 0, i;
	int ret_stat = 0;
	int child_id;

	if (mbschr(com[0], '/')) {
		cmdp = com[0];	/* Path command - no need to search */
	} else {
		if (r = command_opt(argn, com, &cmdp)) {
			return (r);
		}
	}

	if (cmdp == (char *)NULL) {
		return (sh.exitval);	/* already done if builtin */
	} else {
		int argno, len;
		char *p, **newcom;
		/*
		 * Put double quote around arguments to build
		 * the new command line for re-parsing.
		 */
		newcom = (char **)stakalloc((argn + 1) * sizeof (char *));
		len = 0;
		for (argno = 1; argno < argn; argno++) {
			i = strlen(com[argno]);
			len += (i + 3);
			newcom[argno] = (char *)i;
		}
		p = stakseek(len);
		for (argno = 1; argno < argn; argno++) {
			*p = DQUOTE;
			len = (int)newcom[argno];
			(void) memcpy(p + 1, com[argno], len);
			p[1 + len] = DQUOTE;
			p[1 + len + 1] = '\0';
			newcom[argno] = p;
			p += (len + 3);
		}
		newcom[argno] = NULL;
		(void) stakfreeze(0);

		sh.un.com = newcom + 1;
		newcom[0] = cmdp;
		sh_eval(newcom[0]);
		return (sh.exitval);
	}
}
/*
 * From MKS c_umask()
 */
static mode_t mapping[3][3] = {
	/* x: 1,    w: 2,    r: 4 */
	S_IXOTH, S_IWOTH, S_IROTH,  /* o: 000_ */
	S_IXGRP, S_IWGRP, S_IRGRP,  /* g: 00_0 */
	S_IXUSR, S_IWUSR, S_IRUSR,  /* u: 0_00 */
};
static
void
umask_sprint(mode_t m)
{
	char buf[3*6], *op = buf;
	int i, j;
	mode_t	smode = ~m & (READ|WRITE|EXEC);
	/*
	 * Turn mode_t into u=[rwx], g=[rwx], o=[rwx]
	 */
	for (i = 3; --i >= 0; ) {
		*op++ = "ogu"[i];
		*op++ = '=';
		for (j = 3; --j >= 0; ) {
			if (smode & mapping[i][j]) {
				*op++ = "xwr"[j];
			}
		}
		*op++ = "\0,,"[i];
	}
	p_str(buf, NL);
}

static
void
unalias_all(struct Amemory *troot)
{
	char *a1;
	struct namnod *np;
	struct slnod *slp;
	int r = 0;

	if (troot) {
		(void) gscan_some(unalias_nam, troot, 0, 0);
	}
}

static
void
unalias_nam(struct namnod *np)
{
	if ((np->value.namflg !=
	    (unsigned)(N_FREE | N_EXPORT | T_FLAG | NO_ALIAS))) {
		nam_free(np);
	}
}

static
int
getnum(char *numstr, int *argnum)
{
	char	*pestr;

	errno = 0;
	if (isdigit(*numstr)) {
		*argnum = strtol(numstr, &pestr, 10);
		if (errno || (pestr && *pestr != '\0')) {
			return (-1);
		}
	} else {
		return (-1);
	}

	return (0);
}

int
b_hash(int argn, char **com)
{
	int ch;
	longlong_t save_index = opt_index;
	int save_char = opt_char;

	if (com[1]) {
		aflag = (*com[1] == '-' ? '-' : 1);
	} else {
		aflag = 0;
	}
	opt_char = 0;
	opt_index = 1;
	opt_plus = 0;
	while (ch = optget(com, ":r")) {
		switch (ch) {
		case 'r':
			/* hash -r processing */
			(void) gscan_some(rehash, sh.track_tree,
			    T_FLAG, T_FLAG);
			break;
		default:
			sh_cfail(e_option);
			break;
		}
	}

	com += opt_index -1;
	opt_index = save_index;
	opt_char = save_char;

	aflag = '-';
	return (b_common(com, T_FLAG, sh.track_tree));
}

int
b_type(int argn, char **com)
{
	int ch;
	char *a1 = com[1];

	NOT_USED(argn);

	if (a1 && (*a1++ == '-')) {
		if (*a1 == '-') {
			com++;
		} else {
			sh_cfail(e_option);
		}
	}

	p_setout(st.standout);
	return (sh_whence(com, V_FLAG));
}
