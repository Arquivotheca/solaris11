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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
#include <stdarg.h>

void
sunFm_vpanic(const char *format, va_list ap)
{
	(void) snmp_vlog(LOG_ERR, format, ap);
#ifdef DEBUG
	abort();
	exit(1);
#endif
}

void
sunFm_panic(const char *format, ...)
{
	va_list	ap;

	va_start(ap, format);
	sunFm_vpanic(format, ap);
	va_end(ap);
}

int
sunFm_assert(const char *expr, const char *file, int line)
{
	sunFm_panic("\"%s\", line %d: assertion failed: %s\n", file, line,
	    expr);
	return (0);
}
