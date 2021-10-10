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

#include <sys/systm.h>
#include <sys/sunddi.h>
#include <sys/cmn_err.h>
#include <sys/ksynch.h>
#include <sys/avl.h>
#include <sys/nvpair.h>
#include <sys/sysmacros.h>
#include <sys/policy.h>
#include <sys/u8_textprep.h>
#include <sys/sa_share.h>

#include <sharefs/sharefs.h>

/*
 * Cache states
 */
#define	SC_STATE_NONE		0
#define	SC_STATE_CREATED	1
#define	SC_STATE_DESTROYING	2

/*
 * Cache lock modes
 */
#define	SC_RDLOCK	0
#define	SC_WRLOCK	1

#define	SHTAB_PROT_NFS	0
#define	SHTAB_PROT_SMB	1
#define	SHTAB_PROT_CNT	2

/*
 * sharetab cache handle
 *
 * sc_state		cache state machine values
 * sc_id		Current cache id
 * sc_scn_id		id to assign to next name cache node
 * sc_mpn_id		id to assign to next mntpnt cache node
 * sc_size		size in bytes. used to create snapshot
 * sc_count		number of shtab entries in cache
 * sc_generation	increments on cache updates
 * sc_mtime		modification time
 * sc_nops		number of inflight/pending cache operations
 * sc_name_cache	avl tree, shares sorted by name
 * sc_mntpnt_cache	avl tree, sorted by mountpoint
 * sc_mntpnt_id_cache	avl tree, mntpnts sorted by mpn_id
 * sc_lock		synchronize cache read/write accesses
 * sc_mtx		protects cache handle fields
 * sc_cv		cond variable used to signal end of operation
 */
typedef struct shtab_cache {
	uint32_t	sc_state;
	uint32_t	sc_id;
	uint32_t	sc_scn_id;
	uint32_t	sc_mpn_id;
	size_t		sc_size;
	uint_t		sc_count;
	uint_t		sc_generation;
	timestruc_t	sc_mtime;
	uint32_t	sc_nops;
	avl_tree_t	sc_name_cache;
	avl_tree_t	sc_mntpnt_cache;
	avl_tree_t	sc_mntpnt_id_cache;
	krwlock_t	sc_lock;
	kmutex_t	sc_mtx;
	kcondvar_t	sc_cv;
} shtab_cache_t;

/*
 * Mountpoint cache node
 *
 * mpn_mntpnt		mountpoint string
 * mpn_id		unique mountpoint cache node identifier
 * mpn_sh_list		avl_tree of shares for this mountpoint
 * mpn_name_avl		avl node in sc_mntpnt_cache
 * mpn_id_avl		avl node in sc_mntpnt_id_cache
 */
typedef struct mntpnt_node {
	char		*mpn_mntpnt;
	uint32_t	mpn_id;
	avl_tree_t	mpn_sh_list;
	avl_node_t	mpn_name_avl;
	avl_node_t	mpn_id_avl;
} mp_node_t;

/*
 * sharetab entry info
 */
typedef struct ste_info_s {
	char *ste_optstr;
	size_t ste_size;
} ste_info_t;

/*
 * Share cache node
 *
 * scn_share		ptr to share (nvlist_t)
 * scn_sh_name		points to name in scn_share;
 * scn_id		unique share cache node identifier
 * scn_proto		list of active protocols
 * scn_ste[]		sharetab entry info, one for each protocol type
 * scn_mpn		ptr to mp_node_t
 * scn_name_avl		avl node in name cache
 * scn_mp_avl		avl node in mp_node_t.mpn_sh_list
 */
typedef struct sc_node {
	nvlist_t	*scn_share;
	char		*scn_sh_name;
	uint32_t	scn_id;
	sa_proto_t	scn_proto;
	ste_info_t	scn_ste[SHTAB_PROT_CNT];
	mp_node_t	*scn_mpn;
	avl_node_t	scn_name_avl;
	avl_node_t	scn_mp_avl;
} sc_node_t;

typedef struct sharetab_modhandle {
	kmutex_t	smh_mutex;
	ddi_modhandle_t	smh_sharefs;
	ddi_modhandle_t	smh_nfs;
	ddi_modhandle_t	smh_smb;
} sharetab_modhandle_t;

typedef struct sharefs_sops_s {
	krwlock_t	sop_lock;
	sharefs_sop_t	sop_nfs;
	sharefs_sop_t	sop_smb;
} sharefs_sops_t;

/*
 * contains a secpolicy callback routine
 * based on file system type
 */
typedef struct sharefs_secop_s {
	struct sharefs_secop_s *secop_next;
	int			secop_fstype;
	sharefs_secpolicy_op_t	secop_func;
} sharefs_secop_t;

/*
 * A list of secpolicy callback functions
 * Up to one secpolicy routine can be registered
 * per file system type.
 */
typedef struct sharefs_secpolicy_ops_s {
	krwlock_t	sp_lock;
	sharefs_secop_t	*sp_list;
} sharefs_secpolicy_ops_t;

static int shtab_cache_lock(shtab_cache_t *, int);
static void shtab_cache_unlock(shtab_cache_t *);
static sc_node_t *shtab_find_cache_node(shtab_cache_t *, char *);
static mp_node_t *shtab_find_mntpnt_node(shtab_cache_t *, char *, boolean_t);
static void shtab_rem_from_mpn_shlist(shtab_cache_t *, mp_node_t *,
    sc_node_t *);
static uint32_t shtab_get_scn_id(shtab_cache_t *);
static uint32_t shtab_get_mpn_id(shtab_cache_t *);
static int shtab_name_cmp(const void *, const void *);
static int shtab_name_id_cmp(const void *, const void *);
static int shtab_mntpnt_cmp(const void *, const void *);
static int shtab_mntpnt_id_cmp(const void *, const void *);
static int shtab_prot_idx(sa_proto_t);
static void shtab_free_entry_info(ste_info_t *);

/*
 * global data
 */
sharetab_modhandle_t sharetab_mh;
sharefs_sops_t sharefs_sops;
sharefs_secpolicy_ops_t sharefs_secpolicy_ops;

/*
 * shtab_cache_init
 *
 * Create the share cache
 */
void
shtab_cache_init(sharefs_zone_t *szp)
{
	shtab_cache_t *cachep;

	cachep = kmem_zalloc(sizeof (shtab_cache_t), KM_SLEEP);

	mutex_init(&cachep->sc_mtx, NULL, MUTEX_DEFAULT, NULL);
	mutex_enter(&cachep->sc_mtx);

	avl_create(&cachep->sc_name_cache,
	    shtab_name_cmp, sizeof (sc_node_t),
	    offsetof(sc_node_t, scn_name_avl));

	avl_create(&cachep->sc_mntpnt_cache,
	    shtab_mntpnt_cmp, sizeof (mp_node_t),
	    offsetof(mp_node_t, mpn_name_avl));

	avl_create(&cachep->sc_mntpnt_id_cache,
	    shtab_mntpnt_id_cmp, sizeof (mp_node_t),
	    offsetof(mp_node_t, mpn_id_avl));

	rw_init(&cachep->sc_lock, NULL, RW_DEFAULT, NULL);
	cv_init(&cachep->sc_cv, NULL, CV_DEFAULT, NULL);
	cachep->sc_id = 1; /* shared_smf_get_contract(); */
	cachep->sc_scn_id = 1;
	cachep->sc_mpn_id = 1;
	cachep->sc_nops = 0;
	cachep->sc_size = 0;
	cachep->sc_count = 0;
	cachep->sc_generation = 1;
	gethrestime(&cachep->sc_mtime);
	cachep->sc_state = SC_STATE_CREATED;

	mutex_exit(&cachep->sc_mtx);

	szp->sz_shtab_cache = cachep;
}

/*
 * shtab_cache_fini
 *
 * destroy all avl trees and free resources.
 */
void
shtab_cache_fini(sharefs_zone_t *szp)
{
	void *cookie;
	sc_node_t *scn;
	mp_node_t *mpn;
	int prot_idx;
	shtab_cache_t *cachep;

	cachep = (shtab_cache_t *)szp->sz_shtab_cache;
	ASSERT(cachep);

	mutex_enter(&cachep->sc_mtx);

	if (cachep->sc_state == SC_STATE_CREATED) {
		/*
		 * prevent new transactions by
		 * causing lock to fail.
		 */
		cachep->sc_state = SC_STATE_DESTROYING;

		/*
		 * wait for outstanding transactions and threads to complete
		 */
		while (cachep->sc_nops > 0)
			cv_wait(&cachep->sc_cv, &cachep->sc_mtx);

		ASSERT(cachep->sc_count == 0);

		/*
		 * destroy mntpnt id avl tree
		 */
		cookie = NULL;
		while ((mpn = avl_destroy_nodes(
		    &cachep->sc_mntpnt_id_cache, &cookie)) != NULL) {
			/*
			 * mpn will be free'd when
			 * sc_mntpnt_cache is destroyed below.
			 */
			continue;
		}
		avl_destroy(&cachep->sc_mntpnt_id_cache);

		/*
		 * destroy mntpnt avl tree
		 */
		cookie = NULL;
		while ((mpn = avl_destroy_nodes(&cachep->sc_mntpnt_cache,
		    &cookie)) != NULL) {
			/*
			 * destroy share list for this node
			 */
			void *shl_cookie = NULL;
			while ((scn = avl_destroy_nodes(&mpn->mpn_sh_list,
			    &shl_cookie)) != NULL) {
				/*
				 * the share cache node will be freed when
				 * the share cache tree is destroyed below.
				 */
				continue;
			}
			avl_destroy(&mpn->mpn_sh_list);
			strfree(mpn->mpn_mntpnt);
			kmem_free(mpn, sizeof (mp_node_t));
		}
		avl_destroy(&cachep->sc_mntpnt_cache);

		/*
		 * destroy the share cache avl tree, freeing the node as well
		 */
		cookie = NULL;
		while ((scn = avl_destroy_nodes(&cachep->sc_name_cache,
		    &cookie)) != NULL) {
			for (prot_idx = SHTAB_PROT_NFS;
			    prot_idx < SHTAB_PROT_CNT; ++prot_idx) {
				shtab_free_entry_info(&scn->scn_ste[prot_idx]);
			}
			sa_share_free(scn->scn_share);
			kmem_free(scn, sizeof (sc_node_t));
		}
		avl_destroy(&cachep->sc_name_cache);

		rw_destroy(&cachep->sc_lock);
		cv_destroy(&cachep->sc_cv);

		cachep->sc_state = SC_STATE_NONE;
	}
	mutex_exit(&cachep->sc_mtx);
	mutex_destroy(&cachep->sc_mtx);

	kmem_free(cachep, sizeof (shtab_cache_t));

	szp->sz_shtab_cache = NULL;
}

/*
 * shtab_is_empty
 *
 * Returns B_TRUE there are no nodes in name_cache
 */
boolean_t
shtab_is_empty(sharefs_zone_t *szp)
{
	boolean_t empty;
	shtab_cache_t *cachep;

	cachep = (shtab_cache_t *)szp->sz_shtab_cache;
	ASSERT(cachep);

	if (shtab_cache_lock(cachep, SC_RDLOCK) != 0)
		return (B_TRUE);

	empty = avl_is_empty(&cachep->sc_name_cache);

	shtab_cache_unlock(cachep);

	return (empty);
}

/*
 * shtab_stats
 *
 * Updates the passed in stats structure with the latest
 * values from cache.
 */
int
shtab_stats(sharefs_zone_t *szp, shtab_stats_t *stats)
{
	shtab_cache_t *cachep;

	cachep = (shtab_cache_t *)szp->sz_shtab_cache;
	ASSERT(cachep);

	bzero(stats, sizeof (shtab_stats_t));

	if (shtab_cache_lock(cachep, SC_RDLOCK) != 0)
		return (0);

	stats->sts_size = cachep->sc_size;
	stats->sts_count = cachep->sc_count;
	stats->sts_generation = cachep->sc_generation;
	stats->sts_mtime = cachep->sc_mtime;

	shtab_cache_unlock(cachep);
	return (0);
}

/*
 * shtab_cache_add
 *
 * Add (or update) a share to(in) the cache.
 *
 * NOTE: a copy of the share is added to the cache.
 * Therefore it is the responsibilty of the caller to
 * free the share.
 *
 * RETURNS:
 *    0:      share is successfully updated in cache
 *    EINVAL: passed share is NULL or
 *                share does not contain 'name' property or
 *                share does not contain 'mntpnt' property
 *    EAGAIN: share cache has not been created.
 */
static int
shtab_cache_add(sharefs_zone_t *szp, nvlist_t *share,
    char *sh_fstype, char *optstr)
{
	sa_proto_t prot, p;
	int prot_idx;
	char *sh_name;
	char *sh_mntpnt;
	char *sh_path;
	char *sh_desc;
	char *ste_opts;
	size_t ste_size;
	sc_node_t *scn;
	mp_node_t *mpn;
	shtab_cache_t *cachep;
	nvlist_t *prot_nvl;

	cachep = (shtab_cache_t *)szp->sz_shtab_cache;
	ASSERT(cachep);

	if (shtab_cache_lock(cachep, SC_WRLOCK) != 0)
		return (EAGAIN);

	if (share == NULL ||
	    (sh_name = sa_share_get_name(share)) == NULL ||
	    (sh_path = sa_share_get_path(share)) == NULL) {
		shtab_cache_unlock(cachep);
		return (EINVAL);
	}

	/*
	 * Some shares (ie IPC$) do not have paths
	 * convert to '-' so parsing code
	 * will see all fields.
	 */
	if (*sh_path == '\0')
		sh_path = "-";
	/*
	 * If the mountpoint is not specified
	 * (possibly because this is not a disk share)
	 * add it to the mountpoint cache for empty string
	 */
	if ((sh_mntpnt = sa_share_get_mntpnt(share)) == NULL) {
		(void) sa_share_set_mntpnt(share, "");
		sh_mntpnt = sa_share_get_mntpnt(share);
	}

	if ((sh_desc = sa_share_get_desc(share)) == NULL)
		sh_desc = "";

	prot = sa_val_to_proto(sh_fstype);
	prot_idx = shtab_prot_idx(prot);

	if (optstr != NULL)
		ste_opts = strdup(optstr);
	else
		ste_opts = strdup("");

	/*
	 * Calculate size of sharetab entry including
	 * field separators and the EOL.
	 */

	ste_size = strlen(sh_path) +
	    strlen(sh_name) + strlen(sh_fstype) +
	    strlen(ste_opts) + strlen(sh_desc) + 5;

	if ((scn = shtab_find_cache_node(cachep, sh_name)) == NULL) {
		/*
		 * no share, add to cache
		 */
		scn = kmem_zalloc(sizeof (sc_node_t), KM_SLEEP);

		/*
		 * get mntpnt_node for this mountpoint
		 * allocate if it does not exist
		 */
		mpn = shtab_find_mntpnt_node(cachep, sh_mntpnt, B_TRUE);

		/*
		 * initialize and add to share cache
		 */
		(void) nvlist_dup(share, &scn->scn_share, KM_SLEEP);

		scn->scn_sh_name = sa_share_get_name(scn->scn_share);
		scn->scn_id = shtab_get_scn_id(cachep);
		scn->scn_mpn = mpn;
		scn->scn_ste[prot_idx].ste_optstr = ste_opts;
		scn->scn_ste[prot_idx].ste_size = ste_size;
		scn->scn_proto |= prot;
		avl_add(&cachep->sc_name_cache, scn);

		/*
		 * add to mntpnt node share list
		 */
		avl_add(&mpn->mpn_sh_list, scn);
	} else {
		/*
		 * share exists, update share in node
		 */
		nvlist_t *old_share;
		char *old_sh_mntpnt;

		old_share = scn->scn_share;
		ASSERT(old_share);
		old_sh_mntpnt = sa_share_get_mntpnt(old_share);
		ASSERT(old_sh_mntpnt);

		if (strcmp(old_sh_mntpnt, sh_mntpnt) != 0) {
			/*
			 * different mountpoint
			 * move cache node to new mntpnt node
			 */
			mp_node_t *old_mpn;
			mp_node_t *new_mpn;

			old_mpn = scn->scn_mpn;
			new_mpn = shtab_find_mntpnt_node(cachep, sh_mntpnt,
			    B_TRUE);

			/* remove cache node from old mpn_sh_list */
			shtab_rem_from_mpn_shlist(cachep, old_mpn, scn);

			/* add cache node to new mpn_sh_list */
			scn->scn_mpn = new_mpn;
			avl_add(&new_mpn->mpn_sh_list, scn);
		}

		if (prot & scn->scn_proto) {
			/*
			 * this is an update of an existing share
			 * remove old shtab entry info
			 */
			ASSERT(cachep->sc_size >=
			    scn->scn_ste[prot_idx].ste_size);

			cachep->sc_size -= scn->scn_ste[prot_idx].ste_size;
			cachep->sc_count--;
			shtab_free_entry_info(&scn->scn_ste[prot_idx]);
		}

		/* add new shtab entry info */
		scn->scn_ste[prot_idx].ste_optstr = ste_opts;
		scn->scn_ste[prot_idx].ste_size = ste_size;

		/*
		 * copy all other protocol properties to new share
		 */
		for (p = sa_proto_first(); p != SA_PROT_NONE;
		    p = sa_proto_next(p)) {
			if (p == prot)
				continue;
			prot_nvl = sa_share_get_proto(old_share, p);
			if (prot_nvl != NULL)
				(void) sa_share_set_proto(share, p, prot_nvl);
		}

		/* get rid of the old share */
		sa_share_free(old_share);

		/* save the new share */
		(void) nvlist_dup(share, &scn->scn_share, KM_SLEEP);
		scn->scn_sh_name = sa_share_get_name(scn->scn_share);
		scn->scn_proto |= prot;
	}

	cachep->sc_size += ste_size;
	cachep->sc_count++;
	gethrestime(&cachep->sc_mtime);
	cachep->sc_generation++;

	shtab_cache_unlock(cachep);

	return (0);
}

/*
 * shtab_cache_remove
 *
 * Remove share with 'sh_name' from cache
 *
 * RETURNS:
 *    0:      share is successfully added to cache
 *    ENOENT: share with sh_name is not in cache
 *    EINVAL: passed in share name is NULL
 *    EAGAIN: share cache has not been created.
 */
static int
shtab_cache_remove(sharefs_zone_t *szp, char *sh_name, char *sh_fstype)
{
	sc_node_t *scn;
	mp_node_t *mpn;
	shtab_cache_t *cachep;
	int rc;

	cachep = (shtab_cache_t *)szp->sz_shtab_cache;
	ASSERT(cachep);

	if (sh_name == NULL)
		return (EINVAL);

	if (shtab_cache_lock(cachep, SC_WRLOCK) != 0)
		return (EAGAIN);

	if ((scn = shtab_find_cache_node(cachep, sh_name)) != NULL) {
		sa_proto_t proto;
		int prot_idx;

		proto = sa_val_to_proto(sh_fstype);
		prot_idx = shtab_prot_idx(proto);

		if (!(scn->scn_proto & proto)) {
			rc = ENOENT;
			goto unlock_return;
		}

		ASSERT(cachep->sc_size >= scn->scn_ste[prot_idx].ste_size);
		cachep->sc_size -= scn->scn_ste[prot_idx].ste_size;
		cachep->sc_count--;
		cachep->sc_generation++;
		gethrestime(&cachep->sc_mtime);

		scn->scn_proto &= ~proto;
		shtab_free_entry_info(&scn->scn_ste[prot_idx]);
		(void) sa_share_rem_proto(scn->scn_share, proto);

		if (scn->scn_proto == SA_PROT_NONE) {
			mpn = scn->scn_mpn;
			/* remove from mpntpnt node share list */
			shtab_rem_from_mpn_shlist(cachep, mpn, scn);

			/* remove from share cache */
			avl_remove(&cachep->sc_name_cache, scn);

			sa_share_free(scn->scn_share);
			kmem_free(scn, sizeof (sc_node_t));
		}
		rc = 0;
	} else {
		rc = ENOENT;
	}

unlock_return:
	shtab_cache_unlock(cachep);
	return (rc);
}

/*
 * shtab_cache_flush
 *
 * Remove all shares from the cache
 *
 * INPUTS:
 *    szp: zone specific data pointer
 *
 * RETURNS:
 *    0:        successfully flushed all shares.
 *    EAGAIN:   share cache has not been created.
 */
int
shtab_cache_flush(sharefs_zone_t *szp)
{
	shtab_cache_t *cachep;
	sc_node_t *scn;
	mp_node_t *mpn;
	sa_proto_t proto;
	int prot_idx;

	cachep = (shtab_cache_t *)szp->sz_shtab_cache;
	ASSERT(cachep);

	if (shtab_cache_lock(cachep, SC_WRLOCK) != 0)
		return (EAGAIN);

	while ((scn = avl_first(&cachep->sc_name_cache)) != NULL) {
		mpn = scn->scn_mpn;
		/* remove from mpntpnt node share list */
		shtab_rem_from_mpn_shlist(cachep, mpn, scn);

		/* remove from share cache */
		avl_remove(&cachep->sc_name_cache, scn);

		for (proto = sa_proto_first(); proto != SA_PROT_NONE;
		    proto = sa_proto_next(proto)) {
			if (scn->scn_proto & proto) {
				prot_idx = shtab_prot_idx(proto);
				cachep->sc_size -=
				    scn->scn_ste[prot_idx].ste_size;
				cachep->sc_count--;

				scn->scn_proto &= ~proto;
				shtab_free_entry_info(&scn->scn_ste[prot_idx]);
			}
		}

		ASSERT(scn->scn_proto == SA_PROT_NONE);
		sa_share_free(scn->scn_share);
		kmem_free(scn, sizeof (sc_node_t));
	}

	cachep->sc_generation++;
	gethrestime(&cachep->sc_mtime);

	shtab_cache_unlock(cachep);

	return (0);
}

/*
 * shtab_cache_lookup
 *
 * Search the share cache for a share that matches input parameters.
 *
 * INPUTS:
 *    sh_name: name of share. MUST be non-NULL
 *    sh_path: share path. if non-NULL, share must match
 *    proto: SA_PROT_ANY, SA_PROT_NFS, or SA_PROT_SMB
 *
 * OUTPUTS:
 *    sh_buf, sh_buflen: if share is found, it is packed into sh_buf
 *
 * RETURNS:
 *    0:        successful match, share returned in sh_buf
 *    ENOENT:   no match found in cache.
 *    EINVAL:   share does not contain 'name' property
 *    EAGAIN:   share cache has not been created.
 */
int
shtab_cache_lookup(sharefs_zone_t *szp, char *sh_name, char *sh_path,
    uint32_t proto, char *sh_buf, size_t sh_buflen)
{
	sc_node_t *scn;
	char *scn_sh_path;
	int rc = ENOENT;
	shtab_cache_t *cachep;

	if (sh_name == NULL || *sh_name == '\0' ||
	    sh_buf == NULL || sh_buflen == 0)
		return (EINVAL);

	cachep = (shtab_cache_t *)szp->sz_shtab_cache;
	ASSERT(cachep);

	if (shtab_cache_lock(cachep, SC_RDLOCK) != 0)
		return (EAGAIN);

	if ((scn = shtab_find_cache_node(cachep, sh_name)) == NULL)
		goto unlock_return;

	/*
	 * if sh_path is specified, make sure it matches found share
	 */
	if (sh_path != NULL && *sh_path != '\0') {
		if ((scn_sh_path = sa_share_get_path(scn->scn_share)) == NULL) {
			rc = EINVAL;
			goto unlock_return;
		}
		if (strcmp(sh_path, scn_sh_path) != 0)
			goto unlock_return;
	}

	/*
	 * Make sure share is enabled for proto
	 */
	if (scn->scn_proto & proto) {
		(void) sa_share_set_status(scn->scn_share, scn->scn_proto);
		rc = nvlist_pack(scn->scn_share, &sh_buf, &sh_buflen,
		    NV_ENCODE_XDR, KM_SLEEP);
	}

unlock_return:
	shtab_cache_unlock(cachep);
	return (rc);
}

/*
 * shtab_cache_find_init
 *
 * Routine to prepare for retrieving shares from the cache.
 *
 * INPUTS:
 *   sh_mntpnt : mountpoint to search, can be NULL
 *   proto   : protocol type of share to retrieve
 *   hdl     : place holder for returned find handle
 *
 * OUTPUTS
 *   hdl : updated handle.
 *
 * RETURNS:
 *   0 : init was successful, returned hdl is valid and must be deallocated
 *       by calling shtab_cache_find_fini
 *   EAGAIN: share cache has not been initialized
 *   ENOENT: There are no shares for the mountpoint specified.
 *
 * NOTES
 *   If 'sh_mntpnt' is NULL, the entire cache will be searched.
 *   hdl->all_shares is set to indicate all mountpoint caches should
 *   be searched and hdl->mpn_id is set to the first mntpnt node in
 *   the mnppnt_id_cache.
 *
 *   If 'sh_mntpnt is non NULL, only search for shares in the specified
 *   dataset/file system. hdl->all_shares is cleared and hdl->mpn_id is
 *   set to the mntpnt node for the specified mountpoint, if found.
 *
 *   The returned 'hdl' is passed to subsequent calls to
 *   shtab_cache_find_get() and must be freed by calling
 *   shtab_cache_find_fini()
 */
int
shtab_cache_find_init(sharefs_zone_t *szp, char *sh_mntpnt, uint32_t proto,
    sharefs_find_hdl_t *hdl)
{
	int rc = 0;
	mp_node_t *mpn;
	shtab_cache_t *cachep;

	cachep = (shtab_cache_t *)szp->sz_shtab_cache;
	ASSERT(cachep);

	if (shtab_cache_lock(cachep, SC_RDLOCK) != 0)
		return (EAGAIN);

	bzero(hdl, sizeof (sharefs_find_hdl_t));

	if (sh_mntpnt == NULL || *sh_mntpnt == '\0') {
		hdl->all_shares = 1;
		mpn = avl_first(&cachep->sc_mntpnt_id_cache);
	} else {
		hdl->all_shares = 0;
		mpn = shtab_find_mntpnt_node(cachep, sh_mntpnt, B_FALSE);
	}

	if (mpn != NULL) {
		hdl->id = cachep->sc_id;
		hdl->proto = proto;
		hdl->mpn_id = mpn->mpn_id;
	} else {
		rc = ENOENT;
	}

	shtab_cache_unlock(cachep);

	return (rc);
}

/*
 * shtab_cache_find_next
 *
 * Routine to search for and return next share in list.
 *
 * INPUTS:
 *   hdl : initialized in shtab_cache_find_init and updated
 *         in subsequent calls to shtab_cache_find_get.
 *   sh_buf: pointer to caller allocated buffer of sh_buflen
 *   sh_buflen: size of sh_buf.
 *
 * OUTPUTS
 *   sh_buf : contains XDR encoded share nvlist if search successful.
 *
 * RETURNS:
 *   0 : share was found and successfully encoded in sh_buf.
 *   ENOENT: no more shares have been found.
 *   ESTALE: The current mntpnt node cannot be found (hdl->mpn_id)
 *   EAGAIN: share cache has not been initialized
 *
 * NOTES
 *   This routine starts by searching the mntpnt node share list
 *   identified by hdl->mpn_id. If hdl->scn_id is zero start the
 *   search at the beginning of the share list. Otherwise start
 *   with the share following hdl->scn_id.
 *
 *   If the search reaches the end of the mntpnt node share list
 *   without a match, start searching the next mntpnt node share
 *   list only if hdl->all_shares is set.
 *
 *   If a match is found, (both name and proto), XDR encode the
 *   share nvlist into sh_buf and return 0.
 *
 */
int
shtab_cache_find_next(sharefs_zone_t *szp, sharefs_find_hdl_t *hdl,
    char *sh_buf, size_t sh_buflen)
{
	mp_node_t mpn_key;
	mp_node_t *mpn;
	sc_node_t scn_key;
	sc_node_t *scn;
	shtab_cache_t *cachep;
	int rc;

	cachep = (shtab_cache_t *)szp->sz_shtab_cache;
	ASSERT(cachep);

	if (shtab_cache_lock(cachep, SC_RDLOCK) != 0)
		return (EAGAIN);

	/*
	 * make sure the handle is not stale
	 */
	if (hdl->id != cachep->sc_id) {
		shtab_cache_unlock(cachep);
		return (ESTALE);
	}

	/*
	 * hdl->mpn_id was set during find_init
	 * it identifies the current mountpoint node
	 */
	mpn_key.mpn_id = hdl->mpn_id;
	mpn = avl_find(&cachep->sc_mntpnt_id_cache, &mpn_key, NULL);
	if (mpn == NULL) {
		shtab_cache_unlock(cachep);
		return (ESTALE);
	}

	while (mpn != NULL) {

		/*
		 * if the share cache node id in the handle is zero
		 * then start with the head node
		 * else get the next node in the list
		 */
		if (hdl->scn_id == 0) {
			scn = avl_first(&mpn->mpn_sh_list);
		} else {
			scn_key.scn_id = hdl->scn_id;
			scn = avl_find(&mpn->mpn_sh_list, &scn_key, NULL);
			if (scn != NULL)
				scn = AVL_NEXT(&mpn->mpn_sh_list, scn);
		}

		while (scn != NULL) {
			/*
			 * return share if it matches protocol
			 */
			if (hdl->proto & scn->scn_proto) {
				(void) sa_share_set_status(scn->scn_share,
				    scn->scn_proto);
				rc = nvlist_pack(scn->scn_share, &sh_buf,
				    &sh_buflen, NV_ENCODE_XDR, KM_SLEEP);
				if (rc != 0) {
					shtab_cache_unlock(cachep);
					return (rc);
				}
				hdl->mpn_id = mpn->mpn_id;
				hdl->scn_id = scn->scn_id;
				shtab_cache_unlock(cachep);
				return (0);
			}

			/*
			 * otherwise try the next share in the list
			 */
			scn = AVL_NEXT(&mpn->mpn_sh_list, scn);
		}

		/*
		 * no more shares for this mntpnt,
		 */
		hdl->scn_id = 0;
		if (hdl->all_shares) {
			/* process the next mountpoint node */
			mpn = AVL_NEXT(&cachep->sc_mntpnt_id_cache, mpn);
		} else {
			/*
			 * nothing else to do
			 */
			mpn = NULL;
		}
	}

	shtab_cache_unlock(cachep);

	return (ENOENT);
}

/*
 * shtab_cache_find_fini
 */
int
shtab_cache_find_fini(sharefs_zone_t *szp, sharefs_find_hdl_t *hdl)
{
	shtab_cache_t *cachep;

	cachep = (shtab_cache_t *)szp->sz_shtab_cache;
	ASSERT(cachep);

	if (hdl->id != cachep->sc_id)
		return (ESTALE);

	return (0);
}

/*
 * shtab_snap_add
 *
 * Add the share to the sharetab snapshot buffer.
 * One entry is added for each active protocol.
 */
static int
shtab_snap_add(sc_node_t *scn, char *buf, size_t buflen)
{
	nvlist_t *share;
	sa_proto_t prot;
	int prot_idx;
	size_t cnt, tot_cnt = 0;
	char *sh_name;
	char *sh_path;
	char *sh_desc;
	char *sh_fstype;
	char *ste_opts;
	size_t ste_size;

	share = scn->scn_share;

	/*
	 * Some shares (ie IPC$) do not have paths.
	 * Convert to '-' so parsing code
	 * will see all fields.
	 */
	sh_path = sa_share_get_path(share);
	if (sh_path == NULL || *sh_path == '\0')
		sh_path = "-";

	sh_name = sa_share_get_name(share);
	if ((sh_desc = sa_share_get_desc(share)) == NULL)
		sh_desc = "";

	for (prot = sa_proto_first(); prot != SA_PROT_NONE;
	    prot = sa_proto_next(prot)) {
		if (scn->scn_proto & prot) {
			prot_idx = shtab_prot_idx(prot);

			sh_fstype = sa_proto_to_val(prot);
			ste_size = scn->scn_ste[prot_idx].ste_size;
			ste_opts = scn->scn_ste[prot_idx].ste_optstr;
			ASSERT(ste_opts);

			if (ste_size > buflen)
				return (-1);

			cnt = snprintf(buf, ste_size + 1,
			    "%s\t%s\t%s\t%s\t%s\n",
			    sh_path, sh_name, sh_fstype, ste_opts, sh_desc);

			if (cnt != ste_size)
				return (-1);

			tot_cnt += cnt;
			buf += cnt;
			buflen -= cnt;
		}
	}

	return (tot_cnt);
}

/*
 * shtab_snap_create
 *
 * create a large character buffer with a snapshot of the
 * sharetab cache. The shares are formatted in share_t order
 * which each field delimitted with a tab character.
 *
 * This is called by sharefs_open
 */
int
shtab_snap_create(sharefs_zone_t *szp, shnode_t *sft)
{
	char *buf;
	size_t boff;
	size_t cnt;
	sc_node_t *scn;
	shtab_cache_t *cachep;

	cachep = (shtab_cache_t *)szp->sz_shtab_cache;
	ASSERT(cachep);

	if (shtab_cache_lock(cachep, SC_RDLOCK) != 0)
		return (EAGAIN);

	if (sft->sharefs_snap) {
		/*
		 * Nothing has changed, so no need to grab a new copy!
		 */
		if (sft->sharefs_generation == cachep->sc_generation) {
			shtab_cache_unlock(cachep);
			return (0);
		}

		ASSERT(sft->sharefs_size != 0);
		kmem_free(sft->sharefs_snap, sft->sharefs_size + 1);
		sft->sharefs_snap = NULL;
	}

	sft->sharefs_size = cachep->sc_size;
	sft->sharefs_count = cachep->sc_count;

	if (sft->sharefs_size == 0) {
		sft->sharefs_mtime = cachep->sc_mtime;
		shtab_cache_unlock(cachep);
		return (0);
	}

	sft->sharefs_snap = kmem_zalloc(sft->sharefs_size + 1, KM_SLEEP);
	buf = sft->sharefs_snap;
	boff = 0;

	for (scn = avl_first(&cachep->sc_name_cache);
	    scn != NULL;
	    scn = AVL_NEXT(&cachep->sc_name_cache, scn)) {
		cnt = shtab_snap_add(scn, &buf[boff],
		    sft->sharefs_size+1-boff);
		if (cnt == -1)
			goto error_out;

		boff += cnt;
	}

	sft->sharefs_mtime = cachep->sc_mtime;
	sft->sharefs_generation = cachep->sc_generation;

	shtab_cache_unlock(cachep);

	return (0);

error_out:
	kmem_free(sft->sharefs_snap, sft->sharefs_size + 1);
	sft->sharefs_snap = NULL;
	sft->sharefs_count = 0;
	sft->sharefs_size = 0;

	shtab_cache_unlock(cachep);

	return (EFAULT);
}

/*
 * shtab_cache_lock
 *
 * Acquire cache lock for reading or writing.
 *
 * Can only acquire the lock if the cache has been
 * successfully created. (SC_STATE_CREATED)
 * If cache has not been created (NONE or DESTROYING)
 * return immediately with -1.
 *
 * INPUT:
 *   SC_RDLOCK: acquire lock for reading
 *   SC_WRLOCK: acquire lock for writing
 *
 * RETURNS:
 *   0 : lock has been succussfully acquired.
 *  -1 : share cache is not ready.
 */
static int
shtab_cache_lock(shtab_cache_t *cachep, int mode)
{
	mutex_enter(&cachep->sc_mtx);
	if (cachep->sc_state != SC_STATE_CREATED) {
		mutex_exit(&cachep->sc_mtx);
		return (-1);
	}

	cachep->sc_nops++;
	mutex_exit(&cachep->sc_mtx);

	if (mode == SC_RDLOCK)
		rw_enter(&cachep->sc_lock, RW_READER);
	else
		rw_enter(&cachep->sc_lock, RW_WRITER);

	return (0);
}

/*
 * shtab_cache_unlock
 *
 * release the reader/writer cache lock
 * Will also notify waiters of sc_cv
 * shtab_cache_fini will wait for all outstanding transactions
 * and threads to complete.
 */
static void
shtab_cache_unlock(shtab_cache_t *cachep)
{
	mutex_enter(&cachep->sc_mtx);
	ASSERT(cachep->sc_nops > 0);
	cachep->sc_nops--;
	cv_broadcast(&cachep->sc_cv);
	mutex_exit(&cachep->sc_mtx);

	rw_exit(&cachep->sc_lock);
}

/*
 * shtab_find_cache_node
 *
 * search the name cache for a node that matches sh_name
 */
static sc_node_t *
shtab_find_cache_node(shtab_cache_t *cachep, char *sh_name)
{
	sc_node_t scn_key;
	sc_node_t *scn;

	scn_key.scn_sh_name = sh_name;
	scn = avl_find(&cachep->sc_name_cache, &scn_key, NULL);

	return (scn);
}

/*
 * shtab_find_mntpnt_node
 *
 * Returns a ptr to the mntpnt node for mntpnt.
 * If not found and create is B_TRUE, create a new node.
 *
 * In order to create, MUST be called while holding cache write lock
 * otherwise should hold at least read lock.
 */
static mp_node_t *
shtab_find_mntpnt_node(shtab_cache_t *cachep, char *mntpnt, boolean_t create)
{
	mp_node_t *mpn;
	mp_node_t mpn_key;

	mpn_key.mpn_mntpnt = mntpnt;
	mpn = avl_find(&cachep->sc_mntpnt_cache, &mpn_key, NULL);
	if (mpn == NULL && create) {
		/*
		 * mountpoint is not in mntpnt cache
		 * create and add to cache.
		 */
		mpn = kmem_zalloc(sizeof (mp_node_t), KM_SLEEP);

		/* initialize it */
		mpn->mpn_mntpnt = strdup(mntpnt);
		mpn->mpn_id = shtab_get_mpn_id(cachep);

		avl_create(&mpn->mpn_sh_list, shtab_name_id_cmp,
		    sizeof (sc_node_t), offsetof(sc_node_t, scn_mp_avl));

		/* and add it to mntpnt caches */
		avl_add(&cachep->sc_mntpnt_cache, mpn);
		avl_add(&cachep->sc_mntpnt_id_cache, mpn);
	}

	return (mpn);
}

/*
 * Remove a share cache node from the mpn share list.
 * If this is the last entry in the list, cleanup the mntpnt node.
 *
 * MUST be called with write lock
 */
static void
shtab_rem_from_mpn_shlist(shtab_cache_t *cachep, mp_node_t *mpn, sc_node_t *scn)
{
	/* remove from mpntpnt node share list */
	avl_remove(&mpn->mpn_sh_list, scn);

	/*
	 * if mntpnt node share list is now empty,
	 * destroy the mntpnt node share list and
	 * remove the mntpnt node from mntpnt caches.
	 */
	if (avl_numnodes(&mpn->mpn_sh_list) == 0) {
		avl_destroy(&mpn->mpn_sh_list);
		avl_remove(&cachep->sc_mntpnt_id_cache, mpn);
		avl_remove(&cachep->sc_mntpnt_cache, mpn);

		strfree(mpn->mpn_mntpnt);
		kmem_free(mpn, sizeof (mp_node_t));
	}
}

/*
 * shtab_get_scn_id
 *
 * increment cache.sc_scn and return
 */
static uint32_t
shtab_get_scn_id(shtab_cache_t *cachep)
{
	uint32_t id;

	mutex_enter(&cachep->sc_mtx);
	if (++cachep->sc_scn_id == 0)
		cachep->sc_scn_id = 1;
	id = cachep->sc_scn_id;
	mutex_exit(&cachep->sc_mtx);
	return (id);
}

static uint32_t
shtab_get_mpn_id(shtab_cache_t *cachep)
{
	uint32_t id;

	mutex_enter(&cachep->sc_mtx);
	if (++cachep->sc_mpn_id == 0)
		cachep->sc_mpn_id = 1;
	id = cachep->sc_mpn_id;
	mutex_exit(&cachep->sc_mtx);
	return (id);
}

/*
 * shtab_name_cmp
 *
 * avl tree compare routine for sc_name_cache
 * does a utf8 case-insensitive string comparison
 */
static int
shtab_name_cmp(const void *arg1, const void *arg2)
{
	int rc;
	int err = 0;
	const sc_node_t *scn1 = arg1;
	const sc_node_t *scn2 = arg2;
	char *sh_name1 = scn1->scn_sh_name;
	char *sh_name2 = scn2->scn_sh_name;

	rc = u8_strcmp(sh_name1, sh_name2, 0, U8_STRCMP_CI_LOWER,
	    U8_UNICODE_LATEST, &err);

	if (err != 0)
		return (-1);
	if (rc == 0)
		return (0);
	return (rc > 0 ? 1 : -1);
}

/*
 * shtab_name_id_cmp
 *
 * avl tree compare routine for mpn_sh_list
 *
 */
static int
shtab_name_id_cmp(const void *arg1, const void *arg2)
{
	const sc_node_t *scn1 = arg1;
	const sc_node_t *scn2 = arg2;
	uint32_t id1 = scn1->scn_id;
	uint32_t id2 = scn2->scn_id;

	if (id1 == id2)
		return (0);
	else if (id1 > id2)
		return (1);
	else
		return (-1);
}

/*
 * shtab_mntpnt_cmp
 *
 * avl tree compare routine for sc_mntpnt_cache
 */
static int
shtab_mntpnt_cmp(const void *arg1, const void *arg2)
{
	int rc;

	const mp_node_t *mpn1 = arg1;
	const mp_node_t *mpn2 = arg2;
	char *mntpnt1 = mpn1->mpn_mntpnt;
	char *mntpnt2 = mpn2->mpn_mntpnt;

	rc = strcmp(mntpnt1, mntpnt2);
	if (rc == 0)
		return (0);
	return (rc > 0 ? 1 : -1);
}

/*
 * shtab_mntpnt_id_cmp
 *
 * avl tree compare routine for sc_mntpnt_id_cache
 */
static int
shtab_mntpnt_id_cmp(const void *arg1, const void *arg2)
{
	const mp_node_t *mpn1 = arg1;
	const mp_node_t *mpn2 = arg2;
	uint32_t id1 = mpn1->mpn_id;
	uint32_t id2 = mpn2->mpn_id;

	if (id1 == id2)
		return (0);
	else if (id1 > id2)
		return (1);
	else
		return (-1);
}

static int
shtab_prot_idx(sa_proto_t proto)
{
	switch (proto) {
	case SA_PROT_NFS:
		return (SHTAB_PROT_NFS);
	case SA_PROT_SMB:
		return (SHTAB_PROT_SMB);
	default:
		ASSERT(0);
		return (SHTAB_PROT_NFS);
	}
}

static void
shtab_free_entry_info(ste_info_t *infop)
{
	if (infop->ste_optstr != NULL) {
		strfree(infop->ste_optstr);
		infop->ste_optstr = NULL;
	}
	infop->ste_size = 0;
}

/*
 * This function must be called by protocol service modules
 * upon [un]load to [un]register a sharing function.
 *
 * To unregister this function should be called with sop = NULL
 */
int
sharefs_register(sharefs_proto_t proto, sharefs_sop_t sop)
{
	int rc = 0;

	rw_enter(&sharefs_sops.sop_lock, RW_WRITER);
	switch (proto) {
	case SHAREFS_NFS:
		sharefs_sops.sop_nfs = sop;
		break;
	case SHAREFS_SMB:
		sharefs_sops.sop_smb = sop;
		break;
	default:
		rc = EINVAL;
	}
	rw_exit(&sharefs_sops.sop_lock);

	return (rc);
}

/*
 * This function is called by file systems to [un]register a
 * secpolicy function. Up to one secpolicy routine may be
 * registered per file system type.
 *
 * To unregister this function should be called with sec_op = NULL
 */
void
sharefs_secpolicy_register(int fstype, sharefs_secpolicy_op_t sec_op)
{
	sharefs_secop_t *sp_op;

	rw_enter(&sharefs_secpolicy_ops.sp_lock, RW_WRITER);

	/*
	 * update entry if found in list
	 */
	for (sp_op = sharefs_secpolicy_ops.sp_list;
	    sp_op != NULL; sp_op = sp_op->secop_next) {
		if (sp_op->secop_fstype == fstype) {
			sp_op->secop_func = sec_op;
			rw_exit(&sharefs_secpolicy_ops.sp_lock);
			return;
		}
	}

	/*
	 * A secpolicy routine for this file system was not found
	 * in the list, Allocate a new node, initialize and add to
	 * the beginning of the list.
	 */
	sp_op = kmem_zalloc(sizeof (*sp_op), KM_SLEEP);
	sp_op->secop_func = sec_op;
	sp_op->secop_fstype = fstype;
	sp_op->secop_next = sharefs_secpolicy_ops.sp_list;
	sharefs_secpolicy_ops.sp_list = sp_op;
	rw_exit(&sharefs_secpolicy_ops.sp_lock);
}

/*
 * sharefs_secpolicy_share
 *
 * TODO: secpolicy_sys_config() will need to be changed to
 * secpolicy_share() when PRIV_SYS_SHARE becomes available.
 */
int
sharefs_secpolicy_share(char *sh_path)
{
	int rc;
	vnode_t *vp;
	int fstype;
	sharefs_secop_t *sp_op;

	/*
	 * ok if have PRIV_SYS_SHARE
	 */
	if (secpolicy_share(CRED()) == 0)
		return (0);

	/*
	 * no priviledges, how about file system level access
	 * (ie zfs delegation vi zfs allow share)
	 *
	 * First, determine file system type
	 */
	if ((rc = lookupname(sh_path, UIO_SYSSPACE,
	    NO_FOLLOW, NULL, &vp)) != 0)
		return (rc);

	fstype = vp->v_vfsp->vfs_fstype;
	VN_RELE(vp);

	rc = EPERM;
	rw_enter(&sharefs_secpolicy_ops.sp_lock, RW_READER);
	for (sp_op = sharefs_secpolicy_ops.sp_list;
	    sp_op != NULL;
	    sp_op = sp_op->secop_next) {
		if ((sp_op->secop_fstype == fstype) &&
		    (sp_op->secop_func != NULL)) {
			/*
			 * found a registered secpolicy routine
			 * for this file system.
			 */
			rc = sp_op->secop_func(sh_path, CRED());
			break;
		}
	}

	rw_exit(&sharefs_secpolicy_ops.sp_lock);
	return (rc);
}

/*
 * Loads sharefs module.
 *
 * Loads fs/nfs and drv/smbsrv modules if requested
 */
static int
sharefs_modload(sharefs_proto_t proto)
{
	int error;

	mutex_enter(&sharetab_mh.smh_mutex);

	if (sharetab_mh.smh_sharefs == NULL) {
		if ((sharetab_mh.smh_sharefs = ddi_modopen("fs/sharefs",
		    KRTLD_MODE_FIRST, &error)) == NULL) {
			mutex_exit(&sharetab_mh.smh_mutex);
			return (ENOSYS);
		}
	}

	if ((proto == SHAREFS_NFS) && (sharetab_mh.smh_nfs == NULL)) {
		if ((sharetab_mh.smh_nfs =
		    ddi_modopen("fs/nfs", KRTLD_MODE_FIRST, &error)) == NULL) {
			mutex_exit(&sharetab_mh.smh_mutex);
			return (ENOSYS);
		}
	}

	if ((proto == SHAREFS_SMB) && (sharetab_mh.smh_smb == NULL)) {
		if ((sharetab_mh.smh_smb = ddi_modopen("drv/smbsrv",
		    KRTLD_MODE_FIRST, &error)) == NULL) {
			mutex_exit(&sharetab_mh.smh_mutex);
			return (ENOSYS);
		}
	}

	mutex_exit(&sharetab_mh.smh_mutex);
	return (0);
}

int
sharetab_add(nvlist_t *share, char *fstype, char *optstr)
{
	int rc;
	sharefs_zone_t	*szp;

	if ((szp = sharefs_zone_lookup()) == NULL)
		return (ENOENT);

	rc = shtab_cache_add(szp, share, fstype, optstr);

	sharefs_zone_rele(szp);

	return (rc);
}

int
sharetab_remove(char *shrname, char *fstype)
{
	int rc;
	sharefs_zone_t	*szp;

	if ((szp = sharefs_zone_lookup()) == NULL)
		return (ENOENT);

	rc = shtab_cache_remove(szp, shrname, fstype);

	sharefs_zone_rele(szp);
	return (rc);
}

/*
 * Passes the given data and requested share operation to the specified
 * protocol service.
 *
 * Loads the sharefs module and requested protocol service module if
 * needed. Loaded protocol module registers a sharing function upon
 * initialization. The registered function is called by sharefs with
 * the given data and requested operation.
 *
 * The protocol module is responsible for adding/removing sharetab
 * entries by calling sharetab_add/remove functions (above) to update
 * sharetab content upon successful [un]publish operation.
 */
int
sharefs(sharefs_proto_t proto, sharefs_op_t opcode, void *data, size_t datalen)
{
	sharefs_sop_t sop_func;
	int rc;

	if ((proto != SHAREFS_SMB) && (proto != SHAREFS_NFS))
		return (set_errno(EINVAL));

	if ((rc = sharefs_modload(proto)) != 0)
		return (set_errno(rc));

	rw_enter(&sharefs_sops.sop_lock, RW_READER);
	sop_func = (proto == SHAREFS_SMB)
	    ? sharefs_sops.sop_smb : sharefs_sops.sop_nfs;

	if (sop_func == NULL) {
		rw_exit(&sharefs_sops.sop_lock);
		return (set_errno(ENOSYS));
	}

	rc = sop_func(opcode, data, datalen);
	rw_exit(&sharefs_sops.sop_lock);

	return ((rc == 0) ? rc : set_errno(rc));
}
