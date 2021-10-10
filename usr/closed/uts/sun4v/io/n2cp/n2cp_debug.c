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
#include <sys/n2cp.h>

/*
 * Debugging and messaging.
 */
#if DEBUG
static int n2cp_debug = DWARN;

int
n2cp_dflagset(int flag)
{
	return (flag & n2cp_debug);
}

void
n2cp_dprintf(n2cp_t *n2cp, int level, const char *fmt, ...)
{
	va_list ap;
	char	buf[256];

	if (n2cp_debug & level) {
		va_start(ap, fmt);
		if (n2cp == NULL) {
			(void) sprintf(buf, "%s\n", fmt);
		} else {
			(void) sprintf(buf, "%s/%d: %s\n",
			    ddi_driver_name(n2cp->n_dip),
			    ddi_get_instance(n2cp->n_dip), fmt);
		}
		vprintf(buf, ap);
		va_end(ap);
	}
}
void
n2cp_dumphex(void *data, int len)
{
	uchar_t *buff;
	int	i, j, tlen;
	char    scratch[128];
	char    *out;
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
n2cp_error(n2cp_t *n2cp, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	n2cp_dipverror(n2cp->n_dip, fmt, ap);
	va_end(ap);
}

void
n2cp_diperror(dev_info_t *dip, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	n2cp_dipverror(dip, fmt, ap);
	va_end(ap);
}

void
n2cp_dipverror(dev_info_t *dip, const char *fmt, va_list ap)
{
	char	buf[256];
	/*
	 * n2cp0 and n2cp/0 are used else where in the system
	 * (kstat, cryptoadm, etc.) To be consistent,
	 * even for a single instance, n2cp0 still better than just ncp.
	 */
	(void) sprintf(buf, "%s%d: %s", ddi_driver_name(dip),
			ddi_get_instance(dip), fmt);
	vcmn_err(CE_WARN, buf, ap);
}
