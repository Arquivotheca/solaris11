/*
 * Copyright (c) 1994, 1998 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident	"%Z%%M%	%I%	%E% SMI"

/*
 * isyesno.c - Test for yes/no/other meaning of a provided response.
 */

/* Headers */

#include <langinfo.h>
#include "pax.h"

/*
 * isyesno - determine if the string respMb constitutes a yes/no/other
 *	     response.
 *
 * returns:   1    if resp is equivalent to "no"
 *	      2    if resp is equivalent to "yes"
 *	      0    if resp is neither
 *	     -1	   if there was an error.
 */

int
isyesno(const char *respMb, size_t testLen)
{
	static	char	*yesMb;		/* The local version of yes	*/
	static	char	*noMb;		/* The local version of no	*/
	static	size_t	yesMbLen;	/* Multibyte length of yes	*/
	static	size_t	noMbLen;	/* Multibyte length of no	*/
	static	int	yesChWid;	/* Width of chars in yes	*/
	static	int	noChWid;	/* Width of chars in no		*/

	size_t	compLen;		/* Length to compare		*/
	size_t	respMbLen;		/* Length of the response	*/

	if (yesMb == NULL) {
		/*
		 *  Initialize structures on the first pass.
		 */

		yesMb = strdup(nl_langinfo(YESSTR));
		noMb  = strdup(nl_langinfo(NOSTR));

		if (yesMb == NULL || noMb == NULL) {
			perror("isyesno: strdup failed");
			return (-1);
		}

		yesMbLen = strlen(yesMb);
		noMbLen  = strlen(noMb);

		yesChWid = mblen(yesMb, yesMbLen);
		noChWid  = mblen(noMb, noMbLen);
	}

	/*
	 *  Figure out how many bytes to compare.
	 */

	respMbLen = strlen(respMb);
	compLen   = testLen > respMbLen ? respMbLen : testLen;

	if (strncmp(respMb, yesMb, compLen * yesChWid) == 0) {
		return (2);
	}

	if (strncmp(respMb, noMb, compLen * noChWid) == 0) {
		return (1);
	}

	if (strncasecmp(respMb, "yes", (compLen > 3 ? 3 : compLen)) == 0) {
		return (2);
	}

	if (strncasecmp(respMb, "no", (compLen > 2 ? 2 : compLen)) == 0) {
		return (2);
	}

	return (0);
}
