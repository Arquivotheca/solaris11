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

#ifndef	_MCXNEX_MISC_H
#define	_MCXNEX_MISC_H

/*
 * mcxnex_misc.h
 *    Contains all of the prototypes, #defines, and structures necessary
 *    for the Miscellaneous routines - Address Handle, Multicast,
 *    Protection Domain, port-related, statistics (kstat) routines, and
 *    extra VTS related routines.
 *    Many of these functions are called by other parts of the driver
 *    (and several routines are directly exposed through the IBTF CI
 *    interface and/or kstat interface).
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#include "mcxnex_typedef.h"
#include "mcxnex_ioctl.h"
#include "mcxnex_rsrc.h"
#include "mcxnex_hw.h"


/*
 * The following defines specify the default number of Address Handles (AH)
 * and their size (in the hardware).  By default the maximum number of address
 * handles is set to 32K.  This value is controllable through the
 * "mcxnex_log_num_ah" configuration variable.  Note:  Mcxnex Address Handles
 * are also referred to as UD Address Vectors (UDAV).
 */
#define	MCXNEX_NUM_AH_SHIFT		0xF
#define	MCXNEX_NUM_AH			(1 << MCXNEX_NUM_AH_SHIFT)
#define	MCXNEX_UDAV_SIZE_SHIFT		0x5
#define	MCXNEX_UDAV_SIZE		(1 << MCXNEX_UDAV_SIZE_SHIFT)

/*
 * The following macro determines whether the contents of a UDAV need to be
 * sync'd (with ddi_dma_sync()).  This decision is based on whether the
 * UDAV is in DDR memory (no sync) or system memory (sync required).
 */

#define	MCXNEX_UDAV_IS_SYNC_REQ(state)					\
	(((&((state)->ts_rsrc_hdl[MCXNEX_UDAV]))->rsrc_loc ==		\
	MCXNEX_IN_DDR) ? 0 : 1)

/*
 * These defines are used by mcxnex_get_addr_path() and mcxnex_set_addr_path()
 * below.  They indicate the type of hardware context being passed in the
 * "path" argument.  Because the Mcxnex hardware formats for the QP address
 * path and UDAV address path structures is so similar, but not exactly the
 * same, we use these flags to indicate which type of structure is being
 * read from or written to.
 */
#define	MCXNEX_ADDRPATH_QP		0x0
#define	MCXNEX_ADDRPATH_UDAV		0x1

/*
 * The following defines specify the default number of Multicast Groups (MCG)
 * and the maximum number of QP which can be associated with each.  By default
 * the maximum number of multicast groups is set to 256, and the maximum number
 * of QP per multicast group is set to 248 (256 4-byte slots minus the 8 slots
 * in the header).  The first of these values is controllable through the
 * "mcxnex_log_num_mcg" configuration variable.  "mcxnex_num_qp_per_mcg" is
 * also available if the customer needs such a large capability.
 */
#define	MCXNEX_NUM_MCG_SHIFT		0x8
#define	MCXNEX_NUM_QP_PER_MCG_MIN	0x8
#define	MCXNEX_NUM_QP_PER_MCG		0xf8

#define	MCXNEX_MCGMEM_SZ(state)						\
	((((state)->hs_cfg_profile->cp_num_qp_per_mcg) + 8) << 2)

/*
 * Macro to compute the offset of the QP list in a given MCG entry.
 */
#define	MCXNEX_MCG_GET_QPLIST_PTR(mcg)					\
	((mcxnex_hw_mcg_qp_list_t *)((uintptr_t)(mcg) +			\
	sizeof (mcxnex_hw_mcg_t)))

/*
 * The following defines specify the characteristics of the Mcxnex multicast
 * group hash table.  The MCXNEX_NUM_MCG_HASH_SHIFT defines the size of the
 * hash table (as a power-of-2), which is set to 16 by default.  This value
 * is controllable through the "mcxnex_log_num_mcg_hash" configuration variable,
 * but serious consideration should be taken before changing this value.  Note:
 * its appropriate size should be a function of the entire table size (as
 * defined by "mcxnex_log_num_mcg" and MCXNEX_NUM_MCG_SHIFT above).
 */
#define	MCXNEX_NUM_MCG_HASH_SHIFT	0x4

/*
 * The following defines are used by the multicast routines to determine
 * if a given "multicast GID" is valid or not (see mcxnex_mcg_is_mgid_valid
 * for more details.  These values are pulled from the IBA specification,
 * rev. 1.1
 */
#define	MCXNEX_MCG_TOPBITS_SHIFT	56
#define	MCXNEX_MCG_TOPBITS_MASK		0xFF
#define	MCXNEX_MCG_TOPBITS		0xFF

#define	MCXNEX_MCG_FLAGS_SHIFT		52
#define	MCXNEX_MCG_FLAGS_MASK		0xF
#define	MCXNEX_MCG_FLAGS_PERM		0x0
#define	MCXNEX_MCG_FLAGS_NONPERM	0x1

#define	MCXNEX_MCG_SCOPE_SHIFT		48
#define	MCXNEX_MCG_SCOPE_MASK		0xF
#define	MCXNEX_MCG_SCOPE_LINKLOC	0x2
#define	MCXNEX_MCG_SCOPE_SITELOC	0x5
#define	MCXNEX_MCG_SCOPE_ORGLOC		0x8
#define	MCXNEX_MCG_SCOPE_GLOBAL		0xE


/*
 * The following defines specify the default number of Protection Domains (PD).
 * By default the maximum number of protection domains is set to 64K.  This
 * value is controllable through the "mcxnex_log_num_pd" configuration variable.
 */
#define	MCXNEX_NUM_PD_SHIFT		0x10

/*
 * The following defines specify the default number of Partition Keys (PKey)
 * per port.  By default the maximum number of PKeys is set to 32 per port, for
 * a total of 64 (assuming two ports) .  This value is controllable through the
 * "mcxnex_log_max_pkeytbl" configuration variable.
 */
#define	MCXNEX_NUM_PKEYTBL_SHIFT	0x5
#define	MCXNEX_NUM_PKEYTBL		(1 << MCXNEX_NUM_PKEYTBL_SHIFT)

/*
 * The following defines specify the default number of SGIDs per port.  By
 * default the maximum number of GIDS per port is set to 16.  This value
 * is controllable through the "mcxnex_log_max_gidtbl" configuration variable.
 */
#define	MCXNEX_NUM_GIDTBL_SHIFT		0x4
#define	MCXNEX_NUM_GIDTBL		(1 << MCXNEX_NUM_GIDTBL_SHIFT)

/*
 * Below is a define which is the default number of UAR pages.  By default, the
 * maximum number of UAR pages is set to 1024 for mcxnex.  Note that
 * BlueFlame (if enabled) will  take 1/2 the space behind BAR1 (the UAR BAR)
 * and therefore we must limit this even further.  This value is controllable
 * through the "mcxnex_log_num_uar" configuration variable. NOTE: This value
 * should not be set larger than 15 (0xF) because the UAR index number is
 * used as part of the minor number calculation (see mcxnex_open() for details)
 * and the minor numbers should not be larger than eighteen bits (i.e. 15 bits
 * of UAR index, 3 bits of driver instance number).  This is especially true
 * for 32-bit kernels.
 */
#define	MCXNEX_NUM_UAR_SHIFT		0xA

/*
 * A DoorBell record (DBr) will be handled uniquely.  They are not in ICM now,
 * so they don't need the mapping.  And they just need to be accessible to the
 * HCA as an address, so we don't need to register the memory.  AND, since
 * user level (uDAPL, OPEN verbs) won't ever do the unmapping of them we don't
 * really need to worry about that either.  And the DBrs will have to live in
 * user mappable memory.  So, we can shortcut a lot of things given these
 * assumptions.
 *
 * Other facts:  the DBrs for Mcxnex are only two per qp - one for the Receive
 * side (RQ or SRQ) and one for the CQ.  If a QP is associated with an SRQ, we
 * only need the ONE for the SRQ.  Also, although the RQ/SRQ DBr is only 4-bytes
 * while the CQ DBr is 8-bytes, all DBrs will be 8-bytes (see the union below).
 * Though it may lead to minor wastage, it also means that reuse is easier since
 * any DBr can be used for either, and we don't have to play allocation games.
 *
 * The state structure will hold the pointer to the start of a list of struct
 * mcxnex_dbr_info_s, each one containing the necessary information to manage
 * a page of DBr's.
 */

typedef uint64_t mcxnex_dbr_t;

typedef struct mcxnex_dbr_info_s {
	struct mcxnex_dbr_info_s *dbr_link;
	mcxnex_dbr_t		*dbr_page;	/* virtual addr of page */
	uint64_t		dbr_paddr;	/* physical addr of page */
	ddi_acc_handle_t	dbr_acchdl;
	ddi_dma_handle_t	dbr_dmahdl;
	uint32_t		dbr_nfree;	/* #free DBrs in this page */
	uint32_t		dbr_firstfree;	/* idx of first free DBr */
} mcxnex_dbr_info_t;

#define	MCXNEX_NUM_DBR_PER_PAGE	(PAGESIZE / sizeof (mcxnex_dbr_t))


/*
 * These defines specify some miscellaneous port-related configuration
 * information.  Specifically, MCXNEX_MAX_MTU is used to define the maximum
 * MTU supported for each Mcxnex port, MCXNEX_MAX_PORT_WIDTH is used to define
 * the maximum supported port width, and the MCXNEX_MAX_VLCAP define is used
 * to specify the maximum number of VLs supported, excluding VL15.  Both
 * of these values are controllable and get be set using the "mcxnex_max_mtu"
 * and "mcxnex_max_vlcap" configuration variables.  Note: as with many of the
 * configurable variables, caution should be exercised when changing these
 * values.  These values, specifically, should not be set any larger than
 * they are defined here as these are set to the current Mcxnex device
 * maximums.
 *
 * Note that:  with Mcxnex, these capabilities that were formerly retrieved
 * 	as part of QUERY_DEV_LIM/CAP must now be retrieved with QUERY_PORT.
 *	The init sequence will have to be altered vis-a-vis the older HCAs to
 *	accommodate this change.
 *
 *	Also, the maximums will be changed here for now.
 */
#define	MCXNEX_MAX_MTU		0x5 /* was 0x4, 2048 but moved to 4096 */
#define	MCXNEX_ETH_MTU		0x7
#define	MCXNEX_ETH_MSG_MAX	0x1f
#define	MCXNEX_MAX_PORT_WIDTH	0x7 /* was 0x3 (1x/4x) but now 1/4/8x */
#define	MCXNEX_MAX_VLCAP	0x8 /* remain the same for now */

/*
 * These last defines are used by the statistics counting routines (kstats)
 * for initialization of the structures associated with the IB statistics
 * access routines.  The MCXNEX_CNTR_MASK and MCXNEX_CNTR_SIZE defines are
 * used to divide the "pcr" register into two 32-bit counters (one for "pic0"
 * and the other for "pic1")
 */
#define	MCXNEX_CNTR_MASK		0xFFFFFFFF
#define	MCXNEX_CNTR_SIZE		32
#define	MCXNEX_CNTR_NUMENTRIES	17



#define	MCXNEX_QUEUE_LOCATION_NORMAL	0x1
#define	MCXNEX_QUEUE_LOCATION_USERLAND	0x2

/*
 * Minimum number of ticks to delay between successive polls of the CQ in
 * VTS ioctl loopback test
 */
#define	MCXNEX_VTS_LOOPBACK_MIN_WAIT_DUR	50

/*
 * UAR software table, layout and associated structures
 */

/*
 * Doorbell record table bitmap macros
 */
#define	MCXNEX_IND_BYTE(ind)		((ind) >> 3)
#define	MCXNEX_IND_BIT(ind)		(1 << ((ind) & 0x7))

#define	MCXNEX_BMAP_BIT_SET(bmap, ind)	\
	((bmap)[MCXNEX_IND_BYTE(ind)] |= MCXNEX_IND_BIT(ind))
#define	MCXNEX_BMAP_BIT_CLR(bmap, ind)	\
	((bmap)[MCXNEX_IND_BYTE(ind)] &= ~MCXNEX_IND_BIT(ind))
#define	MCXNEX_BMAP_BIT_ISSET(bmap, ind)	\
	((bmap)[MCXNEX_IND_BYTE(ind)] & MCXNEX_IND_BIT(ind))


/*
 * User doorbell record page tracking
 */
typedef struct mcxnex_udbr_page_s mcxnex_udbr_page_t;

struct mcxnex_udbr_page_s {
	mcxnex_udbr_page_t	*upg_link;
	uint_t			upg_index;
	uint_t			upg_nfree;
	uint64_t		*upg_free;
	caddr_t			upg_kvaddr;
	struct buf		*upg_buf;
	ddi_umem_cookie_t	upg_umemcookie;
	ddi_dma_handle_t	upg_dmahdl;
	ddi_dma_cookie_t 	upg_dmacookie;
};

typedef struct mcxnex_udbr_mgmt_s mcxnex_user_dbr_t;

struct mcxnex_udbr_mgmt_s {
	mcxnex_user_dbr_t	*udbr_link;
	uint_t			udbr_index;	/* same as uarpg */
	mcxnex_udbr_page_t	*udbr_pagep;
};


/*
 * doorbell tracking end
 */

/*
 * The mcxnex_sw_ah_s structure is also referred to using the "mcxnex_ahhdl_t"
 * typedef (see mcxnex_typedef.h).  It encodes all the information necessary
 * to track the various resources needed to allocate, query, modify, and
 * free an address handle.
 *
 * In specific, it has a lock to ensure single-threaded access. It stores a
 * pointer to the associated PD handle, and also contains a copy of the
 * GUID stored into the address handle.  The reason for this extra copy of
 * the GUID info has to do with Mcxnex PRM compliance and is fully explained
 * in mcxnex_misc.c
 *
 * To serve in it's primary function, it also contains a UDAV, which contains
 * all of the data associated with the UD address vector that is being
 * utilized by the holder of the address handle. The hardware-specific format
 * of the UDAV is defined in the mcxnex_hw.h file.
 *
 * It also has the always necessary backpointer to the resource for the AH
 * handle structure itself.
 */
struct mcxnex_sw_ah_s {
	kmutex_t	ah_lock;
	mcxnex_pdhdl_t	ah_pdhdl;
	mcxnex_hw_udav_t *ah_udav;
	mcxnex_rsrc_t	*ah_rsrcp;
	uint64_t	ah_save_guid;
};
_NOTE(READ_ONLY_DATA(mcxnex_sw_ah_s::ah_udav))
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_sw_ah_s::ah_lock,
    mcxnex_sw_ah_s::ah_pdhdl
    mcxnex_sw_ah_s::ah_rsrcp
    mcxnex_sw_ah_s::ah_save_guid))

/*
 * The mcxnex_sw_mcg_list_s structure is also referred to using the
 * "mcxnex_mcghdl_t" typedef (see mcxnex_typedef.h).  It encodes all the
 * information necessary to track the various resources needed to for attaching
 * and detaching QP from multicast groups.
 *
 * The Mcxnex driver keeps an array of these and uses them as a shadow for
 * the real HW-based MCG table.  They hold all the necessary information
 * to track the resources and to allow fast access to the MCG table.  First,
 * it had a 128-bit multicast GID (stored in "mcg_mgid_h" and "mcg_mgid_l".
 * next if has a field to indicate the index of the next mcxnex_mcghdl_t in
 * the current hash chain (zero is the end of the chain).  Note: this very
 * closely mimics what the hardware MCG entry has. Then it has a field to
 * indicate how many QP are currently attached to the given MCG.  And, lastly,
 * it has the obligatory backpointer to the resource for the MCH handle
 * structure itself.
 */
struct mcxnex_sw_mcg_list_s {
	uint64_t	mcg_mgid_h;
	uint64_t	mcg_mgid_l;
	uint_t		mcg_next_indx;
	uint_t		mcg_num_qps;
	mcxnex_rsrc_t	*mcg_rsrcp;
};

/*
 * The mcxnex_sw_pd_s structure is also referred to using the "mcxnex_pdhdl_t"
 * typedef (see mcxnex_typedef.h).  It encodes all the information necessary
 * to track the various resources needed to allocate and free protection
 * domains
 *
 * Specifically, it has reference count and a lock to ensure single threaded
 * access to it.  It has a field for the protection domain number ("pd_pdnum").
 * And it also has the obligatory backpointer to the resource for the PD
 * handle structure itself.
 */
struct mcxnex_sw_pd_s {
	kmutex_t	pd_lock;
	uint32_t	pd_pdnum;
	uint32_t	pd_refcnt;
	mcxnex_rsrc_t	*pd_rsrcp;
};
_NOTE(READ_ONLY_DATA(mcxnex_sw_pd_s::pd_pdnum
    mcxnex_sw_pd_s::pd_rsrcp))
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_sw_pd_s::pd_lock,
    mcxnex_sw_pd_s::pd_refcnt))

/*
 * The mcxnex_qalloc_info_s structure is also referred to using the
 * "mcxnex_qalloc_info_t" typedef (see mcxnex_typedef.h).  It holds all the
 * information necessary to track the resources for each of the various Mcxnex
 * queue types (i.e. Event Queue, Completion Queue, Work Queue).
 *
 * Specifically, it has the size, alignment restrictions, and location (in DDR
 * or in system memory).  And depending on the location, it also has the
 * ddi_dma_handle_t, ddi_acc_handle_t, and pointers used for reading/writing to
 * the queue's memory.
 */
struct mcxnex_qalloc_info_s {
	uint64_t		qa_size;
	uint64_t		qa_alloc_align;
	uint64_t		qa_bind_align;
	uint32_t		*qa_buf_real;
	uint32_t		*qa_buf_aligned;
	uint64_t		qa_buf_realsz;
	uint_t			qa_pgoffs;
	uint_t			qa_location;
	ddi_dma_handle_t	qa_dmahdl;
	ddi_acc_handle_t	qa_acchdl;
	ddi_umem_cookie_t	qa_umemcookie;
};

/*
 * The mcxnex_ks_mask_t structure encodes all the information necessary for
 * the individual kstat entries.  The "ks_reg_offset" field contains the
 * hardware offset for the corresponding counter, and "ks_reg_shift" and
 * "ks_reg_mask" contain shift and mask registers used by the access routines.
 * Also the "ks_old_pic0" and "ks_old_pic1" fields contain the most recently
 * read value for the corresponding port ("pic").  Note:  An array of these
 * structures is part of the "mcxnex_ks_info_t" structure below.
 */
typedef struct mcxnex_ks_mask_s {
	char		*ks_evt_name;
	uint32_t	ks_old_pic0;
	uint32_t	ks_old_pic1;
} mcxnex_ks_mask_t;

/*
 * Index into the named data components of 64 bit "perf_counters" kstat.
 */
enum {
	MCXNEX_PERFCNTR64_ENABLE_IDX = 0,
	MCXNEX_PERFCNTR64_XMIT_DATA_IDX,
	MCXNEX_PERFCNTR64_RECV_DATA_IDX,
	MCXNEX_PERFCNTR64_XMIT_PKTS_IDX,
	MCXNEX_PERFCNTR64_RECV_PKTS_IDX,
	MCXNEX_PERFCNTR64_NUM_COUNTERS
};

/*
 * Data associated with the 64 bit "perf_counters" kstat. One for each port.
 */
typedef struct mcxnex_perfcntr64_ks_info_s {
	struct kstat	*hki64_ksp;
	int		hki64_ext_port_counters_supported;
	int		hki64_enabled;
	uint64_t	hki64_counters[MCXNEX_PERFCNTR64_NUM_COUNTERS];
	uint32_t	hki64_last_read[MCXNEX_PERFCNTR64_NUM_COUNTERS];
	uint_t		hki64_port_num;
	mcxnex_state_t	*hki64_state;
} mcxnex_perfcntr64_ks_info_t;

/*
 * The mcxnex_ks_info_t structure stores all the information necessary for
 * tracking the resources associated with each of the various kstats.  In
 * addition to containing pointers to each of the counter and pic kstats,
 * this structure also contains "hki_pcr" which is the control register that
 * determines which of the countable entries (from the "hki_ib_perfcnt[]"
 * array) is being currently accessed.
 */
typedef struct mcxnex_ks_info_s {
	struct kstat	*hki_cntr_ksp;
	struct kstat	*hki_picN_ksp[MCXNEX_MAX_PORTS];
	uint64_t	hki_pcr;
	uint64_t	hki_pic0;
	uint64_t	hki_pic1;
	mcxnex_ks_mask_t	hki_ib_perfcnt[MCXNEX_CNTR_NUMENTRIES];
	kt_did_t	hki_perfcntr64_thread_id;
	kmutex_t	hki_perfcntr64_lock;
	kcondvar_t	hki_perfcntr64_cv;
	uint_t		hki_perfcntr64_flags;	/* see below */
	mcxnex_perfcntr64_ks_info_t	hki_perfcntr64[MCXNEX_MAX_PORTS];
} mcxnex_ks_info_t;

/* hki_perfcntr64_flags */
#define	MCXNEX_PERFCNTR64_THREAD_CREATED	0x0001
#define	MCXNEX_PERFCNTR64_THREAD_EXIT		0x0002

/*
 * The mcxnex_ports_ioctl32_t, mcxnex_loopback_ioctl32_t, and
 * mcxnex_flash_ioctl32_s structures are used internally by the Mcxnex
 * driver to accomodate 32-bit applications which need to access the
 * Mcxnex ioctls.  They are 32-bit versions of externally available
 * structures defined in mcxnex_ioctl.h
 */
typedef struct mcxnex_ports_ioctl32_s {
	uint_t			ap_revision;
	caddr32_t		ap_ports;
	uint8_t			ap_num_ports;
} mcxnex_ports_ioctl32_t;

typedef struct mcxnex_loopback_ioctl32_s {
	uint_t			alb_revision;
	caddr32_t		alb_send_buf;
	caddr32_t		alb_fail_buf;
	uint_t			alb_buf_sz;
	uint_t			alb_num_iter;
	uint_t			alb_pass_done;
	uint_t			alb_timeout;
	mcxnex_loopback_error_t	alb_error_type;
	uint8_t			alb_port_num;
	uint8_t			alb_num_retry;
} mcxnex_loopback_ioctl32_t;

typedef struct mcxnex_flash_ioctl32_s {
	uint32_t	af_type;
	caddr32_t	af_sector;
	uint32_t	af_sector_num;
	uint32_t	af_addr;
	uint32_t	af_quadlet;
	uint8_t		af_byte;
} mcxnex_flash_ioctl32_t;

/*
 * The mcxnex_loopback_comm_t and mcxnex_loopback_state_t structures below
 * are used to store all of the relevant state information needed to keep
 * track of a single VTS ioctl loopback test run.
 */
typedef struct mcxnex_loopback_comm_s {
	uint8_t			*hlc_buf;
	size_t			hlc_buf_sz;
	ibt_mr_desc_t		hlc_mrdesc;

	mcxnex_mrhdl_t		hlc_mrhdl;
	mcxnex_cqhdl_t		hlc_cqhdl[2];
	mcxnex_qphdl_t		hlc_qp_hdl;

	ibt_mr_attr_t		hlc_memattr;
	uint_t			hlc_qp_num;
	ibt_cq_attr_t		hlc_cq_attr;
	ibt_qp_alloc_attr_t	hlc_qp_attr;
	ibt_chan_sizes_t	hlc_chan_sizes;
	ibt_qp_info_t		hlc_qp_info;
	ibt_queue_sizes_t	hlc_queue_sizes;
	ibt_send_wr_t		hlc_wr;
	ibt_wr_ds_t		hlc_sgl;
	ibt_wc_t		hlc_wc;
	uint_t			hlc_num_polled;
	ibt_status_t		hlc_status;
	int			hlc_complete;
	int			hlc_wrid;
} mcxnex_loopback_comm_t;

typedef struct mcxnex_loopback_state_s {
	uint8_t			hls_port;
	uint_t			hls_lid;
	uint8_t			hls_retry;
	mcxnex_state_t		*hls_state;
	ibc_hca_hdl_t		hls_hca_hdl;
	mcxnex_pdhdl_t		hls_pd_hdl;
	mcxnex_loopback_comm_t	hls_tx;
	mcxnex_loopback_comm_t	hls_rx;
	ibt_status_t		hls_status;
	int			hls_err;
	int			hls_pkey_ix;
	int			hls_timeout;
} mcxnex_loopback_state_t;

/*
 * Mellanox FMR
 */
typedef struct mcxnex_fmr_list_s {
	avl_node_t			fmr_avlnode;
	struct mcxnex_fmr_list_s		*fmr_next;

	mcxnex_mrhdl_t			fmr;
	ibt_pmr_desc_t			fmr_desc;
	mcxnex_fmrhdl_t			fmr_pool;
	uint_t				fmr_refcnt;
	uint_t				fmr_remaps;
	uint_t				fmr_in_cache;
} mcxnex_fmr_list_t;

struct mcxnex_sw_fmr_s {
	mcxnex_state_t			*fmr_state;

	kmutex_t			fmr_lock;
	ddi_taskq_t			*fmr_taskq;

	ibt_fmr_flush_handler_t		fmr_flush_function;
	void				*fmr_flush_arg;

	int				fmr_pool_size;
	int				fmr_max_pages;
	int				fmr_page_sz;
	int				fmr_dirty_watermark;
	int				fmr_dirty_len;
	int				fmr_flags;

	mcxnex_fmr_list_t		*fmr_free_list;
	mcxnex_fmr_list_t		*fmr_dirty_list;

	int				fmr_cache;
	avl_tree_t			fmr_cache_avl;
	kmutex_t			fmr_cachelock;
};
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_sw_fmr_s::fmr_lock,
    mcxnex_sw_fmr_s::fmr_state
    mcxnex_sw_fmr_s::fmr_pool_size
    mcxnex_sw_fmr_s::fmr_max_pages
    mcxnex_sw_fmr_s::fmr_page_sz
    mcxnex_sw_fmr_s::fmr_dirty_watermark
    mcxnex_sw_fmr_s::fmr_dirty_len
    mcxnex_sw_fmr_s::fmr_flags
    mcxnex_sw_fmr_s::fmr_free_list
    mcxnex_sw_fmr_s::fmr_dirty_list
    mcxnex_sw_fmr_s::fmr_cache))

_NOTE(MUTEX_PROTECTS_DATA(mcxnex_sw_fmr_s::fmr_cachelock,
    mcxnex_sw_fmr_s::fmr_cache_avl))

#define	MCXNEX_FMR_MAX_REMAPS		32

/* Mcxnex doorbell record routines */

int mcxnex_dbr_page_alloc(mcxnex_state_t *state, mcxnex_dbr_info_t **info);
int mcxnex_dbr_alloc(mcxnex_state_t *state, uint_t index,
    ddi_acc_handle_t *acchdl, mcxnex_dbr_t **vdbr, uint64_t *pdbr,
    uint64_t *mapoffset);
void mcxnex_dbr_free(mcxnex_state_t *state, uint_t indx, mcxnex_dbr_t *record);
void mcxnex_dbr_kern_free(mcxnex_state_t *state);

/* Mcxnex Fast Memory Registration Routines */
int mcxnex_create_fmr_pool(mcxnex_state_t *state, mcxnex_pdhdl_t pdhdl,
    ibt_fmr_pool_attr_t *params, mcxnex_fmrhdl_t *fmrhdl);
int mcxnex_destroy_fmr_pool(mcxnex_state_t *state, mcxnex_fmrhdl_t fmrhdl);
int mcxnex_flush_fmr_pool(mcxnex_state_t *state, mcxnex_fmrhdl_t fmrhdl);
int mcxnex_register_physical_fmr(mcxnex_state_t *state, mcxnex_fmrhdl_t fmrhdl,
    ibt_pmr_attr_t *mem_pattr_p, mcxnex_mrhdl_t *mrhdl,
    ibt_pmr_desc_t *mem_desc_p);
int mcxnex_deregister_fmr(mcxnex_state_t *state, mcxnex_mrhdl_t mr);


/* Mcxnex Address Handle routines */
int mcxnex_ah_alloc(mcxnex_state_t *state, mcxnex_pdhdl_t pd,
    ibt_adds_vect_t *attr_p, mcxnex_ahhdl_t *ahhdl, uint_t sleepflag);
int mcxnex_ah_free(mcxnex_state_t *state, mcxnex_ahhdl_t *ahhdl,
    uint_t sleepflag);
int mcxnex_ah_query(mcxnex_state_t *state, mcxnex_ahhdl_t ahhdl,
    mcxnex_pdhdl_t *pdhdl, ibt_adds_vect_t *attr_p);
int mcxnex_ah_modify(mcxnex_state_t *state, mcxnex_ahhdl_t ahhdl,
    ibt_adds_vect_t *attr_p);

/* Mcxnex Multicast Group routines */
int mcxnex_mcg_attach(mcxnex_state_t *state, mcxnex_qphdl_t qphdl, ib_gid_t gid,
    ib_lid_t lid);
int mcxnex_mcg_detach(mcxnex_state_t *state, mcxnex_qphdl_t qphdl, ib_gid_t gid,
    ib_lid_t lid);

/* Mcxnex Protection Domain routines */
int mcxnex_pd_alloc(mcxnex_state_t *state, mcxnex_pdhdl_t *pdhdl,
    uint_t sleepflag);
int mcxnex_pd_free(mcxnex_state_t *state, mcxnex_pdhdl_t *pdhdl);
void mcxnex_pd_refcnt_inc(mcxnex_pdhdl_t pd);
void mcxnex_pd_refcnt_dec(mcxnex_pdhdl_t pd);

/* Mcxnex port-related routines */
int mcxnex_port_query(mcxnex_state_t *state, uint_t port,
    ibt_hca_portinfo_t *pi);

/* Mcxnex statistics (kstat) routines */
int mcxnex_kstat_init(mcxnex_state_t *state);
void mcxnex_kstat_fini(mcxnex_state_t *state);

/* Miscellaneous routines */
int mcxnex_set_addr_path(mcxnex_state_t *state, ibt_adds_vect_t *av,
    mcxnex_hw_addr_path_t *path, uint_t type);
void mcxnex_get_addr_path(mcxnex_state_t *state, mcxnex_hw_addr_path_t *path,
    ibt_adds_vect_t *av, uint_t type);
int mcxnex_portnum_is_valid(mcxnex_state_t *state, uint_t portnum);
int mcxnex_pkeyindex_is_valid(mcxnex_state_t *state, uint_t pkeyindx);
int mcxnex_queue_alloc(mcxnex_state_t *state, mcxnex_qalloc_info_t *qa_info,
    uint_t sleepflag);
void mcxnex_queue_free(mcxnex_qalloc_info_t *qa_info);
int mcxnex_get_dma_cookies(mcxnex_state_t *state, ibt_phys_buf_t *paddr_list_p,
    ibt_va_attr_t *va_attrs, uint_t list_len, uint_t *cookiecnt,
    ibc_ma_hdl_t *ibc_ma_hdl_p);
int mcxnex_split_dma_cookies(mcxnex_state_t *state,
    ibt_phys_buf_t *paddr_list_p, ib_memlen_t *paddr_offset_p,
    uint_t list_len, uint_t *cookiecnt, uint_t pagesize);
int mcxnex_dma_cookie_shift(ibt_phys_buf_t *paddr_list, int start, int end,
    int cookiecnt, int num_shift);
int mcxnex_free_dma_cookies(ibc_ma_hdl_t ibc_ma_hdl);
int mcxnex_log2(long x);


#endif	/* _MCXNEX_MISC_H */
