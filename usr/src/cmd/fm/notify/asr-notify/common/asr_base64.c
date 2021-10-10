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

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "asr_base64.h"

static unsigned char b64_encode[64] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*
 * Creates a base 64 encoded string of the given data buffer.
 */
char *
asr_b64_encode(const char *mem, const size_t len)
{
	size_t enclen;
	char *out;
	int thresh, i, j, off;
	uchar_t in[3], nib[4];

	if (len % 3 == 0)
		enclen = len / 3;
	else
		enclen = (len / 3) + 1;
	enclen *= 4;

	if ((out = malloc(enclen + 1)) == NULL)
		return (NULL);

	for (i = 0, off = 0; i < len; i += 3) {
		/*
		 * For every group of three, we're going to output four
		 * base64-encoded bytes using the lookup table.
		 */
		for (j = 0; j < 3; j++)
			in[j] = i + j < len ? ((uchar_t *)mem)[i + j] : 0;

		nib[0] = in[0] >> 2;
		nib[1] = ((in[0] & 3) << 4) | ((in[1] >> 4) & 0xf);
		nib[2] = ((in[1] & 0xf) << 2) | ((in[2] >> 6) & 3);
		nib[3] = in[2] & 0x3f;

		thresh = (len - i) + 1;

		for (j = 0; j < 4; j++) {
			out[off++] = j < thresh ?
			    b64_encode[nib[j]] : '=';
		}
	}
	out[enclen] = '\0';

	return (out);
}
