/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "iconv_int.h"

void
error(const char *emsg, ...)
{
	va_list	ap;
	va_start(ap, emsg);

	(void) fprintf(stderr,
	    gettext("iconv [ERROR]: FILE: %s, LINE: %d, CHAR: %d\n"),
	    yyfilenm, yylineno, yycharno);
	(void) vfprintf(stderr, emsg, ap);
	(void) fflush(stderr);

	va_end(ap);

	exit(1);
}

/*ARGSUSED*/
void
yyerror(const char *s)
{
	(void) fprintf(stderr,
	    gettext("iconv [SYNTAX ERROR]: FILE: %s, LINE: %d, CHAR: %d\n"),
	    yyfilenm, yylineno, yycharno);
	(void) fflush(stderr);

	exit(4);
}

char *
safe_strdup(const char *str)
{
	char	*s;

	s = strdup(str);
	if (s == NULL)
		error(gettext("Memory allocation failure\n"));

	return (s);
}

void *
safe_malloc(size_t bytes)
{
	void	*p;

	p = calloc(bytes, 1);
	if (p == NULL)
		error(gettext("Memory allocation failure\n"));

	return (p);
}

void *
safe_realloc(void *p, size_t bytes)
{
	void	*q;

	q = realloc(p, bytes);
	if (q == NULL)
		error(gettext("Memory allocation failure\n"));

	return (q);
}
