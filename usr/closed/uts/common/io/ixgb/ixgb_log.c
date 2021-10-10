/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "ixgb.h"

/*
 * Global variable for default debug flags
 */
uint32_t ixgb_debug;

/*
 * Global mutex used by logging routines below
 */
kmutex_t ixgb_log_mutex[1];

/*
 * Static data used by logging routines; protected by <ixgb_log_mutex>
 */
static struct {
	const char *who;
	const char *fmt;
	int level;
} ixgb_log_data;

/*
 * Backend print routine for all the routines below
 */
static void
ixgb_vprt(const char *fmt, va_list args)
{
	char buf[128];

	ASSERT(mutex_owned(ixgb_log_mutex));

	(void) vsnprintf(buf, sizeof (buf), fmt, args);
	cmn_err(ixgb_log_data.level, ixgb_log_data.fmt, ixgb_log_data.who, buf);
}

/*
 * Report a run-time event (CE_NOTE, to console & log)
 */
void
ixgb_notice(ixgb_t *ixgbp, const char *fmt, ...)
{
	va_list args;

	mutex_enter(ixgb_log_mutex);

	ixgb_log_data.who = ixgbp->ifname;
	ixgb_log_data.fmt = "%s: %s";
	ixgb_log_data.level = CE_NOTE;

	va_start(args, fmt);
	ixgb_vprt(fmt, args);
	va_end(args);

	mutex_exit(ixgb_log_mutex);
}

/*
 * Log a run-time event (CE_NOTE, log only)
 */
void
ixgb_log(ixgb_t *ixgbp, const char *fmt, ...)
{
	va_list args;

	mutex_enter(ixgb_log_mutex);

	ixgb_log_data.who = ixgbp->ifname;
	ixgb_log_data.fmt = "!%s: %s";
	ixgb_log_data.level = CE_NOTE;

	va_start(args, fmt);
	ixgb_vprt(fmt, args);
	va_end(args);

	mutex_exit(ixgb_log_mutex);
}

/*
 * Log a run-time problem (CE_WARN, log only)
 */
void
ixgb_problem(ixgb_t *ixgbp, const char *fmt, ...)
{
	va_list args;

	mutex_enter(ixgb_log_mutex);

	ixgb_log_data.who = ixgbp->ifname;
	ixgb_log_data.fmt = "!%s: %s";
	ixgb_log_data.level = CE_WARN;

	va_start(args, fmt);
	ixgb_vprt(fmt, args);
	va_end(args);

	mutex_exit(ixgb_log_mutex);
}

/*
 * Log a programming error (CE_WARN, log only)
 */
void
ixgb_error(ixgb_t *ixgbp, const char *fmt, ...)
{
	va_list args;

	mutex_enter(ixgb_log_mutex);

	ixgb_log_data.who = ixgbp->ifname;
	ixgb_log_data.fmt = "!%s: %s";
	ixgb_log_data.level = CE_WARN;

	va_start(args, fmt);
	ixgb_vprt(fmt, args);
	va_end(args);

	mutex_exit(ixgb_log_mutex);
}


static void
ixgb_prt(const char *fmt, ...)
{
	va_list args;

	ASSERT(mutex_owned(ixgb_log_mutex));

	va_start(args, fmt);
	ixgb_vprt(fmt, args);
	va_end(args);

	mutex_exit(ixgb_log_mutex);
}

void
(*ixgb_gdb(void))(const char *fmt, ...)
{
	mutex_enter(ixgb_log_mutex);

	ixgb_log_data.who = "ixgb";
	ixgb_log_data.fmt = "?%s: %s\n";
	ixgb_log_data.level = CE_CONT;

	return (ixgb_prt);
}

void
(*ixgb_db(ixgb_t *ixgbp))(const char *fmt, ...)
{
	mutex_enter(ixgb_log_mutex);

	ixgb_log_data.who = ixgbp->ifname;
	ixgb_log_data.fmt = "?%s: %s\n";
	ixgb_log_data.level = CE_CONT;

	return (ixgb_prt);
}

/*
 * Dump a chunk of memory, 16 bytes at a time
 */
void
minidump(ixgb_t *ixgbp, const char *caption, void *dp, uint_t len)
{
	uint32_t buf[4];
	uint32_t nbytes;

	ixgb_log(ixgbp, "%d bytes of %s at address %p:-", len, caption, dp);
	for (; len != 0; len -= nbytes) {
		nbytes = MIN(len, sizeof (buf));
		bzero(buf, sizeof (buf));
		bcopy(dp, buf, nbytes);
		ixgb_log(ixgbp, "%08x %08x %08x %08x",
		    buf[0], buf[1], buf[2], buf[3]);
		dp = (caddr_t)dp + nbytes;
	}
}

void
ixgb_pkt_dump(ixgb_t *ixgbp, void *hbdp, uint_t bd_type, const char *msg)
{
	ixgb_sbd_t *tbdp;
	ixgb_rbd_t *rbdp;

	switch (bd_type) {

	default:
		rbdp = NULL;
		tbdp = NULL;
		break;

	case RECV_BD:
		rbdp = (ixgb_rbd_t *)hbdp;
		ixgb_log(ixgbp, " %s : PCI address %llx buffer_len 0x%x "
		    "status 0x%x, errors 0x%x, special 0x%x",
		    msg,
		    rbdp->host_buf_addr,
		    rbdp->length,
		    rbdp->status,
		    rbdp->errors,
		    rbdp->special);
		break;

	case SEND_BD:
		tbdp = (ixgb_sbd_t *)hbdp;
		ixgb_log(ixgbp, "%s : PCI address %llx buffer len 0x%x,"
		    " cmd 0x%x"
		    "status 0x%x, popts 0x%x, vlan_id 0x%x",
		    msg,
		    tbdp->host_buf_addr,
		    tbdp->len_cmd&IXGB_TBD_LEN_MASK,
		    tbdp->len_cmd&(~IXGB_TBD_LEN_MASK),
		    tbdp->status,
		    tbdp->popts,
		    tbdp->vlan_id);
		break;
	}
}

void
ixgb_dbg_enter(ixgb_t *ixgbp, const char *s)
{
	uint32_t debug;

	debug = ixgbp != NULL ? ixgbp->debug : ixgb_debug;
	if (debug & IXGB_DBG_STOP) {
		cmn_err(CE_CONT, "ixgb_dbg_enter(%p): %s\n", (void *)ixgbp, s);
		debug_enter("");
	}
}
