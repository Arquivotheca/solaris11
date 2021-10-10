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
 * Copyright(c) 2007-2010 Intel Corporation. All rights reserved.
 */

/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include "igb_sw.h"

#define	LOG_BUF_LEN	128

/*
 * igb_notice - Report a run-time event (CE_NOTE, to console & log)
 */
void
igb_notice(void *arg, const char *fmt, ...)
{
	igb_t *igbp = (igb_t *)arg;
	char buf[LOG_BUF_LEN];
	va_list ap;

	va_start(ap, fmt);
	(void) vsnprintf(buf, sizeof (buf), fmt, ap);
	va_end(ap);

	if (igbp != NULL)
		cmn_err(CE_NOTE, "%s%d: %s", MODULE_NAME, igbp->instance, buf);
	else
		cmn_err(CE_NOTE, "%s: %s", MODULE_NAME, buf);
}

/*
 * igb_log - Log a run-time event (CE_NOTE, to log only)
 */
void
igb_log(void *arg, const char *fmt, ...)
{
	igb_t *igbp = (igb_t *)arg;
	char buf[LOG_BUF_LEN];
	va_list ap;

	va_start(ap, fmt);
	(void) vsnprintf(buf, sizeof (buf), fmt, ap);
	va_end(ap);

	if (igbp != NULL)
		cmn_err(CE_NOTE, "!%s%d: %s", MODULE_NAME, igbp->instance, buf);
	else
		cmn_err(CE_NOTE, "!%s: %s", MODULE_NAME, buf);
}

/*
 * igb_error - Log a run-time problem (CE_WARN, to log only)
 */
void
igb_error(void *arg, const char *fmt, ...)
{
	igb_t *igbp = (igb_t *)arg;
	char buf[LOG_BUF_LEN];
	va_list ap;

	va_start(ap, fmt);
	(void) vsnprintf(buf, sizeof (buf), fmt, ap);
	va_end(ap);

	if (igbp != NULL)
		cmn_err(CE_WARN, "!%s%d: %s", MODULE_NAME, igbp->instance, buf);
	else
		cmn_err(CE_WARN, "!%s: %s", MODULE_NAME, buf);
}
