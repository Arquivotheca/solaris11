/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cmn_err.h>
#include <sys/varargs.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <sys/ncp.h>

/*
 * Debugging and messaging.
 */
#if DEBUG
static int ncp_debug = DWARN;

int
ncp_dflagset(int flag)
{
	return (flag & ncp_debug);
}

void
ncp_dprintf(ncp_t *ncp, int level, const char *fmt, ...)
{
	va_list ap;
	char	buf[256];

	if (ncp_debug & level) {
		va_start(ap, fmt);
		if (ncp == NULL) {
			(void) sprintf(buf, "%s\n", fmt);
		} else {
			(void) sprintf(buf, "%s/%d: %s\n",
			    ddi_driver_name(ncp->n_dip),
			    ddi_get_instance(ncp->n_dip), fmt);
		}
		vprintf(buf, ap);
		va_end(ap);
	}
}

void
ncp_dumphex(void *data, int len)
{
	uchar_t	*buff;
	int	i, j, tlen;
	char	scratch[128];
	char	*out;
	if (data == NULL) {
		(void) cmn_err(CE_WARN, "data is NULL");
		return;
	}

	buff = (uchar_t *)data;
	for (i = 0; i < len; i += 16) {
		out = scratch;
		tlen = i + 16;
		tlen = len < tlen ? len : tlen;
		(void) sprintf(out, "%p: ", (void *)(buff + i));
		while (*out) {
			out++;
		}
		out += strlen(out);
		for (j = i; j < tlen; j++) {
			(void) sprintf(out, "%02X ", buff[j]);
			out += 3;
		}
		for (j = len; j < i + 16; j++) {
			(void) strcpy(out, "   ");
			out += 3;
		}
		(void) sprintf(out, "    ");
		out += 4;
		for (j = i; j < tlen; j++) {
			/* poor man's isprint() */
			if ((buff[j] > 32) && (buff[j] < 127)) {
				*(out++) = buff[j];
			} else {
				*(out++) = '.';
			}
			*(out) = 0;
		}
		cmn_err(CE_NOTE, "%s\n", scratch);
	}
}

#endif

void
ncp_error(ncp_t *ncp, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ncp_dipverror(ncp->n_dip, fmt, ap);
	va_end(ap);
}

void
ncp_diperror(dev_info_t *dip, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ncp_dipverror(dip, fmt, ap);
	va_end(ap);
}

void
ncp_dipverror(dev_info_t *dip, const char *fmt, va_list ap)
{
	char	buf[256];
	/*
	 * ncp0 and ncp/0 are used else where in the system
	 * (kstat, cryptoadm, etc.) To be consistent,
	 * even for a single instance, ncp0 still better than just ncp.
	 */
	(void) sprintf(buf, "%s%d: %s", ddi_driver_name(dip),
	    ddi_get_instance(dip), fmt);
	vcmn_err(CE_WARN, buf, ap);
}
