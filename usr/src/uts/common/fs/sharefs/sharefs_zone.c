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

#include <sys/systm.h>
#include <sharefs/sharefs.h>
#include <sys/sysmacros.h>

/*
 * Lock ordering:
 * (1) sharefs_globals_t	sg_lock
 * (2) sharefs_zone_t		sz_lock
 */

#define	SHTOV(sh)	((sh)->sharefs_file.gfs_vnode)

static sharefs_zone_t *sharefs_zone_new(zone_t *);
static void sharefs_zone_destroy(sharefs_zone_t *);
static void sharefs_zone_enable(sharefs_zone_t *);
static void sharefs_zone_hold(sharefs_zone_t *);
static sharefs_zone_t *sharefs_zone_find(int);

sharefs_globals_t sfs;

sharefs_zone_t *
sharefs_zone_create(void)
{
	return (sharefs_zone_find(1));
}

sharefs_zone_t *
sharefs_zone_lookup(void)
{
	return (sharefs_zone_find(0));
}

static sharefs_zone_t *
sharefs_zone_find(int create)
{
#ifdef DEBUG
	sharefs_zone_t *cszp;
#endif
	zone_t *zonep = curproc->p_zone;
	sharefs_zone_t *szp = NULL;
	sharefs_zone_t *new_szp = NULL;
	int rval;

	for (;;) {
		mutex_enter(&sfs.sg_lock);
		szp = zone_getspecific(sfs.sg_sfszone_key, zonep);
		if (szp != NULL) {
			ASSERT(szp->sz_zonep == zonep);
			sharefs_zone_hold(szp);
			goto out;
		}

		if (create) {
			if (new_szp == NULL) {
				mutex_exit(&sfs.sg_lock);
				new_szp = sharefs_zone_new(zonep);
				continue;
			}
#ifdef DEBUG
			for (cszp = list_head(&sfs.sg_zones); cszp != NULL;
			    cszp = list_next(&sfs.sg_zones, cszp)) {
				ASSERT(cszp->sz_zonep->zone_id !=
				    zonep->zone_id);
			}
#endif
			rval = zone_setspecific(sfs.sg_sfszone_key, zonep,
			    new_szp);
			ASSERT(rval == 0);

			sfs.sg_last_minordev =
			    (sfs.sg_last_minordev + 1) & L_MAXMIN32;
			new_szp->sz_minordev = sfs.sg_last_minordev;

			szp = new_szp;
			new_szp = NULL;
			list_insert_head(&sfs.sg_zones, szp);
			sfs.sg_zone_cnt++;
		}
		break;
	}
out:
	mutex_exit(&sfs.sg_lock);
	if (new_szp != NULL)
		sharefs_zone_destroy(new_szp);
	if (szp != NULL) {
		mutex_enter(&szp->sz_lock);
		if (! szp->sz_enabled)
			sharefs_zone_enable(szp);
		mutex_exit(&szp->sz_lock);
	}
	return (szp);
}

static void
sharefs_zone_enable(sharefs_zone_t *szp)
{
	ASSERT(MUTEX_HELD(&szp->sz_lock));
	ASSERT(! szp->sz_enabled);
	shtab_cache_init(szp);
	szp->sz_enabled = 1;
}

static sharefs_zone_t *
sharefs_zone_new(zone_t *zonep)
{
	sharefs_zone_t *szp;

	szp = kmem_zalloc(sizeof (sharefs_zone_t), KM_SLEEP);
	mutex_init(&szp->sz_lock, NULL, MUTEX_DEFAULT, NULL);
	rw_init(&szp->sz_sharefs_rwlock, NULL, RW_DEFAULT, NULL);
	szp->sz_refcount = 1;
	szp->sz_zonep = zonep;
	zone_init_ref(&szp->sz_zone_ref);
	zone_hold_ref(zonep, &szp->sz_zone_ref, ZONE_REF_SHAREFS);
	return (szp);
}

static void
sharefs_zone_destroy(sharefs_zone_t *szp)
{
	ASSERT(szp->sz_refcount == 1);

	shtab_cache_fini(szp);

	rw_destroy(&szp->sz_sharefs_rwlock);
	mutex_destroy(&szp->sz_lock);
	zone_rele_ref(&szp->sz_zone_ref, ZONE_REF_SHAREFS);
	kmem_free(szp, sizeof (sharefs_zone_t));
}

static void
sharefs_zone_hold(sharefs_zone_t *szp)
{
	mutex_enter(&szp->sz_lock);
	szp->sz_refcount++;
	mutex_exit(&szp->sz_lock);
}

void
sharefs_zone_rele(sharefs_zone_t *szp)
{
#ifdef DEBUG
	sharefs_zone_t *cszp;
#endif
	int rval;

	mutex_enter(&szp->sz_lock);
	ASSERT(szp->sz_refcount > 0);
	if (szp->sz_refcount == 1) {
		ASSERT(shtab_is_empty(szp));
		ASSERT(szp->sz_shnode_count == 0);
		mutex_exit(&szp->sz_lock);
		mutex_enter(&sfs.sg_lock);
		mutex_enter(&szp->sz_lock);
		if (szp->sz_refcount > 1) {
			szp->sz_refcount--;
			mutex_exit(&szp->sz_lock);
			mutex_exit(&sfs.sg_lock);
			return;
		}

		ASSERT(sfs.sg_zone_cnt > 0);
		list_remove(&sfs.sg_zones, szp);
		sfs.sg_zone_cnt--;
#ifdef DEBUG
		cszp = zone_getspecific(sfs.sg_sfszone_key, szp->sz_zonep);
		ASSERT(cszp == szp);
#endif
		rval = zone_setspecific(sfs.sg_sfszone_key, szp->sz_zonep,
		    NULL);
		ASSERT(rval == 0);
		mutex_exit(&szp->sz_lock);
		mutex_exit(&sfs.sg_lock);
		sharefs_zone_destroy(szp);
	} else {
		ASSERT(szp->sz_refcount > 1);
		szp->sz_refcount--;
		mutex_exit(&szp->sz_lock);
	}
}

void
sharefs_zone_shnode_hold(sharefs_zone_t *szp)
{
	sharefs_zone_hold(szp);
	mutex_enter(&szp->sz_lock);
	szp->sz_shnode_count++;
	mutex_exit(&szp->sz_lock);
}

void
sharefs_zone_shnode_rele(sharefs_zone_t *szp)
{
	mutex_enter(&szp->sz_lock);
	szp->sz_shnode_count--;
	mutex_exit(&szp->sz_lock);
	sharefs_zone_rele(szp);
}
