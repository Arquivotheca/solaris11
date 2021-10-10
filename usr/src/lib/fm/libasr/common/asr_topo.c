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

#include <atomic.h>
#include <errno.h>
#include <libsysevent.h>
#include <pthread.h>
#include <string.h>
#include <sys/fm/protocol.h>
#include <sys/sysevent/eventdefs.h>

#include "asr.h"
#include "asr_mem.h"
#include "asr_err.h"

static pthread_mutex_t asr_topo_lock;

/*
 * Generates a new topology
 */
static topo_hdl_t *
asr_topo_gen(asr_handle_t *ah)
{
	int err = 0;
	topo_hdl_t *th = NULL;
	char *id;

	(void) pthread_mutex_lock(&asr_topo_lock);
	if ((th = topo_open(TOPO_VERSION, NULL, &err)) == NULL) {
		(void) asr_error(EASR_TOPO,
		    "failed to alloc topo handle: %s", topo_strerror(err));
		goto finally;
	}
	if ((id = topo_snap_hold(th, NULL, &err)) == NULL) {
		topo_close(th);
		th = NULL;
		(void) asr_error(EASR_TOPO,
		    "failed to take libtopo snapshot: %s", topo_strerror(err));
		goto finally;
	}
	asr_log_debug(ah, "Generated topology reference %s", id);
	topo_hdl_strfree(th, id);

finally:
	(void) pthread_mutex_unlock(&asr_topo_lock);
	return (th);
}

/*
 * Given an FMRI (in nvlist form), convert it to a string. Simply wrap around
 * topo_fmri_nvl2str, but fallback to something if we don't have an appropriate
 * libtopo plugin.
 */
char *
asr_topo_fmri2str(topo_hdl_t *thp, nvlist_t *fmri)
{
	int err;
	char *scheme, *fmristr;
	char *buf;
	size_t len;

	if (topo_fmri_nvl2str(thp, fmri, &fmristr, &err) == 0) {
		buf = asr_strdup(fmristr);
		topo_hdl_strfree(thp, fmristr);
		return (buf);
	}

	if (nvlist_lookup_string(fmri, FM_FMRI_SCHEME, &scheme) != 0) {
		(void) asr_error(EASR_FM, "unknown FMRI scheme");
		return (NULL);
	}

	len = snprintf(NULL, 0, "%s://unknown", scheme);
	if ((buf = asr_zalloc(len + 1)) == NULL) {
		(void) asr_error(EASR_NOMEM, "unable to alloc fmri string");
		return (NULL);
	}
	(void) snprintf(buf, len + 1, "%s://unknown", scheme);
	return (buf);
}

/*
 * Walks the FM topology using the given walker callback function and
 * callback data.
 */
int
asr_topo_walk(asr_handle_t *ah, topo_walk_cb_t walker, void *data)
{
	int err = 0;
	topo_hdl_t *th;
	topo_walk_t *twp = NULL;
	asr_topo_enum_data_t tdata;

	if ((th = asr_topo_gen(ah)) == NULL)
		return (ASR_FAILURE);

	tdata.asr_hdl = ah;
	tdata.asr_topoh = th;
	tdata.asr_data = data;
	if ((twp = topo_walk_init(
	    th, FM_FMRI_SCHEME_HC, walker, &tdata, &err)) == NULL)
		goto finally;

	if (topo_walk_step(twp, TOPO_WALK_CHILD) == TOPO_WALK_ERR) {
		asr_log_warn(ah, "topo_error: %s", topo_strerror(err));
		err = EASR_TOPO;
	}

finally:
	if (twp != NULL)
		topo_walk_fini(twp);
	topo_close(th);
	return (err);
}
