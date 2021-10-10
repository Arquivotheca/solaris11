/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * COPYRIGHT NOTICE
 *
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 */
/*
 * OSF/1 1.2
 */
/*
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: err.c,v $ $Revision: 1.4.6.3 $"
 *	" (OSF) $Date: 1992/12/11 14:36:53 $";
 * #endif
 */
/*
 * COMPONENT_NAME: (CMDLOC) Locale Database Commands
 *
 * FUNCTIONS: error, diag_error, safe_malloc, yyerror
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.11  com/cmd/nls/err.c, cmdnls, bos320, 9132320m 8/11/91 14:08:39
 */
#include <stdio.h>
#include <stdlib.h>
#include "locdef.h"


/*
 * Global indicating if an error has been encountered in the source or not.
 * This flag is used in conjunction with the -c option to decide if a locale
 * should be created or not.
 */
int err_flag = 0;


/*
 *  FUNCTION: usage
 *
 *  DESCRIPTION:
 *  Prints a usage statement to stderr and exits with a return code 'status'
 */
void
usage(int status)
{
	(void) fprintf(stderr, "%s", gettext(ERR_USAGE));

	exit(status);
}


/*
 *  FUNCTION: error
 *
 *  DESCRIPTION:
 *  Generic error routine.  This function takes a variable number of arguments
 *  and passes them on to vprintf with the error message text as a format.
 *
 *  Errors from this routine are fatal.
 */
void
error(int err, const char *emsg, ...)
{
	va_list ap;
	va_start(ap, emsg);

	(void) fprintf(stderr, gettext(ERR_ERROR),
	    yyfilenm, yylineno, yycharno);

	(void) vfprintf(stderr, emsg, ap);
	va_end(ap);

	exit(err);
}

/*
 *  FUNCTION: diag_error
 *
 *  DESCRIPTION:
 *  Generic error routine.  This function takes a variable number of arguments
 *  and passes them on to vprintf with the error message text as a format.
 *
 *  Errors from this routine are considered non-fatal, and if the -c flag
 *  is set will still allow generation of a locale.
 */
void
diag_error(const char *emsg, ...)
{
	va_list ap;

	va_start(ap, emsg);
	diag_verror(emsg, ap);
	va_end(ap);
}

void
diag_verror(const char *emsg, va_list ap)
{
	err_flag++;

	(void) fprintf(stderr, gettext(ERR_WARNING),
	    yyfilenm, yylineno, yycharno);

	(void) vfprintf(stderr, emsg, ap);
}

/*
 * FUNCTION: diag_error2
 *
 * DESCRIPTION:
 * This function is equivalent to diag_error() except that the generic
 * warning message beginning with "localedef [WARNING]:" is not generated.
 *
 */
void
diag_error2(const char *emsg, ...)
{
	va_list	ap;

	va_start(ap, emsg);

	err_flag++;

	(void) vfprintf(stderr, emsg, ap);
	va_end(ap);
}

/*
 *  FUNCTION: yyerror
 *
 *  DESCRIPTION:
 *  Replacement for the yyerror() routine.  This is called by the yacc
 *  generated parser when a syntax error is encountered.
 *
 *  Syntax errors are considered fatal.
 */
/* ARGSUSED */
void
yyerror(const char *s)
{
	(void) fprintf(stderr, gettext(ERR_SYNTAX),
	    yyfilenm, yylineno, yycharno);

	exit(4);
}


/*
 *  FUNCTION: safe_malloc
 *
 *  DESCRIPTION:
 *  Backend for the MALLOC macro which verifies that memory is available.
 *
 *  Out-of-memory results in a fatal error.
 */
void *
safe_malloc(size_t bytes, const char *file, int lineno)
{
	void	*p;

	p = calloc(bytes, 1);
	if (p == NULL)
		error(4, gettext(ERR_MEM_ALLOC_FAIL), file, lineno);

	return (p);
}

void *
safe_realloc(void *p, size_t new_size, const char *file, int lineno)
{
	char	*q;

	q = realloc(p, new_size);
	if (q == NULL)
		error(4, gettext(ERR_MEM_ALLOC_FAIL), file, lineno);

	return (q);
}

char *
safe_strdup(const char *str, const char *file, int lineno)
{
	char	*s;

	s = strdup(str);
	if (s == NULL)
		error(4, gettext(ERR_MEM_ALLOC_FAIL), file, lineno);

	return (s);
}
