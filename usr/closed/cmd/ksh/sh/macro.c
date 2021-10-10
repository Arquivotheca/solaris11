/*
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * UNIX shell
 *
 * S. R. Bourne
 * AT&T Bell Laboratories
 * Rewritten by David Korn
 *
 */

#include	<stdio.h>
#include	"defs.h"
#include	"sym.h"
#include	"builtins.h"
#include	"name.h"


void	mac_check();

/* These external routines are referenced by this module */
extern char		*ltos();
extern char		*lltos();
extern void		match_paren(int, int, int);
extern wchar_t		*submatch();

static void	copyto();
static int	substring();
static void	skipto();
static wchar_t	getch();
static int	comsubst();
static void	mac_error();
static void	mac_copy();
#ifdef POSIX
    static void	tilde_expand(int, int);
#endif /* POSIX */

static char	quote;	/* used locally */
static char	quoted;	/* used locally */
static char	mflag;	/* 0 for $x, 1 for here docs */
static const wchar_t *ifs;
static int	w_fd = -1;
static int mactry;
static char *mac_current;
static jmp_buf mac_buf;
static wchar_t	idb[2];
#ifdef FLOAT
    extern char		*etos(),*ftos();
    static double numb;
#else
    static longlong_t numb;
#endif /* FLOAT */

static void copyto(endch,newquote)
register char	endch;
{
	register wchar_t	c;
	register int count = 1;
	int saveq = quote;
#ifdef POSIX
	register int tilde = -1;
#endif /* POSIX */

	quote = newquote;
#ifdef POSIX
	/* check for tilde expansion */
	c = io_readc();
	if(c=='~' && !mflag && !quote)
		tilde = staktell();
	io_unreadc(c);
#endif /* POSIX */
	while (c=getch(endch))
	{
		if((c==endch) && (saveq || !quote) && --count<=0)
			break;
		if(quote || c==ESCAPE)
		{
			if(c==ESCAPE)
			{
				c = io_readc();
				if(quote && !wescchar(c) && c!= '"')
				{
					wstakputascii(ESCAPE);
					wstakputascii(ESCAPE);
				}
			}
			if(!mflag || !wescchar(c))
				wstakputascii(ESCAPE);
		}
		wstakputwc(c);
		if(c=='[' && endch==']')
			count++;
#ifdef POSIX
		else if(c=='/' && tilde>=0)
		{
			tilde_expand(tilde,'/');
			tilde = -1;
		}
#endif /* POSIX */
	}
#ifdef POSIX
	if(tilde>=0)
		tilde_expand(tilde,0);
#endif /* POSIX */
	quote = saveq;
	if(c!=endch)
		mac_error();
}

#ifdef POSIX
/*
 * <offset> is byte offset for beginning of tilde string
 * if <c> is non-zero, append <c> to expansion
 */

static void
tilde_expand(int offset, int c)
{
	extern wchar_t *sh_tilde_wcs();
	register wchar_t *cp;
	int curoff = staktell();
	wstakputascii(0);
	if(cp = sh_tilde_wcs((wchar_t *)stakptr(offset)))
	{
		register struct namnod	*n = (struct namnod *)NULL;
		register wchar_t *v;
		char	*mbs;
		wchar_t	*v_save = NULL;

		/* XPG4: ${P:-W} */
		if ((*cp == '$')) {
			n = nam_search_wcs(cp+1, sh.var_tree, 0);
			if (!((mbs = nam_strval(n)) &&
				(v_save = v = mbstowcs_alloc(mbs))))
				v = cp;
		} else
			v = cp;

		stakseek(offset);
		mac_copy(v, -1);
		xfree((void *)v_save);

		if (c)
			wstakputascii(c);
	}
	else
		stakseek(curoff);
}
#endif /* POSIX */

/* skip chars up to } */

static void skipto(endch)
register char endch;
{
	wchar_t	c;
	while ((c=io_readc()) && c!=endch)
	{
		switch(c)
		{
			case ESCAPE:
				io_readc();
				break;

			case SQUOTE:	case DQUOTE:
				skipto((char)c);
				break;

			case DOLLAR:
				if ((c=io_readc()) == LBRACE)
					skipto(RBRACE);
				else if(!wdolchar(c))
					io_unreadc(c);
		}
	}
	if(c!=endch)
		mac_error();
}

static wchar_t getch(endch)
int	endch;
{
	register wchar_t	c;
	int		i;
	register int	bra; /* {...} bra =1, {#...} bra=2 */
	int atflag=0;  /* set if $@ or ${array[@]} within double quotes */
retry:
	c = io_readc();
	if(c==DOLLAR)
	{
		register char *v;
		register wchar_t *argp;
		register struct namnod	*n=(struct namnod*)NULL;
		int 	dolg=0;
		int dolmax = st.dolc+1;
		int 	nulflg;
		wchar_t *id=idb;
		wchar_t	*id_save = NULL;
		int offset;
		int	vsize = -1;
		bra = 0;
		*id = 0;
	retry1:
		c = io_readc();

		switch(c)
		{
			case DOLLAR:
				v=sh_itos(sh.pid);
				break;

			case '!':
				if(sh.bckpid)
				{
					v=sh_itos(sh.bckpid);
					/*
					 * pid is requested through $!. So
					 * mark the job as it will save the
					 * exit status.
					 */
					job_mark_save(sh.bckpid);
				}
				else
					v = "";
				break;

			case LBRACE:
				if(bra++ ==0)
					goto retry1;

			case LPAREN:
				if(bra==0 && mactry==0)
				{
				/* 1 means $() style; 0 is for `` style */
					if(comsubst(1))
						goto retry;
#ifdef FLOAT
					if((long)numb==numb)
						v = ltos((long)numb,10);
					else
					{
						double abnumb = numb;
						char *cp;
						if(abnumb < 0)
							abnumb = -abnumb;
						if(abnumb <1e10 && abnumb>1e-10)
						{
							v = ftos(numb,12);
							cp = v + strlen(v);
							/* eliminate trailing zeros */
							while(cp>v && *--cp=='0')
								*cp = 0;
						}
						else
							v = etos(numb,12);
					}
#else
					v = lltos(numb,10);
#endif /* FLOAT */
				}
				else
					goto nosub;
				break;

			case RBRACE:
				if(bra!=2)
					goto nosub;
				bra = 0;
			case '#':
				if(bra ==1)
				{
					bra++;
					goto retry1;
				}
				v=sh_itos(st.dolc);
				break;

			case '?':
				v=sh_itos(sh.savexit&EXITMASK);
				break;

			case '-':
				v=arg_dolminus();
				break;
			
			default:
				if(sh_iswalpha(c))
				{
					offset = staktell();
					while(sh_iswalnum(c))
					{
						wstakputwc(c);
						c = io_readc();
					}
					while (c=='[' && bra)
					{
						wstakputascii('[');
						copyto(']',0);
						*id = *(wchar_t *)(stakptr(
						staktell()-sizeof(wchar_t)));
						wstakputascii(']');
						c = io_readc();
					}
					io_unreadc(c);
					wstakputascii(0);
					n=env_namset_wcs(
						(wchar_t *)stakptr(offset),
						sh.var_tree,P_FLAG);
					stakseek(offset);
					v = nam_strval(n);
					i = (bra==2 &&
						((c = *id), wastchar(c)));
					if(nam_istype(n,N_ARRAY))
					{
						if(i || (array_next(n) && v))
							dolg = -1;
						else
							dolg = 0;
					}
					else
					{
						if(i)
							dolmax = 0;
						xfree(id_save);
						id_save = id =
						mbstowcs_alloc(n->namid);
					}
					goto cont1;
				}
				if (iswascii(c)) {
					*id = c;
					i = (int)*id;
				} else
					goto nosub;
				if (wastchar(c))
				{
					dolg=1;
					i=1;
				}
				else if(sh_iswdigit(c))
				{
					i -= '0';
					if(bra)
					{
						wchar_t d;
						int j;
						while(d=io_readc(),
							sh_iswdigit(d)) {
							j = (int)d;
							i = 10*i + (j-'0');
						}
						io_unreadc(d);
					}
				}
				else
					goto nosub;
				if(i==0)
				{
					if((st.states&PROFILE) && !(st.states&FUNCTION))
						v = sh.shname;
					else
						v = st.cmdadr;
				}
				else if(i <= st.dolc)
					v = st.dolv[i];
				else
					dolg = 0, v = 0;
			}
	cont1:
		c = io_readc();
		if(bra==2)
		{
			if(c!=RBRACE)
				mac_error();
			if(dolg==0 && dolmax)
				i = (v?mbschars(v):0);
			else if(dolg>0)
				i = st.dolc;
			else if(dolg<0)
				i = array_elem(n);
			else
				i = (v!=0);
			v = sh_itos(i);
			dolg = 0;
			c = RBRACE;
		}
		/* check for quotes @ */
		if(idb[0]=='@' && quote && !atflag)
		{
			quoted--;
			atflag = 1;
		}
		if(c==':' && bra)	/* null and unset fix */
		{
			nulflg=1;
			c=io_readc();
		}
		else
			nulflg=0;
		if(!wdefchar(c) && bra)
			mac_error();
		argp = 0;
		if(bra)
		{
			if(c!=RBRACE)
			{
				offset = staktell();
				if(((v==0 || (nulflg && *v==0)) ^ (wsetchar(c)!=0))
					|| is_option(NOEXEC))
				{
					int newquote = quote;
					if (c=='#' || c == '%')
						newquote = 0;
					copyto(RBRACE,newquote);
					/* add null byte */
					wstakputascii(0);
					stakseek(staktell()-sizeof(wchar_t));
				}
				else
					skipto(RBRACE);
				argp=(wchar_t *)stakptr(offset);
			}
		}
		else
		{
			io_unreadc(c);
			c=0;
		}
		/* check for substring operations */
		if(c == '#' || c == '%')
		{
			if(dolg != 0)
				mac_error();
			if(v && *v)
			{
				bra = 0;
				if(*argp==c)
				{
					bra++;
					argp++;
				}
				if(c=='#')
				{
 					wchar_t *wv, *wv_save;
 					if ((wv_save = wv = mbstowcs_alloc(v))
 								!= NULL) {
 						wv = submatch(wv,argp,bra);
 						if (wv) {
 							*wv = L'\0';
 		/* After placing terminator at wv, wv_save points */
 		/* wchar_t string of matched prefix part. */
 		/* Advance pointer v to point end of prefix part on */
 		/* multibyte string. */
 							v += wcsbytes(wv_save);
 						}
 						xfree(wv_save);
 					}
 				} else {
 					wchar_t *wv;
 					if (wv = mbstowcs_alloc(v)) {
 						vsize = substring(wv,argp,bra);
 							xfree(wv);
 					} else {
 						vsize = 0;
 					}
 				}
			}
			if(v)
				stakseek(offset);
		}
	retry2:
		if(v && (!nulflg || *v ) && c!='+')
		{
			int type = (int)*id; /* only zero/nonzero has meaning */
			wchar_t	sep;
			if(*ifs)
				sep = *ifs;
			else
				sep = SP;
			while(1)
			{
				/* quoted null strings have to be marked */
				if(*v==0 && quote)
				{
					wstakputascii(ESCAPE);
					wstakputascii(0);
				}
				{
					wchar_t *xv;
					if (xv = mbstowcs_alloc(v)) {
						mac_copy(xv, vsize);
						xfree(xv);
					}
				}
				if(dolg==0)
					 break;
				if(dolg>0)
				{
					if(++dolg >= dolmax)
						break;
					v = st.dolv[dolg];
				}
				else
				{
					if(type == 0)
						break;
					v = nam_strval(n);
					type = array_next(n);
				}
				if(quote && *id=='*')
				{
					if(*ifs==0)
						continue;
					wstakputascii(ESCAPE);
				}
				wstakputwc(sep);
			}
		}
		else if(argp)
		{
			if(c=='?' && !is_option(NOEXEC))
			{
				char *mbs = NULL;

				sh_trim_wcs(argp);
				sh_fail_wcs(id, *argp ? 
					mbs = wcstombs_alloc(argp):
						e_nullset);
				xfree((void *)id_save);
				xfree((void *)mbs);
			}
			else if(c=='=')
			{
				if(n)
				{
					sh_trim_wcs(argp);
					nam_putval_wcs(n,argp);
					v = nam_strval(n);
					nulflg = 0;
					stakseek(offset);
					goto retry2;
				}
				else
					mac_error();
			}
		}
		else if(is_option(NOSET))
#ifdef WEXP
		{
			if (is_option(WEXP_E))
				cmd_shfail_wcs(id, NIL, WEXP_BADVAL);
			else
				sh_fail_wcs(id,e_notset);
		}
#else
			sh_fail(id,e_notset);
#endif /* WEXP */
		xfree((void *)id_save);
		goto retry;
	}
	else if(c==endch) {
		return(c);
	}
	else if(c==SQUOTE && mactry==0)
	{
		comsubst(0);
		goto retry;
	}
	else if(c==DQUOTE && !mflag)
	{
		if(quote ==0)
		{
			atflag = 0;
			quoted++;
		}
		quote ^= 1;
		goto retry;
	}

	return (c);

nosub:
	if(bra)
		mac_error();
	io_unreadc(c);
	return((wchar_t)DOLLAR);
}

	/* Strip "" and do $ substitution
	 * Leaves result on top of stack
	 */
char *mac_expand(as)
char *as;
{
	register int	savqu =quoted;
	register int	savq = quote;
	const wchar_t	*ifs_save = NULL;
	struct peekn	savpeekn = st.peekn;
	struct fileblk	cb;
	mac_current = as;
	ClearPeekn(&st);
	io_push(&cb);
	io_sopen(as);
	stakseek(0);
	mflag = 0;
	quote=0;
	quoted=0;
	{
		char	*mbifs;
		if(!(mbifs = nam_fstrval(IFSNOD)) ||
			!(ifs_save = ifs =
				(const wchar_t *)mbstowcs_alloc(mbifs)))
			ifs = we_sptbnl;
	}
	copyto(0,0);
	if (ifs_save) {
		xfree((void *)ifs_save);
		ifs_save = NULL;
		ifs = we_sptbnl;
	}
	io_pop(1);
	st.peekn = savpeekn;
	if(quoted && staktell()==0)
	{
		wstakputascii(ESCAPE);
		wstakputascii(0);
	}
	/* above is the fix for *'.c' bug	*/
	quote=savq;
	quoted=savqu;
	wstakputascii(0);
	wcstombs_esc(stakptr(0), (wchar_t *)stakptr(0),
		wcslen_esc((wchar_t *)stakptr(0)) * sizeof(wchar_t));
	return((char *)stakfreeze(1));
}

/*
 * command substitution
 * type==0 for ``
 * type==1 for $()
*/

static int comsubst(type)
int type;
{
	struct fileblk	cb;
	register int	fd;
	wchar_t	d;
	register int	e;
	register union anynode *t;
	register char *argc;
	char	*mbs;
	struct ionod *saviotemp = st.iotemp;
	struct slnod *saveslp = st.staklist;
	int savem = mflag;
	int savtop = staktell();
	off_t savptr = staksave();
	char inbuff[IOBSIZE+1];
	int saveflags = (st.states&FIXFLG);
	int stacksize = 0;
	register int waitflag = 0;

	(void) stakfreeze(0);
#ifdef WEXP
	if (is_option(WEXP_N))
		cmd_shcfail(NIL, WEXP_CMDSUB);
#endif /* WEXP */
	if(type)
	{
		type = ((d=io_readc())==LPAREN);
		if(type || d==RPAREN)
			wstakputwc(d);
		else
			io_unreadc(d);
		if(d != RPAREN) {
			if (d == LPAREN)
				match_paren(LPAREN, RPAREN, UseWStak);
			else
				match_paren_parser(RPAREN, UseWStak);
		}
		if(type && (d=io_readc())==RPAREN)
		{
			stakseek(staktell()-sizeof(wchar_t));
			wstakputascii(0);
			sh_wcstombs(stakptr(0), (wchar_t *)stakptr(0),
				wcslen((wchar_t *)stakptr(0))*sizeof(wchar_t));
			argc = (char *)stakfreeze(1);
			numb = sh_arith_expr(mac_trim(argc + 1, 0));
			stakrestore(savptr, savtop);
			mflag = savem;
			return(0);
		}
		else if(type)
		{
			/* nested command substitution, keep reading */
			wstakputwc(d);
			match_paren(LPAREN, RPAREN, UseWStak);
		}
		stakseek(staktell()-sizeof(wchar_t));
	}
	else
	{
		while((d=io_readc())!=SQUOTE && d)
		{
			if(d==ESCAPE)
			{
				d = io_readc();
				/*
				 * This is wrong but it preserves compatibility with
				 * the SVR2 shell
				 */
				if(!(wescchar(d) || (d=='"' && quote)))
					wstakputascii(ESCAPE);
			}
			wstakputwc(d);
		}
	}
	wstakputwc(0);
	sh_wcstombs(stakptr(0), (wchar_t *)stakptr(0),
		wcslen((wchar_t *)stakptr(0)) * sizeof(wchar_t));
	mbs=(char *)stakfreeze(1);
	st.states &= ~FIXFLG;	/* do not save command subs in history file */
	if(w_fd>=0)
	{
		p_setout(w_fd);
		p_flush();	/* flush before executing command */
	}
	io_push(&cb);
	io_sopen(mbs);
	sh.nested_sub = 0;
	st.exec_flag++;
	t = sh_parse(EOFSYM,MTFLG|NLFLG);
	st.exec_flag--;
	if(!t || is_option(NOEXEC))
		goto readit;
	if(!sh.nested_sub && !t->tre.treio && is_rbuiltin(t))
	{
		/* nested command subs not handled specially */
		/* handle command substitution of most builtins separately */
		/* exec, login, cd, ., eval and shift not handled this way */
		/* put output into tmpfile */
		int save1_out = st.standout;
		if((st.states&IS_TMP)==0)
		{
			char tmp_fname[TMPSIZ];
			/* create and keep open a /tmp file for command subs */
			fd = io_mktmp(tmp_fname);
			fd = io_renumber(fd,TMPIO);
			st.states |= IS_TMP;
			/* root cannot unlink because fsck could give bad ref count */
			if(sh.userid || !is_option(INTFLG))
				unlink(tmp_fname);
			else
				st.states |= RM_TMP;
		}
		else
			fd = TMPIO;
		st.standout = fd;
		/* this will only flush the buffer if output is fd already */
		p_setout(fd);
		/* 1192528: Clear the flag to flush output buffer */
		(io_get_ftbl(fd))->flag &= ~IOFLUS;
		st.subflag++;
		sh_funct(t,(char**)0,(int)(st.states&ERRFLG),(struct argnod*)0);
		st.subflag = 0;
		p_setout(fd);
		p_char(0);
		if(((io_get_ftbl(fd))->flag & IOFLUS) || is_option(EXECPR))
		{
			/* file is larger than buffer, read from it */
			p_flush();
			io_seek(fd,(off_t)0,SEEK_SET);
			io_init(input=fd,st.standin,inbuff);
			waitflag = -1;
		}
		else
		{
			/* The file is all in the buffer */
			strcpy(inbuff,_sobuf);
			io_sopen(inbuff);
			(io_get_ftbl(fd))->ptr = (io_get_ftbl(fd))->base;
		}
		st.standout = save1_out;
		goto readit;
	}
	else if(t->tre.tretyp==0 && t->com.comarg==0)
	{
		jmp_buf		retbuf;
		jmp_buf		*savreturn = sh.freturn;
		unsigned	save_states = st.states;
		struct fileblk	*savio = sh.savio;
		int		jmpval;
		sh.freturn = (jmp_buf*)retbuf;
		st.states |= BUILTIN;
		sh.savio = st.standin;
		jmpval = SETJMP(retbuf);
		if(t->tre.treio && !(((t->tre.treio)->iofile)&IOUFD) && jmpval==0)
		{
			struct stat	statb;
			fd = io_redirect(t->tre.treio,3);
			if(fstat(fd,&statb)>=0)
				stacksize = 4*statb.st_size;
		}
		else
			fd = io_safe_fopen((char*)e_devnull);
		st.states = save_states;
		sh.freturn = savreturn;
		sh.savio = savio;
	}
	else
	{
		int 	pv[2];
		int forkflag = FPOU|FCOMSUB;
		waitflag++;
		if(st.iotemp!=saviotemp)
			forkflag |= FTMP;
		t = sh_mkfork(forkflag,t);
		  /* this is done like this so that the pipe
		   * is open only when needed
		   */
		io_popen(pv);
		sh.inpipe = 0;
		sh.outpipe = pv;
		sh_exec(t, (int)(st.states&ERRFLG));
		fd = pv[INPIPE];
		io_fclose(pv[OTPIPE]);
	}
	io_init(input=fd,st.standin,inbuff);

readit:
	sh_freeup();
	st.iotemp = saviotemp;
	st.staklist = saveslp;
	mflag = savem;
	stakrestore(savptr, savtop);
	if(stacksize)
	{
		/* cause preallocation of stack frame */
		stakseek(savtop+stacksize);
		stakseek(savtop);
	}
	mac_copy((wchar_t*)0,-1);
	if(waitflag>0)
		job_wait(sh.subpid);
	e = staktell();
	while(e>savtop)
	{
		if(*(wchar_t *)stakptr(e -= sizeof(wchar_t)) != NL)
		{
			e += sizeof(wchar_t);
			break;
		}
		else if(quote)
			e -= sizeof(wchar_t);
	}
	stakseek(e);
	io_pop(waitflag>=0?0:1);
	st.states |= saveflags;
	if(w_fd >=0)
		p_setout(w_fd);
	return(1);
}


/*
 * Copy and expand a here-document
 */

int
mac_here(int in)
{
	register wchar_t	c;
	register int	ot;
	struct fileblk 	fb;
	char inbuff[IOBSIZE+1];
	quote = 0;
	ifs = we_nullstr;
	mflag = 1;
	ot = io_mktmp(inbuff);
	unlink(inbuff);
	w_fd = ot;
	io_push(&fb);
	io_init(in,&fb,inbuff);
	p_setout(ot);
	stakseek(0);
	if(is_option(EXECPR))
		sh.heretrace=1;
	while(1)
	{
		c=getch(0);
		if(c==ESCAPE)
		{
			c = io_readc();
			if(!wescchar(c))
				wstakputascii(ESCAPE);
		}
		if(staktell())
		{
			*(wchar_t *)stakptr(staktell()) = 0;
			p_wcs((wchar_t *)stakptr(0),c);
			stakseek(0);
		}
		else if (c)
			p_wchar(c);
		if(c==0)
			break;
	}
	p_flush();
	sh.heretrace=0;
	mflag = 0;
	io_pop(0);
	w_fd = -1;
	io_set_ftbl(ot, NULL);
	lseek(ot,(off_t)0,SEEK_SET);
	return(ot);
}


/*
 * copy value of string or file onto the stack inserting backslashes
 * as needed to prevent word splitting and file expansion
 */

static void mac_copy(str,size)
register wchar_t *str;
register int size;
{
	register wchar_t c;
	while(size!=0 && (c = (str?*str++:io_readc())))
	{
		/*@ assert (!mflag&&quote)==0; @*/
		if(quote || (!mflag && waddescape(c) &&
				(c==ESCAPE || !wcschr(ifs, c))))
	 		wstakputascii(ESCAPE); 
 		wstakputwc(c); 
		if(size>0)
			size--;
	}
}
 
/*
 * Deletes the right substring of STRING using the expression PAT
 * the longest substring is deleted when FLAG is set.
 */

static int substring(string,pat,flag)
register wchar_t *string;
wchar_t *pat;
int flag;
{
	register wchar_t *sp = string;
	register int size;
	sp += wcslen(sp);
	size = sp-string;
	while(sp>=string)
	{
		if(wstrmatch(sp,pat))
		{
			size = sp-string;
			if(flag==0)
				break;
		}
		sp--;
	}
	return(size);
}


/*
 * do parameter and command substitution and strip of quotes
 * attempt file name expansion if <type> not zero
 */

char *mac_trim(s,type)
char *s;
{
	register char *t=mac_expand(s);
	struct argnod *schain = st.gchain;
	if(type && f_complete(t,e_nullstr)==1)
		t = st.gchain->argval;
	st.gchain = schain;
	sh_trim(t);
	return(t);
}

/*
 * perform only parameter substitution and catch failures
 */

char *mac_try(s)
register char *s;
{
	if(s)
	{
		struct peekn savec = st.peekn;
		mactry++;
		if(SETJMP(mac_buf)==0)
			s = mac_trim(s,0);
		else
		{
			io_pop(1);
			st.peekn = savec;
		}
		mactry = 0;
	}
	if(s==0)
		return(NULLSTR);
	return(s);
}

static void mac_error()
{
	sh_fail(mac_current,e_subst);
}

/*
 * check to see if the error occured while expanding prompt
 */

void mac_check()
{
	if(mactry)
		LONGJMP(mac_buf,1);
}
