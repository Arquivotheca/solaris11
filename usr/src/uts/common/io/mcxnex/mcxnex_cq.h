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
 *  Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_MCXNEX_CQ_H
#define	_MCXNEX_CQ_H

/*
 * mcxnex_cq.h
 *    Contains all of the prototypes, #defines, and structures necessary
 *    for the Completion Queue Processing routines.
 *    Specifically it contains the various completion types, flags,
 *    structures used for managing completion queues, and prototypes
 *    for many of the functions consumed by other parts of the driver
 *    (including those routines directly exposed through the IBTF CI
 *    interface).
 *
 *    Most of the values defined below establish default values which,
 *    where indicated, can be controlled via their related patchable values,
 *    if 'mcxnex_alt_config_enable' is set.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#include "mcxnex_misc.h"

/*
 * The following defines the default number of Completion Queues. This
 * is controllable via the "mcxnex_log_num_cq" configuration variable.
 * We also have a define for the minimum size of a CQ.  CQs allocated
 * with size 0, 1, 2, or 3 will always get back a CQ of size 4.
 */
#define	MCXNEX_NUM_CQ_SHIFT		0x09 /* 0x10 for IB */
/*
 *	#define	MCXNEX_CQ_MIN_SIZE	0x3
 */

/*
 *	#define	MCXNEX_CQ_MIN_SIZE	0xFF	 testing, try min 1 page
 */

/* page div 32 (cqe size) minus 1, for min size */
#define	MCXNEX_CQ_MIN_SIZE	((PAGESIZE / 32) - 1)

/*
 * These are the defines for the Mcxnex CQ completion statuses.
 */
#define	MCXNEX_CQE_SUCCESS		0x0
#define	MCXNEX_CQE_LOC_LEN_ERR		0x1
#define	MCXNEX_CQE_LOC_OP_ERR		0x2
#define	MCXNEX_CQE_LOC_PROT_ERR		0x4
#define	MCXNEX_CQE_WR_FLUSHED_ERR	0x5
#define	MCXNEX_CQE_MW_BIND_ERR		0x6
#define	MCXNEX_CQE_BAD_RESPONSE_ERR	0x10
#define	MCXNEX_CQE_LOCAL_ACCESS_ERR	0x11
#define	MCXNEX_CQE_REM_INV_REQ_ERR	0x12
#define	MCXNEX_CQE_REM_ACC_ERR		0x13
#define	MCXNEX_CQE_REM_OP_ERR		0x14
#define	MCXNEX_CQE_TRANS_TO_ERR		0x15
#define	MCXNEX_CQE_RNRNAK_TO_ERR	0x16
#define	MCXNEX_CQE_EEC_REM_ABORTED_ERR	0x22

/*
 * These are the defines for the Mcxnex CQ entry types. They indicate what type
 * of work request is completing (for successful completions).  Note: The
 * "SND" or "RCV" in each define is used to indicate whether the completion
 * work request was from the Send work queue or the Receive work queue on
 * the associated QP.
 */
#define	MCXNEX_CQE_SND_NOP		0x0
#define	MCXNEX_CQE_SND_SND_INV		0x1
#define	MCXNEX_CQE_SND_RDMAWR		0x8
#define	MCXNEX_CQE_SND_RDMAWR_IMM	0x9
#define	MCXNEX_CQE_SND_SEND		0xA
#define	MCXNEX_CQE_SND_SEND_IMM		0xB
#define	MCXNEX_CQE_SND_LSO		0xE
#define	MCXNEX_CQE_SND_RDMARD		0x10
#define	MCXNEX_CQE_SND_ATOMIC_CS	0x11
#define	MCXNEX_CQE_SND_ATOMIC_FA	0x12
#define	MCXNEX_CQE_SND_ATOMIC_CS_EX	0x14
#define	MCXNEX_CQE_SND_ATOMIC_FC_EX	0x15
#define	MCXNEX_CQE_SND_FRWR		0x19
#define	MCXNEX_CQE_SND_LCL_INV		0x1B
#define	MCXNEX_CQE_SND_CONFIG		0x1F
#define	MCXNEX_CQE_SND_BIND_MW		0x18

#define	MCXNEX_CQE_RCV_RDMAWR_IMM	0x00
#define	MCXNEX_CQE_RCV_SEND		0x01
#define	MCXNEX_CQE_RCV_SEND_IMM		0x02
#define	MCXNEX_CQE_RCV_SND_INV		0x03
#define	MCXNEX_CQE_RCV_ERROR_CODE	0x1E
#define	MCXNEX_CQE_RCV_RESIZE_CODE	0x16


/* Define for maximum CQ number mask (CQ number is 24 bits) */
#define	MCXNEX_CQ_MAXNUMBER_MSK		0xFFFFFF


/*
 * new EQ mgmt - per domain (when it gets there).
 * The first N are for CQ Completions.  Following that are:
 *
 *	1 for CQ Errors
 *	1 for Asyncs and Command Completions, and finally
 *	1 for All Other events.
 *
 * hs_intrmsi_allocd is the N in the above.
 */

#define	MCXNEX_CQ_EQNUM_GET(state)					\
	(state->hs_devlim.num_rsvd_eq +					\
	    (atomic_inc_uint_nv(&state->hs_eq_dist) %			\
	    state->hs_intrmsi_allocd))

#define	MCXNEX_CQ_ERREQNUM_GET(state)					\
	(state->hs_devlim.num_rsvd_eq + state->hs_intrmsi_allocd)
/*
 * The following defines are used for Mcxnex CQ error handling.  Note: For
 * CQEs which correspond to error events, the Mcxnex device requires some
 * special handling by software.  These defines are used to identify and
 * extract the necessary information from each error CQE, including status
 * code (above), doorbell count, and whether a error completion is for a
 * send or receive work request.
 */


#define	MCXNEX_CQE_ERR_STATUS_SHIFT	0
#define	MCXNEX_CQE_ERR_STATUS_MASK	0xFF
#define	MCXNEX_CQE_ERR_DBDCNT_MASK	0xFFFF
#define	MCXNEX_CQE_SEND_ERR_OPCODE	0x1E
#define	MCXNEX_CQE_RECV_ERR_OPCODE	0x1E

/* Defines for tracking whether a CQ is being used with special QP or not */
#define	MCXNEX_CQ_IS_NORMAL		0
#define	MCXNEX_CQ_IS_SPECIAL		1

/*
 * The mcxnex_sw_cq_s structure is also referred to using the "mcxnex_cqhdl_t"
 * typedef (see mcxnex_typedef.h).  It encodes all the information necessary
 * to track the various resources needed to allocate, initialize, poll, resize,
 * and (later) free a completion queue (CQ).
 *
 * Specifically, it has a consumer index and a lock to ensure single threaded
 * access to it.  It has pointers to the various resources allocated for the
 * completion queue, i.e. a CQC resource and the memory for the completion
 * queue itself. It also has a reference count and the number(s) of the EQs
 * to which it is associated (for success and for errors).
 *
 * Additionally, it has a pointer to the associated MR handle (for the mapped
 * queue memory) and a void pointer that holds the argument that should be
 * passed back to the IBTF when events are generated on the CQ.
 *
 * We also have the always necessary backpointer to the resource for the
 * CQ handle structure itself.  But we also have pointers to the "Work Request
 * ID" processing lists (both the lock and the regular list, as well as the
 * head and tail for the "reapable" list).  See mcxnex_wrid.c for more details.
 */

#define	MCXNEX_CQ_DEF_UAR_DOORBELL	0x11	/* cmd_sn = 1, req solicited */
#define	MCXNEX_CD_DEF_UAR_DB_SHIFT	0x38	/* decimal 56 */

typedef void (*mcxnex_priv_cq_cb_t)(mcxnex_state_t *state,
    mcxnex_cqhdl_t cq, void *arg);

struct mcxnex_sw_cq_s {
	kmutex_t		cq_lock;
	struct mcxnex_sw_cq_s 	*cq_resize_hdl; /* points to tranistory hdl */
	uint32_t		cq_consindx;
	uint32_t		cq_cqnum;
	mcxnex_hw_cqe_t		*cq_buf;
	mcxnex_mrhdl_t		cq_mrhdl;
	uint32_t		cq_bufsz;
	uint32_t		cq_log_cqsz;
	uint_t			cq_refcnt;
	uint32_t		cq_eqnum;
	uint32_t		cq_erreqnum;
	uint_t			cq_is_special;
	uint_t			cq_is_umap;
	uint32_t		cq_uarpg;
	devmap_cookie_t		cq_umap_dhp;
	mcxnex_rsrc_t		*cq_cqcrsrcp;
	mcxnex_rsrc_t		*cq_rsrcp;
	uint_t			cq_intmod_count;
	uint_t			cq_intmod_usec;

	/* DoorBell Record Information */
	ddi_acc_handle_t	cq_arm_ci_dbr_acchdl;
	mcxnex_dbr_t		*cq_arm_ci_vdbr;
	uint64_t		cq_arm_ci_pdbr;
	uint64_t		cq_dbr_mapoffset;	/* user mode access */

	void			*cq_hdlrarg;

	/* private cq callbacks for non ibtf cqs */
	mcxnex_priv_cq_cb_t	cq_priv_cb;
	void			*cq_priv_cb_arg;

	/* For Work Request ID processing */
	avl_tree_t		cq_wrid_wqhdr_avl_tree;

	struct mcxnex_qalloc_info_s cq_cqinfo;
};
_NOTE(READ_ONLY_DATA(mcxnex_sw_cq_s::cq_cqnum
    mcxnex_sw_cq_s::cq_eqnum
    mcxnex_sw_cq_s::cq_erreqnum
    mcxnex_sw_cq_s::cq_cqcrsrcp
    mcxnex_sw_cq_s::cq_rsrcp
    mcxnex_sw_cq_s::cq_hdlrarg
    mcxnex_sw_cq_s::cq_is_umap
    mcxnex_sw_cq_s::cq_uarpg))
_NOTE(DATA_READABLE_WITHOUT_LOCK(mcxnex_sw_cq_s::cq_bufsz
    mcxnex_sw_cq_s::cq_consindx
    mcxnex_sw_cq_s::cq_cqinfo))
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_sw_cq_s::cq_lock,
    mcxnex_sw_cq_s::cq_buf
    mcxnex_sw_cq_s::cq_mrhdl
    mcxnex_sw_cq_s::cq_refcnt
    mcxnex_sw_cq_s::cq_is_special
    mcxnex_sw_cq_s::cq_umap_dhp))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing",
    mcxnex_sw_cq_s::cq_intmod_count
    mcxnex_sw_cq_s::cq_intmod_usec
    mcxnex_sw_cq_s::cq_resize_hdl))

int mcxnex_cq_alloc(mcxnex_state_t *state, ibt_cq_hdl_t ibt_cqhdl,
    ibt_cq_attr_t *attr_p, uint_t *actual_size, mcxnex_cqhdl_t *cqhdl,
    uint_t sleepflag);
int mcxnex_cq_free(mcxnex_state_t *state, mcxnex_cqhdl_t *cqhdl,
    uint_t sleepflag);
int mcxnex_cq_resize(mcxnex_state_t *state, mcxnex_cqhdl_t cqhdl,
    uint_t req_size, uint_t *actual_size, uint_t sleepflag);
int mcxnex_cq_modify(mcxnex_state_t *state, mcxnex_cqhdl_t cqhdl,
    uint_t count, uint_t usec, ibt_cq_handler_id_t hid, uint_t sleepflag);
int mcxnex_cq_notify(mcxnex_state_t *state, mcxnex_cqhdl_t cqhdl,
    ibt_cq_notify_flags_t flags);
int mcxnex_cq_poll(mcxnex_state_t *state, mcxnex_cqhdl_t cqhdl, ibt_wc_t *wc_p,
    uint_t num_wc, uint_t *num_polled);
int mcxnex_cq_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe);
int mcxnex_cq_err_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe);
int mcxnex_cq_refcnt_inc(mcxnex_cqhdl_t cq, uint_t is_special);
void mcxnex_cq_refcnt_dec(mcxnex_cqhdl_t cq);
mcxnex_cqhdl_t mcxnex_cqhdl_from_cqnum(mcxnex_state_t *state, uint_t cqnum);
void mcxnex_cq_entries_flush(mcxnex_state_t *state, mcxnex_qphdl_t qp);
void mcxnex_cq_resize_helper(mcxnex_state_t *state, mcxnex_cqhdl_t cq);

void mcxnex_priv_cq_handler(mcxnex_state_t *state, mcxnex_cqhdl_t cq_hdl);
void mcxnex_priv_cq_set_handler(mcxnex_state_t *state, mcxnex_cqhdl_t cq_hdl,
    mcxnex_priv_cq_cb_t cq_cb, void *cq_cb_arg);

#endif	/* _MCXNEX_CQ_H */
