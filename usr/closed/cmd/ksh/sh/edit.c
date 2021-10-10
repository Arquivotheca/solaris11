/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 *  edit.c - common routines for vi and emacs one line editors in shell
 *
 *   David Korn				P.D. Sullivan
 *   AT&T Bell Laboratories		AT&T Bell Laboratories
 *   Room 3C-526B			Room 1B-286
 *   Murray Hill, N. J. 07974		Columbus, OH 43213
 *   Tel. x7975				Tel. x 2655
 *
 *   Coded April 1983.
 */

#include <errno.h>
#include <string.h>

#ifdef KSHELL
#include "defs.h"
#include "terminal.h"
#include "builtins.h"
#include "sym.h"
#else
#include "io.h"
#include "terminal.h"
#undef SIG_NORESTART
#define	SIG_NORESTART	1
#define	_sobuf	ed_errbuf
	extern char ed_errbuf[];
	const char e_version[] = "\n@(#)Editlib version 11/16/88i\0\n";
#endif	/* KSHELL */
#include "history.h"
#include "edit.h"

#define	BAD	-1
#define	GOOD	0
#define	SYSERR	-1

#ifdef OLDTERMIO
#undef tcgetattr
#undef tcsetattr
#endif /* OLDTERMIO */

#ifdef RT
#define	VENIX	1
#endif	/* RT */

#define	lookahead	editb.e_index
#define	env		editb.e_env
#define	previous	editb.e_lbuf
#define	filedes		editb.e_fd
#define	in_raw		editb.e_addnl


#ifdef _sgtty_
#ifdef TIOCGETP
	static int l_mask;
	static struct tchars l_ttychars;
	static struct ltchars l_chars;
	static  char  l_changed;	/* set if mode bits changed */
#define	L_CHARS	4
#define	T_CHARS	2
#define	L_MASK	1
#endif /* TIOCGETP */
#endif /* _sgtty_ */

#ifndef IODELAY
#undef _SELECT5_
#endif /* IODELAY */
#ifdef _SELECT5_
#ifndef included_sys_time_
#include <sys/time.h>
#endif /* included_sys_time_ */
	static int delay;
#ifndef KSHELL
	int tty_speeds[] = {0, 50, 75, 110, 134, 150, 200, 300,
	    600, 1200, 1800, 2400, 9600, 19200, 0};
#endif	/* KSHELL */
#endif /* _SELECT5_ */

#ifdef KSHELL
	extern wchar_t *sh_tilde_wcs(wchar_t *);
	static char macro[] = "_??";
#define	slowsig()	(sh.trapnote & SIGSLOW)
#else
	struct edit editb;
	extern int errno;
#define	slowsig()	(0)
#endif	/* KSHELL */


static struct termios savetty;
static int savefd = -1;
#ifdef future
static int compare();
#endif
#if VSH || ESH
	extern char *strrchr();
	extern char *strcpy();
	static struct termios ttyparm;	/* initial tty parameters */
	static struct termios nttyparm;	/* raw tty parameters */
	static char bellchr[] = "\7";	/* bell char */
#define	tenex	1
#ifdef tenex
	static genchar *overlay();
#endif /* tenex */
#endif /* VSH || ESH */

static int	wctovwcs(wchar_t *, wchar_t **, wchar_t);

/*
 * This routine returns true if fd refers to a terminal
 * This should be equivalent to isatty
 */

int
tty_check(fd)
{
	savefd = -1;
	return (tty_get(fd, (struct termios *)0) == 0);
}

/*
 * Get the current terminal attributes
 * This routine remembers the attributes and just returns them if it
 * is called again without an intervening tty_set()
 */

int
tty_get(fd, tty)
struct termios *tty;
{
	if (fd != savefd) {
#ifndef SIG_NORESTART
		void (*savint)() = st.intfn;
		st.intfn = 0;
#endif	/* SIG_NORESTART */
		while (tcgetattr(fd, &savetty) == SYSERR) {
			if (errno != EINTR) {
#ifndef SIG_NORESTART
				st.intfn = savint;
#endif	/* SIG_NORESTART */
				return (SYSERR);
			}
			errno = 0;
		}
#ifndef SIG_NORESTART
		st.intfn = savint;
#endif	/* SIG_NORESTART */
		savefd = fd;
	}
	if (tty)
		*tty = savetty;
	return (0);
}

/*
 * Set the terminal attributes
 * If fd<0, then current attributes are invalidated
 */

/* VARARGS 2 */
int
tty_set(fd, action, tty)
struct termios *tty;
{
	if (fd >= 0) {
#ifndef SIG_NORESTART
		void (*savint)() = st.intfn;
#endif	/* SIG_NORESTART */
#ifdef future
		if (savefd >= 0 && compare(&savetty, tty,
		    sizeof (struct termios)))
			return (0);
#endif
#ifndef SIG_NORESTART
		st.intfn = 0;
#endif	/* SIG_NORESTART */
		while (tcsetattr(fd, action, tty) == SYSERR) {
			if (errno != EINTR) {
#ifndef SIG_NORESTART
				st.intfn = savint;
#endif	/* SIG_NORESTART */
				return (SYSERR);
			}
			errno = 0;
		}
#ifndef SIG_NORESTART
		st.intfn = savint;
#endif	/* SIG_NORESTART */
		savetty = *tty;
	}
	savefd = fd;
	return (0);
}

#if ESH || VSH
/*
 * {	TTY_COOKED( fd )
 *
 *	This routine will set the tty in cooked mode.
 *	It is also called by error.done().
 * }
 */

void
tty_cooked(fd)
register int fd;
{

	if (editb.e_raw == 0)
		return;
	if (fd < 0)
		fd = savefd;
#ifdef L_MASK
	/* restore flags */
	if (l_changed & L_MASK)
		ioctl(fd, TIOCLSET, &l_mask);
	if (l_changed & T_CHARS)
		/* restore alternate break character */
		ioctl(fd, TIOCSETC, &l_ttychars);
	if (l_changed & L_CHARS)
		/* restore alternate break character */
		ioctl(fd, TIOCSLTC, &l_chars);
	l_changed = 0;
#endif	/* L_MASK */
	/* don't do tty_set unless ttyparm has valid data */
	if (savefd < 0 || tty_set(fd, TCSANOW, &ttyparm) == SYSERR)
		return;
	editb.e_raw = 0;
}

/*
 * {	TTY_RAW( fd )
 *
 *	This routine will set the tty in raw mode.
 * }
 */

int
tty_raw(int fd)
{
#ifdef L_MASK
	struct ltchars lchars;
#endif	/* L_MASK */
	if (editb.e_raw == RAWMODE)
		return (GOOD);
#ifndef RAWONLY
	if (editb.e_raw != ALTMODE) {
#endif /* RAWONLY */
		if (tty_get(fd, &ttyparm) == SYSERR)
			return (BAD);
#ifndef	RAWONLY
	}
#endif /* RAWONLY */

#if  L_MASK || VENIX
	if (!(ttyparm.sg_flags & ECHO) || (ttyparm.sg_flags & LCASE))
		return (BAD);
	nttyparm = ttyparm;
	nttyparm.sg_flags &= ~(ECHO | TBDELAY);
#ifdef CBREAK
	nttyparm.sg_flags |= CBREAK;
#else
	nttyparm.sg_flags |= RAW;
#endif /* CBREAK */
	editb.e_erase = ttyparm.sg_erase;
	editb.e_kill = ttyparm.sg_kill;
	editb.e_eof = cntl('D');
	if (tty_set(fd, TCSADRAIN, &nttyparm) == SYSERR)
		return (BAD);
	editb.e_ttyspeed = (ttyparm.sg_ospeed >= B1200 ? FAST : SLOW);
#ifdef _SELECT5_
	delay = tty_speeds[ttyparm.sg_ospeed];
#endif /* _SELECT5_ */
#ifdef TIOCGLTC
	/* try to remove effect of ^V  and ^Y and ^O */
	if (ioctl(fd, TIOCGLTC, &l_chars) != SYSERR) {
		lchars = l_chars;
		lchars.t_lnextc = -1;
		lchars.t_flushc = -1;
		lchars.t_dsuspc = -1;	/* no delayed stop process signal */
		if (ioctl(fd, TIOCSLTC, &lchars) != SYSERR)
			l_changed |= L_CHARS;
	}
#endif	/* TIOCGLTC */
#else

	if (!(ttyparm.c_lflag & ECHO))
		return (BAD);

#ifdef FLUSHO
	ttyparm.c_lflag &= ~FLUSHO;
#endif /* FLUSHO */
	nttyparm = ttyparm;
#ifndef u370
	nttyparm.c_iflag &= ~(IGNPAR|PARMRK|INLCR|IGNCR|ICRNL);
	nttyparm.c_iflag |= BRKINT;
#else
	nttyparm.c_iflag &=
	    ~(IGNBRK|PARMRK|INLCR|IGNCR|ICRNL|INPCK);
	nttyparm.c_iflag |= (BRKINT|IGNPAR);
#endif	/* u370 */
	nttyparm.c_lflag &= ~(ICANON|ECHO|ECHOK);
	nttyparm.c_cc[VTIME] = 0;
	nttyparm.c_cc[VMIN] = 1;
#ifdef VDISCARD
	nttyparm.c_cc[VDISCARD] = 0;
#endif /* VDISCARD */
#ifdef VDSUSP
	nttyparm.c_cc[VDSUSP] = 0;
#endif /* VDSUSP */
#ifdef VWERASE
	nttyparm.c_cc[VWERASE] = 0;
#endif /* VWERASE */
#ifdef VLNEXT
	nttyparm.c_cc[VLNEXT] = 0;
#endif /* VLNEXT */
	editb.e_eof = ttyparm.c_cc[VEOF];
	editb.e_erase = ttyparm.c_cc[VERASE];
	editb.e_kill = ttyparm.c_cc[VKILL];
	if (tty_set(fd, TCSADRAIN, &nttyparm) == SYSERR)
		return (BAD);
	editb.e_ttyspeed = (cfgetospeed(&ttyparm) >= B1200 ? FAST : SLOW);
#endif
	editb.e_raw = RAWMODE;
	return (GOOD);
}

#ifndef RAWONLY

/*
 *
 *	Get tty parameters and make ESC and '\r' wakeup characters.
 *
 */

#ifdef TIOCGETC
int
tty_alt(int fd)
{
	int mask;
	struct tchars ttychars;

	if (editb.e_raw == ALTMODE)
		return (GOOD);
	if (editb.e_raw == RAWMODE)
		tty_cooked(fd);
	l_changed = 0;
	if (editb.e_ttyspeed == 0) {
		if ((tty_get(fd, &ttyparm) != SYSERR))
			editb.e_ttyspeed =
			    (ttyparm.sg_ospeed >= B1200 ? FAST : SLOW);
		editb.e_raw = ALTMODE;
	}
	if (ioctl(fd, TIOCGETC, &l_ttychars) == SYSERR)
		return (BAD);
	if (ioctl(fd, TIOCLGET, &l_mask) == SYSERR)
		return (BAD);
	ttychars = l_ttychars;
	mask =  LCRTBS|LCRTERA|LCTLECH|LPENDIN|LCRTKIL;
	if ((l_mask|mask) != l_mask)
		l_changed = L_MASK;
	if (ioctl(fd, TIOCLBIS, &mask) == SYSERR)
		return (BAD);
	if (ttychars.t_brkc != ESC) {
		ttychars.t_brkc = ESC;
		l_changed |= T_CHARS;
		if (ioctl(fd, TIOCSETC, &ttychars) == SYSERR)
			return (BAD);
	}
	return (GOOD);
}
#else
#ifndef PENDIN
#define	PENDIN	0
#endif /* PENDIN */
#ifndef IEXTEN
#define	IEXTEN	0
#endif /* IEXTEN */
int
tty_alt(int fd)
{
	if (editb.e_raw == ALTMODE)
		return (GOOD);
	if (editb.e_raw == RAWMODE)
		tty_cooked(fd);
	if ((tty_get(fd, &ttyparm) == SYSERR) || (!(ttyparm.c_lflag & ECHO)))
		return (BAD);
#ifdef FLUSHO
	ttyparm.c_lflag &= ~FLUSHO;
#endif /* FLUSHO */
	nttyparm = ttyparm;
	editb.e_eof = ttyparm.c_cc[VEOF];
#ifdef ECHOCTL
	/* escape character echos as ^[ */
	nttyparm.c_lflag |= (ECHOE|ECHOK|ECHOCTL|PENDIN|IEXTEN);
	nttyparm.c_cc[VEOL2] = ESC;
#else
	/* switch VEOL2 and EOF, since EOF isn't echo'd by driver */
	nttyparm.c_iflag &= ~(IGNCR|ICRNL);
	nttyparm.c_iflag |= INLCR;
	nttyparm.c_lflag |= (ECHOE|ECHOK);
	nttyparm.c_cc[VEOF] = ESC;	/* make ESC the eof char */
	nttyparm.c_cc[VEOL] = '\r';	/* make CR an eol char */
	nttyparm.c_cc[VEOL2] = editb.e_eof;	/* make EOF an eol char */
#endif /* ECHOCTL */
#ifdef VWERASE
	nttyparm.c_cc[VWERASE] = cntl('W');
#endif /* VWERASE */
#ifdef VLNEXT
	nttyparm.c_cc[VLNEXT] = cntl('V');
#endif /* VLNEXT */
	editb.e_erase = ttyparm.c_cc[VERASE];
	editb.e_kill = ttyparm.c_cc[VKILL];
	if (tty_set(fd, TCSADRAIN, &nttyparm) == SYSERR)
		return (BAD);
	editb.e_ttyspeed = (cfgetospeed(&ttyparm) >= B1200 ? FAST : SLOW);
	editb.e_raw = ALTMODE;
	return (GOOD);
}

#endif /* TIOCGETC */
#endif	/* RAWONLY */

int
tty_is_raw(struct termios *tp)
{
	if (memcmp(tp, &nttyparm, sizeof (struct termios)) == 0)
		return (1);
	else
		return (0);
}

/*
 *	ED_WINDOW()
 *
 *	return the window size
 */

#ifdef _sys_stream_
#include <sys/stream.h>
#endif /* _sys_ptem_ */
#ifdef _sys_ptem_
#include <sys/ptem.h>
#endif /* _sys_stream_ */
#ifdef _sys_jioctl_
#include <sys/jioctl.h>
#define	winsize	jwinsize
#define	ws_col	bytesx
#ifdef TIOCGWINSZ
#undef TIOCGWINSZ
#endif /* TIOCGWINSZ */
#define	TIOCGWINSZ	JWINSIZE
#endif /* _sys_jioctl_ */

int
ed_window()
{
	register int n = DFLTWINDOW-1;
	register char *cp = nam_strval(COLUMNS);

	if (cp) {
		n = atoi(cp) - 1;
	}
#ifdef TIOCGWINSZ
	else {
		/* for 5620's and 630's */
		struct winsize size;
		if (ioctl(ERRIO, TIOCGWINSZ, &size) != -1)
			if (size.ws_col > 0)
				n = size.ws_col - 1;
	}
#endif /* TIOCGWINSZ */
	if (n < MINWINDOW)
		n = MINWINDOW;
	else if (n > MAXWINDOW)
		n = MAXWINDOW;

	return (n);
}

/*
 *	E_FLUSH()
 *
 *	Flush the output buffer.
 *
 */

void
ed_flush()
{
	register int n = editb.e_outptr-editb.e_outbase;
	register int fd = ERRIO;

	if (n <= 0)
		return;
	write(fd, editb.e_outbase, (unsigned)n);
	editb.e_outptr = editb.e_outbase;
#ifdef _SELECT5_
	if (delay && n > delay/100) {
		/* delay until output drains */
		struct timeval timeloc;

		n *= 10;
		timeloc.tv_sec = n / delay;
		timeloc.tv_usec = (1000000 * (n % delay))/delay;
		select(0, (fd_set*)0, (fd_set *)0, (fd_set*)0, &timeloc);
	}
#else
#ifdef IODELAY
	if (editb.e_raw == RAWMODE && n > 16)
		tty_set(fd, TCSADRAIN, &nttyparm);
#endif /* IODELAY */
#endif /* _SELECT5_ */
}

/*
 * send the bell character ^G to the terminal
 */

void
ed_ringbell()
{
	write(ERRIO, bellchr, 1);
}

/*
 * send a carriage return line feed to the terminal
 */

void
ed_crlf()
{
#ifdef cray
	ed_putascii('\r');
#endif /* cray */
#ifdef u370
	ed_putascii('\r');
#endif	/* u370 */
#ifdef VENIX
	ed_putascii('\r');
#endif /* VENIX */
	ed_putascii('\n');
	ed_flush();
}

/*
 *	E_SETUP( max_prompt_size )
 *
 *	This routine sets up the prompt string
 *	The following is an unadvertised feature.
 *	  Escape sequences in the prompt can be excluded from the calculated
 *	  prompt length.  This is accomplished as follows:
 *	  - if the prompt string starts with "%\r, or contains \r%\r", where %
 *	    represents any char, then % is taken to be the quote character.
 *	  - strings enclosed by this quote character, and the quote character,
 *	    are not counted as part of the prompt length.
 *
 *	 Though editb.e_prompt points array of xchar, each element corresponds
 *	 to each _column_. So one multicolumn character occupies multiple
 *	 elements. The character itself is stored in first one of the
 *	 elements; other elements contain MARKER which indicates placeholder.
 */

void
ed_setup(fd)
{
	register wchar_t *pp;
	char *last;
	wchar_t *ppmax;
	wchar_t myquote = 0;
	int qlen = 1;
	char inquote = 0;
	editb.e_fd = fd;
	p_setout(ERRIO);
#ifdef KSHELL
	last = (char *)_sobuf;
#else
	last = editb.e_prbuff;
#endif /* KSHELL */
	if (hist_ptr) {
		register struct history *fp = hist_ptr;

		editb.e_hismax = fp->fixind;
		editb.e_hloff = 0;
		editb.e_hismin = fp->fixind-fp->fixmax;

		if (editb.e_hismin < 0)
			editb.e_hismin = 0;
	} else {
		editb.e_hismax = editb.e_hismin = editb.e_hloff = 0;
	}
	editb.e_hline = editb.e_hismax;
	editb.e_wsize = ed_window() - 2;
	editb.e_crlf = (*last ? YES : NO);
	pp = editb.e_prompt;
	ppmax = pp+PRSIZE - 1;	/* counted in columns */
	*pp++ = '\r';
	{
		register wchar_t c;

		while (c = mb_nextc((const char **)&last)) {
			switch (c) {
			case '\r':
				if (pp == (editb.e_prompt + 2)) /* quote char */
					myquote = *(pp - 1);
				/*FALLTHROUGH*/

			case '\n':
				/* start again */
				editb.e_crlf = YES;
				qlen = 1;
				inquote = 0;
				pp = editb.e_prompt + 1;
				break;

			case '\t':
				/* expand tabs */
				while ((pp - editb.e_prompt) % TABSIZE) {
					if (pp >= ppmax)
						break;
					*pp++ = ' ';
				}
				break;

			case BELL:
				/* cut out bells */
				break;

			default:
				if (c == myquote) {
					qlen += inquote;
					inquote ^= 1;
				}
				if (inquote) {
					/*
					 * Quoted character doesn't affect
					 * column counting. So it's simply
					 * copied to "physical" buffer.
					 */
					if (pp < ppmax) {
						qlen += inquote;
						*pp++ = c;
					}
				} else {
					/*
					 * Unquoted character is checked
					 * if it's multicolumn or not.
					 * For multicolumn (w > 1) character,
					 * use MARKERs to adjust
					 * column counting.
					 */
					if (sh_iswprint(c)) {
						int	w = sh_wcwidth(c);

						if (pp + w <= ppmax) {
							*pp++ = c;

							while (--w > 0)
								*pp++ = MARKER;
						}
					} else {
						/*
						 * Cannot determine
						 * column width.
						 * Assume single column.
						 */
						if (pp < ppmax) {
							*pp++ = c;
							editb.e_crlf = NO;
						}
					}
				}
			}
		}
	}
	editb.e_plen = pp - editb.e_prompt - qlen; /* in columns */
	*pp = 0;
	if ((editb.e_wsize -= editb.e_plen) < 7) {
		register int shift = 7-editb.e_wsize;

		editb.e_wsize = 7;
		pp = editb.e_prompt+1; /* seems to be for leading '\r' */
		while (*(pp + shift) == MARKER) {
			/* If first character is MARKER, shift one more. */
			editb.e_wsize++;
			shift++;
		}
		wcscpy(pp, pp + shift);
		editb.e_plen -= shift;
		/*
		 * "last" points next of terminating null.
		 *
		 * wcbytes(editb.e_prompt+1) returns number of bytes
		 * needed to store string editb.e_prompt
		 * (except leading CR) as multibyte string.
		 *
		 * last[-1] is terminator
		 * last[-wcbytes(...)-1] is top of non-CR character
		 *	to be displayed
		 * last[-wcbytes(...)-2] is the place for new CR
		 */
		last[-wcsbytes((const wchar_t *)editb.e_prompt+1)-2] = '\r';
	}
	p_flush();
	editb.e_outptr = (char *)_sobuf;
	editb.e_outbase = editb.e_outptr;
	editb.e_outlast = editb.e_outptr + IOBSIZE-3;
}

#ifdef KSHELL
/*
 * look for edit macro named _i
 * if found, puts the macro definition into lookahead buffer and returns 1
 */

int
ed_macro(int i)
{
	wchar_t c;
	register char *out;
	struct namnod *np;
	genchar buff[LOOKAHEAD+1];

	if (i != '@')
		macro[1] = i;
	/* undocumented feature, macros of the form <ESC>[c evoke alias __c */
	if (i == '_') {
		c = ed_getchar();
		if (!sh_iswalnum(c))
			return (0);
		macro[2] = c & STRIP;
	} else
		macro[2] = 0;
	if (sh_isalnum(i) &&
	    (np = nam_search(macro, sh.alias_tree, N_NOSCOPE))&&
	    (out = nam_strval(np))) {
		sh_mbstowcs(buff, out, LOOKAHEAD);
		i = wcslen(buff);
		while (i-- > 0)
			ed_ungetchar(buff[i]);
		return (1);
	}
	return (0);
}
/*
 * file name generation for edit modes
 * non-zero exit for error, <0 ring bell
 * don't search back past beginning of the buffer
 * mode is '*' for inline expansion,
 * mode is '\' for filename completion
 * mode is '=' cause files to be listed in select format
 */

int
ed_expand(genchar outbuff[], int *cur, int *eol, int mode)
{
	int	offset = staktell();
	off_t	staksav = staksave();

	struct comnod  *comptr = (struct comnod *)stakalloc(
	    sizeof (struct comnod));
	struct argnod *ap = (struct argnod *)stakseek(ARGVAL);
	register genchar *out;
	genchar *begin;
	int addstar;
	int istilde = 0;
	int rval = 0;
	int strip;
	optflag savflags = opt_flags;

	out = outbuff + *cur;
	comptr->comtyp = COMSCAN;
	comptr->comarg = ap;
	ap->argflag = (A_MAC|A_EXP);
	ap->argnxt.ap = 0;
	{
		register wchar_t c;
		int chktilde;
		wchar_t *cp;

		if (out > outbuff) {
			/* go to beginning of word */
			do {
				out--;
				c = *out;
			} while (out > outbuff && !iswqmeta(c));

			/* copy word into arg */
			if (iswqmeta(c))
				out++;
		}
		else
			out = outbuff;
		begin = out;
		chktilde = (*out == '~');
		/* addstar set to zero if * should not be added */
		addstar = '*';
		strip = 1;
		/* copy word to arg and do ~ expansion */
		while (1) {
			c = *out;
			if (iswexp(c))
				addstar = 0;
			if ((c == '/') && (addstar == 0))
				strip = 0;
			if (chktilde && (c == 0 || c == '/')) {
				chktilde = 0;
				*out = 0;

				if (cp = sh_tilde_wcs(begin)) {
					istilde++;
					stakseek(ARGVAL);
					stakputwcs(cp);
					if (c == 0) {
						addstar = 0;
						strip = 0;
					}
				}
				*out = c;
			}
			out++;
			/*
			 * If c breaks this loop, it should not be put
			 * onto stak. In pre-i18n code, such character
			 * was put and then be overriden by addstar.
			 * It needed backward seek on stak.
			 * So i18n code puts character only if loop
			 * continues.
			 */
			if (c == 0 || iswqmeta(c))
				break;
			stakputwc(c);
		}

		out--;
#ifdef tenex
		if (mode == '\\')
			addstar = '*';
#endif /* tenex */
		if (addstar) {
			stakputascii(addstar);
			stakputascii(0);
		} else
			stakputascii(0);
		stakfreeze(1);
	}
	if (mode != '*')
		on_option(MARKDIR);
	{
		register char **com;
		int	 narg;
		register int size;
		void (*savfn)();

		savfn = st.intfn;
		com = arg_build(&narg, comptr);
		st.intfn = savfn;

		/*  match? */
		if (*com == 0 ||
		    (!istilde && narg <= 1 && eq(ap->argval, *com))) {
			rval = -1;
			goto done;
		}
		if (mode == '=') {
			if (strip) {
				register char **ptrcom;

				for (ptrcom = com; *ptrcom; ptrcom++)
					/* trim directory prefix */
					*ptrcom = path_basename(*ptrcom);
			}
			p_setout(ERRIO);
			newline();
			p_list(narg, com);
			p_flush();
			goto done;
		}
		/* see if there is enough room */
		size = *eol - (out - begin);
#ifdef tenex
		if (mode == '\\') {
			/* just expand until name is unique */
			size += mbschars(*com);
		} else {
#endif
			size += narg;
			{
				char **savcom = com;

				while (*com)
					size += mbschars(*com++);
				com = savcom;
			}
#ifdef tenex
		}
#endif
		/* see if room for expansion */
		if (outbuff + size >= &outbuff[MAXLINE]) {
			com[0] = ap->argval;
			com[1] = 0;
		}
		/* save remainder of the buffer */
		wcscpy((wchar_t *)stakptr(0), out);
		out = begin +
		    sh_mbstowcs(begin, *com++, &outbuff[MAXLINE] - begin);
#ifdef tenex
		if (mode == '\\') {
			if ((*com == NULL) && out[-1] != '/')
				*out++ = ' ';
			while (*com && *begin) {
				out = overlay(begin, *com++);
			}
			if (*begin == 0)
				ed_ringbell();
		} else {
#endif
			while (*com) {
				*out++ = ' ';
				out += sh_mbstowcs(out,
				    *com++, MAXLINE - (out - begin));
			}
#ifdef tenex
		}
#endif
		*cur = (out - outbuff);
		/* restore rest of buffer */
		wcscpy(out, (wchar_t *)stakptr(0));
		out += wcslen(out);
		*eol = (out-outbuff);
	}
done:
	stakrestore(staksav, offset);
	opt_flags = savflags;
	return (rval);
}

#ifdef tenex
static genchar *
overlay(str, newstr)
register genchar *str;
char *newstr;
{
	while (*str && *str == mb_nextc((const char **)&newstr))
		str++;
	*str = 0;
	return (str);
}
#endif

/*
 * Enter the fc command on the current history line
 */
int
ed_fulledit(void)
{
	genchar *cp;

	if (!hist_ptr || (st.states & BUILTIN))
		return (BAD);
	/* use EDITOR on current command */
	if (editb.e_hline == editb.e_hismax) {
		if (editb.e_eol <= 0)
			return (BAD);
		editb.e_inbuf[editb.e_eol + 1] = 0;
		p_setout(hist_ptr->fixfd);
		p_wcs(editb.e_inbuf, 0);
		st.states |= FIXFLG;
		hist_flush();
	}
	cp = editb.e_inbuf;
	cp += sh_mbstowcs(editb.e_inbuf, e_runvi, strlen(e_runvi));
	{
		char	*p;

		p = sh_itos(editb.e_hline);
		cp += mbstowcs(cp, p, strlen(p));
	}
	editb.e_eol = cp - editb.e_inbuf - 1;
	return (GOOD);
}
#endif	/* KSHELL */


/*
 * routine to perform read from terminal for vi and emacs mode
 */


wchar_t
ed_getchar()
{
	register int i;
	register wchar_t c;
	register int maxtry = MAXTRY;
	unsigned nchar = READAHEAD; /* number of characters to read at a time */
	wchar_t		wc;
	int		l;
	static char readin[LOOKAHEAD + MB_LEN_MAX];
	static char	*rp = readin;

	if (lookahead) {
		c =  previous[--lookahead];

		/* map '\r' to '\n' */
		if (c == '\r' && !in_raw)
			c = '\n';
		return (c);
	}

	ed_flush();
	/*
	 * you can't chance read ahead at the end of line
	 * or when the input is a pipe
	 */
#ifdef KSHELL
	if ((editb.e_cur >= editb.e_eol) || fnobuff(io_get_ftbl(filedes)))
#else
	if (editb.e_cur >= editb.e_eol)
#endif /* KSHELL */
		nchar = 1;
	/* Set 'i' to indicate read failed, in case intr set */
retry:
	i = -1;
	errno = 0;
	editb.e_inmacro = 0;

	/*
	 * Do read prior to mbtowc() if this is retry or no remaining bytes.
	 * Otherwise, skip reading to try mbtowc() first with
	 * bytes read and not used by provious mbtowc().
	 */
	if (maxtry < MAXTRY || rp == readin) {
		while (slowsig() == 0 && maxtry--) {
			errno = 0;

			if ((i = read(filedes, rp, nchar)) != -1) {
				rp[i] = '\0';
				rp += i;
				break;
			}
		}
	}
	if ((i == -1) && (errno != 0)) {
		/* read failed -- do nothing */
	} else if (
#ifdef	CSI_ASCIIACCEL
	    (((rp - readin) >= 1 && isascii(readin[0])) &&
	    (l = readin[0] ? 1 : 0,
	    wc = (wchar_t)readin[0] & STRIP,

		/*
		 * `1' below is true which prevents mbtowc()
		 * if isascii() is true.
		 */
	    1)) ||
#endif	/* CSI_ASCIIACCEL */
	    ((l = mbtowc(&wc, readin, rp - readin)) >= 0)) {
		if (l > 0) {
			c = wc;
		} else { /* i.e. readin[0] == '\0' */
			c = '\0';
			l = 1;
		}
		/*
		 * Remove first l bytes from `readin' buffer
		 * because they are used to compose chracter `c'.
		 * After that, adjust `rp'.
		 */
		if (readin + l < rp) {
			/* some unused bytes remain. */
			char	*nrp = readin;

			while (nrp + l < rp) {
				*nrp = *(nrp + l);
				nrp++;
			}
			*nrp = '\0';
			rp = nrp;
		} else {
			readin[0] = '\0';
			rp = readin;
		}
		previous[lookahead++] = c;
#ifndef CBREAK
		if (c == '\0') {
			/* user break key */
			lookahead = 0;
#ifdef KSHELL
			sh_fault(SIGINT);
			LONGJMP(env, UINTR);
#endif	/* KSHELL */
		}
#endif	/* !CBREAK */
	} else if (l < 0 && (rp - readin) >= MB_LEN_MAX) { /* invalid char */
		lookahead = 0;
	} else { /* mbtowc() failed because of insufficient bytes */
		if (maxtry > 0)
			goto retry;
	}

	if (lookahead > 0)
		return (ed_getchar());
	LONGJMP(env, (i == 0 ? UEOF : UINTR)); /* What a mess! Give up */
	/* NOTREACHED */
}

void
ed_ungetchar(c)
register wchar_t c;
{
	if (lookahead < LOOKAHEAD)
		previous[lookahead++] = c;
}

/*
 * put a character into the output buffer
 */

void
ed_putascii(c)
register int c;
{
	register char *dp = editb.e_outptr;

	if (c == '_') {
		*dp++ = ' ';
		*dp++ = '\b';
	}
	*dp++ = c;
	*dp = '\0';
	if (dp >= editb.e_outlast)
		ed_flush();
	else
		editb.e_outptr = dp;
}

void
ed_putchar(c)
wchar_t c;
{
	register char *dp = editb.e_outptr;

	if (c == '_') {
		if (dp + 2 >= editb.e_outlast)
			ed_flush();
		*dp++ = ' ';
		*dp++ = '\b';
	}
	if (dp + (wcbytes(c) + 1) >= editb.e_outlast)
			ed_flush();
	dp += sh_wctomb(dp, c);
	*dp = '\0';
	if (dp >= editb.e_outlast)
		ed_flush();
	else
		editb.e_outptr = dp;
}

/*
 * copy virtual to physical and return the index for cursor in physical buffer
 */
int
ed_virt_to_phys(genchar *virt, genchar *phys, int cur, int voff, int poff)
{
	genchar *sp = virt;
	genchar *dp = phys;
	wchar_t c;
	genchar *curp = sp + cur;
	genchar *curd;	/* cursor position in dp */
	genchar *dpmax = phys+MAXLINE;
	int r;
	int	d;
	sp += voff;
	dp += poff;

	for (r = poff; c = *sp; sp++) {
		if (curp == sp)
			r = dp - phys;
		if (c == '\t') { /* expand a tab to spaces */
			d = dp - phys;
			if (is_option(EDITVI))
				d += editb.e_plen;
			d = TABSIZE - d % TABSIZE;
			/*
			 * if enough physical buffer elements
			 * not available, expand to
			 * as much spaces as possible
			 */
			if (d > (dpmax - dp))
				d = dpmax - dp;
			/* expanding (except the last space) */
			while (--d > 0)
				*dp++ = ' ';
			/*
			 * locate cursor on the last space
			 * if cursor is on the tab
			 */
			if (curp == sp && is_option(EDITVI))
				r = dp - phys;
			/* put the last space */
			*dp++ = ' ';
		} else {
			if (dp + sh_wcwidth(c) >= dpmax)
				break;
			dp += wctovwcs(dp, &curd, c);
			if (curp == sp && is_option(EDITVI))
				r = curd - phys;
			continue;
		}
		if (dp >= dpmax)
			break;
	}
	*dp = 0;
	return (r);
}

#endif /* ESH || VSH */

/*
 * convert external representation <src> to an array of genchars <dest>
 * <src> and <dest> can be the same
 * returns number of chars in dest
 */

int
ed_internal(src, dest)
register char *src;
genchar *dest;
{
	register int c;
	register genchar *dp = dest;

	if ((char *)dest == src) {
		genchar buffer[MAXLINE + 1];

		c = sh_mbstowcs(buffer, src, MAXLINE);
		buffer[MAXLINE] = 0;
		wcscpy(dp, buffer);
		return (c);
	}

	return (sh_mbstowcs(dest, src, MAXLINE));
}

/*
 * convert internal representation <src> into character array <dest>.
 * The <src> and <dest> may be the same.
 * returns number of bytes in dest.
 */

int
ed_external(src, dest)
genchar *src;
char *dest;
{
	register int c;

	if ((char *)src == dest) {
		char buffer[MAXLINE * MB_LEN_MAX + 1];

		c = sh_wcstombs(buffer, src, MAXLINE * MB_LEN_MAX);
		buffer[MAXLINE * MB_LEN_MAX] = '\0';
		strcpy(dest, buffer);
		return (c);
	}

	return (sh_wcstombs(dest, src, MAXLINE*MB_LEN_MAX));
}

#ifdef future
/*
 * returns 1 when <n> bytes starting at <a> and <b> are equal
 */
static int
compare(a, b, n)
register char *a;
register char *b;
register int n;
{
	while (n-- > 0) {
		if (*a++ != *b++)
			return (0);
	}
	return (1);
}
#endif

#ifdef OLDTERMIO

#include <sys/termio.h>

#ifndef ECHOCTL
#define	ECHOCTL	0
#endif /* !ECHOCTL */
char echoctl;
static char tcgeta;
static struct termio ott;

/*
 * For backward compatibility only
 * This version will use termios when possible, otherwise termio
 */


tcgetattr(fd, tt)
struct termios *tt;
{
	register int r;
	register int i;

	tcgeta = 0;
	echoctl = (ECHOCTL != 0);
	if ((r = ioctl(fd, TCGETS, tt)) >= 0 || errno != EINVAL)
		return (r);
	if ((r = ioctl(fd, TCGETA, &ott)) >= 0) {
		tt->c_lflag = ott.c_lflag;
		tt->c_oflag = ott.c_oflag;
		tt->c_iflag = ott.c_iflag;
		tt->c_cflag = ott.c_cflag;
		for (i = 0; i < NCC; i++)
			tt->c_cc[i] = ott.c_cc[i];
		tcgeta++;
		echoctl = 0;
	}
	return (r);
}

tcsetattr(fd, mode, tt)
register int mode;
struct termios *tt;
{
	register int r;

	if (tcgeta) {
		register int i;

		ott.c_lflag = tt->c_lflag;
		ott.c_oflag = tt->c_oflag;
		ott.c_iflag = tt->c_iflag;
		ott.c_cflag = tt->c_cflag;
		for (i = 0; i < NCC; i++)
			ott.c_cc[i] = tt->c_cc[i];
		if (tt->c_lflag & ECHOCTL) {
			ott.c_lflag &= ~(ECHOCTL|IEXTEN);
			ott.c_iflag &= ~(IGNCR|ICRNL);
			ott.c_iflag |= INLCR;
			ott.c_cc[VEOF] = ESC;  /* ESC -> eof char */
			ott.c_cc[VEOL] = '\r'; /* CR -> eol char */
			ott.c_cc[VEOL2] = tt->c_cc[VEOF]; /* EOF -> eol char */
		}
		switch (mode) {
		case TCSANOW:
			mode = TCSETA;
			break;
		case TCSADRAIN:
			mode = TCSETAW;
			break;
		case TCSAFLUSH:
			mode = TCSETAF;
		}
		return (ioctl(fd, mode, &ott));
	}
	return (ioctl(fd, mode, tt));
}
#endif /* OLDTERMIO */

/*
 * Given an wchar_t w, convert it to "visible" representation then put
 * result into vwcs.
 * If w is printable, w is to be put in *vwcs as it is.
 * If w is non-printable or illegal byte, it is converted to
 * wchar_t string which contains one or more octal represention.
 * e.g.
 *	If w contains wide character for EUC mutlibyte <0xa6,0xe0>
 *	which is legal but non-printable in Japanese EUC locale,
 *	it is converted to wchar_t string for "\246\340".
 *
 *	If w contains byte <0xff>
 *	which is illegal byte in Japanese EUC locale,
 *	it is converted to wchar_t string for "\377".
 * Returns number of xchar's stored.
 * Cursor position is stored in *cur.
 */

static int
wctovwcs(
	wchar_t	*vwcs,
	wchar_t	**cur,
	wchar_t	c
)
{
	wchar_t	*dp = vwcs;
	int	w;
	int	d;

	if (sh_iswprint(c)) {
		/*
		 * Printable. Put it as is then put placeholders
		 * if multicolumn character.
		 */
		*cur = dp; /* cursor is on the first column */
		*dp++ = c;
		if ((w = sh_wcwidth(c)) > 1)
			while (--w > 0)
				*dp++ = MARKER;
		return (dp - vwcs);
	} else if (iswascii(c) &&
	    (d = c, (d < ' ' || d == 0x7f))) {
		/* Control code. Use ^x representation. */
		*dp++ = '^';
		*cur = dp; /* cursor is on the last column */
		*dp++ = ((unsigned int)d ^ TO_PRINT);
		return (2);
	} else if (!IsWInvalid(c)) {
		/*
		 * Other legal but non-printable character.
		 * Use one or more of octal representation "\ooo"s.
		 */
		unsigned char	buf[MB_LEN_MAX + 1];
		int		l, i;

		l = sh_wctomb((char *)buf, c);
		for (i = 0; i < l; i++) {
			*dp++ = '\\';
			*dp++ = '0' + ((buf[i]>>6) & 03);
			*dp++ = '0' + ((buf[i]>>3) & 07);
			*cur = dp; /* cursor is on the last column */
			*dp++ = '0' + ((buf[i]) & 07);
		}
		return (l * 4);
	} else {
		/* Illegal byte. Use octal representation "\ooo". */
		int	b = ShWRawByteToByte(c);
		*dp++ = '\\';
		*dp++ = '0' + ((b>>6) & 03);
		*dp++ = '0' + ((b>>3) & 07);
		*cur = dp; /* cursor is on the last column */
		*dp++ = '0' + ((b) & 07);
		return (4);
	}
}
