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

#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <string.h>
#include <strings.h>
#include <sys/sysevent/lldp.h>
#include "lldp_impl.h"
#include "dcbx_impl.h"

/* Wait for an event to occur */
static int
dcbx_fssm_parked(dcbx_feature_t *dfp)
{
	/*
	 * We wait till we :-
	 * 	o are disabled OR
	 * 	o Link goes down OR
	 * 	o we get a DCBx PDU.
	 */
	while (dfp->df_enabled && dfp->df_linkup && !dfp->df_p_dcbxupdate &&
	    !dfp->df_localparamchange) {
		(void) pthread_cond_wait(&dfp->df_condvar, &dfp->df_lock);
	}
	if (!dfp->df_enabled) {
		return (DCBX_FSM_SHUTDOWN);
	} else if (!dfp->df_linkup) {
		return (DCBX_FSM_LINKDOWN);
	} else if (dfp->df_localparamchange) {
		return (DCBX_FSM_SETLOCALPARAM);
	} else if (dfp->df_p_dcbxupdate) {
		dfp->df_p_dcbxupdate = B_FALSE;
		if (dfp->df_process(dfp) != 0)
			return (DCBX_FSM_WAIT);
		if (dfp->df_p_fnopresent) {
			if (dfp->df_pending) {
				return (DCBX_FSM_WAIT);
			} else {
				dfp->df_pending = B_TRUE;
				return (DCBX_FSM_USELOCALCFG);
			}
		} else {
			return (DCBX_FSM_GETPEERCFG);
		}
	}
	return (DCBX_FSM_WAIT);
}

/* Use local configuration as operating configuration */
static int
dcbx_fssm_UseLocalCfg(dcbx_feature_t *dfp)
{
	int	err;

	if ((err = dfp->df_setcfg(dfp, dfp->df_opercfg,
	    dfp->df_localparams)) != 0) {
		return (err);
	}
	/* If the feature has an action associated with it, call it now */
	if (dfp->df_action != NULL) {
		if (dfp->df_action(dfp) != 0) {
			syslog(LOG_WARNING, "dcbx_fssm_UseLocalCfg: Error "
			    "executing action for %d", dfp->df_ftype);
		}
	}
	return (err);
}

/* Use Peer configuration as operating configuration */
static int
dcbx_fssm_UsePeerCfg(dcbx_feature_t *dfp)
{
	int	err;

	if ((err = dfp->df_setcfg(dfp, dfp->df_opercfg,
	    dfp->df_peercfg)) != 0) {
		return (err);
	}
	/* If the feature has an action associated with it, call it now */
	if (dfp->df_action != NULL) {
		if (dfp->df_action(dfp) != 0) {
			syslog(LOG_WARNING, "dcbx_fssm_UsePeerCfg: Error "
			    "executing action for %d", dfp->df_ftype);
			/* Revert to local config */
			if (dfp->df_setcfg(dfp, dfp->df_opercfg,
			    dfp->df_localparams) != 0) {
				syslog(LOG_WARNING, "dcbx_fssm_UsePeerCfg: "
				    "error reverting to local config");
			}
		}
	}
	return (err);
}

/* Symmetric Feature State Machine */
void *
dcbx_ssm(void *arg)
{
	dcbx_feature_t	*dfp = arg;
	lldp_agent_t	*lap = dfp->df_la;
	boolean_t	local_changed;
	int		err;

linkdown:
	lldp_mutex_lock(&dfp->df_lock);
	while (!dfp->df_linkup)
		(void) pthread_cond_wait(&dfp->df_condvar, &dfp->df_lock);
	/* set the default config */
	err = dfp->df_setdcfg(dfp);
	if (err != 0) {
		dfp->df_state = DCBX_FSM_SHUTDOWN;
		syslog(LOG_ERR, "Error setting local config %d", dfp->df_ftype);
	} else {
		/*
		 * Let's get the remote MIB and see if the peer has already sent
		 * us DCBx TLVs for this feature.
		 */
		(void) nvlist_free(dfp->df_peercfg);
		dfp->df_peercfg = NULL;
		dcbx_get_feature_nvl(lap, dfp->df_ftype, &dfp->df_peercfg);
		if (dfp->df_peercfg != NULL)
			(void) dfp->df_process(dfp);
		dfp->df_state = DCBX_FSM_SETLOCALPARAM;
	}
	/*
	 * Will release the lock when we land in dcbx_fssm_parked or
	 * if we have to bail out of this loop
	 */
	for (;;) {
		switch (dfp->df_state) {
		case DCBX_FSM_WAIT:
			dfp->df_state = dcbx_fssm_parked(dfp);
			break;

		case DCBX_FSM_SETLOCALPARAM:
			dfp->df_pending = B_TRUE;
			if (dfp->df_p_fnopresent) {
				local_changed = B_TRUE;
				if (dcbx_fssm_UseLocalCfg(dfp) != 0) {
					syslog(LOG_ERR,
					    "Error setting local config for %d",
					    dfp->df_ftype);
					local_changed = B_FALSE;
				}
				dfp->df_localparamchange = B_FALSE;
				dfp->df_state = DCBX_FSM_WAIT;

				/*
				 * Inform LLDP agent about local change in
				 * DCB feature.
				 */
				lldp_mutex_unlock(&dfp->df_lock);
				if (local_changed)
					dcbx_something_changed_local(dfp);
				lldp_mutex_lock(&dfp->df_lock);
			} else {
				dfp->df_state = DCBX_FSM_GETPEERCFG;
			}
			break;

		case DCBX_FSM_GETPEERCFG:
			if (!dfp->df_pending) {
				if (!dfp->df_iscompatible(dfp,
				    dfp->df_peercfg)) {
					dfp->df_pending = B_TRUE;
				} else {
					/* Refresh */
					dfp->df_state = DCBX_FSM_WAIT;
					break;
				}
			}
			if (!dfp->df_willing) {
				dfp->df_state = DCBX_FSM_USELOCALCFG;
				break;
			}
			if (!dfp->df_p_willing) {
				dfp->df_state = DCBX_FSM_USEPEERCFG;
			} else if (lldp_local_mac_islower(lap, &err)) {
				if (err != 0) {
					syslog(LOG_WARNING, "dcbx_ssm: error "
					    "comparing MAC addr");
				}
				dfp->df_state = DCBX_FSM_USELOCALCFG;
			} else {
				dfp->df_state = DCBX_FSM_USEPEERCFG;
			}

			break;

		case DCBX_FSM_USELOCALCFG:
			if ((dfp->df_localparamchange) ||
			    !dfp->df_iscompatible(dfp, dfp->df_localparams)) {
				if (dcbx_fssm_UseLocalCfg(dfp) != 0) {
					syslog(LOG_ERR,
					    "Error setting local config for %d",
					    dfp->df_ftype);
				}
				dfp->df_localparamchange = B_FALSE;
				/*
				 * Inform LLDP agent about local change in
				 * DCB feature.
				 */
				lldp_mutex_unlock(&dfp->df_lock);
				dcbx_something_changed_local(dfp);
				lldp_mutex_lock(&dfp->df_lock);
			}

			/* Peer has accepted our config */
			if (!dfp->df_p_fnopresent &&
			    dfp->df_iscompatible(dfp, dfp->df_peercfg)) {
				dfp->df_pending = B_FALSE;
				dcbx_post_event(dfp->df_la,
				    dcbx_type2eventsc(dfp),
				    LLDP_OPER_CFG, dfp->df_opercfg);
			}
			dfp->df_state = DCBX_FSM_WAIT;
			break;

		case DCBX_FSM_USEPEERCFG:
			if (!dfp->df_iscompatible(dfp, dfp->df_peercfg)) {
				local_changed = B_TRUE;
				if (dcbx_fssm_UsePeerCfg(dfp) != 0) {
					syslog(LOG_ERR,
					    "Error setting peer config for %d",
					    dfp->df_ftype);
					local_changed = B_FALSE;
				}
				dfp->df_pending = B_FALSE;
				lldp_mutex_unlock(&dfp->df_lock);

				/*
				 * Inform LLDP agent about local change in
				 * DCB feature and also post event about
				 * change in operation configuration.
				 */
				if (local_changed) {
					dcbx_something_changed_local(dfp);
					dcbx_post_event(dfp->df_la,
					    dcbx_type2eventsc(dfp),
					    LLDP_OPER_CFG, dfp->df_opercfg);
				}
				lldp_mutex_lock(&dfp->df_lock);
			} else {
				dfp->df_pending = B_FALSE;
			}
			if (dfp->df_localparamchange)
				dfp->df_localparamchange = B_FALSE;
			dfp->df_state = DCBX_FSM_WAIT;
			break;

		case DCBX_FSM_LINKDOWN:
			lldp_mutex_unlock(&dfp->df_lock);
			goto linkdown;

		case DCBX_FSM_SHUTDOWN:
			lldp_mutex_unlock(&dfp->df_lock);
			dcbx_feature_refcnt_decr(dfp);
			return (NULL);
		}
	}
	/* NOTREACHED */
	return (NULL);
}
