#ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */

/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Miscellaneous routine needed for standalone library for edit modes
 */

#include	"io.h"
#include	"terminal.h"
#include	"history.h"
#include	"edit.h"
#ifdef TIOCLBIC
#   undef TIOCLBIC
#   ifdef _sys_ioctl_
#	include	<sys/ioctl.h>
#   endif /* _sys_ioctl_ */
#endif /* TIOCLBIC */
#undef read

#define e_create	"cannot create"

extern char *strrchr();
extern char *getenv();
char opt_flag;
char ed_errbuf[IOBSIZE+1];
static struct fileblk outfile = { ed_errbuf, ed_errbuf, ed_errbuf+IOBSIZE, 2, IOWRT};
static int	editfd;
static int	output = 0;
static char	beenhere;

#if 0  /* almost always false.  Makes a good block comment */
 **********************************************************************
 * There are two scenarios here.
 *
 * 1) We ALWAYS want ksh-editing on the terminal input.
 * 2) We SOMETIMES want ksh-editing on the terminal input.
 *
 * If you are running with case 1, then just call read() in the usual
 * manner and be happy.
 *
 * If you want to only have line-editing sometimes, and the very first
 * read() is not one of those times, then you must perform the 
 * initialization first, turn OFF editing, perform your read(), and
 * so forth.  An example of this is in a curses application in which
 * single characters are read in RAW mode and acted upon immediately.
 * Line editing here will not work.
 * 
 * Do the following:
 *		edit_Init();
 *		SAVopt_flag = set_edit(0,0);  /* fd=0 set to NO editing   */
 *		read(0,buffer,1);             /* do your single char read */
 *		set_edit(0,SAVopt_flag);      /* restore editing on fd=0  */
 **********************************************************************
#endif /* 0 */

int edit_Init()
{
	register char *sp;
	if(!beenhere)
	{
		beenhere = 1;
		hist_open();
		if(!(sp  = getenv("VISUAL")))
			sp = getenv("EDITOR");
		if(sp)
		{
			if(strrchr(sp,'/'))
				sp = strrchr(sp,'/')+1;
			if(strcmp(sp,"vi") == 0)
				opt_flag = EDITVI;
			else if(strcmp(sp,"emacs")==0)
				opt_flag = EMACS;
			else if(strcmp(sp,"gmacs")==0)
				opt_flag = GMACS;
		}
	}
	return(1);
}
/*
 * read routine with edit modes
 */

int read(fd,buff,n)
char *buff;
{
	register int r, flag;
	if(fd==editfd && !beenhere)
		edit_Init();
	flag = (fd==editfd?opt_flag&EDITMASK:0);
	switch(flag)
	{
		case EMACS:
		case GMACS:
			tty_set(-1);
			r = emacs_read(fd,buff,n);
			break;

		case VIRAW:
		case EDITVI:
			tty_set(-1);
			r = vi_read(fd,buff,n);
			break;
		default:
#ifdef SYSCALL
			r = syscall(3,fd,buff,n);
#else
			r = rEAd(fd,buff,n);
#endif /* SYSCALL */
	}
	if(fd==editfd && hist_ptr && (opt_flag&NOHIST)==0 && r>0)
	{
		/* write and flush history */
		int c = buff[r];
		buff[r] = 0;
		hist_eof();
		p_setout(hist_ptr->fixfd);
		p_str(buff,0);
		hist_flush();
		buff[r] = c;
	}
	return(r);
}


/*
 * enable edit mode <mode> on file number <fd>
 * the NOHIST bit can also be set to avoid writing the history file
 * <fd> cannot be file two
 */

int	set_edit(fd,mode)
{
	int retval = opt_flag;
	opt_flag = mode;
	if(fd==2)
		return(-1);
	editfd = fd;
	return(retval);
}

/*
 *  flush the output queue and reset the output stream
 */

void	p_setout(fd)
register int fd;
{
	register struct fileblk *fp;
	if(!io_get_ftbl(fd))
		io_set_ftbl(fd, &outfile);
	fp = io_get_ftbl(fd);
	fp->last = fp->base + IOBSIZE;
	fp->flag &= ~(IOREAD|IOERR|IOEOF);
	if(output==fd)
		return;
	if(io_get_ftbl(fd)==io_get_ftbl(output))
		p_flush();
	output = fd;
}

/*
 * flush the output if necessary and null terminate the buffer
 */

void p_flush()
{
	register struct fileblk *fp = io_get_ftbl(output);
	register int count;
	if(fp && (count=fp->ptr-fp->base))
	{
		if(write(output,fp->base,count) < 0)
			fp->flag |= IOERR;
		/* leave previous buffer as a null terminated string */
		*fp->ptr = 0;
		fp->ptr = fp->base;
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
 * print a string optionally followed by a character
 */

void	p_str(string,c)
register char *string;
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
		if(cc==0)
			break;
		if(fp->ptr >= fp->last)
			p_flush();
		*fp->ptr++ = cc;
	}
}

/*
 * copy string a to string b and return pointer to end of string b
 */

char *ed_movstr(a,b)
register const char *a;
register char *b;
{
	while(*b++ = *a++);
	return(--b);
}

/*
 * print and error message and exit
 */

ed_failed(name,message)
char *name,*message;
{
	p_setout(ERRIO);
	p_str(name,' ');
	p_char(':');
	p_char(' ');
	p_str(message,'\n');
	exit(2);
}


#ifndef F_DUPFD
#   define F_DUPFD	0
static int fcntl(f1,type,arg)
register int arg;
{
	struct stat statbuf;
	if(type==F_DUPFD)
	{
		register int fd;
		/* find first non-open file */
		while(arg < NFILE &&  (fstat(arg,&statbuf)>=0))
			arg++;
		if(arg >= NFILE)
			return(-1);
		fd = dup2(f1, arg);
		return(fd);
	   }
	else 
		return(0);
}
#endif	/* F_DUPFD */

/*
 * print a prompt
 */
void pr_prompt(string)
register char *string;
{
	register int c;
	register char *dp = editb.e_prbuff;
#ifdef TIOCLBIC
	int mode;
	mode = LFLUSHO;
	ioctl(ERRIO,TIOCLBIC,&mode);
#endif	/* TIOCLBIC */
	p_setout(ERRIO);
	while(c= *string++)
	{
		if(dp < editb.e_prbuff+PRSIZE)
			*dp++ = c;
		p_char(c);
	}
	*dp = 0;
	if (!(opt_flag&EDITMASK))
		p_flush();

}

