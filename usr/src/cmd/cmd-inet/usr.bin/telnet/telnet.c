/*
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 1988, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static char sccsid[] = "@(#)telnet.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */

#include <netdb.h>
#include <sys/types.h>

#include <curses.h>
#include <signal.h>
/*
 * By the way, we need to include curses.h before telnet.h since,
 * among other things, telnet.h #defines 'DO', which is a variable
 * declared in curses.h.
 */

#include <arpa/telnet.h>

#include <ctype.h>

#include "ring.h"

#include "defines.h"
#include "externs.h"
#include "types.h"
#include "general.h"

#include "auth.h"
#include "encrypt.h"

#define	strip(x)	((x)&0x7f)

/* Buffer for sub-options */
static unsigned char	subbuffer[SUBBUFSIZE];
static unsigned char	*subpointer;
static unsigned char	*subend;

#define	SB_CLEAR()	subpointer = subbuffer;
#define	SB_TERM()	{ subend = subpointer; SB_CLEAR(); }
#define	SB_ACCUM(c)	if (subpointer < (subbuffer+sizeof (subbuffer))) { \
				*subpointer++ = (c); \
			}

#define	SB_GET()	((*subpointer++)&0xff)
#define	SB_PEEK()	((*subpointer)&0xff)
#define	SB_EOF()	(subpointer >= subend)
#define	SB_LEN()	(subend - subpointer)

char	options[SUBBUFSIZE];		/* The combined options */
char	do_dont_resp[SUBBUFSIZE];
char	will_wont_resp[SUBBUFSIZE];

int eight = 0;
int autologin = 0;	/* Autologin anyone? */
int skiprc = 0;
int connected;
int showoptions;
static int ISend;	/* trying to send network data in */
int debug = 0;
int crmod;
int netdata;		/* Print out network data flow */
int crlf;		/* Should '\r' be mapped to <CR><LF> (or <CR><NUL>)? */
int telnetport;
int SYNCHing;		/* we are in TELNET SYNCH mode */
int flushout;		/* flush output */
int autoflush = 0;	/* flush output when interrupting? */
int autosynch;		/* send interrupt characters with SYNCH? */
int localflow;		/* we handle flow control locally */
int restartany;		/* if flow control enabled, restart on any character */
int localchars;		/* we recognize interrupt/quit */
int donelclchars;	/* the user has set "localchars" */
int donebinarytoggle;	/* the user has put us in binary */
int dontlecho;		/* do we suppress local echoing right now? */
int eof_pending = 0;	/* we received a genuine EOF on input, send IAC-EOF */
int globalmode;

/* spin while waiting for authentication */
boolean_t scheduler_lockout_tty = B_FALSE;
int encrypt_flag = 0;
int forwardable_flag = 0;
int forward_flag = 0;
boolean_t wantencryption = B_FALSE;

char *prompt = 0;

cc_t escape;
cc_t rlogin;
boolean_t escape_valid = B_TRUE;
#ifdef	KLUDGELINEMODE
cc_t echoc;
#endif

/*
 * Telnet receiver states for fsm
 */
#define	TS_DATA		0
#define	TS_IAC		1
#define	TS_WILL		2
#define	TS_WONT		3
#define	TS_DO		4
#define	TS_DONT		5
#define	TS_CR		6
#define	TS_SB		7		/* sub-option collection */
#define	TS_SE		8		/* looking for sub-option end */

static int	telrcv_state;
#ifdef	OLD_ENVIRON
static	unsigned char telopt_environ = TELOPT_NEW_ENVIRON;
#else
#define	telopt_environ	TELOPT_NEW_ENVIRON
#endif

jmp_buf	toplevel = { 0 };
jmp_buf	peerdied;

static int flushline;
int	linemode;

int	reqd_linemode = 0; /* Set if either new or old line mode in */
			/* effect since before initial negotiations */

#ifdef	KLUDGELINEMODE
int	kludgelinemode = 1;
#endif

/*
 * The following are some clocks used to decide how to interpret
 * the relationship between various variables.
 */

Clocks clocks;

#ifdef	notdef
Modelist modelist[] = {
	{ "telnet command mode", COMMAND_LINE },
	{ "character-at-a-time mode", 0 },
	{ "character-at-a-time mode (local echo)", LOCAL_ECHO|LOCAL_CHARS },
	{ "line-by-line mode (remote echo)", LINE | LOCAL_CHARS },
	{ "line-by-line mode", LINE | LOCAL_ECHO | LOCAL_CHARS },
	{ "line-by-line mode (local echoing suppressed)", LINE | LOCAL_CHARS },
	{ "3270 mode", 0 },
};
#endif

static void willoption(int);
static void wontoption(int);
static void lm_will(unsigned char *, int);
static void lm_wont(unsigned char *, int);
static void lm_do(unsigned char *, int);
static void lm_dont(unsigned char *, int);
static void slc_init(void);
static void slc_import(int);
static void slc_export(void);
static void slc_start_reply(size_t);
static void slc_add_reply(unsigned char, unsigned char, cc_t);
static void slc_end_reply(void);
static void slc(unsigned char *, int);
static int slc_update(void);
static void env_opt(unsigned char *, int);
static void env_opt_start(void);
static void sendeof(void);
static int is_unique(register char *, register char **, register char **);

/*
 * Initialize telnet environment.
 */

int
init_telnet()
{
	if (env_init() == 0)
		return (0);

	SB_CLEAR();
	ClearArray(options);

	connected = ISend = localflow = donebinarytoggle = 0;
	restartany = -1;

	SYNCHing = 0;

	/* Don't change NetTrace */

	escape = CONTROL(']');
	rlogin = _POSIX_VDISABLE;
#ifdef	KLUDGELINEMODE
	echoc = CONTROL('E');
#endif

	flushline = 1;
	telrcv_state = TS_DATA;

	return (1);
}


#ifdef	notdef
#include <varargs.h>

/*VARARGS*/
static void
printring(va_alist)
	va_dcl
{
	va_list ap;
	char buffer[100];		/* where things go */
	char *ptr;
	char *format;
	char *string;
	Ring *ring;
	int i;

	va_start(ap);

	ring = va_arg(ap, Ring *);
	format = va_arg(ap, char *);
	ptr = buffer;

	while ((i = *format++) != 0) {
		if (i == '%') {
			i = *format++;
			switch (i) {
			case 'c':
				*ptr++ = va_arg(ap, int);
				break;
			case 's':
				string = va_arg(ap, char *);
				ring_supply_data(ring, buffer, ptr-buffer);
				ring_supply_data(ring, string, strlen(string));
				ptr = buffer;
				break;
			case 0:
				ExitString("printring: trailing %%.\n",
				    EXIT_FAILURE);
				/*NOTREACHED*/
			default:
				ExitString("printring: unknown format "
				    "character.\n", EXIT_FAILURE);
				/*NOTREACHED*/
			}
		} else {
			*ptr++ = i;
		}
	}
	ring_supply_data(ring, buffer, ptr-buffer);
}
#endif

/*
 * These routines are in charge of sending option negotiations
 * to the other side.
 *
 * The basic idea is that we send the negotiation if either side
 * is in disagreement as to what the current state should be.
 */

void
send_do(c, init)
	register int c, init;
{
	if (init) {
		if (((do_dont_resp[c] == 0) && my_state_is_do(c)) ||
		    my_want_state_is_do(c))
			return;
		set_my_want_state_do(c);
		do_dont_resp[c]++;
	}
	NET2ADD(IAC, DO);
	NETADD(c);
	printoption("SENT", DO, c);
}

void
send_dont(c, init)
	register int c, init;
{
	if (init) {
		if (((do_dont_resp[c] == 0) && my_state_is_dont(c)) ||
		    my_want_state_is_dont(c))
			return;
		set_my_want_state_dont(c);
		do_dont_resp[c]++;
	}
	NET2ADD(IAC, DONT);
	NETADD(c);
	printoption("SENT", DONT, c);
}

void
send_will(c, init)
	register int c, init;
{
	if (init) {
		if (((will_wont_resp[c] == 0) && my_state_is_will(c)) ||
		    my_want_state_is_will(c))
			return;
		set_my_want_state_will(c);
		will_wont_resp[c]++;
	}
	NET2ADD(IAC, WILL);
	NETADD(c);
	printoption("SENT", WILL, c);
}

void
send_wont(c, init)
	register int c, init;
{
	if (init) {
		if (((will_wont_resp[c] == 0) && my_state_is_wont(c)) ||
		    my_want_state_is_wont(c))
			return;
		set_my_want_state_wont(c);
		will_wont_resp[c]++;
	}
	NET2ADD(IAC, WONT);
	NETADD(c);
	printoption("SENT", WONT, c);
}


static void
willoption(option)
	int option;
{
	int new_state_ok = 0;

	if (do_dont_resp[option]) {
	    --do_dont_resp[option];
	    if (do_dont_resp[option] && my_state_is_do(option))
		--do_dont_resp[option];
	}

	if ((do_dont_resp[option] == 0) && my_want_state_is_dont(option)) {

	    switch (option) {

	    case TELOPT_ECHO:
	    case TELOPT_SGA:
		if (reqd_linemode && my_state_is_dont(option)) {
		    break;
		}
		/* FALLTHROUGH */
	    case TELOPT_BINARY:
		settimer(modenegotiated);
		/* FALLTHROUGH */
	    case TELOPT_STATUS:
	    case TELOPT_AUTHENTICATION:
		/* FALLTHROUGH */
	    case TELOPT_ENCRYPT:
		new_state_ok = 1;
		break;

	    case TELOPT_TM:
		if (flushout)
		    flushout = 0;
		/*
		 * Special case for TM.  If we get back a WILL,
		 * pretend we got back a WONT.
		 */
		set_my_want_state_dont(option);
		set_my_state_dont(option);
		return;			/* Never reply to TM will's/wont's */

	    case TELOPT_LINEMODE:
	    default:
		break;
	    }

	    if (new_state_ok) {
		set_my_want_state_do(option);
		send_do(option, 0);
		setconnmode(0);		/* possibly set new tty mode */
	    } else {
		do_dont_resp[option]++;
		send_dont(option, 0);
	    }
	}
	set_my_state_do(option);
	if (option == TELOPT_ENCRYPT)
		encrypt_send_support();
}

static void
wontoption(option)
	int option;
{
	if (do_dont_resp[option]) {
		--do_dont_resp[option];
		if (do_dont_resp[option] && my_state_is_dont(option))
			--do_dont_resp[option];
	}

	if ((do_dont_resp[option] == 0) && my_want_state_is_do(option)) {

		switch (option) {

#ifdef	KLUDGELINEMODE
		case TELOPT_SGA:
			if (!kludgelinemode)
				break;
			/* FALLTHROUGH */
#endif
		case TELOPT_ECHO:
			settimer(modenegotiated);
			break;

		case TELOPT_TM:
			if (flushout)
				flushout = 0;
			set_my_want_state_dont(option);
			set_my_state_dont(option);
			return;		/* Never reply to TM will's/wont's */

		default:
			break;
		}
		set_my_want_state_dont(option);
		if (my_state_is_do(option))
			send_dont(option, 0);
		setconnmode(0);			/* Set new tty mode */
	} else if (option == TELOPT_TM) {
		/*
		 * Special case for TM.
		 */
		if (flushout)
			flushout = 0;
		set_my_want_state_dont(option);
	}
	set_my_state_dont(option);
}

static void
dooption(option)
	int option;
{
	int new_state_ok = 0;

	if (will_wont_resp[option]) {
		--will_wont_resp[option];
		if (will_wont_resp[option] && my_state_is_will(option))
			--will_wont_resp[option];
	}

	if (will_wont_resp[option] == 0) {
		if (my_want_state_is_wont(option)) {

			switch (option) {

			case TELOPT_TM:
				/*
				 * Special case for TM.  We send a WILL,
				 * but pretend we sent WONT.
				 */
				send_will(option, 0);
				set_my_want_state_wont(TELOPT_TM);
				set_my_state_wont(TELOPT_TM);
				return;

			case TELOPT_BINARY:	/* binary mode */
			case TELOPT_NAWS:	/* window size */
			case TELOPT_TSPEED:	/* terminal speed */
			case TELOPT_LFLOW:	/* local flow control */
			case TELOPT_TTYPE:	/* terminal type option */
			case TELOPT_SGA:	/* no big deal */
			case TELOPT_ENCRYPT:	/* encryption variable option */
				new_state_ok = 1;
				break;

			case TELOPT_NEW_ENVIRON:
				/* New environment variable option */
#ifdef	OLD_ENVIRON
				if (my_state_is_will(TELOPT_OLD_ENVIRON))
					/* turn off the old */
					send_wont(TELOPT_OLD_ENVIRON, 1);
goto env_common;
			case TELOPT_OLD_ENVIRON:
				/* Old environment variable option */
				if (my_state_is_will(TELOPT_NEW_ENVIRON))
					/* Don't enable if new one is in use! */
					break;
env_common:
				telopt_environ = option;
#endif
				new_state_ok = 1;
				break;

			case TELOPT_AUTHENTICATION:
				if (autologin)
					new_state_ok = 1;
				break;

			case TELOPT_XDISPLOC:	/* X Display location */
				if (env_getvalue((unsigned char *)"DISPLAY"))
					new_state_ok = 1;
				break;

			case TELOPT_LINEMODE:
#ifdef	KLUDGELINEMODE
				kludgelinemode = 0;
				send_do(TELOPT_SGA, 1);
#endif
				set_my_want_state_will(TELOPT_LINEMODE);
				send_will(option, 0);
				set_my_state_will(TELOPT_LINEMODE);
				slc_init();
				return;

			case TELOPT_ECHO: /* We're never going to echo... */
			default:
				break;
			}

			if (new_state_ok) {
				set_my_want_state_will(option);
				send_will(option, 0);
				setconnmode(0);		/* Set new tty mode */
			} else {
				will_wont_resp[option]++;
				send_wont(option, 0);
			}
		} else {
			/*
			 * Handle options that need more things done after the
			 * other side has acknowledged the option.
			 */
			switch (option) {
			case TELOPT_LINEMODE:
#ifdef	KLUDGELINEMODE
				kludgelinemode = 0;
				send_do(TELOPT_SGA, 1);
#endif
				set_my_state_will(option);
				slc_init();
				send_do(TELOPT_SGA, 0);
				return;
			}
		}
	}
	set_my_state_will(option);
}

	static void
dontoption(option)
	int option;
{

	if (will_wont_resp[option]) {
	    --will_wont_resp[option];
	    if (will_wont_resp[option] && my_state_is_wont(option))
		--will_wont_resp[option];
	}

	if ((will_wont_resp[option] == 0) && my_want_state_is_will(option)) {
	    switch (option) {
	    case TELOPT_LINEMODE:
		linemode = 0;	/* put us back to the default state */
		break;
#ifdef	OLD_ENVIRON
	    case TELOPT_NEW_ENVIRON:
		/*
		 * The new environ option wasn't recognized, try
		 * the old one.
		 */
		send_will(TELOPT_OLD_ENVIRON, 1);
		telopt_environ = TELOPT_OLD_ENVIRON;
		break;
#endif
	    }
	    /* we always accept a DONT */
	    set_my_want_state_wont(option);
	    if (my_state_is_will(option))
		send_wont(option, 0);
	    setconnmode(0);			/* Set new tty mode */
	}
	set_my_state_wont(option);
}

/*
 * Given a buffer returned by tgetent(), this routine will turn
 * the pipe separated list of names in the buffer into an array
 * of pointers to null terminated names.  We toss out any bad,
 * duplicate, or verbose names (names with spaces).
 */

static char *name_unknown = "UNKNOWN";
static char *unknown[] = { 0, 0 };

static char **
mklist(buf, name)
	char *buf, *name;
{
	register int n;
	register char c, *cp, **argvp, *cp2, **argv, **avt;

	if (name) {
		if (strlen(name) > 40u) {
			name = 0;
			unknown[0] = name_unknown;
		} else {
			unknown[0] = name;
			upcase(name);
		}
	} else
		unknown[0] = name_unknown;
	/*
	 * Count up the number of names.
	 */
	for (n = 1, cp = buf; *cp && *cp != ':'; cp++) {
		if (*cp == '|')
			n++;
	}
	/*
	 * Allocate an array to put the name pointers into
	 */
	argv = malloc((n+3)*sizeof (char *));
	if (argv == 0)
		return (unknown);

	/*
	 * Fill up the array of pointers to names.
	 */
	*argv = 0;
	argvp = argv+1;
	n = 0;
	for (cp = cp2 = buf; (c = *cp) != NULL;  cp++) {
		if (c == '|' || c == ':') {
			*cp++ = '\0';
			/*
			 * Skip entries that have spaces or are over 40
			 * characters long.  If this is our environment
			 * name, then put it up front.  Otherwise, as
			 * long as this is not a duplicate name (case
			 * insensitive) add it to the list.
			 */
			if (n || (cp - cp2 > 41))
				/* EMPTY */;
			else if (name && (strncasecmp(name, cp2, cp-cp2) == 0))
				*argv = cp2;
			else if (is_unique(cp2, argv+1, argvp))
				*argvp++ = cp2;
			if (c == ':')
				break;
			/*
			 * Skip multiple delimiters. Reset cp2 to
			 * the beginning of the next name. Reset n,
			 * the flag for names with spaces.
			 */
			while ((c = *cp) == '|')
				cp++;
			cp2 = cp;
			n = 0;
		}
		/*
		 * Skip entries with spaces or non-ascii values.
		 * Convert lower case letters to upper case.
		 */
		if ((c == ' ') || !isascii(c))
			n = 1;
		else if (islower(c))
			*cp = toupper(c);
	}

	/*
	 * Check for an old V6 2 character name.  If the second
	 * name points to the beginning of the buffer, and is
	 * only 2 characters long, move it to the end of the array.
	 */
	if ((argv[1] == buf) && (strlen(argv[1]) == 2)) {
		--argvp;
		for (avt = &argv[1]; avt < argvp; avt++)
			*avt = *(avt+1);
		*argvp++ = buf;
	}

	/*
	 * Duplicate last name, for TTYPE option, and null
	 * terminate the array.  If we didn't find a match on
	 * our terminal name, put that name at the beginning.
	 */
	cp = *(argvp-1);
	*argvp++ = cp;
	*argvp = 0;

	if (*argv == 0) {
		if (name)
			*argv = name;
		else {
			--argvp;
			for (avt = argv; avt < argvp; avt++)
				*avt = *(avt+1);
		}
	}
	if (*argv)
		return (argv);
	else
		return (unknown);
}

static int
is_unique(name, as, ae)
	register char *name, **as, **ae;
{
	register char **ap;
	register int n;

	n = strlen(name) + 1;
	for (ap = as; ap < ae; ap++)
		if (strncasecmp(*ap, name, n) == 0)
			return (0);
	return (1);
}

#define	termbuf	ttytype
extern char ttytype[];

int resettermname = 1;

static char *
gettermname(void)
{
	char *tname;
	static char **tnamep = 0;
	static char **next;
	int err;

	if (resettermname) {
		resettermname = 0;
		if (tnamep && tnamep != unknown)
			free(tnamep);
		tname = (char *)env_getvalue((unsigned char *)"TERM");
		if ((tname != NULL) && (setupterm(tname, 1, &err) == 0)) {
			tnamep = mklist(termbuf, tname);
		} else {
			if (tname && (strlen(tname) <= 40u)) {
				unknown[0] = tname;
				upcase(tname);
			} else
				unknown[0] = name_unknown;
			tnamep = unknown;
		}
		next = tnamep;
	}
	if (*next == 0)
		next = tnamep;
	return (*next++);
}
/*
 * suboption()
 *
 *	Look at the sub-option buffer, and try to be helpful to the other
 * side.
 *
 *	Currently we recognize:
 *
 *		Terminal type, send request.
 *		Terminal speed (send request).
 *		Local flow control (is request).
 *		Linemode
 */

    static void
suboption()
{
	unsigned char subchar;

	printsub('<', subbuffer, SB_LEN()+2);
	switch (subchar = SB_GET()) {
	case TELOPT_TTYPE:
		if (my_want_state_is_wont(TELOPT_TTYPE))
			return;
		if (SB_EOF() || SB_GET() != TELQUAL_SEND) {
			return;
		} else {
			char *name;
			unsigned char temp[50];
			int len, bytes;

			name = gettermname();
			len = strlen(name) + 4 + 2;
			bytes = snprintf((char *)temp, sizeof (temp),
				"%c%c%c%c%s%c%c", IAC, SB,
				TELOPT_TTYPE, TELQUAL_IS, name, IAC, SE);
			if ((len < NETROOM()) && (bytes < sizeof (temp))) {
					ring_supply_data(&netoring, temp, len);
					printsub('>', &temp[2], len-2);
			} else {
				ExitString("No room in buffer for "
				    "terminal type.\n", EXIT_FAILURE);
				/*NOTREACHED*/
			}
		}
		break;
	case TELOPT_TSPEED:
		if (my_want_state_is_wont(TELOPT_TSPEED))
			return;
		if (SB_EOF())
			return;
		if (SB_GET() == TELQUAL_SEND) {
			int ospeed, ispeed;
			unsigned char temp[50];
			int len, bytes;

			TerminalSpeeds(&ispeed, &ospeed);

			bytes = snprintf((char *)temp, sizeof (temp),
			    "%c%c%c%c%d,%d%c%c", IAC, SB,
			    TELOPT_TSPEED, TELQUAL_IS, ospeed, ispeed, IAC, SE);
			len = strlen((char *)temp+4) + 4; /* temp[3] is 0 ... */

			if ((len < NETROOM()) && (bytes < sizeof (temp))) {
				ring_supply_data(&netoring, temp, len);
				printsub('>', temp+2, len - 2);
			}
			else
				(void) printf(
				    "telnet: not enough room in buffer "
				    "for terminal speed option reply\n");
		}
		break;
	case TELOPT_LFLOW:
		if (my_want_state_is_wont(TELOPT_LFLOW))
			return;
		if (SB_EOF())
			return;
		switch (SB_GET()) {
		case LFLOW_RESTART_ANY:
			restartany = 1;
			break;
		case LFLOW_RESTART_XON:
			restartany = 0;
			break;
		case LFLOW_ON:
			localflow = 1;
			break;
		case LFLOW_OFF:
			localflow = 0;
			break;
		default:
			return;
		}
		setcommandmode();
		setconnmode(0);
		break;

	case TELOPT_LINEMODE:
		if (my_want_state_is_wont(TELOPT_LINEMODE))
			return;
		if (SB_EOF())
			return;
		switch (SB_GET()) {
		case WILL:
			lm_will(subpointer, SB_LEN());
			break;
		case WONT:
			lm_wont(subpointer, SB_LEN());
			break;
		case DO:
			lm_do(subpointer, SB_LEN());
			break;
		case DONT:
			lm_dont(subpointer, SB_LEN());
			break;
		case LM_SLC:
			slc(subpointer, SB_LEN());
			break;
		case LM_MODE:
			lm_mode(subpointer, SB_LEN(), 0);
			break;
		default:
			break;
		}
		break;

#ifdef	OLD_ENVIRON
	case TELOPT_OLD_ENVIRON:
#endif
	case TELOPT_NEW_ENVIRON:
		if (SB_EOF())
			return;
		switch (SB_PEEK()) {
		case TELQUAL_IS:
		case TELQUAL_INFO:
			if (my_want_state_is_dont(subchar))
				return;
			break;
		case TELQUAL_SEND:
			if (my_want_state_is_wont(subchar)) {
				return;
			}
			break;
		default:
			return;
		}
		env_opt(subpointer, SB_LEN());
		break;

	case TELOPT_XDISPLOC:
		if (my_want_state_is_wont(TELOPT_XDISPLOC))
			return;
		if (SB_EOF())
			return;
		if (SB_GET() == TELQUAL_SEND) {
			unsigned char temp[50], *dp;
			int len, bytes;

			if ((dp = env_getvalue((unsigned char *)"DISPLAY")) ==
			    NULL) {
				/*
				 * Something happened, we no longer have a
				 * DISPLAY variable.  So, turn off the option.
				 */
				send_wont(TELOPT_XDISPLOC, 1);
				break;
			}
			bytes = snprintf((char *)temp, sizeof (temp),
			    "%c%c%c%c%s%c%c", IAC, SB,
			    TELOPT_XDISPLOC, TELQUAL_IS, dp, IAC, SE);
			len = strlen((char *)temp+4) + 4; /* temp[3] is 0 ... */

			if ((len < NETROOM()) && (bytes < sizeof (temp))) {
				ring_supply_data(&netoring, temp, len);
				printsub('>', temp+2, len - 2);
			}
			else
				(void) printf(
				    "telnet: not enough room in buffer"
				    " for display location option reply\n");
		}
		break;

	case TELOPT_AUTHENTICATION: {
		if (!autologin)
			break;
		if (SB_EOF())
			return;
		switch (SB_GET()) {
		case TELQUAL_SEND:
			if (my_want_state_is_wont(TELOPT_AUTHENTICATION))
				return;
			auth_send(subpointer, SB_LEN());
			break;
		case TELQUAL_REPLY:
			if (my_want_state_is_wont(TELOPT_AUTHENTICATION))
				return;
			auth_reply(subpointer, SB_LEN());
			break;
		}
	}
	break;

	case TELOPT_ENCRYPT:
		if (SB_EOF())
			return;
		switch (SB_GET()) {
		case ENCRYPT_START:
			if (my_want_state_is_dont(TELOPT_ENCRYPT))
				return;
			encrypt_start(subpointer, SB_LEN());
			break;
		case ENCRYPT_END:
			if (my_want_state_is_dont(TELOPT_ENCRYPT))
				return;
			encrypt_end();
			break;
		case ENCRYPT_SUPPORT:
			if (my_want_state_is_wont(TELOPT_ENCRYPT))
				return;
			encrypt_support(subpointer, SB_LEN());
			break;
		case ENCRYPT_REQSTART:
			if (my_want_state_is_wont(TELOPT_ENCRYPT))
				return;
			encrypt_request_start(subpointer, SB_LEN());
			break;
		case ENCRYPT_REQEND:
			if (my_want_state_is_wont(TELOPT_ENCRYPT))
				return;
			/*
			 * We can always send an REQEND so that we cannot
			 * get stuck encrypting.  We should only get this
			 * if we have been able to get in the correct mode
			 * anyhow.
			 */
			encrypt_request_end();
			break;
		case ENCRYPT_IS:
			if (my_want_state_is_dont(TELOPT_ENCRYPT))
				return;
			encrypt_is(subpointer, SB_LEN());
			break;
		case ENCRYPT_REPLY:
			if (my_want_state_is_wont(TELOPT_ENCRYPT))
				return;
			encrypt_reply(subpointer, SB_LEN());
			break;
		case ENCRYPT_ENC_KEYID:
			if (my_want_state_is_dont(TELOPT_ENCRYPT))
				return;
			encrypt_enc_keyid(subpointer, SB_LEN());
			break;
		case ENCRYPT_DEC_KEYID:
			if (my_want_state_is_wont(TELOPT_ENCRYPT))
				return;
			encrypt_dec_keyid(subpointer, SB_LEN());
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static unsigned char str_lm[] = { IAC, SB, TELOPT_LINEMODE, 0, 0, IAC, SE };

static void
lm_will(cmd, len)
	unsigned char *cmd;
	int len;
{
	if (len < 1) {
		/* Should not happen... */
		(void) printf(
		    "telnet: command missing from linemode WILL request\n");
		return;
	}
	switch (cmd[0]) {
	case LM_FORWARDMASK:	/* We shouldn't ever get this... */
	default:
		str_lm[3] = DONT;
		str_lm[4] = cmd[0];
		if (NETROOM() > sizeof (str_lm)) {
			ring_supply_data(&netoring, str_lm, sizeof (str_lm));
			printsub('>', &str_lm[2], sizeof (str_lm)-2);
		}
		else
			(void) printf("telnet: not enough room in buffer for"
			    "reply to linemode WILL request\n");
		break;
	}
}

static void
lm_wont(cmd, len)
	unsigned char *cmd;
	int len;
{
	if (len < 1) {
		/* Should not happen... */
		(void) printf(
		    "telnet: command missing from linemode WONT request\n");
		return;
	}
	switch (cmd[0]) {
	case LM_FORWARDMASK:	/* We shouldn't ever get this... */
	default:
		/* We are always DONT, so don't respond */
		return;
	}
}

static void
lm_do(cmd, len)
	unsigned char *cmd;
	int len;
{
	if (len < 1) {
		/* Should not happen... */
		(void) printf(
		    "telnet: command missing from linemode DO request\n");
		return;
	}
	switch (cmd[0]) {
	case LM_FORWARDMASK:
	default:
		str_lm[3] = WONT;
		str_lm[4] = cmd[0];
		if (NETROOM() > sizeof (str_lm)) {
			ring_supply_data(&netoring, str_lm, sizeof (str_lm));
			printsub('>', &str_lm[2], sizeof (str_lm)-2);
		}
		else
			(void) printf("telnet: not enough room in buffer for"
			    "reply to linemode DO request\n");
		break;
	}
}

static void
lm_dont(cmd, len)
	unsigned char *cmd;
	int len;
{
	if (len < 1) {
		/* Should not happen... */
		(void) printf(
		    "telnet: command missing from linemode DONT request\n");
		return;
	}
	switch (cmd[0]) {
	case LM_FORWARDMASK:
	default:
		/* we are always WONT, so don't respond */
		break;
	}
}

static unsigned char str_lm_mode[] = {
	IAC, SB, TELOPT_LINEMODE, LM_MODE, 0, IAC, SE
};

	void
lm_mode(cmd, len, init)
	unsigned char *cmd;
	int len, init;
{
	if (len != 1)
		return;
	if ((linemode&MODE_MASK&~MODE_ACK) == *cmd)
		return;
	linemode = *cmd&(MODE_MASK&~MODE_ACK);
	str_lm_mode[4] = linemode;
	if (!init)
		str_lm_mode[4] |= MODE_ACK;
	if (NETROOM() > sizeof (str_lm_mode)) {
		ring_supply_data(&netoring, str_lm_mode, sizeof (str_lm_mode));
		printsub('>', &str_lm_mode[2], sizeof (str_lm_mode)-2);
	}
	else
		(void) printf("telnet: not enough room in buffer for"
		    "reply to linemode request\n");
	setconnmode(0);	/* set changed mode */
}



/*
 * slc()
 * Handle special character suboption of LINEMODE.
 */

static struct spc {
	cc_t val;
	cc_t *valp;
	char flags;	/* Current flags & level */
	char mylevel;	/* Maximum level & flags */
} spc_data[NSLC+1];

#define	SLC_IMPORT	0
#define	SLC_EXPORT	1
#define	SLC_RVALUE	2
static int slc_mode = SLC_EXPORT;

static void
slc_init()
{
	register struct spc *spcp;

	localchars = 1;
	for (spcp = spc_data; spcp < &spc_data[NSLC+1]; spcp++) {
		spcp->val = 0;
		spcp->valp = 0;
		spcp->flags = spcp->mylevel = SLC_NOSUPPORT;
	}

#define	initfunc(func, flags) { \
				spcp = &spc_data[func]; \
				if (spcp->valp = tcval(func)) { \
				    spcp->val = *spcp->valp; \
				    spcp->mylevel = SLC_VARIABLE|(flags);\
				} else { \
				    spcp->val = 0; \
				    spcp->mylevel = SLC_DEFAULT; \
				} \
			    }

	initfunc(SLC_SYNCH, 0);
	/* No BRK */
	initfunc(SLC_AO, 0);
	initfunc(SLC_AYT, 0);
	/* No EOR */
	initfunc(SLC_ABORT, SLC_FLUSHIN|SLC_FLUSHOUT);
	initfunc(SLC_EOF, 0);
	initfunc(SLC_SUSP, SLC_FLUSHIN);
	initfunc(SLC_EC, 0);
	initfunc(SLC_EL, 0);
	initfunc(SLC_EW, 0);
	initfunc(SLC_RP, 0);
	initfunc(SLC_LNEXT, 0);
	initfunc(SLC_XON, 0);
	initfunc(SLC_XOFF, 0);
	initfunc(SLC_FORW1, 0);
#ifdef	USE_TERMIO
	initfunc(SLC_FORW2, 0);
	/* No FORW2 */
#endif

	initfunc(SLC_IP, SLC_FLUSHIN|SLC_FLUSHOUT);
#undef	initfunc

	if (slc_mode == SLC_EXPORT)
		slc_export();
	else
		slc_import(1);

}

void
slcstate()
{
	(void) printf("Special characters are %s values\n",
		slc_mode == SLC_IMPORT ? "remote default" :
		slc_mode == SLC_EXPORT ? "local" :
					"remote");
}

void
slc_mode_export()
{
	slc_mode = SLC_EXPORT;
	if (my_state_is_will(TELOPT_LINEMODE))
		slc_export();
}

void
slc_mode_import(def)
    int def;
{
    slc_mode = def ? SLC_IMPORT : SLC_RVALUE;
    if (my_state_is_will(TELOPT_LINEMODE))
	slc_import(def);
}

static unsigned char slc_import_val[] = {
	IAC, SB, TELOPT_LINEMODE, LM_SLC, 0, SLC_VARIABLE, 0, IAC, SE
};
static unsigned char slc_import_def[] = {
	IAC, SB, TELOPT_LINEMODE, LM_SLC, 0, SLC_DEFAULT, 0, IAC, SE
};

static void
slc_import(def)
	int def;
{
	if (NETROOM() > sizeof (slc_import_val)) {
		if (def) {
			ring_supply_data(&netoring, slc_import_def,
			    sizeof (slc_import_def));
			printsub('>', &slc_import_def[2],
			    sizeof (slc_import_def)-2);
		} else {
			ring_supply_data(&netoring, slc_import_val,
			    sizeof (slc_import_val));
			printsub('>', &slc_import_val[2],
			    sizeof (slc_import_val)-2);
		}
	}
	else
		(void) printf(
		    "telnet: not enough room in buffer for slc import"
		    " request\n");
}

static uchar_t *slc_reply = NULL;
static uchar_t *slc_replyp = NULL;
/*
 * The SLC reply consists of: IAC, SB, TELOPT_LINEMODE, LM_SLC,
 * SLC triplets[], IAC, SE. i.e. it has a 'wrapper' of 6 control characters.
 */
#define	SLC_WRAPPER_SIZE 6

static void
slc_export()
{
	register struct spc *spcp;

	TerminalDefaultChars();

	slc_start_reply(NSLC * 3);	/* 3 bytes needed per triplet */
	for (spcp = &spc_data[1]; spcp < &spc_data[NSLC+1]; spcp++) {
		if (spcp->mylevel != SLC_NOSUPPORT) {
			if (spcp->val == (cc_t)(_POSIX_VDISABLE))
				spcp->flags = SLC_NOSUPPORT;
			else
				spcp->flags = spcp->mylevel;
			if (spcp->valp)
				spcp->val = *spcp->valp;
			slc_add_reply(spcp - spc_data, spcp->flags, spcp->val);
		}
	}
	slc_end_reply();
	(void) slc_update();
	setconnmode(1);	/* Make sure the character values are set */
}

static void
slc(cp, len)
	register unsigned char *cp;
	int len;
{
	register struct spc *spcp;
	register int func, level;

	slc_start_reply(len);

	for (; len >= 3; len -= 3, cp += 3) {

		func = cp[SLC_FUNC];

		if (func == 0) {
			/*
			 * Client side: always ignore 0 function.
			 */
			continue;
		}
		if (func > NSLC) {
			if ((cp[SLC_FLAGS] & SLC_LEVELBITS) != SLC_NOSUPPORT)
				slc_add_reply(func, SLC_NOSUPPORT, 0);
			continue;
		}

		spcp = &spc_data[func];

		level = cp[SLC_FLAGS]&(SLC_LEVELBITS|SLC_ACK);

		if ((cp[SLC_VALUE] == (unsigned char)spcp->val) &&
		    ((level&SLC_LEVELBITS) == (spcp->flags&SLC_LEVELBITS))) {
			continue;
		}

		if (level == (SLC_DEFAULT|SLC_ACK)) {
			/*
			 * This is an error condition, the SLC_ACK
			 * bit should never be set for the SLC_DEFAULT
			 * level.  Our best guess to recover is to
			 * ignore the SLC_ACK bit.
			 */
			cp[SLC_FLAGS] &= ~SLC_ACK;
		}

		if (level == ((spcp->flags&SLC_LEVELBITS)|SLC_ACK)) {
			spcp->val = (cc_t)cp[SLC_VALUE];
			spcp->flags = cp[SLC_FLAGS];	/* include SLC_ACK */
			continue;
		}

		level &= ~SLC_ACK;

		if (level <= (spcp->mylevel&SLC_LEVELBITS)) {
			spcp->flags = cp[SLC_FLAGS]|SLC_ACK;
			spcp->val = (cc_t)cp[SLC_VALUE];
		}
		if (level == SLC_DEFAULT) {
			if ((spcp->mylevel&SLC_LEVELBITS) != SLC_DEFAULT)
				spcp->flags = spcp->mylevel;
			else
				spcp->flags = SLC_NOSUPPORT;
		}
		slc_add_reply(func, spcp->flags, spcp->val);
	}
	slc_end_reply();
	if (slc_update())
		setconnmode(1);	/* set the  new character values */
}

void
slc_check()
{
	register struct spc *spcp;

	slc_start_reply(NSLC * 3);	/* 3 bytes needed per triplet */
	for (spcp = &spc_data[1]; spcp < &spc_data[NSLC+1]; spcp++) {
		if (spcp->valp && spcp->val != *spcp->valp) {
			spcp->val = *spcp->valp;
			if (spcp->val == (cc_t)(_POSIX_VDISABLE))
				spcp->flags = SLC_NOSUPPORT;
			else
				spcp->flags = spcp->mylevel;
			slc_add_reply(spcp - spc_data, spcp->flags, spcp->val);
		}
	}
	slc_end_reply();
	setconnmode(1);
}

static void
slc_start_reply(size_t len)
{
	/*
	 * SLC triplets may contain escaped characters, allow for
	 * worst case by allocating 2 bytes for every character.
	 */
	slc_reply = realloc(slc_reply, (len * 2) + SLC_WRAPPER_SIZE);
	if (slc_reply == NULL) {
		fprintf(stderr, "telnet: error allocating SLC reply memory\n");
		return;
	}
	slc_replyp = slc_reply;
	*slc_replyp++ = IAC;
	*slc_replyp++ = SB;
	*slc_replyp++ = TELOPT_LINEMODE;
	*slc_replyp++ = LM_SLC;
}

static void
slc_add_reply(unsigned char func, unsigned char flags, cc_t value)
{
	if ((*slc_replyp++ = func) == IAC)
		*slc_replyp++ = IAC;
	if ((*slc_replyp++ = flags) == IAC)
		*slc_replyp++ = IAC;
	if ((*slc_replyp++ = (unsigned char)value) == IAC)
		*slc_replyp++ = IAC;
}

static void
slc_end_reply()
{
	register int len;

	*slc_replyp++ = IAC;
	*slc_replyp++ = SE;
	len = slc_replyp - slc_reply;
	if (len <= SLC_WRAPPER_SIZE)
		return;
	if (NETROOM() > len) {
		ring_supply_data(&netoring, slc_reply, slc_replyp - slc_reply);
		printsub('>', &slc_reply[2], slc_replyp - slc_reply - 2);
	}
	else
		(void) printf("telnet: not enough room in buffer for slc end "
		    "reply\n");
}

static int
slc_update()
{
	register struct spc *spcp;
	int need_update = 0;

	for (spcp = &spc_data[1]; spcp < &spc_data[NSLC+1]; spcp++) {
		if (!(spcp->flags&SLC_ACK))
			continue;
		spcp->flags &= ~SLC_ACK;
		if (spcp->valp && (*spcp->valp != spcp->val)) {
			*spcp->valp = spcp->val;
			need_update = 1;
		}
	}
	return (need_update);
}

#ifdef	OLD_ENVIRON
#ifdef	ENV_HACK
/*
 * Earlier version of telnet/telnetd from the BSD code had
 * the definitions of VALUE and VAR reversed.  To ensure
 * maximum interoperability, we assume that the server is
 * an older BSD server, until proven otherwise.  The newer
 * BSD servers should be able to handle either definition,
 * so it is better to use the wrong values if we don't
 * know what type of server it is.
 */
int env_auto = 1;
int old_env_var = OLD_ENV_VAR;
int old_env_value = OLD_ENV_VALUE;
#else
#define	old_env_var	OLD_ENV_VAR
#define	old_env_value	OLD_ENV_VALUE
#endif
#endif

static void
env_opt(buf, len)
	register unsigned char *buf;
	register int len;
{
	register unsigned char *ep = 0, *epc = 0;
	register int i;

	switch (buf[0]&0xff) {
	case TELQUAL_SEND:
		env_opt_start();
		if (len == 1) {
			env_opt_add(NULL);
		} else for (i = 1; i < len; i++) {
			switch (buf[i]&0xff) {
#ifdef	OLD_ENVIRON
			case OLD_ENV_VAR:
#ifdef	ENV_HACK
				if (telopt_environ == TELOPT_OLD_ENVIRON &&
				    env_auto) {
					/* Server has the same definitions */
					old_env_var = OLD_ENV_VAR;
					old_env_value = OLD_ENV_VALUE;
				}
				/* FALLTHROUGH */
#endif
			case OLD_ENV_VALUE:
				/*
				 * Although OLD_ENV_VALUE is not legal, we will
				 * still recognize it, just in case it is an
				 * old server that has VAR & VALUE mixed up...
				 */
				/* FALLTHROUGH */
#else
			case NEW_ENV_VAR:
#endif
			case ENV_USERVAR:
				if (ep) {
					*epc = 0;
					env_opt_add(ep);
				}
				ep = epc = &buf[i+1];
				break;
			case ENV_ESC:
				i++;
				/*FALLTHROUGH*/
			default:
				if (epc)
					*epc++ = buf[i];
				break;
			}
		}
		if (ep) {
			*epc = 0;
			env_opt_add(ep);
		}
		env_opt_end(1);
		break;

	case TELQUAL_IS:
	case TELQUAL_INFO:
		/* Ignore for now.  We shouldn't get it anyway. */
		break;

	default:
		break;
	}
}

static unsigned char *opt_reply;
static unsigned char *opt_replyp;
static unsigned char *opt_replyend;
#define	OPT_REPLY_INITIAL_SIZE	256
/*
 * The opt reply consists of: IAC, SB, telopt_environ, TELQUAL_IS,
 * value, IAC, SE. i.e. it has a 'wrapper' of 6 control characters.
 */
#define	OPT_WRAPPER_SIZE 6

static void
env_opt_start()
{
	opt_reply = realloc(opt_reply, OPT_REPLY_INITIAL_SIZE);
	if (opt_reply == NULL) {
		(void) printf(
		    "telnet: error allocating environment option memory\n");
		opt_reply = opt_replyp = opt_replyend = NULL;
		return;
	}
	opt_replyp = opt_reply;
	opt_replyend = opt_reply + OPT_REPLY_INITIAL_SIZE;
	*opt_replyp++ = IAC;
	*opt_replyp++ = SB;
	*opt_replyp++ = telopt_environ;
	*opt_replyp++ = TELQUAL_IS;
}

	void
env_opt_start_info()
{
	env_opt_start();
	if (opt_replyp)
	    opt_replyp[-1] = TELQUAL_INFO;
}

	void
env_opt_add(ep)
	register unsigned char *ep;
{
	register unsigned char *vp, c;
	int opt_reply_size;
	int opt_reply_used;

	if (opt_reply == NULL)		/* XXX */
		return;			/* XXX */

	if (ep == NULL || *ep == '\0') {
		/* Send user defined variables first. */
		(void) env_default(1, 0);
		while (ep = env_default(0, 0))
			env_opt_add(ep);

		/* Now add the list of well know variables.  */
		(void) env_default(1, 1);
		while (ep = env_default(0, 1))
			env_opt_add(ep);
		return;
	}
	vp = env_getvalue(ep);

	/*
	 * Calculate space required for opt_reply and allocate more if required.
	 * Assume worst case that every character is escaped, so needs 2 bytes.
	 */
	opt_reply_used = opt_replyp - opt_reply;	/* existing contents */
	opt_reply_size = opt_reply_used + OPT_WRAPPER_SIZE +
	    (2 * (strlen((char *)ep))) +
	    (vp == NULL ? 0 : (2 * strlen((char *)vp)));

	if (opt_reply_size > (opt_replyend - opt_reply)) {
		opt_reply = realloc(opt_reply, opt_reply_size);
		if (opt_reply == NULL) {
			(void) printf(
			    "telnet: can't allocate environment option "
			    "reply\n");
			opt_reply = opt_replyp = opt_replyend = NULL;
			return;
		}
		opt_replyp = opt_reply + opt_reply_used;
		opt_replyend = opt_reply + opt_reply_size;
	}

	if (opt_welldefined((char *)ep))
#ifdef	OLD_ENVIRON
		if (telopt_environ == TELOPT_OLD_ENVIRON)
			*opt_replyp++ = old_env_var;
		else
#endif
			*opt_replyp++ = NEW_ENV_VAR;
	else
		*opt_replyp++ = ENV_USERVAR;
	for (;;) {
		while ((c = *ep++) != NULL) {
			switch (c&0xff) {
			case IAC:
				*opt_replyp++ = IAC;
				break;
			case NEW_ENV_VAR:
			case NEW_ENV_VALUE:
			case ENV_ESC:
			case ENV_USERVAR:
				*opt_replyp++ = ENV_ESC;
				break;
			}
			*opt_replyp++ = c;
		}
		if ((ep = vp) != NULL) {
#ifdef	OLD_ENVIRON
			if (telopt_environ == TELOPT_OLD_ENVIRON)
				*opt_replyp++ = old_env_value;
			else
#endif
				*opt_replyp++ = NEW_ENV_VALUE;
			vp = NULL;
		} else
			break;
	}
}

	int
opt_welldefined(ep)
	char *ep;
{
	if ((strcmp(ep, "USER") == 0) ||
	    (strcmp(ep, "DISPLAY") == 0) ||
	    (strcmp(ep, "PRINTER") == 0) ||
	    (strcmp(ep, "SYSTEMTYPE") == 0) ||
	    (strcmp(ep, "JOB") == 0) ||
	    (strcmp(ep, "ACCT") == 0))
		return (1);
	return (0);
}
	void
env_opt_end(emptyok)
	register int emptyok;
{
	register int len;

	len = opt_replyp - opt_reply + 2;
	if (emptyok || len > OPT_WRAPPER_SIZE) {
		*opt_replyp++ = IAC;
		*opt_replyp++ = SE;
		if (NETROOM() > len) {
			ring_supply_data(&netoring, opt_reply, len);
			printsub('>', &opt_reply[2], len - 2);
		}
		else
			(void) printf("telnet: not enough room in buffer for "
			    "environment option end reply\n");
	}
	if (opt_reply) {
		free(opt_reply);
		opt_reply = opt_replyp = opt_replyend = NULL;
	}
}



int
telrcv()
{
	register int c;
	register int scc;
	register unsigned char *sbp;
	int count;
	int returnValue = 0;
	int min_room = 0;

	scc = 0;
	count = 0;
	while (--min_room > 2 || (min_room = TTYROOM()) > 2) {
		if (scc == 0) {
			if (count) {
				ring_consumed(&netiring, count);
				returnValue = 1;
				count = 0;
			}
			sbp = netiring.consume;
			scc = ring_full_consecutive(&netiring);
			if (scc == 0) {
				/* No more data coming in */
				break;
			}
		}

		c = *sbp++ & 0xff, scc--; count++;

		if (decrypt_input)
			c = (*decrypt_input)(c);

		switch (telrcv_state) {

		case TS_CR:
			telrcv_state = TS_DATA;
			if (c == '\0') {
				break;	/* Ignore \0 after CR */
			} else if ((c == '\n') &&
			    my_want_state_is_dont(TELOPT_ECHO) && !crmod) {
				TTYADD(c);
				break;
			}
			/* FALLTHROUGH */

		case TS_DATA:
			if (c == IAC) {
				telrcv_state = TS_IAC;
				break;
			}
			/*
			 * The 'crmod' hack (see following) is needed
			 * since we can't * set CRMOD on output only.
			 * Machines like MULTICS like to send \r without
			 * \n; since we must turn off CRMOD to get proper
			 * input, the mapping is done here (sigh).
			 */
			if ((c == '\r') &&
			    my_want_state_is_dont(TELOPT_BINARY)) {
				if (scc > 0) {
					c = *sbp&0xff;

					if (decrypt_input)
						c = (*decrypt_input)(c);

					if (c == 0) {
						sbp++, scc--; count++;
						/* a "true" CR */
						TTYADD('\r');
					} else if (my_want_state_is_dont(
					    TELOPT_ECHO) && (c == '\n')) {
						sbp++, scc--; count++;
						TTYADD('\n');
					} else {

						if (decrypt_input)
							(*decrypt_input)(-1);

						TTYADD('\r');
						if (crmod) {
							TTYADD('\n');
						}
					}
				} else {
					telrcv_state = TS_CR;
					TTYADD('\r');
					if (crmod) {
						TTYADD('\n');
					}
				}
			} else {
				TTYADD(c);
			}
			continue;

		case TS_IAC:
process_iac:
			switch (c) {

			case WILL:
				telrcv_state = TS_WILL;
				continue;

			case WONT:
				telrcv_state = TS_WONT;
				continue;

			case DO:
				telrcv_state = TS_DO;
				continue;

			case DONT:
				telrcv_state = TS_DONT;
				continue;

			case DM:
				/*
				 * We may have missed an urgent notification,
				 * so make sure we flush whatever is in the
				 * buffer currently.
				 */
				printoption("RCVD", IAC, DM);
				SYNCHing = 1;
				if (ttyflush(1) == -2) {
					/* This will not return. */
					fatal_tty_error("write");
				}
				SYNCHing = stilloob();
				settimer(gotDM);
				break;

			case SB:
				SB_CLEAR();
				telrcv_state = TS_SB;
				continue;

			case IAC:
				TTYADD(IAC);
				break;

			case NOP:
			case GA:
			default:
				printoption("RCVD", IAC, c);
				break;
			}
			telrcv_state = TS_DATA;
			continue;

		case TS_WILL:
			printoption("RCVD", WILL, c);
			willoption(c);
			telrcv_state = TS_DATA;
			continue;

		case TS_WONT:
			printoption("RCVD", WONT, c);
			wontoption(c);
			telrcv_state = TS_DATA;
			continue;

		case TS_DO:
			printoption("RCVD", DO, c);
			dooption(c);
			if (c == TELOPT_NAWS) {
				sendnaws();
			} else if (c == TELOPT_LFLOW) {
				localflow = 1;
				setcommandmode();
				setconnmode(0);
			}
			telrcv_state = TS_DATA;
			continue;

		case TS_DONT:
			printoption("RCVD", DONT, c);
			dontoption(c);
			flushline = 1;
			setconnmode(0);	/* set new tty mode (maybe) */
			telrcv_state = TS_DATA;
			continue;

		case TS_SB:
			if (c == IAC) {
				telrcv_state = TS_SE;
			} else {
				SB_ACCUM(c);
			}
			continue;

		case TS_SE:
			if (c != SE) {
				if (c != IAC) {
			/*
			 * This is an error.  We only expect to get
			 * "IAC IAC" or "IAC SE".  Several things may
			 * have happend.  An IAC was not doubled, the
			 * IAC SE was left off, or another option got
			 * inserted into the suboption are all possibilities.
			 * If we assume that the IAC was not doubled,
			 * and really the IAC SE was left off, we could
			 * get into an infinate loop here.  So, instead,
			 * we terminate the suboption, and process the
			 * partial suboption if we can.
			 */
					SB_ACCUM(IAC);
					SB_ACCUM(c);
					subpointer -= 2;
					SB_TERM();

					printoption("In SUBOPTION processing, "
					    "RCVD", IAC, c);
					suboption();	/* handle sub-option */
					telrcv_state = TS_IAC;
					goto process_iac;
				}
				SB_ACCUM(c);
				telrcv_state = TS_SB;
			} else {
				SB_ACCUM(IAC);
				SB_ACCUM(SE);
				subpointer -= 2;
				SB_TERM();
				suboption();	/* handle sub-option */
				telrcv_state = TS_DATA;
			}
		}
	}
	if (count)
		ring_consumed(&netiring, count);
	return (returnValue||count);
}

static int bol = 1, local = 0;

int
rlogin_susp()
{
	if (local) {
		local = 0;
		bol = 1;
		command(0, "z\n", 2);
		return (1);
	}
	return (0);
}

static int
telsnd()
{
	int tcc;
	int count;
	int returnValue = 0;
	unsigned char *tbp;

	tcc = 0;
	count = 0;
	while (NETROOM() > 2) {
		register int sc;
		register int c;

		if (tcc == 0) {
			if (count) {
				ring_consumed(&ttyiring, count);
				returnValue = 1;
				count = 0;
			}
			tbp = ttyiring.consume;
			tcc = ring_full_consecutive(&ttyiring);
			if (tcc == 0) {
				break;
			}
		}
		c = *tbp++ & 0xff, sc = strip(c), tcc--; count++;
		if (rlogin != _POSIX_VDISABLE) {
			if (bol) {
				bol = 0;
				if (sc == rlogin) {
					local = 1;
					continue;
				}
			} else if (local) {
				local = 0;
				if (sc == '.' || c == termEofChar) {
					bol = 1;
					command(0, "close\n", 6);
					continue;
				}
				if (sc == termSuspChar) {
					bol = 1;
					command(0, "z\n", 2);
					continue;
				}
				if (sc == escape) {
					command(0, (char *)tbp, tcc);
					bol = 1;
					count += tcc;
					tcc = 0;
					flushline = 1;
					break;
				}
				if (sc != rlogin) {
					++tcc;
					--tbp;
					--count;
					c = sc = rlogin;
				}
			}
			if ((sc == '\n') || (sc == '\r'))
				bol = 1;
		} else if (sc == escape && escape_valid) {
			/*
			 * Double escape is a pass through of a single
			 * escape character.
			 */
			if (tcc && strip(*tbp) == escape) {
				tbp++;
				tcc--;
				count++;
				bol = 0;
			} else {
				command(0, (char *)tbp, tcc);
				bol = 1;
				count += tcc;
				tcc = 0;
				flushline = 1;
				break;
			}
		} else
			bol = 0;
#ifdef	KLUDGELINEMODE
		if (kludgelinemode && (globalmode&MODE_EDIT) && (sc == echoc)) {
			if (tcc > 0 && strip(*tbp) == echoc) {
				tcc--; tbp++; count++;
			} else {
				dontlecho = !dontlecho;
				settimer(echotoggle);
				setconnmode(0);
				flushline = 1;
				break;
			}
		}
#endif
		if (MODE_LOCAL_CHARS(globalmode)) {
			if (TerminalSpecialChars(sc) == 0) {
				bol = 1;
				break;
			}
		}
		if (my_want_state_is_wont(TELOPT_BINARY)) {
			switch (c) {
			case '\n':
				/*
				 * If we are in CRMOD mode (\r ==> \n)
				 * on our local machine, then probably
				 * a newline (unix) is CRLF (TELNET).
				 */
				if (MODE_LOCAL_CHARS(globalmode)) {
					NETADD('\r');
				}
				NETADD('\n');
				bol = flushline = 1;
				break;
			case '\r':
				if (!crlf) {
					NET2ADD('\r', '\0');
				} else {
					NET2ADD('\r', '\n');
				}
				bol = flushline = 1;
				break;
			case IAC:
				NET2ADD(IAC, IAC);
				break;
			default:
				NETADD(c);
				break;
			}
		} else if (c == IAC) {
			NET2ADD(IAC, IAC);
		} else {
			NETADD(c);
		}
	}
	if (count)
		ring_consumed(&ttyiring, count);
	return (returnValue||count);	/* Non-zero if we did anything */
}

/*
 * Scheduler()
 *
 * Try to do something.
 *
 * If we do something useful, return 1; else return 0.
 *
 */


int
Scheduler(block)
	int	block;		/* should we block in the select ? */
{
	/*
	 * One wants to be a bit careful about setting returnValue
	 * to one, since a one implies we did some useful work,
	 * and therefore probably won't be called to block next
	 * time (TN3270 mode only).
	 */
	int returnValue;
	int netin, netout, netex, ttyin, ttyout;

	/* Decide which rings should be processed */

	netout = ring_full_count(&netoring) &&
	    (flushline ||
	    (my_want_state_is_wont(TELOPT_LINEMODE)
#ifdef	KLUDGELINEMODE
	    /* X */ && (!kludgelinemode || my_want_state_is_do(TELOPT_SGA))
#endif
	    /* XXX */) ||
	    my_want_state_is_will(TELOPT_BINARY));
	ttyout = ring_full_count(&ttyoring);

	ttyin = (ring_empty_count(&ttyiring) && !eof_pending);

	netin = !ISend && ring_empty_count(&netiring);

	netex = !SYNCHing;

	if (scheduler_lockout_tty) {
		ttyin = ttyout = 0;
	}

	/* Call to system code to process rings */

	returnValue = process_rings(netin, netout, netex, ttyin, ttyout,
	    !block);

	/* Now, look at the input rings, looking for work to do. */

	if (ring_full_count(&ttyiring)) {
		returnValue |= telsnd();
	} else {
		/*
		 * If ttyiring is empty, check to see if there is a real EOF
		 * pending.  If so, we can maybe do the EOF write now.
		 */
		if (eof_pending) {
			eof_pending = 0;
			sendeof();
		}
	}

	if (ring_full_count(&netiring)) {
		returnValue |= telrcv();
	}
	return (returnValue);
}

/*
 * Select from tty and network...
 */
void
telnet(user)
	char *user;
{
	sys_telnet_init();

	{
		static char local_host[MAXHOSTNAMELEN] = { 0 };

		if (!local_host[0]) {
			(void) gethostname(local_host, sizeof (local_host));
			local_host[sizeof (local_host)-1] = 0;
		}
		auth_encrypt_init(local_host, hostname, "TELNET");
		auth_encrypt_user(user);
	}

	if (autologin)
		send_will(TELOPT_AUTHENTICATION, 1);

	if (telnetport || wantencryption) {
		send_do(TELOPT_ENCRYPT, 1);
		send_will(TELOPT_ENCRYPT, 1);
	}

	if (telnetport) {
		if (!reqd_linemode)
		    send_do(TELOPT_SGA, 1);
		send_will(TELOPT_TTYPE, 1);
		send_will(TELOPT_NAWS, 1);
		send_will(TELOPT_TSPEED, 1);
		send_will(TELOPT_LFLOW, 1);
		if (!reqd_linemode)
		    send_will(TELOPT_LINEMODE, 1);
		send_will(TELOPT_NEW_ENVIRON, 1);
		send_do(TELOPT_STATUS, 1);
		if (env_getvalue((unsigned char *)"DISPLAY"))
			send_will(TELOPT_XDISPLOC, 1);
		if (eight)
			tel_enter_binary(eight);
	}

	/*
	 * Note: we assume a tie to the authentication option here.  This
	 * is necessary so that authentication fails, we don't spin
	 * forever.
	 */
	if (wantencryption) {
		boolean_t printed_encrypt = B_FALSE;
		extern boolean_t auth_has_failed;
		time_t timeout = time(0) + 60;

		send_do(TELOPT_ENCRYPT, 1);
		send_will(TELOPT_ENCRYPT, 1);
		for (;;) {
		    if (my_want_state_is_wont(TELOPT_AUTHENTICATION)) {
			(void) printf(gettext(
				"\nServer refused to negotiate "
				"authentication, which is required\n"
				"for encryption.  Good-bye.\n\r"));
			Exit(EXIT_FAILURE);
		    }
		    if (auth_has_failed) {
			(void) printf(gettext(
			    "\nAuthentication negotation has failed, "
			    "which is required for\n"
			    "encryption.  Good-bye.\n\r"));
			Exit(EXIT_FAILURE);
		    }
		    if (my_want_state_is_dont(TELOPT_ENCRYPT) ||
			my_want_state_is_wont(TELOPT_ENCRYPT)) {
			    (void) printf(gettext(
				"\nServer refused to negotiate encryption.  "
				"Good-bye.\n\r"));
			    Exit(EXIT_FAILURE);
		    }
		    if (encrypt_is_encrypting())
			break;

		    if (time(0) > timeout) {
			(void) printf(gettext(
				"\nEncryption could not be enabled.  "
				"Good-bye.\n\r"));
			Exit(EXIT_FAILURE);
		    }
		    if (printed_encrypt == B_FALSE) {
			printed_encrypt = B_TRUE;
			(void) printf(gettext(
			    "Waiting for encryption to be negotiated...\n"));
			/*
			 * Turn on MODE_TRAPSIG and then turn off localchars
			 * so that ^C will cause telnet to exit.
			 */
			TerminalNewMode(getconnmode()|MODE_TRAPSIG);
			intr_waiting = 1;
		    }
		    if (intr_happened) {
			(void) printf(gettext(
			    "\nUser requested an interrupt.  Good-bye.\n\r"));
			Exit(EXIT_FAILURE);
		    }
		    telnet_spin();
		}
		if (printed_encrypt) {
			(void) printf(gettext("done.\n"));
			intr_waiting = 0;
			setconnmode(0);
		}
	}

	for (;;) {
		int schedValue;

		while ((schedValue = Scheduler(0)) != 0) {
			if (schedValue == -1) {
				setcommandmode();
				return;
			}
		}

		if (Scheduler(1) == -1) {
			setcommandmode();
			return;
		}
	}
}

#if	0	/* XXX - this not being in is a bug */
/*
 * nextitem()
 *
 *	Return the address of the next "item" in the TELNET data
 * stream.  This will be the address of the next character if
 * the current address is a user data character, or it will
 * be the address of the character following the TELNET command
 * if the current address is a TELNET IAC ("I Am a Command")
 * character.
 */

static char *
nextitem(current)
	char *current;
{
	if ((*current&0xff) != IAC) {
		return (current+1);
	}
	switch (*(current+1)&0xff) {
	case DO:
	case DONT:
	case WILL:
	case WONT:
		return (current+3);
	case SB:		/* loop forever looking for the SE */
	{
		register char *look = current+2;

		for (;;) {
			if ((*look++&0xff) == IAC) {
				if ((*look++&0xff) == SE) {
					return (look);
				}
			}
		}
	}
	default:
		return (current+2);
	}
}
#endif	/* 0 */

/*
 * netclear()
 *
 *	We are about to do a TELNET SYNCH operation.  Clear
 * the path to the network.
 *
 *	Things are a bit tricky since we may have sent the first
 * byte or so of a previous TELNET command into the network.
 * So, we have to scan the network buffer from the beginning
 * until we are up to where we want to be.
 *
 *	A side effect of what we do, just to keep things
 * simple, is to clear the urgent data pointer.  The principal
 * caller should be setting the urgent data pointer AFTER calling
 * us in any case.
 */

static void
netclear()
{
#if	0	/* XXX */
	register char *thisitem, *next;
	char *good;
#define	wewant(p)	((nfrontp > p) && ((*p&0xff) == IAC) && \
				((*(p+1)&0xff) != EC) && ((*(p+1)&0xff) != EL))

	thisitem = netobuf;

	while ((next = nextitem(thisitem)) <= netobuf.send) {
		thisitem = next;
	}

	/* Now, thisitem is first before/at boundary. */

	good = netobuf;	/* where the good bytes go */

	while (netoring.add > thisitem) {
		if (wewant(thisitem)) {
			int length;

			next = thisitem;
			do {
				next = nextitem(next);
			} while (wewant(next) && (nfrontp > next));
			length = next-thisitem;
			memcpy(good, thisitem, length);
			good += length;
			thisitem = next;
		} else {
			thisitem = nextitem(thisitem);
		}
	}

#endif	/* 0 */
}

/*
 * These routines add various telnet commands to the data stream.
 */

/*
 * doflush -  Send do timing mark (for network connection flush) & then
 * get rid of anything in the output buffer.  Return -1 if there was a
 * non-EWOULDBLOCK error on the tty flush, and otherwise return 0.
 */
static int
doflush()
{
	NET2ADD(IAC, DO);
	NETADD(TELOPT_TM);
	flushline = 1;
	flushout = 1;

	/* Drop pending tty output */
	if (ttyflush(1) == -2)
		return (-1);

	/* do printoption AFTER flush, otherwise the output gets tossed... */
	printoption("SENT", DO, TELOPT_TM);
	return (0);
}

int
xmitAO()
{
	NET2ADD(IAC, AO);
	printoption("SENT", IAC, AO);
	if (autoflush) {
		if (doflush() == -1)
			return (-1);
	}
	return (0);
}


void
xmitEL()
{
	NET2ADD(IAC, EL);
	printoption("SENT", IAC, EL);
}

void
xmitEC()
{
	NET2ADD(IAC, EC);
	printoption("SENT", IAC, EC);
}


int
dosynch()
{
	netclear();			/* clear the path to the network */
	NETADD(IAC);
	setneturg();
	NETADD(DM);
	printoption("SENT", IAC, DM);
	return (1);
}

int want_status_response = 0;

int
get_status()
{
	unsigned char tmp[16];
	register unsigned char *cp;

	if (my_want_state_is_dont(TELOPT_STATUS)) {
		(void) printf("Remote side does not support STATUS option\n");
		return (0);
	}
	cp = tmp;

	*cp++ = IAC;
	*cp++ = SB;
	*cp++ = TELOPT_STATUS;
	*cp++ = TELQUAL_SEND;
	*cp++ = IAC;
	*cp++ = SE;
	if (NETROOM() >= cp - tmp) {
		ring_supply_data(&netoring, tmp, cp-tmp);
		printsub('>', tmp+2, cp - tmp - 2);
	}
	++want_status_response;
	return (1);
}

void
intp()
{
	NET2ADD(IAC, IP);
	printoption("SENT", IAC, IP);
	flushline = 1;
	if (autoflush) {
		/* Ignore return as we're ending off anyway. */
		(void) doflush();
	}
	if (autosynch) {
		(void) dosynch();
	}
}

int
sendbrk()
{
	NET2ADD(IAC, BREAK);
	printoption("SENT", IAC, BREAK);
	flushline = 1;
	if (autoflush) {
		if (doflush() == -1)
			return (-1);
	}
	if (autosynch) {
		(void) dosynch();
	}
	return (0);
}

void
sendabort()
{
	NET2ADD(IAC, ABORT);
	printoption("SENT", IAC, ABORT);
	flushline = 1;
	if (autoflush) {
		/*
		 * Since sendabort() gets called while aborting,
		 * ignore the doflush() return
		 */
		(void) doflush();
	}
	if (autosynch) {
		(void) dosynch();
	}
}

void
sendsusp()
{
	NET2ADD(IAC, SUSP);
	printoption("SENT", IAC, SUSP);
	flushline = 1;
	if (autoflush) {
		if (doflush() == -1) {
			/* The following will not return. */
			fatal_tty_error("write");
		}
	}
	if (autosynch) {
		(void) dosynch();
	}
}

static void
sendeof()
{
	NET2ADD(IAC, xEOF);
	printoption("SENT", IAC, xEOF);
}

/*
 * Send a window size update to the remote system.
 */

void
sendnaws()
{
	unsigned short rows, cols;
	unsigned char tmp[16];
	register unsigned char *cp;

	if (my_state_is_wont(TELOPT_NAWS))
		return;

#define	PUTSHORT(cp, x) { if ((*cp++ = ((x)>>8)&0xff) == IAC) *cp++ = IAC; \
			    if ((*cp++ = ((x))&0xff) == IAC) *cp++ = IAC; }

	if (TerminalWindowSize(&rows, &cols) == 0) {	/* Failed */
		return;
	}

	cp = tmp;

	*cp++ = IAC;
	*cp++ = SB;
	*cp++ = TELOPT_NAWS;
	PUTSHORT(cp, cols);
	PUTSHORT(cp, rows);
	*cp++ = IAC;
	*cp++ = SE;
	if (NETROOM() >= cp - tmp) {
		ring_supply_data(&netoring, tmp, cp-tmp);
		printsub('>', tmp+2, cp - tmp - 2);
	}
}

void
tel_enter_binary(rw)
	int rw;
{
	if (rw&1)
		send_do(TELOPT_BINARY, 1);
	if (rw&2)
		send_will(TELOPT_BINARY, 1);
}

void
tel_leave_binary(rw)
	int rw;
{
	if (rw&1)
		send_dont(TELOPT_BINARY, 1);
	if (rw&2)
		send_wont(TELOPT_BINARY, 1);
}
