/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <umem.h>
#include <sys/types.h>

#include <mtst.h>

int
mtst_set_errno(int err)
{
	errno = err;
	return (-1);
}

int
mtst_strtonum(const char *str, uint64_t *valp)
{
	char *endptr;
	uint64_t val;

	errno = 0;
	val = strtoull(str, &endptr, 0);
	if (errno != NULL || *endptr != '\0')
		return (mtst_set_errno(EINVAL));

	*valp = val;
	return (0);
}

/* return the number of tokens */
int
mtst_strntok(const char *str, const char *delims)
{
	int ntok = 1;
	const char *c;

	if (*str == '\0')
		return (0);

	for (c = str; *c != '\0'; c++) {
		if (strchr(delims, *c) != NULL)
			ntok++;
	}

	return (ntok);
}

char *
mtst_strdup(const char *str)
{
	size_t len = strlen(str) + 1;
	char *new = umem_alloc(len, UMEM_NOFAIL);

	bcopy(str, new, len);
	return (new);
}

void
mtst_strfree(char *str)
{
	umem_free(str, strlen(str) + 1);
}
