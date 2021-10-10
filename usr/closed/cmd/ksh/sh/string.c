#ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */

/*
 * string processing routines for Korn shell
 *
 */

#include	"defs.h"
#include	"sym.h"

extern char	*utos();

/*
 * converts integer n into an unsigned decimal string
 */

char *sh_itos(n)
int n;
/*@
	return x satisfying atol(x)==n;
@*/ 
{
	return(utos((ulong)n,10));
}


/*
 * look for the substring <old> in <string> and replace with <new>
 * The new string is put on top of the stack
 */

char *sh_substitute(string,old,new)
const char *string;
const char *old;
char *new;
/*@
	assume string!=NULL && old!=NULL && new!=NULL;
	return x satisfying x==NULL ||
		strlen(x)==(strlen(in string)+strlen(in new)-strlen(in old));
@*/
{
	const char *sp = string;
	const char *cp;
	const char *savesp = NIL;
	wchar_t	sc, oc;
	stakseek(0);
	if(*sp==0)
		return(NIL);
	if(*(cp=old)==0)
		goto found;
	
	do
	{
	/* skip to first character which matches start of old */
		oc = mb_peekc((const char *)cp);
		while ((sc = mb_peekc((const char *)sp)) != L'\0' &&
			(savesp==sp || sc != oc)) {
			stakputwc(sc);
			(void)mb_nextc((const char **)&sp);
		}
		if(*sp == 0)
			return(NIL);
		savesp = sp;
	        for (; *cp; (void)mb_nextc(&cp))
		{
			if ((oc = mb_peekc((const char *)cp)) !=
				(sc = mb_nextc(&sp)))
				break;
		}
		if (*cp==0)
		/* match found */
			goto found;
		sp = savesp;
		cp = old;
	}
	while(*sp);
	return(NIL);

found:
	/* copy new */
	stakputs(new);
	/* copy rest of string */
	stakputs(sp);
	return(stakfreeze(1));
}

/*
 * put string v onto the heap and return the heap pointer
 */

char *sh_heap(v)
register const char *v;
/*@
	return x satisfying (in v? strcmp(v,x)==0: x==0);
@*/
{
	register char *p;
	if(v)
	{
		sh_copy(v,p=malloc((unsigned)strlen(v)+1));
		return(p);
	}
	else
		return(0);
}


/*
 * TRIM(sp)
 * Remove escape characters from characters in <sp> and eliminate quoted nulls.
 */

void	sh_trim(sp)
char *	sp;
/*@
	assume sp!=NULL;
	promise  strlen(in sp) <= in strlen(sp);
@*/
{
	register char *dp;
	register wchar_t c;
	if(sp)
	{
		dp = sp;
		while (c = mb_peekc(sp))
		{
			if (c == ESCAPE) {
				(void)mb_nextc((const char **)&sp);
				c = mb_peekc((const char *)sp);
			}
			(void)mb_nextc((const char **)&sp);
			if (c) {
				dp += sh_wctomb(dp, c);
			}
		}
		*dp = 0;
	}
}

void	sh_trim_wcs(sp)
wchar_t *	sp;
/*@
	assume sp!=NULL;
	promise  wcslen(in sp) <= in wcslen(sp);
@*/
{
	register wchar_t *dp;
	register wchar_t c;
	if(sp)
	{
		dp = sp;
		while (c = *sp++)
		{
			if (c == ESCAPE)
				c = *sp++;
			if (c)
				*dp++ = c;
		}
		*dp = 0;
	}
}

/*
 * copy string a to string b and return a pointer to the end of the string
 */

char *sh_copy(a,b)
register const char *a;
register char *b;
/*@
	assume a!=NULL && b!= NULL;
	promise strcmp(in a,in b)==0;
	return x satisfying (x-(in b))==strlen(in a);
 @*/
{
	while(*b++ = *a++);
	return(--b);
}

/*
 * G. S. Fowler
 * AT&T Bell Laboratories
 *
 * apply file permission expression expr to perm
 *
 * each expression term must match
 *
 *	[ugo]*[-&+|=]?[rwxst0-7]*
 *
 * terms may be combined using ,
 *
 * if non-null, e points to the first unrecognized char in expr
 */


#ifndef S_IRWXU
#   ifndef S_IREAD
#	define S_IREAD		00400
#	define S_IWRITE		00200
#	define S_IEXEC		00100
#   endif
#   ifndef S_ISUID
#	define S_ISUID		04000
#   endif
#   ifndef S_ISGID
#	define S_ISGID		02000
#   endif
#   ifndef S_IRUSR
#	define S_IRUSR		S_IREAD
#	define S_IWUSR		S_IWRITE
#	define S_IXUSR		S_IEXEC
#	define S_IRGRP		(S_IREAD>>3)
#	define S_IWGRP		(S_IWRITE>>3)
#	define S_IXGRP		(S_IEXEC>>3)
#	define S_IROTH		(S_IREAD>>6)
#	define S_IWOTH		(S_IWRITE>>6)
#	define S_IXOTH		(S_IEXEC>>6)
#   endif
#ifndef S_ISVTX
#   define S_ISVTX		01000
#endif

#   define S_IRWXU		(S_IRUSR|S_IWUSR|S_IXUSR)
#   define S_IRWXG		(S_IRGRP|S_IWGRP|S_IXGRP)
#   define S_IRWXO		(S_IROTH|S_IWOTH|S_IXOTH)
#endif


/*
 * some really old systems don't have memcpy()
 */

#ifdef NOMEMCPY
#   ifdef NOBCOPY
	char *memcpy(b,a,n)
	char *b;
	register char *a;
	{
		register int n;
		register char *d = b;
		while(n--)
			*d++ = *a++;
		return(b);
	}
#   else
	char *memcpy(b,a,n)
	char *b,*a;
	{
		bcopy(a,b,n);
		return(b);
	}
#   endif /* NOBCOPY */
#endif /* NOMEMCPY */

#ifdef NOMEMSET
	char *memset(region,c,n)
	register char *region;
	register int c,n;
	{
		register char *sp = region;
		while(n--)
			*sp++ = c;
		return(region);
	}
#endif /* NOMEMSET */
#define	READ	S_IRUSR|S_IRGRP|S_IROTH
#define	WRITE	S_IWUSR|S_IWGRP|S_IWOTH
#define	EXEC	S_IXUSR|S_IXGRP|S_IXOTH
#define	USER	S_ISVTX|S_ISUID|S_IRWXU
#define	GROUP	S_ISGID|S_IRWXG
#define	OTHER	S_IRWXO
#define	ALL		S_ISUID|S_ISGID|S_ISVTX|READ|WRITE|EXEC

static mode_t
who(expr, fmode)
char	**expr;
register mode_t	*fmode;
{
	register mode_t m;
	register char	*maskstr = *expr;

	m = 0;
	for (; ; maskstr++) {
		switch (*maskstr) {
		case 'u':
			m |= USER;
			continue;
		case 'g':
			m |= GROUP;
			continue;
		case 'o':
			m |= OTHER;
			continue;
		case 'a':
			m |= ALL;
			continue;
		default:
			if (m == 0) {
				m = ALL;
				if (*maskstr != '=')
					m &= *fmode;
			}
			*expr = maskstr;
			return (m);
		}
	}
}

static int
what(expr)
char	**expr;
{
	register int	r;
	register char	*maskstr = *expr;

	switch (*maskstr) {
	case '+':
	case '-':
	case '=':
		r = *maskstr++;
		*expr = maskstr;
		return (r);
	}
	return (0);
}

int
strperm(expr, e, perm)
char*		expr;
char**		e;
register int	perm;
/*@
	assume expr!=0;
	assume e==0 || *e!=0;
@*/
{
	char	*maskstr = expr;
	mode_t m, b;
	mode_t om;
	register int o, goon;

	om = (READ|WRITE|EXEC) & perm;
	do {
		m = who(&maskstr, &om);
		while (o = what(&maskstr)) {
			b = 0;
			goon = 0;
			switch (*maskstr) {
			case 'u':
				b = (om & ((mode_t)USER)) >> 6;
				goto dup;
			case 'g':
				b = (om & ((mode_t)GROUP)) >> 3;
				goto dup;
			case 'o':
				b = (om & ((mode_t)OTHER));
		    dup:
				b &= READ|WRITE|EXEC;
				b |= (b << 3) | (b << 6);
				maskstr++;
				goon = 1;
			}
			while (goon == 0) {
				switch (*maskstr++) {
				case 'r':
					b |= READ;
					continue;
				case 'w':
					b |= WRITE;
					continue;
				case 'x':
					b |= EXEC;
					continue;
				case 'X':
				case 's':
					continue;
				default:
					maskstr--;
					goon = 1;
				}
			}

			b &= m;

			switch (o) {
			case '+':
				/* create new mode */
				om |= b;
				break;
			case '-':
				/* create new mode */
				om &= ~b;
				break;
			case '=':
				/* create new mode */
				om &= ~m;
				om |= b;
				break;
			}
		}
	} while (*maskstr++ == ',');
	maskstr--;
	*e = maskstr;

	return (om);
}
