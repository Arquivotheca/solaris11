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

#ifndef	_MCXNEX_SRQ_H
#define	_MCXNEX_SRQ_H

/*
 * mcxnex_srq.h
 *    Contains all of the prototypes, #defines, and structures necessary
 *    for the Shared Receive Queue Processing routines.
 *
 *    (including those routines directly exposed through the IBTF CI
 *    interface).
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The following defines the default number of Shared Receive Queues (SRQ).
 * This value is controllable via the "mcxnex_log_num_srq" configuration
 * variable.
 * We also have a define for the minimum size of a SRQ.  SRQs allocated with
 * size 0, 1, 2, or 3 will always get back a SRQ of size 4.
 */
#define	MCXNEX_NUM_SRQ_SHIFT		0x07 /* 0x10 for IB */
#define	MCXNEX_SRQ_MIN_SIZE		0x4

/*
 * The mcxnex firmware currently limits an SRQ to maximum of 31 SGL
 * per WQE (WQE size is 512 bytes or less).  With a WQE size of 256
 * (SGL 15 or less) no problems are seen.  We set SRQ_MAX_SGL size here, for
 * use in the config profile to be 0xF.
 */
#define	MCXNEX_SRQ_MAX_SGL		0xF

/*
 * SRQ States as defined by Mcxnex.
 */
#define	MCXNEX_SRQ_STATE_SW_OWNER	0xF
#define	MCXNEX_SRQ_STATE_HW_OWNER	0x0
#define	MCXNEX_SRQ_STATE_ERROR		0x1

/*
 * The mcxnex_sw_srq_s structure is also referred to using the "mcxnex_srqhdl_t"
 * typedef (see mcxnex_typedef.h).  It encodes all the information necessary
 * to track the various resources needed to allocate, initialize, query, modify,
 * post, and (later) free a shared receive queue (SRQ).
 */
struct mcxnex_sw_srq_s {
	kmutex_t		srq_lock;
	uint_t			srq_state;
	uint_t			srq_srqnum;
	mcxnex_pdhdl_t		srq_pdhdl;
	mcxnex_mrhdl_t		srq_mrhdl;
	uint_t			srq_is_umap;
	uint32_t		srq_uarpg;
	devmap_cookie_t		srq_umap_dhp;

	ibt_srq_sizes_t		srq_real_sizes;
	mcxnex_rsrc_t		*srq_srqcrsrcp;
	mcxnex_rsrc_t		*srq_rsrcp;
	void			*srq_hdlrarg;
	uint_t			srq_refcnt;

	/* Work Queue */
	mcxnex_workq_hdr_t	*srq_wq_wqhdr;
	uint32_t		*srq_wq_buf;
	uint32_t		srq_wq_bufsz;
	uint32_t		srq_wq_log_wqesz;
	uint32_t		srq_wq_sgl;
	uint32_t		srq_wq_wqecntr;

	/* DoorBell Record information */
	ddi_acc_handle_t	srq_wq_dbr_acchdl;
	mcxnex_dbr_t		*srq_wq_vdbr;
	uint64_t		srq_wq_pdbr;
	uint64_t		srq_rdbr_mapoffset;	/* user mode access */

	/* For zero-based */
	uint64_t		srq_desc_off;

	/* Queue Memory for SRQ */
	struct mcxnex_qalloc_info_s	srq_wqinfo;
};
_NOTE(READ_ONLY_DATA(mcxnex_sw_srq_s::srq_pdhdl
    mcxnex_sw_srq_s::srq_mrhdl
    mcxnex_sw_srq_s::srq_srqnum
    mcxnex_sw_srq_s::srq_wq_sgl
    mcxnex_sw_srq_s::srq_srqcrsrcp
    mcxnex_sw_srq_s::srq_rsrcp
    mcxnex_sw_srq_s::srq_hdlrarg
    mcxnex_sw_srq_s::srq_is_umap
    mcxnex_sw_srq_s::srq_uarpg))
_NOTE(DATA_READABLE_WITHOUT_LOCK(mcxnex_sw_srq_s::srq_wq_bufsz
    mcxnex_sw_srq_s::srq_wqinfo
    mcxnex_sw_srq_s::srq_wq_buf
    mcxnex_sw_srq_s::srq_wq_wqhdr
    mcxnex_sw_srq_s::srq_desc_off))
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_sw_srq_s::srq_lock,
    mcxnex_sw_srq_s::srq_real_sizes
    mcxnex_sw_srq_s::srq_umap_dhp))

/*
 * The mcxnex_srq_info_t structure is used internally by the Mcxnex driver to
 * pass information to and from the mcxnex_srq_alloc() routine.  It contains
 * placeholders for all of the potential inputs and outputs that this routine
 * can take.
 */
typedef struct mcxnex_srq_info_s {
	mcxnex_pdhdl_t		srqi_pd;
	ibt_srq_hdl_t		srqi_ibt_srqhdl;
	ibt_srq_sizes_t		*srqi_sizes;
	ibt_srq_sizes_t		*srqi_real_sizes;
	mcxnex_srqhdl_t		*srqi_srqhdl;
	uint_t			srqi_flags;
} mcxnex_srq_info_t;

/*
 * The mcxnex_srq_options_t structure is used in the Mcxnex SRQ allocation
 * routines to provide additional option functionality.  When a NULL pointer
 * is passed in place of a pointer to this struct, it is a way of specifying
 * the "default" behavior.  Using this structure, however, is a way of
 * controlling any extended behavior.
 */
typedef struct mcxnex_srq_options_s {
	uint_t			srqo_wq_loc;
} mcxnex_srq_options_t;

/*
 * old call
 * int mcxnex_srq_alloc(mcxnex_state_t *state, mcxnex_srq_info_t *srqinfo,
 *  uint_t sleepflag, mcxnex_srq_options_t *op);
 */

int mcxnex_srq_alloc(mcxnex_state_t *state, mcxnex_srq_info_t *srqinfo,
    uint_t sleepflag);
int mcxnex_srq_free(mcxnex_state_t *state, mcxnex_srqhdl_t *srqhdl,
    uint_t sleepflag);
int mcxnex_srq_modify(mcxnex_state_t *state, mcxnex_srqhdl_t srq,
    uint_t size, uint_t *real_size, uint_t sleepflag);
int mcxnex_srq_post(mcxnex_state_t *state, mcxnex_srqhdl_t srq,
    ibt_recv_wr_t *wr_p, uint_t num_wr, uint_t *num_posted);
void mcxnex_srq_refcnt_inc(mcxnex_srqhdl_t srq);
void mcxnex_srq_refcnt_dec(mcxnex_srqhdl_t srq);
mcxnex_srqhdl_t mcxnex_srqhdl_from_srqnum(mcxnex_state_t *state, uint_t srqnum);

#ifdef __cplusplus
}
#endif

#endif	/* _MCXNEX_SRQ_H */
