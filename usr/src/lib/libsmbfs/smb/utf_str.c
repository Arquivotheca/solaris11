/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Unicode conversions (yet more)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <iconv.h>
#include <libintl.h>

#include <sys/u8_textprep.h>

#include <netsmb/smb_lib.h>
#include "charsets.h"


/*
 * Number of unicode symbols in the string,
 * not including the 2-byte null terminator.
 * (multiply by two for storage size)
 */
size_t
unicode_strlen(const uint16_t *us)
{
	size_t len = 0;
	while (*us++)
		len++;
	return (len);
}

static char *convert_ucs2xx_to_utf8(iconv_t, const uint16_t *);

/*
 * Convert (native) Unicode string to UTF-8.
 * Returns allocated memory.
 */
char *
convert_unicode_to_utf8(uint16_t *us)
{
	static iconv_t cd1 = (iconv_t)-1;

	/* Get conversion descriptor (to, from) */
	if (cd1 == (iconv_t)-1)
		cd1 = iconv_open("UTF-8", "UCS-2");

	return (convert_ucs2xx_to_utf8(cd1, us));
}

/*
 * Convert little-endian Unicode string to UTF-8.
 * Returns allocated memory.
 */
char *
convert_leunicode_to_utf8(unsigned short *us)
{
	static iconv_t cd2 = (iconv_t)-1;

	/* Get conversion descriptor (to, from) */
	if (cd2 == (iconv_t)-1)
		cd2 = iconv_open("UTF-8", "UCS-2LE");

	return (convert_ucs2xx_to_utf8(cd2, us));
}

static char *
convert_ucs2xx_to_utf8(iconv_t cd, const uint16_t *us)
{
	char *obuf, *optr;
	char *iptr;
	size_t  ileft, obsize, oleft, ret;

	if (cd == (iconv_t)-1) {
		smb_error(dgettext(TEXT_DOMAIN,
		    "iconv_open(UTF-8/UCS-2)"), -1);
		return (NULL);
	}

	iptr = (char *)us;
	ileft = unicode_strlen(us);
	ileft *= 2; /* now bytes */

	/* Worst-case output size is 2x input size. */
	oleft = ileft * 2;
	obsize = oleft + 2; /* room for null */
	obuf = malloc(obsize);
	if (!obuf)
		return (NULL);
	optr = obuf;

	ret = iconv(cd, &iptr, &ileft, &optr, &oleft);
	*optr = '\0';
	if (ret == (size_t)-1) {
		smb_error(dgettext(TEXT_DOMAIN,
		    "iconv(%s) failed"), errno, obuf);
	}
	if (ileft) {
		smb_error(dgettext(TEXT_DOMAIN,
		    "iconv(%s) failed"), -1, obuf);
		/*
		 * XXX: What's better?  return NULL?
		 * The truncated string? << for now
		 */
	}

	return (obuf);
}

static uint16_t *convert_utf8_to_ucs2xx(iconv_t, const char *);

/*
 * Convert UTF-8 string to Unicode.
 * Returns allocated memory.
 */
uint16_t *
convert_utf8_to_unicode(const char *utf8_string)
{
	static iconv_t cd3 = (iconv_t)-1;

	/* Get conversion descriptor (to, from) */
	if (cd3 == (iconv_t)-1)
		cd3 = iconv_open("UCS-2", "UTF-8");
	return (convert_utf8_to_ucs2xx(cd3, utf8_string));
}

/*
 * Convert UTF-8 string to little-endian Unicode.
 * Returns allocated memory.
 */
uint16_t *
convert_utf8_to_leunicode(const char *utf8_string)
{
	static iconv_t cd4 = (iconv_t)-1;

	/* Get conversion descriptor (to, from) */
	if (cd4 == (iconv_t)-1)
		cd4 = iconv_open("UCS-2LE", "UTF-8");
	return (convert_utf8_to_ucs2xx(cd4, utf8_string));
}

static uint16_t *
convert_utf8_to_ucs2xx(iconv_t cd, const char *utf8_string)
{
	uint16_t *obuf, *optr;
	char *iptr;
	size_t  ileft, obsize, oleft, ret;

	if (cd == (iconv_t)-1) {
		smb_error(dgettext(TEXT_DOMAIN,
		    "iconv_open(UCS-2/UTF-8)"), -1);
		return (NULL);
	}

	iptr = (char *)utf8_string;
	ileft = strlen(iptr);

	/* Worst-case output size is 2x input size. */
	oleft = ileft * 2;
	obsize = oleft + 2; /* room for null */
	obuf = malloc(obsize);
	if (!obuf)
		return (NULL);
	optr = obuf;

	ret = iconv(cd, &iptr, &ileft, (char **)&optr, &oleft);
	*optr = '\0';
	if (ret == (size_t)-1) {
		smb_error(dgettext(TEXT_DOMAIN,
		    "iconv(%s) failed"), errno, utf8_string);
	}
	if (ileft) {
		smb_error(dgettext(TEXT_DOMAIN,
		    "iconv(%s) failed"), -1, utf8_string);
		/*
		 * XXX: What's better?  return NULL?
		 * The truncated string? << for now
		 */
	}

	return (obuf);
}


/*
 * A simple wrapper around u8_textprep_str() that returns the Unicode
 * upper-case version of some string.  Returns memory from malloc.
 * Borrowed from idmapd.
 */
static char *
utf8_str_to_upper_or_lower(const char *s, int upper_lower)
{
	char *res = NULL;
	char *outs;
	size_t inlen, outlen, inbleft, outbleft;
	int rc, err;

	/*
	 * u8_textprep_str() does not allocate memory.  The input and
	 * output buffers may differ in size (though that would be more
	 * likely when normalization is done).  We have to loop over it...
	 *
	 * To improve the chances that we can avoid looping we add 10
	 * bytes of output buffer room the first go around.
	 */
	inlen = inbleft = strlen(s);
	outlen = outbleft = inlen + 10;
	if ((res = malloc(outlen)) == NULL)
		return (NULL);
	outs = res;

	while ((rc = u8_textprep_str((char *)s, &inbleft, outs,
	    &outbleft, upper_lower, U8_UNICODE_LATEST, &err)) < 0 &&
	    err == E2BIG) {
		if ((res = realloc(res, outlen + inbleft)) == NULL)
			return (NULL);
		/* adjust input/output buffer pointers */
		s += (inlen - inbleft);
		outs = res + outlen - outbleft;
		/* adjust outbleft and outlen */
		outlen += inbleft;
		outbleft += inbleft;
	}

	if (rc < 0) {
		free(res);
		res = NULL;
		return (NULL);
	}

	res[outlen - outbleft] = '\0';

	return (res);
}

char *
utf8_str_toupper(const char *s)
{
	return (utf8_str_to_upper_or_lower(s, U8_TEXTPREP_TOUPPER));
}

char *
utf8_str_tolower(const char *s)
{
	return (utf8_str_to_upper_or_lower(s, U8_TEXTPREP_TOLOWER));
}
