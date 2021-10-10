/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * COMPONENT_NAME: (LIBCCHR) LIBC Character Classification Funcions
 *
 * FUNCTIONS: __nl_langinfo_std
 *
 * ORIGINS: 27
 *
 * This module contains IBM CONFIDENTIAL code. -- (IBM
 * Confidential Restricted when combined with the aggregated
 * modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 * (C) COPYRIGHT International Business Machines Corp. 1989 , 1992
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 */

#include "lint.h"
#include "mtlib.h"
#include <sys/localedef.h>
#include <string.h>
#include <langinfo.h>
#include <locale.h>
#include <limits.h>
#include <thread.h>
#include "tsd.h"
#include "libc_i18n.h"
#include "libc.h"

#define	MAXANSLENGTH 128

/*
 *  FUNCTION: __nl_langinfo_std
 *
 *  DESCRIPTION:
 *  Returns the locale database string which corresponds to the specified
 *  nl_item.
 */

char *
__nl_langinfo_std(_LC_locale_t *hdl, nl_item item)
{
	char *ptr1, *ptr2;
	char *langinfobuf = tsdalloc(_T_NL_LANINFO, MAXANSLENGTH, NULL);
	static const char *xpg4_d_t_fmt = "%a %b %e %H:%M:%S %Y";

	if (langinfobuf == NULL)
		return ((char *)"");
	if (item >= _NL_NUM_ITEMS || item < 0) {
		*langinfobuf = '\0';
		return (langinfobuf);
	}

	if (item == CRNCYSTR) {
		ptr1 = hdl->nl_info[item];
		ptr2 = langinfobuf;
		if (hdl->nl_lconv->p_cs_precedes == CHAR_MAX || ptr1[0] == '\0')
			return ((char *)"");

		if (hdl->nl_lconv->p_cs_precedes == 1)
		    ptr2[0] = '-';
		else if (hdl->nl_lconv->p_cs_precedes == 0)
		    ptr2[0] = '+';

		(void) strncpy(&ptr2[1], ptr1, MAXANSLENGTH - 1);
		ptr2[MAXANSLENGTH] = '\0';
		return (ptr2);
	} else if (item == D_T_FMT) {
		if (__xpg4 != 0) {		/* XPG4 mode */
			if (IS_C_TIME(hdl->lc_time)) {
				return ((char *)xpg4_d_t_fmt);
			}
		}
		if (hdl->nl_info[D_T_FMT] == NULL) {
			return ((char *)"");
		} else {
			return (hdl->nl_info[D_T_FMT]);
		}
	} else if ((hdl)->nl_info[item] == NULL) {
		return ((char *)"");
	} else {
		return ((hdl)->nl_info[item]);
	}
}
