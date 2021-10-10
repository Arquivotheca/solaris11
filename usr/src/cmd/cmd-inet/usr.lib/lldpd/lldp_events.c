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

/*
 * This file contains routines that are used to send out sysevents whenever the
 * local information or the remote information changes. For the detailed
 * semantics of the event attributes, please see <sysevent/lldp.h>
 */
#include <errno.h>
#include <unistd.h>
#include <strings.h>
#include <sys/types.h>
#include <libsysevent.h>
#include "lldp_impl.h"
#include "dcbx_impl.h"

static int
lldp_init_eventlist(nvlist_t **elist, lldp_agent_t *lap)
{
	int	err;

	if ((err = nvlist_alloc(elist, NV_UNIQUE_NAME, 0)) != 0) {
		syslog(LOG_ERR,
		    "lldp_init_eventlist: Error alloc'ing elist");
		return (err);
	}

	/*
	 * Add the LLDP agent name, for which the event is generated.
	 */
	if ((err = nvlist_add_string(*elist, LLDP_AGENT_NAME,
	    lap->la_linkname)) != 0) {
		syslog(LOG_ERR, "lldp_init_eventlist: Error adding local info");
		goto fail;
	}

	/* Add event version */
	if ((err = nvlist_add_uint32(*elist, LLDP_EVENT_VERSION,
	    LLDP_EVENT_CUR_VERSION)) != 0) {
		syslog(LOG_ERR, "lldp_init_eventlist: Error adding version");
		goto fail;
	}
	return (0);
fail:
	nvlist_free(*elist);
	return (err);
}

static void
lldp_post_event(const char *subclass, nvlist_t *nvl)
{
	if (sysevent_evc_publish(lldpd_evchp, EC_LLDP, subclass, "com.oracle",
	    "lldpd", nvl, EVCH_SLEEP) != 0) {
		syslog(LOG_ERR, "error sending event %s: %s",
		    subclass, strerror(errno));
	}
}

/*
 * Send an event with the remote information. Frees the input nvlist.
 */
void
lldp_something_changed_remote(lldp_agent_t *lap, nvlist_t *envl)
{
	nvlist_t	*elist;

	if (lldp_init_eventlist(&elist, lap) != 0)
		return;

	/* merge remote changes information */
	if (nvlist_merge(elist, envl, 0) != 0) {
		syslog(LOG_ERR, "lldp_something_changed_remote: "
		    "Error merging remote changes");
	} else {
		lldp_post_event(ESC_LLDP_REMOTE, elist);
	}
	nvlist_free(elist);
}

void
lldp_mode_changed(lldp_agent_t *lap, uint32_t prev_mode)
{
	nvlist_t	*elist;

	if (lldp_init_eventlist(&elist, lap) != 0)
		return;

	/* Add the current and prev modes */
	if (nvlist_add_uint32(elist, LLDP_MODE_PREVIOUS, prev_mode) != 0) {
		syslog(LOG_ERR,
		    "lldp_mode_changed: Error adding prev mode");
		goto ret;
	}

	if (nvlist_add_uint32(elist, LLDP_MODE_CURRENT,
	    lap->la_adminStatus) != 0) {
		syslog(LOG_ERR,
		    "lldp_mode_changed: Error adding current mode");
		goto ret;
	}

	lldp_post_event(ESC_LLDP_MODE, elist);
ret:
	nvlist_free(elist);
}

/*
 * Posts the operating configuration of the given feature as a nvlist_t. The
 * subscribers will issue the exported <feature>_nvlist2opercfg() on the
 * received nvlist.
 */
void
dcbx_post_event(lldp_agent_t *lap, const char *subclass,
    const char *attrname, nvlist_t *nvl)
{
	nvlist_t	*elist;

	if (lldp_init_eventlist(&elist, lap) != 0)
		return;

	if (nvlist_add_nvlist(elist, attrname, nvl) != 0)
		syslog(LOG_ERR, "dcbx_post_event: Error adding mib");
	else
		lldp_post_event(subclass, elist);
	nvlist_free(elist);
}

/*
 * Adds the peer identity (i.e. Chassis ID and Port ID information) from
 * `src' to `dst'.
 */
int
lldp_add_peer_identity(nvlist_t *dst, nvlist_t *src)
{
	nvlist_t	*nvl;

	if (dst == NULL || src == NULL)
		return (EINVAL);

	(void) nvlist_lookup_nvlist(src, LLDP_NVP_CHASSISID, &nvl);
	if (nvlist_add_nvlist(dst, LLDP_CHASSISID, nvl) != 0)
		return (ENOMEM);
	nvl = NULL;
	(void) nvlist_lookup_nvlist(src, LLDP_NVP_PORTID, &nvl);
	if (nvlist_add_nvlist(dst, LLDP_PORTID, nvl) != 0)
		return (ENOMEM);
	return (0);
}
