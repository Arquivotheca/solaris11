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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _DCBX_IMPL_H
#define	_DCBX_IMPL_H

/*
 * Block comment that describes the contents of this file.
 */
#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <door.h>
#include <libnvpair.h>
#include <lldp.h>
#include <liblldp.h>
#include <liblldp_lldpd.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/dlpi.h>
#include <sys/mac.h>
#include <sys/sysmacros.h>
#include <sys/ethernet.h>
#include <sys/list.h>
#include "lldp_impl.h"

/* Feature State Machine states */
typedef enum {
	DCBX_FSM_WAIT,
	DCBX_FSM_SETLOCALPARAM,
	DCBX_FSM_GETPEERCFG,
	DCBX_FSM_USELOCALCFG,
	DCBX_FSM_USEPEERCFG,
	DCBX_FSM_LINKDOWN,
	DCBX_FSM_SHUTDOWN
} lldp_dcbx_ssm_state_t;
/*
 * Stats per DCBX Feature.
 */
typedef struct dcbx_feature_stats_s {
	uint32_t	dfs_stats_FramesInErrorsTotal;
	uint32_t	dfs_stats_FramesInTotal;
	uint32_t	dfs_stats_FramesOutTotal;
} dcbx_feature_stats_t;

/* DCB Feature parameters */
typedef struct dcbx_feature_s {
	list_node_t		df_node;
	uint_t			df_ftype;	/* Feature Type */
	uint32_t		df_refcnt;	/* # of refs */
	lldp_dcbx_ssm_state_t	df_state;	/* State of the SM */
	boolean_t		df_enabled;
	boolean_t		df_localparamchange; /* Local Params changed */
	boolean_t		df_linkup;
	boolean_t		df_pending;	/* Exchange in progress */

	/* Local feature-specific info for this feature */
	void			*df_pvtdata;

	/* DCBX multi-peer detection */
	uint32_t		df_mpeer_toid;	/* timeout id */
	uint32_t		df_npeer;	/* number of peers */
	boolean_t		df_mpeer_detected;

	/* DCBX update from peer */
	boolean_t		df_p_dcbxupdate;

	/* Feature not present in peer's DCBX TLV  */
	boolean_t		df_p_fnopresent;

	nvlist_t		*df_localparams; /* Local config */
	nvlist_t		*df_opercfg;    /* Operating config */
	nvlist_t		*df_peercfg; 	/* Peer config */

	/* willing to accept peer's cfg? */
	boolean_t		df_willing;
	/* peer willing to accept our cfg? */
	boolean_t		df_p_willing;

	lldp_agent_t		*df_la;

	/* subscriber ID for the event channel */
	char			df_subid[MAX_SUBID_LEN];

	/* cfg compatible? */
	boolean_t		(*df_iscompatible)(struct dcbx_feature_s *,
				    nvlist_t *);

	/* Set default configuration */
	int			(*df_setdcfg)(struct dcbx_feature_s *);

	/* Set configuration */
	int			(*df_setcfg)(struct dcbx_feature_s *,
				    nvlist_t *, nvlist_t *);

	/* Read cfg from TLV */
	int			(*df_process)(struct dcbx_feature_s *);

	int			(*df_setprop) (struct dcbx_feature_s *,
				    lldp_proptype_t, void *, uint32_t);

	int			(*df_getprop) (struct dcbx_feature_s *,
				    lldp_proptype_t, char *, uint_t);

	/* optional action routine */
	int			(*df_action) (struct dcbx_feature_s *);

	/* datalink state */
	void			(*df_linkstate) (struct dcbx_feature_s *,
				    boolean_t);

	/* cleanup routine */
	void			(*df_fini) (struct dcbx_feature_s *);

	pthread_t		df_fsm;	/* state machine */
	pthread_mutex_t		df_lock;
	pthread_cond_t		df_condvar;

	dcbx_feature_stats_t	df_stats;
} dcbx_feature_t;

extern int		i_lldpd_set_dcbx_prop(datalink_id_t, uint_t,
			    lldp_proptype_t, uint8_t, uint32_t);
extern void 		*dcbx_ssm(void *);
extern int		dcbx_handle_sysevents(sysevent_t *, void *);
extern void		dcbx_multi_peer(dcbx_feature_t *);
extern void		dcbx_something_changed_local(dcbx_feature_t *);
extern int		dcbx_get_tlv(nvlist_t *, uint8_t, uint8_t, void *);
extern dcbx_feature_t	*dcbx_feature_get(lldp_agent_t *, int);
extern int		dcbx_feature_delete(dcbx_feature_t *);
extern void		dcbx_feature_refcnt_incr(dcbx_feature_t *);
extern void		dcbx_feature_refcnt_decr(dcbx_feature_t *);
extern int		dcbx_set_prop(lldp_agent_t *, lldp_propclass_t,
			    lldp_proptype_t, void *, uint32_t);
extern int		dcbx_get_prop(lldp_agent_t *, lldp_propclass_t,
			    lldp_proptype_t, char *, uint_t);
extern void		dcbx_fc_notify(lldp_agent_t *, dl_fc_info_t *);
extern char		*dcbx_type2eventsc(dcbx_feature_t *);
extern boolean_t	lldpd_islink_indcb(datalink_id_t);
extern void		dcbx_get_feature_nvl(lldp_agent_t *, uint_t,
			    nvlist_t **);

#ifdef __cplusplus
}
#endif

#endif /* _DCBX_IMPL_H */
