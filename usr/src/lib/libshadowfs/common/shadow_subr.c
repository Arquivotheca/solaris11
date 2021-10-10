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

#include <sys/types.h>
#include <alloca.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "shadow_impl.h"

static __thread shadow_errno_t _shadow_errno;
static __thread char _shadow_errmsg[1024];

void
_shadow_dprintf(int fd, const char *file, const char *fmt, va_list ap)
{
	const char *slash;
	char buf[512], *bufp = buf;
	int n;

	n = vsnprintf(buf, sizeof (buf), fmt, ap);

	if (n >= sizeof (buf)) {
		bufp = alloca(n + 1);
		n = vsprintf(bufp, fmt, ap);
	}

	if ((slash = strrchr(file, '/')) != NULL)
		file = slash + 1;

	(void) write(fd, file, strlen(file));
	(void) write(fd, ": ", 2);
	(void) write(fd, bufp, n);
}


void *
shadow_alloc(size_t len)
{
	void *ret;

	if ((ret = malloc(len)) == NULL)
		(void) shadow_set_errno(ESHADOW_NOMEM);

	return (ret);
}

void *
shadow_zalloc(size_t len)
{
	void *ret;

	if ((ret = calloc(len, 1)) == NULL)
		(void) shadow_set_errno(ESHADOW_NOMEM);

	return (ret);
}

char *
shadow_strdup(const char *str)
{
	char *ret;

	if ((ret = strdup(str)) == NULL)
		(void) shadow_set_errno(ESHADOW_NOMEM);

	return (ret);
}

int
shadow_error(shadow_errno_t err, const char *fmt, ...)
{
	va_list ap;

	_shadow_errno = err;

	va_start(ap, fmt);
	(void) vsnprintf(dgettext(TEXT_DOMAIN, _shadow_errmsg),
	    sizeof (_shadow_errmsg), fmt, ap);
	va_end(ap);

	return (-1);
}

int
shadow_set_errno(shadow_errno_t err)
{
	_shadow_errno = err;
	_shadow_errmsg[0] = '\0';

	return (-1);
}

shadow_errno_t
shadow_errno(void)
{
	return (_shadow_errno);
}

const char *
shadow_errmsg(void)
{
	if (_shadow_errmsg[0] == '\0')
		return (shadow_strerror(_shadow_errno));
	else
		return (_shadow_errmsg);
}

/*
 * Translate from a libzfs error into the corresponding shadow error.
 */
int
shadow_zfs_error(libzfs_handle_t *zhdl)
{
	shadow_errno_t err;

	switch (libzfs_errno(zhdl)) {
	case EZFS_NOMEM:
		return (shadow_set_errno(ESHADOW_NOMEM));

	case EZFS_NOENT:
		err = ESHADOW_ZFS_NOENT;
		break;

	case EZFS_IO:
		err = ESHADOW_ZFS_IO;
		break;

	default:
		err = ESHADOW_ZFS_IMPL;
		break;
	}

	return (shadow_error(err, "%s: %s", libzfs_error_action(zhdl),
	    libzfs_error_description(zhdl)));
}
