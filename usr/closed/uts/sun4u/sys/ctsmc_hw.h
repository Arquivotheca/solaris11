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

#ifndef	_SYS_SMC_HW_H
#define	_SYS_SMC_HW_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Structure of the SMC KCS-style register interface.
 */

typedef struct {
	uchar_t		smr_data_io;	/* data in or out */
	uchar_t		smr_csr;		/* status (ro) command (wo) */
} ctsmc_reg_t;

/*
 * Warning: do not dereference smcdev directly--use ddi_get/ddi_put
 */
typedef struct {
	kmutex_t	lock;
	kcondvar_t	smh_hwcv;
	uint8_t		smh_hwflag;
	ctsmc_reg_t		*smh_reg;
	ddi_acc_handle_t	smh_handle;
	ddi_device_acc_attr_t	smh_attr;
	uint8_t	smh_smc_num_pend_req;
	ddi_iblock_cookie_t	smh_ib;
	ddi_idevice_cookie_t	smh_id;
} ctsmc_hwinfo_t;

/*
 * KCS Request message header
 */
typedef struct {
	uint8_t len;
	uint8_t sum;
	uint8_t seq;
	uint8_t netFn;
	uint8_t cmd;
} ctsmc_reqhdr_t;

/*
 * KCS Response message header
 */
typedef struct {
	uint8_t len;
	uint8_t sum;
	uint8_t seq;
	uint8_t netFn;
	uint8_t cmd;
	uint8_t cc;
} ctsmc_rsphdr_t;

#define	SMC_PKT_MAX_SIZE	0x40
#define	SMC_SEND_HEADER		sizeof (ctsmc_reqhdr_t)
#define	SMC_RECV_HEADER		sizeof (ctsmc_rsphdr_t)

#define	SMC_SEND_DSIZE	(SMC_PKT_MAX_SIZE - SMC_SEND_HEADER)
#define	SMC_RECV_DSIZE	(SMC_PKT_MAX_SIZE - SMC_RECV_HEADER)

/*
 * Entire KCS Request Message
 */
typedef struct {
	ctsmc_reqhdr_t hdr;
	uchar_t			data[SMC_SEND_DSIZE];
} ctsmc_reqpkt_t;

/*
 * Entire KCS Response Message
 */
typedef struct {
	ctsmc_rsphdr_t 	hdr;
	uchar_t			data[SMC_RECV_DSIZE];
} ctsmc_rsppkt_t;

/*
 * Any SMC sync/async message
 */
typedef union {
	ctsmc_reqpkt_t	req;
	ctsmc_rsppkt_t	rsp;
} ctsmc_pkt_t;

#define	SMC_MSG_HDR(msg)	((msg)->hdr)
#define	SMC_MSG_DATA(msg)	((msg)->data)

#define	SMC_MSG_CMD(msg)	(SMC_MSG_HDR(msg).cmd)
#define	SMC_MSG_LEN(msg)	(SMC_MSG_HDR(msg).len)
#define	SMC_MSG_SEQ(msg)	(SMC_MSG_HDR(msg).seq)
#define	SMC_MSG_SUM(msg)	(SMC_MSG_HDR(msg).sum)
#define	SMC_MSG_NETFN(msg)	(SMC_MSG_HDR(msg).netFn)
#define	SMC_MSG_CC(msg)		(SMC_MSG_HDR(msg).cc)

#define	SMC_SEND_DLENGTH(msg)	(SMC_MSG_LEN(msg) - SMC_SEND_HEADER)
#define	SMC_RECV_DLENGTH(msg)	(SMC_MSG_LEN(msg) - SMC_RECV_HEADER)

/*
 * Given a message received from SMC, find the length
 * to be sent upstream; and vice versa
 */
#define	SMC_TO_SC_RLEN(msg)	(SMC_MSG_LEN(msg) - SMC_RECV_HEADER)
#define	SC_TO_SMC_XLEN(msg)	(SC_MSG_LEN(msg) + SMC_SEND_HEADER)

#define	SMC_IN_WRITE	0x01
#define	SMC_READ_NO_ATN	0x02

/*
 * System controller status bits (RO)
 */

/*
 * interface state bits
 */
#define	SC_ERR		0xc0		/* error state */
#define	SC_WR		0x80		/* write state */
#define	SC_RD		0x40		/* read state */
#define	SC_IDL		0x00		/* IDLE=>state bits zero */

#define	SC_OEM2		0x20		/* reserved */
#define	SC_OEM1		0x10		/* reserved */
#define	SC_WD		SC_OEM1		/* CSR bit: watchdog expiration */
#define	SC_XIR		SC_OEM2		/* Externally initiated coredump */
#define	SC_CD_		0x08		/* last write to command register */
#define	SC_ATN		0x04		/* Receive message(s) in BMC queue */
#define	SC_IBF		0x02		/* SMC input buffer full */
#define	SC_OBF		0x01		/* SMC output buffer full */

#define	SC_STATEMASK	0xc0		/* highest 2 bits are state */
#define	SC_STATUSMASK	0x07		/* OBF, IBF, ATN */

#define	SC_STATEBITS(csr)	((csr) & SC_STATEMASK)
#define	SC_STATUSBITS(csr)	((csr) & SC_STATUSMASK)

#define	SC_IDLE(csr)		(SC_STATEBITS(csr) == SC_IDL)
#define	SC_READ(csr)		(SC_STATEBITS(csr) == SC_RD)
#define	SC_WRITE(csr)		(SC_STATEBITS(csr) == SC_WR)
#define	SC_ERROR(csr)		(SC_STATEBITS(csr) == SC_ERR)

#define	SC_CTS(csr)			(SC_STATUSBITS(csr) == 0)
#define	SC_DAVAIL(csr)		(((csr) & SC_OBF) == SC_OBF)
#define	SC_DPEND(csr)		(((csr) & SC_IBF) == SC_IBF)
#define	SC_DATN(csr)		(((csr) & SC_ATN) == SC_ATN)
#define	SC_WATCHDOG(csr)	(((csr) & SC_WD) == SC_WD)
#define	SC_PANICSYS(csr)	(((csr) & SC_XIR) == SC_XIR)
#define	SC_PENDING(M, CSR)	\
		(((M)->smh_hwflag & SMC_READ_NO_ATN) ? B_TRUE : SC_DATN(CSR))

/*
 * Define various states during SMC send/recv
 */
#define	SMC_TRANSFER_ERROR	0x0
#define	SMC_TRANSFER_INIT	0x1
#define	SMC_TRANSFER_START	0x2
#define	SMC_TRANSFER_NEXT	0x3
#define	SMC_TRANSFER_END	0x4
#define	SMC_RECEIVE_INIT	0x5
#define	SMC_RECEIVE_START	0x6
#define	SMC_RECEIVE_NEXT	0x7
#define	SMC_RECEIVE_INIT2	0x8
#define	SMC_RECEIVE_END		0x9
#define	SMC_RECEIVE_ERROR	0xa
#define	SMC_MACHINE_END		0xb

#define	SMC_MAX_RETRY_CNT	10
#define	SMC_RECOVERY_TIME_MS	(10 * MILLISEC)

/*
 * System controller Control codes
 */
#define	GET_STATUS	0x60
#define	ABORT		0x60

#define	WRITE_START	0x61
#define	WRITE_END	0x62

#define	WD_CLEAR	0x63
#define	WD1_CMD_NORESTART	0x65
#define	SMC_CLR_OEM2_COND	0x67

#define	READ_START	0x68
#define	FLUSH_ALL	0x66

#define	SMC_NETFUN_REQ	0x18
#define	SMC_NETFUN_RSP	0x1c

#define	CTSMC_NUM_LOGS	4
enum {
	CTSMC_REQ_IDX,
	CTSMC_RSP_IDX,
	CTSMC_ASYNCRSP_IDX,
	CTSMC_IPMIDROP_IDX
};
extern int ctsmc_num_ipmi_buffers, ctsmc_buf_idx[CTSMC_NUM_LOGS];
extern void **ctsmc_buf_logs[CTSMC_NUM_LOGS];

#define	CTSMC_COPY_LOG(MSG, IDX, CNT)	\
do {	\
	if (ctsmc_num_ipmi_buffers) {	\
	bcopy((void *)MSG, (void *)ctsmc_buf_logs[IDX][ctsmc_buf_idx[IDX]],\
		SMC_MSG_LEN(MSG));	\
	SMC_INCR_IDX(ctsmc_buf_idx[IDX], ctsmc_num_ipmi_buffers);	\
	}	\
} while (CNT)

#define	MAX_PKT_COUNT	16

enum {
	CTSMC_PKT_XMIT = 0,
	CTSMC_XMIT_FAILURE,
	CTSMC_PKT_RECV,
	CTSMC_RSP_UNCLAIMED,
	CTSMC_RECV_FAILURE,
	CTSMC_REGULAR_REQ,
	CTSMC_REGULAR_RSP,
	CTSMC_PVT_REQ,
	CTSMC_PVT_RSP,
	CTSMC_SYNC_REQ,
	CTSMC_SYNC_RSP,
	CTSMC_ASYNC_RECV,
	CTSMC_WDOG_EXP,
	CTSMC_IPMI_NOTIF,
	CTSMC_ENUM_NOTIF,
	CTSMC_IPMI_XMIT,
	CTSMC_IPMI_RSPS,
	CTSMC_IPMI_EVTS,
	CTSMC_IPMI_RSPS_DROP,
	CTSMC_BAD_IPMI,
	CTSMC_BMC_XMIT,
	CTSMC_BMC_RSPS,
	CTSMC_BMC_EVTS
};

#define	CTSMC_NUM_COUNTERS	(CTSMC_BMC_EVTS + 1)
extern uint64_t	ctsmc_hw_stat[];

#define	SMC_INCR_CNT(idX)	(ctsmc_hw_stat[idX])++
#define	SMC_STAT_VAL(idX)	(ctsmc_hw_stat[idX])

extern void *ctsmc_regs_map(dev_info_t *dip);
extern void ctsmc_regs_unmap(void *smchw);
extern void ctsmc_msg_copy(mblk_t *mp, uchar_t *bufp);
extern void ctsmc_msg_response(sc_reqmsg_t *reqmsg,
		sc_rspmsg_t *rspmsg, uchar_t cc, uchar_t e);
extern mblk_t *ctsmc_rmsg_message(sc_rspmsg_t *msg);
extern void rmsg_convert(sc_rspmsg_t *ursp,
		ctsmc_rsppkt_t *rsp, uint8_t uSeq);
extern int ctsmc_async_valid(uint8_t cmd);
extern int ctsmc_send_sync_cmd(queue_t *q, mblk_t *mp, int ioclen);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SMC_HW_H */
