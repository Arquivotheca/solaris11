/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include "file64.h"
#include <sys/localedef.h>
#include "mse.h"

/*
 * returns the number of characters for a SINGLE-BYTE codeset
 */
/* ARGSUSED */	/* *handle required for interface, don't remove */
size_t
__mbrlen_sb(_LC_charmap_t *handle, const char *s, size_t len, mbstate_t *ps)
{
	/*
	 * If s is NULL, this is equivalent to mbrlen("", 1, ps),
	 * because:
	 *   mbrlen(s, n, ps) <==> mbrtowc(NULL, s, n, ps),
	 * For mbrtowc(), if s is NULL, then:
	 *   mbrtowc(NULL, NULL, n, ps) <==> mbrtowc(NULL, "", 1, ps),
	 * But note:
	 *   mbrtowc(NULL, "", 1, ps) <==> mbrlen("", 1, ps)
	 */
	if (s == NULL) {
		s = "";
		len = 1;
	}
	/*
	 * zero bytes contribute to an incomplete, but
	 * potentially valid character
	 */
	if (len == 0) {
		return ((size_t)-2);
	}

	MBSTATE_RESTART(ps);

	/*
	 * if s points to a NULL return 0
	 */
	if (*s == '\0')
		return (0);

	return (1);
}
