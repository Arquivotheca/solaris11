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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifdef sparc
#include <mem.h>
#include <fm/fmd_fmri.h>
#include <fm/libtopo.h>
#include <fm/fmd_agent.h>

#include <string.h>
#include <strings.h>
#include <sys/mem.h>

mem_t mem;

static int
mem_fmri_get_unum(nvlist_t *nvl, char **unump)
{
	uint8_t version;
	char *unum;

	if (nvlist_lookup_uint8(nvl, FM_VERSION, &version) != 0 ||
	    version > FM_MEM_SCHEME_VERSION ||
	    nvlist_lookup_string(nvl, FM_FMRI_MEM_UNUM, &unum) != 0)
		return (fmd_fmri_set_errno(EINVAL));

	*unump = unum;

	return (0);
}

static int
page_isretired(nvlist_t *fmri, int *errp)
{
	fmd_agent_hdl_t *hdl;
	int rc, err;

	if ((hdl = fmd_agent_open(FMD_AGENT_VERSION)) == NULL)
		return (-1);
	rc = fmd_agent_page_isretired(hdl, fmri);
	err = fmd_agent_errno(hdl);
	fmd_agent_close(hdl);

	if (errp != NULL)
		*errp = err;
	return (rc);
}

int
fmd_fmri_expand(nvlist_t *nvl)
{
	char *unum, **serids;
	uint_t nnvlserids;
	size_t nserids;
	int rc, err = 0;
	topo_hdl_t *thp;

	/*
	 * If the mem-scheme topology exports this method expand(), invoke it.
	 */
	if ((thp = fmd_fmri_topo_hold(TOPO_VERSION)) == NULL)
		return (fmd_fmri_set_errno(EINVAL));
	rc = topo_fmri_expand(thp, nvl, &err);
	fmd_fmri_topo_rele(thp);
	if (err != ETOPO_METHOD_NOTSUP)
		return (rc);

	if ((mem_fmri_get_unum(nvl, &unum) < 0) || (*unum == '\0'))
		return (fmd_fmri_set_errno(EINVAL));

	if ((rc = nvlist_lookup_string_array(nvl, FM_FMRI_MEM_SERIAL_ID,
	    &serids, &nnvlserids)) == 0) { /* already have serial #s */
		return (0);
	} else if (rc != ENOENT)
		return (fmd_fmri_set_errno(EINVAL));

	if (mem_get_serids_by_unum(unum, &serids, &nserids) < 0) {
		/* errno is set for us */
		if (errno == ENOTSUP)
			return (0); /* nothing to add - no s/n support */
		else
			return (-1);
	}

	rc = nvlist_add_string_array(nvl, FM_FMRI_MEM_SERIAL_ID, serids,
	    nserids);

	mem_strarray_free(serids, nserids);

	if (rc != 0)
		return (fmd_fmri_set_errno(EINVAL));
	else
		return (0);
}

static int
serids_eq(char **serids1, uint_t nserids1, char **serids2, uint_t nserids2)
{
	int i;

	if (nserids1 != nserids2)
		return (0);

	for (i = 0; i < nserids1; i++) {
		if (strcmp(serids1[i], serids2[i]) != 0)
			return (0);
	}

	return (1);
}

int
fmd_fmri_presence_state(nvlist_t *nvl)
{
	char *unum = NULL;
	int rc, err = 0;
	struct topo_hdl *thp;
	char **nvlserids, **serids;
	uint_t nnvlserids;
	size_t nserids;

	/*
	 * If the mem-scheme topology exports this method replaced(), invoke it.
	 */
	if ((thp = fmd_fmri_topo_hold(TOPO_VERSION)) == NULL)
		return (fmd_fmri_set_errno(EINVAL));
	rc = topo_fmri_presence_state(thp, nvl, &err);
	fmd_fmri_topo_rele(thp);
	if (err == 0)
		return (rc);
	else if (err != ETOPO_METHOD_NOTSUP)
		return (FMD_OBJ_STATE_UNKNOWN);

	if (mem_fmri_get_unum(nvl, &unum) < 0)
		return (-1); /* errno is set for us */

	if (nvlist_lookup_string_array(nvl, FM_FMRI_MEM_SERIAL_ID, &nvlserids,
	    &nnvlserids) != 0) {
		/*
		 * Some mem scheme FMRIs don't have serial ids because
		 * either the platform does not support them, or because
		 * the FMRI was created before support for serial ids was
		 * introduced.  If this is the case, assume it is there.
		 */
		if (mem.mem_dm == NULL)
			return (FMD_OBJ_STATE_UNKNOWN);
		else
			return (fmd_fmri_set_errno(EINVAL));
	}

	if (mem_get_serids_by_unum(unum, &serids, &nserids) < 0) {
		if (errno == ENOTSUP)
			return (FMD_OBJ_STATE_UNKNOWN);
		if (errno != ENOENT) {
			/*
			 * Errors are only signalled to the caller if they're
			 * the caller's fault.  This isn't - it's a failure on
			 * our part to burst or read the serial numbers.  We'll
			 * whine about it, and tell the caller the named
			 * module(s) isn't/aren't there.
			 */
			fmd_fmri_warn("failed to retrieve serial number for "
			    "unum %s", unum);
		}
		return (FMD_OBJ_STATE_NOT_PRESENT);
	}

	rc = serids_eq(serids, nserids, nvlserids, nnvlserids) ?
	    FMD_OBJ_STATE_STILL_PRESENT : FMD_OBJ_STATE_REPLACED;

	mem_strarray_free(serids, nserids);
	return (rc);
}

int
fmd_fmri_contains(nvlist_t *er, nvlist_t *ee)
{
	int rc, err = 0;
	struct topo_hdl *thp;
	char *erunum, *eeunum;
	uint64_t erval = 0, eeval = 0;

	/*
	 * If the mem-scheme topology exports this method contains(), invoke it.
	 */
	if ((thp = fmd_fmri_topo_hold(TOPO_VERSION)) == NULL)
		return (fmd_fmri_set_errno(EINVAL));
	rc = topo_fmri_contains(thp, er, ee, &err);
	fmd_fmri_topo_rele(thp);
	if (err != ETOPO_METHOD_NOTSUP)
		return (rc);

	if (mem_fmri_get_unum(er, &erunum) < 0 ||
	    mem_fmri_get_unum(ee, &eeunum) < 0)
		return (-1); /* errno is set for us */

	if (mem_unum_contains(erunum, eeunum) <= 0)
		return (0); /* can't parse/match, so assume no containment */

	if (nvlist_lookup_uint64(er, FM_FMRI_MEM_OFFSET, &erval) == 0) {
		return (nvlist_lookup_uint64(ee,
		    FM_FMRI_MEM_OFFSET, &eeval) == 0 && erval == eeval);
	}

	if (nvlist_lookup_uint64(er, FM_FMRI_MEM_PHYSADDR, &erval) == 0) {
		return (nvlist_lookup_uint64(ee,
		    FM_FMRI_MEM_PHYSADDR, &eeval) == 0 && erval == eeval);
	}

	return (1);
}

/*
 * We can only make a service state determination for pages.  Mem FMRIs
 * without page addresses will be reported as ok since Solaris has no
 * way at present to dynamically disable an entire DIMM or DIMM pair.
 */
int
fmd_fmri_service_state(nvlist_t *nvl)
{
	uint8_t version;
	int err, service_state;
	topo_hdl_t *thp;
	uint64_t val1, val2;
	int rc, err1 = 0, err2;
	nvlist_t *nvlcp = NULL;
	int retval;

	if ((thp = fmd_fmri_topo_hold(TOPO_VERSION)) == NULL)
		return (fmd_fmri_set_errno(EINVAL));
	err = 0;
	service_state = topo_fmri_service_state(thp, nvl, &err);
	fmd_fmri_topo_rele(thp);
	if (err == 0)
		return (service_state);
	else if (err != ETOPO_METHOD_NOTSUP)
		return (FMD_SERVICE_STATE_UNKNOWN);

	if (nvlist_lookup_uint8(nvl, FM_VERSION, &version) != 0 ||
	    version > FM_MEM_SCHEME_VERSION)
		return (fmd_fmri_set_errno(EINVAL));

	err1 = nvlist_lookup_uint64(nvl, FM_FMRI_MEM_OFFSET, &val1);
	err2 = nvlist_lookup_uint64(nvl, FM_FMRI_MEM_PHYSADDR, &val2);

	if (err1 == ENOENT && err2 == ENOENT)
		/* no page, so assume it's still usable */
		return (FMD_SERVICE_STATE_OK);

	if ((err1 != 0 && err1 != ENOENT) || (err2 != 0 && err2 != ENOENT))
		return (fmd_fmri_set_errno(EINVAL));

	if ((rc = mem_unum_rewrite(nvl, &nvlcp)) != 0)
		return (fmd_fmri_set_errno(rc));

	/*
	 * Ask the kernel if the page is retired, using either the rewritten
	 * hc FMRI or the original mem FMRI with the specified offset or PA.
	 * Refer to the kernel's page_retire_check() for the error codes.
	 */
	rc = page_isretired(nvlcp ? nvlcp : nvl, NULL);

	if (rc == FMD_AGENT_RETIRE_FAIL) {
		/*
		 * The page is not retired and is not scheduled for retirement
		 * (i.e. no request pending and has not seen any errors)
		 */
		retval = FMD_SERVICE_STATE_OK;
	} else if (rc == FMD_AGENT_RETIRE_DONE ||
	    rc == FMD_AGENT_RETIRE_ASYNC) {
		/*
		 * The page has been retired, is in the process of being
		 * retired, or doesn't exist.  The latter is valid if the page
		 * existed in the past but has been DR'd out.
		 */
		retval = FMD_SERVICE_STATE_UNUSABLE;
	} else {
		/*
		 * Errors are only signalled to the caller if they're the
		 * caller's fault.  This isn't - it's a failure of the
		 * retirement-check code.  We'll whine about it and tell
		 * the caller the page is unusable.
		 */
		fmd_fmri_warn("failed to determine page %s=%llx usability: "
		    "rc=%d errno=%d\n", err1 == 0 ? FM_FMRI_MEM_OFFSET :
		    FM_FMRI_MEM_PHYSADDR, err1 == 0 ? (u_longlong_t)val1 :
		    (u_longlong_t)val2, rc, errno);
		retval = FMD_SERVICE_STATE_UNUSABLE;
	}

	if (nvlcp)
		nvlist_free(nvlcp);

	return (retval);
}

int
fmd_fmri_init(void)
{
	return (mem_discover());
}

void
fmd_fmri_fini(void)
{
	mem_dimm_map_t *dm, *em;
	mem_bank_map_t *bm, *cm;
	mem_grp_t *gm, *hm;
	mem_seg_map_t *sm, *tm;

	for (dm = mem.mem_dm; dm != NULL; dm = em) {
		em = dm->dm_next;
		fmd_fmri_strfree(dm->dm_label);
		fmd_fmri_strfree(dm->dm_part);
		fmd_fmri_strfree(dm->dm_device);
		fmd_fmri_free(dm, sizeof (mem_dimm_map_t));
	}
	for (bm = mem.mem_bank; bm != NULL; bm = cm) {
		cm = bm->bm_next;
		fmd_fmri_free(bm, sizeof (mem_bank_map_t));
	}
	for (gm = mem.mem_group; gm != NULL; gm = hm) {
		hm = gm->mg_next;
		fmd_fmri_free(gm, sizeof (mem_grp_t));
	}
	for (sm = mem.mem_seg; sm != NULL; sm = tm) {
		tm = sm->sm_next;
		fmd_fmri_free(sm, sizeof (mem_seg_map_t));
	}
}
#endif	/* sparc */
