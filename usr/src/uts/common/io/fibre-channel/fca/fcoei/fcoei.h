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
#ifndef	_FCOEI_H
#define	_FCOEI_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

/*
 * FCOEI logging
 */
extern int fcoei_use_ext_log;
extern void *fcoei_state;

#define	FCOEI_EXT_LOG(log_ident, ...)				\
	{							\
		if (fcoei_use_ext_log) {			\
			fcoe_trace(log_ident, __VA_ARGS__);	\
		}						\
	}

#define	FCOEI_LOG(log_ident, ...)		\
	fcoe_trace(log_ident, __VA_ARGS__)

/*
 * IOCTL supporting stuff
 */
#define	FCOEI_IOCTL_FLAG_MASK		0xFF
#define	FCOEI_IOCTL_FLAG_IDLE		0x00
#define	FCOEI_IOCTL_FLAG_OPEN		0x01
#define	FCOEI_IOCTL_FLAG_EXCL		0x02

/*
 * define common constants
 */
#define	FCOEI_MAX_OPEN_XCHS	2048
#define	FCOEI_SOL_TABLE_SIZE	512
#define	FCOEI_UNSOL_TABLE_SIZE	512
#define	FCOEI_VERSION		"20110331-1.01"
#define	FCOEI_NAME_VERSION	"SunFC FCoEI v" FCOEI_VERSION

/*
 * define RNID Management Info
 */
#define	FCOEI_RNID_HBA	0x7
#define	FCOEI_RNID_IPV4	0x1
#define	FCOEI_RNID_IPV6	0x2

typedef enum event_type {
	AE_EVENT_NONE = 0,
	AE_EVENT_EXCHANGE,
	AE_EVENT_SOL_FRAME,
	AE_EVENT_UNSOL_FRAME,
	AE_EVENT_PORT,
	AE_EVENT_ABORT,
	AE_EVENT_RESET,
} event_type_e;

typedef struct fcoei_event {
	list_node_t	 ae_node;
	event_type_e	 ae_type;

	/*
	 * event specific
	 */
	uint64_t	 ae_specific;

	/*
	 * event related object
	 */
	void		*ae_obj;
} fcoei_event_t;

typedef struct fcoei_soft_state {
	dev_info_t		*ss_dip;
	uint32_t		 ss_flags;
	uint32_t		 ss_fcp_data_payload_size;
	list_t			 ss_comp_xch_list;

	/*
	 * common data structure (fc_local_port_t) between leadville and fcoei
	 */
	void			*ss_port;

	/*
	 * common data structure between fcoei and fcoe module
	 */
	fcoe_port_t		*ss_eport;

	struct fcoei_exchange	**ss_sol_oxid_table;
	struct fcoei_exchange	**ss_unsol_rxid_table;
	uint16_t		 ss_next_sol_oxid;
	uint16_t		 ss_next_unsol_rxid;
	uint16_t		 ss_min_sol_oxid;
	uint16_t		 ss_max_sol_oxid;

	/*
	 * We will use ss_taskq to dispatch watchdog and other tasks
	 */
	ddi_taskq_t		*ss_taskq;

	kcondvar_t		 ss_watchdog_cv;
	kmutex_t		 ss_watchdog_mutex;

	/*
	 * current port state, speed. see fctl.h
	 */
	uint16_t		 ss_link_state;
	uint16_t		 ss_link_speed;

	/*
	 * # of unprocessed port/link change
	 */
	uint32_t		 ss_port_event_counter;
	list_t			 ss_event_list;

	/*
	 * solicited and unsolicited exchanges timing checking
	 */
	uint32_t		 ss_sol_cnt1;
	uint32_t		 ss_sol_cnt2;
	uint32_t		*ss_sol_cnt;
	uint32_t		 ss_unsol_cnt1;
	uint32_t		 ss_unsol_cnt2;
	uint32_t		*ss_unsol_cnt;

	/*
	 * ioctl related stuff
	 */
	uint32_t		 ss_ioctl_flags;
	kmutex_t		 ss_ioctl_mutex;

	/*
	 * fp-defined routines that fcoei will call
	 */
	fc_fca_bind_info_t	 ss_bind_info;

	/*
	 * fcoei-defined plogi response that fp will use
	 */
	la_els_logi_t		 ss_els_logi;

	/*
	 * fcoei-defined routines that fp will call
	 */
	fc_fca_tran_t		 ss_fca_tran;

	/*
	 * Direct p2p information, and ss's fcid will be stored here
	 */
	fc_fca_p2p_info_t	ss_p2p_info;

	/*
	 * RNID Management Information
	 */
	fc_rnid_t			ss_rnid;
} fcoei_soft_state_t;

#define	SS_FLAG_LV_NONE			0x0000
#define	SS_FLAG_LV_BOUND		0x0001
#define	SS_FLAG_PORT_DISABLED		0x0002
#define	SS_FLAG_TERMINATE_WATCHDOG	0x0004
#define	SS_FLAG_WATCHDOG_RUNNING	0x0008
#define	SS_FLAG_WATCHDOG_IDLE		0x0010
#define	SS_FLAG_TRIGGER_FP_ATTACH	0x0020
#define	SS_FLAG_FLOGI_FAILED		0x0040

/*
 * fcoei_frame - corresponding data structure to fcoe_frame/fc_frame
 */
typedef struct fcoei_frame {
	fcoei_event_t		 ifm_ae;
	fcoe_frame_t		*ifm_frm;
	uint32_t		 ifm_flags;
	struct fcoei_exchange	*ifm_xch;

	/*
	 * will be used after the relevant frame mblk was released by ETH layer
	 */
	uint8_t			 ifm_rctl;
} fcoei_frame_t;

#define	IFM_FLAG_NONE		0x0000
#define	IFM_FLAG_FREE_NETB	0x0001

/*
 * fcoei_exchange - corresponding data structure to leadville fc_packet
 */
typedef struct fcoei_exchange {
	list_node_t		 xch_comp_node;
	fcoei_event_t		 xch_ae;
	uint32_t		 xch_flags;
	fcoei_soft_state_t	*xch_ss;
	clock_t			 xch_start_tick;
	clock_t			 xch_end_tick;
	int			 xch_resid;
	ksema_t			 xch_sema;

	/*
	 * current cnt for timing check, when the exchange is created
	 */
	uint32_t		*xch_cnt;

	/*
	 * leadville fc_packet will not maintain oxid/rxid,
	 * so fcoei exchange  need do it
	 */
	uint16_t		 xch_oxid;
	uint16_t		 xch_rxid;

	/*
	 * to link leadville's stuff
	 */
	fc_packet_t		*xch_fpkt;
	fc_unsol_buf_t		*xch_ub;
} fcoei_exchange_t;

#define	XCH_FLAG_NONE		0x00000000
#define	XCH_FLAG_TMOUT		0x00000001
#define	XCH_FLAG_ABORT		0x00000002
#define	XCH_FLAG_IN_SOL_TABLE	0x00000004
#define	XCH_FLAG_IN_UNSOL_TABLE	0x00000008
#define	XCH_FLAG_LRO		0x00000010

typedef struct fcoei_walk_arg
{
	fcoei_exchange_t	*wa_xch;
	uint16_t		 wa_oxid;
} fcoei_walk_arg_t;

/*
 * Define conversion and calculation macros
 */
#define	FRM2IFM(x_frm)	((fcoei_frame_t *)(x_frm)->frm_client_private)
#define	FRM2SS(x_frm)							\
	((fcoei_soft_state_t *)(x_frm)->frm_eport->eport_client_private)

#define	PORT2SS(x_port)	((fcoei_soft_state_t *)(x_port)->port_fca_private)
#define	EPORT2SS(x_eport)					\
	((fcoei_soft_state_t *)(x_eport)->eport_client_private)

#define	FPKT2XCH(x_fpkt)	((fcoei_exchange_t *)x_fpkt->pkt_fca_private)
#define	FRM2FPKT(x_fpkt)	(FRM2IFM(frm)->ifm_xch->xch_fpkt)

#define	HANDLE2SS(x_handle)	((fcoei_soft_state_t *)fca_handle)

#define	FPLD			frm->frm_payload

#define	FCOEI_FRM2FHDR(x_frm, x_fhdr)				\
	{							\
		(x_fhdr)->r_ctl = FRM_R_CTL(x_frm);		\
		(x_fhdr)->d_id = FRM_D_ID(x_frm);		\
		(x_fhdr)->s_id = FRM_S_ID(x_frm);		\
		(x_fhdr)->type = FRM_TYPE(x_frm);		\
		(x_fhdr)->f_ctl = FRM_F_CTL(x_frm);		\
		(x_fhdr)->seq_id = FRM_SEQ_ID(x_frm);		\
		(x_fhdr)->df_ctl = FRM_DF_CTL(x_frm);		\
		(x_fhdr)->seq_cnt = FRM_SEQ_CNT(x_frm);		\
		(x_fhdr)->ox_id = FRM_OXID(x_frm);		\
		(x_fhdr)->rx_id = FRM_RXID(x_frm);		\
		(x_fhdr)->ro = FRM_PARAM(x_frm);		\
	}

#define	FCOEI_PARTIAL_FHDR2FRM(x_fhdr, x_frm)		\
	{						\
		FFM_R_CTL((x_fhdr)->r_ctl, x_frm);	\
		FFM_D_ID((x_fhdr)->d_id, x_frm);	\
		FFM_S_ID((x_fhdr)->s_id, x_frm);	\
		FFM_TYPE((x_fhdr)->type, x_frm);	\
		FFM_F_CTL((x_fhdr)->f_ctl, x_frm);	\
	}

#define	PRT_FRM_HDR(x_p, x_f)						\
	{								\
		FCOEI_LOG(x_p, "rctl/%x, fctl/%x, type/%x, oxid/%x",	\
			FCOE_B2V_1((x_f)->frm_hdr->hdr_r_ctl),		\
			FCOE_B2V_3((x_f)->frm_hdr->hdr_f_ctl),		\
			FCOE_B2V_1((x_f)->frm_hdr->hdr_type),		\
			FCOE_B2V_2((x_f)->frm_hdr->hdr_oxid));		\
	}

#define	FCOEI_INIT_SOL_ID_TABLE(xch)				\
	{								\
		uint16_t min_xid;					\
		min_xid = xch->xch_ss->ss_min_sol_oxid;			\
		do {							\
			xch->xch_oxid = atomic_inc_16_nv(		\
			    &xch->xch_ss->ss_next_sol_oxid) %		\
			    (xch->xch_ss->ss_max_sol_oxid -		\
			    min_xid + 1) + min_xid;			\
			if (xch->xch_oxid == 0xFFFF) {			\
				xch->xch_oxid = min_xid;		\
			}						\
		} while (atomic_cas_ptr(				\
		    &xch->xch_ss->ss_sol_oxid_table[xch->xch_oxid],	\
		    NULL, xch) != NULL);				\
		xch->xch_rxid = 0xFFFF;					\
		xch->xch_flags |= XCH_FLAG_IN_SOL_TABLE;		\
	}

#define	FCOEI_SET_UNSOL_FRM_RXID(frm)					\
	{								\
		uint16_t xid;						\
		do {							\
			xid = atomic_inc_16_nv(				\
			    &FRM2SS(frm)->ss_next_unsol_rxid) %		\
			    FCOEI_UNSOL_TABLE_SIZE;			\
		} while (FRM2SS(frm)->ss_unsol_rxid_table[xid] != NULL); \
		FFM_RXID(xid, frm);					\
	}

#define	FCOEI_INIT_UNSOL_ID_TABLE(xch)					\
	{								\
		xch->xch_oxid = fpkt->pkt_cmd_fhdr.ox_id;		\
		xch->xch_rxid = fpkt->pkt_cmd_fhdr.rx_id;		\
		xch->xch_ss->ss_unsol_rxid_table[xch->xch_rxid] = xch;	\
		xch->xch_flags |= XCH_FLAG_IN_UNSOL_TABLE;		\
	}

/*
 * Common functions defined in fcoei.c
 */
void fcoei_complete_xch(fcoei_exchange_t *xch, fcoe_frame_t *frm,
    uint8_t pkt_state, uint8_t pkt_reason);
void fcoei_init_ifm(fcoe_frame_t *frm, fcoei_exchange_t *xch);
void fcoei_handle_comp_xch_list(fcoei_soft_state_t *ss);

/*
 * Common functions defined in fcoei_lv.c
 */
void fcoei_init_fcatran_vectors(fc_fca_tran_t *fcatran);
void fcoei_process_event_exchange(fcoei_event_t *ae);
void fcoei_process_event_reset(fcoei_event_t *ae);

/*
 * Common functions defined in fcoei_eth.c
 */
void fcoei_init_ect_vectors(fcoe_client_t *ect);
void fcoei_process_unsol_frame(fcoe_frame_t *frm);
void fcoei_handle_sol_frame_done(fcoe_frame_t *frm);
void fcoei_process_event_port(fcoei_event_t *ae);
void fcoei_port_event(fcoe_port_t *eport, uint32_t event);

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _FCOEI_H */
