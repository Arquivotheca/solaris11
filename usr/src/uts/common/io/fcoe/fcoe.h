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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * The following notice accompanied the original version of this file:
 *
 * BSD LICENSE
 *
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef	_FCOE_H_
#define	_FCOE_H_

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

extern int			 fcoe_use_ext_log;
extern struct fcoe_soft_state	*fcoe_global_ss;

/*
 * Caution: 1) LOG will be available in debug/non-debug mode
 *	    2) Anything which can potentially flood the log should be under
 *	       extended logging, and use FCOE_EXT_LOG.
 *	    3) Don't use FCOE_EXT_LOG in performance-critical code path, such
 *	       as normal SCSI I/O code path. It could hurt system performance.
 *	    4) Use kmdb to change foce_use_ext_log in the fly to adjust
 *	       tracing
 */
#define	FCOE_EXT_LOG(log_ident, ...)	\
	do {	\
		if (fcoe_use_ext_log) {	\
			fcoe_trace(log_ident, __VA_ARGS__);	\
		}	\
	} while (0)

#define	FCOE_LOG(log_ident, ...)	\
	fcoe_trace(log_ident, __VA_ARGS__)

/*
 * There will be only one fcoe instance
 */
typedef struct fcoe_soft_state {
	dev_info_t	*ss_dip;
	uint32_t	ss_flags;
	list_t		ss_mac_list;

	kcondvar_t	ss_mlist_cv;
	kmutex_t	ss_mlist_mutex;

	uint32_t	ss_ioctl_flags;
	kmutex_t	ss_ioctl_mutex;

	/*
	 * watchdog stuff
	 */
	ddi_taskq_t	*ss_watchdog_taskq;
	kcondvar_t	ss_watch_cv;
	kmutex_t	ss_watch_mutex;

	/* Pending frame */
	list_t		ss_pfrm_list;
	/* Pending fip frame */
	list_t		ss_pfip_list;
	/* Pending link state affected event */
	list_t		ss_macevent_list;
} fcoe_soft_state_t;

#define	SS_FLAG_TERMINATE_WATCHDOG	0x0020
#define	SS_FLAG_WATCHDOG_RUNNING	0x0040
#define	SS_FLAG_DOG_WAITING		0x0080
#define	SS_FLAG_TERMINATE_TIMER		0x0200
#define	SS_FLAG_TIMER_RUNNING		0x0400

/*
 *  Driver name
 */
#define	FCOEI_DRIVER_NAME	"fcoei"
#define	FCOET_DRIVER_NAME	"fcoet"

typedef struct fcoe_fip_fcf {
	list_node_t	fcf_node;
	uint8_t		fcf_addr[ETHERADDRL];
	uint8_t		fcf_NameID[8];
	uint8_t		fcf_FCMAP[3];
	uint8_t		fcf_FName[8];
	uint8_t		fcf_MaxSizeVerif:1, /* set only when desired */
					    /* solicited DA received */
			fcf_AvaforLogin:1,
			fcf_Reserved:6;
	uint8_t		fcf_pri;
	clock_t		fcf_ka_time;
	clock_t		fcf_timeout;
	uint32_t	fcf_adv_period;
} fcoe_fip_fcf_t;

/*
 * One for each ethernet port
 */
typedef struct fcoe_mac
{
	list_node_t		fm_ss_node;
	datalink_id_t		fm_linkid;
	uint32_t		fm_flags;
	fcoe_soft_state_t	*fm_ss;
	fcoe_port_t		fm_eport;
	fcoe_client_t		fm_client;
	dev_info_t		*fm_client_dev;
	list_t			fm_fip_fcf;
	fcoe_fip_fcf_t		*fm_current_fcf;
	mac_handle_t		fm_handle;
	mac_client_handle_t	fm_cli_handle;
	mac_promisc_handle_t	fm_promisc_handle;
	mac_notify_handle_t	fm_notify_handle;
	mac_unicast_handle_t	fm_unicst_handle;
	uint8_t			fm_primary_addr[ETHERADDRL];
	uint8_t			fm_current_addr[ETHERADDRL];
	uint32_t		fm_running:1,
				fm_force_promisc:1,
				fm_fip_mode:1,
				fm_rsvd:12,
				fm_state:4,
				fm_fip_stage:4,
				mac_fpma:1,
				fm_link_state:8;
	uint32_t		fm_frm_cnt;
	uint32_t		fm_event_cnt;
	clock_t			fm_fip_soldisc;
	kcondvar_t		fm_tx_cv;
	kmutex_t		fm_mutex;
} fcoe_mac_t;

/* fm_state in fcoe_mac_t */
#define	FCOE_MAC_STATE_OFFLINE		0x0
#define	FCOE_MAC_STATE_ONLINE		0x1

/* fm_link_state in fcoe_mac_t */
typedef enum fcoe_mac_link_state {
	FCOE_MAC_LINK_STATE_DOWN = 0,
	FCOE_MAC_LINK_STATE_UP,
	FCOE_MAC_LINK_STATE_INIT_DONE,
	FCOE_MAC_LINK_STATE_KA,
	FCOE_MAC_LINK_STATE_WAIT_FCF
} fcoe_mac_link_state_t;


#define	FCOE_MAC_FLAG_ENABLED		0x01
#define	FCOE_MAC_FLAG_BOUND		0x02
#define	FCOE_MAC_FLAG_USER_DEL		0x04

/* fm_fip_stage in fcoe_mac_t */
typedef enum fcoe_mac_fip_stage {
	FIP_STAGE_PRE_FIP = 0,		/* Pursuing FIP support */
	FIP_STAGE_DO_DISC_SOLICIT,	/* FIP Discovery Solicitation */
	FIP_STAGE_DO_FLOGI,		/* Sending FIP FLOGI and waiting */
	FIP_STAGE_DO_KA,		/* Sending FIP Keep-Alive */
	FIP_STAGE_MAX			/* Not a real state */
} fcoe_mac_fip_stage_t;

/* mac event type */
typedef enum fcoe_event_mac {
	FCOE_EVENT_MAC_LINK_UP = 1,
	FCOE_EVENT_MAC_LINK_DOWN,
	FCOE_EVENT_MAC_LIAFFECT_FIPFRM_DONE,
	FCOE_EVENT_MAC_LIAFFECT_FIPFRM_TMOUT,
	FCOE_EVENT_MAC_SEND_FLOGI,
	FCOE_EVENT_MAC_LINK_INIT_DONE,
	FCOE_EVENT_MAC_LINK_KA,
	FCOE_EVENT_MAC_FORCE_LINK_DOWN,	/* force link down from upper */
	FCOE_EVENT_SOFT_LINK_DOWN, /* faked link event, internal use only */
	FCOE_EVENT_SOFT_LINK_UP
} fcoe_event_mac_t;

typedef struct fcoe_mac_link_state_event {
	list_node_t	event_node;
	fcoe_mac_t	*event_mac;
	uint8_t		event_type;
	uint64_t	event_arg;
} fcoe_mac_ls_event_t;

typedef struct fcoe_frame_header {
	uint8_t		 ffh_ver[1];	/* version field - upper 4 bits */
	uint8_t		 ffh_resvd[12];
	uint8_t		 ffh_sof[1];	/* start of frame per RFC 3643 */
} fcoe_frame_header_t;

typedef struct fcoe_frame_tailer {
	uint8_t		 fft_crc[4];	/* FC packet CRC */
	uint8_t		 fft_eof[1];
	uint8_t		 fft_resvd[3];
} fcoe_frame_tailer_t;

/*
 * RAW frame structure
 * It is used to describe the content of every mblk
 */
typedef struct fcoe_i_frame {
	list_node_t		 fmi_pending_node;

	fcoe_frame_t		*fmi_frame;	/* to common struct */
	fcoe_mac_t		*fmi_mac;	/* to/from where */

	/*
	 * FRAME structure
	 */
	struct ether_header	*fmi_efh;	/* 14 bytes eth header */
	fcoe_frame_header_t	*fmi_ffh;	/* 14 bytes FCOE header */
	uint8_t			*fmi_fc_frame;
	fcoe_frame_tailer_t	*fmi_fft;	/* 8 bytes FCOE tailer */
} fcoe_i_frame_t;

/*
 * FIP protocol code and subcode
 */
#define	FIP_OPCODE_DISC	0x0001
#define	FIP_OPCODE_LINK	0x0002
#define	FIP_OPCODE_KA	0x0003
#define	FIP_SUBCODE_REQ	0x01	/* Link Service Request */
#define	FIP_SUBCODE_SOL	0x01	/* Discovery Solicitation */
#define	FIP_SUBCODE_RSP	0x02	/* Link Service Reply */
#define	FIP_SUBCODE_ADV	0x02	/* Discovery Advertisement */
#define	FIP_SUBCODE_KA  0x01    /* Keep Alive */
#define	FIP_SUBCODE_CVL 0x02    /* Clear Virtual Link */

/* FIP frame type, do not change the order */
typedef enum fip_frame_type {
	FIP_FRAME_DISC = 0x00,
	FIP_FRAME_FLOGI_LS,
	FIP_FRAME_NPIVFDISC_LS,
	FIP_FRAME_LOGO_LS,
	FIP_FRAME_KEEP_ALIVE
} fip_frm_type_t;

#define	FIP_FRAME_TYPE_DISC_SOL \
	(FIP_OPCODE_DISC << 16 | FIP_SUBCODE_SOL << 8 |\
	(FIP_FRAME_DISC & 0xff))
#define	FIP_FRAME_TYPE_FLOGI_REQ \
	(FIP_OPCODE_LINK << 16 | FIP_SUBCODE_REQ << 8 |\
	(FIP_FRAME_FLOGI_LS & 0xff))
#define	FIP_FRAME_TYPE_NPIVFDISC_REQ \
	(FIP_OPCODE_LINK << 16 | FIP_SUBCODE_REQ << 8 |\
	(FIP_FRAME_NPIVFDISC_LS & 0xff))
#define	FIP_FRAME_TYPE_LOGO_REQ \
	(FIP_OPCODE_LINK << 16 | FIP_SUBCODE_REQ << 8 |\
	(FIP_FRAME_LOGO_LS & 0xff))
#define	FIP_FRAME_TYPE_KEEP_ALIVE			\
	(FIP_OPCODE_KA << 16 | FIP_SUBCODE_REQ << 8 |\
	(FIP_FRAME_KEEP_ALIVE & 0xff));


#define	FIP_STARTUP_TIMEOUT  (35000000)
#define	FCOE_STARTUP_TIMEOUT  (5000000)

typedef struct fcoe_fip_eth_ver {
	uint8_t		ffe_ver;
	uint8_t		ffe_resvd;
} fcoe_fip_eth_ver_t;

typedef struct fcoe_fip_op_header {
	uint8_t	fip_opcode[2];
	uint8_t fip_reserved;
	uint8_t fip_subcode;
	uint8_t fip_descrpt_len[2];
	uint8_t fip_flags[2];
} fcoe_fip_op_header_t;

#define	FIP_FPMA_FLAG(fip) \
	((FCOE_B2V_2(fip->frm_op_header->fip_flags)>>15) & 0x0001)
#define	FIP_SPMA_FLAG(fip) \
	((FCOE_B2V_2(fip->frm_op_header->fip_flags)>>14) & 0x0001)
#define	FIP_A_FLAG(fip) \
	((FCOE_B2V_2(fip->frm_op_header->fip_flags)>>2) & 0x0001)
#define	FIP_S_FLAG(fip) \
	((FCOE_B2V_2(fip->frm_op_header->fip_flags)>>1) & 0x0001)
#define	FIP_F_FLAG(fip) \
	(FCOE_B2V_2(fip->frm_op_header->fip_flags) & 0x0001)

/* one descriptor will always be multiple of 4 bytes */
typedef struct fcoe_fip_descriptor {
	uint8_t	fip_descrpt_type;
	uint8_t fip_descrpt_len;
	uint8_t fip_descrpt_value[1];
} fcoe_fip_descriptor_t;

#define	FIP_DESC_TYPE(descriptor)	(descriptor->fip_descrpt_type)

typedef enum fip_descriptor_type_val {
	FIP_DESC_TYPE_VAL_PRIORITY = 1,
	FIP_DESC_TYPE_VAL_MACADDR,
	FIP_DESC_TYPE_VAL_FCMAP,
	FIP_DESC_TYPE_VAL_NAMEIDENTIFIER,
	FIP_DESC_TYPE_VAL_FABRICNAME,
	FIP_DESC_TYPE_VAL_MAXRCVSIZE,
	FIP_DESC_TYPE_VAL_FLOGI,
	FIP_DESC_TYPE_VAL_NPIVFDISC,
	FIP_DESC_TYPE_VAL_LOGO,
	FIP_DESC_TYPE_VAL_ELP,
	FIP_DESC_TYPE_VAL_VXPORTINDENTIFIER,
	FIP_DESC_TYPE_VAL_FKAADVPERIOD,
	FIP_DESC_TYPE_VAL_VENDORID,
	FIP_DESC_TYPE_VAL_VLANID,
	FIP_DESC_TYPE_VAL_VENDORSPECIFIC, /* length is vendor specific */
	FIP_DESC_TYPE_VAL_MAX	/* not a real one */
} fip_descrpt_type_val_t;

typedef struct fcoe_fip_frame {
	uint32_t		frm_type;
	uint8_t			frm_retry;
	clock_t			frm_cmd_timeout;
	list_node_t		frm_pending_node;
	fcoe_mac_t		*frm_mac;
	void			*frm_netb;
	struct ether_header	*frm_eth;	/* 14 bytes eth header */
	fcoe_fip_eth_ver_t	*frm_ffh;	/* 2 bytes */
	fcoe_fip_op_header_t	*frm_op_header; /* 8 bytes */
	fcoe_fip_descriptor_t	*frm_op_desc_list_header; /* n * 4 bytes */
	uint8_t			*frm_pad;
} fcoe_fip_frm_t;

typedef struct fcoe_worker {
	list_t		worker_frm_list;
	kmutex_t	worker_lock;
	kcondvar_t	worker_cv;
	uint32_t	worker_flags;
	uint32_t	worker_ntasks;
} fcoe_worker_t;

#define	FCOE_WORKER_TERMINATE	0x01
#define	FCOE_WORKER_STARTED	0x02
#define	FCOE_WORKER_ACTIVE	0x04

/*
 * IOCTL supporting stuff
 */
#define	FCOE_IOCTL_FLAG_MASK		0xFF
#define	FCOE_IOCTL_FLAG_IDLE		0x00
#define	FCOE_IOCTL_FLAG_OPEN		0x01
#define	FCOE_IOCTL_FLAG_EXCL		0x02
#define	FCOE_IOCTL_FLAG_EXCL_BUSY	0x04

/*
 * common-used macros defined to simplify coding
 */

#define	PADDING_HEADER_SIZE	(sizeof (struct ether_header) + \
	sizeof (fcoe_frame_header_t))
#define	PADDING_FIP_HEADER_SIZE	(sizeof (struct ether_header) + \
	sizeof (fcoe_fip_eth_ver_t) + sizeof (fcoe_fip_op_header_t))
#define	PADDING_SIZE	(PADDING_HEADER_SIZE + sizeof (fcoe_frame_tailer_t))

#define	EPORT2MAC(x_eport)	((fcoe_mac_t *)(x_eport)->eport_fcoe_private)

#define	FRM2MAC(x_frm)		(EPORT2MAC((x_frm)->frm_eport))
#define	FRM2FMI(x_frm)		((fcoe_i_frame_t *)(x_frm)->frm_fcoe_private)
#define	FRM2MBLK(x_frm)		((mblk_t *)(x_frm)->frm_netb)

#define	FCOE_VER			0
#define	FCOE_DECAPS_VER(x_ffh)		((x_ffh)->ffh_ver[0] >> 4)
#define	FCOE_ENCAPS_VER(x_ffh, x_v)			\
	{						\
		(x_ffh)->ffh_ver[0] = ((x_v) << 4);	\
	}

/*
 * fcoe driver common functions
 */
extern fcoe_mac_t *fcoe_lookup_mac_by_id(datalink_id_t);
extern void fcoe_destroy_mac(fcoe_mac_t *);
extern mblk_t *fcoe_get_mblk(fcoe_mac_t *, uint32_t);
extern mblk_t *fcoe_get_fip_mblk(fcoe_mac_t *, uint32_t);
extern void fcoe_post_frame(fcoe_frame_t *);
extern void fcoe_post_fip_event(fcoe_mac_t *, uint8_t, uint64_t);
extern void fcoe_free_fip_event(fcoe_mac_ls_event_t *);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _FCOE_H_ */
