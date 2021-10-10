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

#include <stdlib.h>
#include <alloca.h>

#include <stdio.h>
#include <strings.h>
#include <time.h>

#include "asr_err.h"
#include "asr_mem.h"

/*
 * Wrapper for memory allocation.
 */
void *
asr_alloc(size_t size)
{
	void *mem;

	if (size == 0) {
		(void) asr_set_errno(EASR_ZEROSIZE);
		return (NULL);
	}
	if (size > ASR_MEM_MAX_SIZE) {
		(void) asr_set_errno(EASR_OVERSIZE);
		return (NULL);
	}
	if ((mem = malloc(size)) == NULL)
		(void) asr_set_errno(EASR_NOMEM);

	return (mem);
}

/*
 * Wrapper for zero initialized memory allocation.
 */
void *
asr_zalloc(size_t size)
{
	void *mem = asr_alloc(size);

	if (mem != NULL)
		bzero(mem, size);

	return (mem);
}

/*
 * Wrapper for string duplication
 */
char *
asr_strdup(const char *str)
{
	size_t len;
	char *new;

	if (str == NULL) {
		(void) asr_set_errno(EASR_NULLDATA);
		return (NULL);
	}

	len = strlen(str) + 1;
	new = asr_alloc(len);

	if (new == NULL)
		return (NULL);

	bcopy(str, new, len);
	return (new);
}

/*
 * Erase string following Oracle security guidlines and then free it.
 */
void
asr_strfree_secure(char *str)
{
	size_t len, i;
	int d = 0xff;
	if (str == NULL)
		return;
	len = strlen(str);
	(void) memset(str, d, len);
	for (i = 0; i < len; i++)
		if (str[i] != d)
			break;
	free(str);
}
