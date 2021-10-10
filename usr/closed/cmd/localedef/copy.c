/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
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
 * static char rcsid[] = "@(#)$RCSfile: copy.c,v $ $Revision: 1.1.2.2 $"
 *	" (OSF) $Date: 1992/08/10 14:43:44 $";
 * #endif
 */

#include "locdef.h"


int	copying[_LastCategory + 1] = {0, 0, 0, 0, 0, 0};
int	copyflag = 0;

static const char	*cat_name[] = {
	"LC_CTYPE", "LC_NUMERIC", "LC_TIME",
	"LC_COLLATE", "LC_MONETARY", "LC_MESSAGES"
};

static const char	*charmap_name[] = {
	"mbtowc",	"mbstowcs",
	"wctomb",	"wcstombs",
	"mblen",	"wcswidth",
	"wcwidth",	"mbftowc",
	"fgetwc",	"btowc",
	"wctob",	"mbsinit",
	"mbrlen",	"mbrtowc",
	"wcrtomb",	"mbsrtowcs",
	"wcsrtombs",	"eucpctowc",
	"wctoeucpc",
	NULL
};

static const int	charmap_native[] = {
	CHARMAP_MBTOWC_AT_NATIVE, CHARMAP_MBSTOWCS_AT_NATIVE,
	CHARMAP_WCTOMB_AT_NATIVE, CHARMAP_WCSTOMBS_AT_NATIVE,
	CHARMAP_MBLEN, CHARMAP_WCSWIDTH_AT_NATIVE,
	CHARMAP_WCWIDTH_AT_NATIVE, CHARMAP_MBFTOWC_AT_NATIVE,
	CHARMAP_FGETWC_AT_NATIVE, CHARMAP_BTOWC_AT_NATIVE,
	CHARMAP_WCTOB_AT_NATIVE, CHARMAP_MBSINIT,
	CHARMAP_MBRLEN, CHARMAP_MBRTOWC_AT_NATIVE,
	CHARMAP_WCRTOMB_AT_NATIVE, CHARMAP_MBSRTOWCS_AT_NATIVE,
	CHARMAP_WCSRTOMBS_AT_NATIVE,
	CHARMAP_EUCPCTOWC, CHARMAP_WCTOEUCPC,
	-1
};

static const int	charmap_user[] = {
	CHARMAP_MBTOWC, CHARMAP_MBSTOWCS,
	CHARMAP_WCTOMB, CHARMAP_WCSTOMBS,
	-1, CHARMAP_WCSWIDTH,
	CHARMAP_WCWIDTH, CHARMAP_MBFTOWC,
	CHARMAP_FGETWC, CHARMAP_BTOWC,
	CHARMAP_WCTOB,  -1,
	-1,	CHARMAP_MBRTOWC,
	CHARMAP_WCRTOMB, CHARMAP_MBSRTOWCS,
	CHARMAP_WCSRTOMBS, -1,
	-1,
	-1
};

static const char	*ctype_name[] = {
	"wctype",	"iswctype",
	"towupper",	"towlower",
	"trwctype",	"wctrans",
	"towctrans",
	NULL
};

static const int	ctype_native[] = {
	CTYPE_WCTYPE, CTYPE_ISWCTYPE_AT_NATIVE,
	CTYPE_TOWUPPER_AT_NATIVE, CTYPE_TOWLOWER_AT_NATIVE,
	CTYPE_TRWCTYPE, CTYPE_WCTRANS,
	CTYPE_TOWCTRANS_AT_NATIVE,
	-1
};

static const int	ctype_user[] = {
	-1,	CTYPE_ISWCTYPE,
	CTYPE_TOWUPPER,	CTYPE_TOWLOWER,
	-1, -1,
	CTYPE_TOWCTRANS,
	-1
};

static const char	*collate_name[] = {
	"strcoll",	"strxfrm",
	"wcscoll",	"wcsxfrm",
	"fnmatch",	"regcomp",
	"regerror",	"regexec",
	"regfree",
	NULL
};

static const int	collate_native[] = {
	COLLATE_STRCOLL, COLLATE_STRXFRM,
	COLLATE_WCSCOLL_AT_NATIVE, COLLATE_WCSXFRM_AT_NATIVE,
	COLLATE_FNMATCH, COLLATE_REGCOMP,
	COLLATE_REGERROR, COLLATE_REGEXEC,
	COLLATE_REGFREE,
	-1
};

static const int	collate_user[] = {
	-1, -1,
	COLLATE_WCSCOLL, COLLATE_WCSXFRM,
	-1, -1,
	-1, -1,
	-1,
	-1
};

static const char	*time_name[] = {
	"strftime",	"strptime",
	"getdate",	"wcsftime",
	NULL
};

static const int	time_native[] = {
	TIME_STRFTIME, TIME_STRPTIME,
	TIME_GETDATE, TIME_WCSFTIME,
	-1
};

static const char	*monetary_name[] = {
	"strfmon",
	NULL
};

static const int	monetary_native[] = {
	MONETARY_STRFMON,
	-1
};

struct category_t {
	const char	**name;
	const int	*userlist;
	const int	*nativelist;
};

static struct category_t	category_tbl[] = {
	{charmap_name, charmap_user, charmap_native},
	{ctype_name, ctype_user, ctype_native},
	{NULL, NULL, NULL},
	{time_name, NULL, time_native},
	{collate_name, collate_user, collate_native},
	{monetary_name, NULL, monetary_native},
	{NULL, NULL, NULL}
};


/*
 * Copy_locale  - routine to copy section of locale input files
 * from an existing, installed, locale.
 *
 * We reassign pointers so gen() will use the existing structures.
 */
void
copy_locale(int category)
{
	item_t	*it;
	char *source;		/* user provided locale to copy from */
	char *orig_loc;		/* orginal locale */

	it = sem_pop();
	if (it->type != SK_STR)
		INTERNAL_ERROR;
	source = it->value.str;

	orig_loc = setlocale(category, NULL);
	if (setlocale(category, source) == NULL)
		error(4, gettext(ERR_CANNOT_LOAD_LOCALE), source);

	switch (category) {

	case LC_COLLATE:
		collate_ptr = __lc_collate;
		lc_collate_flag = 1;
		break;

	case LC_CTYPE:
		ctype_ptr = __lc_ctype;
		lc_ctype_flag = 1;
		break;

	case LC_MONETARY:
		monetary_ptr = __lc_monetary;
		lc_monetary_flag = 1;
		break;

	case LC_NUMERIC:
		numeric_ptr = __lc_numeric;
		lc_numeric_flag = 1;
		break;

	case LC_TIME:
		lc_time_ptr = __lc_time;
		lc_time_flag = 1;
		break;

	case LC_MESSAGES:
		messages_ptr = __lc_messages;
		lc_message_flag = 1;
		break;
	}

	copying[category] = 1;
	copyflag++;


	(void) setlocale(category, orig_loc);
}

void
output_copy_notice(FILE *fp, int bits)
{
	int	i;
	int	m;
	int	libflag = 0;
	const int	*ul, *nl;
	const char	**name;

	(void) fprintf(fp, "\n");

	(void) fprintf(fp, gettext(DIAG_COPY_USED));

	for (i = 0; i <= _LastCategory; i++) {
		if (copying[i]) {
			(void) fprintf(fp, "%s ", cat_name[i]);
		}
	}
	(void) fprintf(fp, "\n\n");

	(void) fprintf(fp, gettext(DIAG_COPY_METHODS));

	for (i = 0; i <= _LastCategory + 1; i++) {
		if (i == 0) {
			/*
			 * CHARMAP
			 * if copy is used with LC_CTYPE or LC_COLLATE
			 * CHARMAP also should be consistent
			 */
			if (!copying[LC_CTYPE] && !copying[LC_COLLATE]) {
				continue;
			}
		} else {
			if (!copying[i - 1])
				continue;
		}

		if (!category_tbl[i].name)
			continue;

		name = category_tbl[i].name;
		ul = category_tbl[i].userlist;
		nl = category_tbl[i].nativelist;
		m = 0;
		while (name[m]) {
			if (single_layer == TRUE) {
				if (nl) {
					if (nl[m] != -1) {
						(void) fprintf(fp,
						    "\t%s\t\"%s\"\n",
						    name[m],
						    METH_NAME(nl[m]));
					}
				}
			} else {
				if (ul) {
					if ((ul[m] != -1) &&
						(nl[m] != -1)) {
						(void) fprintf(fp,
						    "\t%s\t\"%s\"\n",
						    name[m],
						    METH_NAME(ul[m]));
						(void) fprintf(fp,
						    "\t%s@native\t\"%s\"\n",
						    name[m],
						    METH_NAME(nl[m]));
					} else if (nl[m] != -1) {
						(void) fprintf(fp,
						    "\t%s\t\"%s\"\n",
						    name[m],
						    METH_NAME(nl[m]));
					} else if (ul[m] != -1) {
						(void) fprintf(fp,
						    "\t%s\t\"%s\"\n",
						    name[m],
						    METH_NAME(ul[m]));
					}
				}
			}
			m++;
		}
	}

	(void) fprintf(fp, "\n");

	(void) fprintf(fp, gettext(DIAG_COPY_LINKLIBS));

	for (i = 0; i <= LAST_METHOD; i++) {
		if (bits == 64) {
			if (!lib_array[i].library64)
				break;
			(void) fprintf(fp, "\t\"%s\"\n",
				lib_array[i].library64);
			libflag++;
		} else {
			if (!lib_array[i].library)
				break;
			(void) fprintf(fp, "\t\"%s\"\n",
				lib_array[i].library);
			libflag++;
		}
	}
	if (!libflag) {
		(void) fprintf(fp, "\t\"libc\"\n");
	}
}
