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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <alloca.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <strings.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/varargs.h>
#include <unistd.h>

#include "asr_err.h"
#include "asr_mem.h"
#include "asr_buf.h"

/*
 * Generic dynamic buffer support.  Allows consumers to create a dynamic buffer
 * and continually append formatted data, expanding the buffer as necessary.
 * Returns NULL is there is an error.
 */
asr_buf_t *
asr_buf_alloc(size_t len)
{
	asr_buf_t *abp;

	if ((abp = asr_zalloc(sizeof (asr_buf_t))) == NULL)
		return (NULL);

	abp->asrb_allocated = len;
	abp->asrb_data = asr_alloc(len);

	if (abp->asrb_data == NULL && len != 0) {
		free(abp);
		return (NULL);
	}

	if (len != 0)
		abp->asrb_data[0] = '\0';

	abp->asrb_error = EASR_NONE;

	return (abp);
}

/*
 * Internal funtion to grow the buffer.  This is called when more room is
 * needed in the buffer.  A new buffer that is at least twice as big is created
 * and the contents of the old buffer is copied to the new one.
 */
static int
asr_buf_grow(asr_buf_t *abp, size_t len)
{
	char *newdata;
	size_t newlen;

	newlen = abp->asrb_allocated * 2;
	if (abp->asrb_length + len + 1 > newlen)
		newlen = abp->asrb_length + len + 1;

	if ((newdata = asr_alloc(newlen)) == NULL) {
		if (abp->asrb_error == EASR_NONE)
			abp->asrb_error = EASR_NOMEM;
		return (-1);
	}

	bcopy(abp->asrb_data, newdata, abp->asrb_length);
	free(abp->asrb_data);

	abp->asrb_data = newdata;
	abp->asrb_allocated = newlen;

	return (0);
}

/*
 * Appends a formatted string to the buffer
 */
/*PRINTFLIKE2*/
int
asr_buf_append(asr_buf_t *abp, const char *fmt, ...)
{
	va_list ap;
	size_t len;

	va_start(ap, fmt);
	len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	if (abp->asrb_length + len >= abp->asrb_allocated &&
	    asr_buf_grow(abp, len) != 0)
		return (-1);

	va_start(ap, fmt);
	(void) vsnprintf(abp->asrb_data + abp->asrb_length,
	    abp->asrb_allocated - abp->asrb_length, fmt, ap);
	va_end(ap);

	abp->asrb_length += len;
	return (ASR_OK);
}

/*
 * Appends a formatted string using a variable argument list.
 */
int
asr_buf_vappend(asr_buf_t *abp, const char *fmt, va_list ap)
{
	size_t len;
	len = vsnprintf(NULL, 0, fmt, ap);

	if (abp->asrb_length + len >= abp->asrb_allocated &&
	    asr_buf_grow(abp, len) != 0)
		return (ASR_FAILURE);

	(void) vsnprintf(abp->asrb_data + abp->asrb_length,
	    abp->asrb_allocated - abp->asrb_length, fmt, ap);

	abp->asrb_length += len;
	return (ASR_OK);
}

/*
 * Append a string to the buffer.  The null termination character will also
 * be copied.
 */
int
asr_buf_append_str(asr_buf_t *abp, const char *str)
{
	int len = (str == NULL) ? 0 : strlen(str);
	int glen = len + 1;

	if (abp->asrb_length + glen >= abp->asrb_allocated &&
	    asr_buf_grow(abp, glen) != 0)
		return (ASR_FAILURE);

	(void) strcpy(abp->asrb_data + abp->asrb_length, str);
	abp->asrb_length += len;
	return (ASR_OK);
}

/*
 * Appends the contents of the second buffer to the first buffer.
 * A null termination character will also be added.
 */
int
asr_buf_append_buf(asr_buf_t *abp, const asr_buf_t *buf)
{
	int len = (buf == NULL) ? 0 : buf->asrb_length;
	int glen = len + 1;

	if (abp->asrb_length + glen >= abp->asrb_allocated &&
	    asr_buf_grow(abp, glen) != 0)
		return (ASR_FAILURE);
	bcopy(buf->asrb_data, abp->asrb_data + abp->asrb_length, len);
	abp->asrb_length += len;
	abp->asrb_data[abp->asrb_length] = '\0';
	return (ASR_OK);
}

/*
 * Appends a data buffer.
 */
int
asr_buf_append_raw(asr_buf_t *abp, const void *ptr, size_t sz)
{
	int len = sz + 1;
	if (abp->asrb_length + len >= abp->asrb_allocated &&
	    asr_buf_grow(abp, len) != 0)
		return (ASR_FAILURE);

	bcopy(ptr, abp->asrb_data + abp->asrb_length, sz);
	abp->asrb_length += sz;
	abp->asrb_data[abp->asrb_length] = '\0';
	return (ASR_OK);
}

/*
 * Appends a single character.  Only the single character is added.  No null
 * termination is added.
 */
int
asr_buf_append_char(asr_buf_t *abp, const char ch)
{
	if (abp->asrb_length + 1 >= abp->asrb_allocated &&
	    asr_buf_grow(abp, ASRB_GROW_LEN) != 0)
		return (ASR_FAILURE);

	abp->asrb_data[abp->asrb_length] = ch;
	abp->asrb_length++;
	return (ASR_OK);
}

/*
 * Adds a null terminator to the string.  This will ensure the buffer is
 * big enough to add an extra '\0' without changing the length.
 * This call is useful after a call to asr_buf_append_char to ensure the
 * data string is properly terminated.
 */
int
asr_buf_terminate(asr_buf_t *abp)
{
	if (abp->asrb_length + 1 >= abp->asrb_allocated &&
	    asr_buf_grow(abp, ASRB_GROW_LEN) != 0)
		return (ASR_FAILURE);

	abp->asrb_data[abp->asrb_length] = '\0';
	return (ASR_OK);
}

/*
 * Removes all leading and trailing white space from the buffer.
 */
void
asr_buf_trim(asr_buf_t *abp)
{
	int r, i, j;
	char *data = abp->asrb_data;

	for (r = 0; r < abp->asrb_length; r++) {
		char c = data[r];
		if (c != ' ' && c != '\t' && c != '\n')
			break;
	}
	if (r > 0) {
		for (i = r, j = 0; i < abp->asrb_length; i++, j++) {
			data[j] = data[i];
		}
		abp->asrb_length -= r;
		data[abp->asrb_length] = '\0';
	}

	for (i = abp->asrb_length - 1; i >= 0; i--) {
		char c = data[i];
		if (c != ' ' && c != '\t' && c != '\n')
			break;
	}
	r = abp->asrb_length - i - 1;
	if (r > 0) {
		abp->asrb_length -= r;
		data[abp->asrb_length] = '\0';
	}
}

/*
 * Releases the buffer along with all resources used by the buffer.
 * If the buffer pointer is NULL then this is a no op.
 */
void
asr_buf_free(asr_buf_t *abp)
{
	if (abp != NULL) {
		free(abp->asrb_data);
		abp->asrb_data = NULL;
		free(abp);
	}
}

/*
 * Releaes just the asr_buf structure ( A shallow free) and returns
 * the char * data in the buffer;
 */
char *
asr_buf_free_struct(asr_buf_t *abp)
{
	char *data = NULL;
	if (abp != NULL) {
		data = abp->asrb_data;
		free(abp);
	}
	return (data);
}

/*
 * Reset the buffer size back to zero and make the buffer contents an
 * empty string.
 */
void
asr_buf_reset(asr_buf_t *abp)
{
	if (abp != NULL) {
		if (abp->asrb_length > 0) {
			abp->asrb_length = 0;
			abp->asrb_data[0] = '\0';
		}
	}
}

/*
 * Gets a pointer to the buffer data.
 */
char *
asr_buf_data(asr_buf_t *abp)
{
	return (abp == NULL ? NULL : abp->asrb_data);
}

/*
 * Gets the size of the buffer data.
 */
size_t
asr_buf_size(asr_buf_t *abp)
{
	return (abp == NULL ? 0 : abp->asrb_length);
}

/*
 * Prints XML CDATA value encoding special XML characters
 */
int
asr_buf_append_xml_nval(asr_buf_t *abp, const char *value, int n)
{
	int err = 0;
	const char *c = (value == NULL) ? "null" : value;
	char ch;
	int limit = 0;
	if (n >= 0)
		limit = abp->asrb_length + n;

	while (*c != '\0' && err == 0) {
		ch = *c;
		switch (ch) {
		case '<':
			if (limit > 0 && (abp->asrb_length + 4) > limit)
				return (err);
			err |= asr_buf_append(abp, "&lt;");
			break;
		case '>':
			if (limit > 0 && (abp->asrb_length + 4) > limit)
				return (err);
			err |= asr_buf_append(abp, "&gt;");
			break;
		case '&':
			if (limit > 0 && (abp->asrb_length + 5) > limit)
				return (err);
			err |= asr_buf_append(abp, "&amp;");
			break;
		case '"':
			if (limit > 0 && (abp->asrb_length + 6) > limit)
				return (err);
			err |= asr_buf_append(abp, "&quot;");
			break;
		case '\'':
			if (limit > 0 && (abp->asrb_length + 6) > limit)
				return (err);
			err |= asr_buf_append(abp, "&apos;");
			break;
		default:
			/*
			 * Filter out unprintable characters.
			 */
			if ((ch < 32) || (126 < ch))
				ch = '.';
			if (limit > 0 && (abp->asrb_length + 1) > limit)
				return (err);
			err |= asr_buf_append_char(abp, ch);
		}
		c++;
	}
	return (err);
}
int
asr_buf_append_xml_val(asr_buf_t *abp, const char *value)
{
	return (asr_buf_append_xml_nval(abp, value, -1));
}
/*
 * Prints an XML value that is a token.
 * A token contains a-z | A-Z | 0-9 | '.' | '-' | '_' | ':'
 * Any non-conforming char will be replaced with a '.' to make the
 * value a legal token.
 */
int
asr_buf_append_xml_token(asr_buf_t *abp, const char *value)
{
	int err = 0;
	const char *c = (value == NULL) ? "null" : value;
	char ch;

	while (*c != '\0' && err == 0) {
		boolean_t istok = B_FALSE;
		ch = *c;

		if (ch >= 'a' && ch <= 'z')
			istok = B_TRUE;
		else if (ch >= 'A' && ch <= 'Z')
			istok = B_TRUE;
		else if (ch >= '0' && ch <= ':')
			istok = B_TRUE;
		else if (ch == '.' || ch == '-' || ch == '_')
			istok = B_TRUE;

		if (istok == B_FALSE)
			ch = '_';
		err |= asr_buf_append_char(abp, ch);
		c++;
	}
	err |= asr_buf_terminate(abp);
	return (err);
}

/*
 * Prints the beginning XML element <name> indented with the given pad value.
 */
int
asr_buf_append_xml_elem(asr_buf_t *abp, unsigned int pad, const char *name)
{
	int err = 0;
	err |= asr_buf_append_pad(abp, pad);
	err |= asr_buf_append(abp, "<%s>\n", name);
	return (err);
}

/*
 * Prints an ending XML element </name>
 */
int
asr_buf_append_xml_end(asr_buf_t *abp, unsigned int pad, const char *name)
{
	int err = 0;
	err |= asr_buf_append_pad(abp, pad);
	err |= asr_buf_append(abp, "</%s>\n", name);
	return (err);
}

/*
 * Encode an xml element with the given value.
 * <name>value</name>
 */
int
asr_buf_append_xml_nv(asr_buf_t *abp, unsigned int pad,
    const char *name, const char *value)
{
	int err = 0;
	if (value != NULL) {
		err |= asr_buf_append_pad(abp, pad);
		if (*value == '\0') {
			err |= asr_buf_append(abp, "<%s/>\n", name);
		} else {
			err |= asr_buf_append(abp, "<%s>", name);
			err |= asr_buf_append_xml_val(abp, value);
			err |= asr_buf_append(abp, "</%s>\n", name);
		}
	}
	return (err);
}
/*
 * Encode an xml element with the given value.  The value length will be
 * limited by n chars
 * <name>value</name>
 */
int
asr_buf_append_xml_nnv(asr_buf_t *abp, unsigned int pad,
    const char *name, const char *value, int n)
{
	int err = 0;
	if (value != NULL) {
		err |= asr_buf_append_pad(abp, pad);
		if (*value == '\0') {
			err |= asr_buf_append(abp, "<%s/>\n", name);
		} else {
			err |= asr_buf_append(abp, "<%s>", name);
			err |= asr_buf_append_xml_nval(abp, value, n);
			err |= asr_buf_append(abp, "</%s>\n", name);
		}
	}
	return (err);
}

/*
 * Encode an xml element with the given boolean value.
 * <name>value</name>
 */
int
asr_buf_append_xml_nb(asr_buf_t *abp, unsigned int pad,
    const char *name, const boolean_t value)
{
	return (asr_buf_append_xml_nv(
	    abp, pad, name, value ? "true" : "false"));
}


/*
 * Encode an xml element with the given value.
 * <name>value</name>
 */
int
asr_buf_append_xml_nvtoken(asr_buf_t *abp, unsigned int pad,
    const char *name, const char *value)
{
	int err = 0;
	if (value != NULL) {
		err |= asr_buf_append_pad(abp, pad);
		if (*value == '\0') {
			err |= asr_buf_append(abp, "<%s/>\n", name);
		} else {
			err |= asr_buf_append(abp, "<%s>", name);
			err |= asr_buf_append_xml_token(abp, value);
			err |= asr_buf_append(abp, "</%s>\n", name);
		}
	}
	return (err);
}

/*
 * Encode an xml element with one attribute.
 * <name aname="avalue">value</name>
 */
int
asr_buf_append_xml_anv(asr_buf_t *abp, unsigned int pad,
    const char *aname, const char *avalue,
    const char *name, const char *value)
{
	int err = 0;
	if (avalue == NULL || *avalue == '\0') {
		return (asr_buf_append_xml_nv(abp, pad, name, value));
	}

	err |= asr_buf_append_pad(abp, pad);
	err |= asr_buf_append(abp, "<%s %s='", name, aname);
	err |= asr_buf_append_xml_val(abp, avalue);
	err |= asr_buf_append(abp, "'>");
	err |= asr_buf_append_xml_val(abp, value);
	err |= asr_buf_append(abp, "</%s>\n", name);
	return (err);
}

/*
 * Prints out XML <additional-information> element with data.
 */
int
asr_buf_append_xml_ai(asr_buf_t *bp, int pad, const char *name, const char *val)
{
	return (asr_buf_append_xml_anv(bp, pad, "name", name,
	    "additional-information", val));
}

/*
 * Prints out the given number of tabs.  Useful for padding data to be
 * formmatted nicely.
 */
int
asr_buf_append_pad(asr_buf_t *abp, unsigned int pad)
{
	int err, i;
	for (i = 0, err = 0; i < pad && err == 0; i++) {
		err = asr_buf_append_char(abp, '\t');
	}
	return (err);
}

/*
 * Reads a line of data from the file into the given buffer.  Either a complete
 * line will be read and the length of that line returned or no data will be
 * added to the buffer and zero will be returned.
 */
int
asr_buf_readln(asr_buf_t *abp, FILE *file)
{
	int c;
	int rollback = abp->asrb_length;
	int err = 0;
	int added = 0;

	while ((c = fgetc(file)) != EOF && err == 0) {
		err = asr_buf_append_char(abp, c);
		added++;
		if (c == '\n') {
			(void) asr_buf_terminate(abp);
			break;
		}
	}
	if (err != 0 && asr_buf_terminate(abp) != 0) {
		abp->asrb_length = rollback;
		added = 0;
		(void) asr_buf_terminate(abp);
	}
	return (added);
}
