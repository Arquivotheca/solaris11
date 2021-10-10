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
#include <stdarg.h>
#include <stdio.h>
#include <strings.h>
#include <sys/varargs.h>

#include "asr_err.h"

static asr_err_t asr_errno;
static char _asr_errmsg[ASR_ERRMSGLEN];

/*
 * Gets the default error string for the given errno.
 */
const char *asr_strerror(asr_err_t err) {
	switch (err) {
	case EASR_NONE:
		return ("no error");
	case EASR_NOMEM:
		return ("no memory");
	case EASR_USAGE:
		return ("option processing error");
	case EASR_UNKNOWN:
		return ("an internal error occured");
	case EASR_UNSUPPORTED:
		return ("unsupported operation");
	case EASR_ZEROSIZE:
		return ("caller attempted zero-length allocation");
	case EASR_OVERSIZE:
		return ("program attempted to allocate to much data");
	case EASR_NULLDATA:
		return ("program attempted to operate on NULL data");
	case EASR_NULLFREE:
		return ("caller attempted to free NULL handle");
	case EASR_MD_NODATA:
		return ("failed to open metadata");
	case EASR_PROP_USAGE:
		return ("property keyword syntax error");
	case EASR_PROP_NOPROP:
		return ("no such property name");
	case EASR_PROP_SET:
		return ("unable to set property");
	case EASR_SC:
		return ("error during phone home");
	case EASR_SC_RESOLV_PROXY:
		return ("error resolving proxy");
	case EASR_SC_RESOLV_HOST:
		return ("error resolving host");
	case EASR_SC_CONN:
		return ("error connecting to support service");
	case EASR_SC_AUTH:
		return ("invalid registration username/password");
	case EASR_SC_REG:
		return ("service not registered");
	case EASR_FM:
		return ("FM module error");
	case EASR_TOPO:
		return ("failed to determine system topology");
	case EASR_SCF:
		return ("SCF library error");
	case EASR_SSL_LIBSSL:
		return ("SSL library error");
	default:
		return ("ERROR");
	}
}

asr_err_t
asr_get_errno()
{
	return (asr_errno);
}

/*
 * Sets the ASR errno
 */
int
asr_set_errno(asr_err_t err)
{
	asr_errno = err;
	_asr_errmsg[0] = '\0';
	return (err);
}

/*
 * Sets the ASR errno and sets a custom error message.
 */
int
asr_verror(asr_err_t err, const char *format, va_list ap)
{
	int syserr = errno;
	size_t n;
	char *errmsg;

	errmsg = alloca(sizeof (_asr_errmsg));
	(void) vsnprintf(errmsg, sizeof (_asr_errmsg), format, ap);
	(void) asr_set_errno(err);

	n = strlen(errmsg);

	if (n != 0 && errmsg[n - 1] == '\n')
		errmsg[n - 1] = '\0';

	bcopy(errmsg, _asr_errmsg, sizeof (_asr_errmsg));
	errno = syserr;
	return (err);
}

/*
 * Sets the ASR errno and sets a custom error message.
 */
/*PRINTFLIKE2*/
int
asr_error(asr_err_t err, const char *format, ...)
{
	va_list ap;

	if (format == NULL)
		return (asr_set_errno(err));

	va_start(ap, format);
	err = asr_verror(err, format, ap);
	va_end(ap);

	return (err);
}

/*
 * Gets the current error message.
 */
const char *
asr_errmsg()
{
	if (_asr_errmsg[0] == '\0') {
		(void) strlcpy(_asr_errmsg,
		    asr_strerror(asr_errno), sizeof (_asr_errmsg));
	}

	return (_asr_errmsg);
}
