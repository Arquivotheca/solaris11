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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <smbsrv/alloc.h>
#include <smbsrv/smbinfo.h>
#include <smbsrv/smb_idmap.h>

#ifndef _KERNEL
#include <syslog.h>
#endif

static idmap_get_handle_t *smb_idmap_get_create(void);
static void smb_idmap_get_destroy(idmap_get_handle_t *);
static idmap_stat smb_idmap_get_mappings(idmap_get_handle_t *);
static idmap_stat smb_idmap_batch_getuidbysid(idmap_get_handle_t *,
    const char *, uint32_t, uid_t *, idmap_stat *);
static idmap_stat smb_idmap_batch_getgidbysid(idmap_get_handle_t *,
    const char *, uint32_t, gid_t *, idmap_stat *);
static idmap_stat smb_idmap_batch_getpidbysid(idmap_get_handle_t *,
    const char *, uint32_t, uid_t *, int *, idmap_stat *);
static idmap_stat smb_idmap_batch_getsidbyuid(idmap_get_handle_t *,
    uid_t, char **, uint32_t *, idmap_stat *);
static idmap_stat smb_idmap_batch_getsidbygid(idmap_get_handle_t *,
    gid_t, char **, uint32_t *, idmap_stat *);
static idmap_stat smb_idmap_batch_binsid(smb_idmap_batch_t *sib);

/*
 * Report an idmap error.
 */
#ifndef _KERNEL
void
smb_idmap_check(const char *s, idmap_stat stat)
{
	if (stat != IDMAP_SUCCESS) {
		if (s == NULL)
			s = "smb_idmap_check";

		syslog(LOG_ERR, "%s: %s", s, idmap_stat2string(stat));
	}
}
#endif

/*
 * Tries to get a mapping for the given uid/gid
 */
idmap_stat
smb_idmap_getsid(uid_t id, int idtype, smb_sid_t **sid)
{
	smb_idmap_batch_t sib;
	idmap_stat stat;

	stat = smb_idmap_batch_create(&sib, 1, SMB_IDMAP_ID2SID);
	if (stat != IDMAP_SUCCESS)
		return (stat);

	stat = smb_idmap_batch_getsid(sib.sib_idmaph, &sib.sib_maps[0],
	    id, idtype);

	if (stat != IDMAP_SUCCESS) {
		smb_idmap_batch_destroy(&sib);
		return (stat);
	}

	stat = smb_idmap_batch_getmappings(&sib);
	if (stat == IDMAP_SUCCESS)
		*sid = smb_sid_dup(sib.sib_maps[0].sim_sid);

	smb_idmap_batch_destroy(&sib);
	return (stat);
}

/*
 * Tries to get a mapping for the given SID
 */
idmap_stat
smb_idmap_getid(smb_sid_t *sid, uid_t *id, int *id_type)
{
	smb_idmap_batch_t sib;
	smb_idmap_t *sim;
	idmap_stat stat;

	stat = smb_idmap_batch_create(&sib, 1, SMB_IDMAP_SID2ID);
	if (stat != IDMAP_SUCCESS)
		return (stat);

	sim = &sib.sib_maps[0];
	sim->sim_id = id;
	stat = smb_idmap_batch_getid(sib.sib_idmaph, sim, sid, *id_type);
	if (stat != IDMAP_SUCCESS) {
		smb_idmap_batch_destroy(&sib);
		return (stat);
	}

	stat = smb_idmap_batch_getmappings(&sib);
	if (stat == IDMAP_SUCCESS)
		*id_type = sim->sim_idtype;

	smb_idmap_batch_destroy(&sib);
	return (stat);
}

/*
 * Creates and initializes the context for batch ID mapping.
 */
idmap_stat
smb_idmap_batch_create(smb_idmap_batch_t *sib, uint16_t nmap, int flags)
{
	SMB_ASSERT(sib);

	if ((sib->sib_idmaph = smb_idmap_get_create()) == NULL)
		return (IDMAP_ERR_MEMORY);

	sib->sib_flags = flags;
	sib->sib_nmap = nmap;
	sib->sib_maps = MEM_ZALLOC("idmap", nmap * sizeof (smb_idmap_t));
	if (sib->sib_maps == NULL) {
		smb_idmap_get_destroy(sib->sib_idmaph);
		return (IDMAP_ERR_MEMORY);
	}

	return (IDMAP_SUCCESS);
}

/*
 * Frees the batch ID mapping context.
 *
 * If ID mapping is Solaris -> Windows it frees memories
 * allocated for binary SIDs. In userspace the memory allocated
 * for string SID prefixes should also be freed.
 */
void
smb_idmap_batch_destroy(smb_idmap_batch_t *sib)
{
	int i;

	SMB_ASSERT(sib);
	SMB_ASSERT(sib->sib_maps);

	smb_idmap_get_destroy(sib->sib_idmaph);

	if (sib->sib_flags & SMB_IDMAP_ID2SID) {
		for (i = 0; i < sib->sib_nmap; i++) {
			smb_sid_free(sib->sib_maps[i].sim_sid);
#ifndef _KERNEL
			free(sib->sib_maps[i].sim_domsid);
#endif
		}
	}

	MEM_FREE("idmap", sib->sib_maps);
}

/*
 * Queue a request to map the given SID to a UID or GID.
 *
 * sim->sim_id should point to variable that's supposed to
 * hold the returned UID/GID. This needs to be setup by caller
 * of this function.
 *
 * If requested ID type is known, it's passed as 'idtype',
 * if it's unknown it'll be returned in sim->sim_idtype.
 */
idmap_stat
smb_idmap_batch_getid(idmap_get_handle_t *idmaph, smb_idmap_t *sim,
    smb_sid_t *sid, int idtype)
{
	char strsid[SMB_SID_STRSZ];
	idmap_stat stat;

	SMB_ASSERT(idmaph);
	SMB_ASSERT(sim);
	SMB_ASSERT(sid);

	smb_sid_tostr(sid, strsid);
	if (smb_sid_splitstr(strsid, &sim->sim_rid) != 0)
		return (IDMAP_ERR_SID);
	sim->sim_idtype = idtype;

	switch (idtype) {
	case SMB_IDMAP_USER:
		stat = smb_idmap_batch_getuidbysid(idmaph, strsid,
		    sim->sim_rid, sim->sim_id, &sim->sim_stat);
		break;

	case SMB_IDMAP_GROUP:
		stat = smb_idmap_batch_getgidbysid(idmaph, strsid,
		    sim->sim_rid, sim->sim_id, &sim->sim_stat);
		break;

	case SMB_IDMAP_UNKNOWN:
		stat = smb_idmap_batch_getpidbysid(idmaph, strsid,
		    sim->sim_rid, sim->sim_id, &sim->sim_idtype,
		    &sim->sim_stat);
		break;

	default:
		SMB_ASSERT(0);
		return (IDMAP_ERR_ARG);
	}

	return (stat);
}

/*
 * Queue a request to map the given UID/GID to a SID.
 *
 * sim->sim_domsid and sim->sim_rid will contain the mapping
 * result upon successful process of the batched request.
 */
idmap_stat
smb_idmap_batch_getsid(idmap_get_handle_t *idmaph, smb_idmap_t *sim,
    uid_t id, int idtype)
{
	idmap_stat stat = IDMAP_SUCCESS;

	SMB_ASSERT(idmaph);
	SMB_ASSERT(sim);
	sim->sim_stat = IDMAP_SUCCESS;

	switch (idtype) {
	case SMB_IDMAP_USER:
		stat = smb_idmap_batch_getsidbyuid(idmaph, id,
		    &sim->sim_domsid, &sim->sim_rid, &sim->sim_stat);
		break;

	case SMB_IDMAP_GROUP:
		stat = smb_idmap_batch_getsidbygid(idmaph, id,
		    &sim->sim_domsid, &sim->sim_rid, &sim->sim_stat);
		break;

	case SMB_IDMAP_OWNERAT:
		/* Current Owner S-1-5-32-766 */
#ifdef _KERNEL
		sim->sim_domsid = NT_BUILTIN_DOMAIN_SIDSTR;
#else
		sim->sim_domsid = strdup(NT_BUILTIN_DOMAIN_SIDSTR);
#endif
		sim->sim_rid = SECURITY_CURRENT_OWNER_RID;
		break;

	case SMB_IDMAP_GROUPAT:
		/* Current Group S-1-5-32-767 */
#ifdef _KERNEL
		sim->sim_domsid = NT_BUILTIN_DOMAIN_SIDSTR;
#else
		sim->sim_domsid = strdup(NT_BUILTIN_DOMAIN_SIDSTR);
#endif
		sim->sim_rid = SECURITY_CURRENT_GROUP_RID;
		break;

	case SMB_IDMAP_EVERYONE:
		/* Everyone S-1-1-0 */
#ifdef _KERNEL
		sim->sim_domsid = NT_WORLD_AUTH_SIDSTR;
#else
		sim->sim_domsid = strdup(NT_WORLD_AUTH_SIDSTR);
#endif
		sim->sim_rid = 0;
		break;

	default:
		return (IDMAP_ERR_ARG);
	}

	return (stat);
}

/*
 * trigger ID mapping service to get the mappings for queued
 * requests.
 *
 * Checks the result of all the queued requests.
 */
idmap_stat
smb_idmap_batch_getmappings(smb_idmap_batch_t *sib)
{
	idmap_stat stat = IDMAP_SUCCESS;
	int i;

	if ((stat = smb_idmap_get_mappings(sib->sib_idmaph)) != IDMAP_SUCCESS)
		return (stat);

	/*
	 * Check the status for all the queued requests
	 */
	for (i = 0; i < sib->sib_nmap; i++) {
		if (sib->sib_maps[i].sim_stat != IDMAP_SUCCESS)
			return (sib->sib_maps[i].sim_stat);
	}

	return (smb_idmap_batch_binsid(sib));
}

/*
 * Convert sidrids to binary sids
 * Returns idmap_stat codes.
 */
static idmap_stat
smb_idmap_batch_binsid(smb_idmap_batch_t *sib)
{
	smb_sid_t *sid;
	smb_idmap_t *sim;
	int i;

	if (sib->sib_flags & SMB_IDMAP_SID2ID)
		/* This operation is not required */
		return (IDMAP_SUCCESS);

	sim = sib->sib_maps;
	for (i = 0; i < sib->sib_nmap; sim++, i++) {
		if ((sid = smb_sid_fromstr(sim->sim_domsid)) == NULL)
			return (IDMAP_ERR_OTHER);

		sim->sim_sid = smb_sid_splice(sid, sim->sim_rid);
		smb_sid_free(sid);
	}

	return (IDMAP_SUCCESS);
}

static idmap_get_handle_t *
smb_idmap_get_create(void)
{
#ifdef _KERNEL
	return (kidmap_get_create(global_zone));
#else
	idmap_get_handle_t *idmaph = NULL;
	(void) idmap_get_create(&idmaph);
	return (idmaph);
#endif
}

static void
smb_idmap_get_destroy(idmap_get_handle_t *idmaph)
{
#ifdef _KERNEL
	kidmap_get_destroy(idmaph);
#else
	idmap_get_destroy(idmaph);
#endif
}

static idmap_stat
smb_idmap_get_mappings(idmap_get_handle_t *idmaph)
{
	idmap_stat ret;

#ifdef _KERNEL
	ret = kidmap_get_mappings(idmaph);
#else
	ret = idmap_get_mappings(idmaph);
	smb_idmap_check("idmap_get_mappings", ret);
#endif

	return (ret);
}

static idmap_stat
smb_idmap_batch_getuidbysid(idmap_get_handle_t *idmaph,
    const char *sid_prefix, uint32_t rid, uid_t *uid, idmap_stat *stat)
{
	idmap_stat ret;

#ifdef _KERNEL
	ret = kidmap_batch_getuidbysid(idmaph, sid_prefix, rid, uid, stat);
#else
	ret = idmap_get_uidbysid(idmaph, (char *)sid_prefix, rid, 0, uid, stat);
	smb_idmap_check("idmap_get_uidbysid", ret);
#endif

	return (ret);
}

static idmap_stat
smb_idmap_batch_getgidbysid(idmap_get_handle_t *idmaph,
    const char *sid_prefix, uint32_t rid, gid_t *gid, idmap_stat *stat)
{
	idmap_stat ret;

#ifdef _KERNEL
	ret = kidmap_batch_getgidbysid(idmaph, sid_prefix, rid, gid, stat);
#else
	ret = idmap_get_gidbysid(idmaph, (char *)sid_prefix, rid, 0, gid, stat);
	smb_idmap_check("idmap_get_gidbysid", ret);
#endif

	return (ret);
}

static idmap_stat
smb_idmap_batch_getpidbysid(idmap_get_handle_t *idmaph, const char *sid_prefix,
    uint32_t rid, uid_t *id, int *is_user, idmap_stat *stat)
{
	idmap_stat ret;

#ifdef _KERNEL
	ret = kidmap_batch_getpidbysid(idmaph, sid_prefix, rid, id, is_user,
	    stat);
#else
	ret = idmap_get_pidbysid(idmaph, (char *)sid_prefix, rid, 0, id,
	    is_user, stat);
	smb_idmap_check("idmap_get_pidbysid", ret);
#endif

	return (ret);
}

static idmap_stat
smb_idmap_batch_getsidbyuid(idmap_get_handle_t *idmaph, uid_t uid,
    char **sid_prefix, uint32_t *rid, idmap_stat *stat)
{
	idmap_stat ret;

#ifdef _KERNEL
	ret = kidmap_batch_getsidbyuid(idmaph, uid, (const char **)sid_prefix,
	    rid, stat);
#else
	ret = idmap_get_sidbyuid(idmaph, uid, 0, sid_prefix, rid, stat);
	smb_idmap_check("idmap_get_sidbyuid", ret);
#endif

	return (ret);
}

static idmap_stat
smb_idmap_batch_getsidbygid(idmap_get_handle_t *idmaph, gid_t gid,
    char **sid_prefix, uint32_t *rid, idmap_stat *stat)
{
	idmap_stat ret;

#ifdef _KERNEL
	ret = kidmap_batch_getsidbygid(idmaph, gid, (const char **)sid_prefix,
	    rid, stat);
#else
	ret = idmap_get_sidbygid(idmaph, gid, 0, sid_prefix, rid, stat);
	smb_idmap_check("idmap_get_sidbygid", ret);
#endif

	return (ret);
}
