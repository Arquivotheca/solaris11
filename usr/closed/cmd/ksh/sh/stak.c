/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 *   Routines to implement a stack-like storage library
 *
 *   A stack consists of a link list of variable size frames
 *   The beginning of each frame is initialized with a frame structure
 *   that contains a pointer to the previous frame and a pointer to the
 *   end of the current frame.
 *
 *   David Korn
 *   AT&T Bell Laboratories
 *   Room 3C-526B
 *   Murray Hill, N. J. 07974
 *   Tel. x7975
 *   ulysses!dgk
 *
 */

#include	<limits.h>
#include	<stdlib.h>
#include	<string.h>

#define _STAK_PRIVATE \
	short		stakflags;	/* stack attributes */ \
	struct  _stak_	*stakcur;	/* current stack pointer  */ \
	char		*stakbase;	/* base of current stack frame */ \
	char		*stakend;	/* end of current stack frame */ \
	char		*(*stakoverflow)();	/* called when malloc fails */

#include	"stak.h"

#define STAK_MYSIZE	2		/* local argument to stakcreate */
#define STAK_FSIZE	(1024*sizeof(int))
	/*
	 * Need 8-byte alignment on sparc. Stack could be allocated
	 * for a union anynode, which includes struct procnod.
	 */
#define	STAK_ALIGN	8
#define round(a,b)	((((a)+b)-1)&~((b)-1))

#ifdef STAKSTATS
    static struct
    {
	int	create;
	int	delete;
	int	install;
	int	alloc;
	int	copy;
	int	puts;
	int	seek;
	int	set;
	int	grow;
	int	addsize;
	int	delsize;
	int	movsize;
    } _stakstats;
#   define increment(x)	(_stakstats.x++)
#   define count(x,n)	(_stakstats.x += (n))
#else
#   define increment(x)
#   define count(x,n)
#endif /* STAKSTATS */

/* Need to keep struct frame and Stak_t 8-byte aligned (see STAK_ALIGN) */
struct frame
{
	char	*prev;		/* previous stak frame */
	char	*end;		/* end of this frame */
	off_t	off;		/* cumulative offset from the stack bottom */
	off_t	topoff;		/* offset to the staktop within the frame */
};

static char *overflow();

/* TRANSLATION_NOTE
 * To be printed when malloc() failed. */
static const char Omsg[] = "malloc failed while growing stack\n";
static int minsize = STAK_FSIZE;

Stak_t _stak_cur =
{
	0,			/* stakleft */
	(char*)(&_stak_cur),	/* staktop */
	(char*)(&_stak_cur),	/* stakbot */
	1,			/* stakref */
	0,			/* stakflags */
	&_stak_cur,		/* stakcur */
	(char*)(&_stak_cur),	/* stakbase */
	(char*)(&_stak_cur),	/* stakend */
	overflow		/* stakoverflow */
};

/*
 * create a stack
 * minsize is normally STAK_FSIZE but may be larger when set by _stakgrow()
 */

Stak_t *stakcreate(flags)
register int flags;
{
	register Stak_t *sp;
	register char *cp;
	register struct frame *fp;
	register int size,fsize;
	if(flags&STAK_MYSIZE)
		fsize = minsize;
#ifndef USE_REALLOC
	else if(flags&STAK_SMALL)
		fsize = STAK_FSIZE/16;
#endif /* USE_REALLOC */
	else
		fsize = STAK_FSIZE;
	minsize = STAK_FSIZE;
	size = fsize + sizeof(struct frame)+sizeof(Stak_t);
	if((cp=malloc(size))==0)
		return((Stak_t*)0);
	increment(create);
	count(addsize,size);
	sp = (Stak_t*)cp;
	sp->stakcur = sp;
	cp += sizeof(Stak_t);
	fp = (struct frame*)cp;
	fp->prev = 0;
	fp->off = 0;
	fp->topoff = 0;
	sp->stakbase = cp;
	sp->stakref = 1;
	cp += sizeof(struct frame);
	sp->staktop = sp->stakbot = cp;
	sp->stakflags = (flags&STAK_SMALL);
	sp->stakoverflow = _stak_cur.stakoverflow;
	sp->stakend  = fp->end = cp+fsize;
	sp->stakleft = fsize;
	return(sp);
}

/*
 * return a pointer to the current stack
 * if <sp> is not null, it becomes the new current stack
 * <oflow> becomes the new overflow function
 */

#if defined(__STDC__)
    Stak_t *stakinstall(Stak_t *sp, char *(*oflow)(int))
#else
    Stak_t *stakinstall(sp,oflow)
    Stak_t *sp;
    char *(*oflow)();
#endif /* __STDC__ */
{
	Stak_t *oldsp = _stak_cur.stakcur;
	increment(install);
	if(sp)
	{
#ifdef USE_REALLOC
		register struct frame *fp;
		register char *cp;
		/* only works if realloc() to reduce does not relocate */
		if(_stak_cur.stakflags&STAK_SMALL)
		{
			/* shrink the last frame */
			fp = (struct frame*)(cp=_stak_cur.stakbase);
			if(fp->prev==0)
				cp = (char*)oldsp;
			_stak_cur.stakend = fp->end = _stak_cur.staktop;
			_stak_cur.stakleft = 0;
			if(realloc(cp,_stak_cur.stakend-cp)!=cp)
				return(0);
		}
#endif /* USE_REALLOC */
		*oldsp = _stak_cur;
		_stak_cur = *sp;
	}
	else
		sp = oldsp;
	if(oflow)
		sp->stakoverflow = (char*(*)())oflow;
	return(oldsp);
}

/*
 * terminate a stack and free up the space
 */

int
stakdelete(Stak_t *sp)
{
	register char *cp = sp->stakbase;
	register struct frame *fp;
	if(--sp->stakref>0)
		return(1);
	increment(delete);
	while(1)
	{
		fp = (struct frame*)cp;
		if(fp->prev)
		{
			cp = fp->prev;
			free((char*)fp);
		}
		else
			break;
	}
	/* now free the first frame */
	if(sp != &_stak_cur)
		free((void*)sp);
	return(0);
}

/*
 * return the location of current stack in offset from
 * the bottom.
 */
off_t
staksave(void)
{
	Stak_t *sp = &_stak_cur;
	struct frame *fp;

	fp = (struct frame *)sp->stakbase;
	return (fp->off + (sp->stakbot - sp->stakbase));
}

/*
 * reset the bottom of the current stack back to <off>
 * if <off> is not in this stack, then the stack is reset to the beginning
 * otherwise, the top of the stack is set to stakbot+<topoff>
 *
 */
void
stakrestore(off_t off, off_t topoff)
{
	Stak_t *sp = &_stak_cur;
	char *cp;
	struct frame *fp, *pfp;
	off_t toploc = off + topoff;

	/* if stack has not been allocated, return */
	if (sp == sp->stakcur)
		return;

	increment(set);
	for (;;) {
		fp = (struct frame *)sp->stakbase;
		if ((pfp = (struct frame *)fp->prev) == NULL) {
			/* the last stack frame */
			if (off <= 0 || off > (sp->stakend - sp->stakbase)) {
				/*
				 * requested location is not valid, or
				 * given offset was 0.
				 * Set stack back to the beginning.
				 * Ignore topoff.
				 */
				sp->staktop = sp->stakbot =
				    ((char *)(sp->stakcur + 1)) +
				    sizeof (struct frame);
			} else {
				sp->stakbot = sp->stakbase +
					round(off, STAK_ALIGN);
				sp->staktop = sp->stakbase + toploc;
			}
			break;
		}
		/*
		 * We are looking at the allocated stack frame which
		 * needs to be freed.
		 * Check previous stack frame to see if it can be
		 * used instead of the current frame.
		 */
		if (off > fp->off &&
		    toploc <= (fp->off + (sp->stakend - sp->stakbase)) &&
		    toploc > (pfp->off + pfp->topoff)) {
			/*
			 * current frame is okay, but the previous stack
			 * frame isn't within the range. we use the current
			 * frame.
			 */
			sp->stakbot = sp->stakbase +
				round(off - fp->off, STAK_ALIGN);
			sp->staktop = sp->stakbase + (off - fp->off) + topoff;
			break;
		}
		/* release the current frame, and go to the next frame */
		cp = sp->stakbase;
		sp->stakbase = fp->prev;
		sp->stakend = pfp->end;
		free(cp);
	}
	sp->stakleft = sp->stakend - sp->staktop;
}

void
stakadjust(off_t topoff)
{
	Stak_t *sp = &_stak_cur;
	off_t	off;

	off = sp->stakbot - sp->stakbase;
	sp->stakbot = sp->stakbase + round(off, STAK_ALIGN);
	sp->staktop = sp->stakbase + off + topoff;
	sp->stakleft = sp->stakend - sp->staktop;
}

/*
 * allocate <n> bytes on the current stack
 */

#if defined(__STDC__)
    char *stakalloc(register unsigned n)
#else
    char *stakalloc(n)
    register unsigned n;
#endif /* __STDC__ */
{
	register Stak_t *sp = &_stak_cur;
	register char *old;
	increment(alloc);
	n = round(n, STAK_ALIGN);
	if((sp->stakleft += ((sp->staktop-sp->stakbot)-(int)n)) <=0)
		_stakgrow(n);
	old = sp->stakbot;
	sp->stakbot = sp->staktop = old+n;
	return(old);
}

/*
 * begin a new stack word of at least <n> bytes
 */
char *stakseek(n)
register unsigned n;
{
	register Stak_t *sp = &_stak_cur;
	increment(seek);
	if((sp->stakleft += ((sp->staktop-sp->stakbot)-(int)n)) <=0)
		_stakgrow(n);
	sp->staktop = sp->stakbot+n;
	return(sp->stakbot);
}

/*
 * put the string <str> onto the stack
 * returns the length of the string
 */
int	stakputs(str)
register const char *str;
{
	register Stak_t *sp = &_stak_cur;
	register const char *cp=str;
	register int n;
	while(*cp++);
	n = cp-str;
	increment(puts);
	if((sp->stakleft -= n) <=0)
		_stakgrow(n);
	strcpy(sp->staktop,str);
	sp->staktop += --n;
	return(n);
}

/*
 * advance the stack to the current top
 * if extra is non-zero, first add a extra bytes and zero the first
 */
char	*stakfreeze(extra)
register unsigned extra;
{
	register Stak_t *sp = &_stak_cur;
	register char *old = sp->stakbot;
	register char *top = sp->staktop;
	if(extra)
	{
		if(extra > sp->stakleft)
		{
			top = _stakgrow(extra);
			old = sp->stakbot;
		}
		*top = 0;
		top += extra;
	}
	sp->staktop = sp->stakbot += round(top-old, STAK_ALIGN);
	sp->stakleft = sp->stakend-sp->staktop;
	return(old);
}

/*
 * copy string <str> onto the stack as a new stack word
 */
char	*stakcopy(str)
const char *str;
{
	register Stak_t *sp = &_stak_cur;
	register char *cp = (char*)str;
	register int n;
	while(*cp++);
	n = round(cp-str, STAK_ALIGN);
	increment(copy);
	if((sp->stakleft += ((sp->staktop-sp->stakbot)-n)) <=0)
		_stakgrow(n);
	strcpy(cp=sp->stakbot,str);
	sp->stakbot = sp->staktop = cp+n;
	return(cp);
}

/*
 * add a new stack frame of size >= <n> to the current stack.
 * if <n> > 0, copy the bytes from stakbot to staktop to the new stack
 * if <n> is zero, then copy the remainder of the stack frame from stakbot
 * to the end is copied into the new stack frame
 */

char *_stakgrow(size)
unsigned size;
{
	register int n = size;
	register Stak_t *sp = &_stak_cur;
	register struct frame *fp;
	register char *cp;
	register unsigned m = (n?sp->staktop:sp->stakend)-sp->stakbot;
	register int reused = 0;
	n += (m + sizeof(struct frame)+1);
	if(sp->stakflags&STAK_SMALL)
#ifndef USE_REALLOC
		n = round(n,STAK_FSIZE/16);
	else
#endif /* !USE_REALLOC */
		n = round(n,STAK_FSIZE);
	/* check for first time default stack reference */
	if(sp==sp->stakcur)
	{
		minsize = n;
		if((sp = stakcreate(STAK_MYSIZE))==0)
			sp = (Stak_t*)overflow(sizeof(Stak_t));
		sp->stakleft -= size;
		_stak_cur = *sp;
		return(sp->stakbot);
	}
	/*
	 * see whether current frame can be extended.
	 * if there is something frozen in the current stak frame, that
	 * means we have references to the stack contents. Thus we don't
	 * realloc the stack space.
	 */
	if(sp->stakbot == sp->stakbase+sizeof(struct frame))
	{
		/* handle first frame specially */
		if((fp = (struct frame*)sp->stakbase)->prev)
			cp = (char*)realloc(sp->stakbase,n);
		else if(cp=(char*)realloc((char*)sp->stakcur,n+sizeof(Stak_t)))
		{
			sp->stakcur = (Stak_t*)cp;
			cp += sizeof(Stak_t);
		}
		reused++;
	}
	else
		cp = malloc(n);
	if(cp==(char*)0)
		cp = (*sp->stakoverflow)(n);
	increment(grow);
	count(addsize,n);
	fp = (struct frame*)cp;
	if (!reused) {
		struct frame *ofp = (struct frame *)sp->stakbase;

		/* record the last top location in the old frame */
		ofp->topoff = sp->staktop - sp->stakbase;
		/* initialize new frame info */
		fp->prev = sp->stakbase;
		fp->off = ofp->off + ofp->topoff;
		fp->topoff = 0;
	}
	sp->stakbase = cp;
	sp->stakend = fp->end = cp+n;
	sp->stakleft = n-(m+size+sizeof(struct frame));
	cp = (char*)(fp+1);
	if(m && !reused)
		memcpy(cp,sp->stakbot,m);
	count(movsize,m);
	sp->stakbot = cp;
	return(sp->staktop = sp->stakbot+m);
}


static char *overflow(n)
int n;
{
	(&n,1);
	write(2,Omsg, sizeof(Omsg)-1);
	exit(2);
	/* NOTREACHED */
	return(0);
}

void _stakputwc(
	const wchar_t	c
)
{
	register Stak_t *sp = &_stak_cur;
	char	buf[MB_LEN_MAX + 1];
	int	len;

	len = sh_wctomb(buf, c);
	buf[len] = '\0';
	if((sp->stakleft -= (len + 1)) <=0)
		_stakgrow(len + 1);
	strcpy(sp->staktop,buf);
	sp->staktop += len;
}

void _stakputwcs(
	const wchar_t	*wcs
)
{
	register Stak_t *sp = &_stak_cur;
	char	*mbs;
	int	len;
	
	if (!(mbs = wcstombs_alloc(wcs)))
		return;
	len = strlen(mbs);
	if((sp->stakleft -= (len + 1)) <=0)
		_stakgrow(len + 1);
	strcpy(sp->staktop, mbs);
	sp->staktop += len;
	xfree(mbs);
}

void _wstakputascii(
	const int	a
)
{
	register Stak_t *sp = &_stak_cur;

	if ((sp->stakleft -= (sizeof(wchar_t))) <=0)
		_stakgrow(sizeof(wchar_t));
	*(wchar_t *)sp->staktop = (wchar_t)a;
	sp->staktop += sizeof(wchar_t);
}

void _wstakputwc(
	const wchar_t	c
)
{
	register Stak_t *sp = &_stak_cur;

	if ((sp->stakleft -= (sizeof(wchar_t))) <=0)
		_stakgrow(sizeof(wchar_t));
	*((wchar_t *)(sp->staktop)) = c;
	sp->staktop += sizeof(wchar_t);
}

void _wstakputwcs(
	const wchar_t	*wcs
)
{
	register Stak_t *sp = &_stak_cur;
	int	len;
	
	len = wcslen(wcs);
	if ((sp->stakleft -= (sizeof(wchar_t) * (len + 1))) <=0)
		_stakgrow(sizeof(wchar_t) * (len + 1));
	wcscpy((wchar_t *)sp->staktop, wcs);
	sp->staktop += (sizeof(wchar_t) * len);
}
