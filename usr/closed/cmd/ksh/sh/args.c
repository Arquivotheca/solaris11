#pragma ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */
/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
*/

/*
 * UNIX shell
 *
 * S. R. Bourne
 * Rewritten by David Korn
 * AT&T Bell Laboratories
 *
 */

#include	"defs.h"
#ifdef DEVFD
#   include	"jobs.h"
#endif	/* DEVFD */
#include	"terminal.h"
#undef ESCAPE
#include	"sym.h"
#include	"builtins.h"
#include	<string.h>
#include	<libintl.h>


#ifdef DEVFD
    void	close_pipes();
#endif	/* DEVFD */

extern void	gsort();
extern int	strcmp();

static int		arg_expand();
static struct dolnod*	copyargs();
static void		print_opts();
static void           	print_opts_formatted();
static int		split();

static char		*null;
static struct dolnod	*argfor; /* linked list of blocks to be cleaned up */
static struct dolnod	*dolh;
static char flagadr[12];
static const char flagchar[] =
{
	'i',	'n',	'v',	't',	's',	'x',	'e',	'r',	'k',
	'u', 'f',	'a',	'm',	'h',	'p',	'c',	'b',	'C',	0
};
static const optflag flagval[]  =
{
	INTFLG,	NOEXEC,	READPR,	ONEFLG, STDFLG,	EXECPR,	ERRFLG,	RSHFLG,	KEYFLG,
	NOSET,	NOGLOB,	ALLEXP,	MONITOR, HASHALL, PRIVM, CFLAG,
	NOTIFY, NOCLOB,	0
};

/* ======== option handling	======== */

/*
 *  This routine turns options on and off
 *  The options "icr" are illegal from set command.
 *  The -o option is used to set option by name
 *  This routine returns the number of non-option arguments
 */

int arg_opts(argc,com,setflag)
char **com;
int  argc;
int setflag;
{
	char *cp;
	register wchar_t c, d;
	register char *flagc;
	register char **argv = com;
	register optflag newflags=opt_flags;
	register optflag opt;
	int trace = is_option(EXECPR);
	char minus;
	struct namnod *np = (struct namnod*)0;
	char sort = 0;
	char minmin = 0;
	while((cp= *++argv) && (((c= mb_peekc(cp))=='-') || c=='+'))
	{
		minus = (c == '-');
		argc--;
		if ((c= mb_peekc(++cp))==0)
		{
			newflags &= ~(EXECPR|READPR);
			trace = 0;
			argv++;
			break;
		}
		else if(c == '-')
		{
			minmin = 1;
			argv++;
			break;
		}
		while (c = mb_nextc((const char **)&cp))
		{
			if(setflag)
			{
				if(c=='s')
				{
					sort = 1;
					continue;
				}
				else if(c=='A')
				{
					if(argv[1]==0)
						sh_fail(*argv, e_argexp);
					np = env_namset(*++argv,sh.var_tree,P_FLAG|V_FLAG);
					argc--;
					if(minus)
						nam_free(np);
					continue;
				}
				else if (c=='i' || c=='c' || c=='r')
					sh_fail(*argv, e_option);
			}
			if(c == 'c' && minus && argc >= 2)
			{
				newflags |= CFLAG;
				continue;
			}
#ifdef apollo
			/* 
			 * New option(-D) allowing the user to define
			 * envirnoment variables on the command line.
			 */
			if (c == 'D')	/* define env variable */
			{
				char *newenv;
				
				if (minus)
				{
					if (cp && *cp)
					{
						if (strchr(cp, '='))
							newenv = cp;
						else
						{
							newenv = malloc(strlen(cp) + 2);
							strcpy(newenv, cp);
							strcat(newenv, "=");
						}
						env_namset(newenv,sh.var_tree,N_EXPORT|N_FREE);
					}
				} else
					sh_fail(*argv, e_option);
				argc--;
				break;
			}
#endif /* apollo */
			/* mbschr() not needed here */
			if(iswascii(c) && (flagc=strchr(flagchar,c)))
				opt = flagval[flagc-flagchar];
#ifdef WEXP
			else if (c == '') {	/* CTRL-E ? */
				char	invchar[]=";|&<>(){}\n";
				/* Start command substitution flag */
				wchar_t lastc = 0;
				int	closeq = 0;
				char *p=argv[1];

				/* Prevent manual -CTRL-E */
				if (is_option(INTFLG) || setflag)
					sh_fail(com[0], e_option);

				/* NULL argument */
				if (p == (char *)NULL || *p == '\0')
					cmd_shfail(com[0], NIL, WEXP_BADCHAR);

				while (c= mb_nextc((const char **)&p)) {
					/*
					 * Scan for special token to skip the
					 * content in between " ", ` ` or $( )
					 */
					switch (c) {
						case ESCAPE:
							c = mb_nextc(
							(const char **)&p);
							continue;
						case LITERAL:
						case DQUOTE:
						case SQUOTE:
							closeq = c;
							break;
						case LPAREN:
							if (lastc == DOLLAR)
								closeq = RPAREN;
							break;
						case LBRACE:
							if (lastc == DOLLAR)
								closeq = RBRACE;
							break;
					}

					/*
					 * If the start of command substitution
					 * is found, then skip until the end of
					 * it.
					 */
					if (closeq)
						while (c=mb_nextc(
							(const char **)&p))
							if (c == closeq) {
								closeq = 0;
								c= mb_nextc(
								(const char **)
								&p);
								break;
							}

					/* The end of cmd subs. is also the end ? */
					if (!c) {
						if (closeq)
							cmd_shfail(com[0], NIL,
								WEXP_BADCHAR);
						break;
					}

					/*
					 * Check for unquoted illegal chars
					 * outside of command substitution context
					 */
					if (iswascii(c) && mbschr(invchar, c))
						cmd_shfail(com[0], NIL,
							WEXP_BADCHAR);

					lastc = c;
				}

				newflags = WEXP_E;

				while (cp && (d = mb_peekc(cp))) {
					if (d == 'N')
						/* No cmd substitution */
						newflags |= WEXP_N;
					else if (d == 'u') {
						/* mbschr() not needed here */
						flagc=strchr(flagchar, 'u');
						newflags |= flagval[flagc-flagchar];
					}
					cp++;
				}

				/* Pad print -r -- in front of the word */
				sh.comdiv = malloc(strlen(*++argv)+13);
				strcpy(sh.comdiv, "print -r -- ");
				strcat(sh.comdiv, *argv);
				argc--;
				break;		/* No other options */
			}
#endif /* WEXP */
			else if (c != 'o' || mb_peekc(cp))
				sh_fail(*argv,e_option);
			else
			{
				if(*++argv==NIL)
				{
					if(trace)
						sh_trace(com,1);
					trace = 0;
					if (minus)
					{
						print_opts(newflags);
					}
					else
					{
						print_opts_formatted(newflags);
					}

					argv--;
					continue;
				}
				else
				{
					int	i;

					argc--;
					i=sh_lookup(*argv,tab_options);
					opt = 1L<<i;
					if(opt&(1|INTFLG|RSHFLG))
						sh_fail(*argv,e_option);
				}
			}
			if(minus)
			{
#if ESH || VSH
				if(opt&(EDITVI|EMACS|GMACS))
					newflags &= ~ (EDITVI|EMACS|GMACS);
#endif
				newflags |= opt;
			}
			else
			{
				if(opt==EXECPR)
					trace = 0;
				newflags &= ~opt;
			}
		}
	}
	if (!setflag &&(newflags & CFLAG)) {
		if (argc) {
			sh.comdiv = *argv++;
			if (!sh.comdiv)
				sh_fail("-c", e_cmdstring);
			argc--;
		}
	}
	/* cannot set -n for interactive shells since there is no way out */
	if(is_option(INTFLG))
		newflags &= ~NOEXEC;
#ifdef RAWONLY
	if(is_option(EDITVI))
		newflags |= VIRAW;
#endif	/* RAWONLY */
	if(!setflag)
		goto skip;
	if(sort)
	{
		if(argc>1)
			gsort(argv,argc-1,strcoll);
		else
			gsort(st.dolv+1,st.dolc,strcoll);
	}
	if((newflags&PRIVM) && !is_option(PRIVM))
	{
		if((sh.userid!=sh.euserid && setuid(sh.euserid)<0) ||
			(sh.groupid!=sh.egroupid && setgid(sh.egroupid)<0) ||
			(sh.userid==sh.euserid && sh.groupid==sh.egroupid))
			newflags &= ~PRIVM;
	}
	else if(!(newflags&PRIVM) && is_option(PRIVM))
	{
		setuid(sh.userid);
		setgid(sh.groupid);
		if(sh.euserid==0)
		{
			sh.euserid = sh.userid;
			sh.egroupid = sh.groupid;
		}
	}
skip:
	if(trace)
		sh_trace(com,1);
	opt_flags = newflags;
	if(setflag)
	{
		argv--;
		if(np)
			env_arrayset(np,argc,argv);
		else if(argc>1 || minmin)
			arg_set(argv);
	}
	return(argc);
}

/*
 * returns the value of $-
 */

char *arg_dolminus()
{
	register const char *flagc=flagchar;
	register char *flagp=flagadr;
	while(*flagc)
	{
		if(opt_flags&flagval[flagc-flagchar])
			*flagp++ = *flagc;
		flagc++;
	}
	*flagp = 0;
	return(flagadr);
}

/*
 * set up positional parameters 
 */

void arg_set(argi)
char *argi[];
{
	register char **argp=argi;
	register int size = 0; /* count number of bytes needed for strings */
	register char *cp;
	register int 	argn;
	/* count args and number of bytes of arglist */
	while((cp=(char*)*argp++) != ENDARGS)
	{
		size += strlen(cp);
	}
	/* free old ones unless on for loop chain */
	argn = argp - argi;
	arg_free(dolh,0);
	dolh=copyargs(argi, --argn, size);
	st.dolc=argn-1;
}

/*
 * free the argument list if the use count is 1
 * If count is greater than 1 decrement count and return same blk
 * Free the argument list if the use count is 1 and return next blk
 * Delete the blk from the argfor chain
 * If flag is set, then the block dolh is not freed
 */

struct dolnod *arg_free(blk,flag)
struct dolnod *	blk;
{
	register struct dolnod*	argr=blk;
	register struct dolnod*	argblk;
	if(argblk=argr)
	{
		if((--argblk->doluse)==0)
		{
			argr = argblk->dolnxt;
			if(flag && argblk==dolh)
				dolh->doluse = 1;
			else
			{
				/* delete from chain */
				if(argfor == argblk)
					argfor = argblk->dolnxt;
				else
				{
					for(argr=argfor;argr;argr=argr->dolnxt)
						if(argr->dolnxt==argblk)
							break;
					if(argr==0)
					{
						return(NULL);
					}
					argr->dolnxt = argblk->dolnxt;
					argr = argblk->dolnxt;
				}
				free((char*)argblk);
			}
		}
	}
	return(argr);
}

/*
 * grab space for arglist and link argblock for cleanup
 * The strings are copied after the argment vector
 */

static struct dolnod *copyargs(from, n, size)
char *from[];
{
	register struct dolnod *dp=new_of(struct dolnod,n*sizeof(char*)+size+n);
	register char **pp;
	register char *sp;
	dp->doluse=1;	/* use count */
	/* link into chain */
	dp->dolnxt = argfor;
	argfor = dp;
	pp= dp->dolarg;
	st.dolv=pp;
	sp = (char*)dp + sizeof(struct dolnod) + n*sizeof(char*);
	while(n--)
	{
		*pp++ = sp;
		sp = sh_copy(*from++,sp) + 1;
	}
	*pp = ENDARGS;
	return(dp);
}

/*
 *  used to set new argument chain for functions
 */

struct dolnod *arg_new(argi,savargfor)
char *argi[];
struct dolnod **savargfor;
{
	register struct dolnod *olddolh = dolh;
	*savargfor = argfor;
	dolh = NULL;
	argfor = NULL;
	arg_set(argi);
	return(olddolh);
}

/*
 * reset arguments as they were before function
 */

void arg_reset(blk,afor)
struct dolnod *blk;
struct dolnod *afor;
{
	while(argfor=arg_free(argfor,0));
	dolh = blk;
	argfor = afor;
}

void arg_clear()
{
	/* force `for' $* lists to go away */
	while(argfor=arg_free(argfor,1));
	argfor = dolh;
#ifdef DEVFD
	close_pipes();
#endif	/* DEVFD */
}

/*
 * increase the use count so that an arg_set will not make it go away
 */

struct dolnod *arg_use()
{
	register struct dolnod *dh;
	if(dh=dolh)
		dh->doluse++;
	return(dh);
}

/*
 *  Print option settings on standard output
 */

static void print_opts(oflags)
optflag oflags;
{
	register const struct sysnod *syscan = tab_options;
	optflag value;
	p_setout(st.standout);
	p_str((const char *)gettext(e_heading), NL);
	while (value = syscan->sysval)
	{
		value = 1<<value;
		p_str(syscan->sysnam,SP);
		p_nchr(SP,16-strlen(syscan->sysnam));
		if (oflags&value)
			p_str(e_on,NL);
		else
			p_str(e_off,NL);
		syscan++;
	}
}

/*
 *  Write the current option settings to standard output
 *  in a format that is suitable for reinput to the shell
 *  as commands that achieve the same option settings.
 *
 *  Format will appear in a single command line:
 *  set -o <option name> - enabled or +o <option name> - disabled or off
 */

static void print_opts_formatted(oflags)
optflag oflags;
{
	const struct sysnod *syscan = tab_options;
	optflag value;

	p_setout(st.standout);
	p_str(e_set, SP);   /* print "set" */
	while (value = syscan->sysval)
	{
		if ((strcmp(syscan->sysnam, "interactive") != 0) &&
			(strcmp(syscan->sysnam, "restricted") != 0))
		{
			value = 1<<value;
			if (oflags&value)
				p_str(e_option_on, SP); /* print "-o" */
			else
				p_str(e_option_off, SP); /* print "+o" */
			p_str(syscan->sysnam, SP); /* print <option name> */
		}
	syscan++;
	}
	p_nchr(NL, 1);
}

#ifdef DEVFD
static int to_close[15];
static int indx;

void close_pipes()
{
	register int *fd = to_close;
	while(*fd)
	{
		close(*fd);
		*fd++ = -1;
	}
	indx = 0;
}
#endif	/* DEVFD */

#ifdef VPIX
#   define EXTRA 2
#else
#   define EXTRA 1
#endif /* VPIX */

/*
 * build an argument list
 */

char **arg_build(nargs,comptr)
int 	*nargs;
struct comnod	*comptr;
{
	register struct argnod	*argp;
	{
		register struct comnod	*ac = comptr;
		register struct argnod	*schain;
		/* see if the arguments have already been expanded */
		if(ac->comarg==NULL)
		{
			*nargs = 0;
			return(&null);
		}
		else if((ac->comtyp&COMSCAN)==0)
		{
			*nargs = ((struct dolnod*)ac->comarg)->doluse;
			return(((struct dolnod*)ac->comarg)->dolarg+EXTRA);
		}
		schain = st.gchain;
		st.gchain = NULL;
#ifdef DEVFD
		close_pipes();
#endif	/* DEVFD */
		*nargs = 0;
		if(ac)
		{
			argp = ac->comarg;
			while(argp)
			{
				*nargs += arg_expand(argp);
				argp = argp->argnxt.ap;
			}
		}
		argp = st.gchain;
		st.gchain = schain;
	}
	{
		register char	**comargn;
		register int	argn;
		register char	**comargm;
		argn = *nargs;
		argn += EXTRA;	/* allow room to prepend args */
		comargn=(char**)stakalloc((unsigned)(argn+1)*sizeof(char*));
		comargm = comargn += argn;
		*comargn = ENDARGS;
		if(argp==0)
		{
			/* reserve an extra null pointer */
			*--comargn = 0;
			return(comargn);
		}
		while(argp)
		{
			struct argnod *nextarg = argp->argchn;
			argp->argchn = 0;
			*--comargn = argp->argval;
			if((argp->argflag&A_RAW)==0)
				sh_trim(*comargn);
			if((argp=nextarg)==0 || (argp->argflag&A_MAKE))
			{
				if((argn=comargm-comargn)>1)
					gsort(comargn,argn,strcoll);
				comargm = comargn;
			}
		}
		return(comargn);
	}
}

/* Argument expansion */

static int arg_expand(argp)
register struct argnod *argp;
{
	register int count = 0;
	argp->argflag &= ~A_MAKE;
#ifdef DEVFD
	if(*argp->argval==0 && (argp->argflag&A_EXP))
	{
		/* argument of the form (cmd) */
		register struct argnod *ap;
		int pv[2];
		int fd;
		ap = (struct argnod*)stakseek(ARGVAL);
		ap->argnxt.ap = 0;
		ap->argflag = A_MAKE;
		count++;
		stakputs(e_devfd);
		io_popen(pv);
		fd = argp->argflag&A_RAW;
		stakputs(sh_itos(pv[fd]));
		ap = (struct argnod*)stakfreeze(1);
		ap->argchn= st.gchain;
		st.gchain = ap;
		sh.inpipe = sh.outpipe = 0;
		if(fd)
		{
			sh.inpipe = pv;
			sh_exec((union anynode*)argp->argchn,(int)(st.states&ERRFLG));
		}
		else
		{
			sh.outpipe = pv;
			sh_exec((union anynode*)argp->argchn,(int)(st.states&ERRFLG));
		}
#ifdef JOBS
		job.pipeflag++;
#endif	/* JOBS */
		close(pv[1-fd]);
		to_close[indx++] = pv[fd];
	}
	else
#endif	/* DEVFD */
	if((argp->argflag&A_RAW)==0)
	{
		register char *ap = argp->argval;
		if(argp->argflag&A_MAC)
			ap = mac_expand(ap);
		count = split(ap,argp->argflag&(A_SPLIT|A_EXP));
	}
	else
	{
		argp->argchn= st.gchain;
		st.gchain = argp;
		argp->argflag |= A_MAKE;
		count++;
	}
	return(count);
}

static int split(mbs,macflg) /* blank interpretation routine */
char *mbs;
{
	register wchar_t 	c;
	wchar_t *s, *s_save = NULL;
	const wchar_t *seps = NULL;
	const wchar_t *seps_save = NULL;
	int		d;
	register struct argnod *ap;
	int 	count=0;
	int expflag = (!is_option(NOGLOB) && (macflg&A_EXP));

	if(macflg &= A_SPLIT) {
		const char	*mbifs;
		if (mbifs = (const char *)nam_fstrval(IFSNOD))
			seps_save = seps =
				(const wchar_t *)mbstowcs_alloc(mbifs);
	} else
		seps = we_nullstr;
	if(seps==NULL)
		seps = we_sptbnl;
	s_save = s = mbstowcs_alloc_esc((const char *)mbs);
	while(1)
	{
		if(sh.trapnote&SIGSET)
			sh_exit(SIGFAIL);
		ap = (struct argnod*)stakseek(ARGVAL);
		while(c= *s++)
		{
			if(c == ESCAPE)
			{
				c = *s++;
				if(c!='/')
					stakputascii(ESCAPE);
			}
			else if(wcschr(seps,c)) {
				wchar_t d = *s;
				if (!(macflg & A_SPLIT))
					break;
				if (!d ||	/* End of string ? */
					/* 1st char is not white space */
					(wcschr(we_sptbnl,c)==(wchar_t*)NULL) ||
					/* Start IFS white space sequence ? */
					(wcschr(seps, d) == (wchar_t *)NULL))
					break;

				/*
				 * IFS white space sequence
				 * c must be a leading white space.
				 * VSC/sh_05.sh(tp364)
				 */
				while (d=*s++) {
					if (!wcschr(seps, d) ||
							!wcschr(we_sptbnl, d))
						break;
					c = d;
				}
				/*
				 * Leading IFS white spaces sequence
				 * or trailing IFS sequence
				 */
				if (staktell()==ARGVAL) {
					s--;
					continue;
				}
				
				c = d;
				if (!c)
					break;
				if (!wcschr(seps, c))
					s--;
				break;
			}
			stakputwc(c);
		}
	/* This allows contiguous visible delimiters to count as delimiters */
		if(staktell()==ARGVAL)
		{
			if(c==0) {
				if (seps_save)
					xfree((void *)seps_save);
				if (s_save)
					xfree((void *)s_save);
				return(count);
			}
			if(macflg==0 || wcschr(we_sptbnl,c))
				continue;
		}
		else if(c==0)
		{
			s--;
		}
		/* file name generation */
		ap = (struct argnod*)stakfreeze(1);
		ap->argflag &= ~(A_RAW|A_MAKE);
#ifdef BRACEPAT
		if(expflag)
			count += expbrace(ap);
#else
		if(expflag && (d=path_expand(ap->argval)))
			count += d;
#endif /* BRACEPAT */
		else
		{
			count++;
			ap->argchn= st.gchain;
			st.gchain = ap;
		}
		st.gchain->argflag |= A_MAKE;
	}
}
