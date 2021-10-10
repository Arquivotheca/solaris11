/*
 * Copyright 1996-2002 Sun Microsystems, Inc.  All rights reserved.
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
 * #ifndef lint
 * #ifndef _NOIDENT
 * static char rcsid[] = "@(#) $RCSfile: check.c,v $ $Revision: 1.2.2.3 $"
 *	" (OSF) $Date: 1992/03/16 18:08:40 $";
 * #endif
 * #endif
 */
/*
 * COMPONENT_NAME: (CMDLOC) Locale Database Commands
 *
 * FUNCTIONS: check_upper, check_lower, check_alpha, check_space,
 *	      check_cntl, check_print, check_graph, check_punct
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.2  com/cmd/nls/check.c, cmdnls, bos320, 9132320m 8/9/91 15:04:53
 */

/*
 * All these functions follow the same logic. For 0 through max_wchar_enc
 * check the mask of the char. If the character has the characteristic
 * being tested, check further to make sure it has none of the characteristic's
 * that are invalid. (ie. an upper can not be a control, a punct, a digit,
 * or a space). These are all POSIX checks (from Draft 11).
 */
#include <ctype.h>
#include "locdef.h"


void
check_upper(void)
{
	int i;
	int fail = FALSE;
	int upper = FALSE;

	for (i = 0; i <= max_wchar_enc; i++) {
		if (ctype.mask[i] & _ISUPPER) {
			upper = TRUE;
			if ((ctype.mask[i] & _ISCNTRL) ||
			    (ctype.mask[i] & _ISDIGIT) ||
			    (ctype.mask[i] & _ISPUNCT) ||
			    (ctype.mask[i] & _ISSPACE))
				fail = TRUE;
		}
	}
	if (fail)
		diag_error(gettext(ERR_INVALID_CLASS), "upper");

	/* no upper specified, define default set */
	fail = FALSE;
	if (!upper)
		for (i = 'A'; i <= 'Z'; i++) {
			ctype.mask[i] |= _ISUPPER;
			if ((ctype.mask[i] & _ISCNTRL) ||
			    (ctype.mask[i] & _ISDIGIT) ||
			    (ctype.mask[i] & _ISPUNCT) ||
			    (ctype.mask[i] & _ISSPACE))
				fail = TRUE;
		}
	if (fail)
		diag_error(gettext(ERR_INVALID_CLASS), "upper");
}

void
check_lower(void)
{
	int i;
	int fail = FALSE;
	int lower = FALSE;

	for (i = 0; i <= max_wchar_enc; i++) {
		if (ctype.mask[i] & _ISLOWER) {
			lower = TRUE;
			if ((ctype.mask[i] & _ISCNTRL) ||
			    (ctype.mask[i] & _ISDIGIT) ||
			    (ctype.mask[i] & _ISPUNCT) ||
			    (ctype.mask[i] & _ISSPACE))
				fail = TRUE;
		}
	}
	if (fail)
		diag_error(gettext(ERR_INVALID_CLASS), "lower");

	/* no lower specified, define default set */
	fail = FALSE;
	if (!lower)
		for (i = 'a'; i <= 'z'; i++) {
			ctype.mask[i] |= _ISLOWER;
			if ((ctype.mask[i] & _ISCNTRL) ||
			    (ctype.mask[i] & _ISDIGIT) ||
			    (ctype.mask[i] & _ISPUNCT) ||
			    (ctype.mask[i] & _ISSPACE))
				fail = TRUE;
		}
	if (fail)
		diag_error(gettext(ERR_INVALID_CLASS), "lower");
}

void
check_alpha(void)
{
	int i;
	int fail = FALSE;

	for (i = 0; i <= max_wchar_enc; i++) {
		if (ctype.mask[i] & _ISALPHA) {
			if ((ctype.mask[i] & _ISCNTRL) ||
			    (ctype.mask[i] & _ISDIGIT) ||
			    (ctype.mask[i] & _ISPUNCT) ||
			    (ctype.mask[i] & _ISSPACE))
				fail = TRUE;
		}
	}
	if (fail)
		diag_error(gettext(ERR_INVALID_CLASS), "alpha");
}

void
check_space(void)
{
	int i;
	int fail = FALSE;
	int space = FALSE;
	/*
	 * default values for tab, newline, verticle tab, form
	 * feed, carriage return, and space
	 */
	int def_space[6] = {'\t', '\n', 11, 12, 13, ' '};


	for (i = 0; i <= max_wchar_enc; i++) {
		if (ctype.mask[i] & _ISSPACE) {
			space = TRUE;
			if ((ctype.mask[i] & _ISDIGIT) ||
			    (ctype.mask[i] & _ISUPPER) ||
			    (ctype.mask[i] & _ISLOWER) ||
			    (ctype.mask[i] & _ISALPHA) ||
			    (ctype.mask[i] & _ISGRAPH) ||
			    (ctype.mask[i] & _ISXDIGIT))
				fail = TRUE;
		}
	}
	if (fail)
		diag_error(gettext(ERR_INVALID_CLASS), "space");

	/* no space specified, define default values */
	fail = FALSE;
	if (!space)
		for (i = 0; i < (sizeof (def_space) / sizeof (def_space[0]));
		    i++) {
			ctype.mask[def_space[i]] |= _ISSPACE;
			if ((ctype.mask[def_space[i]] & _ISDIGIT) ||
			    (ctype.mask[def_space[i]] & _ISUPPER) ||
			    (ctype.mask[def_space[i]] & _ISLOWER) ||
			    (ctype.mask[def_space[i]] & _ISALPHA) ||
			    (ctype.mask[def_space[i]] & _ISGRAPH) ||
			    (ctype.mask[def_space[i]] & _ISXDIGIT))
				fail = TRUE;
		}
	if (fail)
		diag_error(gettext(ERR_INVALID_CLASS), "space");
}

void
check_cntl(void)
{
	int i;
	int fail = FALSE;

	for (i = 0; i <= max_wchar_enc; i++) {
		if (ctype.mask[i] & _ISCNTRL) {
			if ((ctype.mask[i] & _ISUPPER) ||
			    (ctype.mask[i] & _ISLOWER) ||
			    (ctype.mask[i] & _ISALPHA) ||
			    (ctype.mask[i] & _ISDIGIT) ||
			    (ctype.mask[i] & _ISGRAPH) ||
			    (ctype.mask[i] & _ISPUNCT) ||
			    (ctype.mask[i] & _ISPRINT) ||
			    (ctype.mask[i] & _ISXDIGIT))
				fail = TRUE;
		}
	}
	if (fail)
		diag_error(gettext(ERR_INVALID_CLASS), "cntrl");
}

void
check_punct(void)
{
	int i;
	int fail = FALSE;

	for (i = 0; i <= max_wchar_enc; i++) {
		if (ctype.mask[i] & _ISPUNCT) {
			if ((ctype.mask[i] & _ISUPPER) ||
			    (ctype.mask[i] & _ISLOWER) ||
			    (ctype.mask[i] & _ISALPHA) ||
			    (ctype.mask[i] & _ISDIGIT) ||
			    (ctype.mask[i] & _ISCNTRL) ||
			    (ctype.mask[i] & _ISXDIGIT) ||
			    (ctype.mask[i] & _ISSPACE))
				fail = TRUE;
		}
	}
	if (fail)
		diag_error(gettext(ERR_INVALID_CLASS), "punct");
}

void
check_graph(void)
{
	int i;
	int fail = FALSE;

	for (i = 0; i <= max_wchar_enc; i++) {
		if (ctype.mask[i] & _ISGRAPH) {
			if (ctype.mask[i] & _ISCNTRL)
				fail = TRUE;
		}
	}
	if (fail)
		diag_error(gettext(ERR_INVALID_CLASS), "graph");
}

void
check_print(void)
{
	int i;
	int fail = FALSE;

	for (i = 0; i <= max_wchar_enc; i++) {
		if (ctype.mask[i] & _ISPRINT) {
			if (ctype.mask[i] & _ISCNTRL)
				fail = TRUE;
		}
	}
	if (fail)
		diag_error(gettext(ERR_INVALID_CLASS), "print");
}

void
check_digits(void)
{
	int i;
	int fail = FALSE;
	int digit = FALSE;

	/*
	 * if digit is specified 0-9 must be specified as digits
	 */
	for (i = 0; i <= max_wchar_enc; i++) {
		if (ctype.mask[i] & _ISDIGIT)
			digit = TRUE;
		if ((i >= '0') && (i <= '9')) {
			if (!(ctype.mask[i] & _ISDIGIT))
				fail = TRUE;
		}
	}

	if (fail && digit)
		diag_error(gettext(ERR_INV_DIGIT));

	/* no digit specified, defined default set */
	if (!digit)
		for (i = '0'; i <= '9'; i++)
			ctype.mask[i] |= _ISDIGIT;
}

void
check_xdigit(void)
{
	int i;
	int fail = FALSE;
	int xdigit = FALSE;

	/*
	 * if xdigit is specified, 0-9 must be specified as xdigits
	 */
	for (i = 0; i <= max_wchar_enc; i++) {
		if (ctype.mask[i] & _ISXDIGIT)
			xdigit = TRUE;
		if ((i >= '0') && (i <= '9'))  {
			if (!(ctype.mask[i] & _ISXDIGIT))
				fail = TRUE;
		}
	}
	if (fail && xdigit)
		diag_error(gettext(ERR_INV_XDIGIT));

	/* xdigit not specified, define default values */
	if (!xdigit) {
		for (i = '0'; i <= '9'; i++)
			ctype.mask[i] |= _ISXDIGIT;
		for (i = 'a'; i <= 'f'; i++)
			ctype.mask[i] |= _ISXDIGIT;
		for (i = 'A'; i <= 'F'; i++)
			ctype.mask[i] |= _ISXDIGIT;
	}
}
