/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * FUNCTIONS: __wcstombs_sb
 */

#include "lint.h"
#include "file64.h"
#include <errno.h>
#include <sys/localedef.h>
#include <sys/types.h>
#include "libc.h"
#include "mse.h"

/*ARGSUSED*/	/* *hdl, *ps required for interface, don't remove */
size_t
__wcsrtombs_sb(_LC_charmap_t *hdl, char *dst, const wchar_t **src, size_t len,
		mbstate_t *ps)
{
	size_t n = len;
	wchar_t *src0 = (wchar_t *)*src;
	size_t totbytes;

	MBSTATE_RESTART(ps);

	/*
	 * if dst is a null pointer, just count the number of characters
	 * in *src;  don't update *src.
	 */
	if (dst == NULL) {
		while (*src0 != '\0')
			src0++;
		return (src0 - *src);
	}

	/*
	 * only do len or less characters
	 */
	while (n-- > 0) {
		if ((uint32_t)**src < 256U) {
			*dst = (char)**src;
		} else {
			errno = EILSEQ;
			return ((size_t)-1);
		}

	/*
	 * if *src is null, return
	 */
		if (**src == '\0') {
			totbytes = *src - src0;
			*src = NULL;
			return (totbytes);
		}


	/*
	 * increment dst to the next character
	 */
		dst++;
		(*src)++;
	}

	/*
	 *  Ran out of room in dst before null was hit on wcs, return len
	 */
	return (len);
}
