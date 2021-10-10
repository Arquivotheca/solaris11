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
 * libidmap API custom functions for NFSv4
 *
 * These should probably be merged back into the common functions, but
 * we need to get NFSv4 behavior correct for release with minimum risk
 * to other components.
 */

#include "idmap_impl.h"
#include "idmap_cache.h"
#include "sidutil.h"

/*
 * Get uid given SID.
 */
idmap_stat
idmap_nfsv4_getuidbysid(const char *sid, int flag, uid_t *uid)
{
	idmap_retcode	rc;
	int		is_user = 1;
	int		is_wuser = -1;
	int 		direction;
	char		sidprefix[SID_STRSZ + 1];
	idmap_rid_t	rid;

	if (!is_sidstr(sid))
		return (IDMAP_ERR_NOMAPPING);

	if (flag & IDMAP_REQ_FLG_USE_CACHE) {
		rc = idmap_cache_lookup_uidbywinname(sid, NULL, uid);
		if (rc == IDMAP_SUCCESS)
			return (rc);
		if (rc != IDMAP_ERR_NOMAPPING)
			return (rc);
	}

	sid_splitstr(sidprefix, sizeof (sidprefix), &rid, sid);
	rc = idmap_get_w2u_mapping(sidprefix, &rid, NULL, NULL, flag,
	    &is_user, &is_wuser, uid, NULL, &direction, NULL);

	if (rc == IDMAP_SUCCESS && (flag & IDMAP_REQ_FLG_USE_CACHE)) {
		idmap_cache_add_winname2uid(sid, NULL, *uid, direction);
	}

	return (rc);
}


/*
 * Get gid given SID.
 */
idmap_stat
idmap_nfsv4_getgidbysid(const char *sid, int flag, gid_t *gid)
{
	idmap_retcode	rc;
	int		is_user = 0;
	int		is_wuser = -1;
	int		direction;
	char		sidprefix[SID_STRSZ + 1];
	idmap_rid_t	rid;

	if (!is_sidstr(sid))
		return (IDMAP_ERR_NOMAPPING);

	if (flag & IDMAP_REQ_FLG_USE_CACHE) {
		rc = idmap_cache_lookup_gidbywinname(sid, NULL, gid);
		if (rc == IDMAP_SUCCESS)
			return (rc);
		if (rc != IDMAP_ERR_NOMAPPING)
			return (rc);
	}

	sid_splitstr(sidprefix, sizeof (sidprefix), &rid, sid);
	rc = idmap_get_w2u_mapping(sidprefix, &rid, NULL, NULL, flag,
	    &is_user, &is_wuser, gid, NULL, &direction, NULL);

	if (rc == IDMAP_SUCCESS && (flag & IDMAP_REQ_FLG_USE_CACHE)) {
		idmap_cache_add_winname2gid(sid, NULL, *gid, direction);
	}

	return (rc);
}


/*
 * Get winname given pid
 */
static idmap_retcode
idmap_nfsv4_getwinnamebypid(uid_t pid, int is_user, int flag, char **name)
{
	idmap_retcode	rc;
	char		*winname = NULL;
	char		*windomain = NULL;
	int		direction;
	char		*sidprefix = NULL;
	idmap_rid_t	rid;

	if (flag & IDMAP_REQ_FLG_USE_CACHE) {
		if (is_user)
			rc = idmap_cache_lookup_winnamebyuid(&winname,
			    &windomain, pid);
		else
			rc = idmap_cache_lookup_winnamebygid(&winname,
			    &windomain, pid);
		if (rc == IDMAP_SUCCESS)
			goto out;
		if (rc != IDMAP_ERR_NOMAPPING)
			goto err;
	}

	/* Get mapping */
	rc = idmap_get_u2w_mapping(&pid, NULL, flag, is_user, NULL,
	    &sidprefix, &rid, &winname, &windomain, &direction, NULL);

	/* Return on error */
	if (rc != IDMAP_SUCCESS)
		goto err;

	/*
	 * The given PID may have been mapped to a SID with no
	 * proper name@domain name.  In those cases, we use the SID.
	 */
	if (winname == NULL || windomain == NULL) {
		idmap_free(winname);
		winname = NULL;
		idmap_free(windomain);
		windomain = NULL;
		(void) asprintf(&winname, "%s-%u", sidprefix, rid);
		if (winname == NULL) {
			rc = IDMAP_ERR_MEMORY;
			goto err;
		}
	}

	if (flag & IDMAP_REQ_FLG_USE_CACHE) {
		if (is_user)
			idmap_cache_add_winname2uid(winname, windomain,
			    pid, direction);
		else
			idmap_cache_add_winname2gid(winname, windomain,
			    pid, direction);
	}

out:
	if (windomain == NULL) {
		*name = winname;
		winname = NULL;
	} else {
		(void) asprintf(name, "%s@%s", winname, windomain);
		if (name == NULL) {
			rc = IDMAP_ERR_MEMORY;
			goto err;
		}
	}
	rc = IDMAP_SUCCESS;

err:
	idmap_free(winname);
	idmap_free(windomain);
	idmap_free(sidprefix);

	return (rc);
}


/*
 * Get winname given uid
 */
idmap_stat
idmap_nfsv4_getwinnamebyuid(uid_t uid, int flag, char **name)
{
	return (idmap_nfsv4_getwinnamebypid(uid, 1, flag, name));
}


/*
 * Get winname given gid
 */
idmap_stat
idmap_nfsv4_getwinnamebygid(gid_t gid, int flag, char **name)
{
	return (idmap_nfsv4_getwinnamebypid(gid, 0, flag, name));
}
