/*
 * Copyright (c) 1993, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/modctl.h>
#include <sys/flock.h>

#include <nfs/lm.h>
#include <nfs/lm_impl.h>

static struct modlmisc modlmisc = {
	&mod_miscops, "lock mgr common module"
};

static struct modlinkage modlinkage = {
	MODREV_1, &modlmisc, NULL
};

/* drv/ip is for inet_ntop() */
char _depends_on[] = "strmod/rpcmod fs/nfs drv/ip";

int
_init()
{
	int retval;

	mutex_init(&lm_stat.lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&lm_global_list_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&lm_global_list, sizeof (lm_globals_t),
	    offsetof(lm_globals_t, lm_node));

	lm_sysid_init();

	/*
	 * Having intimate workings with the common flock code, we initialize
	 * certain zone-specific data -- managed by common flock code -- that
	 * will only be used to support us.
	 *
	 * We wait till now to initialize this data, as there's no point in
	 * common code maintaining zone-specific data on behalf of the lockmgr
	 * if klmmod isn't even loaded.
	 */
	zone_key_create(&flock_zone_key, flk_zone_init, NULL, flk_zone_fini);

	zone_key_create(&lm_zone_key, lm_zone_init, NULL, lm_zone_fini);

	retval = mod_install(&modlinkage);
	if (retval != 0) {
		/*
		 * Clean up previous initialization work.
		 */
		(void) zone_key_delete(flock_zone_key);
		flock_zone_key = ZONE_KEY_UNINITIALIZED;
		(void) zone_key_delete(lm_zone_key);
		mutex_destroy(&lm_stat.lock);
		mutex_destroy(&lm_global_list_lock);
		list_destroy(&lm_global_list);
		lm_sysid_fini();
	}

	return (retval);
}

int
_fini()
{
	return (EBUSY);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

#ifdef __lock_lint

/*
 * Stub function for warlock only - this is never compiled or called.
 */
void
klmmod_null()
{}

#include <nfs/nfs.h>
#include <nfs/nfs_clnt.h>
#include <nfs/lm_server.h>

/*
 * Function for warlock only - this is never compiled or called.
 *
 * It obtains locks which must be held while calling a few functions which
 * are normally only called from klmops.  This allows warlock to analyze
 * these functions.
 */
void
klmmod_lock_held_roots()
{
	lm_block_t lmb, *lmbp;
	struct flock64 flk64;
	struct lm_vnode lv;
	netobj netobj;

	(void) lm_add_block(NULL, &lmb);
	(void) lm_cancel_granted_rxmit(NULL, &flk64, &lv);
	(void) lm_dump_block(NULL, &lv);
	(void) lm_find_block(NULL, &flk64, &lv, &netobj, &lmbp);
	(void) lm_init_block(NULL, &lmb, &flk64, &lv, &netobj);
	(void) lm_remove_block(NULL, &lmb);
}
#endif /* __lock_lint */
