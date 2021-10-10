#ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */

/*
 * This is the code for the echo and print command
 */

#ifdef KSHELL
#   include	"defs.h"
#endif	/* KSHELL */

#ifdef __STDC__
#   define ALERT	'\a'
#else
#   define ALERT	07
#endif /* __STDC__ */

/*
 * echo the argument list
 * if raw is non-zero then \ is not a special character.
 * returns 0 for \c otherwise 1.
 */

/* CSI assumption1(ascii) made here. See csi.h. */
int echo_list(raw,com)
int raw;
char *com[];
{
	register int outc;
	char *cp;
	register wchar_t c;
	while(cp= *com++)
	{
		if(!raw) for (; c = mb_peekc((const char *)cp);
			mb_nextc((const char **)&cp))
		{
			if (c=='\\')
			{
				outc = c;
				switch(*++cp)
				{
					case 'a':
						outc = ALERT;
						break;
					case 'b':
						outc = '\b';
						break;
					case 'c':
						return(0);
					case 'f':
						outc = '\f';
						break;
					case 'n':
						outc = '\n';
						break;
					case 'r':
						outc = '\r';
						break;
					case 'v':
						outc = '\v';
						break;
					case 't':
						outc = '\t';
						break;
					case '\\':
						outc = '\\';
						break;
					case '0':
					{
						register char *cpmax;
						outc = 0;
						cpmax = cp + 4;
						while(++cp<cpmax && *cp>='0' && 
							*cp<='7')
						{
							outc <<= 3;
							outc |= (*cp-'0');
						}
						cp--;
						break;
					}
					default:
					cp--;
				}
				p_char(outc);
			} else {
				p_wchar(c);
			}
		}
#ifdef POSIX
		else if(raw>1)
			p_qstr(cp,0);
#endif /* POSIX */
		else
			p_str(cp,0);
		if(*com)
#ifdef WEXP
		{
			if (opt_flags & WEXP_E)
				p_char(0);
			else
				p_char(' ');
		}
#else
			p_char(' ');
#endif /* WEXP */

#ifdef KSHELL
		if(sh.trapnote&SIGSET)
			sh_exit(SIGFAIL);
#endif	/* KSHELL */
	}
	return(1);
}

