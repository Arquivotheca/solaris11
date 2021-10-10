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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/fm/protocol.h>

#include <fmd_api.h>
#include <fmd_subr.h>
#include <fmd_string.h>
#include <fmd_protocol.h>
#include <fmd_module.h>
#include <fmd_error.h>

static struct {
	fmd_stat_t nosub;
	fmd_stat_t module;
} self_stats = {
	{ "nosub", FMD_TYPE_UINT64, "event classes with no subscribers seen" },
	{ "module", FMD_TYPE_UINT64, "error events received from fmd modules" },
};

typedef struct self_case {
	enum { SC_CLASS, SC_MODULE } sc_kind;
	char *sc_name;
} self_case_t;

static self_case_t *
self_case_create(fmd_hdl_t *hdl, int kind, const char *name)
{
	self_case_t *scp = fmd_hdl_alloc(hdl, sizeof (self_case_t), FMD_SLEEP);

	scp->sc_kind = kind;
	scp->sc_name = fmd_hdl_strdup(hdl, name, FMD_SLEEP);

	return (scp);
}

static void
self_case_destroy(fmd_hdl_t *hdl, self_case_t *scp)
{
	fmd_hdl_strfree(hdl, scp->sc_name);
	fmd_hdl_free(hdl, scp, sizeof (self_case_t));
}

static fmd_case_t *
self_case_lookup(fmd_hdl_t *hdl, int kind, const char *name)
{
	fmd_case_t *cp = NULL;

	while ((cp = fmd_case_next(hdl, cp)) != NULL) {
		self_case_t *scp = fmd_case_getspecific(hdl, cp);
		if (scp->sc_kind == kind && strcmp(scp->sc_name, name) == 0)
			break;
	}

	return (cp);
}

/*ARGSUSED*/
static void
self_recv(fmd_hdl_t *hdl, fmd_event_t *ep, nvlist_t *nvl, const char *class)
{
	fmd_case_t *cp;
	nvlist_t *flt, *mod;
	char *name;
	int err = 0;

	/*
	 * If we get an error report from another fmd module, then create a
	 * case for the module and add the ereport to it.  The error is either
	 * from fmd_hdl_error() or from fmd_api_error().  If it is the latter,
	 * fmd_module_error() will send another event of class EFMD_MOD_FAIL
	 * when the module has failed, at which point we can solve the case.
	 * We can also close the case on EFMD_MOD_CONF (bad config file).
	 */
	if (strcmp(class, fmd_errclass(EFMD_MODULE)) == 0 &&
	    nvlist_lookup_nvlist(nvl, FM_EREPORT_DETECTOR, &mod) == 0 &&
	    nvlist_lookup_string(mod, FM_FMRI_FMD_NAME, &name) == 0) {

		if ((cp = self_case_lookup(hdl, SC_MODULE, name)) == NULL) {
			cp = fmd_case_open(hdl,
			    self_case_create(hdl, SC_MODULE, name));
		}

		fmd_case_add_ereport(hdl, cp, ep);
		self_stats.module.fmds_value.ui64++;
		(void) nvlist_lookup_int32(nvl, FMD_ERR_MOD_ERRNO, &err);

		if (err != EFMD_MOD_FAIL && err != EFMD_MOD_CONF)
			return; /* module is still active, so keep case open */

		if (fmd_case_solved(hdl, cp))
			return; /* case is already closed but error in _fini */

		class = err == EFMD_MOD_FAIL ? FMD_FLT_MOD : FMD_FLT_CONF;
		flt = fmd_protocol_fault(class, 100, mod, NULL, NULL, NULL);

		fmd_case_add_suspect(hdl, cp, flt);
		fmd_case_solve(hdl, cp);

		return;
	}

	/*
	 * If we get an I/O DDI ereport, drop it for now until the I/O DE is
	 * implemented and integrated.  Existing drivers in O/N have bugs that
	 * will trigger these and we don't want this producing FMD_FLT_NOSUB.
	 */
	if (strncmp(class, "ereport.io.ddi.", strlen("ereport.io.ddi.")) == 0)
		return; /* if we got a DDI ereport, drop it for now */

	/*
	 * If we get any other type of event then it is of a class for which
	 * there are no subscribers.  Some of these correspond to internal fmd
	 * errors, which we ignore.  Otherwise we keep one case per class and
	 * use it to produce a message indicating that something is awry.
	 */
	if (strcmp(class, FM_LIST_SUSPECT_CLASS) == 0 ||
	    strcmp(class, FM_LIST_ISOLATED_CLASS) == 0 ||
	    strcmp(class, FM_LIST_UPDATED_CLASS) == 0 ||
	    strcmp(class, FM_LIST_RESOLVED_CLASS) == 0 ||
	    strcmp(class, FM_LIST_REPAIRED_CLASS) == 0 ||
	    strncmp(class, FM_FAULT_CLASS, strlen(FM_FAULT_CLASS)) == 0 ||
	    strncmp(class, FM_DEFECT_CLASS, strlen(FM_DEFECT_CLASS)) == 0)
		return; /* if no agents are present just drop list.* */

	if (strncmp(class, FMD_ERR_CLASS, FMD_ERR_CLASS_LEN) == 0)
		return; /* if fmd itself produced the error just drop it */

	if (strncmp(class, FMD_RSRC_CLASS, FMD_RSRC_CLASS_LEN) == 0)
		return; /* if fmd itself produced the event just drop it */

	if (strncmp(class, SYSEVENT_RSRC_CLASS, SYSEVENT_RSRC_CLASS_LEN) == 0)
		return; /* sysvent resources are auto generated by fmd */

	if (self_case_lookup(hdl, SC_CLASS, class) != NULL)
		return; /* case is already open against this class */

	if (strncmp(class, FM_IREPORT_CLASS ".",
	    sizeof (FM_IREPORT_CLASS)) == 0)
		return; /* no subscriber required for ireport.* */

	cp = fmd_case_open(hdl, self_case_create(hdl, SC_CLASS, class));
	fmd_case_add_ereport(hdl, cp, ep);
	self_stats.nosub.fmds_value.ui64++;

	flt = fmd_protocol_fault(FMD_FLT_NOSUB, 100, NULL, NULL, NULL, NULL);
	(void) nvlist_add_string(flt, "nosub_class", class);
	fmd_case_add_suspect(hdl, cp, flt);
	fmd_case_solve(hdl, cp);
}

static void
self_close(fmd_hdl_t *hdl, fmd_case_t *cp)
{
	self_case_destroy(hdl, fmd_case_getspecific(hdl, cp));
}

static const fmd_hdl_ops_t self_ops = {
	self_recv,	/* fmdo_recv */
	NULL,		/* fmdo_timeout */
	self_close,	/* fmdo_close */
	NULL,		/* fmdo_stats */
	NULL,		/* fmdo_gc */
};

void
self_init(fmd_hdl_t *hdl)
{
	fmd_module_t *mp = (fmd_module_t *)hdl; /* see below */

	fmd_hdl_info_t info = {
	    "Fault Manager Self-Diagnosis", "1.0", &self_ops, NULL
	};

	/*
	 * Unlike other modules, fmd-self-diagnosis has some special needs that
	 * fall outside of what we want in the module API.  Manually disable
	 * checkpointing for this module by tweaking the mod_stats values.
	 * The self-diagnosis world relates to fmd's running state and modules
	 * which all change when it restarts, so don't bother w/ checkpointing.
	 */
	(void) pthread_mutex_lock(&mp->mod_stats_lock);
	mp->mod_stats->ms_ckpt_save.fmds_value.bool = FMD_B_FALSE;
	mp->mod_stats->ms_ckpt_restore.fmds_value.bool = FMD_B_FALSE;
	(void) pthread_mutex_unlock(&mp->mod_stats_lock);

	if (fmd_hdl_register(hdl, FMD_API_VERSION, &info) != 0)
		return; /* failed to register with fmd */

	(void) fmd_stat_create(hdl, FMD_STAT_NOALLOC, sizeof (self_stats) /
	    sizeof (fmd_stat_t), (fmd_stat_t *)&self_stats);
}

void
self_fini(fmd_hdl_t *hdl)
{
	fmd_case_t *cp = NULL;

	while ((cp = fmd_case_next(hdl, cp)) != NULL)
		self_case_destroy(hdl, fmd_case_getspecific(hdl, cp));
}
