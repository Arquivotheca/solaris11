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

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <libdllink.h>
#include <libdladm.h>
#include "dcbx_pfc.h"

/* Set default config from the local parameters */
static int
dcbx_pfc_setdcfg(dcbx_feature_t *dfp)
{
	lldp_pfc_t	*pfc = (lldp_pfc_t *)dfp->df_pvtdata;
	int		err;

	if ((err = lldp_add_pfc2nvlist(pfc, dfp->df_localparams)) != 0)
		syslog(LOG_ERR, "PFC: Error in setting default config");
	return (err);
}

/*
 * Check if the given PFC map is compatible with our operating config.
 * For two configs to be compatible, the PFC map (lp_enable) must
 * be the same, else they are incompatible.
 */
static boolean_t
dcbx_pfc_iscompatible(dcbx_feature_t *dcbx_pfc, nvlist_t *cfg)
{
	lldp_pfc_t	lpfc;
	lldp_pfc_t	npfc;

	if (lldp_nvlist2pfc(dcbx_pfc->df_opercfg, &lpfc) != 0)
		return (B_FALSE);

	if (lldp_nvlist2pfc(cfg, &npfc) != 0)
		return (B_FALSE);

	/*
	 * We don't allow setting the TC, so check the PFC map for
	 * compatibility
	 */
	if (lpfc.lp_enable != npfc.lp_enable)
		return (B_FALSE);

	return (B_TRUE);
}

/* Set the operational config to the one passed in cfg */
static int
dcbx_pfc_setcfg(dcbx_feature_t *dfp, nvlist_t *tcfg, nvlist_t *fcfg)
{
	lldp_pfc_t	fpfc;
	lldp_pfc_t	*pfc = (lldp_pfc_t *)dfp->df_pvtdata;
	int		err;

	if ((err = lldp_nvlist2pfc(fcfg, &fpfc)) != 0)
		return (err);

	/*
	 * The fcfg could be the peer's cfg, in which case we don't
	 * to copy over the peer's willing etc. into our operating
	 * config.
	 */
	fpfc.lp_willing = pfc->lp_willing;
	fpfc.lp_mbc = pfc->lp_mbc;
	fpfc.lp_cap = pfc->lp_cap;

	return (lldp_add_pfc2nvlist(&fpfc, tcfg));
}

/*
 * Feature-specific action. Change the PFC map per the operating config.
 * Device takes care of switching between PFC and non-PFC mode based
 * on PFC map.
 */
static int
dcbx_pfc_action(dcbx_feature_t *dfp)
{
	char		propval[DLADM_PROP_VAL_MAX];
	char		*valptr[1];
	uint_t		valcnt = 1;
	datalink_id_t	linkid = dfp->df_la->la_linkid;
	char		pfcval[DLADM_PROP_VAL_MAX];
	uint8_t		pfcmap = 0;
	char		*cp = pfcval;
	lldp_pfc_t	pfc;
	int		err;

	pfcval[0] = '\0';
	valptr[0] = propval;

	/* Get the PFC map */
	if (dladm_get_linkprop(dld_handle, linkid, DLADM_PROP_VAL_CURRENT,
	    "pfcmap", (char **)valptr, &valcnt) != DLADM_STATUS_OK) {
		syslog(LOG_WARNING, "dcbx_pfc_action: couldn't get map");
		return (ENOTSUP);
	}
	pfcmap = strtol(propval, NULL, 2);

	/* Get PFC from the operating configuration */
	if ((err = lldp_nvlist2pfc(dfp->df_opercfg, &pfc)) != 0) {
		syslog(LOG_WARNING, "dcbx_pfc_action: couldn't get opercfg");
		return (err);
	}
	/* Set the PFC map, if needed */
	if (pfcmap != pfc.lp_enable) {
		lldp_bitmap2str(pfc.lp_enable, pfcval, DLADM_PROP_VAL_MAX);
		if (dladm_set_linkprop(dld_handle, linkid, "pfcmap",
		    &cp, 1, DLADM_OPT_ACTIVE) != DLADM_STATUS_OK) {
			syslog(LOG_WARNING,
			    "dcbx_pfc_action: couldn't set pfcmap");
			return (ENOTSUP);
		}
	}
	return (0);
}

/* Process an incoming feature TLV */
static int
dcbx_pfc_process(dcbx_feature_t *dfp)
{
	lldp_agent_t	*lap = dfp->df_la;
	lldp_pfc_t	pfc;

	/* check for mutli-peer condition */
	dcbx_multi_peer(dfp);

	/*
	 * If we detected multi-peer condition or peer's information aged or
	 * peer was shutdown or has stopped sending PFC TLV, so we have
	 * to apply local config.
	 */
	if (dfp->df_mpeer_detected) {
		(void) nvlist_free(dfp->df_peercfg);
		dfp->df_peercfg = NULL;
		dfp->df_p_fnopresent = B_TRUE;
		return (0);
	}

	if (dfp->df_peercfg == NULL) {
		if (dfp->df_npeer == 0) {
			dfp->df_p_fnopresent = B_TRUE;
			return (0);
		}
		dcbx_get_feature_nvl(lap, dfp->df_ftype, &dfp->df_peercfg);
	}
	if (lldp_nvlist2pfc(dfp->df_peercfg, &pfc) != 0) {
		dfp->df_p_fnopresent = B_TRUE;
	} else {
		dfp->df_p_fnopresent = B_FALSE;
		dfp->df_p_willing = pfc.lp_willing;
	}
	return (0);
}

/* Set the willing property and update the local config */
/* ARGSUSED */
static int
dcbx_pfc_setprop(dcbx_feature_t *dfp, lldp_proptype_t ptype,
    void *val, uint32_t flags)
{
	lldp_pfc_t	*pfc = (lldp_pfc_t *)dfp->df_pvtdata;
	boolean_t	changed = B_FALSE;
	boolean_t	bool;
	int		err = 0;
	lldp_pfc_t	npfc;
	uint8_t		pval;
	lldp_pfc_t	opfc;

	bcopy(pfc, &npfc, sizeof (lldp_pfc_t));
	lldp_mutex_lock(&dfp->df_lock);
	switch (ptype) {
	case LLDP_PROPTYPE_WILLING:
		pval = *(uint32_t *)val;
		bool = (pval == 0 ? B_FALSE : B_TRUE);

		if (bool && (getzoneid() != GLOBAL_ZONEID)) {
			err = ENOTSUP;
			break;
		}

		if (dfp->df_willing == bool)
			break;
		dfp->df_willing = bool;
		npfc.lp_willing = bool;
		changed = B_TRUE;
		break;
	case LLDP_PROPTYPE_PFCMAP:
		pval = *(uint8_t *)val;
		/*
		 * We could be coming here after setting the operating
		 * config to the peer's
		 */
		if (lldp_nvlist2pfc(dfp->df_opercfg, &opfc) == 0) {
			if (opfc.lp_enable == pval)
				break;
		}
		if (pfc->lp_enable == pval)
			break;
		npfc.lp_enable = pval;
		changed = B_TRUE;
		break;
	default:
		err = EINVAL;
		break;
	}
	if (err == 0 && changed) {
		if ((err = lldp_add_pfc2nvlist(&npfc,
		    dfp->df_localparams)) != 0) {
			lldp_mutex_unlock(&dfp->df_lock);
			return (err);
		}
		/* Update the private data */
		bcopy(&npfc, pfc, sizeof (lldp_pfc_t));
		dfp->df_localparamchange = B_TRUE;
		/* Signal the state machine */
		(void) pthread_cond_signal(&dfp->df_condvar);
	}
	lldp_mutex_unlock(&dfp->df_lock);
	return (err);
}

/* Get the willing property value */
/* ARGSUSED */
static int
dcbx_pfc_getprop(dcbx_feature_t *dfp, lldp_proptype_t ptype,
    char *buf, uint_t bufsize)
{
	lldp_pfc_t	*pfc = (lldp_pfc_t *)dfp->df_pvtdata;
	int		err = 0;

	lldp_mutex_lock(&dfp->df_lock);
	switch (ptype) {
	case LLDP_PROPTYPE_WILLING:
		(void) snprintf(buf, bufsize, "%d", pfc->lp_willing);
		break;
	default:
		err = EINVAL;
		break;
	}
	lldp_mutex_unlock(&dfp->df_lock);
	return (err);
}

static void
dcbx_pfc_linkstate(dcbx_feature_t *dfp, boolean_t linkup)
{
	lldp_mutex_lock(&dfp->df_lock);
	dfp->df_linkup = linkup;
	(void) pthread_cond_signal(&dfp->df_condvar);
	lldp_mutex_unlock(&dfp->df_lock);
}

/* PFC feature cleanup. */
static void
dcbx_pfc_fini(dcbx_feature_t *dfp)
{
	lldp_agent_t	*lap = dfp->df_la;

	if (dfp->df_mpeer_toid != 0)
		(void) lldp_untimeout(dfp->df_mpeer_toid);

	dfp->df_mpeer_toid = 0;
	nvlist_free(dfp->df_localparams);
	nvlist_free(dfp->df_opercfg);
	nvlist_free(dfp->df_peercfg);
	free(dfp->df_pvtdata);
	(void) pthread_mutex_destroy(&dfp->df_lock);
	free(dfp);
	lldp_agent_refcnt_decr(lap);
}

/* PFC feature initialization */
/* ARGSUSED */
static int
dcbx_pfc_init(lldp_agent_t *lap, dcbx_feature_t **dfp, void *arg)
{
	pthread_attr_t	attr;
	lldp_pfc_t	*pfc;
	int		err;

	/* if LLDP is not enabled for both TX and RX, we return */
	if (lap->la_adminStatus != LLDP_MODE_RXTX)
		return (ENOTSUP);

	/* Check if the underlying NIC supports DCB */
	if (!lldpd_islink_indcb(lap->la_linkid))
		return (ENOTSUP);

	if ((*dfp = calloc(1, sizeof (dcbx_feature_t))) == NULL)
		return (ENOMEM);

	if ((pfc = calloc(1, sizeof (lldp_pfc_t))) == NULL) {
		free(*dfp);
		return (ENOMEM);
	}

	/* Get the PFC parameters for the given link */
	err = lldpd_link2pfcparam(lap->la_linkid, pfc);
	if (err != 0)
		goto fail;

	(*dfp)->df_ftype = DCBX_TYPE_PFC;
	(*dfp)->df_refcnt = 1;
	(*dfp)->df_setcfg = dcbx_pfc_setcfg;
	(*dfp)->df_setdcfg = dcbx_pfc_setdcfg;
	(*dfp)->df_iscompatible = dcbx_pfc_iscompatible;
	(*dfp)->df_process =  dcbx_pfc_process;
	(*dfp)->df_setprop = dcbx_pfc_setprop;
	(*dfp)->df_getprop = dcbx_pfc_getprop;
	(*dfp)->df_action = dcbx_pfc_action;
	(*dfp)->df_linkstate = dcbx_pfc_linkstate;
	(*dfp)->df_fini = dcbx_pfc_fini;
	(*dfp)->df_linkup = lap->la_portEnabled;
	(*dfp)->df_enabled = B_TRUE;
	(*dfp)->df_willing = pfc->lp_willing;
	(*dfp)->df_pending = B_TRUE;
	(*dfp)->df_p_fnopresent = B_TRUE;

	err = nvlist_alloc(&((*dfp)->df_localparams), NV_UNIQUE_NAME, 0);
	if (err != 0) {
		syslog(LOG_ERR, "Error allocing local param list..");
		goto fail;
	}

	err = nvlist_alloc(&((*dfp)->df_opercfg), NV_UNIQUE_NAME, 0);
	if (err != 0) {
		syslog(LOG_ERR, "Error allocing operational config..");
		goto fail;
	}

	lldp_agent_refcnt_incr(lap);
	(*dfp)->df_la = lap;
	(*dfp)->df_pvtdata = (void *)pfc;
	(void) pthread_mutex_init(&((*dfp)->df_lock), NULL);

	(void) pthread_attr_init(&attr);
	(void) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	(*dfp)->df_refcnt++;
	(void) pthread_create(&((*dfp)->df_fsm), &attr, dcbx_ssm, *dfp);
	(void) pthread_attr_destroy(&attr);

	return (0);
fail:
	free(pfc);
	nvlist_free((*dfp)->df_localparams);
	nvlist_free((*dfp)->df_opercfg);
	nvlist_free((*dfp)->df_peercfg);
	free(*dfp);
	*dfp = NULL;
	return (err);
}

/* PFC feature TLV initialization */
/* ARGSUSED */
int
dcbx_pfc_tlv_init(lldp_agent_t *lap, nvlist_t *nvl)
{
	dcbx_feature_t	*dfp;
	int		err;

	if ((err = dcbx_pfc_init(lap, &dfp, NULL)) != 0)
		return (err);

	(void) snprintf(dfp->df_subid, MAX_SUBID_LEN, "pfc-%d-%d",
	    lap->la_linkid, getpid());
	(void) dcbx_feature_refcnt_incr(dfp);
	if ((err = sysevent_evc_subscribe(lldpd_evchp, dfp->df_subid, EC_LLDP,
	    dcbx_handle_sysevents, dfp, 0)) != 0) {
		(void) dcbx_feature_refcnt_decr(dfp);
		/* shutdown the feature */
		dfp->df_enabled = B_FALSE;
		(void) pthread_cond_broadcast(&dfp->df_condvar);
		syslog(LOG_ERR, "Error subscribing to pfc events. "
		    "Cannot enable PFC.");
		return (err);
	}

	/*
	 * PFC state machine initialization was successful. Add
	 * the feature into the feature list.
	 */
	lldp_rw_lock(&lap->la_feature_rwlock, LLDP_RWLOCK_WRITER);
	list_insert_tail(&lap->la_features, dfp);
	lldp_rw_unlock(&lap->la_feature_rwlock);

	return (0);
}

/* PFC feature TLV cleanup */
void
dcbx_pfc_tlv_fini(lldp_agent_t *lap)
{
	dcbx_feature_t	*dfp;
	nvlist_t	*ouinvl = NULL;

	if ((dfp = dcbx_feature_get(lap, DCBX_TYPE_PFC)) == NULL)
		return;

	/* Revert back to the local config, if we had accepted the peer's */
	if (!dcbx_pfc_iscompatible(dfp, dfp->df_localparams)) {
		if (dcbx_pfc_setcfg(dfp, dfp->df_opercfg,
		    dfp->df_localparams) == 0) {
			(void) dfp->df_action(dfp);
		}
	}

	/* now remove the TLV from the local mib */
	if (lldp_get_nested_nvl(lap->la_local_mib, lap->la_msap,
	    LLDP_NVP_ORGANIZATION, LLDP_8021_OUI_LIST, &ouinvl) == 0) {
		(void) nvlist_remove(ouinvl, LLDP_NVP_PFC, DATA_TYPE_NVLIST);
	}

	/* disable the feature */
	dfp->df_enabled = B_FALSE;
	(void) pthread_cond_signal(&dfp->df_condvar);

	/* unsubscribe from the event channel */
	(void) sysevent_evc_unsubscribe(lldpd_evchp, dfp->df_subid);

	/* decrement the feature refcnt, since we unsubscribed */
	dcbx_feature_refcnt_decr(dfp);

	/* decrement the refcnt because of get operation */
	dcbx_feature_refcnt_decr(dfp);

	/* delete the feature from the features list */
	(void) dcbx_feature_delete(dfp);
}
