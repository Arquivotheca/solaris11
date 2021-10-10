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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */


/*
 * Translate a string into C literal string constant notation.
 */

#include	<stdio.h>
#include	<ctype.h>
#include	<_conv.h>
#include	<c_literal_msg.h>


/*
 * Convert characters to the form used by the C language to represent
 * literal strings:
 *	- Printable characters are shown as themselves
 *	- Convert special characters to their 2-character escaped forms:
 *		alert (bell)	\a
 *		backspace	\b
 *		formfeed	\f
 *		newline		\n
 *		return		\r
 *		horizontal tab	\t
 *		vertical tab	\v
 *		backspace	\\
 *		single quote	\'
 *		double quote	\"
 *	- Display other non-printable characters as 4-character escaped
 *		octal constants.
 *
 * entry:
 *	buf - Buffer of characters to be processed
 *	n # of characters in buf to be processed
 *	outfunc - Function to be called to move output characters.
 *	uvalue - User value. This argument is passed to outfunc without
 *		examination. The caller can use it to pass additional
 *		information required by the callback.
 *
 * exit:
 *	The string has been processed, with the resulting data passed
 *	to outfunc for processing.
 */
void
conv_str_to_c_literal(const char *buf, size_t n,
    Conv_str_to_c_literal_func_t *outfunc, void *uvalue)
{
	char	bs_buf[2];	/* For two-character backslash codes */
	char	octal_buf[10];	/* For \000 style octal constants */

	bs_buf[0] = '\\';
	while (n > 0) {
		switch (*buf) {
		case '\0':
			bs_buf[1] = '0';
			break;
		case '\a':
			bs_buf[1] = 'a';
			break;
		case '\b':
			bs_buf[1] = 'b';
			break;
		case '\f':
			bs_buf[1] = 'f';
			break;
		case '\n':
			bs_buf[1] = 'n';
			break;
		case '\r':
			bs_buf[1] = 'r';
			break;
		case '\t':
			bs_buf[1] = 't';
			break;
		case '\v':
			bs_buf[1] = 'v';
			break;
		case '\\':
			bs_buf[1] = '\\';
			break;
		case '\'':
			bs_buf[1] = '\'';
			break;
		case '"':
			bs_buf[1] = '"';
			break;
		default:
			bs_buf[1] = '\0';
		}

		if (bs_buf[1] != '\0') {
			(*outfunc)(bs_buf, 2, uvalue);
			buf++;
			n--;
		} else if (isprint(*buf)) {
			/*
			 * Output the entire sequence of printable
			 * characters in a single shot.
			 */
			const char	*start = buf;
			size_t		outlen = 0;

			for (start = buf; (n > 0) && isprint(*buf); buf++, n--)
				outlen++;
			(*outfunc)(start, outlen, uvalue);
		} else {
			/* Generic unprintable character: Use octal notation */
			(void) snprintf(octal_buf, sizeof (octal_buf),
			    MSG_ORIG(MSG_FMT_OCTCONST), (uchar_t)*buf);
			(*outfunc)(octal_buf, strlen(octal_buf), uvalue);
			buf++;
			n--;
		}
	}
}

/*
 * Given the pointer to the character following a '\' character in
 * a C style literal, return the ASCII character code it represents,
 * and advance the string pointer to the character following the last
 * character in the escape sequence.
 *
 * entry:
 *	str - Address of string pointer to first character following
 *		the backslash.
 *
 * exit:
 *	If the character is not valid, -1 is returned. Otherwise
 *	it returns the ASCII code for the translated character, and
 *	*str has been advanced.
 */
int
conv_translate_c_esc(char **str)
{
	char	*s = *str;
	int	ch, i;

	ch = *s++;
	switch (ch) {
	case 'a':
		ch = '\a';
		break;
	case 'b':
		ch = '\b';
		break;
	case 'f':
		ch = '\f';
		break;
	case 'n':
		ch = '\n';
		break;
	case 'r':
		ch = '\r';
		break;
	case 't':
		ch = '\t';
		break;
	case 'v':
		ch = '\v';
		break;

	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
		/* Octal constant: There can be up to 3 digits */
		ch -= '0';
		for (i = 0; i < 2; i++) {
			if ((*s < '0') || (*s > '7'))
				break;
			ch = (ch << 3) + (*s++ - '0');
		}
		break;

	/*
	 * There are some cases where ch already has the desired value.
	 * These cases exist simply to remove the special meaning that
	 * character would otherwise have. We need to match them to
	 * prevent them from falling into the default error case.
	 */
	case '\\':
	case '\'':
	case '"':
		break;

	default:
		ch = -1;
		break;
	}

	*str = s;
	return (ch);
}
