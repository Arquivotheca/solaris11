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

#ifndef	_SYS_DAMAP_IMPL_H
#define	_SYS_DAMAP_IMPL_H

#include <sys/isa_defs.h>
#include <sys/dditypes.h>
#include <sys/time.h>
#include <sys/cmn_err.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddi_implfuncs.h>
#include <sys/ddi_isa.h>
#include <sys/model.h>
#include <sys/devctl.h>
#include <sys/nvpair.h>
#include <sys/sysevent.h>
#include <sys/bitset.h>
#include <sys/sdt.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This is the default low-water/threshold value.  During an error window,
 * if a device did not have an average of at least damap_iops_threshold
 * I/Os per second, the DAM will not make a determination of whether to
 * unconfigure based on jitter or transport errors.
 */
#define	DAMAP_DEFAULT_IOPS_THRESHOLD	100

typedef struct dam dam_t;

/*
 * activate_cb:		Provider callback when reported address is activated
 * deactivate_cb:	Provider callback when address has been released
 *
 * configure_cb:	Class callout to configure newly activated addresses
 * unconfig_cb:		Class callout to unconfigure deactivated addresses
 */
typedef void (*activate_cb_t)(void *, char *addr, int idx, void **privp);
typedef void (*deactivate_cb_t)(void *, char *addr, int idx, void *priv,
    damap_deact_rsn_t deact_rsn);

typedef int (*configure_cb_t)(void *, dam_t *mapp, id_t map_id,
    damap_config_rsn_t);
typedef int (*unconfig_cb_t)(void *, dam_t *mapp, id_t map_id);

/*
 * A few notes about the bitset_t values in the dam structure.
 *
 * There are several bitsets used in the DAM to maintain state with regard
 * to each address.
 *
 * The "<addr>::damap" dcmd will show a given address' existence in each bitset.
 * For example:
 *
 * #: address              [ACUSRX] ref config-private   provider-private
 * 5: w5000c5000940f695    [A.....] 3   ffffff0403ef0600 0000000000000000
 *    ffffff0408e85040::print -ta dam_da_t
 *
 * This address is currently a member of only one bitset, the active set.
 *
 * The meanings of each bitset are:
 *
 * A : active_set
 * The active bitset contains all addresses that have passed the stabilization
 * time and were successfully configured. Under normal operation, all addresses
 * shown by ::damap would only exist in this set.
 *
 * C : cfg_set
 * The configuring bitset contains any addresses that have been reported into
 * the map and have passed stabilization but have not yet completed the
 * configure process.
 *
 * U : uncfg_set
 * The unconfiguring bitset contains those addresses that were in the active
 * set, but were not present in the last set of addresses provided to the
 * DAM.  The stabilization window has closed, but these addresses have not
 * yet completed the unconfigure process.
 *
 * S : stable_set
 * The stable bitset is computed during stabilization and contains what will
 * eventually become the new active set, assuming everything that needs to
 * configured or unconfigured happens successfully.
 *
 * R : report_set
 * The report bitset contains the list of addresses that were reported in
 * by clients prior to the closing of the stabilization window.  During
 * the stabilization callback, the report set becomes the stable set.
 *
 * X : xpterr_set
 * The transport error bitset contains the list of addresses that have
 * exceeded the given threshold for either jitter or I/Os with transport
 * errors.  During stabilization, any addresses in this bitset will not be
 * added to the stable set.
 */

struct dam {
	char		*dam_name;
	int		dam_flags;		/* map state and cv flags */
	int		dam_options;		/* map options */
	int		dam_rptmode;		/* report mode */
	clock_t		dam_stable_ticks;	/* stabilization */
	clock_t		dam_error_ticks;	/* size of error window */
	int		dam_error_thresh;	/* Error threshold */
	int		dam_error_iops_thresh;	/* Minimum IOPs in window */
	uint_t		dam_size;		/* max index for addr hash */
	id_t		dam_high;		/* highest index allocated */
	timeout_id_t	dam_tid;		/* timeout(9F) ID */

	void		*dam_activate_arg;	/* activation private */
	activate_cb_t	dam_activate_cb;	/* activation callback */
	deactivate_cb_t	dam_deactivate_cb;	/* deactivation callback */

	void		*dam_config_arg;	/* config-private */
	configure_cb_t	dam_configure_cb;	/* configure callout */
	unconfig_cb_t	dam_unconfig_cb;	/* unconfigure callout */

	ddi_strid	*dam_addr_hash;		/* addresss to ID hash */
	bitset_t	dam_active_set;		/* activated address set */
	bitset_t	dam_cfg_set;		/* addresses to be configured */
	bitset_t	dam_uncfg_set;		/* addresses to be uncfged */
	bitset_t	dam_stable_set;		/* stable address set */
	bitset_t	dam_report_set;		/* reported address set */
	bitset_t	dam_xpterr_set;		/* xport error address set */
	void		*dam_da;		/* per-address soft state */
	hrtime_t	dam_last_update;	/* last map update */
	hrtime_t	dam_last_stable;	/* last map stable */
	int		dam_stable_cnt;		/* # of times map stabilized */
	int		dam_stable_overrun;
	kcondvar_t	dam_sync_cv;
	kmutex_t	dam_lock;
	kstat_t		*dam_kstatsp;
	int		dam_sync_to_cnt;
	int		dam_sync_reason;
};

#define	DAM_SPEND		0x10	/* stable pending */
#define	DAM_DESTROYPEND		0x20	/* in process of being destroyed */
#define	DAM_SETADD		0x100	/* fullset update pending */

#define	DAM_SYNC_RSN_FLAGS	0x01
#define	DAM_SYNC_RSN_REPORT_SET	0x02
#define	DAM_SYNC_RSN_TID	0x04

/*
 * per address softstate stucture
 */
typedef struct {
	uint_t		da_flags;	/* flags */
	int		da_jitter;	/* address re-report count */
	int64_t		da_err_wclose;	/* Close of error window */
	uint32_t	da_io_wnxpe;	/* Success I/Os count at window open */
	uint32_t	da_io_wxpe;	/* Xport err I/O count at window open */
	int		da_ref;		/* refcount on address */
	void		*da_ppriv;	/* stable provider private */
	void		*da_cfg_priv;	/* config/unconfig private */
	nvlist_t	*da_nvl;	/* stable nvlist */
	void		*da_ppriv_rpt;	/* reported provider-private */
	nvlist_t	*da_nvl_rpt;	/* reported nvlist */
	int64_t		da_deadline;	/* ddi_get_lbolt64 value when stable */
	hrtime_t	da_last_report;	/* timestamp of last report */
	int		da_report_cnt;	/* # of times address reported */
	hrtime_t	da_last_stable;	/* timestamp of last stable address */
	int		da_stable_cnt;	/* # of times address has stabilized */
	char		*da_addr;	/* string in dam_addr_hash (for mdb) */
	uint32_t	da_io_nxpe;	/* I/Os w/out transport error */
	uint32_t	da_io_xpe;	/* I/Os w/ transport error */
} dam_da_t;

/*
 * dam_da_t.da_flags
 */
#define	DA_INIT			0x1	/* address initizized */
#define	DA_FAILED_CONFIG	0x2	/* address failed configure */
#define	DA_RELE			0x4	/* adddress released */


/*
 * report type
 */
#define	RPT_ADDR_ADD		0
#define	RPT_ADDR_DEL		1

#define	DAM_IN_REPORT(m, i)	(bitset_in_set(&(m)->dam_report_set, (i)))
#define	DAM_IS_STABLE(m, i)	(bitset_in_set(&(m)->dam_active_set, (i)))
#define	DAM_XPE_EXCEEDED(m, i)	(bitset_in_set(&(m)->dam_xpterr_set, (i)))
#define	DAM_IN_UNCFG(m, i)	(bitset_in_set(&(m)->dam_uncfg_set, (i)))
#define	DAM_IN_CFG(m, i)	(bitset_in_set(&(m)->dam_cfg_set, (i)))

/*
 * DAM statistics
 */
struct dam_kstats {
	struct kstat_named dam_cycles;
	struct kstat_named dam_overrun;
	struct kstat_named dam_jitter;
	struct kstat_named dam_active;
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DAMAP_IMPL_H */
