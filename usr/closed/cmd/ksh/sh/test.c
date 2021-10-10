/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * test expression
 * [ expression ]
 * Rewritten by David Korn
 */

#include	"defs.h"
#include	"test.h"
#ifdef OLDTEST
#   include	"sym.h"
#endif /* OLDTEST */
#include	<string.h>

#define test_type(file,mask,value)	((test_mode(file)&(mask))==(value))
#define	tio(a,f)	(sh_access(a,f)==0)
static time_t ftime_compare();
static int test_mode();
static int test_stat();
static struct stat statb;
int	test_binop(int, char *, char *);
int	unop_test(int, char *);
int	test_inode(char *, char *);


#ifdef OLDTEST
/* single char string compare */
#define c_eq(a,c)	(*a==c && *(a+1)==0)
/* two character string compare */
#define c2_eq(a,c1,c2)	(*a==c1 && *(a+1)==c2 && *(a+2)==0)

int b_test();

static char *nxtarg();
static int exp(int);
static int e3(void);

static int ap, ac;
static char **av;

/* CSI assumption1(ascii) made here. See csi.h. */
int b_test(argn, com)
char *com[];
register int argn;
{
	register char *p = com[0];
	av = com;
	ap = 1;
	if(c_eq(p,'['))
	{
		p = com[--argn];
		if(!c_eq(p, ']'))
			cmd_shfail(e_test, e_bracket, ETEST);
	}
	if(argn <= 1)
		return(1);
	ac = argn;
	return(!exp(0));
}

/*
 * evaluate a test expression.
 * flag is 0 on outer level
 * flag is 1 when in parenthesis
 * flag is 2 when evaluating -a 
 */

/* CSI assumption1(ascii) made here. See csi.h. */
static int
exp(int flag)
{
	int r;
	char *p;
	r = e3();
	while(ap < ac)
	{
		p = nxtarg(0);
		/* check for -o and -a */
		if(flag && c_eq(p,')'))
		{
			ap--;
			break;
		}
		if(*p=='-' && *(p+2)==0)
		{
			if(*++p == 'o')
			{
				if(flag==2)
				{
					ap--;
					break;
				}
				r |= exp(3);
				continue;
			}
			else if(*p == 'a')
			{
				r &= exp(2);
				continue;
			}
		}
		if(flag==0)
			break;
		cmd_shfail(e_test, e_synbad, ETEST);
	}
	return(r);
}

static char *nxtarg(mt)
{
	if(ap >= ac)
	{
		if(mt)
		{
			ap++;
			return(0);
		}
		cmd_shfail(e_test, e_argexp, ETEST);
	}
	return(av[ap++]);
}


/* CSI assumption1(ascii) made here. See csi.h. */
static int
e3(void)
{
	char *a;
	char *p2;
	int p1;
	char *op;
	a=nxtarg(0);

	/*
	 * XPG4: Fix the parsing problem for test ( uni_opt expr)
	 *       It fails on "test \( !x \) -o y"
	 */
	if (av[ap] && c_eq(av[ap], ')'))
		return (*a != 0);

	/* Fix the parsing problem for [ ! ] */
	if (av[ac] && c_eq(av[ap], ']'))
		return (*a != 0);

	/* XPG4:  don't return here if eg. test "!" = "f" */
	if (c_eq(a, '!') && !c_eq(av[ap], '=') && !c2_eq(av[ap], '!', '='))
		return(!e3());

	/* XPG4:  don't return here if eg. test "(" != "x" */
	if (c_eq(a, '(') && !c_eq(av[ap], '=') && !c2_eq(av[ap], '!', '='))
	{
		p1 = exp(1);
		p2 = nxtarg(0);
		if(!c_eq(p2, ')'))
			cmd_shfail(e_test,e_paren, ETEST);
		return(p1);
	}
	p2 = nxtarg(1);
	/*
	 * XPG4:  don't goto skip if eg. test -n =
	 * or eg. [ -z != ]
	 */
	if (p2 != 0 && (c_eq(p2, '=') || c2_eq(p2, '!', '=')) &&
	    (!strchr(test_unops, a[1]) ||
	    (strchr(test_unops, a[1]) && av[ap] != 0 && !c_eq(av[ap], ']'))))
			goto skip;
	if(c2_eq(a,'-','t'))
	{
		if(p2 && isdigit(*p2))
			 return(*(p2+1)?0:tty_check(*p2-'0'));
		else
		{
		/* test -t with no arguments */
			ap--;
			return(tty_check(1));
		}
	}
	if((*a=='-' && *(a+2)==0))
	{
		if(!p2)
		{
			/* for backward compatibility with new flags */
			/* mbschr not needed here */
			if(a[1]==0 || !strchr(test_unops+10,a[1]))
				return(1);
			cmd_shfail(e_test, e_argexp, ETEST);
		}
		/* mbschr not needed here */
		if(strchr(test_unops,a[1]))
			return(unop_test(a[1],p2));
	}
	if(!p2)
	{
		ap--;
		return(*a!=0);
	}
skip:
	p1 = sh_lookup(p2,test_optable);
	op = p2;
	if((p1&TEST_BINOP)==0)
		p2 = nxtarg(0);
	if(p1==0)
		cmd_shfail(op,e_testop, ETEST);
	return(test_binop(p1,a,p2));
}
#endif /* OLDTEST */

int
unop_test(int op, char *arg)
{
	switch(op)
	{
	case 'r':
		return(tio(arg, R_OK));
	case 'w':
		return(tio(arg, W_OK));
	case 'x':
		return(tio(arg, X_OK));
	case 'd':
		return(test_stat(arg,&statb)>=0 && S_ISDIR(statb.st_mode));
	case 'c':
		return(test_stat(arg,&statb)>=0 && S_ISCHR(statb.st_mode));
	case 'b':
		return(test_stat(arg,&statb)>=0 && S_ISBLK(statb.st_mode));
	case 'f':
		return(test_stat(arg,&statb)>=0 && S_ISREG(statb.st_mode));
	case 'u':
		return((test_mode(arg)&S_ISUID) != 0);
	case 'g':
		return((test_mode(arg)&S_ISGID) != 0);
	case 'H':
#ifdef S_ISCDF
	{
		extern char *strcat(), *malloc();
		int t_mode = test_mode(arg);
		if(S_ISCDF(t_mode))
			return(1);
		else
		{
			char *t_arg = malloc(strlen(arg) + 2);
			strcpy(t_arg, arg);
			t_mode = test_mode(strcat(t_arg, "+"));
			free(t_arg);
			return(S_ISCDF(t_mode) != 0);
			
		}
	}
#else
		return(0);
#endif /* S_ISCFD */
	case 'k':
#ifdef S_ISVTX
		return((test_mode(arg)&S_ISVTX) != 0);
#else
		return(0);
#endif /* S_ISVTX */
	case 'V':
#ifdef FS_3D
	{
		int offset = staktell();
		if(stat(arg,&statb)<0 || !S_ISREG(statb.st_mode))
			return(0);
		/* add trailing / */
		op = strlen(arg);
		stakseek(offset+op+2);
		stakseek(offset);
		arg = (char*)memcpy(stakptr(offset),arg,op);
		arg[op]='/';
		arg[op+1]=0;
		return(stat(arg,&statb)>=0 && S_ISDIR(statb.st_mode));
	}
#else
		return(0);
#endif /* FS_3D */
	case 'L':
	/* -h is not documented, and hopefully will disappear */
	case 'h':
#ifdef LSTAT
	{
		if(*arg==0 || lstat(arg,&statb)<0)
			return(0);
		return((statb.st_mode&S_IFMT)==S_IFLNK);
	}
#else
		return(0);
#endif	/* S_IFLNK */

	case 'C':
#ifdef S_IFCTG
		return(test_type(arg,S_IFMT,S_IFCTG));
#else
		return(0);
#endif	/* S_IFCTG */

	case 'S':
#ifdef S_IFSOCK
		return(test_type(arg,S_IFMT,S_IFSOCK));
#else
		return(0);
#endif	/* S_IFSOCK */

	case 'p':
#ifdef S_ISFIFO
		return(test_stat(arg,&statb)>=0 && S_ISFIFO(statb.st_mode));
#else
		return(0);
#endif	/* S_ISFIFO */
	case 'n':
		return(*arg != 0);
	case 'z':
		return(*arg == 0);
	case 's':
	case 'O':
	case 'G':
	{
		if(test_stat(arg,&statb)<0)
			return(0);
		if(op=='s')
			return(statb.st_size>0);
		else if(op=='O')
			return(statb.st_uid==sh.userid);
		return(statb.st_gid==sh.groupid);
	}
	case 'e':		/* XPG4: New option */
		return(tio(arg, F_OK));
#ifdef NEWTEST
	case 'a':
		return(tio(arg, F_OK));
	case 'o':
		op = sh_lookup(arg,tab_options);
		return(op && is_option((1L<<op))!=0);

	case 't':
		if(isdigit(*arg) && arg[1]==0)
			 return(tty_check(*arg-'0'));
		return(0);
#endif /* NEWTEST */
#ifdef OLDTEST
	default:
	{
		static char a[3] = "-?";
		a[1]= op;
		cmd_shfail(a,e_testop, ETEST);
		/* NOTREACHED  */
	}
#endif /* OLDTEST */
	}
}

int
test_binop(int op, char *left, char *right)
{
	longlong_t ll_1, ll_2;
	if(op&TEST_ARITH)
	{
		ll_1 = sh_arith(left);
		ll_2 = sh_arith(right);
	}
	switch(op)
	{
		/* op must be one of the following values */
#ifdef OLDTEST
		case TEST_AND:
		case TEST_OR:
			ap--;
			return(*left!=0);
#endif /* OLDTEST */
#ifdef NEWTEST
		case TEST_PEQ:
			return(strmatch(left, right));
		case TEST_PNE:
			return(!strmatch(left, right));
		case TEST_SGT:
			return(strcoll(left, right)>0);
		case TEST_SLT:
			return(strcoll(left, right)<0);
#endif /* NEWTEST */
		case TEST_SEQ:
			return(strcmp(left, right)==0);
		case TEST_SNE:
			return(strcmp(left, right)!=0);
		case TEST_EF:
			return(test_inode(left,right));
		case TEST_NT:
			return(ftime_compare(left,right)>0);
		case TEST_OT:
			return(ftime_compare(left,right)<0);
		case TEST_EQ:
			return (ll_1 == ll_2);
		case TEST_NE:
			return (ll_1 != ll_2);
		case TEST_GT:
			return (ll_1 > ll_2);
		case TEST_LT:
			return (ll_1 < ll_2);
		case TEST_GE:
			return (ll_1 >= ll_2);
		case TEST_LE:
			return (ll_1 <= ll_2);
	}
	/* NOTREACHED */
	return (0);
}

/*
 * returns the modification time of f1 - modification time of f2
 */

static time_t ftime_compare(file1,file2)
char *file1,*file2;
{
	struct stat statb1,statb2;
	if(test_stat(file1,&statb1)<0)
		statb1.st_mtime = 0;
	if(test_stat(file2,&statb2)<0)
		statb2.st_mtime = 0;
	return(statb1.st_mtime-statb2.st_mtime);
}

/*
 * return true if inode of two files are the same
 */

int
test_inode(char *file1, char *file2)
{
	struct stat stat1,stat2;
	if(test_stat(file1,&stat1)>=0  && test_stat(file2,&stat2)>=0)
		if(stat1.st_dev == stat2.st_dev && stat1.st_ino == stat2.st_ino)
			return(1);
	return(0);
}


/*
 * This version of access checks against effective uid/gid
 * The static buffer statb is shared with test_mode.
 */

int
sh_access(char *name, int mode)
{
	if(*name==0)
		return(-1);
	if(strmatch(name,(char*)e_devfdNN))
		return(io_access(atoi(name+8),mode));
	/* can't use access function for execute permission with root */
	if(mode==X_OK && sh.euserid==0)
		goto skip;
	if(sh.userid==sh.euserid && sh.groupid==sh.egroupid)
		return(access(name,mode));
#ifdef SETREUID
	/* swap the real uid to effective, check access then restore */
	/* first swap real and effective gid, if different */
	if(sh.groupid==sh.euserid || setregid(sh.egroupid,sh.groupid)==0) 
	{
		/* next swap real and effective uid, if needed */
		if(sh.userid==sh.euserid || setreuid(sh.euserid,sh.userid)==0)
		{
			mode = access(name,mode);
			/* restore ids */
			if(sh.userid!=sh.euserid)
				setreuid(sh.userid,sh.euserid);
			if(sh.groupid!=sh.egroupid)
				setregid(sh.groupid,sh.egroupid);
			return(mode);
		}
		else if(sh.groupid!=sh.egroupid)
			setregid(sh.groupid,sh.egroupid);
	}
#endif /* SETREUID */
skip:
	if(test_stat(name, &statb) == 0)
	{
		if(mode == F_OK)
			return(mode);
		else if(sh.euserid == 0)
		{
			if(!S_ISREG(statb.st_mode) || mode!=X_OK)
				return(0);
		    	/* root needs execute permission for someone */
			mode = (S_IXUSR|S_IXGRP|S_IXOTH);
		}
		else
		{
			/*
                         * Let the kernel check for access
                         * This is for multiple groups and ACL
                         */
                        return(access(name,mode | 010));
		}
		if(statb.st_mode & mode)
			return(0);
	}
	return(-1);
}


/*
 * Return the mode bits of file <file> 
 * If <file> is null, then the previous stat buffer is used.
 * The mode bits are zero if the file doesn't exist.
 */

static int test_mode(file)
register char *file;
{
	if(file && (*file==0 || test_stat(file,&statb)<0))
		return(0);
	return(statb.st_mode);
}

/*
 * do an fstat() for /dev/fd/n, otherwise stat()
 */
static int test_stat(f,buff)
char *f;
struct stat *buff;
{
int ret;
ret = 0;
	if (f == 0 || *f == 0)
		return (-1);
	if (strmatch(f, (char *)e_devfdNN)) {
		ret = fstat(atoi(f+8), buff);
		return (ret);
	} else {
		ret = stat(f, buff);
		return (ret);
	}
}
