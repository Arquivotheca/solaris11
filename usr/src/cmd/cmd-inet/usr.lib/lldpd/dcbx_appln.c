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
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <libfcoe.h>
#include "dcbx_appln.h"

/*
 * Set the operating priority on the FCoE client. When we support
 * actions for multiple applications, we need to handle it here.
 */
static int
dcbx_appln_action(dcbx_feature_t *dfp)
{
	FCOE_PORT_PROP	fpp;
	FCOE_STATUS	status;
	uint8_t		pri;
	int		err;
	lldp_agent_t	*lap = dfp->df_la;

	/* Get the FCoE priority from the operating config */
	if ((err = lldp_nvlist2fcoepri(dfp->df_opercfg, &pri)) != 0)
		return (err);

	/* Check if we can get privs to make change the FCoE priority */
	if (priv_set(PRIV_ON, PRIV_EFFECTIVE, PRIV_FILE_DAC_READ,
	    PRIV_SYS_DEVICES, NULL) < 0) {
		return (EPERM);
	}
	bzero(&fpp, sizeof (fpp));
	(void) FCOE_Set_Priority(&fpp, pri);
	status = FCOE_SetpropPort((FCOE_UINT8 *)lap->la_linkname, &fpp);
	if (status != FCOE_STATUS_OK) {
		switch (status) {
		case FCOE_STATUS_ERROR_MAC_NOT_FOUND:
			err = ENXIO;
			break;
		case FCOE_STATUS_ERROR_PERM:
			err = EPERM;
			break;
		default:
			err = EINVAL;
			break;
		}
	}

	(void) priv_set(PRIV_OFF, PRIV_EFFECTIVE, PRIV_FILE_DAC_READ,
	    PRIV_SYS_DEVICES, NULL);
	return (err);
}

/*
 * Set the operating configuration for all the applications. We set
 * the information from the peer's cfg if the peer supports this
 * application and we are willing to accept the peer's config. Else,
 * we set it to the local configuration.
 */
static int
dcbx_appln_apply_ocfg(dcbx_feature_t *dfp, lldp_appln_t *lappln,
    lldp_appln_t *pappln)
{
	lldp_appln_t	*app;
	nvlist_t	*envl;
	int		err;

	/*
	 * If the peer doesn't support this application or if we are not
	 * willing to accept the peer's config, set our local config as
	 * the operating config.
	 */
	if (pappln == NULL || !dfp->df_willing) {
		/* if there is no local configuration then return */
		if (lappln == NULL)
			return (0);
		err = lldp_add_appln2nvlist(lappln, 1, dfp->df_opercfg);
		if (err != 0)
			return (err);
		app = lappln;
	} else {
		/* Set the operating config to that of the peer */
		err = lldp_add_appln2nvlist(pappln, 1, dfp->df_opercfg);
		if (err != 0)
			return (err);
		app = pappln;
	}

	/*
	 * Send an event with this applications's config.
	 */
	if (nvlist_alloc(&envl, NV_UNIQUE_NAME, 0) == 0) {
		if ((err = lldp_add_appln2nvlist(app, 1, envl)) == 0) {
			dcbx_post_event(dfp->df_la, dcbx_type2eventsc(dfp),
			    LLDP_OPER_CFG, envl);
		}
		nvlist_free(envl);
	}
	return (err);
}

static int
dcbx_nvpair2appln(nvpair_t *nvp, lldp_appln_t *appln)
{
	char	idstr[LLDP_STRSIZE], *selstr;

	bzero(appln, sizeof (lldp_appln_t));
	(void) strlcpy(idstr, nvpair_name(nvp), sizeof (idstr));
	if ((selstr = strchr(idstr, '_')) == NULL)
		return (EINVAL);
	*selstr++ = '\0';
	appln->la_id = atoi(idstr);
	appln->la_sel = atoi(selstr);
	return (nvpair_value_uint8(nvp, &appln->la_pri));
}

/*
 * For each of the applications, set the operating config to its local
 * config or the peer's config.
 */
static int
dcbx_appln_set_opercfg(dcbx_feature_t *dfp)
{
	lldp_appln_t	lappln, pappln, *app;
	nvlist_t	*lnvl = NULL, *pnvl = NULL;
	nvpair_t	*nvp;
	char		*name, fcoe[LLDP_STRSIZE];
	int		err = 0;

	/* delete all the current entries in the operating config */
	(void) lldp_del_nested_nvl(dfp->df_opercfg, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_APPLN);

	/*
	 * retrieve the local application inforamtion. It's ok if we fail,
	 * the `lnvl' will be set to NULL.
	 */
	(void) lldp_get_nested_nvl(dfp->df_localparams, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_APPLN, &lnvl);

	/*
	 * retrieve the peers application information. It's ok if we fail,
	 * the `lnvl' will be set to NULL.
	 */
	(void) lldp_get_nested_nvl(dfp->df_peercfg, LLDP_NVP_ORGANIZATION,
	    LLDP_8021_OUI_LIST, LLDP_NVP_APPLN, &pnvl);

	/*
	 * Walk through all the local application configuration and see if
	 * there is any information from the peer for that application. If
	 * there is none then peer stopped advertising for that application.
	 */
	for (nvp = nvlist_next_nvpair(lnvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(lnvl, nvp)) {
		name = nvpair_name(nvp);
		if ((err = dcbx_nvpair2appln(nvp, &lappln)) != 0)
			break;
		if (!nvlist_exists(pnvl, name)) {
			/*
			 * interesting enough, FCOE can be advertised with any
			 * of the following two IDs.
			 */
			if (lappln.la_id == DCBX_FCOE_APPLICATION_ID1) {
				(void) snprintf(fcoe, sizeof (fcoe), "%u_%u",
				    DCBX_FCOE_APPLICATION_ID2,
				    DCBX_FCOE_APPLICATION_SF);
				name = fcoe;
			} else if (lappln.la_id == DCBX_FCOE_APPLICATION_ID2) {
				(void) snprintf(fcoe, sizeof (fcoe), "%u_%u",
				    DCBX_FCOE_APPLICATION_ID1,
				    DCBX_FCOE_APPLICATION_SF);
				name = fcoe;
			}
			app = NULL;
		}
		if (nvlist_exists(pnvl, name)) {
			if ((err = dcbx_nvpair2appln(nvp, &pappln)) != 0)
				break;
			app = &pappln;
		}
		if ((err = dcbx_appln_apply_ocfg(dfp, &lappln, app)) != 0)
			break;
	}
	if (err != 0)
		return (err);

	/* peer might have sent additional application TLVs */
	for (nvp = nvlist_next_nvpair(pnvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(pnvl, nvp)) {
		/* we already handled this case in the previous loop */
		if (nvlist_exists(lnvl, nvpair_name(nvp)))
			continue;
		if ((err = dcbx_nvpair2appln(nvp, &pappln)) != 0)
			break;
		if ((err = dcbx_appln_apply_ocfg(dfp, NULL, &pappln)) != 0)
			break;
	}

	/*
	 * Execute action, if any, as a result of the operating configuration
	 * change.
	 */
	if (err == 0 && dfp->df_action != NULL) {
		if (dfp->df_action(dfp) != 0) {
			syslog(LOG_WARNING, "dcbx_appln_set_opercfg: Error "
			    "executing action for application");
		}
	}

	return (err);
}

/* Process an incoming application feature TLV */
static int
dcbx_appln_process(dcbx_feature_t *dfp)
{
	lldp_agent_t	*lap = dfp->df_la;
	int		err;

	/*
	 * Check for mutli-peer condition and if there are multiple DCBX peers
	 * then add `dcbx_appln_handle_multipeer` timeout.
	 */
	dcbx_multi_peer(dfp);


	/*
	 * If we detected multi-peer condition or peer's information aged or
	 * peer was shutdown or has stopped sending application TLV, then we
	 * have to apply local config.
	 */
	if (dfp->df_mpeer_detected) {
		nvlist_free(dfp->df_peercfg);
		dfp->df_peercfg = NULL;
	} else if (dfp->df_peercfg == NULL) {
		if (dfp->df_npeer == 0) {
			nvlist_free(dfp->df_peercfg);
			dfp->df_peercfg = NULL;
		} else {
			dcbx_get_feature_nvl(lap, dfp->df_ftype,
			    &dfp->df_peercfg);
		}
	}

	err = dcbx_appln_set_opercfg(dfp);
	return (err);
}

static int
dcbx_appstr2nvlist(const char *appstr, nvlist_t **anvl)
{
	int		err;
	nvlist_t	*nvl;
	nvpair_t	*nvp;
	lldp_appln_t	appln;
	char		idstr[LLDP_MAXPROPVALLEN], *selstr, *pristr, *endp;
	unsigned long	ul;

	*anvl = NULL;
	if ((err = lldp_str2nvlist(appstr, &nvl, B_FALSE)) != 0)
		return (err);
	if ((err = nvlist_alloc(anvl, NV_UNIQUE_NAME, 0)) != 0) {
		nvlist_free(nvl);
		return (err);
	}
	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		/* idstr is of the form <id>/<sel>/<priority> */
		(void) strlcpy(idstr, nvpair_name(nvp), sizeof (idstr));
		/* set up error for failure */
		err = EINVAL;
		if ((selstr = strchr(idstr, '/')) == NULL)
			goto ret;
		*selstr++ = '\0';
		if ((pristr = strchr(selstr, '/')) == NULL)
			goto ret;
		*pristr++ = '\0';
		errno = 0;
		ul = strtoul(idstr, &endp, 16);
		if (errno != 0 || *endp != '\0' || ul > USHRT_MAX)
			goto ret;
		appln.la_id = ul;
		ul = strtoul(selstr, &endp, 10);
		if (errno != 0 || *endp != '\0' || ul > UCHAR_MAX)
			goto ret;
		appln.la_sel = ul;
		ul = strtoul(pristr, &endp, 10);
		if (errno != 0 || *endp != '\0' || ul > UCHAR_MAX)
			goto ret;
		appln.la_pri = ul;
		if ((err = lldp_add_appln2nvlist(&appln, 1, *anvl)) != 0)
			goto ret;
	}
	nvlist_free(nvl);
	return (0);
ret:
	nvlist_free(nvl);
	nvlist_free(*anvl);
	*anvl = NULL;
	return (err);
}

/*
 * Set the parameters for the Application TLV. This includes adding,
 * removing entries from the Application Priority Table, and indicating
 * whether we should accept the peer's config for all the applications
 * supported.
 */
/* ARGSUSED */
static int
dcbx_appln_setprop(dcbx_feature_t *dfp, lldp_proptype_t ptype,
    void *val, uint32_t flags)
{
	boolean_t	changed = B_FALSE;
	boolean_t	bool;
	nvlist_t	*nvl = NULL, *nnvl = NULL, *cnvl = NULL;
	nvpair_t	*nvp;
	int		err = 0, cerr;

	(void) pthread_mutex_lock(&dfp->df_lock);
	switch (ptype) {
	case LLDP_PROPTYPE_APPLN:
		if (flags & LLDP_OPT_DEFAULT) {
			/* we remove all the current application priorities */
			err = lldp_del_nested_nvl(dfp->df_localparams,
			    LLDP_NVP_ORGANIZATION, LLDP_8021_OUI_LIST,
			    LLDP_NVP_APPLN);
			if (err == 0)
				changed = B_TRUE;
			else if (err != 0 && err == ENOENT)
				err = 0;
			break;
		}
		if ((err = dcbx_appstr2nvlist(val, &nvl)) != 0)
			break;
		/* get the new nvl */
		err = lldp_get_nested_nvl(nvl, LLDP_NVP_ORGANIZATION,
		    LLDP_8021_OUI_LIST, LLDP_NVP_APPLN, &nnvl);
		assert(err == 0);

		/* get the current nvl */
		cerr = lldp_get_nested_nvl(dfp->df_localparams,
		    LLDP_NVP_ORGANIZATION, LLDP_8021_OUI_LIST, LLDP_NVP_APPLN,
		    &cnvl);

		/*
		 * check if we are trying to remove a non-existing value or
		 * adding an existing value.
		 */
		if (flags & (LLDP_OPT_REMOVE|LLDP_OPT_APPEND)) {
			char	*name;

			for (nvp = nvlist_next_nvpair(nnvl, NULL); nvp != NULL;
			    nvp = nvlist_next_nvpair(nnvl, nvp)) {
				name = nvpair_name(nvp);
				if ((flags & LLDP_OPT_REMOVE) &&
				    !nvlist_exists(cnvl, name)) {
					err = ENOENT;
					break;
				} else if ((flags & LLDP_OPT_APPEND) &&
				    nvlist_exists(cnvl, name)) {
					uint8_t npri, cpri;

					err = nvpair_value_uint8(nvp, &npri);
					if (err == 0) {
						err = nvlist_lookup_uint8(cnvl,
						    name, &cpri);
					}
					if (err != 0)
						break;
					if (cpri == npri) {
						err = EEXIST;
						break;
					}
				}
			}
			if (err != 0) {
				nvlist_free(nvl);
				break;
			}
		}
		if (flags & LLDP_OPT_REMOVE) {
			for (nvp = nvlist_next_nvpair(nnvl, NULL); nvp != NULL;
			    nvp = nvlist_next_nvpair(nnvl, nvp)) {
				(void) nvlist_remove(cnvl, nvpair_name(nvp),
				    nvpair_type(nvp));
			}
		} else {
			/* append or active case */
			if ((flags & LLDP_OPT_ACTIVE) && cerr == 0) {
				/*
				 * we remove all the current application
				 * priorities and assign only the new
				 * application priorities.
				 */
				(void) lldp_del_nested_nvl(dfp->df_localparams,
				    LLDP_NVP_ORGANIZATION, LLDP_8021_OUI_LIST,
				    LLDP_NVP_APPLN);
			}
			err = lldp_merge_nested_nvl(dfp->df_localparams, nvl,
			    LLDP_NVP_ORGANIZATION, LLDP_8021_OUI_LIST,
			    LLDP_NVP_APPLN);
		}
		if (err == 0)
			changed = B_TRUE;
		nvlist_free(nvl);
		break;
	case LLDP_PROPTYPE_WILLING:
		bool = (*(int *)val == 0 ? B_FALSE : B_TRUE);

		if (dfp->df_willing == bool)
			break;

		dfp->df_willing = bool;
		changed = B_TRUE;
		break;
	default:
		err = EINVAL;
		break;
	}
	if (changed)
		err = dcbx_appln_set_opercfg(dfp);
	(void) pthread_mutex_unlock(&dfp->df_lock);

	if (err == 0 && changed)
		dcbx_something_changed_local(dfp);
	return (err);
}

/*
 * Return the entries in the Application Priority Table or value of
 * willing property.
 */
/* ARGSUSED */
static int
dcbx_appln_getprop(dcbx_feature_t *dfp, lldp_proptype_t ptype,
    char *buf, uint_t bufsize)
{
	lldp_appln_t	*app, *appln;
	uint_t		nappln, i, len = 0, err = 0;

	lldp_mutex_lock(&dfp->df_lock);
	switch (ptype) {
	case LLDP_PROPTYPE_APPLN:
		err = lldp_nvlist2appln(dfp->df_localparams, &appln, &nappln);
		if (err != 0)
			break;
		app = appln;
		for (i = 0; i < nappln; i++, app++) {
			if (i > 0)
				len += snprintf(buf + len, bufsize - len, ",");
			len += snprintf(buf + len, bufsize - len, "%x/%u/%u",
			    app->la_id, app->la_sel, app->la_pri);
			if (len >= bufsize)
				break;
		}
		free(appln);
		break;
	case LLDP_PROPTYPE_WILLING:
		(void) snprintf(buf, bufsize, "%d", dfp->df_willing);
		break;
	default:
		err = EINVAL;
		break;
	}
	lldp_mutex_unlock(&dfp->df_lock);
	return (err);
}

/* application handling thread */
void *
dcbx_appln(void *arg)
{
	dcbx_feature_t	*dfp = arg;
	int		err;

	for (;;) {
		lldp_mutex_lock(&dfp->df_lock);
		while (dfp->df_enabled && !dfp->df_p_dcbxupdate) {
			(void) pthread_cond_wait(&dfp->df_condvar,
			    &dfp->df_lock);
		}
		if (dfp->df_p_dcbxupdate) {
			dfp->df_p_dcbxupdate = B_FALSE;
			err = dfp->df_process(dfp);
			lldp_mutex_unlock(&dfp->df_lock);
		} else if (!dfp->df_enabled) {
			lldp_mutex_unlock(&dfp->df_lock);
			dcbx_feature_refcnt_decr(dfp);
			break;
		}
		if (err == 0)
			dcbx_something_changed_local(dfp);
	}
	return (NULL);
}

/* Application feature cleanup. */
void
dcbx_appln_fini(dcbx_feature_t *dfp)
{
	lldp_agent_t	*lap = dfp->df_la;

	if (dfp->df_mpeer_toid != 0)
		(void) lldp_untimeout(dfp->df_mpeer_toid);

	nvlist_free(dfp->df_localparams);
	nvlist_free(dfp->df_opercfg);
	nvlist_free(dfp->df_peercfg);
	(void) pthread_mutex_destroy(&dfp->df_lock);
	free(dfp);
	lldp_agent_refcnt_decr(lap);
}

/* Application feature initialization */
int
dcbx_appln_init(lldp_agent_t *lap, dcbx_feature_t **dfp)
{
	pthread_attr_t	attr;
	char		pgname[LLDP_STRSIZE], *apt;
	nvlist_t	*cfg = NULL, *anvl = NULL, *nvl = NULL;
	int		err;

	/* if LLDP is not enabled for both TX and RX, we return */
	if (lap->la_adminStatus != LLDP_MODE_RXTX)
		return (ENOTSUP);

	/* Check if the underlying NIC supports DCB */
	if (!lldpd_islink_indcb(lap->la_linkid))
		return (ENOTSUP);

	if ((*dfp = calloc(1, sizeof (dcbx_feature_t))) == NULL)
		return (ENOMEM);

	(*dfp)->df_ftype = DCBX_TYPE_APPLICATION;
	(*dfp)->df_refcnt = 1;
	(*dfp)->df_process =  dcbx_appln_process;
	(*dfp)->df_setprop = dcbx_appln_setprop;
	(*dfp)->df_getprop = dcbx_appln_getprop;
	(*dfp)->df_action = dcbx_appln_action;
	(*dfp)->df_fini = dcbx_appln_fini;
	(*dfp)->df_enabled = B_TRUE;

	err = nvlist_alloc(&(*dfp)->df_localparams, NV_UNIQUE_NAME, 0);
	if (err != 0)
		goto fail;

	err = nvlist_alloc(&(*dfp)->df_opercfg, NV_UNIQUE_NAME, 0);
	if (err != 0)
		goto fail;

	lldp_agent_refcnt_incr(lap);
	(*dfp)->df_la = lap;
	(void) pthread_mutex_init(&((*dfp)->df_lock), NULL);

	/* restore any persistent configuration */
	(void) snprintf(pgname, sizeof (pgname), "agenttlv_%s_%s",
	    lap->la_linkname, LLDP_8021_APPLN_TLVNAME);
	if (nvlist_alloc(&cfg, NV_UNIQUE_NAME, 0) == 0) {
		if (lldpd_walk_db(cfg, pgname) == 0 &&
		    lldp_get_nested_nvl(cfg, "agenttlv", lap->la_linkname,
		    LLDP_8021_APPLN_TLVNAME, &anvl) == 0 &&
		    nvlist_lookup_string(anvl, "apt", &apt) == 0 &&
		    dcbx_appstr2nvlist(apt, &nvl) == 0) {
			(void) nvlist_merge((*dfp)->df_localparams, nvl, 0);
			nvlist_free(nvl);
		}
		nvlist_free(cfg);
	}

	(void) pthread_attr_init(&attr);
	(void) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	(*dfp)->df_refcnt++;
	(void) pthread_create(&((*dfp)->df_fsm), &attr, dcbx_appln, *dfp);
	(void) pthread_attr_destroy(&attr);

	return (0);
fail:
	nvlist_free((*dfp)->df_localparams);
	nvlist_free((*dfp)->df_opercfg);
	nvlist_free((*dfp)->df_peercfg);
	free(*dfp);
	*dfp = NULL;
	return (err);
}

/* Application TLV initialization */
/* ARGSUSED */
int
dcbx_appln_tlv_init(lldp_agent_t *lap, nvlist_t *nvl)
{
	dcbx_feature_t	*dfp;
	int		err;

	if ((err = dcbx_appln_init(lap, &dfp)) != 0)
		return (err);

	(void) snprintf(dfp->df_subid, MAX_SUBID_LEN, "appln-%d-%d",
	    lap->la_linkid, getpid());
	(void) dcbx_feature_refcnt_incr(dfp);
	if ((err = sysevent_evc_subscribe(lldpd_evchp, dfp->df_subid, EC_LLDP,
	    dcbx_handle_sysevents, dfp, 0)) != 0) {
		(void) dcbx_feature_refcnt_decr(dfp);
		/* we need to decrement once again (from the *init()) */
		(void) dcbx_feature_refcnt_incr(dfp);
		syslog(LOG_ERR, "Error subscribing to Application events. "
		    "Cannot enable Application TLV");
		return (err);
	}

	/* Check to see if the peer has already sent us Application TLV */
	dcbx_get_feature_nvl(lap, DCBX_TYPE_APPLICATION, &dfp->df_peercfg);
	if (dfp->df_peercfg != NULL) {
		/*
		 * no `df_lock' is needed because the feature has not
		 * yet been added, so nobody can see this feature yet.
		 */
		(void) dfp->df_process(dfp);
	}

	/* Add the feature into the feature list */
	lldp_rw_lock(&lap->la_feature_rwlock, LLDP_RWLOCK_WRITER);
	list_insert_tail(&lap->la_features, dfp);
	lldp_rw_unlock(&lap->la_feature_rwlock);

	return (0);
}

/* Application TLV fini */
void
dcbx_appln_tlv_fini(lldp_agent_t *lap)
{
	dcbx_feature_t	*dfp;
	nvlist_t	*ouinvl;

	if ((dfp = dcbx_feature_get(lap, DCBX_TYPE_APPLICATION)) == NULL)
		return;

	/* now remove the TLV from the local mib */
	if (lldp_get_nested_nvl(lap->la_local_mib, lap->la_msap,
	    LLDP_NVP_ORGANIZATION, LLDP_8021_OUI_LIST, &ouinvl) == 0) {
		(void) nvlist_remove(ouinvl, LLDP_NVP_APPLN, DATA_TYPE_NVLIST);
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
