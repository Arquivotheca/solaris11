/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * ctsmc_stream - stream related routines
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/cred.h>
#include <sys/log.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/kmem.h>

#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/strsun.h>
#include <sys/poll.h>

#include <sys/debug.h>

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>

#include <sys/inttypes.h>
#include <sys/ksynch.h>

#include <sys/smc_commands.h>
#include <sys/ctsmc_debug.h>
#include <sys/ctsmc.h>

extern int ctsmc_watch_command;
static int ctsmc_vts_hw_debug = 0;

#ifdef DEBUG
static uint_t ctsmc_debug_flag = 0;
static int ctsmc_debug_pri = 1;
#endif /* DEBUG */

extern int ctsmc_pkt_send(ctsmc_state_t *ctsmc,
		ctsmc_minor_t *mnode_p, mblk_t *mp);
extern int ctsmc_findSeqOwner(ctsmc_state_t *ctsmc, uint8_t dAddr,
		uint8_t seq, uint8_t *minor);
extern int ctsmc_search_cmdspec_minor_spec(ctsmc_state_t *ctsmc,
		uint8_t cmd, uint8_t *attr, uint8_t *minor,
		uint16_t **min_mask);
extern int ctsmc_update_cmdspec_list(ctsmc_minor_t *mnode_p, uint8_t ioclen,
		sc_cmdspec_t *cmdspec);
extern int ctsmc_update_seq_list(ctsmc_minor_t *mnode_p, int ioc_cmd,
		sc_seqdesc_t *seqdesc, int ioclen);

static ctsmc_code_ent_t ctsmc_netfn_table[] = {
	{ SMC_NETFN_CHASSIS_REQ, "Chassis Device Request" },
	{ SMC_NETFN_CHASSIS_RSP, "Chassis Device Response" },
	{ SMC_NETFN_BRIDGE_REQ, "Bridge Request" },
	{ SMC_NETFN_BRIDGE_RSP, "Bridge Response" },
	{ SMC_NETFN_SENSOR_REQ, "Sensor and Event Request" },
	{ SMC_NETFN_SENSOR_RSP, "Sensor and Event Response" },
	{ SMC_NETFN_APP_REQ, "Application Request" },
	{ SMC_NETFN_APP_RSP, "Application Response" },
	{ SMC_NETFN_FIRMWARE_REQ, "Firmware Transfer Request" },
	{ SMC_NETFN_FIRMWARE_RSP, "Firmware Transfer Response" },
	{ SMC_NETFN_STORAGE_REQ, "Non-volatile Storage Request" },
	{ SMC_NETFN_STORAGE_RSP, "Non-volatile Storage Response" }
};

/*
 * Netfn codes <= 0x0B are defined in IPMI, codes >= 0x30 are
 * for OEM neffn codes, while range (0x0B, 0x30) is reserved
 */
#define	SMC_DECODE_NETFN(code)	((code) <= 0x0B ? \
			(ctsmc_netfn_table[code].name) : ((code) >= 0x30 ? \
			"OEM" : "RESERVED"))

static ctsmc_code_ent_t ctsmc_lun_table[] = {
	{ SMC_BMC_LUN, "BMC LUN" },
	{ SMC_OEM1_LUN, "OEM LUN 1" },
	{ SMC_SMS_LUN, "SMS LUN" },
	{ SMC_OEM2_LUN, "OEM LUN 2" }
};

#define	SMC_DECODE_LUN(code)	(ctsmc_lun_table[code].name)

#define	SCIDNUM		(202)		/* module id number */
#define	SCMINPSZ	(0)		/* min packet size */
#define	SCMAXPSZ	0x200		/* max packet size */
#define	SCHIWAT		(128 * 1024)	/* hi-water mark */
#define	SCLOWAT		(1)		/* lo-water mark */
#define	SMCNAME		"ctsmc"

static int	ctsmc_open(queue_t *, dev_t *, int, int, cred_t *);
static int	ctsmc_close(queue_t *, int, int, cred_t *);

static int	ctsmc_rsrv(queue_t *);
static int	ctsmc_wput(queue_t *, mblk_t *);
static int	ctsmc_wsrv(queue_t *);

static void	ctsmc_ioctl(queue_t *, mblk_t *);

static struct module_info scinfo = {
	SCIDNUM,
	SMCNAME,
	SCMINPSZ,
	SCMAXPSZ,
	SCHIWAT,
	SCLOWAT
};

static struct qinit sc_rinit = {
	putq, 		/* qi_putq */
	ctsmc_rsrv, 		/* qi_srvq */
	ctsmc_open, 	/* qi_qopen */
	ctsmc_close, 	/* qi_qclose */
	NULL, 		/* qi_qadmin */
	&scinfo,	/* qi_minfo */
	NULL		/* qi_mstat */
};

static struct qinit sc_winit = {
	ctsmc_wput,	/* qi_putq */
	ctsmc_wsrv,	/* qi_srvq */
	NULL, 		/* qi_qopen */
	NULL, 		/* qi_qclose */
	NULL, 		/* qi_qadmin */
	&scinfo,	/* qi_minfo */
	NULL		/* qi_mstat */
};

struct streamtab ctsmc_stab  = {
	&sc_rinit, 	/* upper read init */
	&sc_winit, 	/* upper write init */
	NULL, 		/* lower read init */
	NULL		/* lower write init */
};

extern kmutex_t	ctsmc_excl_lock;
/*
 * message type-to-symbolic name map.
 */
#ifdef	DEBUG
static char *cmd_unknown = "Unknown";

static struct cmd_name {
	int	cmd;
	char	*name;
} ctsmc_msg_name[] = {
	{	M_IOCTL,	"M_IOCTL"	},
	{	M_DATA,		"M_DATA"	},
	{	M_CTL,		"M_CTL"		},
	{	M_IOCDATA,	"M_IOCDATA"	},
	{	M_FLUSH,	"M_FLUSH"	},
};

/*
 * IOCTL value-to-symbolic name map.
 */
static struct cmd_name ctsmc_ioctl_name[] = {
	{ SCIOC_MSG_SPEC,	"SCIOC_MSG_SPEC"	},
	{ SCIOC_RESERVE_SEQN,	"SCIOC_RESERVE_SEQN"	},
	{ SCIOC_FREE_SEQN,	"SCIOC_FREE_SEQN"	},
	{ SCIOC_SEND_SYNC_CMD,	"SCIOC_SEND_SYNC_CMD"	},
	{ SCIOC_ECHO_ON_REQ,	"SCIOC_ECHO_ON_REQ"		},
	{ SCIOC_ECHO_OFF_REQ,	"SCIOC_ECHO_OFF_REQ"	},
	{ SCIOC_ASYNC_SIM,	"SCIOC_ASYNC_SIM"	}
};
#endif	/* DEBUG */

extern int ctsmc_alloc_minor(ctsmc_state_t *ctsmc, ctsmc_minor_t **mnode,
		uint8_t *minor);
extern int ctsmc_free_minor(ctsmc_state_t *ctsmc, uint8_t minor);
extern int ctsmc_cmd_error(queue_t *q, mblk_t *mp, ctsmc_minor_t *mnode_p,
		int ioclen);

#ifdef	DEBUG
static char *ctsmc_debug_strings[SMC_NUM_FLAGS] =
{
	"SMC_UTILS_DEBUG",
	"SMC_STREAM_DEBUG",
	"SMC_ASYNC_DEBUG",
	"SMC_WDOG_DEBUG",
	"SMC_DEVICE_DEBUG",
	"SMC_XPORT_DEBUG",
	"SMC_SEND_DEBUG",
	"SMC_RECV_DEBUG",
	"SMC_IPMI_DEBUG",
	"SMC_DEVI_DEBUG",
	"SMC_I2C_DEBUG",
	"SMC_IOC_DEBUG",
	"SMC_CMD_DEBUG",
	"SMC_REQMSG_DEBUG",
	"SMC_RSPMSG_DEBUG",
	"SMC_HWERR_DEBUG",
	"SMC_INTR_DEBUG",
	"SMC_POLLMODE_DEBUG",
	"SMC_KSTAT_DEBUG",
	"SMC_MCT_DEBUG",
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL,
	"SMC_DEBUG_DEBUG"
};

#define	SMC_FLAG_TO_STR(flag)	(ctsmc_debug_strings[ddi_fls(flag) - 1])
/*
 * Used to map the message type to a symbolic name.
 */
static char *
ctsmc_debug_msg_type(int msg)
{
	int	i;
	int	count;

	count = sizeof (ctsmc_msg_name) / sizeof (struct cmd_name);

	for (i = 0; i < count; ++i) {
		if (msg == ctsmc_msg_name[i].cmd) {
			return (ctsmc_msg_name[i].name);
		}
	}
	return (cmd_unknown);
}

void
ctsmc_debug_log(uint_t ctlbit, int pri, char *fmt, ...)
{
	va_list	adx;
	char	fmtbuf[80];

	if (ctsmc_debug_flag & ctlbit) {
		if (pri <= ctsmc_debug_pri) {
			(void) snprintf(fmtbuf, sizeof (fmtbuf),
					"%s: %s", SMC_FLAG_TO_STR(ctlbit), fmt);
			va_start(adx, fmt);
			vcmn_err(CE_NOTE, fmtbuf, adx);
			va_end(adx);
		}
	}
}

/*
 * Used to map the IOCTL value to a symbolic name.
 */
static char *
ctsmc_debug_ioctl_name(int ioc_cmd)
{
	int	i;
	int	count;

	count = sizeof (ctsmc_ioctl_name) / sizeof (struct cmd_name);

	for (i = 0; i < count; ++i) {
		if (ioc_cmd == ctsmc_ioctl_name[i].cmd) {
			return (ctsmc_ioctl_name[i].name);
		}
	}
	return (cmd_unknown);
}
#endif	/* DEBUG */

/* ARGSUSED */
static int
ctsmc_open(queue_t *q, dev_t *dev, int flag, int sflag, cred_t *cr)
{
	ctsmc_state_t	*ctsmc;
	uint8_t minor;
	minor_t		mdev;
	ctsmc_minor_t	*mnode_p;
	int unit = SMC_UNIT(getminor(*dev));

	SMC_DEBUG3(SMC_STREAM_DEBUG, "open(q=%p), minor = 0x%x, "
			"unit = %d", q, getminor(*dev), unit);

	/*
	 * check whether this q already has a mux attached.
	 * if so, this is a reopen.
	 */
	mutex_enter(&ctsmc_excl_lock);
	if (MUXGET(q)) {	/* reopen */
		cmn_err(CE_NOTE, "ctsmc_open: reopen");
		qprocson(q);
		mutex_exit(&ctsmc_excl_lock);
		return (0);
	}
	mutex_exit(&ctsmc_excl_lock);

	ctsmc = ctsmc_get_soft_state(unit);

	if (ctsmc == NULL) {
		cmn_err(CE_WARN, "Open of SMC called, driver not attached");
		return (ENXIO);
	}

	if (!(ctsmc->ctsmc_init & SMC_IS_ATTACHED)) {
		cmn_err(CE_WARN, "Open of SMC called, driver not attached");
		return (ENXIO);
	}

	/*
	 * Determine minor device number.
	 */
	switch (sflag) {
	case CLONEOPEN:
	case 0:	/* kstr_open() */
		SMC_DEBUG0(SMC_STREAM_DEBUG, "open: CLONEOPEN");
		if (ctsmc_alloc_minor(ctsmc, &mnode_p, &minor) != SMC_SUCCESS) {
			cmn_err(CE_WARN, "ctsmc_open: out of minor devices");
			return (ENXIO);
		}
		mdev = SMC_MAKEMINOR(unit, minor);
		*dev = makedevice(getmajor(*dev), mdev);
		SMC_DEBUG2(SMC_STREAM_DEBUG, "open: Assigned minor = %d, "
				"minornum = 0x%x", minor, mdev);

		break;
	case MODOPEN:
		cmn_err(CE_NOTE, "ctsmc_open: MODOPEN not supported");
		return (ENXIO);
	default:
		SMC_DEBUG0(SMC_STREAM_DEBUG, "open: DEVOPEN: Not supported");
		return (ENXIO);
	}

	mutex_enter(&ctsmc_excl_lock);
	/*
	 * Initialize queue entry of minor node structure
	 */
	mnode_p->ctsmc_rq = q;

	/*
	 * Set queue private data to this minor node pointer
	 */
	MUXPUT(q, mnode_p);
	MUXPUT(WR(q), mnode_p);

	SMC_DEBUG4(SMC_STREAM_DEBUG, "open: rq %p, MUX RD %p wq: %p MUX WR: %p",
			q, MUXGET(q), WR(q), MUXGET(WR(q)));

	qprocson(q);
	mutex_exit(&ctsmc_excl_lock);

	/*
	 * Now setup the stream head to message discard mode for reading
	 */
	if (sflag == CLONEOPEN) {
		mblk_t *mp;

		if (mp = allocb(sizeof (struct stroptions), BPRI_MED)) {
			struct stroptions *sop =
				(struct stroptions *)mp->b_rptr;
			sop->so_flags = SO_READOPT;
			sop->so_readopt = RMSGD;
			mp->b_datap->db_type = M_SETOPTS;
			mp->b_wptr += sizeof (struct stroptions);
			putnext(q, mp);
		}
	}
	LOCK_DATA(ctsmc);
	ctsmc->ctsmc_opens++;
	UNLOCK_DATA(ctsmc);

	return (0);
}

/* ARGSUSED */
static int
ctsmc_close(queue_t *q, int flag, int otyp, cred_t *cr)
{
	ctsmc_minor_t	*mnode_p;
	ctsmc_state_t *ctsmc;
	minor_t minor;

	ASSERT(q != NULL);

	ASSERT(MUXGET(q) != NULL);

	SMC_DEBUG2(SMC_STREAM_DEBUG, "close: q %p, MUX RD %p", q, MUXGET(q));

	qprocsoff(q);

	mutex_enter(&ctsmc_excl_lock);
	mnode_p = MUXGET(q);

	ctsmc = mnode_p->ctsmc_state;
	minor = mnode_p->minor;

	/*
	 * Implicit detach stream from interface.
	 */

	MUXPUT(q, NULL);
	MUXPUT(WR(q), NULL);
	mutex_exit(&ctsmc_excl_lock);

	/*
	 * Make sure this minor number does not have a request pending.
	 * Do all necessary cleanup for this minor number, e.g. remove this
	 * minor from all lists, e.g. async list, excl list, watchdog list etc.
	 */
	(void) ctsmc_free_minor(ctsmc, minor);

	LOCK_DATA(ctsmc);
	ctsmc->ctsmc_opens--;

	UNLOCK_DATA(ctsmc);

	return (0);
}

/*
 * canonical flush handling.
 */
static void
ctsmc_flush(queue_t *q, mblk_t *mp)
{
	if (*mp->b_rptr & FLUSHW) {
		flushq(q, FLUSHDATA);
		/* free any messages tied to sc */
	}

	if (*mp->b_rptr & FLUSHR) {
		*mp->b_rptr &= ~FLUSHW;
		qreply(q, mp);
	} else {
		freemsg(mp);
	}
}

/*
 * upper stream write
 */
static int
ctsmc_wput(queue_t *q, mblk_t *mp)
{
	ctsmc_minor_t	*mnode_p;
	int		msg;

	msg = DB_TYPE(mp);

	SMC_DEBUG4(SMC_STREAM_DEBUG, "wput(q=%p, mp=%p): msg:%d=%s",
			q, mp, msg, ctsmc_debug_msg_type(msg));

	switch (msg) {
	default:
		freemsg(mp);
		break;

	case M_FLUSH:	/* canonical flush handling */
		ctsmc_flush(q, mp);
		break;

	case M_CTL: /* Added for kernel clients */
	case M_IOCTL:
		qwriter(q, mp, ctsmc_ioctl, PERIM_INNER);
		break;

	case M_DATA:
		/*
		 * Check for an invalid command. If there is
		 * an error, ctsmc_cmd_error() will send an error
		 * upstream using qreply().
		 */

		mnode_p = MUXGET(q);
		if (ctsmc_cmd_error(q, mp, mnode_p, 0) ==
				SMC_SUCCESS) { /* if error - handle */
			/*
			 * If the command is valid, we use putq(q, mp), to
			 * leave the lengthy processing for the ctsmc_wsrv()
			 * routine.
			 */
			(void) putq(q, mp);
		}
		break;
	}

	return (0);
}

/*
 * write side server function
 *	- at this time, we only handle data messages down
 *	here.
 */
static int
ctsmc_wsrv(queue_t *q)
{
	ctsmc_minor_t	*mnode_p;
	mblk_t		*mp;

	SMC_DEBUG0(SMC_STREAM_DEBUG, "wsrv");

	while (mp = getq(q))
		switch (DB_TYPE(mp)) {
		case M_DATA:
			/*
			 * process data message
			 * for now we only print the message from the driver.
			 */
			/* echo message upstream to test upstream data flow */

			mnode_p = MUXGET(WR(q));
			if (mnode_p->ctsmc_flags & SMC_ECHO) {
				/*
				 *  send the original message back up
				 */
				qreply(q, mp);
			} else {
				int	e;
				sc_reqmsg_t msg;

				ctsmc_msg_copy(mp, (uchar_t *)&msg);

				e = ctsmc_pkt_send(mnode_p->ctsmc_state,
					mnode_p, mp);

				/*
				 * If sending message failed for some
				 * reason, generate a reply indicating
				 * error. The message pointer would
				 * have been freed already.
				 */
				if (e != SMC_SUCCESS) {
					/* putbq(q, mp); */

					sc_rspmsg_t	rep;
					/*
					 * construct an error reply to this cmd
					 */
					SMC_DEBUG(SMC_STREAM_DEBUG, "error: "
							"error reply: %d", e);
					ctsmc_msg_response(&msg, &rep,
						SMC_CC_NODE_BUSY, e);
					mp = ctsmc_rmsg_message(&rep);
					if (mp != NULL)
						qreply(q, mp);
				}
			}
			break;

		default:
			freemsg(mp);
			break;
		}

	return (0);
}

/*
 * Read side service procedure to push a message
 * upstream
 */
static int
ctsmc_rsrv(queue_t *q)
{
	mblk_t *bp;

	while (canputnext(q) && (bp = getq(q)))
		putnext(q, bp);

	return (0);
}

static void
ctsmc_minor_sendq(ctsmc_minor_t *mnode_p, mblk_t *mp)
{
	queue_t		*q;

	SMC_DEBUG(SMC_ASYNC_DEBUG, "sendup %p", mnode_p);

	q = mnode_p->ctsmc_rq;

	if (canput(q)) {
		SMC_DEBUG(SMC_ASYNC_DEBUG, "sendup: putnext %p", q);
		(void) putq(q, mp);
	} else {
		/* drop packet: queue not available */
		SMC_DEBUG(SMC_ASYNC_DEBUG, "sendup: drop packet"
				"--available q %p", q);
		if (mp)
			freemsg(mp);
		mp = NULL;
	}
}

static int
ctsmc_upkt_putq(ctsmc_minor_t *mnode_p, sc_rspmsg_t *msg)
{
	mblk_t		*mp;
	sc_rspmsg_t *rsp;

	SMC_DEBUG0(SMC_ASYNC_DEBUG, "upstream");

	if (mnode_p == NULL) {
		SMC_DEBUG(SMC_ASYNC_DEBUG, "upstream: mnode_p=%p", mnode_p);
		return (ENXIO);
	}

	/*
	 * create an mblk_t version of our response packet.
	 */
	mp = ctsmc_rmsg_message(msg);

	if (mp == NULL) {
		return (ENOMEM);
	}

	SMC_DEBUG0(SMC_ASYNC_DEBUG, "upstream: sendup");

	rsp = (sc_rspmsg_t *)mp->b_rptr;
	if (ctsmc_vts_hw_debug && SC_MSG_ID(rsp) == 0 &&
		SC_MSG_CMD(rsp) == SMC_IPMI_RESPONSE_NOTIF)
		cmn_err(CE_NOTE, "Sending Async msg upstream: seq = %d "
			"len = %d, cmd = 0x%x, cc = 0x%x", SC_MSG_ID(rsp),
			SC_MSG_LEN(rsp), SC_MSG_CMD(rsp), SC_MSG_CC(rsp));

	ctsmc_minor_sendq(mnode_p, mp);

	SMC_DEBUG0(SMC_ASYNC_DEBUG, "upstream: sendup return");
	return (0);
}

void
ctsmc_async_putq(ctsmc_state_t *ctsmc, sc_rspmsg_t *rsp)
{
	/*
	 * Check whethter it's a regular async message from SMC, or
	 * a different interrupt, e.g. watchdog expiration
	 */
	uint8_t cmd = SC_MSG_CMD(rsp), minor, attr;
	uint16_t i, j, mask, *m_mask;
	uchar_t *data;
	int count = 0;

	/*
	 * If this message is an IPMI asynchronous notification, and
	 * a response message (odd NetFn), send message to the stream
	 * with matching sequence number
	 */
	if (cmd == SMC_IPMI_RESPONSE_NOTIF) {
		uint8_t rAddr, NetFn, seq, lun;
#ifdef DEBUG
		uint8_t *rmsg = (uint8_t *)rsp;
		char msg[256];
#endif

		data = SC_MSG_DATA(rsp);
		rAddr = data[IPMB_OFF_OADDR];
		NetFn = IPMB_GET_NETFN(data[IPMB_OFF_NETFN]);
		lun = IPMB_GET_LUN(data[IPMB_OFF_NETFN]);
		seq = IPMB_GET_SEQ(data[IPMB_OFF_SEQ]);

		SMC_DEBUG4(SMC_IPMI_DEBUG,
			"Received IPMI_NOTIF:Len = "
			" %d, addr = 0x%x, seq = %d, cmd = "
			" 0x%x", SMC_MSG_LEN(rsp), rAddr,
			seq, data[IPMB_OFF_CMD]);
#ifdef DEBUG
		(void) strcpy(msg, "Msg Bytes: ");
		for (i = 0; i < SMC_MSG_LEN(rsp); i++) {
			(void) sprintf(msg, "%s %2x", msg, rmsg[i]);
		}
		SMC_DEBUG(SMC_IPMI_DEBUG, "%s", msg);
#endif

		/*
		 * If it's an IPMB response message, forward it to
		 * appropriate minor number and return
		 */
		if (IS_IPMB_RSP(NetFn) &&
				(lun == SMC_SMS_LUN)) {
			SMC_INCR_CNT(CTSMC_IPMI_RSPS);
			if (ctsmc_findSeqOwner(ctsmc, rAddr,
				seq, &minor) == SMC_SUCCESS) {
				(void) ctsmc_upkt_putq(GETMINOR(ctsmc, minor),
					rsp);
				SMC_DEBUG3(SMC_IPMI_DEBUG,
					"Forwarding "
					"IPMI_NOTIF: cmd 0x%x, "
					"seq %d to minor %d",
					data[IPMB_OFF_CMD], seq,
					minor);
				if (rAddr == BMC_IPMB_ADDR)
					SMC_INCR_CNT(CTSMC_BMC_RSPS);
			} else {
				SMC_INCR_CNT(CTSMC_IPMI_RSPS_DROP);
				CTSMC_COPY_LOG(rsp, CTSMC_IPMIDROP_IDX, count);
			}
			return;
		}
#ifdef	DEBUG
		if (IS_IPMB_RSP(NetFn) && (lun != SMC_SMS_LUN))
			SMC_DEBUG2(SMC_IPMI_DEBUG, "IPMI response not directed "
					"to SMS LUN: NetFn is %s, lun is %s",
					SMC_DECODE_NETFN(NetFn),
					SMC_DECODE_LUN(lun));
#endif

		/*
		 * Increment counter if it's an IPMI request msg
		 */
		if (IS_IPMB_REQ(NetFn)) {
			SMC_INCR_CNT(CTSMC_IPMI_EVTS);
			if (rAddr == BMC_IPMB_ADDR)
				SMC_INCR_CNT(CTSMC_BMC_EVTS);
		}

		/* fall through */
	}

	/*
	 * A 'regular' async message sent by SMC in SMC response message
	 * format. Scan async list and forward to each party which
	 * registered interest in receiving this command as response.
	 */
	if (ctsmc_search_cmdspec_minor_spec(ctsmc, cmd, &attr, &minor,
			&m_mask) != SMC_SUCCESS) {
		SMC_INCR_CNT(CTSMC_IPMI_RSPS_DROP);
		return;
	}

	/* If it's an exclusive entry, just forward to it and return */
	if (attr == SC_ATTR_EXCLUSIVE) {
		SMC_DEBUG2(SMC_ASYNC_DEBUG, "Async Msg 0x%x: Notifying "
			"minor node %d", cmd, minor);
		(void) ctsmc_upkt_putq(GETMINOR(ctsmc, minor), rsp);

		return;
	}

	/* Need to send to multiple clients */
	for (i = 0; i < NUM_BLOCKS; i++) {
		mask = m_mask[i];
		for (j = 0; mask; mask >>= 1, j++) {
			if (mask&1) {
				minor = i * SMC_BLOCK_SZ + j;
				SMC_DEBUG2(SMC_ASYNC_DEBUG, "Async Msg "
					"0x%x: Notifying minor node %d",
					cmd, minor);
				(void) ctsmc_upkt_putq(GETMINOR(ctsmc, minor),
						rsp);
			}
		}
	}
}

static int
ctsmc_command_copyin(int ioclen, mblk_t *mp, void *in, int length)
{
	SMC_DEBUG(SMC_IOC_DEBUG, "copyin: %d bytes", length);
	if (ioclen <= length) {
		bcopy((void *)mp->b_rptr, in, length);
		return (0);
	}
	return (EINVAL);
}

int
ctsmc_command_copyout(int ioclen, mblk_t *mp, void *out, int length)
{
	SMC_DEBUG(SMC_IOC_DEBUG, "copyout: %d bytes", length);
	if (ioclen >= length) {
		bcopy(out, (void *)mp->b_rptr, length);
		mp->b_wptr = mp->b_rptr + length;
		return (0);
	}
	return (EINVAL);
}

/*
 * These are the ioctl functions mainly for managing
 * communication link, user seq etc. and send/recv
 * commands
 */

static void
ctsmc_ioctl(queue_t *q, mblk_t *mp)
{
	ctsmc_minor_t	*mnode_p = MUXGET(q);
	ctsmc_state_t	*ctsmc = mnode_p->ctsmc_state;
	struct iocblk	*ioc = (struct iocblk *)mp->b_rptr;
	int		e = 0, ret, ioclen = ioc->ioc_count;

	SMC_DEBUG2(SMC_STREAM_DEBUG, "ctsmc_ioctl: unit = %d, minor = %d",
			ctsmc->ctsmc_instance, mnode_p->minor);
	SMC_DEBUG4(SMC_IOC_DEBUG, "ioctl(q=%p, mp=%p): ioc_cmd %d=%s",
			q, mp, ioc->ioc_cmd,
			ctsmc_debug_ioctl_name(ioc->ioc_cmd));

	switch (ioc->ioc_cmd) {

	case SCIOC_ECHO_ON_REQ:
		mnode_p->ctsmc_flags |= SMC_ECHO;
		ioc->ioc_error = e;
		break;

	case SCIOC_ECHO_OFF_REQ:
		mnode_p->ctsmc_flags &= ~SMC_ECHO;
		ioc->ioc_error = e;
		break;

	case SCIOC_MSG_SPEC:
		{
			sc_cmdspec_t cmdspec;
			uint8_t minlen = sizeof (sc_cmdspec_t) -
				MAX_CMDS * sizeof (uint8_t);
			uint8_t maxlen = sizeof (sc_cmdspec_t);

			if ((ioclen > maxlen) || (ioclen < minlen)) {
				ioc->ioc_error = EINVAL;
				break;
			}
			(void) ctsmc_command_copyin(ioclen, mp->b_cont,
					(void *)&cmdspec, ioclen);

			if (ctsmc_update_cmdspec_list(mnode_p, ioclen,
					&cmdspec) != SMC_SUCCESS) {
				ioc->ioc_error = EINVAL;
				break;
			}
		}
		break;

	case SCIOC_RESERVE_SEQN:
	case SCIOC_FREE_SEQN:
		{
			sc_seqdesc_t	seq_desc;
			uint8_t len = sizeof (sc_seqdesc_t) -
				SC_SEQ_SZ * sizeof (uint8_t);

			/*
			 * Make sure the space provided is enough for at
			 * least one sequence number
			 */
			if ((ioclen < len) ||
					(ioclen > sizeof (sc_seqdesc_t))) {
				ioc->ioc_error = EINVAL;
				break;
			}

			(void) ctsmc_command_copyin(ioclen, mp->b_cont,
					(void *)&seq_desc, ioclen);

			if (ctsmc_update_seq_list(mnode_p, ioc->ioc_cmd,
					&seq_desc, ioclen) != SMC_SUCCESS) {
				ioc->ioc_error = (ioc->ioc_cmd ==
					SCIOC_RESERVE_SEQN) ? EAGAIN : EINVAL;
				break;
			}

			/* Copyout the data for SCIOC_RESERVE_SEQN */
			if (ioc->ioc_cmd == SCIOC_RESERVE_SEQN) {
				len += seq_desc.n_seqn;
				(void) ctsmc_command_copyout(ioclen, mp->b_cont,
						(void *)&seq_desc, len);
			}
		}
		break;

	case SCIOC_SEND_SYNC_CMD:
		{
			ret = ctsmc_send_sync_cmd(q, mp->b_cont, ioclen);

			if ((ret == SMC_OP_ABORTED) ||
				(ret == SMC_NOMEM)) {
				return; /* No need to ACK */
			} else
			if (ret == SMC_HW_FAILURE)
				ioc->ioc_error = EAGAIN;
			else
			if (ret != SMC_SUCCESS)
				ioc->ioc_error = EINVAL;
			else
				ioc->ioc_error = 0;
		}
		break;

		/*
		 * Simulate an async notification. An application, e.g
		 * diagnostics can use this ioctl to send any async
		 * response message.
		 */
	case SCIOC_ASYNC_SIM:
		{
			sc_rspmsg_t *ursp = (sc_rspmsg_t *)mp->b_cont->b_rptr;
			uint8_t cmd = ursp->hdr.cmd;

			SMC_DEBUG0(SMC_IOC_DEBUG, "Received "
					"SC_ASYNC_SIM ioctl");
			if (ctsmc_async_valid(cmd) == SMC_SUCCESS) {
				ctsmc_async_putq(ctsmc, ursp);
			} else {
				cmn_err(CE_WARN, "Unrecognized async command");
				ioc->ioc_error = EINVAL;
				break;
			}
		}
		break;

	default:
		/*
		 * if we don't understand the ioctl
		 */
		SMC_DEBUG2(SMC_IOC_DEBUG, "ctsmc_ioctl(q=%p): "
				"unknown ioctl %x", q, ioc->ioc_cmd);
		e = EINVAL;
		ioc->ioc_error = e;
		break;
	}

	SMC_DEBUG(SMC_IOC_DEBUG, "ioctl: error = %d", ioc->ioc_error);

	if (DB_TYPE(mp) != M_CTL) { /* for M_CTL, don't change the type */
		DB_TYPE(mp) = ioc->ioc_error ? M_IOCNAK : M_IOCACK;
		ioc->ioc_count = MBLKL(mp->b_cont);
	}

	qreply(q, mp);
}

static void
ctsmc_minor_sendup(ctsmc_minor_t *mnode_p, mblk_t *mp)
{
	queue_t		*q;

	SMC_DEBUG(SMC_RECV_DEBUG, "sendup %p", mnode_p);

	q = mnode_p->ctsmc_rq;

	if (canputnext(q)) {
		SMC_DEBUG(SMC_RECV_DEBUG, "sendup: putnext %p", q);
		putnext(q, mp);
	} else {
		/* drop packet: queue not available */
		SMC_DEBUG(SMC_RECV_DEBUG, "sendup: drop packet"
				"--available q %p", q);
		if (mp)
			freemsg(mp);
		mp = NULL;
	}
}

/*
 * Sends a response packet upstream, no need to convert from
 * SMC to user response packet
 */
static int
ctsmc_upkt_upstream(ctsmc_minor_t *mnode_p, sc_rspmsg_t *msg)
{
	mblk_t		*mp;

	SMC_DEBUG0(SMC_RECV_DEBUG, "upstream");

	if (mnode_p == NULL) {
		SMC_DEBUG(SMC_RECV_DEBUG, "upstream: mnode_p=%p", mnode_p);
		return (ENXIO);
	}

	/*
	 * create an mblk_t version of our response packet.
	 */
	mp = ctsmc_rmsg_message(msg);

	if (mp == NULL) {
		return (ENOMEM);
	}

#ifdef DEBUG
	SMC_DEBUG0(SMC_RECV_DEBUG, "upstream: sendup");

	if (SC_MSG_ID(msg) == 0 &&
		SC_MSG_CMD(msg) == SMC_IPMI_RESPONSE_NOTIF)
		SMC_DEBUG4(SMC_IPMI_DEBUG, "Sending Async msg upstream: "
			"seq = %d, len = %d, cmd = 0x%x, cc = 0x%x",
			SC_MSG_ID(msg), SC_MSG_LEN(msg), SC_MSG_CMD(msg),
			SC_MSG_CC(msg));
#endif

	ctsmc_minor_sendup(mnode_p, mp);

	SMC_DEBUG0(SMC_RECV_DEBUG, "upstream: sendup return");
	return (0);
}

int
ctsmc_pkt_upstream(ctsmc_minor_t *mnode_p, ctsmc_rsppkt_t *msg,
		uint8_t uSeq)
{
	sc_rspmsg_t ursp;
	uint8_t cmd;

	/*
	 * First, convert the raw message we received from SMC into
	 * format we will be sending upstream
	 */
	rmsg_convert(&ursp, msg, uSeq);
	cmd = SC_MSG_CMD(&ursp);

	if (cmd == ctsmc_watch_command) {
		int i;
		uint8_t *rmg = (uint8_t *)&ursp;
		char rmsg[256];
		bzero(rmsg, 256);
		(void) strcpy(rmsg, "Recv User Msg Bytes ");
		(void) sprintf(rmsg, "%s for cmd 0x%x :", rmsg, cmd);
		for (i = 0; i < SC_RECV_HEADER + SC_MSG_LEN(&ursp); i++) {
			(void) sprintf(rmsg, "%s %2x", rmsg, rmg[i]);
		}
		cmn_err(CE_NOTE, "%s", rmsg);
	}

	return (ctsmc_upkt_upstream(mnode_p, &ursp));
}
