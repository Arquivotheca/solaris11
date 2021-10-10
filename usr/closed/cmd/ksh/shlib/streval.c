#ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */

/*
 * Copyright 1996-2002 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * G. S. Fowler
 * D. G. Korn
 * AT&T Bell Laboratories
 *
 * arithmetic expression evaluator
 *
 * NOTE: all operands are evaluated as both the parse
 *	 and evaluation are done on the fly
 */

#include	"csi.h"
#ifdef KSHELL
#   include	"shtype.h"
#else
#   include	<ctype.h>
#endif	/* KSHELL */
#include "streval.h"

#define MAXLEVEL	9

struct vars			 /* vars stacked per invocation		*/
{
	char*		nextchr; /* next char in current expression	*/
	char*		curchr;  /* previous nextchr value for ungetchr() */
	char*		errchr;	 /* next char after error		*/
	struct lval	errmsg;	 /* error message text			*/
	char*		errstr;  /* error string			*/
#ifdef FLOAT
	char		isfloat; /* set when floating number		*/
#endif /* FLOAT */
};


#define getchr()	(cur.curchr=cur.nextchr,mb_nextc((const char **)&cur.nextchr))
#define peekchr()	(mb_peekc(cur.nextchr))
#define ungetchr()	(cur.nextchr = cur.curchr)

#define pushchr(s)	{struct vars old;old=cur;cur.curchr=cur.nextchr=(s);\
				cur.errmsg.value=0;cur.errstr=0
#define popchr()	cur=old;}
#define error(msg)	return(seterror(msg))

extern struct Optable optable[];

static struct vars	cur;
static char		noassign;	/* set to skip assignment	*/
static int		level;
static longlong_t	(*convert)();	/* external conversion routine		*/

static longlong_t	expr(int);		/* subexpression evaluator */
static number		seterror();	/* set error message string */
static struct Optable	*findop(wchar_t);	/* lookup operator */


/*
 * evaluate an integer arithmetic expression in s
 *
 * (number)(*convert)(char** end, struct lval* string, int type) is a user supplied
 * conversion routine that is called when unknown chars are encountered.
 * *end points to the part to be converted and must be adjusted by convert to
 * point to the next non-converted character; if typ is ERRMSG then string
 * points to an error message string
 *
 * NOTE: (*convert)() may call streval()
 */

longlong_t
streval(s, end, conv)
char*	s;
char**	end;
longlong_t (*conv)();
{
	longlong_t n;

	pushchr(s);
#ifdef FLOAT
	cur.isfloat = 0;
#endif /* FLOAT */
	convert = conv;
	if(level++ >= MAXLEVEL)
		(void)seterror(e_recursive);
	else
	{
		n = expr(0);
		if (peekchr() == ':') (void)seterror(e_badcolon);
	}
	if (cur.errmsg.value)
	{
		if(cur.errstr) s = cur.errstr;
		(void)(*convert)( &s , &cur.errmsg, ERRMSG);
		cur.nextchr = cur.errchr;
		n = 0;
	}
	if (end) *end = cur.nextchr;
	if(level>0) level--;
	popchr();
	return(n);
}

/*   
 * evaluate a subexpression with precedence
 */

static longlong_t
expr(precedence)
register int	precedence;
{
	register wchar_t	c;
	register longlong_t	n;
	register longlong_t	x;
	register struct Optable *op;
	int incr = 0;
	int wasop = 1;
	struct lval	assignop;
	struct lval	lvalue;
	char*		pos;
	char		invalid=0;

	while ((c=getchr()) && sh_iswspace(c));

	switch (c)
	{
	case 0:
		if(precedence>5)
			error(e_moretokens);
		return(0);

#ifdef future
	case '-':
		incr = -2;
	case '+':
		incr++;
		if (c != peekchr())
		{
			/* unary plus or minus */
			n = incr*expr(2*MAXPREC-1);
			incr = 0;
		}
		else /* ++ or -- */
		{
			invalid = 1;
			getchr();
		}
		break;
#else
	case '-':
		n = -expr(2*MAXPREC-1);
		break;

	case '+':
		n = expr(2*MAXPREC-1);
		break;
#endif

	case '!':
		n = !expr(2*MAXPREC-1);
		break;
	case '~':
#ifdef FLOAT
		if(cur.isfloat)
			error(e_incompatible);
#endif /* FLOAT */
		n = ~(long)expr(2*MAXPREC-1);
		break;
	default:
		ungetchr();
		invalid = 1;
		break;
	}
	lvalue.value = 0;
	while(1)
	{
		int	d;
		cur.errchr = cur.nextchr;
		if((c=getchr()) && sh_iswspace(c))
			continue;
		assignop.value = 0;
		if(sh_iswalnum(c))
			goto alphanumeric;
		op = findop(c);
		wasop++;
		/* check for assignment operation */
		if (c && peekchr()== '=' && !(op->precedence&NOASSIGN))
		{
			if(!noassign && (!lvalue.value || precedence > 2))
				error(e_notlvalue);
			assignop = lvalue;
			getchr();
			d = 3;
		}
		else
		{
			d = (op->precedence&PRECMASK);
			d *= 2;
		}
		/* from here on c is the new precedence level */
		if(lvalue.value && (op->opcode!=ASSIGNMENT))
		{
			n = (*convert)(&cur.nextchr, &lvalue, VALUE);
			if(cur.nextchr==0)
				error(e_number);
			if(!(op->precedence&SEQPOINT))
				lvalue.value = 0;
			invalid = 0;
		}
		if(invalid && op->opcode>ASSIGNMENT)
			error(e_synbad);
		if(precedence >= d)
			goto done;
		if(op->precedence&RASSOC)
			d--;
		if(d < 2*MAXPREC && !(op->precedence&SEQPOINT))
			x = expr(d);
#ifdef FLOAT
		if((op->precedence&NOFLOAT)&& cur.isfloat)
			error(e_incompatible);
#endif /* FLOAT */
		switch(op->opcode)
		{
		case RPAREN:
			error(e_paren);

		case LPAREN:
		{
#ifdef FLOAT
			char savefloat = cur.isfloat;
			cur.isfloat = 0;
#endif /* FLOAT */
			n = expr(2);
#ifdef FLOAT
			cur.isfloat = savefloat;
#endif /* FLOAT */
			if (getchr() != ')')
				error(e_paren);
			wasop = 0;
			break;
		}

#ifdef future
		case PLUSPLUS:
			incr = 1;
			goto common;
		case MINUSMINUS:
			incr = -1;
		common:
			x = n;
#endif
		case ASSIGNMENT:
			if (!noassign && !lvalue.value)
				error(e_notlvalue);
			n = x;
			assignop = lvalue;
			lvalue.value = 0;
			break;

		case QUEST:
		{
			int nextc;
			if (n)
			{
				x = expr(c);
				nextc = getchr();
			}
			noassign++;
			(void) expr(c);
			noassign--;
			if (!n)
			{
				nextc = getchr();
				x = expr(c);
			}
			n = x;
			if (nextc != ':') {
				(void) seterror(e_questcolon);
			}
			break;
		}
		case COLON:
			(void) seterror(e_badcolon);
			break;

		case OR:
			n = (longlong_t)n | (longlong_t)x;
			break;
#ifdef future
		case QCOLON:
#endif
		case OROR:
			if (n)
			{
				noassign++;
				expr(d);
				noassign--;
			}
			else
				n = expr(d);
#ifdef future
			if(op->opcode==OROR)
#endif
				n = (n!=0);
			lvalue.value = 0;
			break;

		case XOR:
			n = (longlong_t)n ^ (longlong_t)x;
			break;

		case NOT:
			error(e_synbad);

		case AND:
			n = (longlong_t)n & (longlong_t)x;
			break;

		case ANDAND:
			if(n==0)
			{
				noassign++;
				expr(d);
				noassign--;
			}
			else
				n = (expr(d)!=0);
			lvalue.value = 0;
			break;

		case EQ:
			n = n == x;
			break;

		case NEQ:
			n = n != x;
			break;

		case LT:
			n = n < x;
			break;

		case LSHIFT:
			n = (longlong_t)n << (longlong_t)x;
			break;

		case LE:
			n = n <= x;
			break;

		case GT:
			n = n > x;
			break;

		case RSHIFT:
			n = (longlong_t)n >> (longlong_t)x;
			break;

		case GE:
			n = n >= x;
			break;

		case PLUS:
			n +=  x;
			break;
			
		case MINUS:
			n -=  x;
			break;

		case TIMES:
			n *=  x;
			break;

		case DIVIDE:
			if(x!=0)
			{
				n /=  x;
				break;
			}

		case MOD:
			if(x!=0)
				n = n % x;
			else
				error(e_divzero);
			break;

		default:
		alphanumeric:
			if(!wasop)
				error(e_synbad);
			wasop = 0;
			pos = --cur.nextchr;
			n = (*convert)(&cur.nextchr, &lvalue, LOOKUP);
			if (cur.nextchr == pos)
			{
				if(cur.errmsg.value = lvalue.value)
					cur.errstr = pos;
				error(e_synbad);
			}
#ifdef future
			/* this handles ++x and --x */
			if(incr)
			{
				if(lvalue.value)
					n = (*convert)(&cur.nextchr, &lvalue, VALUE);
				n += incr;
				incr = 0;
				goto common;
			}
#endif
			break;
		}
		invalid = 0;
#ifdef FLOAT
		if((long)n != n)
			cur.isfloat++;
#endif /* FLOAT */
		if(!noassign && assignop.value)
			(void)(*convert)(&cur.nextchr,&assignop,ASSIGN,n+incr);
		incr = 0;
	}
 done:
	cur.nextchr = cur.errchr;
	return(n);
}

/*
 * set error message string
 */

static number
seterror(msg)
char*	msg;
{
	if(!cur.errmsg.value)
		cur.errmsg.value = msg;
	cur.errchr = cur.nextchr;
	cur.nextchr = "";
	level = 0;
	return(0);
}

/*
 * look for operator in table
 */

static
struct Optable *findop(w)
register wchar_t w;
{
	register struct Optable *op = optable;
	int	c;
	if (!iswascii(w) || ((c = (int)(w)) > '|'))
		return(op);
	while(++op)
	{
		if(c > op->name[0])
			continue;
		if(c < op->name[0])
			return(optable);
		if(op->name[1]==0)
			break;
		w = getchr();
		if (op->name[1]==w)
			break;
		if ((++op)->name[1]==w)
			break;
		if(op->name[1])
			op++;
		ungetchr();
		break;
	}
	return(op);
}

#ifdef FLOAT
set_float()
{
	cur.isfloat++;
}
#endif /* FLOAT */
