/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * shell arithmetic - uses streval library
 */

#include	"defs.h"
#include	"streval.h"

extern int	sh_lastbase;
int expflag = 0;

static longlong_t arith_expr(char **, struct lval *, int, longlong_t);

#ifdef FLOAT
    extern double atof();
#endif /* FLOAT */

static longlong_t arith(ptr, lvalue, type, n)
char **ptr;
struct lval *lvalue;
longlong_t n;
{
	register longlong_t r= 0;
	char *str = *ptr;
	int	d;
	switch(type)
	{
	case ASSIGN:
	{
		register struct namnod *np = (struct namnod*)(lvalue->value);
		if(nam_istype(np, N_ARRAY))
			array_ptr(np)->cur[0] = lvalue->flag;
		nam_longput(np, n);
		break;
	}
	case LOOKUP:
	{
		register wchar_t c = mb_peekc((const char *)str);
		lvalue->value = (char*)0;
		if(sh_iswalpha(c))
		{
			char	save;
			register struct namnod *np;
			while (mb_nextc((const char **)&str),
				c = mb_peekc((const char*)str), sh_iswalnum(c));
			save = *str;
			*str = 0;
			np = nam_search(*ptr,sh.var_tree,N_ADD);
			*str = save;
			if (c=='(')
			{
				lvalue->value = (char*)e_function;
				str = *ptr;
				break;
			}
			else if(c=='[')
			{
				str =array_subscript(np,str);
			}
			else if(nam_istype(np,N_ARRAY))
				array_dotset(np,ARRAY_UNDEF);
			lvalue->value = (char*)np;
			if(nam_istype(np,N_ARRAY))
				lvalue->flag = array_ptr(np)->cur[0];
		}
		else
		{
#ifdef FLOAT
			char isfloat = 0;
#endif /* FLOAT */
			char *str_save = NULL;
			sh_lastbase = 10;
			while (str_save = str,
				(c= mb_nextc((const char **)&str)))
			switch(c)
			{
			case '#':
				sh_lastbase = r;
				r = 0;
				break;
			case '.':
			{
				/* skip past digits */
				if(sh_lastbase!=10)
					goto badnumber;
				while (str_save = str,
					(c= mb_nextc((const char **)&str))
						!= L'\0' && sh_iswdigit(c));
#ifdef FLOAT
				isfloat = 1;
				if(c=='e' || c == 'E')
				{
				dofloat:
					c = *str;
					if(c=='-'||c=='+')
						c= *++str;
					if(!isdigit(c))
						goto badnumber;
					while(c= *str++,isdigit(c));
				}
				else if(!isfloat)
					goto badnumber;
				set_float();
				r = atof(*ptr);
#endif /* FLOAT */
				goto breakloop;
			}
			default:
				/*
				 * Digit appears after the
				 * integer-suffix
				 */
				if (sh_iswdigit(c)) {
					d = c - '0';
				} else if (iswascii(c) && iswupper(c))
					d = c - ('A'-10); 
				else if (iswascii(c) && iswlower(c))
					d = c - ('a'-10); 
				else
					goto breakloop;
				if (d < sh_lastbase)
					r = sh_lastbase*r + d;
				else
				{
#ifdef FLOAT
					if(c == 0xe && sh_lastbase==10)
						goto dofloat;
#endif /* FLOAT */
					goto badnumber;
				}
			}
		breakloop:
			str = str_save;
		}
		break;

	badnumber:
		lvalue->value = (char*)e_number;
		return(r);
	}
	case VALUE:
	{
		register union Namval *up;
		register struct namnod *np;
		if(is_option(NOEXEC))
			return(0);
		np = (struct namnod*)(lvalue->value);
               	if (nam_istype (np, N_INTGER))
		{
#ifdef NAME_SCOPE
			if (nam_istype (np,N_CWRITE))
				np = nam_copy(np,1);
#endif
			if(nam_istype (np, N_ARRAY))
				up = &(array_find(np,A_ASSIGN)->namval);
			else
				up= &np->value.namval;
			if(nam_istype(np,N_INDIRECT))
				up = up->up;
			if(nam_istype (np, (N_BLTNOD)))
				r = (long)((*up->fp->f_vp)());
			else if(up->lp==NULL)
				r = 0;
#ifdef FLOAT
			else if(nam_istype (np, N_DOUBLE))
			{
				set_float();
       	                	r = *up->dp;
			}
#endif /* FLOAT */
			else
       	                	r = *up->lp;
		}
		else
		{
			if((str=nam_strval(np))==0 || *str==0)
				*ptr = 0;
			else
			{
				/*
				 * Check if came from arith_expr(), thus from
				 * expanding $(( )). If so, need to go back to
				 * arith_expr() and go through its LOOKUP, so
				 * that code understands oct and hex.
				 */
				if (!expflag)
					r = streval(str, &str, arith);
				else
				{
					expflag = 0;
					r = streval(str, &str, arith_expr);
				}
			}
		}
		return(r);
	}
	case ERRMSG:
        /* XPG4: exit status for test(1) > 1 */
		if (sh.cmdname && ((strcmp(sh.cmdname, "test") == 0) ||
				(strcmp(sh.cmdname, "[") == 0)))
	        cmd_shfail(*ptr, lvalue->value, ETEST);
		else
			sh_fail(*ptr,lvalue->value);
	}

	*ptr = str;
	return(r);
}

longlong_t sh_arith(str)
char *str;
{
	return(streval(str,&str, arith));
}

/*
 * This function behaves same as arith() except that it
 * recognizes ISO C integer constants.
 * This function is called while expanding $(( ))
 */
static longlong_t arith_expr(ptr, lvalue, type, n)
char **ptr;
struct lval *lvalue;
longlong_t n;
{
	register longlong_t r = 0;
	char *str = *ptr;
	int	d;
	switch (type) {
	case ASSIGN:
	case ERRMSG:
		return (arith(ptr, lvalue, type, n));
	case VALUE:
	{
#ifdef XPG4
	  /*
	   * This flag is set to make sure that we came through $(( ))
	   * expression.
	   */
		expflag = 1;
#endif
		return (arith(ptr, lvalue, type, n));
	}
	case LOOKUP:
	{
		register wchar_t c = mb_peekc((const char *)str);
		lvalue->value = (char *)0;
		if (sh_iswalpha(c)) {
			char	save;
			register struct namnod *np;
			while (mb_nextc((const char **)&str),
				c = mb_peekc((const char *)str),
				    sh_iswalnum(c));
			save = *str;
			*str = 0;
			np = nam_search(*ptr, sh.var_tree, N_ADD);
			*str = save;
			if (c == '(') {
				lvalue->value = (char *)e_function;
				str = * ptr;
				break;
			} else if (c == '[') {
				str = array_subscript(np, str);
			} else if (nam_istype(np, N_ARRAY))
				array_dotset(np, ARRAY_UNDEF);
			lvalue->value = (char *)np;
			if (nam_istype(np, N_ARRAY))
				lvalue->flag = array_ptr(np)->cur[0];
		} else {
#ifdef FLOAT
			char isfloat = 0;
#endif /* FLOAT */
			unsigned int first_zero = 1;
			unsigned int u_num = 0;
			unsigned int l_num = 0;
			char *str_save = NULL;
			sh_lastbase = 10;
			while (str_save = str,
				(c = mb_nextc((const char **)&str)))
			switch (c) {
			case '#':
				sh_lastbase = r;
				r = 0;
				break;
			case '.':
			{
				/* skip past digits */
				if (sh_lastbase != 10)
					goto badnumber;
				while (str_save = str,
				    (c = mb_nextc((const char **)&str))
				    != L'\0' && sh_iswdigit(c));
#ifdef FLOAT
				isfloat = 1;
				if (c == 'e' || c == 'E') {
				dofloat:
					c = *str;
					if (c == '-' || c == '+')
						c = *++str;
					if (!isdigit(c))
						goto badnumber;
					while (c = *str++, isdigit(c));
				} else if (!isfloat)
					goto badnumber;
				set_float();
				r = atof(*ptr);
#endif /* FLOAT */
				goto breakloop;
			}
			/*
			 * The following code recognizes
			 * integer-suffixes. The suffixes can be
			 *
			 *	u		unsigned
			 *	ul or lu	unsigned long
			 *	ull or llu	unsigned long long
			 * All these constants are stored as longlong_t
			 */
			case 'u':
			case 'U':
				if (sh_lastbase == 8 || sh_lastbase == 10 ||
				    sh_lastbase == 16) {
					if (u_num)
						goto badnumber;
					u_num = 1;
					break;
				}
			case 'l':
			case 'L':
				if (sh_lastbase == 8 || sh_lastbase == 10 ||
				    sh_lastbase == 16) {
					if (l_num)
						goto badnumber;
					l_num = 1;
					/* check for ll or LL */
					if ((mb_nextc((const char **)&str))
					    != c) {
					str--;
					}
					break;
				}
			default:
				/*
				 * Digit appears after the
				 * integer-suffix
				 */
				if (sh_iswdigit(c)) {
					d = c - '0';
					if (c == '0' && first_zero) {
					/*
					 * Number starts with a zero.
					 * It should be either octal or
					 * hexadecimal or decimal 0
					 */
						c = mb_nextc((const
						    char **)&str);
						if (c == 'x' ||
						    c == 'X') {
							sh_lastbase =
							    16;
						} else {
#ifdef XPG4
/*
 * Recognizing octals in /usr/bin/ksh causes incompatibility.
 * So, Recognize octals only in /usr/xpg4/bin/sh.
 */
							sh_lastbase =
							sh_iswdigit(c) ? 8 : 10;
#endif
							str --;
						}
					}
					first_zero = 0;
				} else if (iswascii(c) && iswupper(c))
					d = c - ('A' - 10);
				else if (iswascii(c) && iswlower(c))
					d = c - ('a'- 10);
				else
					goto breakloop;
				if (u_num || l_num)
					goto badnumber;
				if (d < sh_lastbase)
					r = sh_lastbase * r + d;
				else {
#ifdef FLOAT
					if (c == 0xe &&
					    sh_lastbase == 10)
						goto dofloat;
#endif /* FLOAT */
					goto badnumber;
				}
			}
		breakloop:
			str = str_save;
		}
		break;

	badnumber:
		lvalue->value = (char *)e_number;
		return (r);

	}
	}

	*ptr = str;
	return (r);
}

/*
 * same as sh_arith() except that it uses arith_expr()
 */
longlong_t
sh_arith_expr(str)
char *str;
{
	return (streval(str, &str, arith_expr));
}
