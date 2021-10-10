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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/sa_share.h>
#include <smbsrv/smb_door.h>
#include <smbsrv/smb_kproto.h>
#include <smbsrv/smb_ktypes.h>
#include <smbsrv/msgbuf.h>
#include <sharefs/share.h>

static struct {
	char *value;
	uint32_t flag;
} cscopt[] = {
	{ "disabled",	SMB_SHRF_CSC_DISABLED },
	{ "manual",	SMB_SHRF_CSC_MANUAL },
	{ "auto",	SMB_SHRF_CSC_AUTO },
	{ "vdo",	SMB_SHRF_CSC_VDO }
};

#define	SMB_OPTIONS_STRLEN	(4 * 1024)
#define	SMB_FSTYPE		"smb"

/*
 * Flags for smb_shrmgr_share_unpublish()
 *
 * SMB_UNPUBLISH_FLAGS_AUTOHOME	unpublish only if the specified share is an
 * 				autohome share
 */
#define	SMB_UNPUBLISH_FLAGS_AUTOHOME	0x00000001

static smb_shrmgr_t smb_shrmgr;

static int smb_kshare_cmp(const void *, const void *);
static int smb_kshare_add(const void *);
static void smb_kshare_remove(void *);
static void smb_kshare_hold(const void *);
static void smb_kshare_rele(const void *);
static void smb_kshare_destroy(void *);
static char *smb_kshare_oemname(const char *);
static int smb_kshare_is_special(const char *);
static int smb_kshare_is_admin(const char *);
static smb_share_t *smb_kshare_decode(nvlist_t *);
static uint32_t smb_kshare_decode_bool(nvlist_t *, const char *, uint32_t);
static void smb_kshare_csc_flags(smb_share_t *, const char *);
static char *smb_kshare_csc_name(const smb_share_t *si);
static char *smb_kshare_winpath(const char *, uchar_t);
static int smb_kshare_enum(smb_share_t *, smb_svcenum_t *);
static int smb_kshare_update_sharetab(smb_share_t *, sharefs_op_t);
static boolean_t smb_kshare_propcmp(char *, char *);
static int smb_kshare_lookuppnvp(smb_share_t *, vnode_t **);

static int smb_shrmgr_secpolicy_share(nvlist_t *);
static int smb_shrmgr_publish(nvlist_t *);
static int smb_shrmgr_unpublish(nvlist_t *);
static void smb_shrmgr_publish_ipc(void);
static int smb_shrmgr_shareop(sharefs_op_t, void *, size_t);
static void smb_shrmgr_wrlock(void);
static void smb_shrmgr_rdlock(void);
static void smb_shrmgr_unlock(void);

static int smb_shrmgr_share_publish(smb_share_t *);
static int smb_shrmgr_share_unpublish(const char *, uint32_t);
static int smb_shrmgr_share_modify(smb_share_t *, smb_share_t *);

static int smb_shrmgr_notify_init(void);
static void smb_shrmgr_notify_fini(void);
static void smb_shrmgr_notify_signal(void);
static void smb_shrmgr_notify_stop(void);
static void smb_shrmgr_notify_add(uint32_t, const smb_share_t *, const char *,
    uint32_t);
static void smb_shrmgr_notify_handler(smb_thread_t *, void *);

static void smb_nlist_create(void);
static void smb_nlist_destroy(void);
static void smb_nlist_purge(void);
static void smb_nlist_insert_tail(smb_shr_notify_t *);
static smb_shr_notify_t *smb_nlist_head(void);

static smb_avl_nops_t smb_kshare_avlops = {
	smb_kshare_cmp,
	smb_kshare_add,
	smb_kshare_remove,
	smb_kshare_hold,
	smb_kshare_rele,
	smb_kshare_remove
};

/*
 * Executes map and unmap command for shares.
 */
int
smb_kshare_exec(smb_shr_execinfo_t *execinfo)
{
	int exec_rc = 0;

	(void) smb_kdoor_upcall(SMB_DR_SHR_EXEC,
	    execinfo, smb_shr_execinfo_xdr, &exec_rc, xdr_int);

	return (exec_rc);
}

/*
 * Obtains any host access restriction on the specified
 * share for the given host (ipaddr) by calling smbd
 */
uint32_t
smb_kshare_hostaccess(smb_share_t *si, smb_inaddr_t *ipaddr)
{
	smb_shr_hostaccess_query_t req;
	uint32_t host_access = SMB_SHRF_ACC_OPEN;
	uint32_t flag = SMB_SHRF_ACC_OPEN;
	uint32_t access;

	if (smb_inet_iszero(ipaddr))
		return (ACE_ALL_PERMS);

	if ((si->shr_access_none == NULL || *si->shr_access_none == '\0') &&
	    (si->shr_access_ro == NULL || *si->shr_access_ro == '\0') &&
	    (si->shr_access_rw == NULL || *si->shr_access_rw == '\0'))
		return (ACE_ALL_PERMS);

	if (si->shr_access_none != NULL)
		flag |= SMB_SHRF_ACC_NONE;
	if (si->shr_access_ro != NULL)
		flag |= SMB_SHRF_ACC_RO;
	if (si->shr_access_rw != NULL)
		flag |= SMB_SHRF_ACC_RW;

	req.shq_none = si->shr_access_none;
	req.shq_ro = si->shr_access_ro;
	req.shq_rw = si->shr_access_rw;
	req.shq_flag = flag;
	req.shq_ipaddr = *ipaddr;

	(void) smb_kdoor_upcall(SMB_DR_SHR_HOSTACCESS,
	    &req, smb_shr_hostaccess_query_xdr, &host_access, xdr_uint32_t);

	switch (host_access) {
	case SMB_SHRF_ACC_RO:
		access = ACE_ALL_PERMS & ~ACE_ALL_WRITE_PERMS;
		break;
	case SMB_SHRF_ACC_OPEN:
	case SMB_SHRF_ACC_RW:
		access = ACE_ALL_PERMS;
		break;
	case SMB_SHRF_ACC_NONE:
	default:
		access = 0;
	}

	return (access);
}

/*
 * This function is called when smb_server_t is
 * created which means smb/service is ready for
 * exporting SMB shares
 */
void
smb_shrmgr_start(void)
{
	smb_shrmgr_wrlock();
	switch (smb_shrmgr.sm_state) {
	case SMB_SHRMGR_STATE_READY:
		smb_shrmgr.sm_state = SMB_SHRMGR_STATE_ACTIVATING;
		smb_shrmgr_notify_add(SMB_SHARE_NOP_POPULATE, NULL, NULL, 0);
		smb_shrmgr_notify_signal();
		break;

	case SMB_SHRMGR_STATE_ACTIVATING:
	case SMB_SHRMGR_STATE_ACTIVE:
		break;

	case SMB_SHRMGR_STATE_INIT:
	default:
		ASSERT(0);
		break;
	}
	smb_shrmgr_unlock();
}

/*
 * This function is called when smb_server_t goes
 * away which means SMB shares should not be made
 * available to clients.
 *
 * Any pending unpublish event is removed from notify
 * list since smbd is no longer available.
 */
void
smb_shrmgr_stop(void)
{
	smb_shrmgr_wrlock();
	switch (smb_shrmgr.sm_state) {
	case SMB_SHRMGR_STATE_READY:
		break;

	case SMB_SHRMGR_STATE_ACTIVATING:
	case SMB_SHRMGR_STATE_ACTIVE:
		smb_shrmgr_notify_stop();
		smb_shrmgr.sm_state = SMB_SHRMGR_STATE_READY;
		break;

	case SMB_SHRMGR_STATE_INIT:
	default:
		ASSERT(0);
		break;
	}
	smb_shrmgr_unlock();
}

/*
 * Initializes the global infra-structure for managing shares
 */
int
smb_shrmgr_init(void)
{
	int rc;

	smb_shrmgr_wrlock();
	if (smb_shrmgr.sm_state != SMB_SHRMGR_STATE_INIT) {
		smb_shrmgr_unlock();
		return (EBUSY);
	}

	if ((rc = smb_shrmgr_notify_init()) != 0) {
		smb_shrmgr_unlock();
		return (rc);
	}

	smb_shrmgr.sm_cache_share = kmem_cache_create("smb_share_cache",
	    sizeof (smb_share_t), 8, NULL, NULL, NULL, NULL, NULL, 0);

	smb_shrmgr.sm_cache_vfs = kmem_cache_create("smb_vfs_cache",
	    sizeof (smb_vfs_t), 8, NULL, NULL, NULL, NULL, NULL, 0);

	smb_llist_constructor(&smb_shrmgr.sm_vfs_list, sizeof (smb_vfs_t),
	    offsetof(smb_vfs_t, sv_lnd));

	smb_shrmgr.sm_share_avl = smb_avl_create(sizeof (smb_share_t),
	    offsetof(smb_share_t, shr_link), &smb_kshare_avlops);

	(void) sharefs_register(SHAREFS_SMB, smb_shrmgr_shareop);

	smb_shrmgr.sm_nextid = 0;
	smb_shrmgr.sm_state = SMB_SHRMGR_STATE_READY;
	smb_shrmgr_unlock();

	smb_shrmgr_publish_ipc();

	return (0);
}

/*
 * Destroys the global infra-structure of managing shares
 */
void
smb_shrmgr_fini(void)
{
	smb_avl_t *share_avl = smb_shrmgr.sm_share_avl;

	smb_shrmgr_wrlock();
	if (smb_shrmgr.sm_state == SMB_SHRMGR_STATE_INIT) {
		smb_shrmgr_unlock();
		return;
	}

	(void) sharefs_register(SHAREFS_SMB, NULL);

	smb_shrmgr_notify_fini();
	smb_vfs_rele_all(&smb_shrmgr);

	smb_avl_flush(share_avl);
	smb_avl_destroy(share_avl);

	smb_llist_destructor(&smb_shrmgr.sm_vfs_list);

	kmem_cache_destroy(smb_shrmgr.sm_cache_share);
	kmem_cache_destroy(smb_shrmgr.sm_cache_vfs);

	smb_shrmgr.sm_state = SMB_SHRMGR_STATE_INIT;
	smb_shrmgr_unlock();
}

static void
smb_shrmgr_wrlock(void)
{
	rw_enter(&smb_shrmgr.sm_lock, RW_WRITER);
}

static void
smb_shrmgr_rdlock(void)
{
	rw_enter(&smb_shrmgr.sm_lock, RW_READER);
}

static void
smb_shrmgr_unlock(void)
{
	rw_exit(&smb_shrmgr.sm_lock);
}

/*
 * A list of shares in nvlist format can be sent down
 * from userspace through the sharefs interface. Since each
 * share in the list belongs to the same dataset / file system,
 * only need to check access for one of the shares.
 */
static int
smb_shrmgr_secpolicy_share(nvlist_t *shrlist)
{
	nvlist_t *share;
	nvpair_t *nvp;
	char *shr_name;
	char *shr_path;
	int rc;

	for (nvp = nvlist_next_nvpair(shrlist, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(shrlist, nvp)) {
		if (nvpair_type(nvp) != DATA_TYPE_NVLIST)
			continue;

		shr_name = nvpair_name(nvp);
		ASSERT(shr_name);

		if ((rc = nvpair_value_nvlist(nvp, &share)) != 0) {
			cmn_err(CE_WARN, "unable to access "
			    "share information for %s (%d)",
			    shr_name, rc);
			continue;
		}

		rc = nvlist_lookup_string(share, "path", &shr_path);
		if (rc != 0) {
			cmn_err(CE_WARN, "unable to access "
			    "share path for %s (%d)",
			    shr_name, rc);
			continue;
		}

		return (sharefs_secpolicy_share(shr_path));
	}

	/*
	 * no valid shares found, return ok
	 */
	return (0);
}

/*
 * A list of shares in nvlist format can be sent down
 * from userspace through the IOCTL interface. The nvlist
 * is unpacked here and all the shares in the list will
 * be published.
 */
static int
smb_shrmgr_publish(nvlist_t *shrlist)
{
	nvlist_t	 *share;
	nvpair_t	 *nvp;
	smb_share_t	 *si;
	char		*shrname;
	int		rc;

	for (nvp = nvlist_next_nvpair(shrlist, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(shrlist, nvp)) {
		if (nvpair_type(nvp) != DATA_TYPE_NVLIST)
			continue;

		shrname = nvpair_name(nvp);
		ASSERT(shrname);

		if ((rc = nvpair_value_nvlist(nvp, &share)) != 0) {
			cmn_err(CE_WARN, "unable to share %s: "
			    "failed accessing share information (%d)",
			    shrname, rc);
			continue;
		}

		if ((si = smb_kshare_decode(share)) == NULL)
			continue;

		if ((rc = smb_shrmgr_share_publish(si)) != 0) {
			smb_kshare_destroy(si);
			if (rc == ENOTACTIVE)
				return (rc);
		}
	}

	return (0);
}

/*
 * A list of shares in nvlist format can be sent down
 * from userspace through the IOCTL interface. The nvlist
 * is unpacked here and all the shares in the list will
 * be unpublished.
 */
static int
smb_shrmgr_unpublish(nvlist_t *shrlist)
{
	nvpair_t	*nvp;
	int		rc;

	for (nvp = nvlist_next_nvpair(shrlist, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(shrlist, nvp)) {
		if (nvpair_type(nvp) != DATA_TYPE_NVLIST)
			continue;

		rc = smb_shrmgr_share_unpublish(nvpair_name(nvp), 0);
		if (rc == ENOTACTIVE)
			return (rc);
	}

	return (0);
}

/*
 * Returns the number of shares in ioc->num based on the
 * specified qualifier:
 *
 *  - SMB_SHARENUM_FLAG_ALL	all shares
 *  - SMB_SHARENUM_FLAG_DFS	DFS root shares
 *
 *  Returns 0 on success; errno on failure
 */
int
smb_shrmgr_share_num(smb_ioc_sharenum_t *ioc)
{
	smb_avl_t *share_avl;
	smb_share_t *si, *next_si;
	int rc = 0;

	smb_shrmgr_rdlock();
	if (smb_shrmgr.sm_state != SMB_SHRMGR_STATE_ACTIVE) {
		smb_shrmgr_unlock();
		return (ENOTACTIVE);
	}

	share_avl = smb_shrmgr.sm_share_avl;
	ioc->num = 0;

	switch (ioc->qualifier) {
	case SMB_SHARENUM_FLAG_ALL:
		ioc->num = smb_avl_numnodes(share_avl);
		break;

	case SMB_SHARENUM_FLAG_DFS:
		si = smb_avl_first(share_avl);
		while (si != NULL) {
			if (si->shr_flags & SMB_SHRF_DFSROOT)
				ioc->num++;
			next_si = smb_avl_next(share_avl, si);
			smb_avl_release(share_avl, si);
			si = next_si;
		}

		break;

	default:
		rc = EINVAL;
		break;
	}

	smb_shrmgr_unlock();
	return (rc);
}

/*
 * Looks up the sharename specified in the ioc structure
 * and if it exists encodes the information back into
 * the ioc
 */
int
smb_shrmgr_share_get(smb_ioc_share_t *ioc)
{
	smb_share_t *si;
	uint_t nbytes;
	int rc;

	if ((si = smb_shrmgr_share_lookup(ioc->shr)) == NULL)
		return (ENOENT);

	rc = smb_share_encode(si, (uint8_t *)ioc->shr, ioc->shrlen, &nbytes);
	ioc->shrlen = (uint32_t)nbytes;
	smb_shrmgr_share_release(si);

	return (rc);
}

/*
 * Looks up the share list for the specified Windows style
 * path (passed via ioc). If a match is found the share
 * information is encoded and passed back via ioc
 */
int
smb_shrmgr_share_chk(smb_ioc_share_t *ioc)
{
	smb_avl_t *share_avl;
	smb_share_t *si, *next_si;
	int rc = ENOENT;
	uint_t nbytes;

	smb_shrmgr_rdlock();
	if (smb_shrmgr.sm_state != SMB_SHRMGR_STATE_ACTIVE) {
		smb_shrmgr_unlock();
		return (ENOTACTIVE);
	}

	share_avl = smb_shrmgr.sm_share_avl;
	si = smb_avl_first(share_avl);

	while (si != NULL) {
		if (smb_strcasecmp(ioc->shr, si->shr_winpath,
		    0) == 0) {
			rc = smb_share_encode(si, (uint8_t *)ioc->shr,
			    ioc->shrlen, &nbytes);
			ioc->shrlen = (uint32_t)nbytes;
			smb_avl_release(share_avl, si);
			break;
		}
		next_si = smb_avl_next(share_avl, si);
		smb_avl_release(share_avl, si);
		si = next_si;
	}

	smb_shrmgr_unlock();
	return (rc);
}

/*
 * Enumerates the share list and encodes the information
 * as needed by NetShareEnum RPC
 */
void
smb_shrmgr_share_enumsvc(smb_svcenum_t *svcenum)
{
	smb_avl_t *share_avl;
	smb_share_t *si, *next_si;
	int rc = 0;

	smb_shrmgr_rdlock();
	if (smb_shrmgr.sm_state != SMB_SHRMGR_STATE_ACTIVE) {
		smb_shrmgr_unlock();
		return;
	}

	share_avl = smb_shrmgr.sm_share_avl;
	si = smb_avl_first(share_avl);

	while (si != NULL) {
		rc = smb_kshare_enum(si, svcenum);
		if (rc != 0) {
			smb_avl_release(share_avl, si);
			break;
		}
		next_si = smb_avl_next(share_avl, si);
		smb_avl_release(share_avl, si);
		si = next_si;
	}

	smb_shrmgr_unlock();
}

/*
 * This function builds a response for a NetShareEnum RAP request.
 * List of shares is scanned twice. In the first round the total number
 * of shares which their OEM name is shorter than 13 chars (esi->es_ntotal)
 * and also the number of shares that fit in the given buffer are calculated.
 * In the second round the shares data are encoded in the buffer.
 *
 * The data associated with each share has two parts, a fixed size part and
 * a variable size part which is share's comment. The outline of the response
 * buffer is so that fixed part for all the shares will appear first and follows
 * with the comments for all those shares and that's why the data cannot be
 * encoded in one round without unnecessarily complicating the code.
 */
void
smb_shrmgr_share_enumrap(smb_enumshare_info_t *esi)
{
	smb_avl_t *share_avl;
	smb_share_t *si, *next_si;
	int remained;
	uint16_t infolen = 0;
	uint16_t cmntlen = 0;
	uint16_t sharelen;
	uint16_t clen;
	uint32_t cmnt_offs;
	smb_msgbuf_t info_mb;
	smb_msgbuf_t cmnt_mb;
	boolean_t autohome_added = B_FALSE;
	char *cmnt;

	smb_shrmgr_rdlock();
	if (smb_shrmgr.sm_state != SMB_SHRMGR_STATE_ACTIVE) {
		esi->es_ntotal = esi->es_nsent = 0;
		esi->es_datasize = 0;
		smb_shrmgr_unlock();
		return;
	}

	esi->es_ntotal = esi->es_nsent = 0;
	remained = esi->es_bufsize;
	share_avl = smb_shrmgr.sm_share_avl;

	/* Do the necessary calculations in the first round */
	si = smb_avl_first(share_avl);

	while (si != NULL) {
		if (si->shr_oemname == NULL)
			goto next1;

		if ((si->shr_flags & SMB_SHRF_AUTOHOME) && !autohome_added) {
			if (esi->es_posix_uid == si->shr_uid)
				autohome_added = B_TRUE;
			else
				goto next1;
		}

		esi->es_ntotal++;

		if (remained <= 0)
			goto next1;

		cmnt = (si->shr_cmnt) ? si->shr_cmnt : "";
		clen = strlen(cmnt) + 1;
		sharelen = SHARE_INFO_1_SIZE + clen;

		if (sharelen <= remained) {
			infolen += SHARE_INFO_1_SIZE;
			cmntlen += clen;
		}

		remained -= sharelen;
next1:
		next_si = smb_avl_next(share_avl, si);
		smb_avl_release(share_avl, si);
		si = next_si;
	}

	esi->es_datasize = infolen + cmntlen;

	smb_msgbuf_init(&info_mb, (uint8_t *)esi->es_buf, infolen, 0);
	smb_msgbuf_init(&cmnt_mb, (uint8_t *)esi->es_buf + infolen, cmntlen, 0);
	cmnt_offs = infolen;

	/* Encode the data in the second round */
	si = smb_avl_first(share_avl);
	autohome_added = B_FALSE;

	while (si != NULL) {
		if (si->shr_oemname == NULL)
			goto next2;

		if ((si->shr_flags & SMB_SHRF_AUTOHOME) && !autohome_added) {
			if (esi->es_posix_uid == si->shr_uid)
				autohome_added = B_TRUE;
			else
				goto next2;
		}

		if (smb_msgbuf_encode(&info_mb, "13c.wl",
		    si->shr_oemname, si->shr_type, cmnt_offs) < 0) {
			smb_avl_release(share_avl, si);
			break;
		}

		cmnt = (si->shr_cmnt) ? si->shr_cmnt : "";
		if (smb_msgbuf_encode(&cmnt_mb, "s", cmnt) < 0) {
			smb_avl_release(share_avl, si);
			break;
		}

		cmnt_offs += strlen(cmnt) + 1;
		esi->es_nsent++;
next2:
		next_si = smb_avl_next(share_avl, si);
		smb_avl_release(share_avl, si);
		si = next_si;
	}

	smb_msgbuf_term(&info_mb);
	smb_msgbuf_term(&cmnt_mb);
	smb_shrmgr_unlock();
}

/*
 * Looks up the given share and returns a pointer
 * to its definition if it's found. A hold on the
 * object is taken before the pointer is returned
 * in which case the caller MUST always call
 * smb_shrmgr_share_release().
 */
smb_share_t *
smb_shrmgr_share_lookup(const char *shrname)
{
	smb_share_t key;
	smb_share_t *si = NULL;

	ASSERT(shrname);

	smb_shrmgr_rdlock();
	if (smb_shrmgr.sm_state == SMB_SHRMGR_STATE_ACTIVE) {
		key.shr_name = (char *)shrname;
		si = smb_avl_lookup(smb_shrmgr.sm_share_avl, &key);
	}
	smb_shrmgr_unlock();

	return (si);
}

/*
 * Releases the hold taken on the specified share object
 */
void
smb_shrmgr_share_release(smb_share_t *si)
{
	ASSERT(si);
	ASSERT(si->shr_magic == SMB_SHARE_MAGIC);

	smb_avl_release(smb_shrmgr.sm_share_avl, si);
}

/*
 * Unpublishes the specified share if it is an autohome share
 */
void
smb_shrmgr_autohome_unpublish(const char *shrname)
{
	(void) smb_shrmgr_share_unpublish(shrname,
	    SMB_UNPUBLISH_FLAGS_AUTOHOME);
}

/*
 * Publishes the specified share. If the share does not
 * exist it will be added to the shares AVL. If the share
 * already exists, its definition will be modified with
 * new information. For more details about share modification
 * see the comments for smb_shrmgr_share_modify().
 *
 * If the share is a new disk share, smb_vfs_hold() is
 * invoked to ensure that there is a hold on the
 * corresponding file system before the share is
 * added to shares AVL.
 */
static int
smb_shrmgr_share_publish(smb_share_t *si)
{
	smb_avl_t	*share_avl;
	smb_share_t	*cached_si;
	vnode_t		*vp;
	int		rc = 0;

	smb_shrmgr_wrlock();

	if (smb_shrmgr.sm_state == SMB_SHRMGR_STATE_INIT) {
		smb_shrmgr_unlock();
		return (ENOTACTIVE);
	}

	share_avl = smb_shrmgr.sm_share_avl;

	if (!STYPE_ISDSK(si->shr_type)) {
		si->shr_kid = ++smb_shrmgr.sm_nextid;
		if (((rc = smb_avl_add(share_avl, si)) != 0) && (rc != EEXIST))
			cmn_err(CE_WARN, "unable to share %s: "
			    "cache failed (%d)", si->shr_name, rc);

		smb_shrmgr_unlock();
		return (rc);
	}

	if ((cached_si = smb_avl_lookup(share_avl, si)) != NULL) {
		rc = smb_shrmgr_share_modify(cached_si, si);
		smb_avl_release(share_avl, cached_si);
		smb_shrmgr_unlock();
		return (rc);
	}

	if ((rc = smb_kshare_lookuppnvp(si, &vp)) != 0) {
		cmn_err(CE_WARN, "unable to share %s: "
		    "failed resolving the path %s (%d)",
		    si->shr_name, si->shr_path, rc);
		smb_shrmgr_unlock();
		return (rc);
	}

	si->shr_vfs = smb_vfs_hold(&smb_shrmgr, vp->v_vfsp, &rc);
	if (si->shr_vfs != NULL) {
		si->shr_kid = ++smb_shrmgr.sm_nextid;
		if ((rc = smb_avl_add(share_avl, si)) != 0) {
			cmn_err(CE_WARN, "unable to share %s: "
			    "cache failed (%d)", si->shr_name, rc);
			smb_vfs_rele(&smb_shrmgr, si->shr_vfs);
			si->shr_vfs = NULL;
		}
	} else {
		cmn_err(CE_WARN, "unable to share %s: "
		    "no such file system %s (%d)",
		    si->shr_name, si->shr_path, rc);
	}

	VN_RELE(vp);

	if (rc == 0) {
		/* It's either ACTIVATING or ACTIVE */
		if (smb_shrmgr.sm_state != SMB_SHRMGR_STATE_READY) {
			smb_shrmgr_notify_add(SMB_SHARE_NOP_PUBLISH, si,
			    NULL, 0);
			smb_shrmgr_notify_signal();
		}
	}

	smb_shrmgr_unlock();
	return (rc);
}

/*
 * Unpublishes the specified share.
 *
 * If the share is an Autohome share, the autohome count
 * is decremented and the share is only removed if the
 * count goes to zero.
 *
 * If the share is a disk share, the hold on the corresponding
 * file system is released before removing the share from
 * the AVL tree.
 *
 * An unpublish event is posted to the shrmgr notify list in order
 * to disconnect all the trees associated with the share that is
 * being removed. Cleaning up may involve VOP and/or VFS calls, which
 * may conflict/deadlock with stuck threads if something is amiss with the
 * file system.  Queueing the request for asynchronous processing allows the
 * call to return immediately so that, if the unshare is being done in the
 * context of a forced unmount, the forced unmount will always be able to
 * proceed (unblocking stuck I/O and eventually allowing all blocked unshare
 * processes to complete).
 *
 * The operation waits for maximum 1 second before returning to the caller in
 * order to allow the connection cleanup to finish. If cleanup is finished
 * earlier, the notify handler thread will signal this thread to wakeup and
 * return to its caller. This is needed as a fully asychronous cleanup could
 * result in unmount failure with "Device Busy" error.
 *
 * The path lookup to find the root vnode of the VFS in question and the
 * release of this vnode are done synchronously prior to any associated
 * unmount.  Doing these asynchronous to an associated unmount could run
 * the risk of a spurious EBUSY for a standard unmount or an EIO during
 * the path lookup due to a forced unmount finishing first.
 */
static int
smb_shrmgr_share_unpublish(const char *shrname, uint32_t flags)
{
	smb_share_t	*si;
	smb_share_t	key;
	smb_avl_t	*share_avl;
	smb_event_t	*event = NULL;
	uint32_t	event_id = 0;
	boolean_t	auto_unpublish;
	boolean_t	autohome;

	ASSERT(shrname);

	smb_shrmgr_wrlock();

	if (smb_shrmgr.sm_state == SMB_SHRMGR_STATE_INIT) {
		smb_shrmgr_unlock();
		return (ENOTACTIVE);
	}

	share_avl = smb_shrmgr.sm_share_avl;

	key.shr_name = (char *)shrname;
	if ((si = smb_avl_lookup(share_avl, &key)) == NULL) {
		smb_shrmgr_unlock();
		return (ENOENT);
	}

	autohome = ((si->shr_flags & SMB_SHRF_AUTOHOME) != 0);

	if (!autohome && ((flags & SMB_UNPUBLISH_FLAGS_AUTOHOME) != 0)) {
		smb_avl_release(share_avl, si);
		smb_shrmgr_unlock();
		return (ENOENT);
	}

	if (autohome) {
		mutex_enter(&si->shr_mutex);
		si->shr_autocnt--;
		auto_unpublish = (si->shr_autocnt == 0);
		mutex_exit(&si->shr_mutex);
		if (!auto_unpublish) {
			smb_avl_release(share_avl, si);
			smb_shrmgr_unlock();
			return (0);
		}
	}

	if (STYPE_ISDSK(si->shr_type)) {
		smb_vfs_rele(&smb_shrmgr, si->shr_vfs);
		si->shr_vfs = NULL;
	} else if (STYPE_ISIPC(si->shr_type)) {
		/* IPC$ cannot be unpublished */
		smb_avl_release(share_avl, si);
		smb_shrmgr_unlock();
		return (EPERM);
	}

	smb_avl_remove(share_avl, si);

	/* It's either ACTIVATING or ACTIVE */
	if (smb_shrmgr.sm_state != SMB_SHRMGR_STATE_READY) {
		if ((event = smb_event_create(SMB_SHR_NOTIFY_TIMEOUT)) != NULL)
			event_id = smb_event_txid(event);
		smb_shrmgr_notify_add(SMB_SHARE_NOP_UNPUBLISH, si,
		    NULL, event_id);
		smb_shrmgr_notify_signal();
	}

	smb_avl_release(share_avl, si);
	smb_shrmgr_unlock();

	(void) smb_event_wait(event);
	smb_event_destroy(event);

	return (0);
}

/*
 * Modifies the share definition as specified by 'si'
 * with new information provided via 'new_si'. This function
 * assumes both si and new_si contain the same name and
 * path. In-place name and path modification is not allowed
 * because a share is primarily identified by these two
 * properties. Share should be removed and added with the new
 * name/path.
 *
 * For Autohome shares only a reference count is incremented
 * and ENOTUNIQ is returned.
 *
 * If the container has changed, smbd is notified to republish
 * the share with the new container.
 *
 * If a property is modified, /etc/dfs/sharetab file is updated,
 * the existing definition is removed from AVL and the new defintion
 * is added. The existing share definition is not modified in place
 * because the rest of the code assumes once a share is added to
 * the AVL tree it will not change so there is no protection in
 * place. Having a read-only share definition reduces code complexity
 * and increases concurrency.
 *
 * If no properties are modified, this is a re-publish of the same
 * share. Do not update the share cache and return ENOTUNIQ so
 * that the caller will destroy the new share.
 */
static int
smb_shrmgr_share_modify(smb_share_t *si, smb_share_t *new_si)
{
	ASSERT(si->shr_type == new_si->shr_type);

	if ((si->shr_flags & SMB_SHRF_AUTOHOME) != 0) {
		mutex_enter(&si->shr_mutex);
		si->shr_autocnt++;
		mutex_exit(&si->shr_mutex);
		return (ENOTUNIQ);
	}

	if (strcmp(si->shr_path, new_si->shr_path) != 0)
		return (EPERM);

	if ((si->shr_flags != new_si->shr_flags) ||
	    !smb_kshare_propcmp(si->shr_cmnt, new_si->shr_cmnt) ||
	    !smb_kshare_propcmp(si->shr_access_none, new_si->shr_access_none) ||
	    !smb_kshare_propcmp(si->shr_access_ro, new_si->shr_access_ro) ||
	    !smb_kshare_propcmp(si->shr_access_rw, new_si->shr_access_rw) ||
	    !smb_kshare_propcmp(si->shr_container, new_si->shr_container)) {
		smb_avl_remove(smb_shrmgr.sm_share_avl, si);
		new_si->shr_vfs = si->shr_vfs;
		si->shr_vfs = NULL;
		(void) smb_avl_add(smb_shrmgr.sm_share_avl, new_si);

		if (!smb_kshare_propcmp(si->shr_container,
		    new_si->shr_container)) {
			smb_shrmgr_notify_add(SMB_SHARE_NOP_REPUBLISH, si,
			    new_si->shr_container, 0);
			smb_shrmgr_notify_signal();
		}

		return (0);
	} else {
		return (ENOTUNIQ);
	}
}

/*
 * Creates and publishes the IPC$ share
 */
static void
smb_shrmgr_publish_ipc(void)
{
	smb_share_t *si;
	nvlist_t *tmp_shr;
	nvlist_t *smb = NULL;

	si = kmem_cache_alloc(smb_shrmgr.sm_cache_share, KM_SLEEP);
	bzero(si, sizeof (smb_share_t));

	si->shr_magic = SMB_SHARE_MAGIC;
	si->shr_refcnt = 0;
	si->shr_name = smb_mem_strdup("IPC$");
	si->shr_path = smb_mem_strdup("");
	si->shr_cmnt = smb_mem_strdup("Remote IPC");
	si->shr_winpath = smb_mem_strdup("");
	si->shr_oemname = smb_kshare_oemname(si->shr_name);
	si->shr_type = STYPE_IPC | STYPE_SPECIAL;
	si->shr_flags = SMB_SHRF_TRANS;
	if (smb_shortnames)
		si->shr_flags |= SMB_SHRF_SHORTNAME;

	tmp_shr = sa_share_alloc(si->shr_name, si->shr_path);
	(void) sa_share_set_desc(tmp_shr, si->shr_cmnt);

	if (nvlist_alloc(&smb, NV_UNIQUE_NAME, KM_SLEEP) != 0 ||
	    sa_share_set_proto(tmp_shr, SA_PROT_SMB, smb) != SA_OK) {
		ASSERT(0);
		if (smb != NULL)
			nvlist_free(smb);
		sa_share_free(tmp_shr);
		smb_kshare_destroy(si);
		return;
	}
	nvlist_free(smb);
	si->shr_nvdata = tmp_shr;

	if (smb_shrmgr_share_publish(si) != 0) {
		ASSERT(0);
		smb_kshare_destroy(si);
	}

	sa_share_free(tmp_shr);
}

/*
 * Decodes share information in an nvlist format into a smb_share_t
 * structure.
 *
 * This is a temporary function and will be replaced by functions
 * provided by libsharev2 code after it's available.
 */
static smb_share_t *
smb_kshare_decode(nvlist_t *share)
{
	smb_share_t tmp;
	smb_share_t *si;
	nvlist_t *smb;
	char *csc_name = NULL;
	int rc;

	ASSERT(share);

	bzero(&tmp, sizeof (smb_share_t));

	tmp.shr_name = sa_share_get_name(share);
	tmp.shr_path = sa_share_get_path(share);
	tmp.shr_cmnt = sa_share_get_desc(share);

	ASSERT(tmp.shr_name && tmp.shr_path);

	if ((smb = sa_share_get_proto(share, SA_PROT_SMB)) != NULL) {
		rc = nvlist_lookup_uint32(smb, "type", &tmp.shr_type);
		if (rc != 0)
			tmp.shr_type = STYPE_DISKTREE;

		tmp.shr_container = sa_share_get_prop(smb, SHOPT_AD_CONTAINER);
		tmp.shr_access_none = sa_share_get_prop(smb, SHOPT_NONE);
		tmp.shr_access_ro = sa_share_get_prop(smb, SHOPT_RO);
		tmp.shr_access_rw = sa_share_get_prop(smb, SHOPT_RW);

		(void) nvlist_lookup_byte(smb, "drive", &tmp.shr_drive);

		if (sa_share_is_transient(share))
			tmp.shr_flags |= SMB_SHRF_TRANS;

		tmp.shr_flags |= smb_kshare_decode_bool(smb, SHOPT_ABE,
		    SMB_SHRF_ABE);
		tmp.shr_flags |= smb_kshare_decode_bool(smb, SHOPT_CATIA,
		    SMB_SHRF_CATIA);
		tmp.shr_flags |= smb_kshare_decode_bool(smb, SHOPT_GUEST,
		    SMB_SHRF_GUEST_OK);
		tmp.shr_flags |= smb_kshare_decode_bool(smb, SHOPT_DFSROOT,
		    SMB_SHRF_DFSROOT);
		tmp.shr_flags |= smb_kshare_decode_bool(smb, "Autohome",
		    SMB_SHRF_AUTOHOME);

		if ((tmp.shr_flags & SMB_SHRF_AUTOHOME) == SMB_SHRF_AUTOHOME) {
			rc = nvlist_lookup_uint32(smb, "uid", &tmp.shr_uid);
			rc |= nvlist_lookup_uint32(smb, "gid", &tmp.shr_gid);
			if (rc != 0) {
				cmn_err(CE_WARN, "unable to share %s: "
				    "missing UID/GID (%d)", tmp.shr_name, rc);
				return (NULL);
			}
		}

		csc_name = sa_share_get_prop(smb, SHOPT_CSC);
		smb_kshare_csc_flags(&tmp, csc_name);
	}

	si = kmem_cache_alloc(smb_shrmgr.sm_cache_share, KM_SLEEP);
	bzero(si, sizeof (smb_share_t));

	si->shr_magic = SMB_SHARE_MAGIC;
	si->shr_refcnt = 0;

	si->shr_name = smb_mem_strdup(tmp.shr_name);
	si->shr_path = smb_mem_strdup(tmp.shr_path);
	if (tmp.shr_cmnt)
		si->shr_cmnt = smb_mem_strdup(tmp.shr_cmnt);
	if (tmp.shr_container)
		si->shr_container = smb_mem_strdup(tmp.shr_container);
	if (tmp.shr_access_none)
		si->shr_access_none = smb_mem_strdup(tmp.shr_access_none);
	if (tmp.shr_access_ro)
		si->shr_access_ro = smb_mem_strdup(tmp.shr_access_ro);
	if (tmp.shr_access_rw)
		si->shr_access_rw = smb_mem_strdup(tmp.shr_access_rw);

	si->shr_drive = tmp.shr_drive;
	si->shr_winpath = smb_kshare_winpath(si->shr_path, si->shr_drive);
	si->shr_oemname = smb_kshare_oemname(si->shr_name);

	si->shr_flags = tmp.shr_flags | smb_kshare_is_admin(si->shr_name);
	if ((si->shr_flags & SMB_SHRF_TRANS) == 0)
		si->shr_flags |= SMB_SHRF_PERM;

	if (smb_shortnames)
		si->shr_flags |= SMB_SHRF_SHORTNAME;

	si->shr_type = tmp.shr_type | smb_kshare_is_special(si->shr_name);

	si->shr_uid = tmp.shr_uid;
	si->shr_gid = tmp.shr_gid;

	if ((si->shr_flags & SMB_SHRF_AUTOHOME) == SMB_SHRF_AUTOHOME)
		si->shr_autocnt = 1;
	si->shr_nvdata = share;

	return (si);
}

/*
 * Compare function used by shares AVL
 */
static int
smb_kshare_cmp(const void *p1, const void *p2)
{
	smb_share_t *shr1 = (smb_share_t *)p1;
	smb_share_t *shr2 = (smb_share_t *)p2;
	int rc;

	ASSERT(shr1);
	ASSERT(shr1->shr_name);

	ASSERT(shr2);
	ASSERT(shr2->shr_name);

	rc = smb_strcasecmp(shr1->shr_name, shr2->shr_name, 0);

	if (rc < 0)
		return (-1);

	if (rc > 0)
		return (1);

	return (0);
}

static int
smb_kshare_add(const void *p)
{
	smb_share_t *si = (smb_share_t *)p;

	ASSERT(si);
	ASSERT(si->shr_magic == SMB_SHARE_MAGIC);

	smb_kshare_hold(si);
	(void) smb_kshare_update_sharetab(si, SHAREFS_PUBLISH);
	return (0);
}

static void
smb_kshare_remove(void *p)
{
	smb_share_t *si = (smb_share_t *)p;

	ASSERT(si);
	ASSERT(si->shr_magic == SMB_SHARE_MAGIC);

	(void) smb_kshare_update_sharetab(si, SHAREFS_UNPUBLISH);
	smb_kshare_rele(si);
}

/*
 * This function is called by smb_avl routines whenever
 * there is a need to take a hold on a share structure
 * inside AVL
 */
static void
smb_kshare_hold(const void *p)
{
	smb_share_t *si = (smb_share_t *)p;

	ASSERT(si);
	ASSERT(si->shr_magic == SMB_SHARE_MAGIC);

	mutex_enter(&si->shr_mutex);
	si->shr_refcnt++;
	mutex_exit(&si->shr_mutex);
}

/*
 * This function must be called by smb_avl routines whenever
 * smb_kshare_hold is called and the hold needs to be released.
 */
static void
smb_kshare_rele(const void *p)
{
	smb_share_t *si = (smb_share_t *)p;
	boolean_t destroy;

	ASSERT(si);
	ASSERT(si->shr_magic == SMB_SHARE_MAGIC);

	mutex_enter(&si->shr_mutex);
	ASSERT(si->shr_refcnt > 0);
	si->shr_refcnt--;
	destroy = (si->shr_refcnt == 0);
	mutex_exit(&si->shr_mutex);

	if (destroy)
		smb_kshare_destroy(si);
}

/*
 * Frees all the memory allocated for the given
 * share structure. It also removes the structure
 * from the share cache.
 */
static void
smb_kshare_destroy(void *p)
{
	smb_share_t *si = (smb_share_t *)p;

	ASSERT(si);
	ASSERT(si->shr_magic == SMB_SHARE_MAGIC);
	ASSERT(si->shr_refcnt == 0);

	si->shr_magic = (uint32_t)~SMB_SHARE_MAGIC;
	smb_mem_free(si->shr_name);
	smb_mem_free(si->shr_path);
	smb_mem_free(si->shr_cmnt);
	smb_mem_free(si->shr_container);
	smb_mem_free(si->shr_winpath);
	smb_mem_free(si->shr_oemname);
	smb_mem_free(si->shr_access_none);
	smb_mem_free(si->shr_access_ro);
	smb_mem_free(si->shr_access_rw);

	kmem_cache_free(smb_shrmgr.sm_cache_share, si);
}

/*
 * Compares given share properties in string format.
 * Returns B_TRUE if they are the same, otherwise returns
 * B_FALSE.
 */
static boolean_t
smb_kshare_propcmp(char *strprop1, char *strprop2)
{
	if ((strprop1 == NULL) && (strprop2 == NULL))
		return (B_TRUE);

	if ((strprop1 == NULL) || (strprop2 == NULL))
		return (B_FALSE);

	return (smb_strcasecmp(strprop1, strprop2, 0) == 0);
}

/*
 * Generate an OEM name for the given share name.  If the name is
 * shorter than 13 bytes the oemname will be returned; otherwise NULL
 * is returned.
 */
static char *
smb_kshare_oemname(const char *shrname)
{
	smb_wchar_t *unibuf;
	char *oem_name;
	int length;

	length = strlen(shrname) + 1;

	oem_name = smb_mem_alloc(length);
	unibuf = smb_mem_alloc(length * sizeof (smb_wchar_t));

	(void) smb_mbstowcs(unibuf, shrname, length);

	if (ucstooem(oem_name, unibuf, length, OEM_CPG_850) == 0)
		(void) strcpy(oem_name, shrname);

	smb_mem_free(unibuf);

	if (strlen(oem_name) + 1 > SMB_SHARE_OEMNAME_MAX) {
		smb_mem_free(oem_name);
		return (NULL);
	}

	return (oem_name);
}

/*
 * Special share reserved for interprocess communication (IPC$) or
 * remote administration of the server (ADMIN$). Can also refer to
 * administrative shares such as C$, D$, E$, and so forth.
 */
static int
smb_kshare_is_special(const char *sharename)
{
	int len;

	if (sharename == NULL)
		return (0);

	if ((len = strlen(sharename)) == 0)
		return (0);

	if (sharename[len - 1] == '$')
		return (STYPE_SPECIAL);

	return (0);
}

/*
 * Check whether or not this is a default admin share: C$, D$ etc.
 */
static int
smb_kshare_is_admin(const char *sharename)
{
	if (sharename == NULL)
		return (0);

	if (strlen(sharename) == 2 &&
	    smb_isalpha(sharename[0]) && sharename[1] == '$') {
		return (SMB_SHRF_ADMIN);
	}

	return (0);
}

/*
 * Decodes the given boolean share option.
 * If the option is present in the nvlist and it's value is true
 * returns the corresponding flag value, otherwise returns 0.
 */
static uint32_t
smb_kshare_decode_bool(nvlist_t *nvl, const char *propname, uint32_t flag)
{
	char *boolp;

	if (nvlist_lookup_string(nvl, propname, &boolp) == 0)
		if (strcasecmp(boolp, "true") == 0)
			return (flag);

	return (0);
}

/*
 * Map a client-side caching (CSC) option to the appropriate share
 * flag.  Only one option is allowed; an error will be logged if
 * multiple options have been specified.  We don't need to do anything
 * about multiple values here because the SRVSVC will not recognize
 * a value containing multiple flags and will return the default value.
 *
 * If the option value is not recognized, it will be ignored: invalid
 * values will typically be caught and rejected by sharemgr.
 */
static void
smb_kshare_csc_flags(smb_share_t *si, const char *value)
{
	int i;

	if (value == NULL)
		return;

	for (i = 0; i < (sizeof (cscopt) / sizeof (cscopt[0])); ++i) {
		if (strcasecmp(value, cscopt[i].value) == 0) {
			si->shr_flags |= cscopt[i].flag;
			break;
		}
	}

	switch (si->shr_flags & SMB_SHRF_CSC_MASK) {
	case 0:
	case SMB_SHRF_CSC_DISABLED:
	case SMB_SHRF_CSC_MANUAL:
	case SMB_SHRF_CSC_AUTO:
	case SMB_SHRF_CSC_VDO:
		break;

	default:
		cmn_err(CE_NOTE, "%s: csc option conflict: 0x%08x",
		    si->shr_name, si->shr_flags & SMB_SHRF_CSC_MASK);
		break;
	}
}

/*
 * Return the option name for the first CSC flag (there should be only
 * one) encountered in the share flags.
 */
static char *
smb_kshare_csc_name(const smb_share_t *si)
{
	int i;

	for (i = 0; i < (sizeof (cscopt) / sizeof (cscopt[0])); ++i) {
		if (si->shr_flags & cscopt[i].flag)
			return (cscopt[i].value);
	}

	return (NULL);
}

/*
 * Creates the notification cache, list and thread
 */
static int
smb_shrmgr_notify_init(void)
{
	int rc;

	smb_shrmgr.sm_cache_notify = kmem_cache_create("smb_shr_notify_cache",
	    sizeof (smb_shr_notify_t), 8, NULL, NULL, NULL, NULL, NULL, 0);

	smb_nlist_create();

	smb_thread_init(&smb_shrmgr.sm_notify_thread, "smb_thread_notify",
	    smb_shrmgr_notify_handler, NULL);

	if ((rc = smb_thread_start(&smb_shrmgr.sm_notify_thread)) != 0) {
		cmn_err(CE_NOTE, "publish: failed to start the notify thread");
		smb_shrmgr_notify_fini();
	}

	return (rc);
}

/*
 * Destroys the notification cache, list and thread
 */
static void
smb_shrmgr_notify_fini(void)
{
	smb_nlist_purge();
	smb_thread_stop(&smb_shrmgr.sm_notify_thread);
	smb_thread_destroy(&smb_shrmgr.sm_notify_thread);
	smb_nlist_destroy();
	kmem_cache_destroy(smb_shrmgr.sm_cache_notify);
}

/*
 * Wakes up notify handler thread
 */
static void
smb_shrmgr_notify_signal(void)
{
	smb_thread_signal(&smb_shrmgr.sm_notify_thread);
}

/*
 * Remove all pending unpublish events from the notify list
 */
static void
smb_shrmgr_notify_stop(void)
{
	smb_nlist_purge();
}

/*
 * Creates a publish, unpublish, or republish event and insert it at the
 * end of the notify list. Notify handler thread should be signaled by
 * the caller to pick up the new event.
 */
static void
smb_shrmgr_notify_add(uint32_t op, const smb_share_t *si,
    const char *new_container, uint32_t event_id)
{
	smb_shr_notify_t *sn;

	sn = kmem_cache_alloc(smb_shrmgr.sm_cache_notify, KM_SLEEP);
	bzero(sn, sizeof (smb_shr_notify_t));

	sn->sn_magic = SMB_SHR_NOTIFY_MAGIC;
	sn->sn_op = op;
	sn->sn_eventid = event_id;
	if (op != SMB_SHARE_NOP_POPULATE) {
		ASSERT(si);
		ASSERT(si->shr_magic == SMB_SHARE_MAGIC);

		sn->sn_kid = si->shr_kid;
		sn->sn_name = smb_mem_strdup(si->shr_name);
		sn->sn_path = smb_mem_strdup(si->shr_path);
		sn->sn_dfsroot = ((si->shr_flags & SMB_SHRF_DFSROOT) != 0);
		if (si->shr_container)
			sn->sn_container = smb_mem_strdup(si->shr_container);
		if (new_container)
			sn->sn_newcontainer = smb_mem_strdup(new_container);
	}

	smb_nlist_insert_tail(sn);
}

/*
 * Frees any memory allocated for the given notify structure
 */
static void
smb_shrmgr_notify_free(smb_shr_notify_t *sn)
{
	if (sn == NULL)
		return;

	ASSERT(sn->sn_magic == SMB_SHR_NOTIFY_MAGIC);

	sn->sn_magic = (uint32_t)~SMB_SHR_NOTIFY_MAGIC;
	smb_mem_free(sn->sn_name);
	smb_mem_free(sn->sn_path);
	smb_mem_free(sn->sn_container);
	smb_mem_free(sn->sn_newcontainer);
	kmem_cache_free(smb_shrmgr.sm_cache_notify, sn);
}

/*
 * Populates the notification list based on the current content
 * of shares AVL if smb_shrmgr is in ACTIVATING state which means
 * smbd was just enabled. After the list is populated smb_shrmgr
 * is transferred to the ACTIVE state.
 */
static void
smb_shrmgr_notify_populate(void)
{
	smb_avl_t *share_avl = smb_shrmgr.sm_share_avl;
	smb_share_t *si, *next_si;

	smb_shrmgr_rdlock();
	if (smb_shrmgr.sm_state != SMB_SHRMGR_STATE_ACTIVATING) {
		smb_shrmgr_unlock();
		return;
	}

	si = smb_avl_first(share_avl);

	while (si != NULL) {
		smb_shrmgr_notify_add(SMB_SHARE_NOP_PUBLISH, si, NULL, 0);
		next_si = smb_avl_next(share_avl, si);
		smb_avl_release(share_avl, si);
		si = next_si;
	}
	smb_shrmgr.sm_state = SMB_SHRMGR_STATE_ACTIVE;
	smb_shrmgr_unlock();
}

/*
 * Each time a new share is added, a publish event is added to
 * the end of the notify list. Each time a share is removed an
 * unpublish event is added to the end of the notify list.
 *
 * The job of this function which executes in a separate thread is to
 * pick up events from the notify list and execute the event according
 * to its type. The events are processed sequentially starting from the
 * head of the list.
 *
 * The processing of the list happens only when smbd is running.
 * When smbd starts up, the list is populated based on the existing
 * shares in the AVL cache and then this thread starts processing
 * the events from the head of the list.
 *
 * When all the events in the list are processed this thread goes to
 * sleep waiting for new events to arrive. Each event is removed from
 * the list before it is processed and is destroyed after the processing
 * is finished. Upon receiving new events all the events in the list will
 * be processed.
 *
 * For publishing smbd is contacted via a door call to perform
 * what's needed in userspace. Shares AVL is looked up to make
 * sure the requested share still exists before calling smbd.
 *
 * For unpublishing, all existing connections to the specified
 * share will be torn down.
 */
static void /*ARGSUSED*/
smb_shrmgr_notify_handler(smb_thread_t *thread, void *arg)
{
	smb_shr_notify_t *sn;
	smb_share_t *si;
	int rc;

	while (smb_thread_continue(thread)) {
		while ((sn = smb_nlist_head()) != NULL) {
			ASSERT(sn);
			ASSERT(sn->sn_magic == SMB_SHR_NOTIFY_MAGIC);

			switch (sn->sn_op) {
			case SMB_SHARE_NOP_PUBLISH:
				si = smb_shrmgr_share_lookup(sn->sn_name);
				if (si == NULL)
					break;

				if (si->shr_kid == sn->sn_kid) {
					rc = smb_kdoor_upcall(
					    SMB_DR_SHR_NOTIFY, sn,
					    smb_shr_notify_xdr, NULL, NULL);
					if (rc != 0)
						cmn_err(CE_WARN,
						    "unable to notify smbd for"
						    " publishing %s (%d)",
						    sn->sn_name, rc);
				}

				smb_shrmgr_share_release(si);
				break;

			case SMB_SHARE_NOP_UNPUBLISH:
				(void) smb_server_unshare(sn->sn_kid);
				(void) smb_kdoor_upcall(SMB_DR_SHR_NOTIFY,
				    sn, smb_shr_notify_xdr, NULL, NULL);
				break;

			case SMB_SHARE_NOP_REPUBLISH:
				(void) smb_kdoor_upcall(SMB_DR_SHR_NOTIFY,
				    sn, smb_shr_notify_xdr, NULL, NULL);
				break;

			case SMB_SHARE_NOP_POPULATE:
				smb_shrmgr_notify_populate();
				break;

			default:
				break;
			}

			smb_event_signal(sn->sn_eventid);
			smb_shrmgr_notify_free(sn);
		}
	}
}

/*
 * This function is registered to be called by sharefs system call
 */
static int
smb_shrmgr_shareop(sharefs_op_t opcode, void *data, size_t datalen)
{
	nvlist_t	*shrlist;
	char		*shrbuf;
	int		rc;

	/*
	 * can only share in the global zone.
	 */
	if (curproc->p_zone->zone_id != GLOBAL_ZONEID)
		return (ENOTSUP);

	shrbuf = smb_mem_alloc(datalen);
	if (ddi_copyin((const void *)data, shrbuf, datalen, 0)) {
		smb_mem_free(shrbuf);
		return (EFAULT);
	}

	if ((rc = nvlist_unpack(shrbuf, datalen, &shrlist, KM_SLEEP)) != 0) {
		smb_mem_free(shrbuf);
		return (rc);
	}

	if ((rc = smb_shrmgr_secpolicy_share(shrlist)) == 0) {
		switch (opcode) {
		case SHAREFS_PUBLISH:
			rc = smb_shrmgr_publish(shrlist);
			break;
		case SHAREFS_UNPUBLISH:
			rc = smb_shrmgr_unpublish(shrlist);
			break;
		default:
			rc = EINVAL;
		}
	}

	nvlist_free(shrlist);
	smb_mem_free(shrbuf);

	return (rc);
}

/*
 * Initialize a share_t structure based on the give smb_share_t
 * to be added to sharetab.
 */
static int
smb_kshare_update_sharetab(smb_share_t *si, sharefs_op_t opcode)
{
	char	*options;
	char	*shrname;
	size_t	len;
	int	rc;

	ASSERT(si);

	switch (opcode) {
	case SHAREFS_PUBLISH:
		options = smb_mem_zalloc(SMB_OPTIONS_STRLEN);
		*options = '-';
		len = 0;
		if (si->shr_flags & SMB_SHRF_ABE)
			len += snprintf(options + len, SMB_OPTIONS_STRLEN - len,
			"%s,", SHOPT_ABE);

		if (si->shr_flags & SMB_SHRF_CATIA)
			len += snprintf(options + len, SMB_OPTIONS_STRLEN - len,
			"%s,", SHOPT_CATIA);

		if (si->shr_flags & SMB_SHRF_DFSROOT)
			len += snprintf(options + len, SMB_OPTIONS_STRLEN - len,
			"%s,", SHOPT_DFSROOT);

		if (si->shr_flags & SMB_SHRF_GUEST_OK)
			len += snprintf(options + len, SMB_OPTIONS_STRLEN - len,
			"%s,", SHOPT_GUEST);

		if (si->shr_container != NULL)
			len += snprintf(options + len, SMB_OPTIONS_STRLEN - len,
			"%s=%s,", SHOPT_AD_CONTAINER, si->shr_container);

		if (si->shr_flags & SMB_SHRF_CSC_MASK)
			len += snprintf(options + len, SMB_OPTIONS_STRLEN - len,
			"%s=%s,", SHOPT_CSC, smb_kshare_csc_name(si));

		if (si->shr_access_rw != NULL)
			len += snprintf(options + len, SMB_OPTIONS_STRLEN - len,
			"%s=%s,", SHOPT_RW, si->shr_access_rw);

		if (si->shr_access_ro != NULL)
			len += snprintf(options + len, SMB_OPTIONS_STRLEN - len,
			"%s=%s,", SHOPT_RO, si->shr_access_ro);

		if (si->shr_access_none != NULL)
			len += snprintf(options + len, SMB_OPTIONS_STRLEN - len,
			"%s=%s,", SHOPT_NONE, si->shr_access_none);

		/* remove the last comma */
		if (len > 0)
			options[len - 1] = '\0';

		rc = sharetab_add(si->shr_nvdata, SMB_FSTYPE, options);
		/*
		 * original share is no longer needed,
		 * it will be freed by caller
		 */
		si->shr_nvdata = NULL;
		smb_mem_free(options);
		break;
	case SHAREFS_UNPUBLISH:
		shrname = smb_mem_strdup(si->shr_name);
		rc = sharetab_remove(shrname, SMB_FSTYPE);
		smb_mem_free(shrname);
		break;
	default:
		rc = EINVAL;
		break;
	}

	return (rc);
}

/*
 * Encodes the specified share into svcenum structure
 * based on share properties and the given enumeration
 * mode.
 */
static int
smb_kshare_enum(smb_share_t *si, smb_svcenum_t *svcenum)
{
	smb_svcenum_qualifier_t *seq;
	uint8_t *pb;
	uint_t nbytes;
	int rc;

	if (svcenum->se_nskip > 0) {
		svcenum->se_nskip--;
		return (0);
	}

	seq = &svcenum->se_qualifier;

	if ((seq->seq_mode == SMB_SVCENUM_SHARE_PERM) &&
	    (si->shr_flags & SMB_SHRF_TRANS))
		return (0);

	if ((seq->seq_mode == SMB_SVCENUM_SHARE_TRANS) &&
	    (si->shr_flags & SMB_SHRF_PERM))
		return (0);

	if ((seq->seq_mode == SMB_SVCENUM_SHARE_RPC) &&
	    (si->shr_flags & SMB_SHRF_AUTOHOME)) {
		if (smb_strcasecmp(seq->seq_qualstr, si->shr_name,
		    sizeof (seq->seq_qualstr)) != 0)
			return (0);
	}

	if (svcenum->se_nitems >= svcenum->se_nlimit) {
		svcenum->se_nitems = svcenum->se_nlimit;
		return (-1);
	}

	pb = &svcenum->se_buf[svcenum->se_bused];
	rc = smb_share_encode(si, pb, svcenum->se_bavail, &nbytes);
	if (rc == 0) {
		svcenum->se_bavail -= nbytes;
		svcenum->se_bused += nbytes;
		svcenum->se_nitems++;
	}

	return (rc);
}

/*
 * Create the share path required by the share enum calls. The path
 * is created in a heap buffer ready for use by the caller.
 *
 * Some Windows over-the-wire backup applications do not work unless a
 * drive letter is present in the share path.  We don't care about the
 * drive letter since the path is fully qualified with the volume name.
 *
 * Windows clients seem to be mostly okay with forward slashes in
 * share paths but they cannot handle one immediately after the drive
 * letter, i.e. B:/.  For consistency we convert all the slashes in
 * the path.
 *
 * Returns a pointer to a heap buffer containing the share path, which
 * could be a null pointer if the heap allocation fails.
 */
static char *
smb_kshare_winpath(const char *path, uchar_t drive_letter)
{
	char *winpath;
	char *p;

	if ((strlen(path) == 0) || (drive_letter == '\0'))
		return (smb_mem_strdup(""));

	winpath = smb_mem_alloc(MAXPATHLEN);
	if (drive_letter != 'B') {
		(void) snprintf(winpath, MAXPATHLEN, "%c:\\", drive_letter);
		return (winpath);
	}

	/*
	 * Strip the volume name from the path (/vol1/home -> /home).
	 */
	p = (char *)path;
	p += strspn(p, "/");
	p = strchr(p, '/');
	if (p != NULL) {
		p += strspn(p, "/");
		(void) snprintf(winpath, MAXPATHLEN, "%c:/%s", 'B', p);
	} else {
		(void) snprintf(winpath, MAXPATHLEN, "%c:/", 'B');
	}
	(void) strsubst(winpath, '/', '\\');

	return (winpath);
}

/*
 * Looks up the vnode for the given share path
 */
static int
smb_kshare_lookuppnvp(smb_share_t *si, vnode_t **sharevp)
{
	pathname_t pn;
	vnode_t *dvp;
	int rc;

	pn_alloc(&pn);
	if ((rc = pn_set(&pn, si->shr_path)) != 0)
		return (rc);

	dvp = rootdir;
	VN_HOLD(dvp);

	rc = lookuppnvp(&pn, NULL, NO_FOLLOW, NULL, sharevp, rootdir, dvp,
	    kcred);
	pn_free(&pn);

	return (rc);
}

/*
 * Creates the notification list
 */
static void
smb_nlist_create(void)
{
	smb_llist_constructor(&smb_shrmgr.sm_notify_list,
	    sizeof (smb_shr_notify_t), offsetof(smb_shr_notify_t, sn_lnd));
}

/*
 * Removes and frees any existing entries in the notification list
 * and then destroys the list
 */
static void
smb_nlist_destroy(void)
{
	smb_llist_t		*nlist = &smb_shrmgr.sm_notify_list;
	smb_shr_notify_t	*notify;

	smb_llist_enter(nlist, RW_WRITER);
	while ((notify = smb_llist_head(nlist)) != NULL) {
		smb_llist_remove(nlist, notify);
		kmem_cache_free(smb_shrmgr.sm_cache_notify, notify);
	}
	smb_llist_exit(nlist);
	smb_llist_destructor(nlist);
}

/*
 * Inserts the given notification item at the end of the notify list
 */
static void
smb_nlist_insert_tail(smb_shr_notify_t *notify)
{
	smb_llist_t	*nlist = &smb_shrmgr.sm_notify_list;

	ASSERT(notify);
	ASSERT(notify->sn_magic == SMB_SHR_NOTIFY_MAGIC);

	smb_llist_enter(nlist, RW_WRITER);
	smb_llist_insert_tail(nlist, notify);
	smb_llist_exit(nlist);
}

/*
 * Removes the first entry in the list and returns it to the caller.
 * The caller is responsible for destroying the returned entry.
 */
static smb_shr_notify_t *
smb_nlist_head(void)
{
	smb_llist_t		*nlist = &smb_shrmgr.sm_notify_list;
	smb_shr_notify_t	*notify;

	smb_llist_enter(nlist, RW_READER);
	if ((notify = smb_llist_head(nlist)) != NULL)
		smb_llist_remove(nlist, notify);
	smb_llist_exit(nlist);
	return (notify);
}

/*
 * Removes all the entries from the notify list
 */
static void
smb_nlist_purge(void)
{
	smb_llist_t		*nlist = &smb_shrmgr.sm_notify_list;
	smb_shr_notify_t	*ncur;

	smb_llist_enter(nlist, RW_WRITER);
	while ((ncur = smb_llist_head(nlist)) != NULL) {
		smb_llist_remove(nlist, ncur);
		ncur->sn_magic = (uint32_t)~SMB_SHR_NOTIFY_MAGIC;
		kmem_cache_free(smb_shrmgr.sm_cache_notify, ncur);
	}
	smb_llist_exit(nlist);
}
