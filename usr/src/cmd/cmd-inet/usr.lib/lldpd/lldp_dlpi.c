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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stropts.h>
#include <stp_in.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <sys/dlpi.h>
#include <sys/pfmod.h>
#include "lldp_impl.h"
#include "dcbx_impl.h"

/*
 * Callback function registered with `dh' to receive notifications for any
 * of the registered event types.
 */
/* ARGSUSED */
static void
lldp_dlpi_notify(dlpi_handle_t dh, dlpi_notifyinfo_t *info, void *arg)
{
	lldp_agent_t	*lap = arg;
	dcbx_feature_t	*dfp;
	boolean_t	maxfz_changed = B_FALSE;
	dl_fc_info_t	fcinfo;

	switch (info->dni_note) {
	case DL_NOTE_SDU_SIZE:
		lap->la_maxfsz = info->dni_size;
		lldp_rw_lock(&lap->la_txmib_rwlock, LLDP_RWLOCK_WRITER);
		if (i_lldp_get_write2pdu_nolock(lap,
		    LLDP_8023_MAXFRAMESZ_TLVNAME) != NULL) {
			/* Maximum Frame Size TLV is enabled */
			if (lldp_add_maxfsz2nvlist(lap->la_maxfsz,
			    lap->la_local_mib) == 0)
				maxfz_changed = B_TRUE;
		}
		lldp_rw_unlock(&lap->la_txmib_rwlock);
		if (maxfz_changed)
			lldp_something_changed_local(lap);
		break;

	case DL_NOTE_LINK_DOWN:
		lap->la_portEnabled = B_FALSE;
		(void) pthread_cond_broadcast(&lap->la_rx_cv);
		(void) pthread_cond_broadcast(&lap->la_cond_var);

		/*
		 * also walk through the DCB feature list and inform the
		 * feature state machine about the link status.
		 */
		lldp_rw_lock(&lap->la_feature_rwlock, LLDP_RWLOCK_READER);
		for (dfp = list_head(&lap->la_features); dfp != NULL;
		    dfp = list_next(&lap->la_features, dfp)) {
			if (dfp->df_linkstate != NULL)
				dfp->df_linkstate(dfp, B_FALSE);
		}
		lldp_rw_unlock(&lap->la_feature_rwlock);
		break;

	case DL_NOTE_LINK_UP:
		lap->la_portEnabled = B_TRUE;
		(void) pthread_cond_broadcast(&lap->la_rx_cv);
		(void) pthread_cond_broadcast(&lap->la_cond_var);

		/*
		 * also walk through the DCB feature list and inform the
		 * feature state machine about the link status.
		 */
		lldp_rw_lock(&lap->la_feature_rwlock, LLDP_RWLOCK_READER);
		for (dfp = list_head(&lap->la_features); dfp != NULL;
		    dfp = list_next(&lap->la_features, dfp)) {
			if (dfp->df_linkstate != NULL)
				dfp->df_linkstate(dfp, B_TRUE);
		}
		lldp_rw_unlock(&lap->la_feature_rwlock);
		break;

	case DL_NOTE_FC_MODE:
		fcinfo.dfi_mode = info->dni_fc_mode;
		fcinfo.dfi_pfc = info->dni_fc_pfc;
		fcinfo.dfi_ntc = info->dni_fc_ntc;
		dcbx_fc_notify(lap, &fcinfo);
		break;
	}
}

static void *
lldp_dlpi_close(void *arg)
{
	lldp_agent_t	*lap = arg;

	dlpi_close(lap->la_dh);
	lap->la_dh = NULL;
	lldp_agent_refcnt_decr(lap);
	return (NULL);
}

/*
 * This thread is created for every port on which LLDP in enabled and
 * is killed whenever the LLDP agent is disabled.
 */
/* ARGSUSED */
static void *
lldp_port_monitor(void *arg)
{
	lldp_agent_t	*lap = arg;

	pthread_cleanup_push(lldp_dlpi_close, arg);
	for (;;) {
		(void) dlpi_recv(lap->la_dh, NULL, NULL, NULL, NULL, -1,
		    NULL);
	}

	/* NOTREACHED */
	pthread_cleanup_pop(0);
	return (NULL);
}

/*
 * Open the DLPI provider and enable notifications for any change in link state,
 * SDU size, MAC address and flow control mode of the link.
 */
boolean_t
lldp_dlpi(lldp_agent_t *lap)
{
	pthread_attr_t	attr;
	int		rc;
	size_t		alen;

	rc = dlpi_open(lap->la_linkname, &lap->la_dh, DLPI_RAW);
	if (rc != DLPI_SUCCESS) {
		lap->la_dh = NULL;
		syslog(LOG_ERR, "can't open %s: %s", lap->la_linkname,
		    dlpi_strerror(rc));
		return (B_FALSE);
	}
	lldp_agent_refcnt_incr(lap);
	if ((rc = dlpi_enabnotify(lap->la_dh,
	    DL_NOTE_PHYS_ADDR | DL_NOTE_LINK_DOWN | DL_NOTE_LINK_UP |
	    DL_NOTE_FC_MODE | DL_NOTE_SDU_SIZE, lldp_dlpi_notify, lap,
	    &lap->la_notify_id)) != DLPI_SUCCESS) {
		syslog(LOG_ERR, "Could not enable DLPI notification on %s: %s",
		    lap->la_linkname,  dlpi_strerror(rc));
		goto fail;
	}

	if (lap->la_aggr_linkid == DATALINK_INVALID_LINKID) {
		alen = sizeof (lap->la_physaddr);
		rc = dlpi_get_physaddr(lap->la_dh, DL_CURR_PHYS_ADDR,
		    lap->la_physaddr, &alen);
		if (rc != DLPI_SUCCESS) {
			syslog(LOG_ERR, "dlpi_get_physaddr failed: %s: %s",
			    lap->la_linkname, dlpi_strerror(rc));
			goto fail;
		} else if (alen != ETHERADDRL) {
			syslog(LOG_ERR, "Bad length MAC address: %s: %d",
			    lap->la_linkname, alen);
			goto fail;
		}
		lap->la_physaddrlen = alen;
	}

	(void) pthread_attr_init(&attr);
	(void) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	rc = pthread_create(&lap->la_portmonitor, &attr, lldp_port_monitor,
	    lap);
	(void) pthread_attr_destroy(&attr);
	if (rc == 0)
		return (B_TRUE);
fail:
	dlpi_close(lap->la_dh);
	lap->la_dh = NULL;
	lldp_agent_refcnt_decr(lap);
	return (B_FALSE);
}
