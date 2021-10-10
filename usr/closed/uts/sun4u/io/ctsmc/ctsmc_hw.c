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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * ctsmc_hw - Hardware interface and related routines
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/dditypes.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ksynch.h>
#include <sys/strsun.h>

#include <sys/smc_commands.h>
#include <sys/ctsmc_debug.h>
#include <sys/ctsmc.h>

/* Index into ctsmc_cc_table */
#define	CC_TO_INDEX(cc)	(((cc) == 0) ? 0 :	\
		((cc == SMC_CC_UNSPECIFIED_ERROR) ?	\
			1 : ((cc) - SMC_CC_NODE_BUSY + 2)))

static ctsmc_code_ent_t ctsmc_cc_table[] = {
	{ CC_TO_INDEX(SMC_CC_SUCCESS), "SMC_CC_SUCCESS" },
	{ CC_TO_INDEX(SMC_CC_UNSPECIFIED_ERROR), "SMC_CC_UNSPECIFIED_ERROR" },
	{ CC_TO_INDEX(SMC_CC_NODE_BUSY), "SMC_CC_NODE_BUSY" },
	{ CC_TO_INDEX(SMC_CC_INVALID_COMMAND), "SMC_CC_INVALID_COMMAND" },
	{ CC_TO_INDEX(SMC_CC_INVALID_COMMAND_ON_LUN),
		"SMC_CC_INVALID_COMMAND_ON_LUN" },
	{ CC_TO_INDEX(SMC_CC_TIMEOUT), "SMC_CC_TIMEOUT" },
	{ CC_TO_INDEX(SMC_CC_RESOURCE_NOTAVAIL), "SMC_CC_RESOURCE_NOTAVAIL" },
	{ CC_TO_INDEX(SMC_CC_RESERVATION), "SMC_CC_RESERVATION" },
	{ CC_TO_INDEX(SMC_CC_REQ_TRUNC), "SMC_CC_REQ_TRUNC" },
	{ CC_TO_INDEX(SMC_CC_REQLEN_NOTVALID), "SMC_CC_REQLEN_NOTVALID" },
	{ CC_TO_INDEX(SMC_CC_REQLEN_EXCEED), "SMC_CC_REQLEN_EXCEED" },
	{ CC_TO_INDEX(SMC_CC_PARAM_OUT_OF_RANGE), "SMC_CC_PARAM_OUT_OF_RANGE" },
	{ CC_TO_INDEX(SMC_CC_REQUEST_BYTES_FAILED),
		"SMC_CC_REQUEST_BYTES_FAILED" },
	{ CC_TO_INDEX(SMC_CC_NOT_PRESENT), "SMC_CC_NOT_PRESENT" },
	{ CC_TO_INDEX(SMC_CC_INVALID_FIELD), "SMC_CC_INVALID_FIELD" },
	{ CC_TO_INDEX(SMC_CC_ILLEGAL_COMMAND), "SMC_CC_ILLEGAL_COMMAND" },
	{ CC_TO_INDEX(SMC_CC_RESPONSE_FAILED), "SMC_CC_RESPONSE_FAILED" },
	{ CC_TO_INDEX(SMC_CC_DUPLICATE_REQUEST), "SMC_CC_DUPLICATE_REQUEST" },
	{ CC_TO_INDEX(SMC_CC_SDR_UPDATE_MODE), "SMC_CC_SDR_UPDATE_MODE" },
	{ CC_TO_INDEX(SMC_CC_FIRMWARE_UPDATE_MODE),
		"SMC_CC_FIRMWARE_UPDATE_MODE" },
	{ CC_TO_INDEX(SMC_CC_INIT_IN_PROGRESS), "SMC_CC_INIT_IN_PROGRESS" }
};

#define	SMC_DECODE_CC(code)	\
	(ctsmc_cc_table[CC_TO_INDEX(((code) & 0x3F))].name)

#define	LOOPMAX		200
#define	SMCDELAY	100

enum_intr_desc_t enum_intr_desc;

static int ctsmc_wd_cmd = WD1_CMD_NORESTART;

#define	SMC_WATCHDOG_DEBUG	0
/*
 * To watch a particular command
 */
int ctsmc_watch_command = 0;

#define	SMC_NUM_STAT	8
#define	SMC_INCR(X)	((X) = ((X) == SMC_NUM_STAT - 1 ? 0 : (X) + 1))
#if SMC_WATCHDOG_DEBUG
typedef	struct {
	hrtime_t	ctsmc_time_wr_start;
	hrtime_t	ctsmc_time_wr_end;
	hrtime_t	ctsmc_time_rd_start;
	hrtime_t	ctsmc_time_rd_end;
	uint8_t		ctsmc_seq;
	uint8_t		app_seq;
} ctsmc_stat_t;

static	ctsmc_stat_t	ctsmc_stat[SMC_NUM_STAT];

static	int ctsmc_idx = 0, ctsmc_last_idx = 0;
static hrtime_t	ctsmc_wdog_rsp_end = 0;
static uint8_t ctsmc_wdog_rsp_seq = 0;
#endif

/*
 * SMC hardware statictics. We maintain all statistics about
 * the number of packets sent and received, number of sync
 * requests, ordinary data requests, async responses etc.
 */
uint64_t	ctsmc_hw_stat[CTSMC_NUM_COUNTERS];

static int ctsmc_read_kcs_error = 0;
static int ctsmc_wr_cond[20], ctsmc_wr_err[20], ctsmc_rd_err[20];

#define	CTSMC_INCVAR(vaR, idX) ((vaR)[idX])++
#define	CTSMC_INCR_WRCOND(idx)	CTSMC_INCVAR(ctsmc_wr_cond, idx)
#define	CTSMC_INCR_WRERR(idx)	CTSMC_INCVAR(ctsmc_wr_err, idx)
#define	CTSMC_INCR_RDERR(idx)	CTSMC_INCVAR(ctsmc_rd_err, idx)

static	int ctsmc_cnt_noseq = 0, ctsmc_cnt_seqfull = 0;
static	uint64_t ctsmc_clearerr_cnt = 0;
static	uint64_t ctsmc_num_write_errors = 0;
static	uint64_t ctsmc_cnt_write_failed = 0;
static	uint64_t ctsmc_num_write_init_errors = 0;
static	int ctsmc_no_intr1 = 0;
static	int ctsmc_no_intr2 = 0;
/*
 * Maximum number of requests that can be outstanding before
 * host receives a response.
 */
static int ctsmc_max_requests = 16;
static uint64_t ctsmc_cnt_threshold = 0; /* #times request threshold exceeded */

#define	SMC_MAX_KCS_REC	1024
static	uint8_t	ctsmc_wr_kcs_reg[SMC_MAX_KCS_REC];
static int ctsmc_wr_kcs_reg_index = 0;
static	uint8_t	ctsmc_rd_kcs_reg[SMC_MAX_KCS_REC];
static int ctsmc_rd_kcs_reg_index = 0;

extern int ctsmc_mct_process_enum;

#ifdef DEBUG
static int ctsmc_hw_stat_flag = 0;
#endif

static int ctsmc_int_tout = 5;
hrtime_t	ctsmc_intr_start, ctsmc_intr_end;
hrtime_t	ctsmc_write_start, ctsmc_write_end;

/*
 * Maximum time SMC driver waits for response
 * to SCIOC_SEND_SYNC_CMD ioctl
 */
static int ctsmc_max_timeout = 60;

#ifdef	DEBUG
#define	SMC_GET_HRTIME(VAR) { VAR = gethrtime(); }
#else
#define	SMC_GET_HRTIME(VAR) {; }
#endif

/*
 * The following variable specifies how many bufers to allocate during
 * attach time to keep track of number of IPMI packets sent and received
 * May be set in /etc/system to track packets
 */
#ifdef	DEBUG
static int ctsmc_num_logs = 50;
#else
static int ctsmc_num_logs = 0;
#endif
int	ctsmc_num_ipmi_buffers;

int ctsmc_buf_idx[CTSMC_NUM_LOGS];
void **ctsmc_buf_logs[CTSMC_NUM_LOGS];

extern void ctsmc_async_putq(ctsmc_state_t *ctsmc, sc_rspmsg_t *rsp);
extern int ctsmc_init_seq_counter(ctsmc_state_t *ctsmc, ctsmc_reqpkt_t *req);
extern int ctsmc_command_copyout(int ioclen, mblk_t *mp, void *out, int length);
static int ctsmc_read(ctsmc_state_t *ctsmc, ctsmc_rsppkt_t *msg);
static int ctsmc_pkt_readall(ctsmc_state_t *ctsmc);
static int ctsmc_readpoll(ctsmc_state_t *ctsmc, ctsmc_reqpkt_t *req);
int ctsmc_command_send(ctsmc_state_t *ctsmc, uint8_t cmd,
		uchar_t *inbuf, uint8_t	inlen,
		uchar_t	*outbuf, uint8_t *outlen);

extern uint8_t ctsmc_findSeq(ctsmc_state_t *ctsmc, uint8_t seq, uint8_t cmd,
		uint8_t *minor, uint8_t *uSequence, uint8_t *pos);
extern void ctsmc_freeSeq(ctsmc_state_t *ctsmc, uint8_t seq, uint8_t pos);
extern uint8_t ctsmc_getSeq(ctsmc_state_t *ctsmc, uint8_t minor,
		uint8_t uSequence, uint8_t cmd, uint8_t *seq, uint8_t *pos);
extern uint8_t ctsmc_getSeq0(ctsmc_state_t *ctsmc, uint8_t minor,
		uint8_t uSequence, uint8_t cmd, uint8_t *pos);
extern int ctsmc_pkt_upstream(ctsmc_minor_t *mnode_p,
		ctsmc_rsppkt_t *msg, uint8_t uSeq);
extern int ctsmc_cmd_error(queue_t *q, mblk_t *mp, ctsmc_minor_t *mnode_p,
		int ioclen);

/*
 * Send a single data byte to the SMC
 */

static void
ctsmc_put(ctsmc_hwinfo_t *hwptr, uchar_t byte)
{
	ddi_put8(hwptr->smh_handle, &hwptr->smh_reg->smr_data_io, byte);
	drv_usecwait(SMCDELAY);
}

/*
 * Read a single data byte from the SMC
 */

static uchar_t
ctsmc_get(ctsmc_hwinfo_t *hwptr)
{
	uchar_t		datum;

	datum = ddi_get8(hwptr->smh_handle, &hwptr->smh_reg->smr_data_io);
	drv_usecwait(SMCDELAY);
	return (datum);
}

/*
 * Read the SMC's current status.
 */

static uchar_t
ctsmc_status(ctsmc_hwinfo_t *hwptr)
{
	uchar_t		csr;

	csr = ddi_get8(hwptr->smh_handle, &hwptr->smh_reg->smr_csr);
	drv_usecwait(SMCDELAY);
	return (csr);
}

static uchar_t
ctsmc_getcsr(ctsmc_hwinfo_t *hwptr)
{
	return (ddi_get8(hwptr->smh_handle, &hwptr->smh_reg->smr_csr));
}

/*
 * wait for IBF to clear
 */

static int
ctsmc_data_not_pending(ctsmc_hwinfo_t *hwptr, int loopmax)
{
	uchar_t	csr = ctsmc_status(hwptr);

	while (SC_DPEND(csr) && (loopmax-- > 0)) {
		csr = ctsmc_status(hwptr); /* drv_usecwait in smc_status */
	}
	if (SC_DPEND(csr))
		return (SMC_FAILURE);
	else
		return (SMC_SUCCESS);
}

/*
 * Check few times to see whether SMC needs host to
 * take care of it, e.g. watchdog expiry, data etc.
 */
static int
ctsmc_data_not_waiting(ctsmc_hwinfo_t *m)
{
	uint8_t csr;
	int waitcnt = 10;

	while (waitcnt--) {
		csr = ctsmc_getcsr(m);
		if (SC_WATCHDOG(csr) || SC_PANICSYS(csr) ||
			SC_DATN(csr) || SC_DAVAIL(csr)) {
			return (SMC_FAILURE);
		}
		drv_usecwait(50);
	}

	return (SMC_SUCCESS);
}

/*
 * SMC command op: WRITE_START | WRITE_END | READ_START | ABORT
 */

static int
ctsmc_command(ctsmc_hwinfo_t *hwptr, uchar_t cmd)
{
	ddi_put8(hwptr->smh_handle, &hwptr->smh_reg->smr_csr, cmd);
	drv_usecwait(SMCDELAY);

	/*
	 * ddi_put8 of the cmd to the CSR causes the IBF
	 * to be set: wait for IBF to clear before we
	 * proceed.
	 */
	return (ctsmc_data_not_pending(hwptr, LOOPMAX));
}

/*
 * wait until SMC reaches READ state
 */

static int
ctsmc_readable(ctsmc_hwinfo_t *hwptr, int loopmax)
{
	uchar_t	csr = ctsmc_status(hwptr);

	while (!SC_READ(csr) && (loopmax-- > 0)) {
		csr = ctsmc_status(hwptr);
	}
	if (!SC_READ(csr))
		return (SMC_FAILURE);
	else
		return (SMC_SUCCESS);
}

/*
 * wait until SMC reaches WRITE state
 */

static int
ctsmc_writeable(ctsmc_hwinfo_t *hwptr, int loopmax)
{
	uchar_t	csr = ctsmc_status(hwptr);

	while (!SC_WRITE(csr) && (loopmax-- > 0)) {
		csr = ctsmc_status(hwptr);
	}
	if (!SC_WRITE(csr))
		return (SMC_FAILURE);
	else
		return (SMC_SUCCESS);
}

/*
 * wait until SMC reaches IDLE state
 */

static int
ctsmc_idle(ctsmc_hwinfo_t *hwptr, int loopmax)
{
	uchar_t	csr = ctsmc_status(hwptr);

	while (!SC_IDLE(csr) && (loopmax-- > 0)) {
		csr = ctsmc_status(hwptr);
	}
	if (!SC_IDLE(csr))
		return (SMC_FAILURE);
	else
		return (SMC_SUCCESS);
}

static void
ctsmc_regs_attr(ctsmc_hwinfo_t *hwptr)
{
	hwptr->smh_attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	hwptr->smh_attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
	hwptr->smh_attr.devacc_attr_endian_flags = DDI_NEVERSWAP_ACC;
}


/*
 * Allocate a structure to contain HW-specific setup
 * information--e.g., mapping, interrupts, attributes...
 *
 * Setup HW mapping and return an opaque pointer to the
 * setup information.
 */
void *
ctsmc_regs_map(dev_info_t *dip)
{
	ctsmc_hwinfo_t	*smchw = NEW(1, ctsmc_hwinfo_t);
	int		e;
	ctsmc_state_t *ctsmc = ctsmc_get_soft_state(ddi_get_instance(dip));

	if (smchw == NULL)
		return (NULL);
	ctsmc_regs_attr(smchw);

	e = ddi_regs_map_setup(dip, 0, (caddr_t *)&smchw->smh_reg, 0, 0,
				&smchw->smh_attr, &smchw->smh_handle);

	if (e != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: unable to map register entry 0\n",
			ddi_driver_name(dip), ddi_get_instance(dip));

		FREE(smchw, 1, ctsmc_hwinfo_t);
		smchw = NULL;
		return (NULL);
	}
	ctsmc->ctsmc_hw = (void *) smchw;

	return ((void *) smchw);
}

void
ctsmc_setup_intr(ctsmc_state_t *ctsmc, dev_info_t *dip)
{
	ctsmc_hwinfo_t *smchw = (ctsmc_hwinfo_t *)ctsmc->ctsmc_hw;
	ctsmc_rsppkt_t msg;
	uint8_t csr = ctsmc_status(smchw);
	int loopmax = LOOPMAX;

	/*
	 * If there is a watchdog expiry pending, handle it first
	 */
	if (SC_WATCHDOG(csr)) {
		SMC_DEBUG0(SMC_WDOG_DEBUG, "Clearing pending watchdog event");
		(void) ctsmc_command(smchw, ctsmc_wd_cmd);
	}
	if (SC_PANICSYS(csr)) {
		(void) ctsmc_command(smchw, SMC_CLR_OEM2_COND);
		cmn_err(CE_PANIC, "User initiated panic!!");

		return;
	}

	/*
	 * Read incoming messages if waiting in SMC
	 */
	csr = ctsmc_status(smchw);
	if (SC_DATN(csr)) {
		SMC_DEBUG0(SMC_RECV_DEBUG, "Reading Pending messages");
		while ((ctsmc_read(ctsmc, &msg) == SMC_SUCCESS) &&
				loopmax--)
			;
	}

	/*
	 * Initialize interrupt block cookie
	 */
	if (ddi_get_iblock_cookie(dip, 0, &smchw->smh_ib) !=
			DDI_SUCCESS) {
		return;
	}
}

/*
 * Free HW-associated mappings.
 */
void
ctsmc_regs_unmap(void *smchw)
{
	ctsmc_hwinfo_t *m = (ctsmc_hwinfo_t *)smchw;

	if (m) {
		if (m->smh_handle) {
			ddi_regs_map_free(&m->smh_handle);
		}
		FREE(m, 1, ctsmc_hwinfo_t);
		m = NULL;
	}
}

/*
 * Install interrupt handlers
 */
int
ctsmc_intr_load(dev_info_t *dip, ctsmc_state_t *ctsmc,
		int (*hostintr)(ctsmc_state_t *))
{
	uint8_t csr;
	ctsmc_hwinfo_t	*m = (ctsmc_hwinfo_t *)ctsmc->ctsmc_hw;
	char *wdprop;

	if (ddi_intr_hilevel(dip, 0)) {
		cmn_err(CE_WARN, "ctsmc_intr_load: "
				"hi-level HOST interrupt not supported!");
		return (DDI_FAILURE);
	}

	m->smh_hwflag = 0;
	/*
	 * mutex for controlling access to SMC intr layer.
	 */
	mutex_init(&m->lock, NULL, MUTEX_DRIVER, (void *)m->smh_ib);

	/*
	 * Initialize Condvar
	 */
	cv_init(&m->smh_hwcv, NULL, CV_DRIVER, NULL);

	/*
	 * Check if "wd-l1-restart" property is "enabled", in this
	 * case driver will send WD_CLEAR to restart L1 watchdog
	 */
	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		"wd-l1-restart", &wdprop) == DDI_PROP_SUCCESS) {
		if (strcmp(wdprop, "enabled") == 0)
			ctsmc_wd_cmd = WD_CLEAR;
		ddi_prop_free(wdprop);
	}

	/*
	 * Check if an watchdog expiry event is pending and if so,
	 * handle it. OBP disables IPMI notification bit before
	 * booting solaris, so we need not worry about IPMI msgs.
	 */
	csr = ctsmc_status(m);
	if (SC_WATCHDOG(csr)) {
		SMC_DEBUG(SMC_INTR_DEBUG, "Watchdog expiry event.. "
			"csr = 0x%x", csr);

		(void) ctsmc_command(m, ctsmc_wd_cmd);
	}

	/*
	 * add the handler for the Host <-> SMC interrupt handler
	 */
	if (ddi_add_intr(dip, 0, &m->smh_ib, &m->smh_id,
			(intr_handler_t)hostintr, (caddr_t)ctsmc) !=
			DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * Remove interrupt and event handlers
 */
void
ctsmc_intr_unload(dev_info_t *dip, void *hwptr)
{
	ctsmc_hwinfo_t	*m = (ctsmc_hwinfo_t *)hwptr;

	ddi_remove_intr(dip, 0, m->smh_ib);
	cv_destroy(&m->smh_hwcv);
	mutex_destroy(&m->lock);
}

/* ==Operations for creating a message for sending to SMC== */

/*
 * calculate a checksum for this message and
 * store it in the header.
 */
static uint8_t
ctsmc_msg_cksum(uchar_t *packet, uint8_t len)
{
	uchar_t	cksum = 0;
	uint_t	i;

	for (i = 0; i < len; ++i) {
		cksum = cksum ^ packet[i];
	}

	return (cksum);
}

/*
 * initialize message header with no data.
 */
static void
ctsmc_msg_init(ctsmc_reqpkt_t *msg, uchar_t cmd, uchar_t seq,
		uchar_t netfun, uchar_t *datap, uchar_t dlength)
{
	int	i;

	SMC_MSG_CMD(msg) = cmd;
	SMC_MSG_SEQ(msg) = seq;
	SMC_MSG_NETFN(msg) = netfun;
	SMC_MSG_SUM(msg) = 0;
	SMC_MSG_LEN(msg) = dlength + SMC_SEND_HEADER;

	for (i = 0; i < dlength; ++i) {
		SMC_MSG_DATA(msg)[i] = *datap++;
	}
	SMC_MSG_SUM(msg) = ctsmc_msg_cksum((uchar_t *)msg, SMC_MSG_LEN(msg));
}

/*
 * construct a command message "atomically".
 */
static void
ctsmc_msg_command(ctsmc_reqpkt_t *msg, uchar_t cmd, uchar_t seq,
			uchar_t *data, uchar_t dlength)
{
	SMC_DEBUG4(SMC_REQMSG_DEBUG, "command(cmd %x sequence %x "
			"data %p dlength %x)", cmd, seq, data, dlength);

	ctsmc_msg_init(msg, cmd, seq, SMC_NETFUN_REQ, data, dlength);
}

/*
 * Copy the mblk chain into a buffer without discarding
 * the mblk.
 */
void
ctsmc_msg_copy(mblk_t *mp, uchar_t *bufp)
{
	mblk_t  *bp;
	size_t  n;

	for (bp = mp; bp; bp = bp->b_cont) {
		n = MBLKL(bp);
		bcopy(bp->b_rptr, bufp, n);
		bufp += n;
	}
}

/*
 * Given a response sequence number, wake up all threads waiting
 * on this sequence number. If fact, for seq# 'n', it will wake
 * up all threads waiting on seq x, where x % SMC_BLOCK_SZ equals
 * n % SMC_BLOCK_SZ. The requesting thread needs to check whether
 * it should go back to sleep in case it's not the right sequence
 * number.
 */
static void
ctsmc_wakeupRequestor(ctsmc_state_t *ctsmc, uint8_t seq)
{
	cv_broadcast(&ctsmc->ctsmc_seq_list->reqcond[seq%SMC_BLOCK_SZ]);
}

#ifdef DEBUG
/* ARGSUSED */
static void
msg_print(ctsmc_reqpkt_t *msg)
{
	int	dlength;

	SMC_DEBUG5(SMC_REQMSG_DEBUG, "LEN: x%x CSUM: x%x SEQ: x%x "
			"NETFUN: x%x OP: x%x",
			SMC_MSG_LEN(msg),
			SMC_MSG_SUM(msg),
			SMC_MSG_SEQ(msg),
			SMC_MSG_NETFN(msg),
			SMC_MSG_CMD(msg));
	dlength = SMC_SEND_DLENGTH(msg);
	if ((dlength > 0) && (dlength < SMC_PKT_MAX_SIZE)) {
		int	i;

		for (i = 0; i < dlength; ++i) {
			SMC_DEBUG(SMC_REQMSG_DEBUG, "%x ",
					SMC_MSG_DATA(msg)[i]);
		}
		SMC_DEBUG0(SMC_REQMSG_DEBUG, "------");
	}
}
#endif	/* DEBUG */

/* ****************** Response message Operations ********************* */

/*
 * initialize a response message header before
 * adding the data.
 */
static void
ctsmc_rmsg_init(sc_rspmsg_t *msg, uchar_t uSeq, uchar_t cmd,
		uchar_t cc)
{
	SMC_DEBUG4(SMC_RSPMSG_DEBUG, "init(msg %p, uSeq %x, cmd %x cc %x",
		msg, uSeq, cmd, cc);

	SC_MSG_ID(msg) = uSeq;
	SC_MSG_LEN(msg) = 0;
	SC_MSG_CMD(msg) = cmd;
	SC_MSG_CC(msg) = cc;
}

/*
 * copy data into the response message.
 */
static void
ctsmc_rmsg_data(sc_rspmsg_t *msg, uchar_t *datap, uchar_t dlength)
{
	int	i;

	SMC_DEBUG3(SMC_RSPMSG_DEBUG, "data(msg %p, datap %p, "
			"dlength x%x", msg, datap, dlength);

	/* Copy data */
	for (i = 0; i < dlength; ++i) {
		SMC_DEBUG2(SMC_RSPMSG_DEBUG, "data[%d] %x", i, *datap);
		SC_MSG_DATA(msg)[i] = *datap++;
	}

	/* Update the length field by the length provided */
	SC_MSG_LEN(msg) += dlength;
}

/*
 * Given a raw message received from SMC, convert into a sc_resmsg_t
 * format
 */
void
rmsg_convert(sc_rspmsg_t *ursp, ctsmc_rsppkt_t *rsp, uint8_t uSeq)
{
	ctsmc_rmsg_init(ursp, uSeq, SMC_MSG_CMD(rsp), SMC_MSG_CC(rsp));
	ctsmc_rmsg_data(ursp, SMC_MSG_DATA(rsp), SMC_RECV_DLENGTH(rsp));
}

/*
 * construct a response message based on the given
 * user request message.
 */
void
ctsmc_msg_response(sc_reqmsg_t *reqmsg, sc_rspmsg_t *rspmsg,
		uchar_t cc, uchar_t e)
{
	SMC_DEBUG4(SMC_RSPMSG_DEBUG, "reply(reqmsg %p rspmsg %p cc %x e %x)",
		reqmsg, rspmsg, cc, e);

	ctsmc_rmsg_init(rspmsg, SC_MSG_ID(reqmsg), SC_MSG_CMD(reqmsg), cc);
	ctsmc_rmsg_data(rspmsg, &e, sizeof (e));
}

/*
 * copy an entire response message into one mblk that
 * we allocate here for just this purpose.
 */
mblk_t *
ctsmc_rmsg_message(sc_rspmsg_t *msg)
{
	int	i, dlength = SC_RECV_DLENGTH(msg);
	mblk_t	*mp = allocb(SC_RECV_HEADER + dlength, BPRI_HI);

	if (mp == NULL) {
		cmn_err(CE_NOTE, "?ctsmc_rmsg.message: allocb failed");
		return (NULL);
	}

	mp->b_datap->db_type = M_DATA;

	SMC_DEBUG(SMC_RSPMSG_DEBUG, "message(msg=%p)", msg);
	*mp->b_wptr++ = SC_MSG_ID(msg);
	*mp->b_wptr++ = SC_MSG_CMD(msg);
	*mp->b_wptr++ = SC_MSG_LEN(msg);
	*mp->b_wptr++ = SC_MSG_CC(msg);

	for (i = 0; i < dlength; ++i) {
		*mp->b_wptr++ = SC_MSG_DATA(msg)[i];
	}

	return (mp);
}

#ifdef DEBUG
/*
 * send message header + data to the console for
 * debugging purposes.
 */
/* ARGSUSED */
static void
ctsmc_rmsg_print(ctsmc_rsppkt_t *msg)
{
	int	dlength;

	SMC_DEBUG6(SMC_RSPMSG_DEBUG, "LEN: x%x CSUM: x%x SEQ: x%x "
			"NETFUN: x%x CMD: x%x CC: x%x",
			SMC_MSG_LEN(msg),
			SMC_MSG_SUM(msg),
			SMC_MSG_SEQ(msg),
			SMC_MSG_NETFN(msg),
			SMC_MSG_CMD(msg),
			SMC_MSG_CC(msg));

	dlength = SMC_RECV_DLENGTH(msg);
	if ((dlength > 0) && (dlength < SMC_PKT_MAX_SIZE)) {
		int	i;

		for (i = 0; i < dlength; ++i) {
			SMC_DEBUG(SMC_RSPMSG_DEBUG, "x%x ",
					SMC_MSG_DATA(msg)[i]);
		}
		SMC_DEBUG0(SMC_RSPMSG_DEBUG, "------");
	}
}
#endif /* DEBUG */

/*
 * Wait for OBF to be set
 */
static int
ctsmc_wait_for_OBF(ctsmc_hwinfo_t *hwptr, int loopmax)
{
	uint8_t csr;
	/*
	 * Dummy read of data byte to clear OBF
	 */
	csr = ctsmc_getcsr(hwptr);
	if (SC_WATCHDOG(csr) || SC_PANICSYS(csr))
		return (SMC_SUCCESS);

	while ((!SC_DAVAIL(csr)) && loopmax--) {
		csr = ctsmc_status(hwptr);
	}

	if (SC_DAVAIL(csr))
		return (SMC_SUCCESS);
	else
		return (SMC_FAILURE);
}

/*
 * Wait for OBF to clear
 */
static int
ctsmc_clear_OBF(ctsmc_hwinfo_t *hwptr, int loopmax)
{
	uint8_t csr;
	/*
	 * Dummy read of data byte to clear OBF
	 */
	csr = ctsmc_getcsr(hwptr);
	while (SC_DAVAIL(csr) && loopmax--) {
		(void) ctsmc_get(hwptr);
		csr = ctsmc_getcsr(hwptr);
	}

	if (SC_DAVAIL(csr))
		return (SMC_FAILURE);

	return (SMC_SUCCESS);
}

/*
 * ctsmc_clearerr
 * An ABORT can be send only when SMC is NOT IN IDLE STATE.
 */
static int
ctsmc_clearerr(ctsmc_state_t *ctsmc, int loopmax)
{
	int e = SMC_SUCCESS;
	uint8_t csr;
	int ret, count = loopmax;
	ctsmc_hwinfo_t *hwptr = ctsmc->ctsmc_hw;

	ctsmc_clearerr_cnt++;
	csr = ctsmc_status(hwptr);
	while (!SC_IDLE(csr) && loopmax--) {
		/*
		 * Wait for IBF to clear before issuing ABORT
		 */
		(void) ctsmc_data_not_pending(hwptr, LOOPMAX);

		/* Issue abort */
		e = ctsmc_command(hwptr, (uchar_t)ABORT);
		if (e != SMC_SUCCESS)
			continue;

		/*
		 * Wait until IBF is clear after the
		 * command register is written
		 */
		ret = ctsmc_data_not_pending(hwptr, LOOPMAX);
		if (ret != SMC_SUCCESS)
			continue;

		/*
		 * Now SMC should be in READ state with ATN set
		 * If OBF is set, read data, wait for OBF to clear
		 */
		ret = ctsmc_clear_OBF(hwptr, LOOPMAX);
		if (ret != SMC_SUCCESS)
			continue;

		/*
		 * Read SMC status and if it's not IDLE, continue
		 */
		csr = ctsmc_status(hwptr);
		while ((!SC_IDLE(csr) ||
				SC_DATN(csr)) && (count-- > 0)) {
			csr = ctsmc_status(hwptr);
		}
		if (!SC_DATN(csr) && SC_IDLE(csr))
			return (SMC_SUCCESS);
	}

	/*
	 * if SMC in a weird state where ATN/OEM1/OEM2 is on,
	 * no intr and it's IDLE, ABORT.
	 */
	if (!(ctsmc->ctsmc_init & SMC_IN_INTR) && SC_IDLE(csr)) {
		if (SC_DATN(csr)) {
			/* Issue abort */
			e = ctsmc_command(hwptr, (uchar_t)ABORT);
			if (e != SMC_SUCCESS)
				return (SMC_FAILURE);

			/*
			 * Now SMC should be in READ state with ATN set
			 * Read OBF and wait for it to clear
			 */
			ret = ctsmc_clear_OBF(hwptr, loopmax);
			if (ret != SMC_SUCCESS)
				return (ret);

			/*
			 * Read SMC status and if it's not IDLE, continue
			 */
			csr = ctsmc_status(hwptr);
			if (!SC_DATN(csr) && SC_IDLE(csr))
				return (SMC_SUCCESS);
		} else
		if (SC_WATCHDOG(csr)) {
			sc_rspmsg_t rsp;

			e = ctsmc_command(hwptr, ctsmc_wd_cmd);

			ctsmc_rmsg_init(&rsp, 0, SMC_EXPIRED_WATCHDOG_NOTIF, 0);

			/*
			 * enque message in front of upstream queue.
			 */
			ctsmc_async_putq(ctsmc, &rsp);

			csr = ctsmc_status(hwptr);
			if (SC_IDLE(csr))
				return (SMC_SUCCESS);
		} else
		if (SC_PANICSYS(csr)) {
			e = ctsmc_command(hwptr, SMC_CLR_OEM2_COND);
			cmn_err(CE_PANIC, "User initiated panic!!");

			return (SMC_SUCCESS);
		}
	}

	return (SMC_FAILURE);
}

/*
 * Written using IPMI spec & Ebus state machices. This function
 * should be invoked with lock held.
 */
#define	SMC_STATE_NORMAL	0
#define	SMC_STATE_SERVICE	1
#define	SMC_KCS_DATA(CSR)	(SC_DATN(CSR) || SC_DAVAIL(CSR))
#define	SMC_NEEDS_SERVICE(SMC, CSR)	\
		(((SMC)->ctsmc_init & SMC_IN_INTR) ||	SMC_KCS_DATA(CSR) || \
			SC_WATCHDOG(CSR) ||	SC_PANICSYS(CSR))
#define	SMC_PRI_SERV(SMC, CSR)					\
		(((SMC)->ctsmc_init & SMC_IN_INTR) &&		\
			(SC_WATCHDOG(CSR) ||	SC_PANICSYS(CSR)))
#define	SMC_WR_REC_KCS_REG(CSR)	\
	ctsmc_wr_kcs_reg[(ctsmc_wr_kcs_reg_index++)%SMC_MAX_KCS_REC] = \
		(CSR)

static void
ctsmc_service_kcs(ctsmc_state_t *ctsmc, int *var1, int *var2)
{
	ctsmc_hwinfo_t *m = ctsmc->ctsmc_hw;
	uchar_t		csr;
	int ctsmc_in_intr = 0, loopmax = LOOPMAX;

	csr = ctsmc_getcsr(m);
	while (SMC_NEEDS_SERVICE(ctsmc, csr) &&
		loopmax--) {
		(*var1)++;
		(void) ctsmc_wait_for_OBF(m, LOOPMAX);
		ctsmc_in_intr = ctsmc->ctsmc_init &
			SMC_IN_INTR;
		if (ctsmc_in_intr) {
			UNLOCK_DATA(m);
			delay(1);
			LOCK_DATA(m);
		} else {
			(*var2)++;
			SMC_WR_REC_KCS_REG(csr);
			(void) ctsmc_pkt_readall(ctsmc);
		}
		csr = ctsmc_getcsr(m);
	}
}

static int
ctsmc_write(ctsmc_state_t *ctsmc, uchar_t *buf_start, uint_t pktlength)
{
	ctsmc_hwinfo_t *m = ctsmc->ctsmc_hw;
	uchar_t		csr;
	uchar_t		e = SMC_SUCCESS;

	uint8_t	stateMc = SMC_TRANSFER_INIT;
	uint8_t retryCount = 0;
	int i = 0;

	/*
	 * Make sure there is only one writer allowed
	 */
	while (m->smh_hwflag & SMC_IN_WRITE)
		cv_wait(&m->smh_hwcv, &m->lock);
	m->smh_hwflag |= SMC_IN_WRITE;
	m->smh_hwflag &= ~SMC_READ_NO_ATN;

	/*CONSTANTCONDITION*/
	while (1) {
		if (stateMc == SMC_MACHINE_END)
			break;
		else
		if (retryCount > SMC_MAX_RETRY_CNT) {
			ctsmc_num_write_errors++;
			ctsmc_num_write_init_errors++;

			m->smh_hwflag &= ~SMC_READ_NO_ATN;
			e = SMC_FAILURE;
			break;
		}

		switch (stateMc) {
			case SMC_TRANSFER_INIT:
				{
					i = 0;
					stateMc = SMC_TRANSFER_START;

					/*
					 * Check if IBF is clear
					 */
					if (ctsmc_data_not_pending(m,
							LOOPMAX) !=
							SMC_SUCCESS) {
						stateMc = SMC_TRANSFER_ERROR;
						CTSMC_INCR_WRERR(1);
						break;
					}

					/*
					 * Fall through to SMC_TRANSFER_START
					 */
					/*FALLTHROUGH*/
				}

			case SMC_TRANSFER_START:
				{
					stateMc = SMC_TRANSFER_NEXT;

					/*
					 * Before WRITE_START, do a quick check
					 * whether SMC interrupt is active, or
					 * if ATN bit is set. Then release the
					 * mutex and give a chance to the
					 * interrupt handler to finish reading.
					 * We are still holding the condvar so
					 * that no other writer can enter.
					 */
					ctsmc_service_kcs(ctsmc,
						ctsmc_wr_cond + 1,
						&ctsmc_no_intr1);

					/*
					 * If interrupt condition still exists,
					 * start from beginning
					 */
					csr = ctsmc_getcsr(m);
					if (SMC_NEEDS_SERVICE(ctsmc, csr)) {
						stateMc = SMC_TRANSFER_ERROR;
						CTSMC_INCR_WRERR(2);
						retryCount++;
						break;
					}

					/*
					 * Write WRITE_START in command register
					 * to start write mode and check for IBF
					 * clear
					 */
					e = ctsmc_command(m, WRITE_START);

					if (e != SMC_SUCCESS) {
						CTSMC_INCR_WRERR(3);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}

					/*
					 * Do a quick check whether there is a
					 * service request from SMC
					 */
					csr = ctsmc_getcsr(m);
					if (!SC_WRITE(csr)) {
						ctsmc_service_kcs(ctsmc,
							ctsmc_wr_cond + 4,
							&ctsmc_no_intr2);
					}

					/*
					 * Ensure SMC is indeed in write mode
					 */
					e = ctsmc_writeable(m, LOOPMAX);
					if (e != SMC_SUCCESS) {
						CTSMC_INCR_WRERR(4);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}
					/*
					 * Fall through to SMC_TRANSFER_NEXT
					 */
					/*FALLTHROUGH*/
				}

			case SMC_TRANSFER_NEXT:
				{
					/*
					 * Check again if ATN bit is set
					 * for some reason; In case ATN is
					 * set and SMC is in writeable mode,
					 * we must complete the write, but
					 * remember to go back to read once
					 * the write is over. However, if SMC
					 * is not in write mode, we can just
					 * go to error mode and re-start the
					 * write.
					 */
					csr = ctsmc_getcsr(m);
					if (SMC_NEEDS_SERVICE(ctsmc, csr)) {
						SMC_DEBUG(SMC_SEND_DEBUG,
							"COND ON after "
							"write mode:CSR ="
							" 0x%x", csr);
						CTSMC_INCR_WRCOND(2);
						m->smh_hwflag |=
							SMC_READ_NO_ATN;
						CTSMC_INCR_WRERR(5);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}

					/*
					 * Write a byte
					 */
					ctsmc_put(m, buf_start[i++]);

					/*
					 * Wait until IBF is clear
					 */
					if (ctsmc_data_not_pending(m,
							LOOPMAX) !=
							SMC_SUCCESS) {
						CTSMC_INCR_WRERR(6);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}

					/*
					 * Ensure SMC is still in write mode
					 */
					e = ctsmc_writeable(m, LOOPMAX);
					if (e != SMC_SUCCESS) {
						CTSMC_INCR_WRERR(7);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}
					/*
					 * Write WRITE_END in command register
					 * to notify SMC of last byte being
					 * written. This is not a true KCS
					 * interface, and this step is being
					 * performed after finishing write.
					 * e = ctsmc_command(m, WRITE_END);
					 */

					if (i == pktlength)
						stateMc = SMC_TRANSFER_END;
				}
				break;

			case SMC_TRANSFER_END:
				{

					csr = ctsmc_getcsr(m);
					if (SMC_PRI_SERV(ctsmc, csr)) {
						CTSMC_INCR_WRCOND(3);
						CTSMC_INCR_WRERR(8);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}
					/*
					 * NOW send WRITE_END command, notifying
					 * SMC that last byte has been written
					 */
					e = ctsmc_command(m, WRITE_END);
					if (e != SMC_SUCCESS) {
						CTSMC_INCR_WRERR(9);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}

					/*
					 * Wait until SMC is in IDLE state. In
					 * true KCS case, SMC will be in READ
					 * state
					 */
					e = ctsmc_idle(m, LOOPMAX);
					if (e != SMC_SUCCESS) {
						CTSMC_INCR_WRERR(10);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}
					/*
					 * Mark this seq#/entry NOT BUSY
					 */
					e = ctsmc_init_seq_counter(ctsmc,
						(ctsmc_reqpkt_t *)buf_start);

					stateMc = SMC_MACHINE_END;
				}
				break;

			case SMC_TRANSFER_ERROR:
				{
					delay(1); /* Wait for 1 tick */
					csr = ctsmc_getcsr(m);
					if (SC_WRITE(csr))
						(void) ctsmc_clearerr(ctsmc,
								LOOPMAX);
					else
						ctsmc_service_kcs(ctsmc,
							ctsmc_wr_cond + 5,
							&ctsmc_no_intr2);
					stateMc = SMC_TRANSFER_INIT;
					if (retryCount++ > SMC_MAX_RETRY_CNT) {
						e = SMC_FAILURE;
						stateMc = SMC_MACHINE_END;
					}
				}
				break;
		}	/* switch */
	}	/* while */

	m->smh_hwflag &= ~SMC_IN_WRITE;
	cv_broadcast(&m->smh_hwcv);

	return (e);
}

/*
 * This is a non blocking version of write routine which may
 * be called from a very high level interrupt context. It
 * does not wait. If interrupted by a pending read, it will
 * abort the current write, finish reading all pending packets
 * and then resume
 */
static int
ctsmc_write_noblock(ctsmc_state_t *ctsmc, uchar_t *buf_start, uint_t pktlength)
{
	ctsmc_hwinfo_t *m = ctsmc->ctsmc_hw;
	uchar_t		csr;
	uchar_t		e = SMC_SUCCESS;

	uint8_t	stateMc = SMC_TRANSFER_INIT;
	uint8_t retryCount = 0;
	int i = 0;
	int loopmax = LOOPMAX;

	/*CONSTANTCONDITION*/
	while (1) {
		if (stateMc == SMC_MACHINE_END)
			break;
		else
		if (retryCount > SMC_MAX_RETRY_CNT) {
			ctsmc_num_write_errors++;
			ctsmc_num_write_init_errors++;

			e = SMC_FAILURE;
			break;
		}

		switch (stateMc) {
			case SMC_TRANSFER_INIT:
				{
					i = 0;
					stateMc = SMC_TRANSFER_START;

					/*
					 * Check if IBF is clear
					 */
					if (ctsmc_data_not_pending(m,
							LOOPMAX) !=
							SMC_SUCCESS) {
						stateMc = SMC_TRANSFER_ERROR;
						CTSMC_INCR_WRERR(1);
						break;
					}

					/*
					 * Fall through to SMC_TRANSFER_START
					 */
					/*FALLTHROUGH*/
				}

			case SMC_TRANSFER_START:
				{
					stateMc = SMC_TRANSFER_NEXT;

					/*
					 * Before WRITE_START, do a quick check
					 * if ATN/OEM/OBF bit is set. Then read
					 * all pending packets.
					 */
					csr = ctsmc_getcsr(m);
					while (SMC_NEEDS_SERVICE(ctsmc, csr) &&
							loopmax--) {
						CTSMC_INCR_WRCOND(1);
						/*
						 * Finish processing pending
						 * packets
						 */
						(void) ctsmc_pkt_readall(ctsmc);
						csr = ctsmc_getcsr(m);
					}
					loopmax = LOOPMAX;

					/*
					 * If interrupt condition still exists,
					 * start from beginning
					 */
					if (SMC_NEEDS_SERVICE(ctsmc, csr)) {
						stateMc = SMC_TRANSFER_ERROR;
						CTSMC_INCR_WRERR(2);
						retryCount++;
						break;
					}

					/*
					 * Write WRITE_START in command register
					 * to start write mode and check for IBF
					 * clear
					 */
					e = ctsmc_command(m, WRITE_START);

					if (e != SMC_SUCCESS) {
						CTSMC_INCR_WRERR(3);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}

					/*
					 * Ensure SMC is indeed in write mode
					 */
					e = ctsmc_writeable(m, LOOPMAX);
					if (e != SMC_SUCCESS) {
						CTSMC_INCR_WRERR(4);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}
					/*
					 * Fall through to SMC_TRANSFER_NEXT
					 */
					/*FALLTHROUGH*/
				}

			case SMC_TRANSFER_NEXT:
				{
					/*
					 * Check again if ATN bit is set
					 * for some reason; In case ATN is
					 * set and SMC is in writeable mode,
					 * we must complete the write, but
					 * remember to go back to read once
					 * the write is over. However, if SMC
					 * is not in write mode, we can just
					 * go to error mode and re-start the
					 * write.
					 */
					csr = ctsmc_getcsr(m);
					if (SMC_NEEDS_SERVICE(ctsmc, csr)) {
						SMC_DEBUG(SMC_SEND_DEBUG,
							"COND ON after write"
							" mode:CSR = "
							"0x%x", csr);
						CTSMC_INCR_WRCOND(2);
						CTSMC_INCR_WRERR(5);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}

					/*
					 * Write a byte
					 */
					ctsmc_put(m, buf_start[i++]);

					/*
					 * Wait until IBF is clear
					 */
					if (ctsmc_data_not_pending(m,
							LOOPMAX) !=
							SMC_SUCCESS) {
						CTSMC_INCR_WRERR(6);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}

					/*
					 * Ensure SMC is still in write mode
					 */
					e = ctsmc_writeable(m, LOOPMAX);
					if (e != SMC_SUCCESS) {
						CTSMC_INCR_WRERR(7);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}
					/*
					 * Write WRITE_END in command register
					 * to notify SMC of last byte being
					 * written. This is not a true KCS
					 * interface, and this step is being
					 * performed after finishing write.
					 * e = ctsmc_command(m, WRITE_END);
					 */

					if (i == pktlength)
						stateMc = SMC_TRANSFER_END;
				}
				break;

			case SMC_TRANSFER_END:
				{

					csr = ctsmc_getcsr(m);
					if (SMC_PRI_SERV(ctsmc, csr)) {
						CTSMC_INCR_WRCOND(3);
						CTSMC_INCR_WRERR(8);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}
					/*
					 * NOW send WRITE_END command, notifying
					 * SMC that last byte has been written
					 */
					e = ctsmc_command(m, WRITE_END);
					if (e != SMC_SUCCESS) {
						CTSMC_INCR_WRERR(9);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}

					/*
					 * Wait until SMC is in IDLE state. In
					 * true KCS case, SMC will be in READ
					 * state
					 */
					e = ctsmc_idle(m, LOOPMAX);
					if (e != SMC_SUCCESS) {
						CTSMC_INCR_WRERR(10);
						stateMc = SMC_TRANSFER_ERROR;
						break;
					}
					/*
					 * Mark this seq#/entry NOT BUSY
					 */
					e = ctsmc_init_seq_counter(ctsmc,
						(ctsmc_reqpkt_t *)buf_start);

					stateMc = SMC_MACHINE_END;
				}
				break;

			case SMC_TRANSFER_ERROR:
				{
					csr = ctsmc_getcsr(m);
					if (SMC_NEEDS_SERVICE(ctsmc, csr) &&
						SC_WRITE(csr)) {
						(void) ctsmc_clearerr(ctsmc,
								LOOPMAX);
					}
					(void) ctsmc_pkt_readall(ctsmc);

					stateMc = SMC_TRANSFER_INIT;
					if (retryCount++ > SMC_MAX_RETRY_CNT) {
						e = SMC_FAILURE;
						stateMc = SMC_MACHINE_END;
					}
				}
				break;
		}	/* switch */
	}	/* while */

	return (e);
}

/*
 * Written using IPMI spec & Ebus state machices. This function
 * should be invoked with lock held.
 */
#define	SMC_READ_RETRIES	5
#define	SMC_RD_REC_KCS_REG(CSR)	\
	ctsmc_rd_kcs_reg[(ctsmc_rd_kcs_reg_index++)%SMC_MAX_KCS_REC] = (CSR)
static int
ctsmc_read(ctsmc_state_t *ctsmc, ctsmc_rsppkt_t *msg)
{
	ctsmc_hwinfo_t *m = ctsmc->ctsmc_hw;
	uchar_t		csr;
	uchar_t		e = SMC_SUCCESS;
	uchar_t *buf = (uchar_t *)msg;

	uint8_t	stateMc = SMC_RECEIVE_INIT;
	uint8_t retryCount = 0;
	int i = 0, j = 0, loopmax = LOOPMAX;


	bzero(msg, SMC_RECV_HEADER);
	/*CONSTANTCONDITION*/
	while (1) {
		if (stateMc == SMC_MACHINE_END)
			break;
		else
		if (retryCount > SMC_MAX_RETRY_CNT) {
			e = SMC_FAILURE;
			break;
		}

		switch (stateMc) {
			case SMC_RECEIVE_INIT:
				{
					/*
					 * Before we do anything, check status
					 * register to check which state we are
					 * in. We should be in IDLE state with
					 * ATN on; and we will go to READ state
					 * after issuing READ_START command.
					 */
					csr = ctsmc_status(m);
					while ((!SC_IDLE(csr) ||
						SC_DPEND(csr)) && loopmax--) {
						if (SC_ERROR(csr)) {
							ctsmc_read_kcs_error++;
							stateMc =
							    SMC_RECEIVE_ERROR;
							break;
						}
						csr = ctsmc_status(m);
					}
					if (SC_IDLE(csr) && !SC_DPEND(csr))
						stateMc = SMC_RECEIVE_START;
					else {
						if (SC_IDLE(csr))
							CTSMC_INCR_RDERR(1);
						else {
							cmn_err(CE_NOTE,
								"ctsmc_read: "
								"Status = %x",
								csr);
							CTSMC_INCR_RDERR(2);
						}
						stateMc = SMC_RECEIVE_ERROR;
					}
				}
				break;

			case SMC_RECEIVE_START:
				{
					stateMc = SMC_RECEIVE_NEXT;

					/*
					 * Check if OBF is clear, this will also
					 * read the reason code but ignore it.
					 * (void ) ctsmc_intr_reason(m);
					 */
					if (ctsmc_clear_OBF(m, LOOPMAX) !=
							SMC_SUCCESS) {
						CTSMC_INCR_RDERR(3);
						stateMc = SMC_RECEIVE_ERROR;
						break;
					}

					/*
					 * Check if IBF is clear, before issuing
					 * READ command
					 */
					if (ctsmc_data_not_pending(m,
							LOOPMAX) !=
							SMC_SUCCESS) {
						CTSMC_INCR_RDERR(4);
						stateMc = SMC_RECEIVE_ERROR;
						break;
					}

					/*
					 * Write READ command to instruct SMC
					 * that read is going to commence
					 */
					e = ctsmc_command(m, READ_START);
					if (e != SMC_SUCCESS) {
						stateMc = SMC_RECEIVE_ERROR;
						CTSMC_INCR_RDERR(5);
						break;
					}

					/* Wait until SMC enters read mode. */
					e = ctsmc_readable(m, LOOPMAX);
					if (e != SMC_SUCCESS) {
						CTSMC_INCR_RDERR(6);
						csr = ctsmc_getcsr(m);
						SMC_RD_REC_KCS_REG(csr);
						stateMc = (j++ <
							SMC_READ_RETRIES) ?
							(SMC_KCS_DATA(csr) ?
							SMC_RECEIVE_START :
							SMC_RECEIVE_INIT) :
							SMC_RECEIVE_ERROR;
						if (j == SMC_READ_RETRIES)
							CTSMC_INCR_RDERR(7);
						if (!SMC_KCS_DATA(csr))
							CTSMC_INCR_RDERR(8);
						break;
					}

					/*
					 * Fall through to SMC_RECEIVE_NEXT
					 */
					/*FALLTHROUGH*/
				}

			case SMC_RECEIVE_NEXT:
				{
					/*
					 * SMC should now be in read mode.
					 * Make sure the OBF bit is
					 * ON before attempting to read
					 */
					loopmax = LOOPMAX;
					csr = ctsmc_status(m);
					while (!(SC_READ(csr) &&
							SC_DAVAIL(csr)) &&
							loopmax--) {
						csr = ctsmc_status(m);
					}

					if (SC_READ(csr) &&
						SC_DAVAIL(csr)) {
						buf[i++] = ctsmc_get(m);
					} else {
						CTSMC_INCR_RDERR(9);
						stateMc = SMC_RECEIVE_ERROR;
					}

					/*
					 * buf[0] contains the length, check
					 * if we read the last byte
					 */
					if ((stateMc == SMC_RECEIVE_NEXT) &&
							(i == buf[0])) {
#ifdef	DEBUG
						if (ctsmc_idle(m, LOOPMAX) !=
							SMC_SUCCESS) {
							SMC_DEBUG0(
								SMC_HWERR_DEBUG,
								"READ END NOT "
								"IDLE");
						}
#endif
						stateMc = SMC_RECEIVE_END;
					}
				}
				break;

			case SMC_RECEIVE_END:
				{
#ifdef	DEBUG
					uint8_t calsum;
					uint8_t msgsum = SMC_MSG_SUM(msg);
					ctsmc_rmsg_print(msg);

					/* Checksum validation */
					SMC_MSG_SUM(msg) = 0;
					calsum = ctsmc_msg_cksum((uchar_t *)msg,
							SMC_MSG_LEN(msg));
					SMC_MSG_SUM(msg) = msgsum;

					if (msgsum && (msgsum != calsum)) {
						SMC_DEBUG2(SMC_RSPMSG_DEBUG,
								"Invalid "
								"checksum "
								"Computed %x, "
								"Received %x",
								calsum, msgsum);
					}
#endif
					stateMc = SMC_MACHINE_END;
				}
				break;

			case SMC_RECEIVE_ERROR:
				{
					/*
					 * There is an error. ABORT
					 */
					loopmax = LOOPMAX;
					CTSMC_INCR_RDERR(10);
					csr = ctsmc_getcsr(m);
					/*
					 * If no data pending, may be
					 * a false read
					 */
					while (SMC_KCS_DATA(csr) &&
							loopmax--) {
						/* flushing the buffer */
						CTSMC_INCR_RDERR(11);
						(void) ctsmc_get(m);
						csr = ctsmc_getcsr(m);
					}
					(void) ctsmc_clearerr(ctsmc, LOOPMAX);
					e = SMC_FAILURE;
					stateMc = SMC_MACHINE_END;
				}
				break;
		}	/* switch */
	}	/* while */

	return (e);
}

/*
 * Routine to handle an watchdog expiry/OEM2 condition. This
 * is not an interrupt handler, but gets invoked from the
 * interrupt handler.
 */
static int
ctsmc_watchdog(void *hwptr)
{
	ctsmc_hwinfo_t	*m = (ctsmc_hwinfo_t *)hwptr;
	uchar_t		csr;
	int		e = DDI_INTR_UNCLAIMED;
#if SMC_WATCHDOG_DEBUG
	int i;
#endif

	SMC_DEBUG0(SMC_WDOG_DEBUG, "ctsmc_watchdog");

	csr = ctsmc_status(m);

	/*
	 * If the OEM2 bit is set, initiate a panic
	 */
	if (SC_PANICSYS(csr)) {
		e = ctsmc_command(m, SMC_CLR_OEM2_COND); /* To clear OEM2 bit */
		cmn_err(CE_PANIC, "User initiated panic!!");
		return (DDI_INTR_CLAIMED);
	}
	/*
	 * If the WATCHDOG (OEM1) bit is set in the CSR,
	 * there is a pending watchdog interrupt to be
	 * serviced.
	 */
	if (SC_WATCHDOG(csr)) {
		SMC_DEBUG(SMC_WDOG_DEBUG, "watchdog: OEM1 set: csr = %x", csr);

		/*
		 * There is a zero in the data register to tell
		 * the SPARC host that this is a WD interrupt.
		 * Clear out the data register by reading it.
		 */
		(void) ctsmc_get(m);

		/*
		 * Clear Watchdog command: The SMC will
		 * clear the OEM1/WD bit and restart the
		 * watchdog timer.
		 */
		(void) ctsmc_command(m, ctsmc_wd_cmd);

		SMC_DEBUG0(SMC_WDOG_DEBUG, "watchdog: cleared");

		e = DDI_INTR_CLAIMED;
		SMC_DEBUG0(SMC_WDOG_DEBUG, "Handled watchdog expiry");
#if SMC_WATCHDOG_DEBUG
		/*
		 * Dump the statistics when watchdog expires
		 */
		cmn_err(CE_NOTE, "Watchdog Expired: Index = %d "
			"wd_expiry_time = %ld, last_wd_rsp = %ld, "
			"last_wd_rsp_seq = %d", ctsmc_last_idx,
			gethrtime(), ctsmc_wdog_rsp_end, ctsmc_wdog_rsp_seq);
		for (i = 0; i < SMC_NUM_STAT; i++) {
			cmn_err(CE_NOTE, "WR_TIME = %ld, RD_TIME = %ld,"
				"ctsmc_seq = %d, app_seq = %d, "
				"WR_TO_RD_TIME = %ld, wr_end = %ld, "
				"last_intr_start = %ld, prev_rd_end = %ld",
				ctsmc_stat[i].ctsmc_time_wr_end -
				ctsmc_stat[i].ctsmc_time_wr_start,
				ctsmc_stat[i].ctsmc_time_rd_end -
				ctsmc_stat[i].ctsmc_time_rd_start,
				ctsmc_stat[i].ctsmc_seq,
				ctsmc_stat[i].app_seq,
				ctsmc_stat[i].ctsmc_time_rd_end -
				ctsmc_stat[i].ctsmc_time_wr_end,
				ctsmc_stat[i].ctsmc_time_wr_end,
				ctsmc_stat[i].ctsmc_time_rd_start,
				ctsmc_stat[i].ctsmc_time_rd_end);
		}
#endif
	}

	return (e);
}

/*
 * This routine will be used to re-activate ENUM# interrupt
 * after an ENUM# is generated and SMC automatically
 * disables it
 */
#define	SMC_GE_ENUM_NOTIF	0x0040
uchar_t
ctsmc_nct_hs_activate(ctsmc_state_t *ctsmc)
{
	int	ret;
	ushort_t	ge_mask = 0;
	SMC_DEBUG(SMC_MCT_DEBUG, "ctsmc_nct_hs_activate(ctsmc %p", ctsmc);

	if (ctsmc_command_send(ctsmc, SMC_GET_GLOBAL_ENABLES,
			NULL, 0, (uchar_t *)&ge_mask, NULL) != SMC_SUCCESS)
		return (SMC_FAILURE);

	/*
	 * Set the ENUM interrupt enable so we can start
	 * receiving ENUM messages.
	 */
	ge_mask |= SMC_GE_ENUM_NOTIF;
	ret = ctsmc_command_send(ctsmc, SMC_SET_GLOBAL_ENABLES,
			(uchar_t *)&ge_mask, sizeof (ge_mask), NULL, 0);

	SMC_DEBUG(SMC_MCT_DEBUG, "Enabled ENUM# interrupt on SMC: "
			"mask = 0x%x", (uint_t)ge_mask);

	return (ret);
}

/*
 * Process the ENUM# interrupt for MC/T platforms
 */
static void
ctsmc_enum_process(caddr_t arg)
{
	int e;
	ctsmc_state_t *ctsmc = (ctsmc_state_t *)arg;
	enum_intr_desc.enum_handler(enum_intr_desc.intr_arg);
	if ((e = ctsmc_nct_hs_activate(ctsmc)) != SMC_SUCCESS) {
		cmn_err(CE_NOTE,
			"ctsmc_nct_hs_activate failed, err %d: "
			"HS not available.", e);
	}
}

/*
 * We have a packet and it MAY be an event.
 * We snoop the packet OPCODE field and if
 * it is an event, we start processing it right
 * here.
 */
static int
ctsmc_pkt_events(ctsmc_state_t *ctsmc, ctsmc_rsppkt_t *msg)
{
	sc_rspmsg_t ursp;
	uchar_t		cmd, cc;
	int		e = DDI_INTR_UNCLAIMED;

	SMC_DEBUG0(SMC_ASYNC_DEBUG, "events");

	cmd = SMC_MSG_CMD(msg);
	cc = SMC_MSG_CC(msg);
	/*
	 * Async messages with non-zero completion code
	 * are of no use, so we drop them right away, but
	 * claim the interrupt
	 */
	if (cc && (SMC_MSG_CMD(msg) == SMC_IPMI_RESPONSE_NOTIF)) {
		cmn_err(CE_NOTE, "Received Bad IPMI message, Cmd = 0x%x, "
				"CC = 0x%x: %s", cmd, cc, SMC_DECODE_CC(cc));
		SMC_INCR_CNT(CTSMC_BAD_IPMI);
		return (DDI_INTR_CLAIMED);
	}
	rmsg_convert(&ursp, msg, 0);

	switch (cmd) {
	case SMC_SMC_LOCAL_EVENT_NOTIF:
	case SMC_IPMI_RESPONSE_NOTIF:
		SMC_INCR_CNT(CTSMC_IPMI_NOTIF);
		SMC_DEBUG(SMC_ASYNC_DEBUG, "Received IPMI "
				"response/Local event: cmd = %x", cmd);
		e = DDI_INTR_CLAIMED;
#ifdef DEBUG
		{
			uchar_t *data;
			uint8_t cmd, iseq;

			data = SC_MSG_DATA(&ursp);
			cmd = data[IPMB_OFF_CMD];
			iseq = IPMB_GET_SEQ(data[IPMB_OFF_SEQ]);

			SMC_DEBUG3(SMC_IPMI_DEBUG, "SMC_PKT_EVENTS: "
				"IPMI SEQ = %d, CMD = 0x%x, TIMESTAMP = %ld",
				iseq, cmd, gethrtime());
		}
#endif
		break;

	case SMC_ENUM_NOTIF:
		SMC_INCR_CNT(CTSMC_ENUM_NOTIF);
		SMC_DEBUG0(SMC_ASYNC_DEBUG, "Received ENUM# notification");
		if (ctsmc_mct_process_enum)
			(void) timeout((void (*)(void *))ctsmc_enum_process,
				(void *)ctsmc, drv_usectohz(1000));
		e = DDI_INTR_CLAIMED;

		break;

	default:
		SMC_DEBUG(SMC_ASYNC_DEBUG, "events: non-event cmd: x%x", cmd);
		e = DDI_INTR_UNCLAIMED;
		break;
	}

	/*
	 * Signal timeout to process this message
	 */
	if (e == DDI_INTR_CLAIMED) {
		SMC_INCR_CNT(CTSMC_ASYNC_RECV);
		ctsmc_async_putq(ctsmc, &ursp);
	}

	return (e);
}

/*
 * The ctsmc_pkt_receive function has already filtered the packet.
 * If ctsmc_pkt_process is called, we can be sure that we have
 * received a normal packet in response to one of our
 * requests.
 *
 * Determine what to do with it:
 *	1) Normal message: send it up stream
 *	2) Internal SMC message (e.g., generated by IOCTL):
 *	   awaken consumer
 */
static uint_t
ctsmc_pkt_process(ctsmc_state_t *ctsmc, ctsmc_rsppkt_t *msg)
{
	int count = 0;
	ctsmc_minor_t	*mnode_p;
	int		e;
	uint8_t uSeq, pos, minor, buf_index;
	uint8_t seq = SMC_MSG_SEQ(msg), cmd = SMC_MSG_CMD(msg);
#ifdef	DEBUG
	uint8_t cc = SMC_MSG_CC(msg), len = SMC_MSG_LEN(msg);
#endif
#if SMC_WATCHDOG_DEBUG
	int		j = 0, found = 0;
#endif

	SMC_DEBUG0(SMC_RECV_DEBUG, "process");

	/*
	 * The message is a response to a request originating
	 * on the SPARC host. Look for a matching request.
	 */
	e = ctsmc_findSeq(ctsmc, seq, cmd, &minor, &uSeq, &pos);
	if (e != SMC_SUCCESS) {
		ctsmc_cnt_noseq++;
		SMC_DEBUG4(SMC_RECV_DEBUG, "?process: can't find matching \
			request: cmd 0x%x, seq %d, len %d, CC 0x%x",
			cmd, seq, len, cc);
	}

	/*
	 * Decrement the number of pending requests
	 */
	if (ctsmc->ctsmc_hw->smh_smc_num_pend_req)
		ctsmc->ctsmc_hw->smh_smc_num_pend_req--;

	if (e != SMC_SUCCESS) {
		SMC_INCR_CNT(CTSMC_RSP_UNCLAIMED);
		return (DDI_INTR_CLAIMED);
	}

	mnode_p = GETMINOR(ctsmc, minor);
	GET_BUF_INDEX(ctsmc, seq, pos, buf_index, count);

	/*
	 * Is this a response to an internal SPARC-to-SMC
	 * message? We use sequence number 0 exclusively for
	 * SMC driver internal messages, all other request
	 * messages must have originated upstream; so we just
	 * send response message upstream;
	 */
	if (seq == 0 || buf_index != 0) {
		sc_rspmsg_t *rsp = FIND_BUF(ctsmc, buf_index);
		/*
		 * This is an synchronous request (SCIOC_SEND_SYNC_CMD)
		 * that expects a synchronous reply. All we need to do
		 * is signal the waiting function.
		 */
		SMC_INCR_CNT(CTSMC_SYNC_RSP);

		/*
		 * Copy message to appropriate buffer pointer, making
		 * sure the buffer is still valid. It's possible that
		 * the requesting thread timed out and freed the
		 * necessary request, in which case we have nothing to
		 * do.
		 */
		if (rsp != NULL)
			rmsg_convert(rsp, msg, uSeq);

	} else {
		/*
		 * This is the "normal" case: the original
		 * request message originated somewhere upstream.
		 * Similarly, we send our reply message upstream.
		 */

		SMC_INCR_CNT(CTSMC_REGULAR_RSP);
		SMC_DEBUG0(SMC_RECV_DEBUG, "process: normal packet");

#if SMC_WATCHDOG_DEBUG
		/*
		 * Collect statistics for cmd 0x22
		 */
		if (cmd == SMC_RESET_WATCHDOG_TIMER) {
			for (j = 0; j < SMC_NUM_STAT; j++)
				if (seq == ctsmc_stat[j].ctsmc_seq) {
					found = 1;
					break;
				}

			if (found) {
				ctsmc_stat[j].ctsmc_time_rd_start =
					ctsmc_intr_start;
				ctsmc_stat[j].ctsmc_time_rd_end = gethrtime();
			}

			if (cc)
				cmn_err(CE_NOTE, "Cmd 0x22 non-0 CC, Seq = %d, "
					"CC = 0x%x, rd_end = %ld",
					seq, cc,
					ctsmc_stat[j].ctsmc_time_rd_end);
		}
#endif
		if (cmd == ctsmc_watch_command) {
			int i;
			uint8_t *rmg = (uint8_t *)msg;
			char rmsg[256];
			bzero(rmsg, 256);
			(void) strcpy(rmsg, "Recv Msg Bytes ");
			(void) sprintf(rmsg, "%s for cmd 0x%x :", rmsg, cmd);
			for (i = 0; i < SMC_MSG_LEN(msg); i++) {
				(void) sprintf(rmsg, "%s %2x", rmsg, rmg[i]);
			}
			cmn_err(CE_NOTE, "%s", rmsg);
		}


		(void) ctsmc_pkt_upstream(mnode_p, msg, uSeq);
	}

	/*
	 * Free the sequence number and then signal the conditional
	 * variable so that some requestor threads can wake up
	 */
	ctsmc_freeSeq(ctsmc, seq, pos);
	ctsmc_wakeupRequestor(ctsmc, seq);

	return (DDI_INTR_CLAIMED);
}

/*
 * Host (SPARC) KCS interface interrupt handler.
 *
 * receive a message from the SMC
 */
int
ctsmc_pkt_receive(ctsmc_state_t *ctsmc)
{
	ctsmc_hwinfo_t	*m = (ctsmc_hwinfo_t *)ctsmc->ctsmc_hw;
	ctsmc_rsppkt_t	*msg = (ctsmc_rsppkt_t *)ctsmc->ctsmc_rsppkt;
	int		e = DDI_INTR_CLAIMED;
	uint8_t csr;
	int count = 0;

	ctsmc->ctsmc_init |= SMC_IN_INTR;

	SMC_GET_HRTIME(ctsmc_intr_start)
	SMC_DEBUG(SMC_RECV_DEBUG, "receive(ctsmc %p)", ctsmc);

	SMC_INCR_CNT(CTSMC_PKT_RECV);

	LOCK_DATA(m);
	/*
	 * If it's poll-mode and status bits do not reflect
	 * pending data, just return
	 */
	if (ctsmc->ctsmc_mode & SMC_POLL_MODE) {
		csr = ctsmc_getcsr(m);
		if (!(SC_WATCHDOG(csr) || SC_DAVAIL(csr) ||
				SC_PANICSYS(csr))) {
			UNLOCK_DATA(m);
			ctsmc->ctsmc_init &= ~SMC_IN_INTR;
			return (DDI_INTR_CLAIMED);
		}
	}

	/*
	 * Check if it is a watchdog timer interrupt.
	 */
	e = ctsmc_watchdog(m);
	if (e == DDI_INTR_CLAIMED) {
		sc_rspmsg_t rsp;

		SMC_INCR_CNT(CTSMC_WDOG_EXP);
		SC_MSG_ID(&rsp) = 0;
		SC_MSG_CMD(&rsp) = SMC_EXPIRED_WATCHDOG_NOTIF;
		SC_MSG_LEN(&rsp) = 0;
		SC_MSG_CC(&rsp) = 0;

		/*
		 * enque message in the upstream queue
		 */
		ctsmc_async_putq(ctsmc, &rsp);
		SMC_GET_HRTIME(ctsmc_intr_end)
		ctsmc->ctsmc_init &= ~SMC_IN_INTR;
		UNLOCK_DATA(m);
		return (e);
	}

	/*
	 * Read incoming message from the SMC
	 */

	e = ctsmc_read(ctsmc, msg);

	/*
	 * If we got a message, process it.
	 */
	if (e == SMC_SUCCESS) {
			if (ctsmc_async_valid(SMC_MSG_CMD(msg)) ==
					SMC_SUCCESS) {
				CTSMC_COPY_LOG(msg, CTSMC_ASYNCRSP_IDX, count);
			} else {
				CTSMC_COPY_LOG(msg, CTSMC_RSP_IDX, count);
			}
		/*
		 * Check if it is an event notification message (e.g., ENUM).
		 * These messages are "asynchronous," since they are not the
		 * result of a request message.
		 */
		e = ctsmc_pkt_events(ctsmc, msg);

		if (e == DDI_INTR_CLAIMED) {
			SMC_GET_HRTIME(ctsmc_intr_end)
			ctsmc->ctsmc_init &= ~SMC_IN_INTR;
			UNLOCK_DATA(m);
			return (e);
		}

		/*
		 * This is neither a watchdog timer interrupt,
		 * nor an event message. Process the message in
		 * the "normal" way.
		 */
#if SMC_WATCHDOG_DEBUG
		if (SMC_MSG_CMD(msg) == SMC_RESET_WATCHDOG_TIMER) {
			ctsmc_wdog_rsp_end = gethrtime();
			ctsmc_wdog_rsp_seq = SMC_MSG_SEQ(msg);
		}
#endif

		e = ctsmc_pkt_process(ctsmc, msg);
		if (e == DDI_INTR_CLAIMED) {
			SMC_GET_HRTIME(ctsmc_intr_end)
			ctsmc->ctsmc_init &= ~SMC_IN_INTR;
			UNLOCK_DATA(m);
			return (e);
		}
	} else {
		SMC_INCR_CNT(CTSMC_RECV_FAILURE);
	}

	SMC_DEBUG(SMC_RECV_DEBUG,
		"receive(ctsmc %p): unprocessed packet", ctsmc);
	ctsmc->ctsmc_init &= ~SMC_IN_INTR;

	UNLOCK_DATA(m);
	return (DDI_INTR_CLAIMED);
}

static int
ctsmc_process_incoming(ctsmc_state_t *ctsmc)
{
	ctsmc_hwinfo_t	*m = (ctsmc_hwinfo_t *)ctsmc->ctsmc_hw;
	ctsmc_rsppkt_t	*msg = (ctsmc_rsppkt_t *)ctsmc->ctsmc_rsppkt;
	int		e = DDI_INTR_CLAIMED;

	/*
	 * Check if it is a watchdog timer interrupt.
	 */
	e = ctsmc_watchdog(m);
	if (e == DDI_INTR_CLAIMED) {
		sc_rspmsg_t rsp;

		SMC_INCR_CNT(CTSMC_WDOG_EXP);
		SC_MSG_ID(&rsp) = 0;
		SC_MSG_CMD(&rsp) = SMC_EXPIRED_WATCHDOG_NOTIF;
		SC_MSG_LEN(&rsp) = 0;
		SC_MSG_CC(&rsp) = 0;

		/*
		 * enque message in front of queue and schedule a timeout
		 * to process the entries in the queue
		 */
		ctsmc_async_putq(ctsmc, &rsp);
		return (e);
	}

	/*
	 * Read incoming message from the SMC
	 */

	e = ctsmc_read(ctsmc, msg);

	/*
	 * If we got a message, process it.
	 */
	if (e == SMC_SUCCESS) {
		/*
		 * Check if it is an event notification message (e.g., ENUM).
		 * These messages are "asynchronous," since they are not the
		 * results of a request message.
		 */
		e = ctsmc_pkt_events(ctsmc, msg);

		if (e == DDI_INTR_CLAIMED) {
			return (e);
		}

		/*
		 * This is neither a watchdog timer interrupt,
		 * nor an event message. Process the message in
		 * the "normal" way.
		 */
		e = ctsmc_pkt_process(ctsmc, msg);
		if (e == DDI_INTR_CLAIMED) {
			return (e);
		}
	} else {
		SMC_INCR_CNT(CTSMC_RECV_FAILURE);
	}

	return (e);
}

/*
 * Flush SMC's write buffer by reading all pending packets
 * waiting for host to read. It returns SMC_FAILURE if
 * attention indicator bits are still set, and SMC_SUCCESS
 * if no more data to be read.
 */
static int
ctsmc_pkt_readall(ctsmc_state_t *ctsmc)
{
	ctsmc_hwinfo_t	*m = (ctsmc_hwinfo_t *)ctsmc->ctsmc_hw;
	int		maxcount = 20;

	SMC_GET_HRTIME(ctsmc_intr_start)
	SMC_DEBUG(SMC_RECV_DEBUG, "receive(ctsmc %p)", ctsmc);

	/*
	 * Poll a few times to check if any more packet is waiting
	 * to be read, otherwise return
	 */
	while (maxcount-- && (ctsmc_data_not_waiting(m) ==
				SMC_FAILURE)) {
		(void) ctsmc_process_incoming(ctsmc);
	}

	/*
	 * If data is still waiting to be read, return failure
	 */
	return (ctsmc_data_not_waiting(m));
}

/*
 * converts an user-sent request message into SMC format
 */
static void
ctsmc_copymsg(ctsmc_reqpkt_t *req, sc_reqmsg_t *ureq, uint8_t seq)
{
	ctsmc_msg_command(req, SC_MSG_CMD(ureq), seq,
			SC_MSG_DATA(ureq), SC_MSG_LEN(ureq));
}

/*
 * Send a message to SMC and wait synchronously for response
 * Validation of parameters should be done before making this
 * call
 */
/*ARGSUSED*/
int
ctsmc_command_send(ctsmc_state_t *ctsmc,
		uint8_t cmd,
		uchar_t *inbuf,
		uint8_t	inlen,
		uchar_t	*outbuf,
		uint8_t	*outlen)
{

	uint8_t pos, ret, index;
	uint8_t	uSeq = 0xd3;
	ctsmc_reqpkt_t req;
	sc_rspmsg_t *rsp;	/* Response copied here */
	int e = SMC_SUCCESS;
	clock_t tout;
	int count = 0;

	/*
	 * Check if we have already exceeded the threshold of maximum
	 * number of outstanding requests allowed
	 */
	LOCK_DATA(ctsmc->ctsmc_hw);
	if (ctsmc->ctsmc_hw->smh_smc_num_pend_req >= ctsmc_max_requests)
		ctsmc_cnt_threshold++;
	ctsmc->ctsmc_hw->smh_smc_num_pend_req++;

	/*
	 * We don't want to allocate memory in interrupt handler. So, here
	 * we pre-allocate a response message buffer wherein interrupt handler
	 * will copy data once response is received.
	 */
	if (ctsmc_allocBuf(ctsmc, 0, &index) != SMC_SUCCESS) {
		if (ctsmc->ctsmc_hw->smh_smc_num_pend_req)
			ctsmc->ctsmc_hw->smh_smc_num_pend_req--;

		UNLOCK_DATA(ctsmc->ctsmc_hw);
		return (SMC_FAILURE);
	}

	rsp = FIND_BUF(ctsmc, index);

	/*
	 * Use use sequence number 0 for all internal clients.
	 */
	if (ctsmc_getSeq0(ctsmc, 0, uSeq, cmd, &pos) != SMC_SUCCESS) {
		if (ctsmc->ctsmc_hw->smh_smc_num_pend_req)
			ctsmc->ctsmc_hw->smh_smc_num_pend_req--;

		UNLOCK_DATA(ctsmc->ctsmc_hw);
		ctsmc_freeBuf(ctsmc, index);
		return (SMC_FAILURE);
	}

	/*
	 * Set buffer index to 0, since this is a normal M_DATA
	 * message, and no need to reserve a buffer
	 */
	SET_BUF_INDEX(ctsmc, 0, pos, index, count);

	/*
	 * Frame a packet
	 */
	ctsmc_msg_command(&req, cmd, 0, inbuf, inlen);
	SMC_GET_HRTIME(ctsmc_write_start)
	e = ctsmc_write_noblock(ctsmc, (uchar_t *)&req,
			SMC_MSG_LEN(&req));
	SMC_GET_HRTIME(ctsmc_write_end)

	/*
	 * If it's not success, we should free the sequence number
	 * and buffers here and return
	 */
	if (e != SMC_SUCCESS) {
		SMC_INCR_CNT(CTSMC_XMIT_FAILURE);
		ctsmc_cnt_write_failed++;
		if (ctsmc->ctsmc_hw->smh_smc_num_pend_req)
			ctsmc->ctsmc_hw->smh_smc_num_pend_req--;
		UNLOCK_DATA(ctsmc->ctsmc_hw);
		ctsmc_freeBuf(ctsmc, index);
		ctsmc_freeSeq(ctsmc, 0, pos);
		return (SMC_FAILURE);
	} else {
		SMC_INCR_CNT(CTSMC_PKT_XMIT);
		SMC_INCR_CNT(CTSMC_PVT_REQ);
		if (cmd == SMC_SEND_MESSAGE) {
			SMC_INCR_CNT(CTSMC_IPMI_XMIT);
		}
	}

	if (ctsmc->ctsmc_mode & SMC_POLL_MODE) {
		ret = ctsmc_readpoll(ctsmc, &req);
		UNLOCK_DATA(ctsmc->ctsmc_hw);
	} else {
		UNLOCK_DATA(ctsmc->ctsmc_hw);
		/*
		 * Now wait for interrupt handler to signal when a response
		 * arrives. The interrupt handler would have already completed
		 * processing the response when this thread is woken up.
		 */
		tout = ddi_get_lbolt() +
			drv_usectohz(ctsmc_int_tout * MICROSEC);
		SEQ_WAIT(ctsmc, 0, pos, tout, ret, count);
	}

	/*
	 * Once we return, check whether we are successful. If not
	 * successful, free the sequence number manually
	 */
	if (ret != SMC_SUCCESS) {
		ctsmc_freeSeq(ctsmc, 0, pos);
	}

	if (ret == SMC_SUCCESS) {
		if (outlen) {
			*outlen = SC_RECV_DLENGTH(rsp);

			/*
			 * Copy the data into buffer if necessary
			 */
			if (*outlen && outbuf)
				bcopy(SC_MSG_DATA(rsp), outbuf, *outlen);
		}
		if (SC_MSG_CC(rsp))
			ret = SMC_FAILURE;
	}

	/*
	 * Free the buffer and return. For success case, the sequence
	 * number is freed by read handler
	 */
	ctsmc_freeBuf(ctsmc, index);

	return (ret);
}

/*
 * Send a message contained in mblk to SMC. This will
 * be used for sending commands to SMC in scnarios where we
 * do not need to wait for a response synchronously (normal
 * M_DATA requests)
 */
int
ctsmc_pkt_send(ctsmc_state_t *ctsmc, ctsmc_minor_t *mnode_p, mblk_t *mp)
{
	ctsmc_reqpkt_t req;
	sc_reqmsg_t ureq;	/* Message sent downstream */
	uint8_t seq, pos;
	uchar_t	e;
	int count = 0;
#if SMC_WATCHDOG_DEBUG
	hrtime_t ctsmc_send_start = gethrtime();
#endif

	/* Copy mblk into sc_reqmsg_t and free mp */
	mcopymsg(mp, (uchar_t *)&ureq);
	mp = NULL;

	/*
	 * Check if we have already exceeded the threshold of maximum
	 * number of outstanding requests allowed
	 */
	LOCK_DATA(ctsmc->ctsmc_hw);
	if (ctsmc->ctsmc_hw->smh_smc_num_pend_req >= ctsmc_max_requests)
		ctsmc_cnt_threshold++;
	ctsmc->ctsmc_hw->smh_smc_num_pend_req++;
	UNLOCK_DATA(ctsmc->ctsmc_hw);

	/*
	 * Get a sequence number
	 */
	if (ctsmc_getSeq(ctsmc, mnode_p->minor, SC_MSG_ID(&ureq),
			SC_MSG_CMD(&ureq), &seq, &pos) != SMC_SUCCESS) {
		ctsmc_cnt_seqfull++;
		SMC_DEBUG2(SMC_SEND_DEBUG, "Unable to allocate sequence "
			" number, uSeq = %x, Cmd = 0x%x",
			SC_MSG_ID(&ureq), SC_MSG_CMD(&ureq));
		LOCK_DATA(ctsmc->ctsmc_hw);
		if (ctsmc->ctsmc_hw->smh_smc_num_pend_req)
			ctsmc->ctsmc_hw->smh_smc_num_pend_req--;
		UNLOCK_DATA(ctsmc->ctsmc_hw);

		return (SMC_FAILURE);
	}

	/*
	 * Set buffer index to 0, since this is a normal M_DATA
	 * message, and no need to reserve a buffer
	 */
	SET_BUF_INDEX(ctsmc, seq, pos, 0, count);
	/*
	 * A message sent downstream contains a sc_reqmsg_t structure.
	 * We need to validate the arguments and convert it into a
	 * ctsmc_reqpkt_t structure before sending over to SMC
	 * Prepare message into KCS format expected by SMC
	 */
	ctsmc_copymsg(&req, &ureq, seq);

#ifdef DEBUG
	msg_print(&req);
	if (SC_MSG_CMD(&ureq) == SMC_SEND_MESSAGE) {
		uchar_t *data;
		uint8_t rAddr, cmd, iseq;

		data = SC_MSG_DATA(&ureq);
		rAddr = data[IPMB_OFF_DADDR];
		cmd = data[IPMB_OFF_CMD];
		iseq = IPMB_GET_SEQ(data[IPMB_OFF_SEQ]);

		SMC_DEBUG6(SMC_IPMI_DEBUG, "SendMessage Cmd: SMC Seq = %d, "
			"App seq = %d, len = %d, dest = 0x%x, IPMI Cmd = 0x%x, "
			"IPMI Seq = %d", SMC_MSG_SEQ(&req), SC_MSG_ID(&ureq),
			SC_MSG_LEN(&ureq), rAddr, cmd, iseq);
		if (cmd == 0x81) {
			SMC_DEBUG(SMC_IPMI_DEBUG, "0x81 subcmd = 0x%x",
					data[IPMB_OFF_CMD+1]);
		}
	}
#endif	/* DEBUG */

	/*
	 * Watch the command
	 */
	if (SC_MSG_CMD(&ureq) == ctsmc_watch_command) {
		int i;
		uint8_t *xmsg = (uint8_t *)&req;
		char msg[256];
		bzero(msg, 256);
		(void) strcpy(msg, "Sent Msg Bytes ");
		(void) sprintf(msg, "%s for cmd 0x%x :",
					msg, SMC_MSG_CMD(&req));
		for (i = 0; i < SMC_MSG_LEN(&req); i++) {
			(void) sprintf(msg, "%s %2x", msg, xmsg[i]);
		}
		cmn_err(CE_NOTE, "%s", msg);
	}

	/*
	 * Collect some statistics during send
	 */
	SMC_GET_HRTIME(ctsmc_write_start)
	LOCK_DATA(ctsmc->ctsmc_hw);
	e = ctsmc_write(ctsmc, (uchar_t *)&req, SMC_MSG_LEN(&req));
	UNLOCK_DATA(ctsmc->ctsmc_hw);
	SMC_GET_HRTIME(ctsmc_write_end)

#if SMC_WATCHDOG_DEBUG
	if (SC_MSG_CMD(&ureq) == SMC_RESET_WATCHDOG_TIMER) {
		ctsmc_stat[ctsmc_idx].ctsmc_time_wr_start = ctsmc_send_start;
		ctsmc_stat[ctsmc_idx].ctsmc_time_wr_end = ctsmc_write_end;
		ctsmc_stat[ctsmc_idx].ctsmc_seq = seq;
		ctsmc_stat[ctsmc_idx].app_seq = SC_MSG_ID(&ureq);

		ctsmc_last_idx = ctsmc_idx;
		SMC_INCR(ctsmc_idx);
	}
#endif

#ifdef DEBUG
	if ((e == SMC_SUCCESS) &&
			(SC_MSG_CMD(&ureq) == SMC_SEND_MESSAGE)) {
		uchar_t *data;
		uint8_t cmd, iseq;

		data = SC_MSG_DATA(&ureq);
		cmd = data[IPMB_OFF_CMD];
		iseq = IPMB_GET_SEQ(data[IPMB_OFF_SEQ]);

		SMC_DEBUG3(SMC_IPMI_DEBUG, "SMC_WRITE_NEW: IPMI SEQ = %d "
			"CMD = 0x%x, TIMESTAMP = %ld", iseq, cmd,
			ctsmc_write_end);
	}
#endif

	/*
	 * If write failed, free the sequence number now. We probably never
	 * get a response for this request
	 */
	if (e != SMC_SUCCESS) {
		ctsmc_freeSeq(ctsmc, seq, pos);

		SMC_INCR_CNT(CTSMC_XMIT_FAILURE);
		LOCK_DATA(ctsmc->ctsmc_hw);
		ctsmc_cnt_write_failed++;
		if (ctsmc->ctsmc_hw->smh_smc_num_pend_req)
			ctsmc->ctsmc_hw->smh_smc_num_pend_req--;
		UNLOCK_DATA(ctsmc->ctsmc_hw);
	} else {
		CTSMC_COPY_LOG(&req, CTSMC_REQ_IDX, count);
		SMC_INCR_CNT(CTSMC_PKT_XMIT);
		SMC_INCR_CNT(CTSMC_REGULAR_REQ);
		if (SC_MSG_CMD(&ureq) == SMC_SEND_MESSAGE) {
			SMC_INCR_CNT(CTSMC_IPMI_XMIT);
			if (SC_MSG_DATA(&ureq)[IPMB_OFF_DADDR] == 0x20)
				SMC_INCR_CNT(CTSMC_BMC_XMIT);
		}
	}

	return (e);
}

/*
 * Send a message contained in mblk to SMC. This will
 * be used for sending commands to SMC in scnarios where we
 * need to wait for a response synchronously
 */
static int
ctsmc_pkt_xmit(ctsmc_state_t *ctsmc, ctsmc_minor_t *mnode_p, mblk_t *mp,
		uint8_t *seq, uint8_t *pos)
{
	ctsmc_reqpkt_t req;
	sc_reqmsg_t *ureq;	/* Message sent downstream */
	uint8_t index;
	uchar_t	e;
	int count = 0;

	/*
	 * Check if we have already exceeded the threshold of maximum
	 * number of outstanding requests allowed
	 */
	LOCK_DATA(ctsmc->ctsmc_hw);
	if (ctsmc->ctsmc_hw->smh_smc_num_pend_req >= ctsmc_max_requests)
		ctsmc_cnt_threshold++;
	ctsmc->ctsmc_hw->smh_smc_num_pend_req++;
	UNLOCK_DATA(ctsmc->ctsmc_hw);

	/*
	 * A message sent downstream contains a sc_reqmsg_t structure.
	 * We need to validate the arguments and convert it into a
	 * ctsmc_reqpkt_t structure before sending over to SMC
	 */

	/*
	 * Copy mblk into ctsmc_reqpkt_t
	 */
	ureq = (sc_reqmsg_t *)mp->b_rptr;

	/*
	 * We don't want to allocate memory in interrupt handler. So, here
	 * we pre-allocate a response message buffer wherein interrupt handler
	 * will copy data once response is received.
	 */
	if (ctsmc_allocBuf(ctsmc, mnode_p->minor, &index) != SMC_SUCCESS) {
		LOCK_DATA(ctsmc->ctsmc_hw);
		if (ctsmc->ctsmc_hw->smh_smc_num_pend_req)
			ctsmc->ctsmc_hw->smh_smc_num_pend_req--;
		UNLOCK_DATA(ctsmc->ctsmc_hw);

		return (SMC_NOMEM);
	}

	/*
	 * Get a sequence number
	 */
	if (ctsmc_getSeq(ctsmc, mnode_p->minor, SC_MSG_ID(ureq),
			SC_MSG_CMD(ureq), seq, pos) != SMC_SUCCESS) {
		cmn_err(CE_WARN, "?Unable to allocate sequence number, "
				"uSeq = %x, Cmd = %x", SC_MSG_ID(ureq),
				SC_MSG_CMD(ureq));

		ctsmc_freeBuf(ctsmc, index);
		LOCK_DATA(ctsmc->ctsmc_hw);
		if (ctsmc->ctsmc_hw->smh_smc_num_pend_req)
			ctsmc->ctsmc_hw->smh_smc_num_pend_req--;
		UNLOCK_DATA(ctsmc->ctsmc_hw);

		return (SMC_FAILURE);
	}

	/*
	 * Set buffer index. When response interrupt is received,
	 * the hander will decide based on this whether it's a
	 * synchronous or asynchronous request
	 */
	SET_BUF_INDEX(ctsmc, *seq, *pos, index, count);

	/*
	 * Prepare message into KCS format expected by SMC
	 */
	ctsmc_copymsg(&req, ureq, *seq);

#ifdef DEBUG
	msg_print(&req);
#endif	/* DEBUG */
	SMC_GET_HRTIME(ctsmc_write_start)
	LOCK_DATA(ctsmc->ctsmc_hw);
	e = ctsmc_write(ctsmc, (uchar_t *)&req, SMC_MSG_LEN(&req));
	UNLOCK_DATA(ctsmc->ctsmc_hw);
	SMC_GET_HRTIME(ctsmc_write_end)

	if (e == SMC_SUCCESS) {
		SMC_INCR_CNT(CTSMC_PKT_XMIT);
		SMC_INCR_CNT(CTSMC_SYNC_REQ);
		CTSMC_COPY_LOG(&req, CTSMC_REQ_IDX, count);
	} else {
		ctsmc_freeBuf(ctsmc, index);
		ctsmc_freeSeq(ctsmc, *seq, *pos);

		SMC_INCR_CNT(CTSMC_XMIT_FAILURE);
		LOCK_DATA(ctsmc->ctsmc_hw);
		ctsmc_cnt_write_failed++;
		if (ctsmc->ctsmc_hw->smh_smc_num_pend_req)
			ctsmc->ctsmc_hw->smh_smc_num_pend_req--;
		UNLOCK_DATA(ctsmc->ctsmc_hw);
	}

	return (e);
}

/*
 * Sends a message to SMC and synchronously waits for response
 */
int
ctsmc_send_sync_cmd(queue_t *q, mblk_t *mp, int ioclen)
{
	ctsmc_minor_t	*mnode_p = MUXGET(q);
	ctsmc_state_t *ctsmc = mnode_p->ctsmc_state;
	int		e = 0, ret;
	int	cansend;
	uint8_t seq, pos, idx;
	sc_rspmsg_t ursp;
	sc_rspmsg_t *rsp = &ursp;
#ifdef DEBUG
	hrtime_t	ctsmc_send_pre, ctsmc_send_post, ctsmc_recv;
#endif	/* DEBUG */
	sc_reqmsg_t *ureq = (sc_reqmsg_t *)mp->b_rptr;
	clock_t tout;
	int count = 0;

	/*
	 * Check whether the command being sent is
	 * valid and permissible on this minor
	 */
	ret = ctsmc_cmd_error(q, mp, mnode_p, ioclen);
	if (ret != SMC_SUCCESS) {
		ctsmc_msg_response(ureq, rsp, SMC_CC_TIMEOUT, -1);
		e = ctsmc_command_copyout(ioclen, mp, (void *)rsp,
				SC_RECV_HEADER + SC_MSG_LEN(rsp));

		return (SMC_INVALID_ARG);
	}

	SMC_GET_HRTIME(ctsmc_send_pre)
	cansend = ctsmc_pkt_xmit(ctsmc, mnode_p, mp, &seq, &pos);
	SMC_GET_HRTIME(ctsmc_send_post)

	GET_BUF_INDEX(ctsmc, seq, pos, idx, count);
	rsp = FIND_BUF(ctsmc, idx);

	if ((cansend != SMC_SUCCESS) && (mp != NULL)) {
		rsp = &ursp; /* buffer already freed on failure */
		ctsmc_msg_response(ureq, rsp, SMC_CC_TIMEOUT, -1);
		e = ctsmc_command_copyout(ioclen, mp, (void *)rsp,
				SC_RECV_HEADER + SC_MSG_LEN(rsp));

		return (SMC_HW_FAILURE);
	} else
	if ((cansend == SMC_SUCCESS) &&
			(mp == NULL)) {	/* ioctl interrupted */
		ctsmc_freeBuf(ctsmc, idx);
		return (SMC_OP_ABORTED);
	}

	/*
	 * Now wait until response is received
	 */
	tout = ddi_get_lbolt() + drv_usectohz(ctsmc_max_timeout *
			MICROSEC);
	SEQ_WAIT(ctsmc, seq, pos, tout, ret, count);
#ifdef DEBUG
	ctsmc_recv = gethrtime();

	if (ctsmc_hw_stat_flag)
		cmn_err(CE_NOTE, "Sync Send: Time to send = %llu"
			"ms, Time to write = %llu", (ctsmc_send_post -
			ctsmc_send_pre)/MILLISEC, (ctsmc_write_end -
			ctsmc_write_start)/MILLISEC);
#endif

	/*
	 * Once we return from sleep, the response message will
	 * be available in appropriate buffer. Copy it into
	 * ioctl's message pointer. Before we copy, make sure
	 * the message pointer 'mp' is still valid - it might
	 * be that the pointer was freed because of an ioctl
	 * timeout, while we were waiting for a response.
	 */
	if ((mp == NULL) || (rsp == NULL)) {
		ctsmc_freeSeq(ctsmc, seq, pos);
		ctsmc_freeBuf(ctsmc, idx);
		return (SMC_OP_ABORTED);
	}

	/*
	 * Check whether we returned from wait successfully
	 */
	if (ret != SMC_SUCCESS) {
		/*
		 * UNSUCCESSFUL: Probably a timeout or signal.
		 * Free all resources and prepare a message for
		 * sending back an unsuccessful response
		 */
		SMC_DEBUG0(SMC_IOC_DEBUG, "ioctl SCIOC_SEND_SYNC_CMD:TIMEOUT");
		ctsmc_msg_response(ureq, rsp, SMC_CC_TIMEOUT, -1);
		ctsmc_freeSeq(ctsmc, seq, pos);

		ret = SMC_TIMEOUT;
	}
#ifdef DEBUG
	else
	if (ctsmc_hw_stat_flag)
		cmn_err(CE_CONT, "Time to recv = %llu "
			" Time to intr = %llu, Intr Proc = %llu",
			(ctsmc_recv - ctsmc_send_post)/MILLISEC,
			(ctsmc_intr_start -
			ctsmc_send_post)/MILLISEC,
			(ctsmc_intr_end -
			ctsmc_intr_start)/MILLISEC);
#endif

	if (mp != NULL)
		e = ctsmc_command_copyout(ioclen, mp, (void *)rsp,
				SC_RECV_HEADER + SC_MSG_LEN(rsp));
	else
		ret = SMC_OP_ABORTED;

	if (e != SMC_SUCCESS)
		ret = SMC_NOMEM;

	/*
	 * Free buffer allocated in ctsmc_pkt_xmit. Freeing
	 * sequence number is done when handling interrupt
	 */
	ctsmc_freeBuf(ctsmc, idx);

	return (ret);
}

/*
 * Routine invoked to read respose to a command sent to SMC
 * synchronously
 */
static int
ctsmc_readpoll(ctsmc_state_t *ctsmc, ctsmc_reqpkt_t *req)
{
	int e;
	ctsmc_rsppkt_t msg;
	sc_rspmsg_t *rsp;
	uint8_t uSeq, pos, minor, buf_index;
	int loopmax, maxcount = 20;
	uchar_t		csr;
	ctsmc_hwinfo_t	*m = (ctsmc_hwinfo_t *)ctsmc->ctsmc_hw;
	int count = 0;

	do {
		loopmax = LOOPMAX;
		csr = ctsmc_status(m);
		while (!(SC_DAVAIL(csr) || SC_WATCHDOG(csr) ||
				SC_PANICSYS(csr)) && loopmax--) {
			csr = ctsmc_status(m);
			drv_usecwait(50);
		}

		if (!(SC_DAVAIL(csr) || SC_WATCHDOG(csr) ||
				SC_PANICSYS(csr)) && !loopmax) {
			cmn_err(CE_WARN, "DATA NOT AVAIL");
			return (SMC_FAILURE);
		}

		/*
		 * Check if watchdog expired
		 */
		if (ctsmc_watchdog(m) == DDI_INTR_CLAIMED) {
			sc_rspmsg_t rsp;

			SMC_INCR_CNT(CTSMC_WDOG_EXP);
			SC_MSG_ID(&rsp) = 0;
			SC_MSG_CMD(&rsp) = SMC_EXPIRED_WATCHDOG_NOTIF;
			SC_MSG_LEN(&rsp) = 0;
			SC_MSG_CC(&rsp) = 0;

			/*
			 * enque message in upstream queue
			 */
			ctsmc_async_putq(ctsmc, &rsp);
			continue;
		}

		/*
		 * Read incoming message from the SMC
		 */
		e = ctsmc_read(ctsmc, &msg);
		if (e != SMC_SUCCESS) {
			continue;
		}

		/*
		 * Check if async event
		 */
		if (ctsmc_pkt_events(ctsmc, &msg) == DDI_INTR_CLAIMED)
			continue;

		/*
		 * If it's the expected response, break
		 */
		if ((SMC_MSG_CMD(&msg) == SMC_MSG_CMD(req)) &&
				(SMC_MSG_SEQ(&msg) == SMC_MSG_SEQ(req)))
			break;
		else
			(void) ctsmc_pkt_process(ctsmc, &msg);
	} while ((SMC_MSG_CMD(&msg) != SMC_MSG_CMD(req)) && maxcount--);

	if (maxcount == 0)
		return (SMC_FAILURE);

	/*
	 * If we got a message, process it.
	 */
	if (e == SMC_SUCCESS) {
		/*
		 * The message is a response to a request originating
		 * on the SPARC host. Look for a matching request.
		 */
		e = ctsmc_findSeq(ctsmc, SMC_MSG_SEQ(&msg), SMC_MSG_CMD(&msg),
				&minor, &uSeq, &pos);
		if (e != SMC_SUCCESS)
			return (e);
		GET_BUF_INDEX(ctsmc, SMC_MSG_SEQ(&msg), pos, buf_index, count);
		rsp = FIND_BUF(ctsmc, buf_index);
		if (rsp != NULL)
			rmsg_convert(rsp, &msg, uSeq);
		ctsmc_freeSeq(ctsmc, SMC_MSG_SEQ(&msg), pos);
	}
	return (e);
}

/*
 * Setup outgoing and incoming buffers to log IPMI messages
 */
void
ctsmc_setup_ipmi_logs(ctsmc_state_t *ctsmc)
{
	int i;

	ctsmc_num_ipmi_buffers = ctsmc_num_logs;
	if (ctsmc_num_ipmi_buffers) {
		ctsmc_buf_logs[0] = ctsmc->msglog[0] =
			(void **)NEW(ctsmc_num_ipmi_buffers,
				ctsmc_reqpkt_t *);
		for (i = 1; i < 4; i++) {
			ctsmc_buf_logs[i] = ctsmc->msglog[i] =
				(void **)NEW(ctsmc_num_ipmi_buffers,
							ctsmc_rsppkt_t *);
		}

		for (i = 0; i < ctsmc_num_ipmi_buffers; i++) {
			ctsmc_buf_logs[0][i] = NEW(1, ctsmc_reqpkt_t);
			ctsmc_buf_logs[1][i] = NEW(1, ctsmc_rsppkt_t);
			ctsmc_buf_logs[2][i] = NEW(1, ctsmc_rsppkt_t);
			ctsmc_buf_logs[3][i] = NEW(1, ctsmc_rsppkt_t);
		}
	}
}

void
ctsmc_destroy_ipmi_logs(ctsmc_state_t *ctsmc)
{
	int i;
	if (ctsmc_num_ipmi_buffers) {
		for (i = 0; i < ctsmc_num_ipmi_buffers; i++) {
			FREE(ctsmc_buf_logs[0][i], 1, ctsmc_reqpkt_t);
			FREE(ctsmc_buf_logs[1][i], 1, ctsmc_rsppkt_t);
			FREE(ctsmc_buf_logs[2][i], 1, ctsmc_rsppkt_t);
			FREE(ctsmc_buf_logs[3][i], 1, ctsmc_rsppkt_t);
		}

		FREE(ctsmc->msglog[0],
				ctsmc_num_ipmi_buffers, ctsmc_reqpkt_t *);
		FREE(ctsmc->msglog[1],
				ctsmc_num_ipmi_buffers, ctsmc_rsppkt_t *);
		FREE(ctsmc->msglog[2],
				ctsmc_num_ipmi_buffers, ctsmc_rsppkt_t *);
		FREE(ctsmc->msglog[3],
				ctsmc_num_ipmi_buffers, ctsmc_rsppkt_t *);
	}
}
