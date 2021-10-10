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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "drmP.h"

void
drm_debug_print(int cmn_err, const char *func, int line, const char *fmt, ...)
{
	va_list ap;
	char format[80];

	(void) snprintf(format, sizeof (format), "[drm:%s:%d] %s",
	    func, line, fmt);

	va_start(ap, fmt);
	vcmn_err(cmn_err, format, ap);
	va_end(ap);
}

void
drm_debug(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vcmn_err(CE_NOTE, fmt, ap);
	va_end(ap);
}

void
drm_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vcmn_err(CE_WARN, fmt, ap);
	va_end(ap);
}

void
drm_info(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vcmn_err(CE_NOTE, fmt, ap);
	va_end(ap);
}
