#ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */

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
#include	"builtins.h"

/* This module references the following external */
extern void	nam_rjust();

/* printing and io conversion */

#ifndef TIC_SEC
#   ifdef HZ
#	define TIC_SEC	HZ	/* number of ticks per second */
#   else
#	define TIC_SEC	60	/* number of ticks per second */
#   endif /* HZ */
#endif /* TIC_SEC */


/*
 *  flush the output queue and reset the output stream
 */

void	p_setout(fd)
register int fd;
{
	register struct fileblk *fp;
	register int count;
	if(!(fp=io_get_ftbl(fd))) {
		fp = &io_stdout;
		io_set_ftbl(fd, fp);
	}
	else if(fp->flag&IOREAD)
	{
		if(count=fp->last-fp->ptr)
			lseek(fd,-((off_t)count),SEEK_CUR);
		fp->fseek = 0;
		fp->ptr = fp->base;
	}
	fp->last = fp->base + IOBSIZE;
	fp->flag &= ~(IOREAD|IOERR|IOEOF);
	fp->flag |= IOWRT;
	if(output==fd)
		return;
	if(fp = io_get_ftbl(output))
		if(io_get_ftbl(fd)==fp || (fp->flag&IOSLOW))
			p_flush();
	output = fd;
}

/*
 * flush the output if necessary and null terminate the buffer
 */

void p_flush()
{
	register struct fileblk *fp = io_get_ftbl(output);
	register unsigned count;
	if(fp)
	{
		if(count=fp->ptr-fp->base)
		{
			if(write(output,fp->base,count) < 0)
				fp->flag |= IOERR;
			if(sh.heretrace)
				write(ERRIO,fp->base,count);
			fp->ptr = fp->base;
			fp->flag |= IOFLUS;
		}
		/* leave buffer as a null terminated string */
		*fp->ptr = 0;
	}
}

/*
 * print a given character
 */

void	p_char(c)
register int c;
{
	register struct fileblk *fp = io_get_ftbl(output);
	if(fp->ptr >= fp->last)
		p_flush();
	*fp->ptr++ = c;
}

/*
 * print a given wchar_t (may contain special value or illegal byte)
 */

void	p_wchar(c)
register wchar_t c;
{
	register struct fileblk *fp = io_get_ftbl(output);
	if ((fp->ptr + wcbytes(c)) > fp->last)
		p_flush();
	fp->ptr += sh_wctomb(fp->ptr, c);
}

/*
 * print a string optionally followed by a character
 * The buffer is always terminated with a zero byte.
 */

void	p_str(string,c)
register const char *string;
int c;
{
	register struct fileblk *fp = io_get_ftbl(output);
	register int cc;
	while(1)
	{
		if((cc= *string)==0)
			cc = c,c = 0;
		else
			string++;
		if(fp->ptr >= fp->last)
			p_flush();
		*fp->ptr = cc;
		if(cc==0)
			break;
		fp->ptr++;
	}
}

/*
 * print an wchar_t string optionally followed by a character
 * (may contain special values or illegal bytes)
 * The buffer is always terminated with a zero byte.
 */

void	p_wcs(wcs,c)
register const wchar_t *wcs;
wchar_t c;
{
	register struct fileblk *fp = io_get_ftbl(output);

	while (*wcs) {
		if ((fp->ptr + sh_wcwidth(*wcs)) > fp->last) {
			p_flush();
		}
		fp->ptr += sh_wctomb(fp->ptr, *wcs);
		wcs++;
	}
	if (c) p_wchar(c);
}

/*
 * print a given character a given number of times
 */

void	p_nchr(c,n)
register int c,n;
{
	register struct fileblk *fp = io_get_ftbl(output);
	while(n-- > 0)
	{
		if(fp->ptr >= fp->last)
			p_flush();
		*fp->ptr++ = c;
	}
}
/*
 * print a message preceded by the command name
 */

void p_prp(s1)
const char *s1;
{
	unsigned char *cp;
	register int c;
	wchar_t w;

	if(cp=(unsigned char *)st.cmdadr)
	{
		if(*cp=='-')
			cp++;
		c = ((st.cmdline>1)?0:':');
		p_str((char*)cp,c);
		if(c==0)
			p_sub(st.cmdline,':');
		p_char(SP);
	}
	if(cp = (unsigned char*)s1)
	{
		for (; w= mb_nextc((const char **)&cp); ) {
			p_vwc(w, 0);
		}
	}
}

/*
 * print a message preceded by the command name
 */

void p_prp_wcs(ws1)
const wchar_t *ws1;
{
	register unsigned char *cp;
	register int c;
	if(cp=(unsigned char *)st.cmdadr)
	{
		if(*cp=='-')
			cp++;
		c = ((st.cmdline>1)?0:':');
		p_str((char*)cp,c);
		if(c==0)
			p_sub(st.cmdline,':');
		p_char(SP);
	}
	p_vwcs(ws1, 0);
}

/*
 * print a time and a separator 
 */

void	p_time(t,c)
#ifndef pdp11
    register
#endif /* pdp11 */
clock_t t;
int c;
{
	register int  min, sec, frac;
	register int hr;
	frac = t%TIC_SEC;
	frac = (frac*100)/TIC_SEC;
	t /= TIC_SEC;
	sec=t%60; t /= 60;
	min=t%60;
	if(hr=t/60)
	{
		p_num(hr,'h');
	}
	p_num(min,'m');
	p_num(sec,'.');
	if(frac<10)
		p_char('0');
	p_num(frac,'s');
	p_char(c);
}

/*
 * print a number optionally followed by a character
 */

void	p_num(n,c)
int 	n;
int c;
{
	p_str(sh_itos(n),c);
}


/* 
 * print a list of arguments in columns
 */
#define NROW	15	/* number of rows in output before going to multi-columns */
#define LBLSIZ	3	/* size of label field and interfield spacing */

void	p_list(argn,com)
char *com[];
{
	register int i,j;
	register char **arg;
	char a1[12];
	int nrow;
	int ncol = 1;
	int ndigits = 1;
	int fldsize;
#if ESH || VSH
	int wsize = ed_window();
#else
	int wsize = 80;
#endif
	char *cp = nam_fstrval(LINES);
	nrow = (cp?1+2*(atoi(cp)/3):NROW);
	for(i=argn;i >= 10;i /= 10)
		ndigits++;
	if(argn < nrow)
	{
		nrow = argn;
		goto skip;
	}
	i = 0;
	for(arg=com; *arg;arg++)
	{
		i = max(i,mbscolumns(*arg));
	}
	i += (ndigits+LBLSIZ);
	if(i < wsize)
		ncol = wsize/i;
	if(argn > nrow*ncol)
	{
		nrow = 1 + (argn-1)/ncol;
	}
	else
	{
		ncol = 1 + (argn-1)/nrow;
		nrow = 1 + (argn-1)/ncol;
	}
skip:
	fldsize = (wsize/ncol)-(ndigits+LBLSIZ);
	for(i=0;i<nrow;i++)
	{
		j = i;
		while(1)
		{
			arg = com+j;
			strcpy(a1,sh_itos(j+1));
			nam_rjust(a1,ndigits,' ');
			p_str(a1,')');
			p_char(SP);
			{
				wchar_t	*wcs;
				wcs = mbstowcs_alloc(*arg);
				p_vwcs(wcs,0);
				xfree((void *)wcs);
			}
			j += nrow;
			if(j >= argn)
				break;
			p_nchr(SP,fldsize-mbscolumns(*arg));
		}
		newline();
	}
}

/*
 * Print a number enclosed in [] followed by a character
 */

void	p_sub(n,c)
register int n;
register int c;
{
	p_char('[');
	p_num(n,']');
	if(c)
		p_char(c);
}

#ifdef POSIX
/*
 * print <str> qouting chars so that it can be read by the shell
 * terminate with the character <cc>
 */
/* CSI assumption1(ascii) made here. See csi.h. */
void	p_qstr(str,cc)
char *str;
{
	char *cp = str;
	register wchar_t c = mb_peekc((const char *)cp);
	register int state = (c==0);

	do
	{
		if (sh_iswalpha(c))
		{
			while (mb_nextc((const char **)&cp),
				c = mb_peekc((const char *)cp), sh_iswalnum(c));
			if (c=='=')
			{
				*cp = 0;
				p_str(str, '=');
				*cp = '=';
				(void)mb_nextc((const char **)&cp);
				str = cp;
				c = mb_peekc((const char *)cp);
			}
		}
		if (c=='~')
			state++;
		while ((c = mb_nextc((const char **)&cp)) && (c!= '\'')) {
			if (iswascii(c))
				state |= _ctype1[(int)c];
		}
		if (c || state)
		{
			char	savec = '\0';

			/* needs single quotes */
			p_char('\'');
			if (c)
			{
				/* string contains single quote */
				savec = *cp;
				*cp = 0;
				state = '\\';
			}
			else
				state = '\'';
			p_str(str, state);
			if (savec != '\0') {
				*cp = savec;
				c = mb_peekc((const char *)cp);
			}
			str = (cp-1);
		}
	}
	while (c);
	p_str(str,cc);
}
#endif /* POSIX */

void
p_vwc(
	wchar_t	w,
	int	c
)
{
	if (sh_iswprint(w)) {
		/* Printable. */
		p_wchar(w);
	} else if (iswascii(w) && (w < ' ' || w == 0x7f)) {
		/* Control code. Use ^x representation. */
		p_char('^');
		p_char((unsigned int)w ^ TO_PRINT);
	} else if (!IsWInvalid(w)) {
		/* Other legal but non-printable character.
		   Use one or more of octal representation "\ooo"s. */
		unsigned char	buf[MB_LEN_MAX + 1];
		int		l, i;

		l = sh_wctomb((char *)buf, w);
		for (i = 0; i < l; i++) {
			p_char('\\');
			p_char('0' + ((buf[i]>>6) & 03));
			p_char('0' + ((buf[i]>>3) & 07));
			p_char('0' + ((buf[i]) & 07));
		}
	} else {
		/* Illegal byte. Use octal representation "\ooo". */
		int	b = ShWRawByteToByte(w);
		p_char('\\');
		p_char('0' + ((b>>6) & 03));
		p_char('0' + ((b>>3) & 07));
		p_char('0' + ((b) & 07));
	}
	if (c) p_char(c);
	return;
}

void
p_vwcs(
	const wchar_t	*wcs,
	int	c
)
{
	while (*wcs) {
		p_vwc(*wcs++, 0);
	}
	if (c != 0) p_char(c);
}
