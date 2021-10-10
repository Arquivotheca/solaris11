/*
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
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
#include	<libintl.h>
#include	"defs.h"
#include	"jobs.h"
#include	"sym.h"
#include	"history.h"
#include	"timeout.h"
#include	"builtins.h"
#ifdef pdp11
#   include	<execargs.h>
#endif	/* pdp11 */
#ifdef TIOCLBIC
#   undef TIOCLBIC
#   ifdef _sys_ioctl_
#	include	<sys/ioctl.h>
#   endif /* _sys_ioctl_ */
#endif /* TIOCLBIC */

/* These routines are referenced by this module */
extern int	gscan_some();

static void	exfile();
static void	pr_prompt();
static void	chkmail();
static int	notstacked();
static void	dfault();
static void	swallow();
static char	*nospace();

extern char	**environ;

static struct fileblk topfile;
static time_t	mailtime;
static char	beenhere = 0;

#ifdef _lib_sigvec
    void clearsigmask(sig)
    register int sig;
    {
	struct sigvec vec;
	if(sigvec(sig,(struct sigvec*)0,&vec)>=0 && vec.sv_mask)
	{
		vec.sv_mask = 0;
		sigvec(sig,&vec,(struct sigvec*)0);
	}
    }
#endif /* _lib_sigvec */

static void
resolve_default_pwd(void)
{
	char	pathptr[PATH_MAX];
	size_t	pathlen;

	if (sh.pwd == NULL) {
		/* ensure the default shell path is physical */
		path_pwd(0);
		if (sh.pwd == NULL) {
			/*
			 * if sh.pwd is still NULL display warning and exit
			 * this function
			 */
			sh_cwarn(e_pwd);
			return;
		}
	}

	/*
	 * sh.pwd never becomes longer than PATH_MAX,
	 * but just in case..
	 */
	(void) strlcpy(pathptr, sh.pwd, sizeof (pathptr));
	(void) path_physical(pathptr);
	nam_offtype(PWDNOD, ~N_FREE);
	nam_free(PWDNOD);
	nam_fputval(PWDNOD, pathptr);
	nam_ontype(PWDNOD, N_FREE | N_EXPORT);
	sh.pwd = PWDNOD->value.namval.cp;
}
	
/* CSI assumption1(ascii) made here. See csi.h. */
int
main(int c, char *v[])
{
	char *sim;
	int 	rsflag;		/* local restricted flag */
	char *command;
	int prof;
#ifdef MTRACE
	extern int Mt_certify;
	Mt_certify = 1;
#endif /* MTRACE */
#ifdef _lib_sigvec
	clearsigmask(SIGALRM);
	clearsigmask(SIGHUP);
	clearsigmask(SIGCHLD);
#endif /* _lib_sigvec */
	(void) sigemptyset(&childmask);
	(void) sigaddset(&childmask, SIGCLD);
	st.standout = 1;
	sh.cpipe[0] = -1;
	sh.userid=getuid();
	sh.euserid=geteuid();
	sh.groupid=getgid();
	sh.egroupid=getegid();
	time(&mailtime);
	stakinstall((Stak_t*)0,nospace);

	/* set message domain for gettext() */
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void)textdomain(TEXT_DOMAIN);

	/* set up memory for name-value pairs */
	nam_init();

	/* set name-value pairs from userenv
	 *  'rsflag' is zero if SHELL variable is
	 * set in environment and contains an'r' in
	 * the basename part of the value.
	 */
	rsflag=env_init();
	/* a shell is also restricted if argv(0) has
	 *  an 'rsh' for its basename
	 */
	sim = path_basename(*v);
	if(*sim=='-')
	{
		sim++;
		sh.login_sh = 2;
		signal(SIGXCPU, SIG_DFL);
		signal(SIGXFSZ, SIG_DFL);
	}
	sig_init();
	/* check for restricted shell */
	if(c>0 && strmatch(sim,"@(rk|kr|r)sh"))
		rsflag=0;

	/* look for options */
	/* st.dolc is $#	*/
	st.dolc=arg_opts(c,v,0);
	st.dolv=v+c-st.dolc--;
	st.dolv[0] = v[0];
	if(st.dolc < 1)
		on_option(STDFLG);
	if(!is_option(STDFLG))
	{
		st.dolc--;
		st.dolv++;
	}
	/* set[ug]id scripts require the -p flag */
	prof = !is_option(PRIVM);
	if(sh.userid!=sh.euserid || sh.groupid!=sh.egroupid)
	{
#ifdef P_SUID
		/* require sh -p to run setuid and/or setgid */
		if(!is_option(PRIVM) && sh.userid >= P_SUID)
		{
			setuid(sh.euserid=sh.userid);
			setgid(sh.egroupid=sh.groupid);
		}
		else
#else
		{
			on_option(PRIVM);
			prof = 0;
		}
#endif /* P_SUID */
#ifdef SHELLMAGIC
		/* careful of #! setuid scripts with name beginning with - */
		if(sh.login_sh && c >=2 && eq(v[0],v[1]))
			sh_fail((const char *)gettext(e_prohibited), NIL);
#endif /*SHELLMAGIC*/
	}
	else
		off_option(PRIVM);
	if(is_option(RSHFLG))
	{
		rsflag = 0;
		off_option(RSHFLG);
	}
	sh.shname = st.dolv[0]; /* shname for $0 in profiles */

	if (rsflag) {
		/* not a restricted shell */
		resolve_default_pwd();
	}
	
	/*
	 * return here for shell file execution
	 * but not for parenthesis subshells
	 */
	st.cmdadr = st.dolv[0]; /* cmdadr is $0 */
	SETJMP(sh.subshell);
	command = st.cmdadr;
	/* set pidname '$$' */
	sh.pid = getpid();
	io_settemp(sh.pid);
	srand(sh.pid&0x7fff);
	sh.ppid = (longlong_t)getppid();
	dfault(PS4NOD,e_traceprompt);
#ifdef apollo
	/*
	 * The following code initializes apollo specific features.
	 * See description below for more details.
	 */
	{
		extern char *getenv();
		/*
		 * initialize the SYSTYPENOD internal data. Use
		 * the current "SYSTYPE" environment variable value
		 * as the initial value.
		 */		
		dfault(SYSTYPENOD,getenv("SYSTYPE"));
		
		/* 
		 * if the current systype is not bsd..., then
		 * set the default path to the sys5.3 default path.
		 * It is setup in this order because the default path
		 * for bsd4.3 is longer than the sys5.3, this order insures that
		 * there is always enough storage space allocated.
		 */
		if (strncmp(SYSTYPENOD->value.namval.cp, "bsd", 3) != 0)
		{
			/* change the default path of bsd4.3 to sys5.3 */
			strcpy(e_defpath,"/bin:/usr/bin:/usr/apollo/bin:");
		}
		/*
		 * setup cd and pwd to use physical instead of
		 * logical path.
		 */
		on_option(PHYSICAL);
	}
#endif	/* apollo */
	path_pwd(1);
	if (beenhere == 0) {
		beenhere = 1;
		st.states |= PROFILE;
		/* decide whether shell is interactive */
#ifdef WEXP
		if (is_option(ONEFLG|CFLAG) == 0 && is_option(STDFLG) &&
				!is_option(WEXP_E) &&
#else
		if (is_option(ONEFLG|CFLAG) == 0 && is_option(STDFLG) &&
#endif
			tty_check(0) && tty_check(ERRIO))
			on_option(INTFLG|BGNICE);
		if(sh.ppid==1)
			sh.login_sh++;
		if(sh.login_sh >= 2)
		{
			/* ? profile */
			sh.login_sh += 2;
			/* Only if interactive shell,grab controlling tty */
			if(is_option(INTFLG))
				job_init(1);
			/*	system profile	*/
			if((input=path_open(e_sysprofile,NULLSTR)) >= 0)
			{
				st.cmdadr = (char*)e_sysprofile;
				exfile();	/* file exists */
				io_fclose(input);
			}
			if(prof &&  (input=path_open(mac_try((char*)e_profile),NULLSTR)) >= 0)
			{
				st.cmdadr = path_basename(e_profile);
				exfile();
				io_fclose(input);
			}
		}
		/* make sure PWD is set up correctly */
		path_pwd(0);
		/* ENV is expanded for an interactive shell */
		if (prof && is_option(INTFLG))
			sim = mac_try(nam_strval(ENVNOD));
		else if (is_option(PRIVM))
			sim = (char*)e_suidprofile;
		else
			sim = NULLSTR;
		if(*sim && (input = path_open(sim,NULLSTR)) >= 0)
		{
			st.cmdadr = sh_heap(sim);
			exfile();
			free(st.cmdadr);
			st.cmdadr = command;
			io_fclose(input);
		}
		st.cmdadr = command;
		st.states &= ~PROFILE;
		/*
		 * Turn on restricted flag if interactive shell or if reading
		 * commands from a string via '-c'
		 */
		if((rsflag==0) && ((is_option(INTFLG)) || (is_option(CFLAG))))
		{
			on_option(RSHFLG);
			nam_ontype(PATHNOD,N_RESTRICT);
			nam_ontype(ENVNOD,N_RESTRICT);
			nam_ontype(SHELLNOD,N_RESTRICT);
		}
		/* open input file if specified */
		st.cmdline = 0;
		st.standin = 0;
		if(sh.comdiv)
		{
		shell_c:
			st.standin = &topfile;
			io_sopen(sh.comdiv);
			input = F_STRING;
		}
		else
		{
			sim = st.cmdadr;
			st.cmdadr = v[0];
			if(is_option(STDFLG))
				input = 0;
			else
			{
				char *sp;
				/* open stream should have been passed into shell */
				if(strmatch(sim,e_devfdNN))
				{
					struct stat statb;
					input = atoi(sim+8);
					if(fstat(input,&statb)<0)
						sh_fail(st.cmdadr,e_open);
					sim = st.cmdadr;
					off_option(EXECPR|READPR);
				}
				else
				{
#ifdef VFORK
					if(strcmp(sim,path_basename(*environ))==0)
						sp = (*environ)+2;
					else
#endif /* VFORK */
					sp = path_absolute(sim);
					if(!sp || (input=open(sp,O_RDONLY))<0)
					{
#ifndef VFORK
						/* for compatibility with bsh */
						if((input=open(sim,O_RDONLY))<0)
#endif /* VFORK */
						{
							if(sp || errno!=ENOENT)
								sh_fail(sim,e_open);
							/* try sh -c 'name "$@"' */
							on_option(CFLAG);
							sh.comdiv = malloc(strlen(sim)+7);
							sim = sh_copy(sim,sh.comdiv);
							if(st.dolc)
								sh_copy(" \"$@\"",sim);
							goto shell_c;
						}
					}
					input = io_movefd(input);
				}
				sh.readscript = v[0];
			}
			st.cmdadr = sim;
			sh.comdiv--;
#ifdef ACCT
			initacct();
			if(input != 0)
				preacct(st.cmdadr);
#endif	/* ACCT */
		}
		if(!is_option(INTFLG))
		{
			/* eliminate local aliases and functions */
			gscan_some(env_nolocal,sh.alias_tree,N_EXPORT,0);
			gscan_some(env_nolocal,sh.fun_tree,N_EXPORT,0);
		}
	}
#ifdef pdp11
	else
		*execargs=(char *) st.dolv;	/* for `ps' cmd */
#endif	/* pdp11 */
	st.states |= is_option(INTFLG);
#ifdef WEXP
	if (is_option(WEXP_E)) {
		char	*ifsptr;
		if ((ifsptr=(char *)getenv((node_names+
			(IFSNOD-sh.bltin_nodes))->nv_name)) != (char *)NULL)
			nam_fputval(IFSNOD,ifsptr);
		else
			nam_fputval(IFSNOD,(char*)e_sptbnl);
	} else
#endif /* WEXP */
	nam_fputval(IFSNOD,(char*)e_sptbnl);
	exfile();
	if(st.states&PROMPT)
	        newline();
	sh_done(0);
	/* NOTREACHED */
}

/* CSI assumption1(ascii) made here. See csi.h. */
static void	exfile()
{
	time_t curtime;
	union anynode *t;
	register struct fileblk *fp = st.standin;
	register int fno = input;
	int maxtry = MAXTRY;
	int execflags;
	int jmpval;
	/* move input */
	if(fno!=F_STRING)
	{
		if(fno > 0)
		{
			fno = input = io_renumber(fno,INIO);
			io_set_ftbl(0, NULL);
		}
		io_init(input=fno,(struct fileblk*)0,NIL);
		st.standin = fp = io_get_ftbl(fno);
		if(is_option(ONEFLG) && !(st.states&PROFILE))
			fp->flag |= IONBF;
	}
	if(st.states&INTFLG)
	{
		dfault(PS1NOD, (sh.euserid?e_stdprompt:e_supprompt));
		fp->flag |= IOEDIT;
		sig_ignore(SIGTERM);
		if(hist_open())
		{
			on_option(FIXFLG);
		}
		if(sh.login_sh<=1)
			job_init(0);
	}
	else
	{
		if(!(st.states&PROFILE))
		{
			on_option(HASHALL);
			off_option(MONITOR);
		}
		st.states &= ~(PROMPT|MONITOR|FIXFLG);
		off_option(FIXFLG);
	}
	jmpval = SETJMP(sh.errshell);
	if(jmpval && (st.states&PROFILE))
	{
		io_fclose(fno);
		st.states &= ~(INTFLG|MONITOR);
		return;
	}
	else if (!is_option(INTFLG) && jmpval == 5)	/* exit */
 	{
 		/* jmpval == 5 for error in special builtin - Exit immediately */
 		st.states &= ~(PROMPT|PROFILE|BUILTIN|FUNCTION|LASTPIPE);
 		sh_exit(sh.exitval);
	}
	/* error return here */
	opt_char = 0;
	opt_index = 1;
	sh.freturn = (jmp_buf*)sh.errshell;
	st.loopcnt = 0;
	ClearPeekn(&st);
	st.linked = 0;
	st.fn_depth = 0;
	sh.trapnote = 0;
	sh.intrap = 0;
	if(fiseof(fp))
		goto eof_or_error;
	/* command loop */
	while(1)
	{
		sh_freeup();
		stakrestore(0, 0);
		exitset();
		st.states &= ~(ERRFLG|READPR|GRACE|MONITOR);
		st.states |= is_option(READPR)|WAITING|ERRFLG;
		/* -eim  flags don't apply to profiles */
		if(st.states&PROFILE)
			st.states &= ~(INTFLG|ERRFLG|MONITOR);
		p_setout(ERRIO);
		if((st.states&PROMPT) && notstacked() && !fiseof(fp))
		{
			register char *mail;
#ifdef JOBS
			st.states |= is_option(MONITOR);
			job_walk(job_list,N_FLAG,(char**)0);
#endif	/* JOBS */
			if((mail=nam_strval(MAILPNOD)) || (mail=nam_strval(MAILNOD)))
			{
				time(&curtime);
				if ((curtime - mailtime) >= sh_mailchk)
				{
					chkmail(mail);
					mailtime = curtime;
				}
			}
			if(hist_ptr)
				hist_eof();
			pr_prompt(mac_try(nam_strval(PS1NOD)));
			/* sets timeout for command entry */
#ifdef TIMEOUT
			if(sh_timeout <= 0 || sh_timeout > TIMEOUT)
				sh_timeout = TIMEOUT;
#endif /* TIMEOUT */
			if(sh_timeout>0)
				alarm((unsigned int)sh_timeout);
			fp->flin = 1;
		}
		{
			wchar_t	c;
			if (c = io_readc())
				io_unreadc(c);
		}
		if(fiseof(fp) || fiserror(fp))
		{
		eof_or_error:
			if((st.states&PROMPT) && notstacked() && fiserror(fp)==0) 
			{
				io_clearerr(fp);
				ClearPeekn(&st);
				if(--maxtry>0 && is_option(NOEOF) &&
					 !fiserror(&io_stdout) && tty_check(fno))
				{
					p_str((const char *)gettext(e_logout),
						NL);
					continue;
				}
				else if(job_close()<0)
					continue;
				hist_close();
			}
			return;
		}
		maxtry = MAXTRY;
		if(sh_timeout>0)
			alarm(0);
		if((st.states&PROMPT) && hist_ptr && HavePeekn(&st))
		{
			wchar_t	c;
			hist_eof();
			p_wchar(c = io_readc());
			io_unreadc(c);
			p_setout(ERRIO);
		}
		st.states |= is_option(FIXFLG);
		st.states &= ~ WAITING;
		st.cmdline = fp->flin;
		job.waitall = job.pipeflag = 0;
		t = sh_parse(NL,MTFLG);
		sh.readscript = 0;
		if((st.states&PROMPT) && hist_ptr)
		{
			if(t==0 && nextchar(st.standin)=='#')
				swallow(st.standin);
			hist_flush();
		}
		if(t)
		{
			execflags = (ERRFLG|MONITOR|PROMPT);
			/* sh -c simple-command may not have to fork */
			if(!(st.states&PROFILE) && is_option(CFLAG) && nextchar(fp)==0)
				execflags |= 1;
			st.execbrk = 0;
			p_setout(ERRIO);
			sh_exec(t,execflags);
			/* This is for sh -t */
			if(is_option(ONEFLG))
			{
				io_unreadc((wchar_t)NL);
				fp->flag |= IOEOF;
			}
		}
	}
}

/*
 * if there is pending input, the prompt is not printed.
 * prints PS2 if flag is zero otherwise PS3
 */

void	sh_prompt(flag)
register int flag;
{
	register char *cp;
	register struct fileblk *fp;
	if(flag || (st.states&PROMPT) && notstacked())
	{
		if(flag)
			fp = io_get_ftbl(0);
		else
			fp = st.standin;
		/* if characters are in input buffer don't issue prompt */
		if(fp && finbuff(fp))
			return;
		if(cp = nam_fstrval(flag?PS3NOD:PS2NOD))
		{
			flag = output;
			p_setout(ERRIO);
			p_str(cp,0);
			p_setout(flag);
		}
	}
}



/* prints out messages if files in list have been modified since last call */
static void chkmail(files)
char *files;
{
	register char *cp,*qp;
	char *sp;
	register char save;
	struct stat	statb;
	if(*(cp=files) == 0)
		return;
	sp = cp;
	do
	{
		/* skip to : or end of string saving first '?' */
		for(qp=0;*sp && *sp != ':'; mb_nextc((const char **)&sp)) {
			if((*sp == '?' || *sp=='%') && qp == 0)
				qp = sp;
		}
		save = *sp;
		*sp = 0;
		/* change '?' to end-of-string */
		if(qp)
			*qp = 0;
		st.gchain = NULL;
		do
		{
			/* see if time has been modified since last checked
			 * and the access time <= the modification time
			 */
			if(stat(cp,&statb) >= 0 && statb.st_mtime >= mailtime
				&& statb.st_atime <= statb.st_mtime)
			{
				/* check for directory */
				if(st.gchain==NULL && S_ISDIR(statb.st_mode)) 
				{
					/* generate list of directory entries */
					f_complete(cp,"/*");
				}
				else
				{
					/*
					 * If the size is zero
					 * then don't print anything
					 */
					if(statb.st_size != 0)
					{
						/* save and restore $_ */
						char *save = sh.lastarg;
						sh.lastarg = cp;
						p_str(mac_try(qp==0?
						(char*)gettext(e_mailmsg):
						qp+1),NL);
						sh.lastarg = save;
					}
					break;
				}
			}
			if(st.gchain)
			{
				cp = st.gchain->argval;
				st.gchain = st.gchain->argchn;
			}
			else
				cp = NULL;
		}
		while(cp);
		if(qp)
			*qp = '?';
		*sp++ = save;
		cp = sp;
	}
	while(save);
	stakrestore(0, 0);
}

/*
 * print the primary prompt
 */

/* CSI assumption1(ascii) made here. See csi.h. */
static void pr_prompt(str)
char *str;
{
	register wchar_t c;
	static short cmdno;
#ifdef TIOCLBIC
	int mode;
	mode = LFLUSHO;
	ioctl(ERRIO,TIOCLBIC,&mode);
#endif	/* TIOCLBIC */
	p_flush();
	p_setout(ERRIO);
	for(; c= mb_peekc((const char *)str); mb_nextc((const char **)&str))
	{
		if (c==FC_CHAR)
		{
			/* look at next character */
			c = mb_peekc(++str);
			/* print out line number if not !! */
			if (c!= FC_CHAR)
				p_num(hist_ptr?hist_ptr->fixind:++cmdno,0);
			if (c==0)
				break;
		}
		p_wchar(c);
	}
#if VSH || ESH
	*(io_get_ftbl(ERRIO))->ptr = 0;
	/* prompt flushed later */
#else
	p_flush();
#endif
}

static void dfault(np,value)
register struct namnod *np;
char *value;
{
	if(isnull(np))
		nam_fputval(np,value);
}

static int
notstacked(void)
{
	struct fileblk *fp;
	/* clean up any completed aliases */
	while((fp=st.standin)->ftype==F_ISALIAS && fp->flast==0)
		io_pop(1);
	return(fp->fstak==0);
}

/*
 * swallow up additional comment lines and put them into the history file
 */

/* CSI assumption1(ascii) made here. See csi.h. */
static void swallow(fp)
struct fileblk *fp;
{
	register char *sp = fp->ptr;
	char *ep;
	p_setout(hist_ptr->fixfd);
	for(ep=sp; *ep; mb_nextc((const char **)&ep))
	{
		if(*ep==NL)
		{
			*ep = 0;
			p_str(sp,NL);
			*ep++ = NL;
			if(*(sp=ep)!='#')
				break;
		}
	}
	fp->ptr = sp;
}

static char *nospace()
{
	sh_fail((const char *)gettext(e_space), NIL);
	return((char*)0);
}
