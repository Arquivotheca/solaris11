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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Zones
 *
 *   A zone is a named collection of processes, namespace constraints,
 *   and other system resources which comprise a secure and manageable
 *   application containment facility.
 *
 *   Zones (represented by the reference counted zone_t) are tracked in
 *   the kernel in the zonehash.  Elsewhere in the kernel, Zone IDs
 *   (zoneid_t) are used to track zone association.  Zone IDs are
 *   dynamically generated when the zone is created; if a persistent
 *   identifier is needed (core files, accounting logs, audit trail,
 *   etc.), the zone name should be used.
 *
 *
 *   Global Zone:
 *
 *   The global zone (zoneid 0) is automatically associated with all
 *   system resources that have not been bound to a user-created zone.
 *   This means that even systems where zones are not in active use
 *   have a global zone, and all processes, mounts, etc. are
 *   associated with that zone.  The global zone is generally
 *   unconstrained in terms of privileges and access, though the usual
 *   credential and privilege based restrictions apply.
 *
 *
 *   Zone States:
 *
 *   The states in which a zone may be in and the transitions are as
 *   follows:
 *
 *   ZONE_IS_UNINITIALIZED: primordial state for a zone. The partially
 *   initialized zone is added to the list of active zones on the system but
 *   isn't accessible.
 *
 *   ZONE_IS_INITIALIZED: Initialization complete except the ZSD callbacks are
 *   not yet completed. Not possible to enter the zone, but attributes can
 *   be retrieved.
 *
 *   ZONE_IS_READY: zsched (the kernel dummy process for a zone) is
 *   ready.  The zone is made visible after the ZSD constructor callbacks are
 *   executed.  A zone remains in this state until it transitions into
 *   the ZONE_IS_BOOTING state as a result of a call to zone_boot().
 *
 *   ZONE_IS_BOOTING: in this shortlived-state, zsched attempts to start
 *   init.  Should that fail, the zone proceeds to the ZONE_IS_SHUTTING_DOWN
 *   state.
 *
 *   ZONE_IS_RUNNING: The zone is open for business: zsched has
 *   successfully started init.   A zone remains in this state until
 *   zone_shutdown() is called.
 *
 *   ZONE_IS_SHUTTING_DOWN: zone_shutdown() has been called, the system is
 *   killing all processes running in the zone. The zone remains
 *   in this state until there are no more user processes running in the zone.
 *   zone_create(), zone_enter(), and zone_destroy() on this zone will fail.
 *   Since zone_shutdown() is restartable, it may be called successfully
 *   multiple times for the same zone_t.
 *
 *   ZONE_IS_EMPTY: zone_shutdown() has been called, and there
 *   are no more user processes in the zone.  The zone remains in this
 *   state until there are no more kernel threads associated with the
 *   zone.  zone_create(), zone_enter(), and zone_destroy() on this zone will
 *   fail.
 *
 *   ZONE_IS_DOWN: All kernel threads doing work on behalf of the zone
 *   have exited.  zone_shutdown() returns.  Henceforth it is not possible to
 *   join the zone or create kernel threads therein.
 *
 *   ZONE_IS_DYING: zone_destroy() has been called on the zone; zone
 *   remains in this state until zsched exits.  Calls to zone_find_by_*()
 *   return NULL from now on.
 *
 *   ZONE_IS_DEAD: zsched has exited (zone_ntasks == 0).  There are no
 *   processes or threads doing work on behalf of the zone.  ZSD
 *   destructor callbacks are executed. The zone is removed from the
 *   list of active zones and put on "deathrow". zone_destroy() returns,
 *   and the zone can be recreated.
 *
 *   ZONE_IS_FREE (internal state): zone_ref goes to 0, and all memory
 *   associated with the zone is freed.
 *
 *   Threads can wait for the zone to enter a requested state by using
 *   zone_status_wait() or zone_status_timedwait() with the desired
 *   state passed in as an argument.  Zone state transitions are
 *   uni-directional; it is not possible to move back to an earlier state.
 *
 *
 *   Zone-Specific Data:
 *
 *   Subsystems needing to maintain zone-specific data can store that
 *   data using the ZSD mechanism.  This provides a zone-specific data
 *   store, similar to thread-specific data (see pthread_getspecific(3C)
 *   or the TSD code in uts/common/disp/thread.c.  Also, ZSD can be used
 *   to register callbacks to be invoked when a zone is created, shut
 *   down, or destroyed.  This can be used to initialize zone-specific
 *   data for new zones and to clean up when zones go away.
 *
 *
 *   Data Structures:
 *
 *   The per-zone structure (zone_t) is reference counted, and freed
 *   when all references are released.  zone_hold and zone_rele can be
 *   used to adjust the reference count.  In addition, reference counts
 *   associated with the cred_t structure are tracked separately using
 *   zone_cred_hold and zone_cred_rele.
 *
 *   Pointers to active zone_t's are stored in two hash tables; one
 *   for searching by id, the other for searching by name.  Lookups
 *   can be performed on either basis, using zone_find_by_id and
 *   zone_find_by_name.  Both return zone_t pointers with the zone
 *   held, so zone_rele should be called when the pointer is no longer
 *   needed.  Zones can also be searched by path; zone_find_by_path
 *   returns the zone with which a path name is associated (global
 *   zone if the path is not within some other zone's file system
 *   hierarchy).  This currently requires iterating through each zone,
 *   so it is slower than an id or name search via a hash table.
 *
 *
 *   Locking:
 *
 *   zonehash_lock: This is a top-level global lock used to protect the
 *       zone hash tables and lists.  Zones cannot be created or destroyed
 *       while this lock is held.
 *   zone_lock: This is a per-zone lock used to protect several fields of
 *       the zone_t (see <sys/zone.h> for details). In particular, a zone
 *	 cannot change its state while a thread is holding this lock.
 *   zone_nlwps_lock: This is a per-zone lock used to protect the fields
 *	 related to the zone.max-lwps rctl.
 *   zone_mem_lock: This is a per-zone lock used to protect the fields
 *	 related to the zone.max-locked-memory and zone.max-swap rctls.
 *   zone_rctl_lock: This is a per-zone lock used to protect other rctls,
 *       currently just max_lofi
 *   zsd_key_lock: This is a global lock protecting the key state for ZSD.
 *   zone_deathrow_lock: This is a global lock protecting the "deathrow"
 *       list (a list of zones in the ZONE_IS_DEAD state with outstanding
 *	 references).
 *
 *   Ordering requirements:
 *       pool_lock --> cpu_lock --> zonehash_lock -->
 *       	zone_lock --> zsd_key_lock --> pidlock --> p_lock
 *
 *   Blocking memory allocations are permitted while holding any of the
 *   above zone locks.
 *
 *   When taking zone_mem_lock or zone_nlwps_lock, the lock ordering is:
 *	zonehash_lock --> a_lock --> pidlock --> p_lock --> zone_mem_lock
 *	zonehash_lock --> a_lock --> pidlock --> p_lock --> zone_nlwps_lock
 *
 *   System Call Interface:
 *
 *   The zone subsystem can be managed and queried from user level with
 *   the following system calls (all subcodes of the primary "zone"
 *   system call):
 *   - zone_create: creates a zone with selected attributes (name,
 *     root path, privileges, resource controls, ZFS datasets)
 *   - zone_enter: allows the current process to enter a zone
 *   - zone_getattr: reports attributes of a zone
 *   - zone_setattr: set attributes of a zone
 *   - zone_boot: set 'init' running for the zone
 *   - zone_list: lists all zones active in the system
 *   - zone_lookup: looks up zone id based on name
 *   - zone_shutdown: initiates shutdown process (see states above)
 *   - zone_destroy: completes shutdown process (see states above)
 *
 */

#include <sys/priv_impl.h>
#include <sys/cred.h>
#include <c2/audit.h>
#include <sys/debug.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/kstat.h>
#include <sys/mutex.h>
#include <sys/note.h>
#include <sys/pathname.h>
#include <sys/proc.h>
#include <sys/project.h>
#include <sys/sysevent.h>
#include <sys/task.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/systeminfo.h>
#include <sys/policy.h>
#include <sys/cred_impl.h>
#include <sys/contract_impl.h>
#include <sys/contract/process_impl.h>
#include <sys/class.h>
#include <sys/pool.h>
#include <sys/pool_pset.h>
#include <sys/pset.h>
#include <sys/strlog.h>
#include <sys/sysmacros.h>
#include <sys/callb.h>
#include <sys/vmparam.h>
#include <sys/corectl.h>
#include <sys/ipc_impl.h>
#include <sys/klpd.h>

#include <sys/door.h>
#include <sys/cpuvar.h>
#include <sys/sdt.h>

#include <sys/uadmin.h>
#include <sys/session.h>
#include <sys/cmn_err.h>
#include <sys/modhash.h>
#include <sys/sunddi.h>
#include <sys/nvpair.h>
#include <sys/rctl.h>
#include <sys/fss.h>
#include <sys/brand.h>
#include <sys/zone.h>
#include <net/if.h>
#include <sys/cpucaps.h>
#include <vm/seg.h>
#include <sys/mac.h>

#define	DEVANN_HASH_NAME_PREFIX "devann_hash_"
#define	DEVANN_HASH_NAME_MAX (sizeof (DEVANN_HASH_NAME_PREFIX) + ZONENAME_MAX)

/* List of data link IDs which are accessible from the zone */
typedef struct zone_dl {
	datalink_id_t	zdl_id;
	nvlist_t	*zdl_net;
	list_node_t	zdl_linkage;
} zone_dl_t;

/*
 * ZSD-related global variables.
 */
static kmutex_t zsd_key_lock;	/* protects the following two */
/*
 * The next caller of zone_key_create() will be assigned a key of ++zsd_keyval.
 */
static zone_key_t zsd_keyval = 0;
/*
 * Global list of registered keys.  We use this when a new zone is created.
 */
static list_t zsd_registered_keys;

int zone_hash_size = 256;
int zone_devann_hash_size = 128;
static mod_hash_t *zonehashbyname, *zonehashbyid, *zonehashbylabel;
static kmutex_t zonehash_lock;
static uint_t zonecount;
static id_space_t *zoneid_space;

/*
 * The global zone (aka zone0) is the all-seeing, all-knowing zone in which the
 * kernel proper runs, and which manages all other zones.
 *
 * Although not declared as static, the variable "zone0" should not be used
 * except for by code that needs to reference the global zone early on in boot,
 * before it is fully initialized.  All other consumers should use
 * 'global_zone'.
 */
zone_t zone0;
zone_t *global_zone = NULL;	/* Set when the global zone is initialized */

/*
 * List of active zones, protected by zonehash_lock.
 */
static list_t zone_active;

/*
 * List of destroyed zones that still have outstanding cred references.
 * Used for debugging.  Uses a separate lock to avoid lock ordering
 * problems in zone_free.
 */
static list_t zone_deathrow;
static kmutex_t zone_deathrow_lock;
static uint_t zone_deathrow_count;

/* number of zones is limited by virtual interface limit in IP */
uint_t maxzones = 8192;

/* Event channel to sent zone state change notifications */
evchan_t *zone_event_chan;

/*
 * This table holds the mapping from kernel zone states to
 * states visible in the state notification API.
 * The idea is that we only expose "obvious" states and
 * do not expose states which are just implementation details.
 */
const char  *zone_status_table[] = {
	ZONE_EVENT_UNINITIALIZED,	/* uninitialized */
	ZONE_EVENT_INITIALIZED,		/* initialized */
	ZONE_EVENT_READY,		/* ready */
	ZONE_EVENT_READY,		/* booting */
	ZONE_EVENT_RUNNING,		/* running */
	ZONE_EVENT_SHUTTING_DOWN,	/* shutting_down */
	ZONE_EVENT_SHUTTING_DOWN,	/* empty */
	ZONE_EVENT_SHUTTING_DOWN,	/* down */
	ZONE_EVENT_SHUTTING_DOWN,	/* dying */
	ZONE_EVENT_UNINITIALIZED	/* dead */
};

/*
 * This array contains the names of the subsystems listed in zone_ref_subsys_t
 * (see sys/zone.h).
 */
static char *zone_ref_subsys_names[] = {
	"NFS",		/* ZONE_REF_NFS */
	"NFSv4",	/* ZONE_REF_NFSV4 */
	"SMBFS",	/* ZONE_REF_SMBFS */
	"LOFI",		/* ZONE_REF_LOFI */
	"VFS",		/* ZONE_REF_VFS */
	"IPC",		/* ZONE_REF_IPC */
	"NFSSRV",	/* ZONE_REF_NFSSRV */
	"SHAREFS"	/* ZONE_REF_SHAREFS */
};

/*
 * This isn't static so lint doesn't complain.
 */
rctl_hndl_t rc_zone_cpu_shares;
rctl_hndl_t rc_zone_locked_mem;
rctl_hndl_t rc_zone_max_swap;
rctl_hndl_t rc_zone_max_lofi;
rctl_hndl_t rc_zone_cpu_cap;
rctl_hndl_t rc_zone_nlwps;
rctl_hndl_t rc_zone_nprocs;
rctl_hndl_t rc_zone_shmmax;
rctl_hndl_t rc_zone_shmmni;
rctl_hndl_t rc_zone_semmni;
rctl_hndl_t rc_zone_msgmni;

const char * const zone_default_initname = "/usr/sbin/init";
static char * const zone_prefix = "/zone/";
static int zone_shutdown(zoneid_t zoneid);
static int zone_add_datalink(zoneid_t, datalink_id_t);
static int zone_remove_datalink(zoneid_t, datalink_id_t);
static int zone_list_datalink(zoneid_t, int *, datalink_id_t *);
static int zone_set_network(zoneid_t, zone_net_data_t *);
static int zone_get_network(zoneid_t, zone_net_data_t *);

typedef boolean_t zsd_applyfn_t(kmutex_t *, boolean_t, zone_t *, zone_key_t);

static void zsd_apply_all_zones(zsd_applyfn_t *, zone_key_t);
static void zsd_apply_all_keys(zsd_applyfn_t *, zone_t *);
static boolean_t zsd_apply_create(kmutex_t *, boolean_t, zone_t *, zone_key_t);
static boolean_t zsd_apply_shutdown(kmutex_t *, boolean_t, zone_t *,
    zone_key_t);
static boolean_t zsd_apply_destroy(kmutex_t *, boolean_t, zone_t *, zone_key_t);
static boolean_t zsd_wait_for_creator(zone_t *, struct zsd_entry *,
    kmutex_t *);
static boolean_t zsd_wait_for_inprogress(zone_t *, struct zsd_entry *,
    kmutex_t *);

/*
 * ZSD routines.
 *
 * Zone Specific Data (ZSD) is modeled after Thread Specific Data as
 * defined by the pthread_key_create() and related interfaces.
 *
 * Kernel subsystems may register one or more data items and/or
 * callbacks to be executed when a zone is created, shutdown, or
 * destroyed.
 *
 * Unlike the thread counterpart, destructor callbacks will be executed
 * even if the data pointer is NULL and/or there are no constructor
 * callbacks, so it is the responsibility of such callbacks to check for
 * NULL data values if necessary.
 *
 * The locking strategy and overall picture is as follows:
 *
 * When someone calls zone_key_create(), a template ZSD entry is added to the
 * global list "zsd_registered_keys", protected by zsd_key_lock.  While
 * holding that lock all the existing zones are marked as
 * ZSD_CREATE_NEEDED and a copy of the ZSD entry added to the per-zone
 * zone_zsd list (protected by zone_lock). The global list is updated first
 * (under zone_key_lock) to make sure that newly created zones use the
 * most recent list of keys. Then under zonehash_lock we walk the zones
 * and mark them.  Similar locking is used in zone_key_delete().
 *
 * The actual create, shutdown, and destroy callbacks are done without
 * holding any lock. And zsd_flags are used to ensure that the operations
 * completed so that when zone_key_create (and zone_create) is done, as well as
 * zone_key_delete (and zone_destroy) is done, all the necessary callbacks
 * are completed.
 *
 * When new zones are created constructor callbacks for all registered ZSD
 * entries will be called. That also uses the above two phases of marking
 * what needs to be done, and then running the callbacks without holding
 * any locks.
 *
 * The framework does not provide any locking around zone_getspecific() and
 * zone_setspecific() apart from that needed for internal consistency, so
 * callers interested in atomic "test-and-set" semantics will need to provide
 * their own locking.
 */

/*
 * Helper function to find the zsd_entry associated with the key in the
 * given list.
 */
static struct zsd_entry *
zsd_find(list_t *l, zone_key_t key)
{
	struct zsd_entry *zsd;

	for (zsd = list_head(l); zsd != NULL; zsd = list_next(l, zsd)) {
		if (zsd->zsd_key == key) {
			return (zsd);
		}
	}
	return (NULL);
}

/*
 * Helper function to find the zsd_entry associated with the key in the
 * given list. Move it to the front of the list.
 */
static struct zsd_entry *
zsd_find_mru(list_t *l, zone_key_t key)
{
	struct zsd_entry *zsd;

	for (zsd = list_head(l); zsd != NULL; zsd = list_next(l, zsd)) {
		if (zsd->zsd_key == key) {
			/*
			 * Move to head of list to keep list in MRU order.
			 */
			if (zsd != list_head(l)) {
				list_remove(l, zsd);
				list_insert_head(l, zsd);
			}
			return (zsd);
		}
	}
	return (NULL);
}

void
zone_key_create(zone_key_t *keyp, void *(*create)(zoneid_t),
    void (*shutdown)(zoneid_t, void *), void (*destroy)(zoneid_t, void *))
{
	struct zsd_entry *zsdp;
	struct zsd_entry *t;
	struct zone *zone;
	zone_key_t  key;

	zsdp = kmem_zalloc(sizeof (*zsdp), KM_SLEEP);
	zsdp->zsd_data = NULL;
	zsdp->zsd_create = create;
	zsdp->zsd_shutdown = shutdown;
	zsdp->zsd_destroy = destroy;

	/*
	 * Insert in global list of callbacks. Makes future zone creations
	 * see it.
	 */
	mutex_enter(&zsd_key_lock);
	key = zsdp->zsd_key = ++zsd_keyval;
	ASSERT(zsd_keyval != 0);
	list_insert_tail(&zsd_registered_keys, zsdp);
	mutex_exit(&zsd_key_lock);

	/*
	 * Insert for all existing zones and mark them as needing
	 * a create callback.
	 */
	mutex_enter(&zonehash_lock);	/* stop the world */
	for (zone = list_head(&zone_active); zone != NULL;
	    zone = list_next(&zone_active, zone)) {
		zone_status_t status;

		mutex_enter(&zone->zone_lock);

		/* Skip zones that are on the way down or not yet up */
		status = zone_status_get(zone);
		if (status >= ZONE_IS_DOWN ||
		    status == ZONE_IS_UNINITIALIZED) {
			mutex_exit(&zone->zone_lock);
			continue;
		}

		t = zsd_find_mru(&zone->zone_zsd, key);
		if (t != NULL) {
			/*
			 * A zsd_configure already inserted it after
			 * we dropped zsd_key_lock above.
			 */
			mutex_exit(&zone->zone_lock);
			continue;
		}
		t = kmem_zalloc(sizeof (*t), KM_SLEEP);
		t->zsd_key = key;
		t->zsd_create = create;
		t->zsd_shutdown = shutdown;
		t->zsd_destroy = destroy;
		if (create != NULL) {
			t->zsd_flags = ZSD_CREATE_NEEDED;
			DTRACE_PROBE2(zsd__create__needed,
			    zone_t *, zone, zone_key_t, key);
		}
		list_insert_tail(&zone->zone_zsd, t);
		mutex_exit(&zone->zone_lock);
	}
	mutex_exit(&zonehash_lock);

	if (create != NULL) {
		/* Now call the create callback for this key */
		zsd_apply_all_zones(zsd_apply_create, key);
	}
	/*
	 * It is safe for consumers to use the key now, make it
	 * globally visible. Specifically zone_getspecific() will
	 * always successfully return the zone specific data associated
	 * with the key.
	 */
	*keyp = key;

}

/*
 * Function called when a module is being unloaded, or otherwise wishes
 * to unregister its ZSD key and callbacks.
 *
 * Remove from the global list and determine the functions that need to
 * be called under a global lock. Then call the functions without
 * holding any locks. Finally free up the zone_zsd entries. (The apply
 * functions need to access the zone_zsd entries to find zsd_data etc.)
 */
int
zone_key_delete(zone_key_t key)
{
	struct zsd_entry *zsdp = NULL;
	zone_t *zone;

	mutex_enter(&zsd_key_lock);
	zsdp = zsd_find_mru(&zsd_registered_keys, key);
	if (zsdp == NULL) {
		mutex_exit(&zsd_key_lock);
		return (-1);
	}
	list_remove(&zsd_registered_keys, zsdp);
	mutex_exit(&zsd_key_lock);

	mutex_enter(&zonehash_lock);
	for (zone = list_head(&zone_active); zone != NULL;
	    zone = list_next(&zone_active, zone)) {
		struct zsd_entry *del;

		mutex_enter(&zone->zone_lock);
		del = zsd_find_mru(&zone->zone_zsd, key);
		if (del == NULL) {
			/*
			 * Somebody else got here first e.g the zone going
			 * away.
			 */
			mutex_exit(&zone->zone_lock);
			continue;
		}
		ASSERT(del->zsd_shutdown == zsdp->zsd_shutdown);
		ASSERT(del->zsd_destroy == zsdp->zsd_destroy);
		if (del->zsd_shutdown != NULL &&
		    (del->zsd_flags & ZSD_SHUTDOWN_ALL) == 0) {
			del->zsd_flags |= ZSD_SHUTDOWN_NEEDED;
			DTRACE_PROBE2(zsd__shutdown__needed,
			    zone_t *, zone, zone_key_t, key);
		}
		if (del->zsd_destroy != NULL &&
		    (del->zsd_flags & ZSD_DESTROY_ALL) == 0) {
			del->zsd_flags |= ZSD_DESTROY_NEEDED;
			DTRACE_PROBE2(zsd__destroy__needed,
			    zone_t *, zone, zone_key_t, key);
		}
		mutex_exit(&zone->zone_lock);
	}
	mutex_exit(&zonehash_lock);
	kmem_free(zsdp, sizeof (*zsdp));

	/* Now call the shutdown and destroy callback for this key */
	zsd_apply_all_zones(zsd_apply_shutdown, key);
	zsd_apply_all_zones(zsd_apply_destroy, key);

	/* Now we can free up the zsdp structures in each zone */
	mutex_enter(&zonehash_lock);
	for (zone = list_head(&zone_active); zone != NULL;
	    zone = list_next(&zone_active, zone)) {
		struct zsd_entry *del;

		mutex_enter(&zone->zone_lock);
		del = zsd_find(&zone->zone_zsd, key);
		if (del != NULL) {
			list_remove(&zone->zone_zsd, del);
			ASSERT(!(del->zsd_flags & ZSD_ALL_INPROGRESS));
			kmem_free(del, sizeof (*del));
		}
		mutex_exit(&zone->zone_lock);
	}
	mutex_exit(&zonehash_lock);

	return (0);
}

/*
 * ZSD counterpart of pthread_setspecific().
 *
 * Since all zsd callbacks, including those with no create function,
 * have an entry in zone_zsd, if the key is registered it is part of
 * the zone_zsd list.
 * Return an error if the key wasn't registerd.
 */
int
zone_setspecific(zone_key_t key, zone_t *zone, const void *data)
{
	struct zsd_entry *t;

	mutex_enter(&zone->zone_lock);
	t = zsd_find_mru(&zone->zone_zsd, key);
	if (t != NULL) {
		/*
		 * Replace old value with new
		 */
		t->zsd_data = (void *)data;
		mutex_exit(&zone->zone_lock);
		return (0);
	}
	mutex_exit(&zone->zone_lock);
	return (-1);
}

/*
 * ZSD counterpart of pthread_getspecific().
 */
void *
zone_getspecific(zone_key_t key, zone_t *zone)
{
	struct zsd_entry *t;
	void *data;

	mutex_enter(&zone->zone_lock);
	t = zsd_find_mru(&zone->zone_zsd, key);
	data = (t == NULL ? NULL : t->zsd_data);
	mutex_exit(&zone->zone_lock);
	return (data);
}

/*
 * Function used to initialize a zone's list of ZSD callbacks and data
 * when the zone is being created.  The callbacks are initialized from
 * the template list (zsd_registered_keys). The constructor callback is
 * executed later (once the zone exists and with locks dropped).
 */
static void
zone_zsd_configure(zone_t *zone)
{
	struct zsd_entry *zsdp;
	struct zsd_entry *t;

	ASSERT(MUTEX_HELD(&zonehash_lock));
	ASSERT(list_head(&zone->zone_zsd) == NULL);
	mutex_enter(&zone->zone_lock);
	mutex_enter(&zsd_key_lock);
	for (zsdp = list_head(&zsd_registered_keys); zsdp != NULL;
	    zsdp = list_next(&zsd_registered_keys, zsdp)) {
		/*
		 * Since this zone is ZONE_IS_UNCONFIGURED, zone_key_create
		 * should not have added anything to it.
		 */
		ASSERT(zsd_find(&zone->zone_zsd, zsdp->zsd_key) == NULL);

		t = kmem_zalloc(sizeof (*t), KM_SLEEP);
		t->zsd_key = zsdp->zsd_key;
		t->zsd_create = zsdp->zsd_create;
		t->zsd_shutdown = zsdp->zsd_shutdown;
		t->zsd_destroy = zsdp->zsd_destroy;
		if (zsdp->zsd_create != NULL) {
			t->zsd_flags = ZSD_CREATE_NEEDED;
			DTRACE_PROBE2(zsd__create__needed,
			    zone_t *, zone, zone_key_t, zsdp->zsd_key);
		}
		list_insert_tail(&zone->zone_zsd, t);
	}
	mutex_exit(&zsd_key_lock);
	mutex_exit(&zone->zone_lock);
}

enum zsd_callback_type { ZSD_CREATE, ZSD_SHUTDOWN, ZSD_DESTROY };

/*
 * Helper function to execute shutdown or destructor callbacks.
 */
static void
zone_zsd_callbacks(zone_t *zone, enum zsd_callback_type ct)
{
	struct zsd_entry *t;

	ASSERT(ct == ZSD_SHUTDOWN || ct == ZSD_DESTROY);
	ASSERT(ct != ZSD_SHUTDOWN || zone_status_get(zone) >= ZONE_IS_EMPTY);
	ASSERT(ct != ZSD_DESTROY || zone_status_get(zone) >= ZONE_IS_DOWN);

	/*
	 * Run the callback solely based on what is registered for the zone
	 * in zone_zsd. The global list can change independently of this
	 * as keys are registered and unregistered and we don't register new
	 * callbacks for a zone that is in the process of going away.
	 */
	mutex_enter(&zone->zone_lock);
	for (t = list_head(&zone->zone_zsd); t != NULL;
	    t = list_next(&zone->zone_zsd, t)) {
		zone_key_t key = t->zsd_key;

		/* Skip if no callbacks registered */

		if (ct == ZSD_SHUTDOWN) {
			if (t->zsd_shutdown != NULL &&
			    (t->zsd_flags & ZSD_SHUTDOWN_ALL) == 0) {
				t->zsd_flags |= ZSD_SHUTDOWN_NEEDED;
				DTRACE_PROBE2(zsd__shutdown__needed,
				    zone_t *, zone, zone_key_t, key);
			}
		} else {
			if (t->zsd_destroy != NULL &&
			    (t->zsd_flags & ZSD_DESTROY_ALL) == 0) {
				t->zsd_flags |= ZSD_DESTROY_NEEDED;
				DTRACE_PROBE2(zsd__destroy__needed,
				    zone_t *, zone, zone_key_t, key);
			}
		}
	}
	mutex_exit(&zone->zone_lock);

	/* Now call the shutdown and destroy callback for this key */
	zsd_apply_all_keys(zsd_apply_shutdown, zone);
	zsd_apply_all_keys(zsd_apply_destroy, zone);
}

/*
 * Clear zone shutdown callback flags to allow calling the zsd shutdown
 * callbacks again.
 */
static void
zone_zsd_clear_shutdown_callbacks(zone_t *zone)
{
	struct zsd_entry *t;

	ASSERT(zone_status_get(zone) >= ZONE_IS_EMPTY);
	ASSERT(MUTEX_HELD(&zone->zone_lock));

	for (t = list_head(&zone->zone_zsd); t != NULL;
	    t = list_next(&zone->zone_zsd, t)) {

		/* Skip if no callbacks registered */
		if (t->zsd_shutdown != NULL &&
		    (t->zsd_flags & ZSD_SHUTDOWN_ALL) != 0) {
			t->zsd_flags &= ~(ZSD_SHUTDOWN_ALL);
		}
	}
}

/*
 * Called when the zone is going away; free ZSD-related memory, and
 * destroy the zone_zsd list.
 */
static void
zone_free_zsd(zone_t *zone)
{
	struct zsd_entry *t, *next;

	/*
	 * Free all the zsd_entry's we had on this zone.
	 */
	mutex_enter(&zone->zone_lock);
	for (t = list_head(&zone->zone_zsd); t != NULL; t = next) {
		next = list_next(&zone->zone_zsd, t);
		list_remove(&zone->zone_zsd, t);
		ASSERT(!(t->zsd_flags & ZSD_ALL_INPROGRESS));
		kmem_free(t, sizeof (*t));
	}
	list_destroy(&zone->zone_zsd);
	mutex_exit(&zone->zone_lock);

}

/*
 * Apply a function to all zones for particular key value.
 *
 * The applyfn has to drop zonehash_lock if it does some work, and
 * then reacquire it before it returns.
 * When the lock is dropped we don't follow list_next even
 * if it is possible to do so without any hazards. This is
 * because we want the design to allow for the list of zones
 * to change in any arbitrary way during the time the
 * lock was dropped.
 *
 * It is safe to restart the loop at list_head since the applyfn
 * changes the zsd_flags as it does work, so a subsequent
 * pass through will have no effect in applyfn, hence the loop will terminate
 * in at worst O(N^2).
 */
static void
zsd_apply_all_zones(zsd_applyfn_t *applyfn, zone_key_t key)
{
	zone_t *zone;

	mutex_enter(&zonehash_lock);
	zone = list_head(&zone_active);
	while (zone != NULL) {
		if ((applyfn)(&zonehash_lock, B_FALSE, zone, key)) {
			/* Lock dropped - restart at head */
			zone = list_head(&zone_active);
		} else {
			zone = list_next(&zone_active, zone);
		}
	}
	mutex_exit(&zonehash_lock);
}

/*
 * Apply a function to all keys for a particular zone.
 *
 * The applyfn has to drop zonehash_lock if it does some work, and
 * then reacquire it before it returns.
 * When the lock is dropped we don't follow list_next even
 * if it is possible to do so without any hazards. This is
 * because we want the design to allow for the list of zsd callbacks
 * to change in any arbitrary way during the time the
 * lock was dropped.
 *
 * It is safe to restart the loop at list_head since the applyfn
 * changes the zsd_flags as it does work, so a subsequent
 * pass through will have no effect in applyfn, hence the loop will terminate
 * in at worst O(N^2).
 */
static void
zsd_apply_all_keys(zsd_applyfn_t *applyfn, zone_t *zone)
{
	struct zsd_entry *t;

	mutex_enter(&zone->zone_lock);
	t = list_head(&zone->zone_zsd);
	while (t != NULL) {
		if ((applyfn)(NULL, B_TRUE, zone, t->zsd_key)) {
			/* Lock dropped - restart at head */
			t = list_head(&zone->zone_zsd);
		} else {
			t = list_next(&zone->zone_zsd, t);
		}
	}
	mutex_exit(&zone->zone_lock);
}

/*
 * Call the create function for the zone and key if CREATE_NEEDED
 * is set.
 * If some other thread gets here first and sets CREATE_INPROGRESS, then
 * we wait for that thread to complete so that we can ensure that
 * all the callbacks are done when we've looped over all zones/keys.
 *
 * When we call the create function, we drop the global held by the
 * caller, and return true to tell the caller it needs to re-evalute the
 * state.
 * If the caller holds zone_lock then zone_lock_held is set, and zone_lock
 * remains held on exit.
 */
static boolean_t
zsd_apply_create(kmutex_t *lockp, boolean_t zone_lock_held,
    zone_t *zone, zone_key_t key)
{
	void *result;
	struct zsd_entry *t;
	boolean_t dropped;

	if (lockp != NULL) {
		ASSERT(MUTEX_HELD(lockp));
	}
	if (zone_lock_held) {
		ASSERT(MUTEX_HELD(&zone->zone_lock));
	} else {
		mutex_enter(&zone->zone_lock);
	}

	t = zsd_find(&zone->zone_zsd, key);
	if (t == NULL) {
		/*
		 * Somebody else got here first e.g the zone going
		 * away.
		 */
		if (!zone_lock_held)
			mutex_exit(&zone->zone_lock);
		return (B_FALSE);
	}
	dropped = B_FALSE;
	if (zsd_wait_for_inprogress(zone, t, lockp))
		dropped = B_TRUE;

	if (t->zsd_flags & ZSD_CREATE_NEEDED) {
		t->zsd_flags &= ~ZSD_CREATE_NEEDED;
		t->zsd_flags |= ZSD_CREATE_INPROGRESS;
		DTRACE_PROBE2(zsd__create__inprogress,
		    zone_t *, zone, zone_key_t, key);
		mutex_exit(&zone->zone_lock);
		if (lockp != NULL)
			mutex_exit(lockp);

		dropped = B_TRUE;
		ASSERT(t->zsd_create != NULL);
		DTRACE_PROBE2(zsd__create__start,
		    zone_t *, zone, zone_key_t, key);

		result = (*t->zsd_create)(zone->zone_id);

		DTRACE_PROBE2(zsd__create__end,
		    zone_t *, zone, voidn *, result);

		if (lockp != NULL)
			mutex_enter(lockp);
		mutex_enter(&zone->zone_lock);
		t->zsd_data = result;
		t->zsd_flags &= ~ZSD_CREATE_INPROGRESS;
		t->zsd_flags |= ZSD_CREATE_COMPLETED;
		cv_broadcast(&t->zsd_cv);
		DTRACE_PROBE2(zsd__create__completed,
		    zone_t *, zone, zone_key_t, key);
	}
	if (!zone_lock_held)
		mutex_exit(&zone->zone_lock);
	return (dropped);
}

/*
 * Call the shutdown function for the zone and key if SHUTDOWN_NEEDED
 * is set.
 * If some other thread gets here first and sets *_INPROGRESS, then
 * we wait for that thread to complete so that we can ensure that
 * all the callbacks are done when we've looped over all zones/keys.
 *
 * When we call the shutdown function, we drop the global held by the
 * caller, and return true to tell the caller it needs to re-evalute the
 * state.
 * If the caller holds zone_lock then zone_lock_held is set, and zone_lock
 * remains held on exit.
 */
static boolean_t
zsd_apply_shutdown(kmutex_t *lockp, boolean_t zone_lock_held,
    zone_t *zone, zone_key_t key)
{
	struct zsd_entry *t;
	void *data;
	boolean_t dropped;

	if (lockp != NULL) {
		ASSERT(MUTEX_HELD(lockp));
	}
	if (zone_lock_held) {
		ASSERT(MUTEX_HELD(&zone->zone_lock));
	} else {
		mutex_enter(&zone->zone_lock);
	}

	t = zsd_find(&zone->zone_zsd, key);
	if (t == NULL) {
		/*
		 * Somebody else got here first e.g the zone going
		 * away.
		 */
		if (!zone_lock_held)
			mutex_exit(&zone->zone_lock);
		return (B_FALSE);
	}
	dropped = B_FALSE;
	if (zsd_wait_for_creator(zone, t, lockp))
		dropped = B_TRUE;

	if (zsd_wait_for_inprogress(zone, t, lockp))
		dropped = B_TRUE;

	if (t->zsd_flags & ZSD_SHUTDOWN_NEEDED) {
		t->zsd_flags &= ~ZSD_SHUTDOWN_NEEDED;
		t->zsd_flags |= ZSD_SHUTDOWN_INPROGRESS;
		DTRACE_PROBE2(zsd__shutdown__inprogress,
		    zone_t *, zone, zone_key_t, key);
		mutex_exit(&zone->zone_lock);
		if (lockp != NULL)
			mutex_exit(lockp);
		dropped = B_TRUE;

		ASSERT(t->zsd_shutdown != NULL);
		data = t->zsd_data;

		DTRACE_PROBE2(zsd__shutdown__start,
		    zone_t *, zone, zone_key_t, key);

		(t->zsd_shutdown)(zone->zone_id, data);
		DTRACE_PROBE2(zsd__shutdown__end,
		    zone_t *, zone, zone_key_t, key);

		if (lockp != NULL)
			mutex_enter(lockp);
		mutex_enter(&zone->zone_lock);
		t->zsd_flags &= ~ZSD_SHUTDOWN_INPROGRESS;
		t->zsd_flags |= ZSD_SHUTDOWN_COMPLETED;
		cv_broadcast(&t->zsd_cv);
		DTRACE_PROBE2(zsd__shutdown__completed,
		    zone_t *, zone, zone_key_t, key);
	}
	if (!zone_lock_held)
		mutex_exit(&zone->zone_lock);
	return (dropped);
}

/*
 * Call the destroy function for the zone and key if DESTROY_NEEDED
 * is set.
 * If some other thread gets here first and sets *_INPROGRESS, then
 * we wait for that thread to complete so that we can ensure that
 * all the callbacks are done when we've looped over all zones/keys.
 *
 * When we call the destroy function, we drop the global held by the
 * caller, and return true to tell the caller it needs to re-evalute the
 * state.
 * If the caller holds zone_lock then zone_lock_held is set, and zone_lock
 * remains held on exit.
 */
static boolean_t
zsd_apply_destroy(kmutex_t *lockp, boolean_t zone_lock_held,
    zone_t *zone, zone_key_t key)
{
	struct zsd_entry *t;
	void *data;
	boolean_t dropped;

	if (lockp != NULL) {
		ASSERT(MUTEX_HELD(lockp));
	}
	if (zone_lock_held) {
		ASSERT(MUTEX_HELD(&zone->zone_lock));
	} else {
		mutex_enter(&zone->zone_lock);
	}

	t = zsd_find(&zone->zone_zsd, key);
	if (t == NULL) {
		/*
		 * Somebody else got here first e.g the zone going
		 * away.
		 */
		if (!zone_lock_held)
			mutex_exit(&zone->zone_lock);
		return (B_FALSE);
	}
	dropped = B_FALSE;
	if (zsd_wait_for_creator(zone, t, lockp))
		dropped = B_TRUE;

	if (zsd_wait_for_inprogress(zone, t, lockp))
		dropped = B_TRUE;

	if (t->zsd_flags & ZSD_DESTROY_NEEDED) {
		t->zsd_flags &= ~ZSD_DESTROY_NEEDED;
		t->zsd_flags |= ZSD_DESTROY_INPROGRESS;
		DTRACE_PROBE2(zsd__destroy__inprogress,
		    zone_t *, zone, zone_key_t, key);
		mutex_exit(&zone->zone_lock);
		if (lockp != NULL)
			mutex_exit(lockp);
		dropped = B_TRUE;

		ASSERT(t->zsd_destroy != NULL);
		data = t->zsd_data;
		DTRACE_PROBE2(zsd__destroy__start,
		    zone_t *, zone, zone_key_t, key);

		(t->zsd_destroy)(zone->zone_id, data);
		DTRACE_PROBE2(zsd__destroy__end,
		    zone_t *, zone, zone_key_t, key);

		if (lockp != NULL)
			mutex_enter(lockp);
		mutex_enter(&zone->zone_lock);
		t->zsd_data = NULL;
		t->zsd_flags &= ~ZSD_DESTROY_INPROGRESS;
		t->zsd_flags |= ZSD_DESTROY_COMPLETED;
		cv_broadcast(&t->zsd_cv);
		DTRACE_PROBE2(zsd__destroy__completed,
		    zone_t *, zone, zone_key_t, key);
	}
	if (!zone_lock_held)
		mutex_exit(&zone->zone_lock);
	return (dropped);
}

/*
 * Wait for any CREATE_NEEDED flag to be cleared.
 * Returns true if lockp was temporarily dropped while waiting.
 */
static boolean_t
zsd_wait_for_creator(zone_t *zone, struct zsd_entry *t, kmutex_t *lockp)
{
	boolean_t dropped = B_FALSE;

	while (t->zsd_flags & ZSD_CREATE_NEEDED) {
		DTRACE_PROBE2(zsd__wait__for__creator,
		    zone_t *, zone, struct zsd_entry *, t);
		if (lockp != NULL) {
			dropped = B_TRUE;
			mutex_exit(lockp);
		}
		cv_wait(&t->zsd_cv, &zone->zone_lock);
		if (lockp != NULL) {
			/* First drop zone_lock to preserve order */
			mutex_exit(&zone->zone_lock);
			mutex_enter(lockp);
			mutex_enter(&zone->zone_lock);
		}
	}
	return (dropped);
}

/*
 * Wait for any INPROGRESS flag to be cleared.
 * Returns true if lockp was temporarily dropped while waiting.
 */
static boolean_t
zsd_wait_for_inprogress(zone_t *zone, struct zsd_entry *t, kmutex_t *lockp)
{
	boolean_t dropped = B_FALSE;

	while (t->zsd_flags & ZSD_ALL_INPROGRESS) {
		DTRACE_PROBE2(zsd__wait__for__inprogress,
		    zone_t *, zone, struct zsd_entry *, t);
		if (lockp != NULL) {
			dropped = B_TRUE;
			mutex_exit(lockp);
		}
		cv_wait(&t->zsd_cv, &zone->zone_lock);
		if (lockp != NULL) {
			/* First drop zone_lock to preserve order */
			mutex_exit(&zone->zone_lock);
			mutex_enter(lockp);
			mutex_enter(&zone->zone_lock);
		}
	}
	return (dropped);
}

/*
 * Frees memory associated with the zone dataset list.
 */
static void
zone_free_datasets(zone_t *zone)
{
	zone_dataset_t *t, *next;

	for (t = list_head(&zone->zone_datasets); t != NULL; t = next) {
		next = list_next(&zone->zone_datasets, t);
		list_remove(&zone->zone_datasets, t);
		kmem_free(t->zd_dataset, strlen(t->zd_dataset) + 1);
		kmem_free(t->zd_alias, strlen(t->zd_alias) + 1);
		kmem_free(t, sizeof (*t));
	}
	list_destroy(&zone->zone_datasets);
}

/*
 * zone.cpu-shares resource control support.
 */
/*ARGSUSED*/
static rctl_qty_t
zone_cpu_shares_usage(rctl_t *rctl, struct proc *p)
{
	ASSERT(MUTEX_HELD(&p->p_lock));
	return (p->p_zone->zone_shares);
}

/*ARGSUSED*/
static int
zone_cpu_shares_set(rctl_t *rctl, struct proc *p, rctl_entity_p_t *e,
    rctl_qty_t nv)
{
	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(e->rcep_t == RCENTITY_ZONE);
	if (e->rcep_p.zone == NULL)
		return (0);

	e->rcep_p.zone->zone_shares = nv;
	return (0);
}

static rctl_ops_t zone_cpu_shares_ops = {
	rcop_no_action,
	zone_cpu_shares_usage,
	zone_cpu_shares_set,
	rcop_no_test
};

/*
 * zone.cpu-cap resource control support.
 */
/*ARGSUSED*/
static rctl_qty_t
zone_cpu_cap_get(rctl_t *rctl, struct proc *p)
{
	ASSERT(MUTEX_HELD(&p->p_lock));
	return (cpucaps_zone_get(p->p_zone));
}

/*ARGSUSED*/
static int
zone_cpu_cap_set(rctl_t *rctl, struct proc *p, rctl_entity_p_t *e,
    rctl_qty_t nv)
{
	zone_t *zone = e->rcep_p.zone;

	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(e->rcep_t == RCENTITY_ZONE);

	if (zone == NULL)
		return (0);

	/*
	 * set cap to the new value.
	 */
	return (cpucaps_zone_set(zone, nv));
}

static rctl_ops_t zone_cpu_cap_ops = {
	rcop_no_action,
	zone_cpu_cap_get,
	zone_cpu_cap_set,
	rcop_no_test
};

/*ARGSUSED*/
static rctl_qty_t
zone_lwps_usage(rctl_t *r, proc_t *p)
{
	rctl_qty_t nlwps;
	zone_t *zone = p->p_zone;

	ASSERT(MUTEX_HELD(&p->p_lock));

	mutex_enter(&zone->zone_nlwps_lock);
	nlwps = zone->zone_nlwps;
	mutex_exit(&zone->zone_nlwps_lock);

	return (nlwps);
}

/*ARGSUSED*/
static int
zone_lwps_test(rctl_t *r, proc_t *p, rctl_entity_p_t *e, rctl_val_t *rcntl,
    rctl_qty_t incr, uint_t flags)
{
	rctl_qty_t nlwps;

	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(e->rcep_t == RCENTITY_ZONE);
	if (e->rcep_p.zone == NULL)
		return (0);
	ASSERT(MUTEX_HELD(&(e->rcep_p.zone->zone_nlwps_lock)));
	nlwps = e->rcep_p.zone->zone_nlwps;

	if (nlwps + incr > rcntl->rcv_value)
		return (1);

	return (0);
}

/*ARGSUSED*/
static int
zone_lwps_set(rctl_t *rctl, struct proc *p, rctl_entity_p_t *e, rctl_qty_t nv)
{
	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(e->rcep_t == RCENTITY_ZONE);
	if (e->rcep_p.zone == NULL)
		return (0);
	e->rcep_p.zone->zone_nlwps_ctl = nv;
	return (0);
}

static rctl_ops_t zone_lwps_ops = {
	rcop_no_action,
	zone_lwps_usage,
	zone_lwps_set,
	zone_lwps_test,
};

/*ARGSUSED*/
static rctl_qty_t
zone_procs_usage(rctl_t *r, proc_t *p)
{
	rctl_qty_t nprocs;
	zone_t *zone = p->p_zone;

	ASSERT(MUTEX_HELD(&p->p_lock));

	mutex_enter(&zone->zone_nlwps_lock);
	nprocs = zone->zone_nprocs;
	mutex_exit(&zone->zone_nlwps_lock);

	return (nprocs);
}

/*ARGSUSED*/
static int
zone_procs_test(rctl_t *r, proc_t *p, rctl_entity_p_t *e, rctl_val_t *rcntl,
    rctl_qty_t incr, uint_t flags)
{
	rctl_qty_t nprocs;

	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(e->rcep_t == RCENTITY_ZONE);
	if (e->rcep_p.zone == NULL)
		return (0);
	ASSERT(MUTEX_HELD(&(e->rcep_p.zone->zone_nlwps_lock)));
	nprocs = e->rcep_p.zone->zone_nprocs;

	if (nprocs + incr > rcntl->rcv_value)
		return (1);

	return (0);
}

/*ARGSUSED*/
static int
zone_procs_set(rctl_t *rctl, struct proc *p, rctl_entity_p_t *e, rctl_qty_t nv)
{
	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(e->rcep_t == RCENTITY_ZONE);
	if (e->rcep_p.zone == NULL)
		return (0);
	e->rcep_p.zone->zone_nprocs_ctl = nv;
	return (0);
}

static rctl_ops_t zone_procs_ops = {
	rcop_no_action,
	zone_procs_usage,
	zone_procs_set,
	zone_procs_test,
};

/*ARGSUSED*/
static int
zone_shmmax_test(rctl_t *r, proc_t *p, rctl_entity_p_t *e, rctl_val_t *rval,
    rctl_qty_t incr, uint_t flags)
{
	rctl_qty_t v;
	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(e->rcep_t == RCENTITY_ZONE);
	v = e->rcep_p.zone->zone_shmmax + incr;
	if (v > rval->rcv_value)
		return (1);
	return (0);
}

static rctl_ops_t zone_shmmax_ops = {
	rcop_no_action,
	rcop_no_usage,
	rcop_no_set,
	zone_shmmax_test
};

/*ARGSUSED*/
static int
zone_shmmni_test(rctl_t *r, proc_t *p, rctl_entity_p_t *e, rctl_val_t *rval,
    rctl_qty_t incr, uint_t flags)
{
	rctl_qty_t v;
	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(e->rcep_t == RCENTITY_ZONE);
	v = e->rcep_p.zone->zone_ipc.ipcq_shmmni + incr;
	if (v > rval->rcv_value)
		return (1);
	return (0);
}

static rctl_ops_t zone_shmmni_ops = {
	rcop_no_action,
	rcop_no_usage,
	rcop_no_set,
	zone_shmmni_test
};

/*ARGSUSED*/
static int
zone_semmni_test(rctl_t *r, proc_t *p, rctl_entity_p_t *e, rctl_val_t *rval,
    rctl_qty_t incr, uint_t flags)
{
	rctl_qty_t v;
	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(e->rcep_t == RCENTITY_ZONE);
	v = e->rcep_p.zone->zone_ipc.ipcq_semmni + incr;
	if (v > rval->rcv_value)
		return (1);
	return (0);
}

static rctl_ops_t zone_semmni_ops = {
	rcop_no_action,
	rcop_no_usage,
	rcop_no_set,
	zone_semmni_test
};

/*ARGSUSED*/
static int
zone_msgmni_test(rctl_t *r, proc_t *p, rctl_entity_p_t *e, rctl_val_t *rval,
    rctl_qty_t incr, uint_t flags)
{
	rctl_qty_t v;
	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(e->rcep_t == RCENTITY_ZONE);
	v = e->rcep_p.zone->zone_ipc.ipcq_msgmni + incr;
	if (v > rval->rcv_value)
		return (1);
	return (0);
}

static rctl_ops_t zone_msgmni_ops = {
	rcop_no_action,
	rcop_no_usage,
	rcop_no_set,
	zone_msgmni_test
};

/*ARGSUSED*/
static rctl_qty_t
zone_locked_mem_usage(rctl_t *rctl, struct proc *p)
{
	rctl_qty_t q;
	ASSERT(MUTEX_HELD(&p->p_lock));
	mutex_enter(&p->p_zone->zone_mem_lock);
	q = p->p_zone->zone_locked_mem;
	mutex_exit(&p->p_zone->zone_mem_lock);
	return (q);
}

/*ARGSUSED*/
static int
zone_locked_mem_test(rctl_t *r, proc_t *p, rctl_entity_p_t *e,
    rctl_val_t *rcntl, rctl_qty_t incr, uint_t flags)
{
	rctl_qty_t q;
	zone_t *z;

	z = e->rcep_p.zone;
	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(MUTEX_HELD(&z->zone_mem_lock));
	q = z->zone_locked_mem;
	if (q + incr > rcntl->rcv_value)
		return (1);
	return (0);
}

/*ARGSUSED*/
static int
zone_locked_mem_set(rctl_t *rctl, struct proc *p, rctl_entity_p_t *e,
    rctl_qty_t nv)
{
	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(e->rcep_t == RCENTITY_ZONE);
	if (e->rcep_p.zone == NULL)
		return (0);
	e->rcep_p.zone->zone_locked_mem_ctl = nv;
	return (0);
}

static rctl_ops_t zone_locked_mem_ops = {
	rcop_no_action,
	zone_locked_mem_usage,
	zone_locked_mem_set,
	zone_locked_mem_test
};

/*ARGSUSED*/
static rctl_qty_t
zone_max_swap_usage(rctl_t *rctl, struct proc *p)
{
	rctl_qty_t q;
	zone_t *z = p->p_zone;

	ASSERT(MUTEX_HELD(&p->p_lock));
	mutex_enter(&z->zone_mem_lock);
	q = z->zone_max_swap;
	mutex_exit(&z->zone_mem_lock);
	return (q);
}

/*ARGSUSED*/
static int
zone_max_swap_test(rctl_t *r, proc_t *p, rctl_entity_p_t *e,
    rctl_val_t *rcntl, rctl_qty_t incr, uint_t flags)
{
	rctl_qty_t q;
	zone_t *z;

	z = e->rcep_p.zone;
	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(MUTEX_HELD(&z->zone_mem_lock));
	q = z->zone_max_swap;
	if (q + incr > rcntl->rcv_value)
		return (1);
	return (0);
}

/*ARGSUSED*/
static int
zone_max_swap_set(rctl_t *rctl, struct proc *p, rctl_entity_p_t *e,
    rctl_qty_t nv)
{
	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(e->rcep_t == RCENTITY_ZONE);
	if (e->rcep_p.zone == NULL)
		return (0);
	e->rcep_p.zone->zone_max_swap_ctl = nv;
	return (0);
}

static rctl_ops_t zone_max_swap_ops = {
	rcop_no_action,
	zone_max_swap_usage,
	zone_max_swap_set,
	zone_max_swap_test
};

/*ARGSUSED*/
static rctl_qty_t
zone_max_lofi_usage(rctl_t *rctl, struct proc *p)
{
	rctl_qty_t q;
	zone_t *z = p->p_zone;

	ASSERT(MUTEX_HELD(&p->p_lock));
	mutex_enter(&z->zone_rctl_lock);
	q = z->zone_max_lofi;
	mutex_exit(&z->zone_rctl_lock);
	return (q);
}

/*ARGSUSED*/
static int
zone_max_lofi_test(rctl_t *r, proc_t *p, rctl_entity_p_t *e,
    rctl_val_t *rcntl, rctl_qty_t incr, uint_t flags)
{
	rctl_qty_t q;
	zone_t *z;

	z = e->rcep_p.zone;
	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(MUTEX_HELD(&z->zone_rctl_lock));
	q = z->zone_max_lofi;
	if (q + incr > rcntl->rcv_value)
		return (1);
	return (0);
}

/*ARGSUSED*/
static int
zone_max_lofi_set(rctl_t *rctl, struct proc *p, rctl_entity_p_t *e,
    rctl_qty_t nv)
{
	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(e->rcep_t == RCENTITY_ZONE);
	if (e->rcep_p.zone == NULL)
		return (0);
	e->rcep_p.zone->zone_max_lofi_ctl = nv;
	return (0);
}

static rctl_ops_t zone_max_lofi_ops = {
	rcop_no_action,
	zone_max_lofi_usage,
	zone_max_lofi_set,
	zone_max_lofi_test
};

/*
 * Helper function to brand the zone with a unique ID.
 */
static void
zone_uniqid(zone_t *zone)
{
	static uint64_t uniqid = GLOBAL_ZONEUNIQID;

	ASSERT(MUTEX_HELD(&zonehash_lock));
	zone->zone_uniqid = uniqid++;
}

/*
 * Returns a held pointer to the "kcred" for the specified zone.
 */
struct cred *
zone_get_kcred(zoneid_t zoneid)
{
	zone_t *zone;
	cred_t *cr;

	if ((zone = zone_find_by_id(zoneid)) == NULL)
		return (NULL);
	cr = zone->zone_kcred;
	crhold(cr);
	zone_rele(zone);
	return (cr);
}

static int
zone_lockedmem_kstat_update(kstat_t *ksp, int rw)
{
	zone_t *zone = ksp->ks_private;
	zone_kstat_t *zk = ksp->ks_data;

	if (rw == KSTAT_WRITE)
		return (EACCES);

	zk->zk_usage.value.ui64 = zone->zone_locked_mem;
	zk->zk_value.value.ui64 = zone->zone_locked_mem_ctl;
	return (0);
}

static int
zone_nprocs_kstat_update(kstat_t *ksp, int rw)
{
	zone_t *zone = ksp->ks_private;
	zone_kstat_t *zk = ksp->ks_data;

	if (rw == KSTAT_WRITE)
		return (EACCES);

	zk->zk_usage.value.ui64 = zone->zone_nprocs;
	zk->zk_value.value.ui64 = zone->zone_nprocs_ctl;
	return (0);
}

static int
zone_swapresv_kstat_update(kstat_t *ksp, int rw)
{
	zone_t *zone = ksp->ks_private;
	zone_kstat_t *zk = ksp->ks_data;

	if (rw == KSTAT_WRITE)
		return (EACCES);

	zk->zk_usage.value.ui64 = zone->zone_max_swap;
	zk->zk_value.value.ui64 = zone->zone_max_swap_ctl;
	return (0);
}

static kstat_t *
zone_kstat_create_common(zone_t *zone, char *name,
    int (*updatefunc) (kstat_t *, int))
{
	kstat_t *ksp;
	zone_kstat_t *zk;

	ksp = rctl_kstat_create_zone(zone, name, KSTAT_TYPE_NAMED,
	    sizeof (zone_kstat_t) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);

	if (ksp == NULL)
		return (NULL);

	zk = ksp->ks_data = kmem_alloc(sizeof (zone_kstat_t), KM_SLEEP);
	ksp->ks_data_size += strlen(zone->zone_name) + 1;
	kstat_named_init(&zk->zk_zonename, "zonename", KSTAT_DATA_STRING);
	kstat_named_setstr(&zk->zk_zonename, zone->zone_name);
	kstat_named_init(&zk->zk_usage, "usage", KSTAT_DATA_UINT64);
	kstat_named_init(&zk->zk_value, "value", KSTAT_DATA_UINT64);
	ksp->ks_update = updatefunc;
	ksp->ks_private = zone;
	kstat_install(ksp);
	return (ksp);
}

static void
zone_kstat_create(zone_t *zone)
{
	zone->zone_lockedmem_kstat = zone_kstat_create_common(zone,
	    "lockedmem", zone_lockedmem_kstat_update);
	zone->zone_swapresv_kstat = zone_kstat_create_common(zone,
	    "swapresv", zone_swapresv_kstat_update);
	zone->zone_nprocs_kstat = zone_kstat_create_common(zone,
	    "nprocs", zone_nprocs_kstat_update);
}

static void
zone_kstat_delete_common(kstat_t **pkstat)
{
	void *data;

	if (*pkstat != NULL) {
		data = (*pkstat)->ks_data;
		kstat_delete(*pkstat);
		kmem_free(data, sizeof (zone_kstat_t));
		*pkstat = NULL;
	}
}

static void
zone_kstat_delete(zone_t *zone)
{
	zone_kstat_delete_common(&zone->zone_lockedmem_kstat);
	zone_kstat_delete_common(&zone->zone_swapresv_kstat);
	zone_kstat_delete_common(&zone->zone_nprocs_kstat);
}

/*
 * Called very early on in boot to initialize the ZSD list so that
 * zone_key_create() can be called before zone_init().  It also initializes
 * portions of zone0 which may be used before zone_init() is called.  The
 * variable "global_zone" will be set when zone0 is fully initialized by
 * zone_init().
 */
void
zone_zsd_init(void)
{
	mutex_init(&zonehash_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zsd_key_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zsd_registered_keys, sizeof (struct zsd_entry),
	    offsetof(struct zsd_entry, zsd_linkage));
	list_create(&zone_active, sizeof (zone_t),
	    offsetof(zone_t, zone_linkage));
	list_create(&zone_deathrow, sizeof (zone_t),
	    offsetof(zone_t, zone_linkage));

	mutex_init(&zone0.zone_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zone0.zone_nlwps_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zone0.zone_mem_lock, NULL, MUTEX_DEFAULT, NULL);
	zone0.zone_shares = 1;
	zone0.zone_nlwps = 0;
	zone0.zone_nlwps_ctl = INT_MAX;
	zone0.zone_nprocs = 0;
	zone0.zone_nprocs_ctl = INT_MAX;
	zone0.zone_locked_mem = 0;
	zone0.zone_locked_mem_ctl = UINT64_MAX;
	ASSERT(zone0.zone_max_swap == 0);
	zone0.zone_max_swap_ctl = UINT64_MAX;
	zone0.zone_max_lofi = 0;
	zone0.zone_max_lofi_ctl = UINT64_MAX;
	zone0.zone_shmmax = 0;
	zone0.zone_ipc.ipcq_shmmni = 0;
	zone0.zone_ipc.ipcq_semmni = 0;
	zone0.zone_ipc.ipcq_msgmni = 0;
	zone0.zone_name = GLOBAL_ZONENAME;
	zone0.zone_nodename = utsname.nodename;
	zone0.zone_domain = srpc_domain;
	zone0.zone_hostid = HW_INVALID_HOSTID;
	zone0.zone_fs_allowed = NULL;
	zone0.zone_ref = 1;
	zone0.zone_id = GLOBAL_ZONEID;
	zone0.zone_status = ZONE_IS_RUNNING;
	zone0.zone_rootpath = "/";
	zone0.zone_rootpathlen = 2;
	zone0.zone_psetid = ZONE_PS_INVAL;
	zone0.zone_ncpus = 0;
	zone0.zone_ncpus_online = 0;
	zone0.zone_proc_initpid = 1;
	zone0.zone_initname = initname;
	zone0.zone_lockedmem_kstat = NULL;
	zone0.zone_swapresv_kstat = NULL;
	zone0.zone_nprocs_kstat = NULL;
	zone0.zone_devann_hash = NULL;
	mutex_init(&zone0.zone_devann_lock, NULL, MUTEX_DEFAULT, NULL);

	list_create(&zone0.zone_ref_list, sizeof (zone_ref_t),
	    offsetof(zone_ref_t, zref_linkage));
	list_create(&zone0.zone_zsd, sizeof (struct zsd_entry),
	    offsetof(struct zsd_entry, zsd_linkage));
	list_insert_head(&zone_active, &zone0);

	/*
	 * These lists will not be used for global zone and all the ways
	 * of accessing these lists are blocked as of now. Initializing
	 * these lists to invalid values will help to find new bugs.
	 */
	(void) memset(&zone0.zone_datasets, -1, sizeof (list_t));
	(void) memset(&zone0.zone_dl_list, -1, sizeof (list_t));

	/*
	 * The root filesystem is not mounted yet, so zone_rootvp cannot be set
	 * to anything meaningful.  It is assigned to be 'rootdir' in
	 * vfs_mountroot().
	 */
	zone0.zone_rootvp = NULL;
	zone0.zone_vfslist = NULL;
	zone0.zone_bootargs = initargs;
	zone0.zone_macprofile = NULL;
	zone0.zone_privset = kmem_alloc(sizeof (priv_set_t), KM_SLEEP);
	/*
	 * The global zone has all privileges
	 */
	priv_fillset(zone0.zone_privset);
	/*
	 * Add p0 to the global zone
	 */
	zone0.zone_zsched = &p0;
	p0.p_zone = &zone0;
}

/*
 * Compute a hash value based on the contents of the label and the DOI.  The
 * hash algorithm is somewhat arbitrary, but is based on the observation that
 * humans will likely pick labels that differ by amounts that work out to be
 * multiples of the number of hash chains, and thus stirring in some primes
 * should help.
 */
static uint_t
hash_bylabel(void *hdata, mod_hash_key_t key)
{
	const ts_label_t *lab = (ts_label_t *)key;
	const uint32_t *up, *ue;
	uint_t hash;
	int i;

	_NOTE(ARGUNUSED(hdata));

	hash = lab->tsl_doi + (lab->tsl_doi << 1);
	/* we depend on alignment of label, but not representation */
	up = (const uint32_t *)&lab->tsl_label;
	ue = up + sizeof (lab->tsl_label) / sizeof (*up);
	i = 1;
	while (up < ue) {
		/* using 2^n + 1, 1 <= n <= 16 as source of many primes */
		hash += *up + (*up << ((i % 16) + 1));
		up++;
		i++;
	}
	return (hash);
}

/*
 * All that mod_hash cares about here is zero (equal) versus non-zero (not
 * equal).  This may need to be changed if less than / greater than is ever
 * needed.
 */
static int
hash_labelkey_cmp(mod_hash_key_t key1, mod_hash_key_t key2)
{
	ts_label_t *lab1 = (ts_label_t *)key1;
	ts_label_t *lab2 = (ts_label_t *)key2;

	return (label_equal(lab1, lab2) ? 0 : 1);
}

/*
 * Called by main() to initialize the zones framework.
 */
void
zone_init(void)
{
	rctl_dict_entry_t *rde;
	rctl_val_t *dval;
	rctl_set_t *set;
	rctl_alloc_gp_t *gp;
	rctl_entity_p_t e;
	int res;

	ASSERT(curproc == &p0);

	/*
	 * Create ID space for zone IDs.  ID 0 is reserved for the
	 * global zone.
	 */
	zoneid_space = id_space_create("zoneid_space", 1, MAX_ZONEID);

	/*
	 * Initialize generic zone resource controls, if any.
	 */
	rc_zone_cpu_shares = rctl_register("zone.cpu-shares",
	    RCENTITY_ZONE, RCTL_GLOBAL_SIGNAL_NEVER | RCTL_GLOBAL_DENY_NEVER |
	    RCTL_GLOBAL_NOBASIC | RCTL_GLOBAL_COUNT | RCTL_GLOBAL_SYSLOG_NEVER,
	    FSS_MAXSHARES, FSS_MAXSHARES, &zone_cpu_shares_ops);

	rc_zone_cpu_cap = rctl_register("zone.cpu-cap",
	    RCENTITY_ZONE, RCTL_GLOBAL_SIGNAL_NEVER | RCTL_GLOBAL_DENY_ALWAYS |
	    RCTL_GLOBAL_NOBASIC | RCTL_GLOBAL_COUNT |RCTL_GLOBAL_SYSLOG_NEVER |
	    RCTL_GLOBAL_INFINITE,
	    MAXCAP, MAXCAP, &zone_cpu_cap_ops);

	rc_zone_nlwps = rctl_register("zone.max-lwps", RCENTITY_ZONE,
	    RCTL_GLOBAL_NOACTION | RCTL_GLOBAL_NOBASIC | RCTL_GLOBAL_COUNT,
	    INT_MAX, INT_MAX, &zone_lwps_ops);

	rc_zone_nprocs = rctl_register("zone.max-processes", RCENTITY_ZONE,
	    RCTL_GLOBAL_NOACTION | RCTL_GLOBAL_NOBASIC | RCTL_GLOBAL_COUNT,
	    INT_MAX, INT_MAX, &zone_procs_ops);

	/*
	 * System V IPC resource controls
	 */
	rc_zone_msgmni = rctl_register("zone.max-msg-ids",
	    RCENTITY_ZONE, RCTL_GLOBAL_DENY_ALWAYS | RCTL_GLOBAL_NOBASIC |
	    RCTL_GLOBAL_COUNT, IPC_IDS_MAX, IPC_IDS_MAX, &zone_msgmni_ops);

	rc_zone_semmni = rctl_register("zone.max-sem-ids",
	    RCENTITY_ZONE, RCTL_GLOBAL_DENY_ALWAYS | RCTL_GLOBAL_NOBASIC |
	    RCTL_GLOBAL_COUNT, IPC_IDS_MAX, IPC_IDS_MAX, &zone_semmni_ops);

	rc_zone_shmmni = rctl_register("zone.max-shm-ids",
	    RCENTITY_ZONE, RCTL_GLOBAL_DENY_ALWAYS | RCTL_GLOBAL_NOBASIC |
	    RCTL_GLOBAL_COUNT, IPC_IDS_MAX, IPC_IDS_MAX, &zone_shmmni_ops);

	rc_zone_shmmax = rctl_register("zone.max-shm-memory",
	    RCENTITY_ZONE, RCTL_GLOBAL_DENY_ALWAYS | RCTL_GLOBAL_NOBASIC |
	    RCTL_GLOBAL_BYTES, UINT64_MAX, UINT64_MAX, &zone_shmmax_ops);

	/*
	 * Create a rctl_val with PRIVILEGED, NOACTION, value = 1.  Then attach
	 * this at the head of the rctl_dict_entry for "zone.cpu-shares".
	 */
	dval = kmem_cache_alloc(rctl_val_cache, KM_SLEEP);
	bzero(dval, sizeof (rctl_val_t));
	dval->rcv_value = 1;
	dval->rcv_privilege = RCPRIV_PRIVILEGED;
	dval->rcv_flagaction = RCTL_LOCAL_NOACTION;
	dval->rcv_action_recip_pid = -1;

	rde = rctl_dict_lookup("zone.cpu-shares");
	(void) rctl_val_list_insert(&rde->rcd_default_value, dval);

	rc_zone_locked_mem = rctl_register("zone.max-locked-memory",
	    RCENTITY_ZONE, RCTL_GLOBAL_NOBASIC | RCTL_GLOBAL_BYTES |
	    RCTL_GLOBAL_DENY_ALWAYS, UINT64_MAX, UINT64_MAX,
	    &zone_locked_mem_ops);

	rc_zone_max_swap = rctl_register("zone.max-swap",
	    RCENTITY_ZONE, RCTL_GLOBAL_NOBASIC | RCTL_GLOBAL_BYTES |
	    RCTL_GLOBAL_DENY_ALWAYS, UINT64_MAX, UINT64_MAX,
	    &zone_max_swap_ops);

	rc_zone_max_lofi = rctl_register("zone.max-lofi",
	    RCENTITY_ZONE, RCTL_GLOBAL_NOBASIC | RCTL_GLOBAL_COUNT |
	    RCTL_GLOBAL_DENY_ALWAYS, UINT64_MAX, UINT64_MAX,
	    &zone_max_lofi_ops);

	/*
	 * Initialize the "global zone".
	 */
	set = rctl_set_create();
	gp = rctl_set_init_prealloc(RCENTITY_ZONE);
	mutex_enter(&p0.p_lock);
	e.rcep_p.zone = &zone0;
	e.rcep_t = RCENTITY_ZONE;
	zone0.zone_rctls = rctl_set_init(RCENTITY_ZONE, &p0, &e, set,
	    gp);

	zone0.zone_nlwps = p0.p_lwpcnt;
	zone0.zone_nprocs = 1;
	zone0.zone_ntasks = 1;
	t0.t_zone = &zone0;
	mutex_exit(&p0.p_lock);
	zone0.zone_restart_init = B_TRUE;
	zone0.zone_brand_name = NULL;
	zone0.zone_brand = NULL;
	rctl_prealloc_destroy(gp);
	/*
	 * pool_default hasn't been initialized yet, so we let pool_init()
	 * take care of making sure the global zone is in the default pool.
	 */

	/*
	 * Initialize global zone kstats
	 */
	zone_kstat_create(&zone0);

	/*
	 * Initialize zone label.
	 * mlp are initialized when tnzonecfg is loaded.
	 */
	zone0.zone_slabel = l_admin_low;
	rw_init(&zone0.zone_mlps.mlpl_rwlock, NULL, RW_DEFAULT, NULL);
	label_hold(l_admin_low);

	/*
	 * Initialise the lock for the database structure used by mntfs.
	 */
	rw_init(&zone0.zone_mntfs_db_lock, NULL, RW_DEFAULT, NULL);

	mutex_enter(&zonehash_lock);
	zone_uniqid(&zone0);
	ASSERT(zone0.zone_uniqid == GLOBAL_ZONEUNIQID);

	zonehashbyid = mod_hash_create_idhash("zone_by_id", zone_hash_size,
	    mod_hash_null_valdtor);
	zonehashbyname = mod_hash_create_strhash("zone_by_name",
	    zone_hash_size, mod_hash_null_valdtor);
	/*
	 * maintain zonehashbylabel only for labeled systems
	 */
	if (is_system_labeled())
		zonehashbylabel = mod_hash_create_extended("zone_by_label",
		    zone_hash_size, mod_hash_null_keydtor,
		    mod_hash_null_valdtor, hash_bylabel, NULL,
		    hash_labelkey_cmp, KM_SLEEP);
	zonecount = 1;

	(void) mod_hash_insert(zonehashbyid, (mod_hash_key_t)GLOBAL_ZONEID,
	    (mod_hash_val_t)&zone0);
	(void) mod_hash_insert(zonehashbyname, (mod_hash_key_t)zone0.zone_name,
	    (mod_hash_val_t)&zone0);
	if (is_system_labeled()) {
		zone0.zone_flags |= ZF_HASHED_LABEL;
		(void) mod_hash_insert(zonehashbylabel,
		    (mod_hash_key_t)zone0.zone_slabel, (mod_hash_val_t)&zone0);
	}
	mutex_exit(&zonehash_lock);

	/*
	 * We avoid setting zone_kcred until now, since kcred is initialized
	 * sometime after zone_zsd_init() and before zone_init().
	 */
	zone0.zone_kcred = kcred;
	/*
	 * The global zone is fully initialized (except for zone_rootvp which
	 * will be set when the root filesystem is mounted).
	 */
	global_zone = &zone0;

	/*
	 * Setup an event channel to send zone status change notifications on
	 */
	res = sysevent_evc_bind(ZONE_EVENT_CHANNEL, &zone_event_chan,
	    EVCH_CREAT);

	if (res)
		panic("Sysevent_evc_bind failed during zone setup.\n");

}

static void
zone_free(zone_t *zone)
{
	ASSERT(zone != global_zone);
	ASSERT(zone->zone_ntasks == 0);
	ASSERT(zone->zone_nlwps == 0);
	ASSERT(zone->zone_nprocs == 0);
	ASSERT(zone->zone_cred_ref == 0);
	ASSERT(zone->zone_kcred == NULL);
	ASSERT(zone_status_get(zone) == ZONE_IS_DEAD ||
	    zone_status_get(zone) == ZONE_IS_UNINITIALIZED);
	ASSERT(list_is_empty(&zone->zone_ref_list));
	ASSERT(zone->zone_cpucap == NULL);

	/* remove from deathrow list */
	if (zone_status_get(zone) == ZONE_IS_DEAD) {
		ASSERT(zone->zone_ref == 0);
		mutex_enter(&zone_deathrow_lock);
		ASSERT(zone_deathrow_count > 0);
		zone_deathrow_count--;
		list_remove(&zone_deathrow, zone);
		mutex_exit(&zone_deathrow_lock);
	}

	list_destroy(&zone->zone_ref_list);
	zone_free_zsd(zone);
	zone_free_datasets(zone);
	list_destroy(&zone->zone_dl_list);

	if (zone->zone_rootvp != NULL)
		VN_RELE(zone->zone_rootvp);
	if (zone->zone_rootpath)
		kmem_free(zone->zone_rootpath, zone->zone_rootpathlen);
	if (zone->zone_name != NULL)
		kmem_free(zone->zone_name, ZONENAME_MAX);
	if (zone->zone_slabel != NULL)
		label_rele(zone->zone_slabel);
	if (zone->zone_nodename != NULL)
		kmem_free(zone->zone_nodename, _SYS_NMLN);
	if (zone->zone_domain != NULL)
		kmem_free(zone->zone_domain, _SYS_NMLN);
	if (zone->zone_privset != NULL)
		kmem_free(zone->zone_privset, sizeof (priv_set_t));
	if (zone->zone_rctls != NULL)
		rctl_set_free(zone->zone_rctls);
	if (zone->zone_bootargs != NULL)
		strfree(zone->zone_bootargs);
	if (zone->zone_initname != NULL)
		strfree(zone->zone_initname);
	if (zone->zone_fs_allowed != NULL)
		strfree(zone->zone_fs_allowed);
	if (zone->zone_macprofile != NULL)
		strfree(zone->zone_macprofile);
	if (zone->zone_pfexecd != NULL)
		klpd_freelist(&zone->zone_pfexecd);
	if (zone->zone_brand_name != NULL)
		strfree(zone->zone_brand_name);

	if (zone->zone_devann_hash != NULL)
		mod_hash_destroy_hash(zone->zone_devann_hash);
	mutex_destroy(&zone->zone_devann_lock);
	if (zone->zone_mwac_white_list != NULL)
		klpd_freemwac_tree(zone->zone_mwac_white_list);
	if (zone->zone_mwac_black_list != NULL)
		klpd_freemwac_tree(zone->zone_mwac_black_list);
	mutex_destroy(&zone->zone_lock);
	cv_destroy(&zone->zone_cv);
	rw_destroy(&zone->zone_mlps.mlpl_rwlock);
	rw_destroy(&zone->zone_mntfs_db_lock);
	kmem_free(zone, sizeof (zone_t));
}

/*
 * See block comment at the top of this file for information about zone
 * status values.
 */
/*
 * Convenience function for setting zone status.
 */
static void
zone_status_set(zone_t *zone, zone_status_t status)
{
	nvlist_t *nvl = NULL;

	ASSERT(MUTEX_HELD(&zone->zone_lock));
	ASSERT(status > ZONE_MIN_STATE && status <= ZONE_MAX_STATE &&
	    status >= zone_status_get(zone));

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP) ||
	    nvlist_add_string(nvl, ZONE_CB_NAME, zone->zone_name) ||
	    nvlist_add_string(nvl, ZONE_CB_NEWSTATE,
	    zone_status_table[status]) ||
	    nvlist_add_string(nvl, ZONE_CB_OLDSTATE,
	    zone_status_table[zone->zone_status]) ||
	    nvlist_add_int32(nvl, ZONE_CB_ZONEID, zone->zone_id) ||
	    nvlist_add_uint64(nvl, ZONE_CB_TIMESTAMP, (uint64_t)gethrtime()) ||
	    sysevent_evc_publish(zone_event_chan, ZONE_EVENT_STATUS_CLASS,
	    ZONE_EVENT_STATUS_SUBCLASS, "sun.com", "kernel", nvl, EVCH_SLEEP)) {
#ifdef DEBUG
		(void) printf(
		    "Failed to allocate and send zone state change event.\n");
#endif
	}
	nvlist_free(nvl);

	zone->zone_status = status;

	cv_broadcast(&zone->zone_cv);
}

/*
 * Public function to retrieve the zone status.  The zone status may
 * change after it is retrieved.
 */
zone_status_t
zone_status_get(zone_t *zone)
{
	return (zone->zone_status);
}

static int
zone_set_bootargs(zone_t *zone, const char *zone_bootargs)
{
	char *buf = kmem_zalloc(BOOTARGS_MAX, KM_SLEEP);
	int err = 0;

	ASSERT(zone != global_zone);
	if ((err = copyinstr(zone_bootargs, buf, BOOTARGS_MAX, NULL)) != 0)
		goto done;	/* EFAULT or ENAMETOOLONG */

	if (zone->zone_bootargs != NULL)
		strfree(zone->zone_bootargs);

	zone->zone_bootargs = strdup(buf);

done:
	kmem_free(buf, BOOTARGS_MAX);
	return (err);
}

static int
zone_set_mac_profile(zone_t *zone, const char *zone_mac_profile)
{
	char *buf = kmem_zalloc(MAXNAMELEN, KM_SLEEP);
	int err = 0;

	ASSERT(zone != global_zone);
	if ((err = copyinstr(zone_mac_profile, buf, MAXNAMELEN, NULL)) != 0)
		goto done;	/* EFAULT or ENAMETOOLONG */

	if (zone->zone_macprofile != NULL)
		strfree(zone->zone_macprofile);

	zone->zone_macprofile = strdup(buf);

done:
	kmem_free(buf, MAXNAMELEN);
	return (err);
}

static int
zone_set_brand(zone_t *zone, const char *brand)
{
	struct brand_attr *attrp;
	brand_t *bp = NULL;

	attrp = kmem_alloc(sizeof (struct brand_attr), KM_SLEEP);
	if (copyin(brand, attrp, sizeof (struct brand_attr)) != 0) {
		kmem_free(attrp, sizeof (struct brand_attr));
		return (EFAULT);
	}

	/* if branding, a brand name must be specified */
	if (strlen(attrp->ba_brandname) == 0) {
		kmem_free(attrp, sizeof (struct brand_attr));
		return (EINVAL);
	}

	/* if a brand module was requested then open it */
	if ((strlen(attrp->ba_modname) != 0) &&
	    ((bp = brand_modopen(attrp->ba_modname)) == NULL)) {
		kmem_free(attrp, sizeof (struct brand_attr));
		return (EINVAL);
	}

	/*
	 * This is the only place where a zone can change it's brand.
	 */
	mutex_enter(&zone->zone_lock);

	/* Re-Branding is not allowed and the zone can't be booted yet */
	if ((zone->zone_brand_name != NULL) || ZONE_HAS_BRANDOPS(zone) ||
	    (zone_status_get(zone) >= ZONE_IS_BOOTING)) {
		mutex_exit(&zone->zone_lock);
		if (bp != NULL)
			brand_modclose(bp);
		kmem_free(attrp, sizeof (struct brand_attr));
		return (EINVAL);
	}

	/* set up the brand specific data */
	zone->zone_brand_name = strdup(attrp->ba_brandname);
	zone->zone_brand = bp;
	if (ZONE_HAS_BRANDOPS(zone))
		ZBROP(zone)->b_init_brand_data(zone);
	mutex_exit(&zone->zone_lock);
	kmem_free(attrp, sizeof (struct brand_attr));
	return (0);
}

static int
zone_set_fs_allowed(zone_t *zone, const char *zone_fs_allowed)
{
	char *buf = kmem_zalloc(ZONE_FS_ALLOWED_MAX, KM_SLEEP);
	int err = 0;

	ASSERT(zone != global_zone);
	if ((err = copyinstr(zone_fs_allowed, buf,
	    ZONE_FS_ALLOWED_MAX, NULL)) != 0)
		goto done;

	if (zone->zone_fs_allowed != NULL)
		strfree(zone->zone_fs_allowed);

	zone->zone_fs_allowed = strdup(buf);

done:
	kmem_free(buf, ZONE_FS_ALLOWED_MAX);
	return (err);
}

static int
zone_set_initname(zone_t *zone, const char *zone_initname)
{
	char initname[INITNAME_SZ];
	size_t len;
	int err = 0;

	ASSERT(zone != global_zone);
	if ((err = copyinstr(zone_initname, initname, INITNAME_SZ, &len)) != 0)
		return (err);	/* EFAULT or ENAMETOOLONG */

	if (zone->zone_initname != NULL)
		strfree(zone->zone_initname);

	zone->zone_initname = kmem_alloc(strlen(initname) + 1, KM_SLEEP);
	(void) strcpy(zone->zone_initname, initname);
	return (0);
}

static int
zone_set_phys_mcap(zone_t *zone, const uint64_t *zone_mcap)
{
	uint64_t mcap;
	int err = 0;

	if ((err = copyin(zone_mcap, &mcap, sizeof (uint64_t))) == 0)
		zone->zone_phys_mcap = mcap;

	return (err);
}

static int
zone_set_sched_class(zone_t *zone, const char *new_class)
{
	char sched_class[PC_CLNMSZ];
	id_t classid;
	int err;

	ASSERT(zone != global_zone);
	if ((err = copyinstr(new_class, sched_class, PC_CLNMSZ, NULL)) != 0)
		return (err);	/* EFAULT or ENAMETOOLONG */

	if (getcid(sched_class, &classid) != 0 || CLASS_KERNEL(classid))
		return (EINVAL);
	zone->zone_defaultcid = classid;
	ASSERT(zone->zone_defaultcid > 0 &&
	    zone->zone_defaultcid < loaded_classes);

	return (0);
}

/*
 * Block indefinitely waiting for (zone_status >= status)
 */
void
zone_status_wait(zone_t *zone, zone_status_t status)
{
	ASSERT(status > ZONE_MIN_STATE && status <= ZONE_MAX_STATE);

	mutex_enter(&zone->zone_lock);
	while (zone->zone_status < status) {
		cv_wait(&zone->zone_cv, &zone->zone_lock);
	}
	mutex_exit(&zone->zone_lock);
}

/*
 * Private CPR-safe version of zone_status_wait().
 */
static void
zone_status_wait_cpr(zone_t *zone, zone_status_t status, char *str)
{
	callb_cpr_t cprinfo;

	ASSERT(status > ZONE_MIN_STATE && status <= ZONE_MAX_STATE);

	CALLB_CPR_INIT(&cprinfo, &zone->zone_lock, callb_generic_cpr,
	    str);
	mutex_enter(&zone->zone_lock);
	while (zone->zone_status < status) {
		CALLB_CPR_SAFE_BEGIN(&cprinfo);
		cv_wait(&zone->zone_cv, &zone->zone_lock);
		CALLB_CPR_SAFE_END(&cprinfo, &zone->zone_lock);
	}
	/*
	 * zone_lock is implicitly released by the following.
	 */
	CALLB_CPR_EXIT(&cprinfo);
}

/*
 * Block until zone enters requested state or signal is received.  Return (0)
 * if signaled, non-zero otherwise.
 */
int
zone_status_wait_sig(zone_t *zone, zone_status_t status)
{
	ASSERT(status > ZONE_MIN_STATE && status <= ZONE_MAX_STATE);

	mutex_enter(&zone->zone_lock);
	while (zone->zone_status < status) {
		if (!cv_wait_sig(&zone->zone_cv, &zone->zone_lock)) {
			mutex_exit(&zone->zone_lock);
			return (0);
		}
	}
	mutex_exit(&zone->zone_lock);
	return (1);
}

/*
 * Block until the zone enters the requested state or the timeout expires,
 * whichever happens first.  Return (-1) if operation timed out, time remaining
 * otherwise.
 */
clock_t
zone_status_timedwait(zone_t *zone, clock_t tim, zone_status_t status)
{
	clock_t timeleft = 0;

	ASSERT(status > ZONE_MIN_STATE && status <= ZONE_MAX_STATE);

	mutex_enter(&zone->zone_lock);
	while (zone->zone_status < status && timeleft != -1) {
		timeleft = cv_timedwait(&zone->zone_cv, &zone->zone_lock, tim);
	}
	mutex_exit(&zone->zone_lock);
	return (timeleft);
}

/*
 * Block until the zone enters the requested state, the current process is
 * signaled,  or the timeout expires, whichever happens first.  Return (-1) if
 * operation timed out, 0 if signaled, time remaining otherwise.
 */
clock_t
zone_status_timedwait_sig(zone_t *zone, clock_t tim, zone_status_t status)
{
	clock_t timeleft = tim - ddi_get_lbolt();

	ASSERT(status > ZONE_MIN_STATE && status <= ZONE_MAX_STATE);

	mutex_enter(&zone->zone_lock);
	while (zone->zone_status < status) {
		timeleft = cv_timedwait_sig(&zone->zone_cv, &zone->zone_lock,
		    tim);
		if (timeleft <= 0)
			break;
	}
	mutex_exit(&zone->zone_lock);
	return (timeleft);
}

/*
 * Zones have two reference counts: one for references from credential
 * structures (zone_cred_ref), and one (zone_ref) for everything else.
 * The two are kept separate because drivers may cache dblk_ts (which
 * have creds) and we would like to treat that kind of "reference leak"
 * distinct from the more significant kind of an actual reference (or
 * memory associated with a reference) leak.
 *
 * Zones also provide a tracked reference counting mechanism in which zone
 * references are represented by "crumbs" (zone_ref structures).  Crumbs help
 * debuggers determine the sources of leaked zone references.  See
 * zone_hold_ref() and zone_rele_ref() below for more information.
 */
static void
zone_hold_locked(zone_t *z)
{
	ASSERT(MUTEX_HELD(&z->zone_lock));
	z->zone_ref++;
	ASSERT(z->zone_ref != 0);
}

/*
 * Increment the specified zone's reference count.  The zone's zone_t structure
 * will not be freed as long as the zone's reference count is nonzero.
 * Decrement the zone's reference count via zone_rele().
 *
 * NOTE: This function should only be used to hold zones for short periods of
 * time.  Use zone_hold_ref() if the zone must be held for a long time.
 */
void
zone_hold(zone_t *z)
{
	mutex_enter(&z->zone_lock);
	zone_hold_locked(z);
	mutex_exit(&z->zone_lock);
}

/*
 * Common zone reference release function invoked by zone_rele() and
 * zone_rele_ref().  If subsys is ZONE_REF_NUM_SUBSYS, then the specified
 * zone's subsystem-specific reference counters are not affected by the
 * release.  If ref is not NULL, then the zone_ref_t to which it refers is
 * removed from the specified zone's reference list.  ref must be non-NULL iff
 * subsys is not ZONE_REF_NUM_SUBSYS.
 */
static void
zone_rele_common(zone_t *z, zone_ref_t *ref, zone_ref_subsys_t subsys)
{
	mutex_enter(&z->zone_lock);
	ASSERT(z->zone_ref != 0);
	z->zone_ref--;
	if (subsys != ZONE_REF_NUM_SUBSYS) {
		ASSERT(z->zone_subsys_ref[subsys] != 0);
		z->zone_subsys_ref[subsys]--;
		list_remove(&z->zone_ref_list, ref);
	}
	if (z->zone_ref == 0 && z->zone_cred_ref == 0) {
		/* no more refs, free the structure */
		mutex_exit(&z->zone_lock);
		zone_free(z);
		return;
	}
	mutex_exit(&z->zone_lock);
}

/*
 * Decrement the specified zone's reference count.  The specified zone will
 * cease to exist after this function returns if the reference count drops to
 * zero.  This function should be paired with zone_hold().
 */
void
zone_rele(zone_t *z)
{
	zone_rele_common(z, NULL, ZONE_REF_NUM_SUBSYS);
}

/*
 * Initialize a zone reference structure.  This function must be invoked for
 * a reference structure before the structure is passed to zone_hold_ref().
 */
void
zone_init_ref(zone_ref_t *ref)
{
	ref->zref_zone = NULL;
	list_link_init(&ref->zref_linkage);
}

/*
 * Acquire a reference to zone z.  The caller must specify the
 * zone_ref_subsys_t constant associated with its subsystem.  The specified
 * zone_ref_t structure will represent a reference to the specified zone.  Use
 * zone_rele_ref() to release the reference.
 *
 * The referenced zone_t structure will not be freed as long as the zone_t's
 * zone_status field is not ZONE_IS_DEAD and the zone has outstanding
 * references.
 *
 * NOTE: The zone_ref_t structure must be initialized before it is used.
 * See zone_init_ref() above.
 */
void
zone_hold_ref(zone_t *z, zone_ref_t *ref, zone_ref_subsys_t subsys)
{
	ASSERT(subsys >= 0 && subsys < ZONE_REF_NUM_SUBSYS);

	/*
	 * Prevent consumers from reusing a reference structure before
	 * releasing it.
	 */
	VERIFY(ref->zref_zone == NULL);

	ref->zref_zone = z;
	mutex_enter(&z->zone_lock);
	zone_hold_locked(z);
	z->zone_subsys_ref[subsys]++;
	ASSERT(z->zone_subsys_ref[subsys] != 0);
	list_insert_head(&z->zone_ref_list, ref);
	mutex_exit(&z->zone_lock);
}

/*
 * Release the zone reference represented by the specified zone_ref_t.
 * The reference is invalid after it's released; however, the zone_ref_t
 * structure can be reused without having to invoke zone_init_ref().
 * subsys should be the same value that was passed to zone_hold_ref()
 * when the reference was acquired.
 */
void
zone_rele_ref(zone_ref_t *ref, zone_ref_subsys_t subsys)
{
	zone_rele_common(ref->zref_zone, ref, subsys);

	/*
	 * Set the zone_ref_t's zref_zone field to NULL to generate panics
	 * when consumers dereference the reference.  This helps us catch
	 * consumers who use released references.  Furthermore, this lets
	 * consumers reuse the zone_ref_t structure without having to
	 * invoke zone_init_ref().
	 */
	ref->zref_zone = NULL;
}

void
zone_cred_hold(zone_t *z)
{
	mutex_enter(&z->zone_lock);
	z->zone_cred_ref++;
	ASSERT(z->zone_cred_ref != 0);
	mutex_exit(&z->zone_lock);
}

void
zone_cred_rele(zone_t *z)
{
	mutex_enter(&z->zone_lock);
	ASSERT(z->zone_cred_ref != 0);
	z->zone_cred_ref--;
	if (z->zone_ref == 0 && z->zone_cred_ref == 0) {
		/* no more refs, free the structure */
		mutex_exit(&z->zone_lock);
		zone_free(z);
		return;
	}
	mutex_exit(&z->zone_lock);
}

void
zone_task_hold(zone_t *z)
{
	mutex_enter(&z->zone_lock);
	z->zone_ntasks++;
	ASSERT(z->zone_ntasks != 0);
	mutex_exit(&z->zone_lock);
}

void
zone_task_rele(zone_t *zone)
{
	uint_t refcnt;

	mutex_enter(&zone->zone_lock);
	ASSERT(zone->zone_ntasks != 0);
	refcnt = --zone->zone_ntasks;
	if (refcnt > 1)	{	/* Common case */
		mutex_exit(&zone->zone_lock);
		return;
	}
	if (refcnt == 1) {
		/*
		 * Ignore if the zone isn't shutting down.
		 * The zone can be ZONE_IS_READY if zone_enter(2) was
		 * called before zone_boot(2)
		 */
		if (zone_status_get(zone) <= ZONE_IS_RUNNING) {
			mutex_exit(&zone->zone_lock);
			return;
		}

		/*
		 * No more user processes in the zone.  The zone is empty.
		 */
		ASSERT(zone_status_get(zone) == ZONE_IS_SHUTTING_DOWN);
		zone_status_set(zone, ZONE_IS_EMPTY);
		mutex_exit(&zone->zone_lock);
		return;
	}

	ASSERT(refcnt == 0);
	/*
	 * zsched has exited; the zone is dead.
	 */
	zone->zone_zsched = NULL;		/* paranoia */
	zone_status_set(zone, ZONE_IS_DEAD);
	mutex_exit(&zone->zone_lock);
}

zoneid_t
getzoneid(void)
{
	return (curproc->p_zone->zone_id);
}

/*
 * Internal versions of zone_find_by_*().  These don't zone_hold() or
 * check the validity of a zone's state.
 */
static zone_t *
zone_find_all_by_id(zoneid_t zoneid)
{
	mod_hash_val_t hv;
	zone_t *zone = NULL;

	ASSERT(MUTEX_HELD(&zonehash_lock));

	if (mod_hash_find(zonehashbyid,
	    (mod_hash_key_t)(uintptr_t)zoneid, &hv) == 0)
		zone = (zone_t *)hv;
	return (zone);
}

static zone_t *
zone_find_all_by_label(const ts_label_t *label)
{
	mod_hash_val_t hv;
	zone_t *zone = NULL;

	ASSERT(MUTEX_HELD(&zonehash_lock));

	/*
	 * zonehashbylabel is not maintained for unlabeled systems
	 */
	if (!is_system_labeled())
		return (NULL);
	if (mod_hash_find(zonehashbylabel, (mod_hash_key_t)label, &hv) == 0)
		zone = (zone_t *)hv;
	return (zone);
}

static zone_t *
zone_find_all_by_name(const char *name)
{
	mod_hash_val_t hv;
	zone_t *zone = NULL;

	ASSERT(MUTEX_HELD(&zonehash_lock));

	if (mod_hash_find(zonehashbyname, (mod_hash_key_t)name, &hv) == 0)
		zone = (zone_t *)hv;
	return (zone);
}

/*
 * Public interface for looking up a zone by zoneid.  Only returns the zone if
 * it is fully initialized, and has not yet begun the zone_destroy() sequence.
 * Caller must call zone_rele() once it is done with the zone.
 *
 * The zone may begin the zone_destroy() sequence immediately after this
 * function returns, but may be safely used until zone_rele() is called.
 */
zone_t *
zone_find_by_id(zoneid_t zoneid)
{
	zone_t *zone;
	zone_status_t status;

	mutex_enter(&zonehash_lock);
	if ((zone = zone_find_all_by_id(zoneid)) == NULL) {
		mutex_exit(&zonehash_lock);
		return (NULL);
	}
	status = zone_status_get(zone);
	if (status < ZONE_IS_READY || status > ZONE_IS_DOWN) {
		/*
		 * For all practical purposes the zone doesn't exist.
		 */
		mutex_exit(&zonehash_lock);
		return (NULL);
	}
	zone_hold(zone);
	mutex_exit(&zonehash_lock);
	return (zone);
}

/*
 * Similar to zone_find_by_id, but using zone label as the key.
 */
zone_t *
zone_find_by_label(const ts_label_t *label)
{
	zone_t *zone;
	zone_status_t status;

	mutex_enter(&zonehash_lock);
	if ((zone = zone_find_all_by_label(label)) == NULL) {
		mutex_exit(&zonehash_lock);
		return (NULL);
	}

	status = zone_status_get(zone);
	if (status > ZONE_IS_DOWN) {
		/*
		 * For all practical purposes the zone doesn't exist.
		 */
		mutex_exit(&zonehash_lock);
		return (NULL);
	}
	zone_hold(zone);
	mutex_exit(&zonehash_lock);
	return (zone);
}

/*
 * Similar to zone_find_by_id, but using zone name as the key.
 */
zone_t *
zone_find_by_name(const char *name)
{
	zone_t *zone;
	zone_status_t status;

	mutex_enter(&zonehash_lock);
	if ((zone = zone_find_all_by_name(name)) == NULL) {
		mutex_exit(&zonehash_lock);
		return (NULL);
	}
	status = zone_status_get(zone);
	if (status < ZONE_IS_READY || status > ZONE_IS_DOWN) {
		/*
		 * For all practical purposes the zone doesn't exist.
		 */
		mutex_exit(&zonehash_lock);
		return (NULL);
	}
	zone_hold(zone);
	mutex_exit(&zonehash_lock);
	return (zone);
}

/*
 * Similar to zone_find_by_id(), using the path as a key.  For instance,
 * if there is a zone "foo" rooted at /foo/root, and the path argument
 * is "/foo/root/proc", it will return the held zone_t corresponding to
 * zone "foo".
 *
 * zone_find_by_path() always returns a non-NULL value, since at the
 * very least every path will be contained in the global zone.
 *
 * As with the other zone_find_by_*() functions, the caller is
 * responsible for zone_rele()ing the return value of this function.
 */
zone_t *
zone_find_by_path(const char *path)
{
	zone_t *zone;
	zone_t *zret = NULL;

	ASSERT(path != NULL && *path == '/');
	mutex_enter(&zonehash_lock);
	for (zone = list_head(&zone_active); zone != NULL;
	    zone = list_next(&zone_active, zone)) {
		if (ZONE_PATH_VISIBLE(path, zone))
			zret = zone;
	}
	ASSERT(zret != NULL);
	/*
	 * Unlike the other zone_find_by_*() variants, we really want to
	 * *always* report that a zone exists at a given path,
	 * regardless of what status it may have.
	 *
	 * The mount(2) path, for instance, needs to know that a zone
	 * will be coming up in the given path (or, that a zone is dying
	 * on the same path) so it can make the appropriate decision to
	 * fail.
	 *
	 * That is, we *don't* test "zone_status"
	 */
	zone_hold(zret);
	mutex_exit(&zonehash_lock);
	return (zret);
}

/*
 * Get the number of cpus visible to this zone.  The system-wide global
 * 'ncpus' is returned if pools are disabled, the caller is in the
 * global zone, or a NULL zone argument is passed in.
 */
int
zone_ncpus_get(zone_t *zone)
{
	int myncpus = zone == NULL ? 0 : zone->zone_ncpus;

	return (myncpus != 0 ? myncpus : ncpus);
}

/*
 * Get the number of online cpus visible to this zone.  The system-wide
 * global 'ncpus_online' is returned if pools are disabled, the caller
 * is in the global zone, or a NULL zone argument is passed in.
 */
int
zone_ncpus_online_get(zone_t *zone)
{
	int myncpus_online = zone == NULL ? 0 : zone->zone_ncpus_online;

	return (myncpus_online != 0 ? myncpus_online : ncpus_online);
}

/*
 * Return the pool to which the zone is currently bound.
 */
pool_t *
zone_pool_get(zone_t *zone)
{
	ASSERT(pool_lock_held());

	return (zone->zone_pool);
}

/*
 * Set the zone's pool pointer and update the zone's visibility to match
 * the resources in the new pool.
 */
void
zone_pool_set(zone_t *zone, pool_t *pool)
{
	ASSERT(pool_lock_held());
	ASSERT(MUTEX_HELD(&cpu_lock));

	zone->zone_pool = pool;
	zone_pset_set(zone, pool->pool_pset->pset_id);
}

/*
 * Return the cached value of the id of the processor set to which the
 * zone is currently bound.  The value will be ZONE_PS_INVAL if the pools
 * facility is disabled.
 */
psetid_t
zone_pset_get(zone_t *zone)
{
	ASSERT(MUTEX_HELD(&cpu_lock));

	return (zone->zone_psetid);
}

/*
 * Set the cached value of the id of the processor set to which the zone
 * is currently bound.  Also update the zone's visibility to match the
 * resources in the new processor set.
 */
void
zone_pset_set(zone_t *zone, psetid_t newpsetid)
{
	psetid_t oldpsetid;

	ASSERT(MUTEX_HELD(&cpu_lock));
	oldpsetid = zone_pset_get(zone);

	if (oldpsetid == newpsetid)
		return;
	/*
	 * Global zone sees all.
	 */
	if (zone != global_zone) {
		zone->zone_psetid = newpsetid;
		if (newpsetid != ZONE_PS_INVAL)
			pool_pset_visibility_add(newpsetid, zone);
		if (oldpsetid != ZONE_PS_INVAL)
			pool_pset_visibility_remove(oldpsetid, zone);
	}
	/*
	 * Disabling pools, so we should start using the global values
	 * for ncpus and ncpus_online.
	 */
	if (newpsetid == ZONE_PS_INVAL) {
		zone->zone_ncpus = 0;
		zone->zone_ncpus_online = 0;
	}
}

/*
 * Walk the list of active zones and issue the provided callback for
 * each of them.
 *
 * Caller must not be holding any locks that may be acquired under
 * zonehash_lock.  See comment at the beginning of the file for a list of
 * common locks and their interactions with zones.
 */
int
zone_walk(int (*cb)(zone_t *, void *), void *data)
{
	zone_t *zone;
	int ret = 0;
	zone_status_t status;

	mutex_enter(&zonehash_lock);
	for (zone = list_head(&zone_active); zone != NULL;
	    zone = list_next(&zone_active, zone)) {
		/*
		 * Skip zones that shouldn't be externally visible.
		 */
		status = zone_status_get(zone);
		if (status < ZONE_IS_READY || status > ZONE_IS_DOWN)
			continue;
		/*
		 * Bail immediately if any callback invocation returns a
		 * non-zero value.
		 */
		ret = (*cb)(zone, data);
		if (ret != 0)
			break;
	}
	mutex_exit(&zonehash_lock);
	return (ret);
}

static int
zone_set_root(zone_t *zone, const char *upath)
{
	vnode_t *vp;
	int trycount;
	int error = 0;
	char *path;
	struct pathname upn, pn;
	size_t pathlen;

	if ((error = pn_get((char *)upath, UIO_USERSPACE, &upn)) != 0)
		return (error);

	pn_alloc(&pn);

	/* prevent infinite loop */
	trycount = 10;
	for (;;) {
		if (--trycount <= 0) {
			error = ESTALE;
			goto out;
		}

		if ((error = lookuppn(&upn, &pn, FOLLOW, NULLVPP, &vp)) == 0) {
			/*
			 * VOP_ACCESS() may cover 'vp' with a new
			 * filesystem, if 'vp' is an autoFS vnode.
			 * Get the new 'vp' if so.
			 */
			if ((error =
			    VOP_ACCESS(vp, VEXEC, 0, CRED(), NULL)) == 0 &&
			    (!vn_ismntpt(vp) ||
			    (error = traverse(&vp)) == 0)) {
				pathlen = pn.pn_pathlen + 2;
				path = kmem_alloc(pathlen, KM_SLEEP);
				(void) strncpy(path, pn.pn_path,
				    pn.pn_pathlen + 1);
				path[pathlen - 2] = '/';
				path[pathlen - 1] = '\0';
				pn_free(&pn);
				pn_free(&upn);

				/* Success! */
				break;
			}
			VN_RELE(vp);
		}
		if (error != ESTALE)
			goto out;
	}

	ASSERT(error == 0);
	zone->zone_rootvp = vp;		/* we hold a reference to vp */
	zone->zone_rootpath = path;
	zone->zone_rootpathlen = pathlen;
	if (pathlen > 5 && strcmp(path + pathlen - 5, "/lu/") == 0)
		zone->zone_flags |= ZF_IS_SCRATCH;
	return (0);

out:
	pn_free(&pn);
	pn_free(&upn);
	return (error);
}

#define	isalnum(c)	(((c) >= '0' && (c) <= '9') || \
			((c) >= 'a' && (c) <= 'z') || \
			((c) >= 'A' && (c) <= 'Z'))

static int
zone_set_name(zone_t *zone, const char *uname)
{
	char *kname = kmem_zalloc(ZONENAME_MAX, KM_SLEEP);
	size_t len;
	int i, err;

	if ((err = copyinstr(uname, kname, ZONENAME_MAX, &len)) != 0) {
		kmem_free(kname, ZONENAME_MAX);
		return (err);	/* EFAULT or ENAMETOOLONG */
	}

	/* must be less than ZONENAME_MAX */
	if (len == ZONENAME_MAX && kname[ZONENAME_MAX - 1] != '\0') {
		kmem_free(kname, ZONENAME_MAX);
		return (EINVAL);
	}

	/*
	 * Name must start with an alphanumeric and must contain only
	 * alphanumerics, '-', '_' and '.'.
	 */
	if (!isalnum(kname[0])) {
		kmem_free(kname, ZONENAME_MAX);
		return (EINVAL);
	}
	for (i = 1; i < len - 1; i++) {
		if (!isalnum(kname[i]) && kname[i] != '-' && kname[i] != '_' &&
		    kname[i] != '.') {
			kmem_free(kname, ZONENAME_MAX);
			return (EINVAL);
		}
	}

	zone->zone_name = kname;
	return (0);
}

/*
 * Gets the 32-bit hostid of the specified zone as an unsigned int.  If 'zonep'
 * is NULL or it points to a zone with no hostid emulation, then the machine's
 * hostid (i.e., the global zone's hostid) is returned.  This function returns
 * zero if neither the zone nor the host machine (global zone) have hostids.  It
 * returns HW_INVALID_HOSTID if the function attempts to return the machine's
 * hostid and the machine's hostid is invalid.
 */
uint32_t
zone_get_hostid(zone_t *zonep)
{
	unsigned long machine_hostid;

	if (zonep == NULL || zonep->zone_hostid == HW_INVALID_HOSTID) {
		if (ddi_strtoul(hw_serial, NULL, 10, &machine_hostid) != 0)
			return (HW_INVALID_HOSTID);
		return ((uint32_t)machine_hostid);
	}
	return (zonep->zone_hostid);
}

/*
 * Similar to thread_create(), but makes sure the thread is in the appropriate
 * zone's zsched process (curproc->p_zone->zone_zsched) before returning.
 */
/*ARGSUSED*/
kthread_t *
zthread_create(
    caddr_t stk,
    size_t stksize,
    void (*proc)(),
    void *arg,
    size_t len,
    pri_t pri)
{
	kthread_t *t;
	zone_t *zone = curproc->p_zone;
	proc_t *pp = zone->zone_zsched;
	kproject_t *proj;

	zone_hold(zone);	/* Reference to be dropped when thread exits */

	/*
	 * No-one should be trying to create threads if the zone is shutting
	 * down and there aren't any kernel threads around.  See comment
	 * in zthread_exit().
	 */
	ASSERT(!(zone->zone_kthreads == NULL &&
	    zone_status_get(zone) >= ZONE_IS_EMPTY));
	/*
	 * Create a thread, but don't let it run until we've finished setting
	 * things up.
	 */
	t = thread_create(stk, stksize, proc, arg, len, pp, TS_STOPPED, pri);
	ASSERT(t->t_forw == NULL);
	mutex_enter(&zone->zone_lock);
	if (zone->zone_kthreads == NULL) {
		t->t_forw = t->t_back = t;
	} else {
		kthread_t *tx = zone->zone_kthreads;

		t->t_forw = tx;
		t->t_back = tx->t_back;
		tx->t_back->t_forw = t;
		tx->t_back = t;
	}
	zone->zone_kthreads = t;
	mutex_exit(&zone->zone_lock);

	mutex_enter(&pp->p_lock);
	t->t_proc_flag |= TP_ZTHREAD;
	project_rele(t->t_proj);
	proj = project_hold(pp->p_task->tk_proj);

	/*
	 * Setup complete, let it run.
	 */
	thread_lock(t);
	t->t_proj = proj;
	t->t_schedflag |= TS_ALLSTART;
	setrun_locked(t);
	thread_unlock(t);

	mutex_exit(&pp->p_lock);

	return (t);
}

/*
 * Similar to thread_exit().  Must be called by threads created via
 * zthread_create().
 */
void
zthread_exit(void)
{
	kthread_t *t = curthread;
	proc_t *pp = curproc;
	zone_t *zone = pp->p_zone;

	mutex_enter(&zone->zone_lock);

	/*
	 * Reparent to p0
	 */
	mutex_enter(&pp->p_lock);
	kpreempt_disable();
	t->t_proc_flag &= ~TP_ZTHREAD;
	t->t_procp = &p0;
	hat_thread_exit(t);
	kpreempt_enable();
	mutex_exit(&pp->p_lock);

	if (t->t_back == t) {
		ASSERT(t->t_forw == t);
		/*
		 * If the zone is empty, once the thread count
		 * goes to zero no further kernel threads can be
		 * created.  This is because if the creator is a process
		 * in the zone, then it must have exited before the zone
		 * state could be set to ZONE_IS_EMPTY.
		 * Otherwise, if the creator is a kernel thread in the
		 * zone, the thread count is non-zero.
		 *
		 * This really means that non-zone kernel threads should
		 * not create zone kernel threads.
		 */
		zone->zone_kthreads = NULL;
		if (zone_status_get(zone) == ZONE_IS_EMPTY) {
			zone_status_set(zone, ZONE_IS_DOWN);
			/*
			 * Remove any CPU caps on this zone.
			 */
			cpucaps_zone_remove(zone);
		}
	} else {
		t->t_forw->t_back = t->t_back;
		t->t_back->t_forw = t->t_forw;
		if (zone->zone_kthreads == t)
			zone->zone_kthreads = t->t_forw;
	}
	mutex_exit(&zone->zone_lock);
	zone_rele(zone);
	thread_exit();
	/* NOTREACHED */
}

static void
zone_chdir(vnode_t *vp, vnode_t **vpp, proc_t *pp)
{
	vnode_t *oldvp;

	/* we're going to hold a reference here to the directory */
	VN_HOLD(vp);

	mutex_enter(&pp->p_lock);
	oldvp = *vpp;
	*vpp = vp;
	mutex_exit(&pp->p_lock);
	if (oldvp != NULL)
		VN_RELE(oldvp);
}

/*
 * Convert an rctl value represented by an nvlist_t into an rctl_val_t.
 */
static int
nvlist2rctlval(nvlist_t *nvl, rctl_val_t *rv)
{
	nvpair_t *nvp = NULL;
	boolean_t priv_set = B_FALSE;
	boolean_t limit_set = B_FALSE;
	boolean_t action_set = B_FALSE;

	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		const char *name;
		uint64_t ui64;

		name = nvpair_name(nvp);
		if (nvpair_type(nvp) != DATA_TYPE_UINT64)
			return (EINVAL);
		(void) nvpair_value_uint64(nvp, &ui64);
		if (strcmp(name, "privilege") == 0) {
			/*
			 * Currently only privileged values are allowed, but
			 * this may change in the future.
			 */
			if (ui64 != RCPRIV_PRIVILEGED)
				return (EINVAL);
			rv->rcv_privilege = ui64;
			priv_set = B_TRUE;
		} else if (strcmp(name, "limit") == 0) {
			rv->rcv_value = ui64;
			limit_set = B_TRUE;
		} else if (strcmp(name, "action") == 0) {
			if (ui64 != RCTL_LOCAL_NOACTION &&
			    ui64 != RCTL_LOCAL_DENY)
				return (EINVAL);
			rv->rcv_flagaction = ui64;
			action_set = B_TRUE;
		} else {
			return (EINVAL);
		}
	}

	if (!(priv_set && limit_set && action_set))
		return (EINVAL);
	rv->rcv_action_signal = 0;
	rv->rcv_action_recipient = NULL;
	rv->rcv_action_recip_pid = -1;
	rv->rcv_firing_time = 0;

	return (0);
}

/*
 * Non-global zone version of start_init.
 */
void
zone_start_init(void)
{
	proc_t *p = ttoproc(curthread);
	zone_t *z = p->p_zone;

	ASSERT(!INGLOBALZONE(curproc));

	/*
	 * For all purposes (ZONE_ATTR_INITPID and restart_init),
	 * storing just the pid of init is sufficient.
	 */
	z->zone_proc_initpid = p->p_pid;

	/*
	 * We maintain zone_boot_err so that we can return the cause of the
	 * failure back to the caller of the zone_boot syscall.
	 */
	p->p_zone->zone_boot_err = start_init_common();

	/*
	 * getproc() in the fork() path will notice if the global_zone
	 * is going away and fail: so we don't need to check the
	 * global_zone's status and explicitly fail here.
	 */
	mutex_enter(&z->zone_lock);
	if (z->zone_boot_err != 0) {
		/*
		 * Make sure we are still in the booting state-- we could have
		 * raced and already be shutting down, or even further along.
		 */
		if (zone_status_get(z) == ZONE_IS_BOOTING) {
			zone_status_set(z, ZONE_IS_SHUTTING_DOWN);
		}
		mutex_exit(&z->zone_lock);
		/* It's gone bad, dispose of the process */
		if (proc_exit(CLD_EXITED, z->zone_boot_err) != 0) {
			mutex_enter(&p->p_lock);
			ASSERT(p->p_flag & SEXITLWPS);
			lwp_exit();
		}
	} else {
		if (zone_status_get(z) == ZONE_IS_BOOTING)
			zone_status_set(z, ZONE_IS_RUNNING);
		mutex_exit(&z->zone_lock);
		/* cause the process to return to userland. */
		lwp_rtt();
	}
	/*NOTREACHED*/
}

struct zsched_arg {
	zone_t *zone;
	nvlist_t *nvlist;
	vfs_t *rootvfsp;
};

/*
 * Convenience routine to enter a SSYS proc in a zone.
 * In future newproc(), lwp_create() and other primitives
 * should be made zone aware instead. rctl limits are
 * currently not enforced on system processes. Callers
 * of this routine are expected to enforce adequate
 * limits on their thread counts.
 */
void
zproc_enter(zone_t *zone)
{
	task_t *tk;
	task_t *oldtk = NULL;
	cred_t *cr, *oldcred;
	kproject_t *pj;
	proc_t *pp = curproc;
	int zsched = (zone->zone_zsched == pp);
	proc_t *ppp = zsched ? proc_init : zone->zone_zsched;

	mutex_enter(&pp->p_lock);
	pp->p_zone = zone;
	LWP_AC_LOCK(curthread);
	curthread->t_zone = zone;
	LWP_AC_UNLOCK(curthread);
	mutex_exit(&pp->p_lock);

	/*
	 * Disassociate process from its 'parent'; In the case of zsched
	 * set parent to init (pid 1), while other processes set zsched
	 * as parent and change other values as needed.
	 */
	sess_create();

	mutex_enter(&pidlock);
	proc_detach(pp);

	if (zsched)
		pp->p_flag |= SZONETOP;

	pp->p_ancpid = pp->p_ppid = ppp->p_pid;
	pp->p_parent = ppp;
	pp->p_psibling = NULL;
	if (ppp->p_child)
		ppp->p_child->p_psibling = pp;
	pp->p_sibling = ppp->p_child;
	ppp->p_child = pp;

	/* Decrement what newproc() incremented. */
	upcount_dec(crgetruid(CRED()), GLOBAL_ZONEID);

	/*
	 * Our credentials are about to become kcred-like, so we don't care
	 * about the caller's ruid.
	 */
	upcount_inc(crgetruid(kcred), zone->zone_id);
	mutex_exit(&pidlock);

	/*
	 * getting out of global zone, so decrement lwp and process counts
	 */
	pj = pp->p_task->tk_proj;
	mutex_enter(&global_zone->zone_nlwps_lock);
	pj->kpj_nlwps -= pp->p_lwpcnt;
	global_zone->zone_nlwps -= pp->p_lwpcnt;
	pj->kpj_nprocs--;
	global_zone->zone_nprocs--;
	mutex_exit(&global_zone->zone_nlwps_lock);

	/*
	 * Decrement locked memory counts on old zone and project.
	 */
	mutex_enter(&global_zone->zone_mem_lock);
	global_zone->zone_locked_mem -= pp->p_locked_mem;
	pj->kpj_data.kpd_locked_mem -= pp->p_locked_mem;
	mutex_exit(&global_zone->zone_mem_lock);

	if (zsched) {
		/*
		 * Create and join a new task in project '0' of this zone.
		 *
		 * We don't need to call holdlwps() since we know we're
		 * the only lwp in this process.
		 *
		 * task_join() returns with p_lock held.
		 */
		tk = task_create(0, zone);
		mutex_enter(&cpu_lock);
		oldtk = task_join(tk, 0);

		pj = pp->p_task->tk_proj;

		mutex_exit(&pp->p_lock);
		mutex_exit(&cpu_lock);
	} else {
		/*
		 * newproc attaches the proc to task0p
		 * Change it to the zone's task.
		 */
		mutex_enter(&pidlock);
		mutex_enter(&pp->p_lock);
		task_change(ppp->p_task, pp);
		pj = pp->p_task->tk_proj;
		mutex_exit(&pp->p_lock);
		mutex_exit(&pidlock);
	}

	mutex_enter(&zone->zone_mem_lock);
	zone->zone_locked_mem += pp->p_locked_mem;
	pj->kpj_data.kpd_locked_mem += pp->p_locked_mem;
	mutex_exit(&zone->zone_mem_lock);

	/*
	 * add lwp and process counts to zsched's zone, and increment
	 * project's task and process count due to the task created in
	 * the above task_create.
	 */
	mutex_enter(&zone->zone_nlwps_lock);
	pj->kpj_nlwps += pp->p_lwpcnt;
	pj->kpj_ntasks += 1;
	zone->zone_nlwps += pp->p_lwpcnt;
	pj->kpj_nprocs++;
	zone->zone_nprocs++;
	mutex_exit(&zone->zone_nlwps_lock);

	if (oldtk)
		task_rele(oldtk);

	if (!zsched && (pool_state == POOL_ENABLED)) {
		pool_lock();
		if (pool_do_bind(zone_pool_get(zone), P_PID, P_MYID,
		    POOL_BIND_ALL) != 0) {
			cmn_err(CE_WARN, "Couldn't bind process for"
			    " newproc \"%p\" to pool %p\n",
			    (void *)pp, (void *)zone_pool_get(zone));
		}
		pool_unlock();
	}

	/*
	 * The process was created by a process in the global zone, hence the
	 * credentials are wrong.  We might as well have kcred-ish credentials.
	 */
	cr = zone->zone_kcred;
	crhold(cr);
	mutex_enter(&pp->p_crlock);
	oldcred = pp->p_cred;
	pp->p_cred = cr;
	mutex_exit(&pp->p_crlock);
	crfree(oldcred);

	/*
	 * Hold credentials again (for thread)
	 */
	crhold(cr);

	/*
	 * p_lwpcnt can't change since this is a kernel process.
	 */
	crset(pp, cr);

	/*
	 * Chroot
	 */
	zone_chdir(zone->zone_rootvp, &PTOU(pp)->u_cdir, pp);
	zone_chdir(zone->zone_rootvp, &PTOU(pp)->u_rdir, pp);

}

/*
 * Per-zone "sched" workalike.  The similarity to "sched" doesn't have
 * anything to do with scheduling, but rather with the fact that
 * per-zone kernel threads are parented to zsched, just like regular
 * kernel threads are parented to sched (p0).
 *
 * zsched is also responsible for launching init for the zone.
 */
static void
zsched(void *arg)
{
	struct zsched_arg *za = arg;
	proc_t *pp = curproc;
	zone_t *zone = za->zone;
	rctl_set_t *set;
	rctl_alloc_gp_t *gp;
	contract_t *ct = NULL;
	rctl_entity_p_t e;

	nvlist_t *nvl = za->nvlist;
	nvpair_t *nvp = NULL;

	bcopy("zsched", PTOU(pp)->u_psargs, sizeof ("zsched"));
	bcopy("zsched", PTOU(pp)->u_comm, sizeof ("zsched"));
	PTOU(pp)->u_argc = 0;
	PTOU(pp)->u_argv = NULL;
	PTOU(pp)->u_envp = NULL;
	closeall(P_FINFO(pp));

	/*
	 * We are this zone's "zsched" process.  As the zone isn't generally
	 * visible yet we don't need to grab any locks before initializing its
	 * zone_proc pointer.
	 */
	zone_hold(zone);  /* this hold is released by zone_destroy() */
	zone->zone_zsched = pp;

	/* Transition pp into the zone */
	zproc_enter(zone);

	/*
	 * Initialize zone's rctl set.
	 */
	set = rctl_set_create();
	gp = rctl_set_init_prealloc(RCENTITY_ZONE);
	mutex_enter(&pp->p_lock);
	e.rcep_p.zone = zone;
	e.rcep_t = RCENTITY_ZONE;
	zone->zone_rctls = rctl_set_init(RCENTITY_ZONE, pp, &e, set, gp);
	mutex_exit(&pp->p_lock);
	rctl_prealloc_destroy(gp);

	/*
	 * Apply the rctls passed in to zone_create().  This is basically a list
	 * assignment: all of the old values are removed and the new ones
	 * inserted.  That is, if an empty list is passed in, all values are
	 * removed.
	 */
	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		rctl_dict_entry_t *rde;
		rctl_hndl_t hndl;
		char *name;
		nvlist_t **nvlarray;
		uint_t i, nelem;
		int error;	/* For ASSERT()s */

		name = nvpair_name(nvp);
		hndl = rctl_hndl_lookup(name);
		ASSERT(hndl != -1);
		rde = rctl_dict_lookup_hndl(hndl);
		ASSERT(rde != NULL);

		for (; /* ever */; ) {
			rctl_val_t oval;

			mutex_enter(&pp->p_lock);
			error = rctl_local_get(hndl, NULL, &oval, pp);
			mutex_exit(&pp->p_lock);
			ASSERT(error == 0);	/* Can't fail for RCTL_FIRST */
			ASSERT(oval.rcv_privilege != RCPRIV_BASIC);
			if (oval.rcv_privilege == RCPRIV_SYSTEM)
				break;
			mutex_enter(&pp->p_lock);
			error = rctl_local_delete(hndl, &oval, pp);
			mutex_exit(&pp->p_lock);
			ASSERT(error == 0);
		}
		error = nvpair_value_nvlist_array(nvp, &nvlarray, &nelem);
		ASSERT(error == 0);
		for (i = 0; i < nelem; i++) {
			rctl_val_t *nvalp;

			nvalp = kmem_cache_alloc(rctl_val_cache, KM_SLEEP);
			error = nvlist2rctlval(nvlarray[i], nvalp);
			ASSERT(error == 0);
			/*
			 * rctl_local_insert can fail if the value being
			 * inserted is a duplicate; this is OK.
			 */
			mutex_enter(&pp->p_lock);
			if (rctl_local_insert(hndl, nvalp, pp) != 0)
				kmem_cache_free(rctl_val_cache, nvalp);
			mutex_exit(&pp->p_lock);
		}
	}
	/*
	 * Tell the world that we're done setting up.
	 *
	 * At this point we want to set the zone status to ZONE_IS_INITIALIZED
	 * and atomically set the zone's processor set visibility.  Once
	 * we drop pool_lock() this zone will automatically get updated
	 * to reflect any future changes to the pools configuration.
	 *
	 * Note that after we drop the locks below (zonehash_lock in
	 * particular) other operations such as a zone_getattr call can
	 * now proceed and observe the zone. That is the reason for doing a
	 * state transition to the INITIALIZED state.
	 */
	pool_lock();
	mutex_enter(&cpu_lock);
	mutex_enter(&zonehash_lock);
	zone_uniqid(zone);
	zone_zsd_configure(zone);
	if (pool_state == POOL_ENABLED)
		zone_pset_set(zone, pool_default->pool_pset->pset_id);
	mutex_enter(&zone->zone_lock);
	ASSERT(zone_status_get(zone) == ZONE_IS_UNINITIALIZED);
	zone_status_set(zone, ZONE_IS_INITIALIZED);
	mutex_exit(&zone->zone_lock);
	mutex_exit(&zonehash_lock);
	mutex_exit(&cpu_lock);
	pool_unlock();

	/* If the zone root is a mount point, transfer the vfs to the zone */
	if (za->rootvfsp != NULL)
		vfs_zone_change(za->rootvfsp, zone);

	/* Now call the create callback for this key */
	zsd_apply_all_keys(zsd_apply_create, zone);

	/* The callbacks are complete. Mark ZONE_IS_READY */
	mutex_enter(&zone->zone_lock);
	ASSERT(zone_status_get(zone) == ZONE_IS_INITIALIZED);
	zone_status_set(zone, ZONE_IS_READY);
	mutex_exit(&zone->zone_lock);

	/*
	 * Once we see the zone transition to the ZONE_IS_BOOTING state,
	 * we launch init, and set the state to running.
	 */
	zone_status_wait_cpr(zone, ZONE_IS_BOOTING, "zsched");

	if (zone_status_get(zone) == ZONE_IS_BOOTING) {
		id_t cid;

		/*
		 * Ok, this is a little complicated.  We need to grab the
		 * zone's pool's scheduling class ID; note that by now, we
		 * are already bound to a pool if we need to be (zoneadmd
		 * will have done that to us while we're in the READY
		 * state).  *But* the scheduling class for the zone's 'init'
		 * must be explicitly passed to newproc, which doesn't
		 * respect pool bindings.
		 *
		 * We hold the pool_lock across the call to newproc() to
		 * close the obvious race: the pool's scheduling class
		 * could change before we manage to create the LWP with
		 * classid 'cid'.
		 */
		pool_lock();
		if (zone->zone_defaultcid > 0)
			cid = zone->zone_defaultcid;
		else
			cid = pool_get_class(zone->zone_pool);
		if (cid == -1)
			cid = defaultcid;

		/*
		 * If this fails, zone_boot will ultimately fail.  The
		 * state of the zone will be set to SHUTTING_DOWN-- userland
		 * will have to tear down the zone, and fail, or try again.
		 */
		if ((zone->zone_boot_err = newproc(zone_start_init, NULL, cid,
		    minclsyspri - 1, &ct, 0)) != 0) {
			mutex_enter(&zone->zone_lock);
			zone_status_set(zone, ZONE_IS_SHUTTING_DOWN);
			mutex_exit(&zone->zone_lock);
		}
		pool_unlock();
	}

	/*
	 * Eschatological wait.  This is what we spend most of our life doing.
	 */
	zone_status_wait_cpr(zone, ZONE_IS_DYING, "zsched");

	if (ct)
		/*
		 * At this point the process contract should be empty.
		 * (Though if it isn't, it's not the end of the world.)
		 */
		VERIFY(contract_abandon(ct, curproc, B_TRUE) == 0);

	/*
	 * Allow kcred to be freed when all referring processes
	 * (including this one) go away.  We can't just do this in
	 * zone_free because we need to wait for the zone_cred_ref to
	 * drop to 0 before calling zone_free, and the existence of
	 * zone_kcred will prevent that.  Thus, we call crfree here to
	 * balance the crdup in zone_create.  The crhold calls earlier
	 * in zsched will be dropped when the thread and process exit.
	 */
	crfree(zone->zone_kcred);
	zone->zone_kcred = NULL;

	exit(CLD_EXITED, 0);
}

static uint_t
devann_hash(void *hash_data, mod_hash_key_t key)
{
	uint64_t val = (uint64_t)(uintptr_t)key;
	uint64_t hash = (val >> 32) ^ (val ^ (val >> 6));

	return (mod_hash_byid(hash_data,
	    (mod_hash_key_t)(uintptr_t)hash));
}

static int
devann_cmp(mod_hash_key_t key1, mod_hash_key_t key2)
{
	return ((dev_t)key1 - (dev_t)key2);
}

static void
devann_hash_val_dtor(mod_hash_val_t val)
{
	nvlist_free((nvlist_t *)val);
}

static nvlist_t *
devann_find_devt(zone_t *zone, dev_t devt)
{
	mod_hash_val_t val;

	ASSERT(MUTEX_HELD(&zone->zone_devann_lock));

	if (zone->zone_devann_hash == NULL)
		return (NULL);

	if (mod_hash_find(zone->zone_devann_hash,
	    (mod_hash_key_t)devt, &val) != 0)
		return (NULL);

	return ((nvlist_t *)val);
}

/*
 * Returns true if any device annotation for the given dev_t is present.
 */
int
zone_devann_present(zone_t *zone, dev_t devt)
{
	nvlist_t *nvl;
	mutex_enter(&zone->zone_devann_lock);
	nvl = devann_find_devt(zone, devt);
	mutex_exit(&zone->zone_devann_lock);
	return (nvl != NULL);
}

/*
 * Returns the value of the requested annotation on the requested dev_t,
 * or NULL if no such annotation is present.  Caller must strfree() the
 * returned string.
 */
char *
zone_devann_find(zone_t *zone, dev_t devt, const char *name)
{
	nvlist_t *nvl = NULL;
	nvpair_t *nvp = NULL;
	char *value = NULL;

	mutex_enter(&zone->zone_devann_lock);

	if ((nvl = devann_find_devt(zone, devt)) == NULL)
		goto out;

	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {

		if (strcmp(name, nvpair_name(nvp)) == 0) {
			(void) nvpair_value_string(nvp, &value);
			value = strdup(value);
			break;
		}
	}

out:
	mutex_exit(&zone->zone_devann_lock);
	return (value);
}

/*
 * Insert an annotation for a device.  Returns 0 on success, or EEXIST
 * if the annotation already exists.
 */
int
zone_devann_insert(zone_t *zone, dev_t devt,
    const char *name, const char *value)
{
	nvlist_t *nvl = NULL;
	nvpair_t *nvp = NULL;
	int ret = 0;

	ASSERT(zone->zone_devann_hash != NULL);

	mutex_enter(&zone->zone_devann_lock);

	if ((nvl = devann_find_devt(zone, devt)) == NULL) {
		(void) nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP);

		(void) mod_hash_insert(zone->zone_devann_hash,
		    (mod_hash_key_t)devt, (mod_hash_val_t)nvl);
	}

	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {

		if (strcmp(name, nvpair_name(nvp)) == 0) {
			ret = EEXIST;
			goto out;
		}
	}

	(void) nvlist_add_string(nvl, name, value);
	ret = 0;

out:
	mutex_exit(&zone->zone_devann_lock);
	return (ret);
}

static int
devann_clear(zone_t *zone, void *data)
{
	dev_t devt = (dev_t)data;

	if (zone->zone_devann_hash == NULL)
		return (0);

	mutex_enter(&zone->zone_devann_lock);
	(void) mod_hash_destroy(zone->zone_devann_hash, (mod_hash_key_t)devt);
	mutex_exit(&zone->zone_devann_lock);
	return (0);
}

/*
 * Clear all annotations for the given dev_t across all zones.
 */
void
zone_devann_clear(dev_t devt)
{
	(void) zone_walk(devann_clear, (void *)devt);
}

/*
 * Helper function to determine if there are any submounts of the
 * provided path.  Used to make sure the zone doesn't "inherit" any
 * mounts from before it is created.
 */
static uint_t
zone_mount_count(const char *rootpath)
{
	vfs_t *vfsp;
	uint_t count = 0;
	size_t rootpathlen = strlen(rootpath);

	/*
	 * Holding zonehash_lock prevents race conditions with
	 * vfs_list_add()/vfs_list_remove() since we serialize with
	 * zone_find_by_path().
	 */
	ASSERT(MUTEX_HELD(&zonehash_lock));
	/*
	 * The rootpath must end with a '/'
	 */
	ASSERT(rootpath[rootpathlen - 1] == '/');

	/*
	 * This intentionally does not count the rootpath itself if that
	 * happens to be a mount point.
	 */
	vfs_list_read_lock();
	vfsp = rootvfs;
	do {
		if (strncmp(rootpath, refstr_value(vfsp->vfs_mntpt),
		    rootpathlen) == 0)
			count++;
		vfsp = vfsp->vfs_next;
	} while (vfsp != rootvfs);
	vfs_list_unlock();
	return (count);
}

/*
 * Helper function to make sure that a zone created on 'rootpath'
 * wouldn't end up containing other zones' rootpaths.
 */
static boolean_t
zone_is_nested(const char *rootpath)
{
	zone_t *zone;
	size_t rootpathlen = strlen(rootpath);
	size_t len;

	ASSERT(MUTEX_HELD(&zonehash_lock));

	/*
	 * zone_set_root() appended '/' and '\0' at the end of rootpath
	 */
	if ((rootpathlen <= 3) && (rootpath[0] == '/') &&
	    (rootpath[1] == '/') && (rootpath[2] == '\0'))
		return (B_TRUE);

	for (zone = list_head(&zone_active); zone != NULL;
	    zone = list_next(&zone_active, zone)) {
		if (zone == global_zone)
			continue;
		len = strlen(zone->zone_rootpath);
		if (strncmp(rootpath, zone->zone_rootpath,
		    MIN(rootpathlen, len)) == 0)
			return (B_TRUE);
	}
	return (B_FALSE);
}

static int
zone_set_privset(zone_t *zone, const priv_set_t *zone_privs,
    size_t zone_privssz)
{
	priv_set_t *privs;

	if (zone_privssz < sizeof (priv_set_t))
		return (ENOMEM);

	privs = kmem_alloc(sizeof (priv_set_t), KM_SLEEP);

	if (copyin(zone_privs, privs, sizeof (priv_set_t))) {
		kmem_free(privs, sizeof (priv_set_t));
		return (EFAULT);
	}

	zone->zone_privset = privs;
	return (0);
}

/*
 * We make creative use of nvlists to pass in rctls from userland.  The list is
 * a list of the following structures:
 *
 * (name = rctl_name, value = nvpair_list_array)
 *
 * Where each element of the nvpair_list_array is of the form:
 *
 * [(name = "privilege", value = RCPRIV_PRIVILEGED),
 * 	(name = "limit", value = uint64_t),
 * 	(name = "action", value = (RCTL_LOCAL_NOACTION || RCTL_LOCAL_DENY))]
 */
static int
parse_rctls(caddr_t ubuf, size_t buflen, nvlist_t **nvlp)
{
	nvpair_t *nvp = NULL;
	nvlist_t *nvl = NULL;
	char *kbuf;
	int error;
	rctl_val_t rv;

	*nvlp = NULL;

	if (buflen == 0)
		return (0);

	if ((kbuf = kmem_alloc(buflen, KM_NOSLEEP)) == NULL)
		return (ENOMEM);
	if (copyin(ubuf, kbuf, buflen)) {
		error = EFAULT;
		goto out;
	}
	if (nvlist_unpack(kbuf, buflen, &nvl, KM_SLEEP) != 0) {
		/*
		 * nvl may have been allocated/free'd, but the value set to
		 * non-NULL, so we reset it here.
		 */
		nvl = NULL;
		error = EINVAL;
		goto out;
	}
	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		rctl_dict_entry_t *rde;
		rctl_hndl_t hndl;
		nvlist_t **nvlarray;
		uint_t i, nelem;
		char *name;

		error = EINVAL;
		name = nvpair_name(nvp);
		if (strncmp(nvpair_name(nvp), "zone.", sizeof ("zone.") - 1)
		    != 0 || nvpair_type(nvp) != DATA_TYPE_NVLIST_ARRAY) {
			goto out;
		}
		if ((hndl = rctl_hndl_lookup(name)) == -1) {
			goto out;
		}
		rde = rctl_dict_lookup_hndl(hndl);
		error = nvpair_value_nvlist_array(nvp, &nvlarray, &nelem);
		ASSERT(error == 0);
		for (i = 0; i < nelem; i++) {
			if (error = nvlist2rctlval(nvlarray[i], &rv))
				goto out;
		}
		if (rctl_invalid_value(rde, &rv)) {
			error = EINVAL;
			goto out;
		}
	}
	error = 0;
	*nvlp = nvl;
out:
	kmem_free(kbuf, buflen);
	if (error && nvl != NULL)
		nvlist_free(nvl);
	return (error);
}

int
zone_create_error(int er_error, int er_ext, int *er_out) {
	if (er_out != NULL) {
		if (copyout(&er_ext, er_out, sizeof (int))) {
			return (set_errno(EFAULT));
		}
	}
	return (set_errno(er_error));
}

static int
zone_set_label(zone_t *zone, const bslabel_t *lab, uint32_t doi)
{
	ts_label_t *tsl;
	bslabel_t blab;

	/* Get label from user */
	if (copyin(lab, &blab, sizeof (blab)) != 0)
		return (EFAULT);
	tsl = labelalloc(&blab, doi, KM_NOSLEEP);
	if (tsl == NULL)
		return (ENOMEM);

	zone->zone_slabel = tsl;
	return (0);
}

/*
 * Parses a packed nvlist of ZFS datasets into a per-zone dictionary.  For
 * more details, including nvlist structure, see the comment above declaration
 * of zone_dataset_t in <sys/zone.h>.
 */
static int
parse_zfs(zone_t *zone, caddr_t ubuf, size_t buflen)
{
	char *kbuf;
	zone_dataset_t *zd = NULL;
	nvpair_t *nvp = NULL;
	nvlist_t *nvl = NULL;
	int ret = EINVAL;
	ddi_modhandle_t zfs_mod;
	/*
	 * pool_nc_t needs to be compatible with pool_namecheck() in
	 * usr/src/common/zfs/zfs_namecheck.c.  Because pool_namecheck() is in
	 * the fs/zfs module (not in genunix), it needs to be dynamically
	 * loaded to be used.  If it can't be loaded, there is very little
	 * reason that parse_zfs() would be called with buflen != 0.
	 */
	typedef int (*pool_nc_t)(const char *, int *, char *);
	pool_nc_t pool_nc;

	if (ubuf == NULL || buflen == 0)
		return (0);

	if ((kbuf = kmem_alloc(buflen, KM_NOSLEEP)) == NULL)
		return (ENOMEM);

	if (copyin(ubuf, kbuf, buflen) != 0) {
		kmem_free(kbuf, buflen);
		return (EFAULT);
	}
	if (nvlist_unpack(kbuf, buflen, &nvl, KM_SLEEP) != 0) {
		kmem_free(kbuf, buflen);
		return (EINVAL);
	}

	/* Load pool_namecheck() from fs/zfs */
	if ((zfs_mod = ddi_modopen("fs/zfs", KRTLD_MODE_FIRST, &ret)) == NULL) {
		cmn_err(CE_WARN, "could not find fs/zfs kernel module");
		return (ret);
	}
	if ((pool_nc = (pool_nc_t)ddi_modsym(zfs_mod, "pool_namecheck",
	    &ret)) == NULL) {
		cmn_err(CE_WARN, "could not find pool_namecheck in fs/zfs");
		goto out;
	}

	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		char *alias, *dataset;
		nvlist_t *dsnvlp;	/* for a dataset */
		int cfgsrc;

		/* Get the nvlist for a dataset */
		if (nvpair_value_nvlist(nvp, &dsnvlp) != 0)
			goto out;
		/*
		 * The name of the nvlist is the dataset name.  The value
		 * is an nvlist of other properties.
		 */
		dataset = nvpair_name(nvp);

		if (nvlist_lookup_string(dsnvlp, ZONE_DS_ALIAS_STR,
		    &alias) != 0)
			goto out;
		if (nvlist_lookup_int32(dsnvlp, ZONE_DS_CFGSRC_STR,
		    &cfgsrc) != 0)
			goto out;

		/*
		 * Sanity checks.
		 */
		if (!ZONE_DS_CFG_VALID(cfgsrc))
			goto out;

		if (pool_nc(alias, NULL, NULL) != 0) {
			cmn_err(CE_WARN, "invalid dataset alias '%s' in zone "
			    "%s", alias, zone->zone_name);
			ret = EINVAL;
			goto out;
		}

		/*
		 * Sanity checks complete
		 */
		zd = kmem_alloc(sizeof (zone_dataset_t), KM_SLEEP);
		zd->zd_dataset = strdup(dataset);
		zd->zd_alias = strdup(alias);
		zd->zd_cfgsrc = cfgsrc;

		list_insert_head(&zone->zone_datasets, zd);
	}
	ret = 0;

out:
	if (zfs_mod != NULL)
		(void) ddi_modclose(zfs_mod);
	kmem_free(kbuf, buflen);
	nvlist_free(nvl);
	return (ret);
}

/*
 * System call to create/initialize a new zone named 'zone_name', rooted
 * at 'zone_root', with a zone-wide privilege limit set of 'zone_privs',
 * and initialized with the zone-wide rctls described in 'rctlbuf', and
 * with labeling set by 'match', 'doi', and 'label'.
 *
 * If extended error is non-null, we may use it to return more detailed
 * error information.
 */
static zoneid_t
zone_create(const char *zone_name, const char *zone_root,
    const priv_set_t *zone_privs, size_t zone_privssz,
    caddr_t rctlbuf, size_t rctlbufsz,
    caddr_t zfsbuf, size_t zfsbufsz,
    int *extended_error, int match, uint32_t doi, const bslabel_t *label,
    int flags)
{
	struct zsched_arg zarg;
	nvlist_t *rctls = NULL;
	proc_t *pp = curproc;
	zone_t *zone, *ztmp;
	zoneid_t zoneid;
	int error;
	int error2 = 0;
	char *str;
	cred_t *zkcr;
	boolean_t insert_label_hash;
	char devann_hash_name[DEVANN_HASH_NAME_MAX];
	boolean_t share_ref = 0;
	vfs_t *rootvfsp = NULL;

	extern vfs_t EIO_vfs;

	if (secpolicy_zone_config(CRED()) != 0)
		return (set_errno(EPERM));

	/* can't boot zone from within chroot environment */
	if (PTOU(pp)->u_rdir != NULL && PTOU(pp)->u_rdir != rootdir)
		return (zone_create_error(ENOTSUP, ZE_CHROOTED,
		    extended_error));

	zone = kmem_zalloc(sizeof (zone_t), KM_SLEEP);
	zoneid = zone->zone_id = id_alloc(zoneid_space);
	zone->zone_status = ZONE_IS_UNINITIALIZED;
	zone->zone_pool = pool_default;
	zone->zone_pool_mod = gethrtime();
	zone->zone_psetid = ZONE_PS_INVAL;
	zone->zone_ncpus = 0;
	zone->zone_ncpus_online = 0;
	zone->zone_restart_init = B_TRUE;
	zone->zone_brand_name = NULL;
	zone->zone_brand = NULL;
	zone->zone_initname = NULL;
	mutex_init(&zone->zone_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zone->zone_nlwps_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zone->zone_mem_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&zone->zone_cv, NULL, CV_DEFAULT, NULL);
	list_create(&zone->zone_ref_list, sizeof (zone_ref_t),
	    offsetof(zone_ref_t, zref_linkage));
	list_create(&zone->zone_zsd, sizeof (struct zsd_entry),
	    offsetof(struct zsd_entry, zsd_linkage));
	list_create(&zone->zone_datasets, sizeof (zone_dataset_t),
	    offsetof(zone_dataset_t, zd_linkage));
	list_create(&zone->zone_dl_list, sizeof (zone_dl_t),
	    offsetof(zone_dl_t, zdl_linkage));
	rw_init(&zone->zone_mlps.mlpl_rwlock, NULL, RW_DEFAULT, NULL);
	rw_init(&zone->zone_mntfs_db_lock, NULL, RW_DEFAULT, NULL);

	if (flags & ZCF_NET_EXCL) {
		zone->zone_flags |= ZF_NET_EXCL;
	}

	if ((error = zone_set_name(zone, zone_name)) != 0) {
		zone_free(zone);
		return (zone_create_error(error, 0, extended_error));
	}

	if ((error = zone_set_root(zone, zone_root)) != 0) {
		zone_free(zone);
		return (zone_create_error(error, 0, extended_error));
	}
	if ((error = zone_set_privset(zone, zone_privs, zone_privssz)) != 0) {
		zone_free(zone);
		return (zone_create_error(error, 0, extended_error));
	}

	/* initialize node name to be the same as zone name */
	zone->zone_nodename = kmem_alloc(_SYS_NMLN, KM_SLEEP);
	(void) strncpy(zone->zone_nodename, zone->zone_name, _SYS_NMLN);
	zone->zone_nodename[_SYS_NMLN - 1] = '\0';

	zone->zone_domain = kmem_alloc(_SYS_NMLN, KM_SLEEP);
	zone->zone_domain[0] = '\0';
	zone->zone_hostid = HW_INVALID_HOSTID;
	zone->zone_shares = 1;
	zone->zone_shmmax = 0;
	zone->zone_ipc.ipcq_shmmni = 0;
	zone->zone_ipc.ipcq_semmni = 0;
	zone->zone_ipc.ipcq_msgmni = 0;
	zone->zone_bootargs = NULL;
	zone->zone_fs_allowed = NULL;
	zone->zone_macprofile = NULL;
	zone->zone_initname =
	    kmem_alloc(strlen(zone_default_initname) + 1, KM_SLEEP);
	(void) strcpy(zone->zone_initname, zone_default_initname);
	zone->zone_nlwps = 0;
	zone->zone_nlwps_ctl = INT_MAX;
	zone->zone_nprocs = 0;
	zone->zone_nprocs_ctl = INT_MAX;
	zone->zone_locked_mem = 0;
	zone->zone_locked_mem_ctl = UINT64_MAX;
	zone->zone_max_swap = 0;
	zone->zone_max_swap_ctl = UINT64_MAX;
	zone->zone_max_lofi = 0;
	zone->zone_max_lofi_ctl = UINT64_MAX;
	zone->zone_lockedmem_kstat = NULL;
	zone->zone_swapresv_kstat = NULL;
	zone->zone_destroy_time = 0;

	/*
	 * Zsched initializes the rctls.
	 */
	zone->zone_rctls = NULL;

	if ((error = parse_rctls(rctlbuf, rctlbufsz, &rctls)) != 0) {
		zone_free(zone);
		return (zone_create_error(error, 0, extended_error));
	}

	if ((error = parse_zfs(zone, zfsbuf, zfsbufsz)) != 0) {
		zone_free(zone);
		return (set_errno(error));
	}

	(void) sprintf(devann_hash_name,
	    DEVANN_HASH_NAME_PREFIX "%s", zone->zone_name);
	zone->zone_devann_hash = mod_hash_create_extended(devann_hash_name,
	    zone_devann_hash_size, mod_hash_null_keydtor, devann_hash_val_dtor,
	    devann_hash, NULL, devann_cmp, KM_SLEEP);
	mutex_init(&zone->zone_devann_lock, NULL, MUTEX_DEFAULT, NULL);

	/*
	 * Read in the trusted system parameters:
	 * match flag and sensitivity label.
	 */
	zone->zone_match = match;
	if (is_system_labeled() && !(zone->zone_flags & ZF_IS_SCRATCH)) {
		/* Fail if requested to set doi to anything but system's doi */
		if (doi != 0 && doi != default_doi) {
			zone_free(zone);
			return (set_errno(EINVAL));
		}
		/* Always apply system's doi to the zone */
		error = zone_set_label(zone, label, default_doi);
		if (error != 0) {
			zone_free(zone);
			return (set_errno(error));
		}
		insert_label_hash = B_TRUE;
	} else {
		/* all zones get an admin_low label if system is not labeled */
		zone->zone_slabel = l_admin_low;
		label_hold(l_admin_low);
		insert_label_hash = B_FALSE;
	}

	/*
	 * Stop all lwps since that's what normally happens as part of fork().
	 * This needs to happen before we grab any locks to avoid deadlock
	 * (another lwp in the process could be waiting for the held lock).
	 */
	if (curthread != pp->p_agenttp && !holdlwps(SHOLDFORK)) {
		zone_free(zone);
		if (rctls)
			nvlist_free(rctls);
		return (zone_create_error(error, 0, extended_error));
	}

	/*
	 * Set up credential for kernel access.  After this, any errors
	 * should go through the dance in errout rather than calling
	 * zone_free directly.
	 */
	zone->zone_kcred = crdup(kcred);
	crsetzone(zone->zone_kcred, zone);
	priv_intersect(zone->zone_privset, &CR_PPRIV(zone->zone_kcred));
	priv_intersect(zone->zone_privset, &CR_EPRIV(zone->zone_kcred));
	priv_intersect(zone->zone_privset, &CR_IPRIV(zone->zone_kcred));
	priv_intersect(zone->zone_privset, &CR_LPRIV(zone->zone_kcred));

	mutex_enter(&zonehash_lock);
	/*
	 * Make sure zone doesn't already exist.
	 *
	 * If the system and zone are labeled,
	 * make sure no other zone exists that has the same label.
	 */
	if ((ztmp = zone_find_all_by_name(zone->zone_name)) != NULL ||
	    (insert_label_hash &&
	    (ztmp = zone_find_all_by_label(zone->zone_slabel)) != NULL)) {
		zone_status_t status;

		status = zone_status_get(ztmp);
		if (status == ZONE_IS_READY || status == ZONE_IS_RUNNING)
			error = EEXIST;
		else
			error = EBUSY;

		if (insert_label_hash)
			error2 = ZE_LABELINUSE;

		goto errout;
	}

	/*
	 * Don't allow zone creations which would cause one zone's rootpath to
	 * be accessible from that of another (non-global) zone.
	 */
	if (zone_is_nested(zone->zone_rootpath)) {
		error = EBUSY;
		goto errout;
	}

	/*
	 * Make sure vfs_zone isn't stale; this could happen if userland
	 * failed to unmount a previous zone's root.
	 */
	if (VTOZONE(zone->zone_rootvp) != global_zone) {
		/*
		 * We've just checked that there is no active zone with this
		 * path, so it must be a zombie zone.
		 */
		ASSERT(zone_status_get(VTOZONE(zone->zone_rootvp)) ==
		    ZONE_IS_DEAD);
		error = EBUSY;
		goto errout;
	}

	if (vfs_shrzid_set(zone->zone_rootvp->v_vfsp, zone->zone_id)) {
		error = EBUSY;
		error2 = ZE_ARESHARES;
		goto errout;
	}
	share_ref = 1;

	ASSERT(zonecount != 0);		/* check for leaks */
	if (zonecount + 1 > maxzones) {
		error = ENOMEM;
		goto errout;
	}

	if (zone_mount_count(zone->zone_rootpath) != 0) {
		error = EBUSY;
		error2 = ZE_AREMOUNTS;
		goto errout;
	}

	/*
	 * If the zone's root vnode is the root of a filesystem (VROOT),
	 * then we will want to set vfs_zone = thiszone on it (later, in
	 * zsched()). Stash a pointer to the vfs_t now so we won't need
	 * to worry about things getting inconsistent if it gets swapped
	 * out with EIO_vfs later.
	 */
	if (zone->zone_rootvp->v_flag & VROOT) {
		if ((rootvfsp = zone->zone_rootvp->v_vfsp) == &EIO_vfs) {
			error = EIO;
			goto errout;
		}
		VFS_HOLD(rootvfsp);
	}

	/*
	 * Zone is still incomplete, but we need to drop all locks while
	 * zsched() initializes this zone's kernel process.  We
	 * optimistically add the zone to the hashtable and associated
	 * lists so a parallel zone_create() doesn't try to create the
	 * same zone.
	 */
	zonecount++;
	(void) mod_hash_insert(zonehashbyid,
	    (mod_hash_key_t)(uintptr_t)zone->zone_id,
	    (mod_hash_val_t)(uintptr_t)zone);
	str = kmem_alloc(strlen(zone->zone_name) + 1, KM_SLEEP);
	(void) strcpy(str, zone->zone_name);
	(void) mod_hash_insert(zonehashbyname, (mod_hash_key_t)str,
	    (mod_hash_val_t)(uintptr_t)zone);
	if (insert_label_hash) {
		(void) mod_hash_insert(zonehashbylabel,
		    (mod_hash_key_t)zone->zone_slabel, (mod_hash_val_t)zone);
		zone->zone_flags |= ZF_HASHED_LABEL;
	}

	/*
	 * Insert into active list.  At this point there are no 'hold's
	 * on the zone, but everyone else knows not to use it, so we can
	 * continue to use it.  zsched() will do a zone_hold() if the
	 * newproc() is successful.
	 */
	list_insert_tail(&zone_active, zone);
	mutex_exit(&zonehash_lock);

	zarg.zone = zone;
	zarg.nvlist = rctls;
	zarg.rootvfsp = rootvfsp;
	/*
	 * The process, task, and project rctls are probably wrong;
	 * we need an interface to get the default values of all rctls,
	 * and initialize zsched appropriately.  I'm not sure that that
	 * makes much of a difference, though.
	 */
	error = newproc(zsched, (void *)&zarg, syscid, minclsyspri, NULL, 0);
	if (error != 0) {
		/*
		 * We need to undo all globally visible state.
		 */
		mutex_enter(&zonehash_lock);
		list_remove(&zone_active, zone);
		if (zone->zone_flags & ZF_HASHED_LABEL) {
			ASSERT(zone->zone_slabel != NULL);
			(void) mod_hash_destroy(zonehashbylabel,
			    (mod_hash_key_t)zone->zone_slabel);
		}
		(void) mod_hash_destroy(zonehashbyname,
		    (mod_hash_key_t)(uintptr_t)zone->zone_name);
		(void) mod_hash_destroy(zonehashbyid,
		    (mod_hash_key_t)(uintptr_t)zone->zone_id);
		ASSERT(zonecount > 1);
		zonecount--;
		goto errout;
	}

	/*
	 * Zone creation can't fail from now on.
	 */

	/*
	 * Create zone kstats
	 */
	zone_kstat_create(zone);

	/*
	 * Let the other lwps continue.
	 */
	mutex_enter(&pp->p_lock);
	if (curthread != pp->p_agenttp)
		continuelwps(pp);
	mutex_exit(&pp->p_lock);

	/*
	 * Wait for zsched to finish initializing the zone.
	 */
	zone_status_wait(zone, ZONE_IS_READY);

	if (rootvfsp != NULL)
		VFS_RELE(rootvfsp);

	if (rctls)
		nvlist_free(rctls);

	return (zoneid);

errout:
	mutex_exit(&zonehash_lock);
	/*
	 * Let the other lwps continue.
	 */
	mutex_enter(&pp->p_lock);
	if (curthread != pp->p_agenttp)
		continuelwps(pp);
	mutex_exit(&pp->p_lock);

	if (share_ref) {
		int err;

		err = vfs_shrzid_set(zone->zone_rootvp->v_vfsp, GLOBAL_ZONEID);
		ASSERT(err == 0);
	}

	if (rootvfsp != NULL && rootvfsp != &EIO_vfs)
		VFS_RELE(rootvfsp);

	if (rctls)
		nvlist_free(rctls);
	/*
	 * There is currently one reference to the zone, a cred_ref from
	 * zone_kcred.  To free the zone, we call crfree, which will call
	 * zone_cred_rele, which will call zone_free.
	 */
	ASSERT(zone->zone_cred_ref == 1);
	ASSERT(zone->zone_kcred->cr_ref == 1);
	ASSERT(zone->zone_ref == 0);
	zkcr = zone->zone_kcred;
	zone->zone_kcred = NULL;
	crfree(zkcr);				/* triggers call to zone_free */
	return (zone_create_error(error, error2, extended_error));
}

/*
 * Cause the zone to boot.  This is pretty simple, since we let zoneadmd do
 * the heavy lifting.  initname is the path to the program to launch
 * at the "top" of the zone; if this is NULL, we use the system default,
 * which is stored at zone_default_initname.
 */
static int
zone_boot(zoneid_t zoneid)
{
	int err;
	zone_t *zone;

	if (secpolicy_zone_config(CRED()) != 0)
		return (set_errno(EPERM));
	if (zoneid < MIN_USERZONEID || zoneid > MAX_ZONEID)
		return (set_errno(EINVAL));

	mutex_enter(&zonehash_lock);
	/*
	 * Look for zone under hash lock to prevent races with calls to
	 * zone_shutdown, zone_destroy, etc.
	 */
	if ((zone = zone_find_all_by_id(zoneid)) == NULL) {
		mutex_exit(&zonehash_lock);
		return (set_errno(EINVAL));
	}

	mutex_enter(&zone->zone_lock);
	if (zone_status_get(zone) != ZONE_IS_READY) {
		mutex_exit(&zone->zone_lock);
		mutex_exit(&zonehash_lock);
		return (set_errno(EINVAL));
	}
	zone_status_set(zone, ZONE_IS_BOOTING);
	mutex_exit(&zone->zone_lock);

	zone_hold(zone);	/* so we can use the zone_t later */
	mutex_exit(&zonehash_lock);

	if (zone_status_wait_sig(zone, ZONE_IS_RUNNING) == 0) {
		zone_rele(zone);
		return (set_errno(EINTR));
	}

	/*
	 * Boot (starting init) might have failed, in which case the zone
	 * will go to the SHUTTING_DOWN state; an appropriate errno will
	 * be placed in zone->zone_boot_err, and so we return that.
	 */
	err = zone->zone_boot_err;
	zone_rele(zone);
	return (err ? set_errno(err) : 0);
}

/*
 * Kills all user processes in the zone, waiting for them all to exit
 * before returning.
 */
static int
zone_empty(zone_t *zone)
{
	int waitstatus;

	/*
	 * We need to drop zonehash_lock before killing all
	 * processes, otherwise we'll deadlock with zone_find_*
	 * which can be called from the exit path.
	 */
	ASSERT(MUTEX_NOT_HELD(&zonehash_lock));
	while ((waitstatus = zone_status_timedwait_sig(zone,
	    ddi_get_lbolt() + hz, ZONE_IS_EMPTY)) == -1) {
		killall(zone->zone_id);
	}
	/*
	 * return EINTR if we were signaled
	 */
	if (waitstatus == 0)
		return (EINTR);
	return (0);
}

/*
 * This function implements the policy for zone visibility.
 *
 * In standard Solaris, a non-global zone can only see itself.
 *
 * In Trusted Extensions, a labeled zone can lookup any zone whose label
 * it dominates. For this test, the label of the global zone is treated as
 * admin_high so it is special-cased instead of being checked for dominance.
 *
 * Returns true if zone attributes are viewable, false otherwise.
 */
static boolean_t
zone_list_access(zone_t *zone)
{

	if (curproc->p_zone == global_zone ||
	    curproc->p_zone == zone) {
		return (B_TRUE);
	} else if (is_system_labeled() && !(zone->zone_flags & ZF_IS_SCRATCH)) {
		bslabel_t *curproc_label;
		bslabel_t *zone_label;

		curproc_label = label2bslabel(curproc->p_zone->zone_slabel);
		zone_label = label2bslabel(zone->zone_slabel);

		if (zone->zone_id != GLOBAL_ZONEID &&
		    bldominates(curproc_label, zone_label)) {
			return (B_TRUE);
		} else {
			return (B_FALSE);
		}
	} else {
		return (B_FALSE);
	}
}

/*
 * Systemcall to start the zone's halt sequence.  By the time this
 * function successfully returns, all user processes and kernel threads
 * executing in it will have exited, ZSD shutdown callbacks executed,
 * and the zone status set to ZONE_IS_DOWN.
 *
 * It is possible that the call will interrupt itself if the caller is the
 * parent of any process running in the zone, and doesn't have SIGCHLD blocked.
 */
static int
zone_shutdown(zoneid_t zoneid)
{
	int error;
	zone_t *zone;
	zone_status_t status;

	if (secpolicy_zone_config(CRED()) != 0)
		return (set_errno(EPERM));
	if (zoneid < MIN_USERZONEID || zoneid > MAX_ZONEID)
		return (set_errno(EINVAL));

	mutex_enter(&zonehash_lock);
	/*
	 * Look for zone under hash lock to prevent races with other
	 * calls to zone_shutdown and zone_destroy.
	 */
	if ((zone = zone_find_all_by_id(zoneid)) == NULL) {
		mutex_exit(&zonehash_lock);
		return (set_errno(EINVAL));
	}
	mutex_enter(&zone->zone_lock);
	status = zone_status_get(zone);
	/*
	 * Fail if the zone isn't fully initialized yet.
	 */
	if (status < ZONE_IS_READY) {
		mutex_exit(&zone->zone_lock);
		mutex_exit(&zonehash_lock);
		return (set_errno(EINVAL));
	}
	/*
	 * If conditions required for zone_shutdown() to return have been met,
	 * return success.
	 */
	if (status >= ZONE_IS_DOWN) {
		mutex_exit(&zone->zone_lock);
		mutex_exit(&zonehash_lock);
		return (0);
	}
	/*
	 * If zone_shutdown() hasn't been called before, go through the motions.
	 * If it has, there's nothing to do but wait for the kernel threads to
	 * drain.
	 */
	if (status < ZONE_IS_EMPTY) {
		uint_t ntasks;

		if ((ntasks = zone->zone_ntasks) != 1) {
			/*
			 * There's still stuff running.
			 */
			zone_status_set(zone, ZONE_IS_SHUTTING_DOWN);
		}
		if (ntasks == 1) {
			/*
			 * The only way to create another task is through
			 * zone_enter(), which will block until we drop
			 * zonehash_lock.  The zone is empty.
			 */
			if (zone->zone_kthreads == NULL) {
				/*
				 * Skip ahead to ZONE_IS_DOWN
				 */
				zone_status_set(zone, ZONE_IS_DOWN);
			} else {
				zone_status_set(zone, ZONE_IS_EMPTY);
			}
		}
	}
	zone_hold_locked(zone);	/* so we can use the zone_t later */
	mutex_exit(&zone->zone_lock);
	mutex_exit(&zonehash_lock);

	if (error = zone_empty(zone)) {
		zone_rele(zone);
		return (set_errno(error));
	}
	ASSERT(zone_status_get(zone) >= ZONE_IS_EMPTY);

	/*
	 * After the zone status goes to ZONE_IS_DOWN this zone will no
	 * longer be notified of changes to the pools configuration, so
	 * in order to not end up with a stale pool pointer, we point
	 * ourselves at the default pool and remove all resource
	 * visibility.  This is especially important as the zone_t may
	 * languish on the deathrow for a very long time waiting for
	 * cred's to drain out.
	 *
	 * This rebinding of the zone can happen multiple times
	 * (presumably due to interrupted or parallel systemcalls)
	 * without any adverse effects.
	 */
	if (pool_lock_intr() != 0) {
		zone_rele(zone);
		return (set_errno(EINTR));
	}
	if (pool_state == POOL_ENABLED) {
		mutex_enter(&cpu_lock);
		zone_pool_set(zone, pool_default);
		/*
		 * The zone no longer needs to be able to see any cpus.
		 */
		zone_pset_set(zone, ZONE_PS_INVAL);
		mutex_exit(&cpu_lock);
	}
	pool_unlock();

	/*
	 * ZSD shutdown callbacks can be executed multiple times, hence
	 * it is safe to not be holding any locks across this call.
	 */
	zone_zsd_callbacks(zone, ZSD_SHUTDOWN);

	mutex_enter(&zone->zone_lock);
	/*
	 * Check if netstack zsd shutdown callbacks succeeded by
	 * checking for any datalinks that still remain inside the zone.
	 */
	if (!list_is_empty(&zone->zone_dl_list)) {
		zone_zsd_clear_shutdown_callbacks(zone);
		mutex_exit(&zone->zone_lock);
		zone_rele(zone);
		return (set_errno(EAGAIN));
	}

	if (zone->zone_kthreads == NULL && zone_status_get(zone) < ZONE_IS_DOWN)
		zone_status_set(zone, ZONE_IS_DOWN);
	mutex_exit(&zone->zone_lock);

	/*
	 * Wait for kernel threads to drain.
	 */
	if (!zone_status_wait_sig(zone, ZONE_IS_DOWN)) {
		zone_rele(zone);
		return (set_errno(EINTR));
	}

	/*
	 * Zone can be become down/destroyable even if the above wait
	 * returns EINTR, so any code added here may never execute.
	 * (i.e. don't add code here)
	 */

	zone_rele(zone);
	return (0);
}

/*
 * Systemcall entry point to finalize the zone halt process.  The caller
 * must have already successfully called zone_shutdown(2).
 *
 * When zone_destroy(2) returns, the zone structure has been placed on
 * "zone_deathrow", a place of waiting for zones which have received
 * their final sentencing.  Its zone_id has been released and may be
 * immediately reused by another zone entity. Consumers not in the
 * context of the zone should not make assumptions about the ID of a
 * zone they have a hold on (ie, that a zone_find_by_id() will always
 * return the same zone_t they have held). If such functionality is
 * desired, one may inspect the zone_uniqid field which is guaranteed to
 * be unique on a live system.
 *
 * Later on, when both zone_ref and zone_cred_ref drop to 0, the zone_t
 * itself will be freed.
 *
 * Since the ZSD_DESTROY callbacks are executed when the zone is in the
 * ZONE_IS_DEAD state, consumers not executing in the context of the
 * zone should make sure the zone is in a valid state before making
 * assumptions about its ZSD data.  In fact, subsystems should begin to
 * drop references once the zone makes it to ZONE_IS_SHUTTING_DOWN. Consumers
 * in the context of the zone -- whether user processes or kernel threads
 * (zthreads) -- may safely assume their zone (and ZSD data) is always
 * valid, as the zone is torn down only after all user processes and
 * kernel threads in the zone have exited. (Keep in mind that the
 * _shutdown() callbacks are executed while there are still active
 * zthreads.)
 *
 * Upon successful completion, the zone will have been fully destroyed:
 * zsched will have exited, destructor callbacks executed, and the zone
 * removed from the list of active zones.
 */
static int
zone_destroy(zoneid_t zoneid)
{
	zone_t *zone;
	zone_status_t status;
	int err;

	if (secpolicy_zone_config(CRED()) != 0)
		return (set_errno(EPERM));
	if (zoneid < MIN_USERZONEID || zoneid > MAX_ZONEID)
		return (set_errno(EINVAL));

	mutex_enter(&zonehash_lock);
	/*
	 * Look for zone under hash lock to prevent races with other
	 * calls to zone_destroy.
	 */
	if ((zone = zone_find_all_by_id(zoneid)) == NULL) {
		mutex_exit(&zonehash_lock);
		return (set_errno(EINVAL));
	}

	if (zone_mount_count(zone->zone_rootpath) != 0) {
		mutex_exit(&zonehash_lock);
		return (set_errno(EBUSY));
	}
	mutex_enter(&zone->zone_lock);
	status = zone_status_get(zone);
	if (status != ZONE_IS_DOWN) {
		mutex_exit(&zone->zone_lock);
		mutex_exit(&zonehash_lock);
		return (set_errno(EBUSY));
	}
	zone_status_set(zone, ZONE_IS_DYING); /* Tell zsched to exit */
	/*
	 * We are effectively single-threaded at this point, since any other
	 * caller to zone_destroy(2) will notice that we have set
	 * ZONE_IS_DYING above, and will return EBUSY.
	 *
	 * There is still (at least) the reference added by zsched() so
	 * we can safely use the zone without doing a zone_hold()
	 *
	 * zone_destroy(2) cannot fail from now on.
	 */
	mutex_exit(&zone->zone_lock);
	mutex_exit(&zonehash_lock);

	/*
	 * wait for zsched to exit
	 */
	zone_status_wait(zone, ZONE_IS_DEAD);

	/*
	 * Execute the destructor callbacks
	 */
	zone_zsd_callbacks(zone, ZSD_DESTROY);

	zone->zone_netstack = NULL;
	err = vfs_shrzid_set(zone->zone_rootvp->v_vfsp, GLOBAL_ZONEID);
	ASSERT(err == 0);
	/* Release the zoneid so it can be reused */
	id_free(zoneid_space, zone->zone_id);

	/*
	 * Remove CPU cap for this zone now
	 */
	cpucaps_zone_remove(zone);

	/* Get rid of the zone's kstats before it is recreated */
	zone_kstat_delete(zone);

	/* remove the pfexecd doors */
	if (zone->zone_pfexecd != NULL) {
		klpd_freelist(&zone->zone_pfexecd);
		zone->zone_pfexecd = NULL;
	}
	if (zone->zone_mwac_white_list != NULL) {
		klpd_freemwac_tree(zone->zone_mwac_white_list);
		zone->zone_mwac_white_list = NULL;
	}
	if (zone->zone_mwac_black_list != NULL) {
		klpd_freemwac_tree(zone->zone_mwac_black_list);
		zone->zone_mwac_black_list = NULL;
	}

	/* free brand specific data */
	if (ZONE_HAS_BRANDOPS(zone))
		ZBROP(zone)->b_free_brand_data(zone);

	/* Say goodbye to brand framework but keep the brand name around. */
	brand_modclose(zone->zone_brand);
	zone->zone_brand = NULL;

	/* zero out audit pointer. Memory was freed in au_zone_destroy() */
	zone->zone_audit_kctxt = NULL;

	mutex_enter(&zonehash_lock);
	/*
	 * It is now safe to let the zone be recreated; remove it from the
	 * lists.  The memory will not be freed until zone_ref and
	 * zone_cred_ref both drop to zero.
	 */
	ASSERT(zonecount > 1);	/* must be > 1; can't destroy global zone */
	zonecount--;

	/* remove from active list and hash tables */
	list_remove(&zone_active, zone);
	(void) mod_hash_destroy(zonehashbyname,
	    (mod_hash_key_t)zone->zone_name);
	(void) mod_hash_destroy(zonehashbyid,
	    (mod_hash_key_t)(uintptr_t)zone->zone_id);
	if (zone->zone_flags & ZF_HASHED_LABEL)
		(void) mod_hash_destroy(zonehashbylabel,
		    (mod_hash_key_t)zone->zone_slabel);
	mutex_exit(&zonehash_lock);

	/*
	 * Release the root vnode; we're not using it anymore.  Nor should any
	 * other thread that might access it exist.
	 */
	if (zone->zone_rootvp != NULL) {
		VN_RELE(zone->zone_rootvp);
		zone->zone_rootvp = NULL;
	}

	/* save the time so we will know how long zones have been on deathrow */
	zone->zone_destroy_time = ddi_get_time();

	/* add to deathrow list */
	mutex_enter(&zone_deathrow_lock);
	zone_deathrow_count++;
	list_insert_tail(&zone_deathrow, zone);
	mutex_exit(&zone_deathrow_lock);

	/*
	 * Drop the reference added by zsched(). The zone may languish on the
	 * deathrow if there are outstanding references (or cred references).
	 * At any rate, the zone can now be re-created.
	 */
	zone_rele(zone);
	return (0);
}

/*
 * Common function used by zone_getattr(2) and zone_getattr_defunct(2)
 * to lookup a zone's attributes.
 */
static int
zone_getattr_common(zone_t *zone, int attr, void *buf, size_t bufsize,
    ssize_t *retsizep)
{
	zoneid_t zoneid = zone->zone_id;
	size_t size;
	int error = 0, err;
	char *zonepath;
	char *outstr;
	zone_status_t zone_status;
	pid_t initpid;
	boolean_t global = (curzone == global_zone);
	boolean_t inzone = (curzone->zone_id == zoneid);
	ushort_t flags;
	zone_net_data_t *zbuf;

	switch (attr) {
	case ZONE_ATTR_ROOT:
		if (global) {
			/*
			 * Copy the path to trim the trailing "/" (except for
			 * the global zone).
			 */
			if (zone != global_zone)
				size = zone->zone_rootpathlen - 1;
			else
				size = zone->zone_rootpathlen;
			zonepath = kmem_alloc(size, KM_SLEEP);
			bcopy(zone->zone_rootpath, zonepath, size);
			zonepath[size - 1] = '\0';
		} else {
			if (inzone || !is_system_labeled()) {
				/*
				 * Caller is not in the global zone.
				 * if the query is on the current zone
				 * or the system is not labeled,
				 * just return faked-up path for current zone.
				 */
				zonepath = "/";
				size = 2;
			} else {
				/*
				 * Return related path for current zone.
				 */
				int prefix_len = strlen(zone_prefix);
				int zname_len = strlen(zone->zone_name);

				size = prefix_len + zname_len + 1;
				zonepath = kmem_alloc(size, KM_SLEEP);
				bcopy(zone_prefix, zonepath, prefix_len);
				bcopy(zone->zone_name, zonepath +
				    prefix_len, zname_len);
				zonepath[size - 1] = '\0';
			}
		}
		if (bufsize > size)
			bufsize = size;
		if (buf != NULL) {
			err = copyoutstr(zonepath, buf, bufsize, NULL);
			if (err != 0 && err != ENAMETOOLONG)
				error = EFAULT;
		}
		if (global || (is_system_labeled() && !inzone))
			kmem_free(zonepath, size);
		break;

	case ZONE_ATTR_NAME:
		size = strlen(zone->zone_name) + 1;
		if (bufsize > size)
			bufsize = size;
		if (buf != NULL) {
			err = copyoutstr(zone->zone_name, buf, bufsize, NULL);
			if (err != 0 && err != ENAMETOOLONG)
				error = EFAULT;
		}
		break;

	case ZONE_ATTR_STATUS:
		/*
		 * Since we're not holding zonehash_lock, the zone status
		 * may be anything; leave it up to userland to sort it out.
		 */
		size = sizeof (zone_status);
		if (bufsize > size)
			bufsize = size;
		zone_status = zone_status_get(zone);
		if (buf != NULL &&
		    copyout(&zone_status, buf, bufsize) != 0)
			error = EFAULT;
		break;
	case ZONE_ATTR_FLAGS:
		size = sizeof (zone->zone_flags);
		if (bufsize > size)
			bufsize = size;
		flags = zone->zone_flags;
		if (buf != NULL &&
		    copyout(&flags, buf, bufsize) != 0)
			error = EFAULT;
		break;
	case ZONE_ATTR_PRIVSET:
		size = sizeof (priv_set_t);
		if (bufsize > size)
			bufsize = size;
		if (buf != NULL &&
		    copyout(zone->zone_privset, buf, bufsize) != 0)
			error = EFAULT;
		break;
	case ZONE_ATTR_UNIQID:
		size = sizeof (zone->zone_uniqid);
		if (bufsize > size)
			bufsize = size;
		if (buf != NULL &&
		    copyout(&zone->zone_uniqid, buf, bufsize) != 0)
			error = EFAULT;
		break;
	case ZONE_ATTR_POOLID:
		{
			pool_t *pool;
			poolid_t poolid;

			if (pool_lock_intr() != 0) {
				error = EINTR;
				break;
			}
			pool = zone_pool_get(zone);
			poolid = pool->pool_id;
			pool_unlock();
			size = sizeof (poolid);
			if (bufsize > size)
				bufsize = size;
			if (buf != NULL && copyout(&poolid, buf, size) != 0)
				error = EFAULT;
		}
		break;
	case ZONE_ATTR_SLBL:
		size = sizeof (bslabel_t);
		if (bufsize > size)
			bufsize = size;
		if (zone->zone_slabel == NULL)
			error = EINVAL;
		else if (buf != NULL &&
		    copyout(label2bslabel(zone->zone_slabel), buf,
		    bufsize) != 0)
			error = EFAULT;
		break;
	case ZONE_ATTR_INITPID:
		size = sizeof (initpid);
		if (bufsize > size)
			bufsize = size;
		initpid = zone->zone_proc_initpid;
		if (initpid == -1) {
			error = ESRCH;
			break;
		}
		if (buf != NULL &&
		    copyout(&initpid, buf, bufsize) != 0)
			error = EFAULT;
		break;
	case ZONE_ATTR_BRAND:
		if (zone->zone_brand_name == NULL) {
			error = ENOTACTIVE;
			break;
		}

		size = strlen(zone->zone_brand_name) + 1;

		if (bufsize > size)
			bufsize = size;
		if (buf != NULL) {
			err = copyoutstr(zone->zone_brand_name, buf,
			    bufsize, NULL);
			if (err != 0 && err != ENAMETOOLONG)
				error = EFAULT;
		}
		break;
	case ZONE_ATTR_INITNAME:
		size = strlen(zone->zone_initname) + 1;
		if (bufsize > size)
			bufsize = size;
		if (buf != NULL) {
			err = copyoutstr(zone->zone_initname, buf, bufsize,
			    NULL);
			if (err != 0 && err != ENAMETOOLONG)
				error = EFAULT;
		}
		break;
	case ZONE_ATTR_BOOTARGS:
		if (zone->zone_bootargs == NULL)
			outstr = "";
		else
			outstr = zone->zone_bootargs;
		size = strlen(outstr) + 1;
		if (bufsize > size)
			bufsize = size;
		if (buf != NULL) {
			err = copyoutstr(outstr, buf, bufsize, NULL);
			if (err != 0 && err != ENAMETOOLONG)
				error = EFAULT;
		}
		break;
	case ZONE_ATTR_PHYS_MCAP:
		size = sizeof (zone->zone_phys_mcap);
		if (bufsize > size)
			bufsize = size;
		if (buf != NULL &&
		    copyout(&zone->zone_phys_mcap, buf, bufsize) != 0)
			error = EFAULT;
		break;
	case ZONE_ATTR_SCHED_CLASS:
		mutex_enter(&class_lock);

		if (zone->zone_defaultcid >= loaded_classes)
			outstr = "";
		else
			outstr = sclass[zone->zone_defaultcid].cl_name;
		mutex_exit(&class_lock);
		size = strlen(outstr) + 1;
		if (bufsize > size)
			bufsize = size;
		if (buf != NULL) {
			err = copyoutstr(outstr, buf, bufsize, NULL);
			if (err != 0 && err != ENAMETOOLONG)
				error = EFAULT;
		}
		break;
	case ZONE_ATTR_HOSTID:
		if (zone->zone_hostid != HW_INVALID_HOSTID &&
		    bufsize == sizeof (zone->zone_hostid)) {
			size = sizeof (zone->zone_hostid);
			if (buf != NULL && copyout(&zone->zone_hostid, buf,
			    bufsize) != 0)
				error = EFAULT;
		} else {
			error = EINVAL;
		}
		break;
	case ZONE_ATTR_FS_ALLOWED:
		if (zone->zone_fs_allowed == NULL)
			outstr = "";
		else
			outstr = zone->zone_fs_allowed;
		size = strlen(outstr) + 1;
		if (bufsize > size)
			bufsize = size;
		if (buf != NULL) {
			err = copyoutstr(outstr, buf, bufsize, NULL);
			if (err != 0 && err != ENAMETOOLONG)
				error = EFAULT;
		}
		break;
	case ZONE_ATTR_NETWORK:
		zbuf = kmem_alloc(bufsize, KM_SLEEP);
		if (copyin(buf, zbuf, bufsize) != 0) {
			error = EFAULT;
		} else {
			error = zone_get_network(zoneid, zbuf);
			if (error == 0) {
				size = sizeof (*zbuf) + zbuf->zn_len;
				if (copyout(zbuf, buf, size) != 0)
					error = EFAULT;
			}
		}
		kmem_free(zbuf, bufsize);
		break;
	case ZONE_ATTR_MAC_PROFILE:
		if (zone->zone_macprofile == NULL)
			outstr = "none";
		else
			outstr = zone->zone_macprofile;
		size = strlen(outstr) + 1;
		if (bufsize > size)
			bufsize = size;
		if (buf != NULL) {
			err = copyoutstr(outstr, buf, bufsize, NULL);
			if (err != 0 && err != ENAMETOOLONG)
				error = EFAULT;
		}
		break;
	default:
		if ((attr >= ZONE_ATTR_BRAND_ATTRS) &&
		    ZONE_HAS_BRANDOPS(zone)) {
			size = bufsize;
			error = ZBROP(zone)->b_getattr(zone, attr, buf, &size);
		} else {
			error = EINVAL;
		}
	}

	*retsizep = (ssize_t)size;
	return (error);
}

/*
 * Systemcall entry point for zone_getattr(2).
 */
static ssize_t
zone_getattr(zoneid_t zoneid, int attr, void *buf, size_t bufsize)
{
	zone_status_t zone_status;
	zone_t *zone;
	ssize_t retsize;
	int error;

	mutex_enter(&zonehash_lock);
	if ((zone = zone_find_all_by_id(zoneid)) == NULL) {
		mutex_exit(&zonehash_lock);
		return (set_errno(EINVAL));
	}
	zone_status = zone_status_get(zone);
	if (zone_status < ZONE_IS_READY) { /* same as zone_find_by_id() */
		mutex_exit(&zonehash_lock);
		return (set_errno(EINVAL));
	}
	zone_hold(zone);
	mutex_exit(&zonehash_lock);

	/*
	 * If not in the global zone, don't show information about other zones,
	 * unless the system is labeled and the local zone's label dominates
	 * the other zone.
	 */
	if (!zone_list_access(zone)) {
		zone_rele(zone);
		return (set_errno(EINVAL));
	}
	error = zone_getattr_common(zone, attr, buf, bufsize, &retsize);
	zone_rele(zone);
	if (error)
		return (set_errno(error));
	return (retsize);
}

/*
 * Systemcall entry point for zone_getattr_defunct(2)
 */
static ssize_t
zone_getattr_defunct(uint64_t uniqid, int attr, void *buf, size_t bufsize)
{
	zone_t *zone;
	ssize_t retsize;
	int error;

	switch (attr) {
	/* Only these attributes are valid for a defunct zone */
	case ZONE_ATTR_ROOT:
	case ZONE_ATTR_NAME:
	case ZONE_ATTR_FLAGS:
	case ZONE_ATTR_BRAND:
	case ZONE_ATTR_MAC_PROFILE:
		break;
	default:
		return (set_errno(EINVAL));
	}

	if (!INGLOBALZONE(curproc))
		return (set_errno(ENOENT));

	mutex_enter(&zone_deathrow_lock);
	for (zone = list_head(&zone_deathrow); zone != NULL;
	    zone = list_next(&zone_deathrow, zone)) {
		if (zone->zone_uniqid == uniqid) {
			zone_hold(zone);
			break;
		}
	}
	mutex_exit(&zone_deathrow_lock);
	if (zone == NULL) 	/* Couldn't find it */
		return (set_errno(ENOENT));

	error = zone_getattr_common(zone, attr, buf, bufsize, &retsize);
	zone_rele(zone);
	if (error)
		return (set_errno(error));
	return (retsize);
}

/*
 * Systemcall entry point for zone_setattr(2).
 */
/*ARGSUSED*/
static int
zone_setattr(zoneid_t zoneid, int attr, void *buf, size_t bufsize)
{
	zone_t *zone;
	zone_status_t zone_status;
	int err = -1;
	zone_net_data_t *zbuf;

	if (secpolicy_zone_config(CRED()) != 0)
		return (set_errno(EPERM));

	/*
	 * Only the ZONE_ATTR_PHYS_MCAP attribute can be set on the
	 * global zone.
	 */
	if (zoneid == GLOBAL_ZONEID && attr != ZONE_ATTR_PHYS_MCAP) {
		return (set_errno(EINVAL));
	}

	mutex_enter(&zonehash_lock);
	if ((zone = zone_find_all_by_id(zoneid)) == NULL) {
		mutex_exit(&zonehash_lock);
		return (set_errno(EINVAL));
	}
	zone_hold(zone);
	mutex_exit(&zonehash_lock);

	/*
	 * At present most attributes can only be set on non-running,
	 * non-global zones.
	 */
	zone_status = zone_status_get(zone);
	if (attr != ZONE_ATTR_PHYS_MCAP && zone_status != ZONE_IS_READY) {
		err = EINVAL;
		goto done;
	}

	switch (attr) {
	case ZONE_ATTR_INITNAME:
		err = zone_set_initname(zone, (const char *)buf);
		break;
	case ZONE_ATTR_BOOTARGS:
		err = zone_set_bootargs(zone, (const char *)buf);
		break;
	case ZONE_ATTR_BRAND:
		err = zone_set_brand(zone, (const char *)buf);
		break;
	case ZONE_ATTR_FS_ALLOWED:
		err = zone_set_fs_allowed(zone, (const char *)buf);
		break;
	case ZONE_ATTR_PHYS_MCAP:
		err = zone_set_phys_mcap(zone, (const uint64_t *)buf);
		break;
	case ZONE_ATTR_SCHED_CLASS:
		err = zone_set_sched_class(zone, (const char *)buf);
		break;
	case ZONE_ATTR_HOSTID:
		if (bufsize == sizeof (zone->zone_hostid)) {
			if (copyin(buf, &zone->zone_hostid, bufsize) == 0)
				err = 0;
			else
				err = EFAULT;
		} else {
			err = EINVAL;
		}
		break;
	case ZONE_ATTR_NETWORK:
		if (bufsize > (PIPE_BUF + sizeof (zone_net_data_t))) {
			err = EINVAL;
			break;
		}
		zbuf = kmem_alloc(bufsize, KM_SLEEP);
		if (copyin(buf, zbuf, bufsize) != 0) {
			kmem_free(zbuf, bufsize);
			err = EFAULT;
			break;
		}
		err = zone_set_network(zoneid, zbuf);
		kmem_free(zbuf, bufsize);
		break;
	case ZONE_ATTR_MAC_PROFILE:
		err = zone_set_mac_profile(zone, (const char *)buf);
		break;
	default:
		if ((attr >= ZONE_ATTR_BRAND_ATTRS) && ZONE_HAS_BRANDOPS(zone))
			err = ZBROP(zone)->b_setattr(zone, attr, buf, bufsize);
		else
			err = EINVAL;
	}

done:
	zone_rele(zone);
	ASSERT(err != -1);
	return (err != 0 ? set_errno(err) : 0);
}

/*
 * Return zero if the process has at least one vnode mapped in to its
 * address space which shouldn't be allowed to change zones.
 *
 * Also return zero if the process has any shared mappings which reserve
 * swap.  This is because the counting for zone.max-swap does not allow swap
 * reservation to be shared between zones.  zone swap reservation is counted
 * on zone->zone_max_swap.
 */
static int
as_can_change_zones(void)
{
	proc_t *pp = curproc;
	struct seg *seg;
	struct as *as = pp->p_as;
	vnode_t *vp;
	int allow = 1;

	ASSERT(pp->p_as != &kas);
	AS_LOCK_ENTER(as, &as->a_lock, RW_READER);
	for (seg = AS_SEGFIRST(as); seg != NULL; seg = AS_SEGNEXT(as, seg)) {

		/*
		 * Cannot enter zone with shared anon memory which
		 * reserves swap.  See comment above.
		 */
		if (seg_can_change_zones(seg) == B_FALSE) {
			allow = 0;
			break;
		}
		/*
		 * if we can't get a backing vnode for this segment then skip
		 * it.
		 */
		vp = NULL;
		if (SEGOP_GETVP(seg, seg->s_base, &vp) != 0 || vp == NULL)
			continue;
		if (!vn_can_change_zones(vp)) { /* bail on first match */
			allow = 0;
			break;
		}
	}
	AS_LOCK_EXIT(as, &as->a_lock);
	return (allow);
}

/*
 * Count swap reserved by curproc's address space
 */
static size_t
as_swresv(void)
{
	proc_t *pp = curproc;
	struct seg *seg;
	struct as *as = pp->p_as;
	size_t swap = 0;

	ASSERT(pp->p_as != &kas);
	ASSERT(AS_WRITE_HELD(as, &as->a_lock));
	for (seg = AS_SEGFIRST(as); seg != NULL; seg = AS_SEGNEXT(as, seg))
		swap += seg_swresv(seg);

	return (swap);
}

static void
change_zone(proc_t *pp, zone_t *zone)
{
	kthread_t *t;

	ASSERT(MUTEX_HELD(&pp->p_lock));

	pp->p_zone = zone;

	if ((t = pp->p_tlist) != NULL) {
		do {
			LWP_AC_LOCK(t);
			thread_lock(t);
			t->t_zone = zone;
			thread_unlock(t);
			LWP_AC_UNLOCK(t);
		} while ((t = t->t_forw) != pp->p_tlist);
	}
}

/*
 * Systemcall entry point for zone_enter().
 *
 * The current process is injected into said zone.  In the process
 * it will change its project membership, privileges, rootdir/cwd,
 * zone-wide rctls, and pool association to match those of the zone.
 *
 * The first zone_enter() called while the zone is in the ZONE_IS_READY
 * state will transition it to ZONE_IS_RUNNING.  Processes may only
 * enter a zone that is "ready" or "running".
 */
static int
zone_enter(zoneid_t zoneid)
{
	zone_t *zone;
	vnode_t *vp;
	proc_t *pp = curproc;
	contract_t *ct;
	cont_process_t *ctp;
	task_t *tk, *oldtk;
	kproject_t *zone_proj0;
	cred_t *cr, *newcr;
	pool_t *oldpool, *newpool;
	sess_t *sp;
	uid_t uid;
	zone_status_t status;
	int err = 0;
	rctl_entity_p_t e;
	size_t swap;
	kthread_id_t t;

	if (secpolicy_zone_config(CRED()) != 0)
		return (set_errno(EPERM));
	if (zoneid < MIN_USERZONEID || zoneid > MAX_ZONEID)
		return (set_errno(EINVAL));

	/*
	 * Stop all lwps so we don't need to hold a lock to look at
	 * curproc->p_zone.  This needs to happen before we grab any
	 * locks to avoid deadlock (another lwp in the process could
	 * be waiting for the held lock).
	 */
	if (curthread != pp->p_agenttp && !holdlwps(SHOLDFORK))
		return (set_errno(EINTR));

	/*
	 * Make sure we're not changing zones with files open or mapped in
	 * to our address space which shouldn't be changing zones.
	 */
	if (!files_can_change_zones()) {
		err = EBADF;
		goto out;
	}
	if (!as_can_change_zones()) {
		err = EFAULT;
		goto out;
	}

	mutex_enter(&zonehash_lock);
	if (pp->p_zone != global_zone) {
		mutex_exit(&zonehash_lock);
		err = EINVAL;
		goto out;
	}

	zone = zone_find_all_by_id(zoneid);
	if (zone == NULL) {
		mutex_exit(&zonehash_lock);
		err = EINVAL;
		goto out;
	}

	/*
	 * To prevent processes in a zone from holding contracts on
	 * extrazonal resources, and to avoid process contract
	 * memberships which span zones, contract holders and processes
	 * which aren't the sole members of their encapsulating process
	 * contracts are not allowed to zone_enter.
	 */
	ctp = pp->p_ct_process;
	ct = &ctp->conp_contract;
	mutex_enter(&ct->ct_lock);
	mutex_enter(&pp->p_lock);
	if ((avl_numnodes(&pp->p_ct_held) != 0) || (ctp->conp_nmembers != 1)) {
		mutex_exit(&pp->p_lock);
		mutex_exit(&ct->ct_lock);
		mutex_exit(&zonehash_lock);
		err = EINVAL;
		goto out;
	}

	/*
	 * Moreover, we don't allow processes whose encapsulating
	 * process contracts have inherited extrazonal contracts.
	 * While it would be easier to eliminate all process contracts
	 * with inherited contracts, we need to be able to give a
	 * restarted init (or other zone-penetrating process) its
	 * predecessor's contracts.
	 */
	if (ctp->conp_ninherited != 0) {
		contract_t *next;
		for (next = list_head(&ctp->conp_inherited); next;
		    next = list_next(&ctp->conp_inherited, next)) {
			if (contract_getzuniqid(next) != zone->zone_uniqid) {
				mutex_exit(&pp->p_lock);
				mutex_exit(&ct->ct_lock);
				mutex_exit(&zonehash_lock);
				err = EINVAL;
				goto out;
			}
		}
	}

	mutex_exit(&pp->p_lock);
	mutex_exit(&ct->ct_lock);

	status = zone_status_get(zone);
	if (status < ZONE_IS_READY || status >= ZONE_IS_SHUTTING_DOWN) {
		/*
		 * Can't join
		 */
		mutex_exit(&zonehash_lock);
		err = EINVAL;
		goto out;
	}

	/*
	 * Make sure new priv set is within the permitted set for caller
	 */
	if (!priv_issubset(zone->zone_privset, &CR_OPPRIV(CRED()))) {
		mutex_exit(&zonehash_lock);
		err = EPERM;
		goto out;
	}
	/*
	 * We want to momentarily drop zonehash_lock while we optimistically
	 * bind curproc to the pool it should be running in.  This is safe
	 * since the zone can't disappear (we have a hold on it).
	 */
	zone_hold(zone);
	mutex_exit(&zonehash_lock);

	/*
	 * Grab pool_lock to keep the pools configuration from changing
	 * and to stop ourselves from getting rebound to another pool
	 * until we join the zone.
	 */
	if (pool_lock_intr() != 0) {
		zone_rele(zone);
		err = EINTR;
		goto out;
	}
	ASSERT(secpolicy_pool(CRED()) == 0);
	/*
	 * Bind ourselves to the pool currently associated with the zone.
	 */
	oldpool = curproc->p_pool;
	newpool = zone_pool_get(zone);
	if (pool_state == POOL_ENABLED && newpool != oldpool &&
	    (err = pool_do_bind(newpool, P_PID, P_MYID,
	    POOL_BIND_ALL)) != 0) {
		pool_unlock();
		zone_rele(zone);
		goto out;
	}

	/*
	 * Grab cpu_lock now; we'll need it later when we call
	 * task_join().
	 */
	mutex_enter(&cpu_lock);
	mutex_enter(&zonehash_lock);
	/*
	 * Make sure the zone hasn't moved on since we dropped zonehash_lock.
	 */
	if (zone_status_get(zone) >= ZONE_IS_SHUTTING_DOWN) {
		/*
		 * Can't join anymore.
		 */
		mutex_exit(&zonehash_lock);
		mutex_exit(&cpu_lock);
		if (pool_state == POOL_ENABLED &&
		    newpool != oldpool)
			(void) pool_do_bind(oldpool, P_PID, P_MYID,
			    POOL_BIND_ALL);
		pool_unlock();
		zone_rele(zone);
		err = EINVAL;
		goto out;
	}

	/*
	 * a_lock must be held while transferring locked memory and swap
	 * reservation from the global zone to the non global zone because
	 * asynchronous faults on the processes' address space can lock
	 * memory and reserve swap via MCL_FUTURE and MAP_NORESERVE
	 * segments respectively.
	 */
	AS_LOCK_ENTER(pp->as, &pp->p_as->a_lock, RW_WRITER);
	swap = as_swresv();
	mutex_enter(&pp->p_lock);
	zone_proj0 = zone->zone_zsched->p_task->tk_proj;
	/* verify that we do not exceed and task or lwp limits */
	mutex_enter(&zone->zone_nlwps_lock);
	/* add new lwps to zone and zone's proj0 */
	zone_proj0->kpj_nlwps += pp->p_lwpcnt;
	zone->zone_nlwps += pp->p_lwpcnt;
	/* add 1 task to zone's proj0 */
	zone_proj0->kpj_ntasks += 1;

	zone_proj0->kpj_nprocs++;
	zone->zone_nprocs++;
	mutex_exit(&zone->zone_nlwps_lock);

	mutex_enter(&zone->zone_mem_lock);
	zone->zone_locked_mem += pp->p_locked_mem;
	zone_proj0->kpj_data.kpd_locked_mem += pp->p_locked_mem;
	zone->zone_max_swap += swap;
	mutex_exit(&zone->zone_mem_lock);

	mutex_enter(&(zone_proj0->kpj_data.kpd_crypto_lock));
	zone_proj0->kpj_data.kpd_crypto_mem += pp->p_crypto_mem;
	mutex_exit(&(zone_proj0->kpj_data.kpd_crypto_lock));

	/* remove lwps and process from proc's old zone and old project */
	mutex_enter(&pp->p_zone->zone_nlwps_lock);
	pp->p_zone->zone_nlwps -= pp->p_lwpcnt;
	pp->p_task->tk_proj->kpj_nlwps -= pp->p_lwpcnt;
	pp->p_task->tk_proj->kpj_nprocs--;
	pp->p_zone->zone_nprocs--;
	mutex_exit(&pp->p_zone->zone_nlwps_lock);

	mutex_enter(&pp->p_zone->zone_mem_lock);
	pp->p_zone->zone_locked_mem -= pp->p_locked_mem;
	pp->p_task->tk_proj->kpj_data.kpd_locked_mem -= pp->p_locked_mem;
	pp->p_zone->zone_max_swap -= swap;
	mutex_exit(&pp->p_zone->zone_mem_lock);

	mutex_enter(&(pp->p_task->tk_proj->kpj_data.kpd_crypto_lock));
	pp->p_task->tk_proj->kpj_data.kpd_crypto_mem -= pp->p_crypto_mem;
	mutex_exit(&(pp->p_task->tk_proj->kpj_data.kpd_crypto_lock));

	pp->p_flag |= SZONETOP;
	change_zone(pp, zone);
	mutex_exit(&pp->p_lock);
	AS_LOCK_EXIT(pp->p_as, &pp->p_as->a_lock);

	/*
	 * Joining the zone cannot fail from now on.
	 *
	 * This means that a lot of the following code can be commonized and
	 * shared with zsched().
	 */

	/*
	 * If the process contract fmri was inherited, we need to
	 * flag this so that any contract status will not leak
	 * extra zone information, svc_fmri in this case
	 */
	if (ctp->conp_svc_ctid != ct->ct_id) {
		mutex_enter(&ct->ct_lock);
		ctp->conp_svc_zone_enter = ct->ct_id;
		mutex_exit(&ct->ct_lock);
	}

	/*
	 * Reset the encapsulating process contract's zone.
	 */
	ASSERT(ct->ct_mzuniqid == GLOBAL_ZONEUNIQID);
	contract_setzuniqid(ct, zone->zone_uniqid);

	/*
	 * Create a new task and associate the process with the project keyed
	 * by (projid,zoneid).
	 *
	 * We might as well be in project 0; the global zone's projid doesn't
	 * make much sense in a zone anyhow.
	 *
	 * This also increments zone_ntasks, and returns with p_lock held.
	 */
	tk = task_create(0, zone);
	oldtk = task_join(tk, 0);
	mutex_exit(&cpu_lock);

	/*
	 * call RCTLOP_SET functions on this proc
	 */
	e.rcep_p.zone = zone;
	e.rcep_t = RCENTITY_ZONE;
	(void) rctl_set_dup(NULL, NULL, pp, &e, zone->zone_rctls, NULL,
	    RCD_CALLBACK);
	mutex_exit(&pp->p_lock);

	/*
	 * We don't need to hold any of zsched's locks here; not only do we know
	 * the process and zone aren't going away, we know its session isn't
	 * changing either.
	 *
	 * By joining zsched's session here, we mimic the behavior in the
	 * global zone of init's sid being the pid of sched.  We extend this
	 * to all zlogin-like zone_enter()'ing processes as well.
	 */
	mutex_enter(&pidlock);
	sp = zone->zone_zsched->p_sessp;
	sess_hold(zone->zone_zsched);
	mutex_enter(&pp->p_lock);
	pgexit(pp);
	sess_rele(pp->p_sessp, B_TRUE);
	pp->p_sessp = sp;
	pgjoin(pp, zone->zone_zsched->p_pidp);

	/*
	 * If any threads are scheduled to be placed on zone wait queue they
	 * should abandon the idea since the wait queue is changing.
	 * We need to be holding pidlock & p_lock to do this.
	 */
	if ((t = pp->p_tlist) != NULL) {
		do {
			thread_lock(t);
			/*
			 * Kick this thread so that he doesn't sit
			 * on a wrong wait queue.
			 */
			if (ISWAITING(t))
				setrun_locked(t);

			if (t->t_schedflag & TS_ANYWAITQ)
				t->t_schedflag &= ~ TS_ANYWAITQ;

			thread_unlock(t);
		} while ((t = t->t_forw) != pp->p_tlist);
	}

	/*
	 * If there is a default scheduling class for the zone and it is not
	 * the class we are currently in, change all of the threads in the
	 * process to the new class.  We need to be holding pidlock & p_lock
	 * when we call parmsset so this is a good place to do it.
	 */
	if (zone->zone_defaultcid > 0 &&
	    zone->zone_defaultcid != curthread->t_cid) {
		pcparms_t pcparms;

		pcparms.pc_cid = zone->zone_defaultcid;
		pcparms.pc_clparms[0] = 0;

		/*
		 * If setting the class fails, we still want to enter the zone.
		 */
		if ((t = pp->p_tlist) != NULL) {
			do {
				(void) parmsset(&pcparms, t);
			} while ((t = t->t_forw) != pp->p_tlist);
		}
	}

	mutex_exit(&pp->p_lock);
	mutex_exit(&pidlock);

	mutex_exit(&zonehash_lock);
	/*
	 * We're firmly in the zone; let pools progress.
	 */
	pool_unlock();
	task_rele(oldtk);
	/*
	 * We don't need to retain a hold on the zone since we already
	 * incremented zone_ntasks, so the zone isn't going anywhere.
	 */
	zone_rele(zone);

	/*
	 * Chroot
	 */
	vp = zone->zone_rootvp;
	zone_chdir(vp, &PTOU(pp)->u_cdir, pp);
	zone_chdir(vp, &PTOU(pp)->u_rdir, pp);

	/*
	 * Change process credentials
	 */
	newcr = cralloc();
	mutex_enter(&pp->p_crlock);
	cr = pp->p_cred;
	crcopy_to(cr, newcr);
	crsetzone(newcr, zone);
	pp->p_cred = newcr;

	/*
	 * Restrict all process privilege sets to zone limit
	 */
	priv_intersect(zone->zone_privset, &CR_PPRIV(newcr));
	priv_intersect(zone->zone_privset, &CR_EPRIV(newcr));
	priv_intersect(zone->zone_privset, &CR_IPRIV(newcr));
	priv_intersect(zone->zone_privset, &CR_LPRIV(newcr));
	mutex_exit(&pp->p_crlock);
	crset(pp, newcr);

	/*
	 * Adjust upcount to reflect zone entry.
	 */
	uid = crgetruid(newcr);
	mutex_enter(&pidlock);
	upcount_dec(uid, GLOBAL_ZONEID);
	upcount_inc(uid, zoneid);
	mutex_exit(&pidlock);

	/*
	 * Set up core file path and content.
	 */
	set_core_defaults();

out:
	/*
	 * Let the other lwps continue.
	 */
	mutex_enter(&pp->p_lock);
	if (curthread != pp->p_agenttp)
		continuelwps(pp);
	mutex_exit(&pp->p_lock);

	return (err != 0 ? set_errno(err) : 0);
}

static int
zone_list_defunct(uint64_t *uniqidlist, uint_t *numzones)
{
	zone_t *zone;
	uint64_t *uniqids;
	uint_t deathrow_count, defunct_count = 0;
	uint_t user_nzones;
	int error = 0;

	if (copyin(numzones, &user_nzones, sizeof (uint_t)) != 0)
		return (set_errno(EFAULT));

	if (!INGLOBALZONE(curproc)) {
		deathrow_count = 0;
		goto out;
	}

	mutex_enter(&zone_deathrow_lock);
	deathrow_count = zone_deathrow_count;
	if (deathrow_count == 0) {
		mutex_exit(&zone_deathrow_lock);
		goto out;
	}
	uniqids = kmem_alloc(deathrow_count * sizeof (*uniqids), KM_SLEEP);
	for (zone = list_head(&zone_deathrow); zone != NULL;
	    zone = list_next(&zone_deathrow, zone)) {
		mutex_enter(&zone->zone_lock);
		if (zone->zone_ref == 0) {
			/* skip the ones with only outstanding cred refs */
			ASSERT(zone->zone_cred_ref != 0);
		} else {
			uniqids[defunct_count++] = zone->zone_uniqid;
		}
		mutex_exit(&zone->zone_lock);
	}
	mutex_exit(&zone_deathrow_lock);

out:
	ASSERT(defunct_count <= deathrow_count);
	if (defunct_count < user_nzones)
		user_nzones = defunct_count;
	if (copyout(&defunct_count, numzones, sizeof (uint_t)) != 0) {
		error = EFAULT;
	} else if (uniqidlist != NULL && user_nzones != 0) {
		if (copyout(uniqids, uniqidlist,
		    user_nzones * sizeof (uint64_t)) != 0) {
			error = EFAULT;
		}
	}

	if (deathrow_count != 0)
		kmem_free(uniqids, deathrow_count * sizeof (*uniqids));
	if (error != 0)
		return (set_errno(error));
	return (0);
}

/*
 * Systemcall entry point for zone_list(2).
 *
 * Processes running in a (non-global) zone only see themselves.
 * On labeled systems, they see all zones whose label they dominate.
 */
static int
zone_list(zoneid_t *zoneidlist, uint_t *numzones)
{
	zoneid_t *zoneids;
	zone_t *zone, *myzone;
	uint_t user_nzones, real_nzones;
	uint_t domi_nzones;
	int error;

	if (copyin(numzones, &user_nzones, sizeof (uint_t)) != 0)
		return (set_errno(EFAULT));

	myzone = curproc->p_zone;
	if (myzone != global_zone) {
		bslabel_t *mybslab;

		if (!is_system_labeled()) {
			/* just return current zone */
			real_nzones = domi_nzones = 1;
			zoneids = kmem_alloc(sizeof (zoneid_t), KM_SLEEP);
			zoneids[0] = myzone->zone_id;
		} else {
			/* return all zones that are dominated */
			mutex_enter(&zonehash_lock);
			real_nzones = zonecount;
			domi_nzones = 0;
			if (real_nzones > 0) {
				zoneids = kmem_alloc(real_nzones *
				    sizeof (zoneid_t), KM_SLEEP);
				mybslab = label2bslabel(myzone->zone_slabel);
				for (zone = list_head(&zone_active);
				    zone != NULL;
				    zone = list_next(&zone_active, zone)) {
					if (zone->zone_id == GLOBAL_ZONEID)
						continue;
					if (zone != myzone &&
					    (zone->zone_flags & ZF_IS_SCRATCH))
						continue;
					/*
					 * Note that a label always dominates
					 * itself, so myzone is always included
					 * in the list.
					 */
					if (bldominates(mybslab,
					    label2bslabel(zone->zone_slabel))) {
						zoneids[domi_nzones++] =
						    zone->zone_id;
					}
				}
			}
			mutex_exit(&zonehash_lock);
		}
	} else {
		mutex_enter(&zonehash_lock);
		real_nzones = zonecount;
		domi_nzones = 0;
		if (real_nzones > 0) {
			zoneids = kmem_alloc(real_nzones * sizeof (zoneid_t),
			    KM_SLEEP);
			for (zone = list_head(&zone_active); zone != NULL;
			    zone = list_next(&zone_active, zone))
				zoneids[domi_nzones++] = zone->zone_id;
			ASSERT(domi_nzones == real_nzones);
		}
		mutex_exit(&zonehash_lock);
	}

	/*
	 * If user has allocated space for fewer entries than we found, then
	 * return only up to his limit.  Either way, tell him exactly how many
	 * we found.
	 */
	if (domi_nzones < user_nzones)
		user_nzones = domi_nzones;
	error = 0;
	if (copyout(&domi_nzones, numzones, sizeof (uint_t)) != 0) {
		error = EFAULT;
	} else if (zoneidlist != NULL && user_nzones != 0) {
		if (copyout(zoneids, zoneidlist,
		    user_nzones * sizeof (zoneid_t)) != 0)
			error = EFAULT;
	}

	if (real_nzones > 0)
		kmem_free(zoneids, real_nzones * sizeof (zoneid_t));

	if (error != 0)
		return (set_errno(error));
	else
		return (0);
}

/*
 * Systemcall entry point for zone_lookup(2).
 *
 * Non-global zones are only able to see themselves and (on labeled systems)
 * the zones they dominate.
 */
static zoneid_t
zone_lookup(const char *zone_name)
{
	char *kname;
	zone_t *zone;
	zoneid_t zoneid;
	int err;

	if (zone_name == NULL) {
		/* return caller's zone id */
		return (getzoneid());
	}

	kname = kmem_zalloc(ZONENAME_MAX, KM_SLEEP);
	if ((err = copyinstr(zone_name, kname, ZONENAME_MAX, NULL)) != 0) {
		kmem_free(kname, ZONENAME_MAX);
		return (set_errno(err));
	}

	mutex_enter(&zonehash_lock);
	zone = zone_find_all_by_name(kname);
	kmem_free(kname, ZONENAME_MAX);
	/*
	 * In a non-global zone, can only lookup global and own name.
	 * In Trusted Extensions zone label dominance rules apply.
	 */
	if (zone == NULL ||
	    zone_status_get(zone) < ZONE_IS_READY ||
	    !zone_list_access(zone)) {
		mutex_exit(&zonehash_lock);
		return (set_errno(EINVAL));
	} else {
		zoneid = zone->zone_id;
		mutex_exit(&zonehash_lock);
		return (zoneid);
	}
}

/* ARGSUSED */
long
zone(int cmd, void *arg1, void *arg2, void *arg3, void *arg4)
{
	zone_def zs;
	int err;

	switch (cmd) {
	case ZONE_CREATE:
		if (get_udatamodel() == DATAMODEL_NATIVE) {
			if (copyin(arg1, &zs, sizeof (zone_def))) {
				return (set_errno(EFAULT));
			}
		} else {
#ifdef _SYSCALL32_IMPL
			zone_def32 zs32;

			if (copyin(arg1, &zs32, sizeof (zone_def32))) {
				return (set_errno(EFAULT));
			}
			zs.zone_name =
			    (const char *)(unsigned long)zs32.zone_name;
			zs.zone_root =
			    (const char *)(unsigned long)zs32.zone_root;
			zs.zone_privs =
			    (const struct priv_set *)
			    (unsigned long)zs32.zone_privs;
			zs.zone_privssz = zs32.zone_privssz;
			zs.rctlbuf = (caddr_t)(unsigned long)zs32.rctlbuf;
			zs.rctlbufsz = zs32.rctlbufsz;
			zs.zfsbuf = (caddr_t)(unsigned long)zs32.zfsbuf;
			zs.zfsbufsz = zs32.zfsbufsz;
			zs.bezfsbuf = (caddr_t)(unsigned long)zs32.bezfsbuf;
			zs.bezfsbufsz = zs32.bezfsbufsz;
			zs.extended_error =
			    (int *)(unsigned long)zs32.extended_error;
			zs.match = zs32.match;
			zs.doi = zs32.doi;
			zs.label = (const bslabel_t *)(uintptr_t)zs32.label;
			zs.flags = zs32.flags;
#else
			panic("get_udatamodel() returned bogus result\n");
#endif
		}

		return (zone_create(zs.zone_name, zs.zone_root,
		    zs.zone_privs, zs.zone_privssz,
		    (caddr_t)zs.rctlbuf, zs.rctlbufsz,
		    (caddr_t)zs.zfsbuf, zs.zfsbufsz,
		    zs.extended_error, zs.match, zs.doi,
		    zs.label, zs.flags));
	case ZONE_BOOT:
		return (zone_boot((zoneid_t)(uintptr_t)arg1));
	case ZONE_DESTROY:
		return (zone_destroy((zoneid_t)(uintptr_t)arg1));
	case ZONE_GETATTR:
		return (zone_getattr((zoneid_t)(uintptr_t)arg1,
		    (int)(uintptr_t)arg2, arg3, (size_t)arg4));
	case ZONE_SETATTR:
		return (zone_setattr((zoneid_t)(uintptr_t)arg1,
		    (int)(uintptr_t)arg2, arg3, (size_t)arg4));
	case ZONE_ENTER:
		return (zone_enter((zoneid_t)(uintptr_t)arg1));
	case ZONE_LIST:
		return (zone_list((zoneid_t *)arg1, (uint_t *)arg2));
	case ZONE_SHUTDOWN:
		return (zone_shutdown((zoneid_t)(uintptr_t)arg1));
	case ZONE_LOOKUP:
		return (zone_lookup((const char *)arg1));
	case ZONE_ADD_DATALINK:
		return (zone_add_datalink((zoneid_t)(uintptr_t)arg1,
		    (datalink_id_t)(uintptr_t)arg2));
	case ZONE_DEL_DATALINK:
		return (zone_remove_datalink((zoneid_t)(uintptr_t)arg1,
		    (datalink_id_t)(uintptr_t)arg2));
	case ZONE_CHECK_DATALINK: {
		zoneid_t	zoneid;
		boolean_t	need_copyout;

		if (copyin(arg1, &zoneid, sizeof (zoneid)) != 0)
			return (set_errno(EFAULT));
		need_copyout = (zoneid == ALL_ZONES);
		err = zone_check_datalink(&zoneid,
		    (datalink_id_t)(uintptr_t)arg2);
		if (err == 0 && need_copyout) {
			if (copyout(&zoneid, arg1, sizeof (zoneid)) != 0)
				err = EFAULT;
		}
		return (err == 0 ? 0 : set_errno(err));
	}
	case ZONE_LIST_DATALINK:
		return (zone_list_datalink((zoneid_t)(uintptr_t)arg1,
		    (int *)arg2, (datalink_id_t *)(uintptr_t)arg3));
	case ZONE_LIST_DEFUNCT:
		return (zone_list_defunct((uint64_t *)arg1, (uint_t *)arg2));
	case ZONE_GETATTR_DEFUNCT: {
		uint64_t uniqid;

		if (copyin(arg1, &uniqid, sizeof (uniqid)) != 0)
			return (set_errno(EFAULT));
		return (zone_getattr_defunct(uniqid,
		    (int)(uintptr_t)arg2, arg3, (size_t)arg4));
	}
	default:
		return (set_errno(EINVAL));
	}
}

struct zarg {
	zone_t *zone;
	zone_cmd_arg_t arg;
};

static int
zone_lookup_door(const char *zone_name, door_handle_t *doorp)
{
	char *buf;
	size_t buflen;
	int error;

	buflen = sizeof (ZONE_DOOR_PATH) + strlen(zone_name);
	buf = kmem_alloc(buflen, KM_SLEEP);
	(void) snprintf(buf, buflen, ZONE_DOOR_PATH, zone_name);
	error = door_ki_open(buf, doorp);
	kmem_free(buf, buflen);
	return (error);
}

static void
zone_release_door(door_handle_t *doorp)
{
	door_ki_rele(*doorp);
	*doorp = NULL;
}

static void
zone_ki_call_zoneadmd(struct zarg *zargp)
{
	door_handle_t door = NULL;
	door_arg_t darg, save_arg;
	char *zone_name;
	size_t zone_namelen;
	zoneid_t zoneid;
	zone_t *zone;
	zone_cmd_arg_t arg;
	uint64_t uniqid;
	size_t size;
	int error;
	int retry;

	zone = zargp->zone;
	arg = zargp->arg;
	kmem_free(zargp, sizeof (*zargp));

	zone_namelen = strlen(zone->zone_name) + 1;
	zone_name = kmem_alloc(zone_namelen, KM_SLEEP);
	bcopy(zone->zone_name, zone_name, zone_namelen);
	zoneid = zone->zone_id;
	uniqid = zone->zone_uniqid;
	/*
	 * zoneadmd may be down, but at least we can empty out the zone.
	 * We can ignore the return value of zone_empty() since we're called
	 * from a kernel thread and know we won't be delivered any signals.
	 */
	ASSERT(curproc == &p0);
	(void) zone_empty(zone);
	ASSERT(zone_status_get(zone) >= ZONE_IS_EMPTY);
	zone_rele(zone);

	size = sizeof (arg);
	darg.rbuf = (char *)&arg;
	darg.data_ptr = (char *)&arg;
	darg.rsize = size;
	darg.data_size = size;
	darg.desc_ptr = NULL;
	darg.desc_num = 0;

	save_arg = darg;
	/*
	 * Since we're not holding a reference to the zone, any number of
	 * things can go wrong, including the zone disappearing before we get a
	 * chance to talk to zoneadmd.
	 */
	for (retry = 0; /* forever */; retry++) {
		if (door == NULL &&
		    (error = zone_lookup_door(zone_name, &door)) != 0) {
			goto next;
		}
		ASSERT(door != NULL);

		if ((error = door_ki_upcall_limited(door, &darg, NULL,
		    SIZE_MAX, 0)) == 0) {
			break;
		}
		switch (error) {
		case EINTR:
			/* FALLTHROUGH */
		case EAGAIN:	/* process may be forking */
			/*
			 * Back off for a bit
			 */
			break;
		case EBADF:
			zone_release_door(&door);
			if (zone_lookup_door(zone_name, &door) != 0) {
				/*
				 * zoneadmd may be dead, but it may come back to
				 * life later.
				 */
				break;
			}
			break;
		default:
			cmn_err(CE_WARN,
			    "zone_ki_call_zoneadmd: door_ki_upcall error %d\n",
			    error);
			goto out;
		}
next:
		/*
		 * If this isn't the same zone_t that we originally had in mind,
		 * then this is the same as if two kadmin requests come in at
		 * the same time: the first one wins.  This means we lose, so we
		 * bail.
		 */
		if ((zone = zone_find_by_id(zoneid)) == NULL) {
			/*
			 * Problem is solved.
			 */
			break;
		}
		if (zone->zone_uniqid != uniqid) {
			/*
			 * zoneid recycled
			 */
			zone_rele(zone);
			break;
		}
		/*
		 * We could zone_status_timedwait(), but there doesn't seem to
		 * be much point in doing that (plus, it would mean that
		 * zone_free() isn't called until this thread exits).
		 */
		zone_rele(zone);
		delay(hz);
		darg = save_arg;
	}
out:
	if (door != NULL) {
		zone_release_door(&door);
	}
	kmem_free(zone_name, zone_namelen);
	thread_exit();
}

/*
 * Entry point for uadmin() to tell the zone to go away or reboot.  Analog to
 * kadmin().  The caller is a process in the zone.
 *
 * In order to shutdown the zone, we will hand off control to zoneadmd
 * (running in the global zone) via a door.  We do a half-hearted job at
 * killing all processes in the zone, create a kernel thread to contact
 * zoneadmd, and make note of the "uniqid" of the zone.  The uniqid is
 * a form of generation number used to let zoneadmd (as well as
 * zone_destroy()) know exactly which zone they're re talking about.
 */
int
zone_kadmin(int cmd, int fcn, const char *mdep, cred_t *credp)
{
	struct zarg *zargp;
	zone_cmd_t zcmd;
	zone_t *zone;

	zone = curproc->p_zone;
	ASSERT(getzoneid() != GLOBAL_ZONEID);

	switch (cmd) {
	case A_SHUTDOWN:
	case A_REBOOT:
		switch (fcn) {
		case AD_HALT:
		case AD_POWEROFF:
			zcmd = Z_HALT;
			break;
		case AD_BOOT:
		case AD_FASTREBOOT:
			zcmd = Z_REBOOT;
			break;
		case AD_IBOOT:
		case AD_SBOOT:
		case AD_SIBOOT:
		case AD_NOSYNC:
			return (ENOTSUP);
		default:
			return (EINVAL);
		}
		break;
	case A_FTRACE:
	case A_REMOUNT:
	case A_FREEZE:
	case A_DUMP:
	case A_CONFIG:
		return (ENOTSUP);
	default:
		ASSERT(cmd != A_SWAPCTL);	/* handled by uadmin() */
		return (EINVAL);
	}

	if (secpolicy_zone_admin(credp, B_FALSE))
		return (EPERM);
	mutex_enter(&zone->zone_lock);

	/*
	 * zone_status can't be ZONE_IS_EMPTY or higher since curproc
	 * is in the zone.
	 */
	ASSERT(zone_status_get(zone) < ZONE_IS_EMPTY);
	if (zone_status_get(zone) > ZONE_IS_RUNNING) {
		/*
		 * This zone is already on its way down.
		 */
		mutex_exit(&zone->zone_lock);
		return (0);
	}
	/*
	 * Prevent future zone_enter()s
	 */
	zone_status_set(zone, ZONE_IS_SHUTTING_DOWN);
	mutex_exit(&zone->zone_lock);

	/*
	 * Kill everyone now and call zoneadmd later.
	 * zone_ki_call_zoneadmd() will do a more thorough job of this
	 * later.
	 */
	killall(zone->zone_id);
	/*
	 * Now, create the thread to contact zoneadmd and do the rest of the
	 * work.  This thread can't be created in our zone otherwise
	 * zone_destroy() would deadlock.
	 */
	zargp = kmem_zalloc(sizeof (*zargp), KM_SLEEP);
	zargp->arg.cmd = zcmd;
	zargp->arg.flags = Z_CMD_FLAG_NONE;
	zargp->arg.uniqid = zone->zone_uniqid;
	zargp->zone = zone;
	(void) strcpy(zargp->arg.locale, "C");
	/* mdep was already copied in for us by uadmin */
	if (mdep != NULL)
		(void) strlcpy(zargp->arg.bootbuf, mdep,
		    sizeof (zargp->arg.bootbuf));
	zone_hold(zone); /* ref dropped by zone_ki_call_zoneadmd */

	(void) thread_create(NULL, 0, zone_ki_call_zoneadmd, zargp, 0, &p0,
	    TS_RUN, minclsyspri);
	exit(CLD_EXITED, 0);

	return (EINVAL);
}

/*
 * Entry point so kadmin(A_SHUTDOWN, ...) can set the global zone's
 * status to ZONE_IS_SHUTTING_DOWN.
 *
 * This function also shuts down all running zones to ensure that they won't
 * fork new processes.
 */
void
zone_shutdown_global(void)
{
	ASSERT(INGLOBALZONE(curproc));

	/*
	 * Set the global zone's status. getproc() in the fork() path
	 * will refuse to create new processes if it sees the global
	 * zone is going away, so we don't need to worry about forkbombs
	 * preventing the system from shutting down.
	 */
	mutex_enter(&global_zone->zone_lock);
	ASSERT(zone_status_get(global_zone) == ZONE_IS_RUNNING);
	zone_status_set(global_zone, ZONE_IS_SHUTTING_DOWN);
	mutex_exit(&global_zone->zone_lock);
}

/*
 * Returns true if the named dataset is visible in the given zone.
 *
 *    zone		The zone for which dataset access is being checked.
 *    dataset		The unaliased dataset name.
 *    write		If not NULL, it is set to 1 if the dataset is writable
 *    			or 0 if it is not writable.
 */
int
zone_dataset_visible(zone_t *zone, const char *dataset, int *write)
{
	zone_dataset_t	*zd;		/* Current dataset in the list */
	size_t		dslen;		/* Length of dataset passed in */
	size_t		curlen;		/* Length of current dataset */

	if (zone == global_zone) {
		if (write)
			*write = 1;
		return (1);
	}

	if (dataset[0] == '\0')
		return (0);

	/* Ingore trailing '/' if it exists */
	dslen = strlen(dataset);
	if (dataset[dslen - 1] == '/')
		dslen--;

	/*
	 * Datasets may be configured as platform datasets in platform.xml
	 * or as dataset resources via zonecfg.  The following rules apply:
	 *
	 * - All of these datasets and their hierarchical descendants are
	 *   at least readable.
	 * - Dataset resources specified in zonecfg and their hierarchical
	 *   descendants are writable.
	 * - Platform datasets may be read-only, subject to mandatory write
	 *   access control (MWAC).
	 *
	 * It is not possible for a delegated dataset and its hierarchical
	 * descendent to both be delegated.  As such, as soon as a match is
	 * found, we are done looking.
	 */
	for (zd = list_head(&zone->zone_datasets); zd != NULL;
	    zd = list_next(&zone->zone_datasets, zd)) {

		curlen = strlen(zd->zd_dataset);

		/* Start the process of elimination */
		if (dslen < curlen)
			continue;
		if (strncmp(dataset, zd->zd_dataset, curlen) != 0)
			continue;
		if (dataset[curlen] != '\0' && dataset[curlen] != '/' &&
		    dataset[curlen] != '@')
			continue;

		/*
		 * Found a match - we know it at least has read access.  If
		 * the caller doesn't care about write access, return
		 * immediately.
		 */
		if (write == NULL)
			return (1);

		if (zd->zd_cfgsrc == ZONE_DS_CFG_PLATFORM) {
			/* If not locked down, it is writable */
			if ((zone->zone_flags & ZF_LOCKDOWN) == 0)
				*write = 1;
			else
				*write = 0;
			return (1);
		}

		/* Not a platform dataset - it has write access */
		ASSERT(zd->zd_cfgsrc == ZONE_DS_CFG_DELEGATED);
		*write = 1;
		return (1);
	}

	return (0);
}

/*
 * Performs a conversion of a dataset name from the real name to the aliased
 * name.  sz is the size of the "alias" buffer.  If dataset and alias
 * overlap, dataset and alias must reference the same address.  The function
 * returns the number of bytes that would have been written into the buffer if
 * it had been sufficiently large.
 *
 * It is expected that the alias name will be copied to user space and to
 * protect against leaking data, the buffer beyond the end of the result
 * string is cleared.
 */
int
zone_dataset_alias(zone_t *zone, const char *dataset, char *alias, size_t sz)
{
	zone_dataset_t *zd;
	size_t len = 0;

	if (zone == global_zone)
		return (0);
	if (dataset[0] == '\0')
		goto out;

	/*
	 * Walk the dataset list looking for a dataset name that matches.
	 */
	for (zd = list_head(&zone->zone_datasets); zd != NULL;
	    zd = list_next(&zone->zone_datasets, zd)) {
		len = strlen(zd->zd_dataset);
		if (strlen(dataset) >= len &&
		    strncmp(dataset, zd->zd_dataset, len) == 0 &&
		    (dataset[len] == '\0' || dataset[len] == '/' ||
		    dataset[len] == '@')) {
			/*
			 * Dataset names are typically passed to the kernel
			 * via zfs_cmd_t's zc_name.  dataset_c should be
			 * the same size or larger than zc_name.
			 */
			char *dataset_c = NULL;
			const char *src = dataset;
			/*
			 * If modifying in place, make a copy of the original
			 * to ensure that snprintf doesn't write before reading.
			 */
			if (dataset == alias) {
				dataset_c = strdup(dataset);
				src = dataset_c;
			}
			if ((len = snprintf(alias, sz, "%s%s", zd->zd_alias,
			    &src[len])) < sz)
				bzero(&alias[len], sz - len);
			if (dataset_c != NULL)
				strfree(dataset_c);
			return (len);
		}
	}

	/*
	 * No match.  Give a special value to indicate it comes from outside
	 * of the visible namespace.
	 */
	len = strlcpy(alias, ZONE_INVISIBLE_SOURCE, sz);
out:
	if (len < sz)
		bzero(&alias[len], sz - len);
	return (len);
}

/*
 * Performs a conversion of an aliased dataset name to the real name.  sz is
 * the size of the "dataset" buffer.  If dataset and alias overlap, dataset
 * and alias must reference the same address.  The function returns the number
 * of bytes that would have been written into the buffer if it had been
 * sufficiently large.
 */
int
zone_dataset_unalias(zone_t *zone, const char *alias, char *dataset,
    size_t sz)
{
	zone_dataset_t *zd;
	size_t len;

	if (zone == global_zone || alias[0] == '\0')
		return (0);

	/*
	 * Walk the dataset list looking for a dataset name that has an alias
	 * that matches the first component of alias.
	 */
	for (zd = list_head(&zone->zone_datasets); zd != NULL;
	    zd = list_next(&zone->zone_datasets, zd)) {
		len = strlen(zd->zd_alias);
		if (strlen(alias) >= len &&
		    strncmp(alias, zd->zd_alias, len) == 0 &&
		    (alias[len] == '\0' || alias[len] == '/' ||
		    alias[len] == '@')) {
			/*
			 * Dataset names are typically passed to the kernel
			 * via zfs_cmd_t's zc_name.  dataset_c should be
			 * the same size or larger than zc_name.
			 */
			char alias_c[MAXPATHLEN];
			const char *src = alias;
			/*
			 * If modifying in place, make a copy of the original
			 * to ensure that snprintf doesn't write before reading.
			 */
			if (dataset == alias) {
				(void) strlcpy(alias_c, alias,
				    sizeof (alias_c));
				src = alias_c;
			}
			return (snprintf(dataset, sz, "%s%s", zd->zd_dataset,
			    &src[len]));
		}
	}

	/*
	 * No match.  Just copy if needed.  Future calls to
	 * zone_dataset_visible() will sort out whether access should be
	 * granted.
	 */
	if (dataset != alias)
		return (strlcpy(dataset, alias, sz));
	else
		return (0);
}

/*
 * zone_find_by_any_path() -
 *
 * kernel-private routine similar to zone_find_by_path(), but which
 * effectively compares against zone paths rather than zonerootpath
 * (i.e., the last component of zonerootpaths, which should be "root/",
 * are not compared.)  This is done in order to accurately identify all
 * paths, whether zone-visible or not, including those which are parallel
 * to /root/, such as /dev/, /home/, etc...
 *
 * If the specified path does not fall under any zone path then global
 * zone is returned.
 *
 * The treat_abs parameter indicates whether the path should be treated as
 * an absolute path although it does not begin with "/".  (This supports
 * nfs mount syntax such as host:any/path.)
 *
 * The caller is responsible for zone_rele of the returned zone.
 */
zone_t *
zone_find_by_any_path(const char *path, boolean_t treat_abs)
{
	zone_t *zone;
	int path_offset = 0;

	if (path == NULL) {
		zone_hold(global_zone);
		return (global_zone);
	}

	if (*path != '/') {
		ASSERT(treat_abs);
		path_offset = 1;
	}

	mutex_enter(&zonehash_lock);
	for (zone = list_head(&zone_active); zone != NULL;
	    zone = list_next(&zone_active, zone)) {
		char	*c;
		size_t	pathlen;
		char *rootpath_start;

		if (zone == global_zone)	/* skip global zone */
			continue;

		/* scan backwards to find start of last component */
		c = zone->zone_rootpath + zone->zone_rootpathlen - 2;
		do {
			c--;
		} while (*c != '/');

		pathlen = c - zone->zone_rootpath + 1 - path_offset;
		rootpath_start = (zone->zone_rootpath + path_offset);
		if (strncmp(path, rootpath_start, pathlen) == 0)
			break;
	}
	if (zone == NULL)
		zone = global_zone;
	zone_hold(zone);
	mutex_exit(&zonehash_lock);
	return (zone);
}

/*
 * Finds a zone_dl_t with the given linkid in the given zone.  Returns the
 * zone_dl_t pointer if found, and NULL otherwise.
 */
static zone_dl_t *
zone_find_dl(zone_t *zone, datalink_id_t linkid)
{
	zone_dl_t *zdl;

	ASSERT(MUTEX_HELD(&zone->zone_lock));
	for (zdl = list_head(&zone->zone_dl_list); zdl != NULL;
	    zdl = list_next(&zone->zone_dl_list, zdl)) {
		if (zdl->zdl_id == linkid)
			break;
	}
	return (zdl);
}

static boolean_t
zone_dl_exists(zone_t *zone, datalink_id_t linkid)
{
	boolean_t exists;

	mutex_enter(&zone->zone_lock);
	exists = (zone_find_dl(zone, linkid) != NULL);
	mutex_exit(&zone->zone_lock);
	return (exists);
}

/*
 * Add an data link name for the zone.
 */
static int
zone_add_datalink(zoneid_t zoneid, datalink_id_t linkid)
{
	zone_dl_t *zdl;
	zone_t *zone;
	zone_t *thiszone;

	if (secpolicy_dl_config(CRED()) != 0)
		return (set_errno(EPERM));
	/* Operation not supported for global zone */
	if (zoneid == GLOBAL_ZONEID)
		return (set_errno(ENOTSUP));

	if (!INGLOBALZONE(curproc) && zoneid != getzoneid())
		return (set_errno(ENXIO));
	if ((thiszone = zone_find_by_id(zoneid)) == NULL)
		return (set_errno(ENXIO));

	/* Verify that the datalink ID doesn't already belong to a zone. */
	mutex_enter(&zonehash_lock);
	for (zone = list_head(&zone_active); zone != NULL;
	    zone = list_next(&zone_active, zone)) {
		if (zone_dl_exists(zone, linkid)) {
			mutex_exit(&zonehash_lock);
			zone_rele(thiszone);
			return (set_errno((zone == thiszone) ? EEXIST : EPERM));
		}
	}

	zdl = kmem_zalloc(sizeof (*zdl), KM_SLEEP);
	zdl->zdl_id = linkid;
	zdl->zdl_net = NULL;
	mutex_enter(&thiszone->zone_lock);
	list_insert_head(&thiszone->zone_dl_list, zdl);
	mutex_exit(&thiszone->zone_lock);
	mutex_exit(&zonehash_lock);
	zone_rele(thiszone);
	return (0);
}

static int
zone_remove_datalink(zoneid_t zoneid, datalink_id_t linkid)
{
	zone_dl_t *zdl;
	zone_t *zone;
	int err = 0;

	if (secpolicy_dl_config(CRED()) != 0)
		return (set_errno(EPERM));

	/* Operation not supported for global zone */
	if (zoneid == GLOBAL_ZONEID)
		return (set_errno(ENOTSUP));

	if (!INGLOBALZONE(curproc) && zoneid != getzoneid())
		return (set_errno(EINVAL));
	if ((zone = zone_find_by_id(zoneid)) == NULL)
		return (set_errno(EINVAL));

	mutex_enter(&zone->zone_lock);
	if ((zdl = zone_find_dl(zone, linkid)) == NULL) {
		err = ENXIO;
	} else {
		list_remove(&zone->zone_dl_list, zdl);
		if (zdl->zdl_net != NULL)
			nvlist_free(zdl->zdl_net);
		kmem_free(zdl, sizeof (zone_dl_t));
	}
	mutex_exit(&zone->zone_lock);
	zone_rele(zone);
	return (err == 0 ? 0 : set_errno(err));
}

/*
 * Using the zoneidp as ALL_ZONES, we can lookup which zone has been assigned
 * the linkid.  Otherwise we just check if the specified zoneidp has been
 * assigned the supplied linkid. This check doesn't work against global zone.
 */
int
zone_check_datalink(zoneid_t *zoneidp, datalink_id_t linkid)
{
	zone_t *zone;
	int err = ENXIO;

	/* Operation not supported for global zone */
	if (*zoneidp == GLOBAL_ZONEID)
		return (ENOTSUP);

	/* Don't report information about other zones */
	if (!INGLOBALZONE(curproc)) {
		if (*zoneidp != ALL_ZONES && *zoneidp != getzoneid())
			return (ENXIO);
		/* only tell whether the link is in the zone */
		if (zone_dl_exists(curproc->p_zone, linkid))
			return (0);
		return (ENXIO);
	}

	if (*zoneidp != ALL_ZONES) {
		if ((zone = zone_find_by_id(*zoneidp)) != NULL) {
			if (zone_dl_exists(zone, linkid))
				err = 0;
			zone_rele(zone);
		}
		return (err);
	}

	mutex_enter(&zonehash_lock);
	for (zone = list_head(&zone_active); zone != NULL;
	    zone = list_next(&zone_active, zone)) {
		if (zone_dl_exists(zone, linkid)) {
			*zoneidp = zone->zone_id;
			err = 0;
			break;
		}
	}
	mutex_exit(&zonehash_lock);
	return (err);
}

/*
 * Get the list of datalink IDs assigned to a zone.
 *
 * On input, *nump is the number of datalink IDs that can fit in the supplied
 * idarray.  Upon return, *nump is either set to the number of datalink IDs
 * that were placed in the array if the array was large enough, or to the
 * number of datalink IDs that the function needs to place in the array if the
 * array is too small.
 */
static int
zone_list_datalink(zoneid_t zoneid, int *nump, datalink_id_t *idarray)
{
	uint_t num, dlcount;
	zone_t *zone;
	zone_dl_t *zdl;
	datalink_id_t *idptr = idarray;

	/*
	 * Operation not supported for global zone and we don't tell
	 * anything about other zones if we're in a non-global zone.
	 */
	if (zoneid == GLOBAL_ZONEID)
		return (set_errno(ENOTSUP));
	if (!INGLOBALZONE(curproc) && zoneid != getzoneid())
		return (set_errno(ENXIO));

	if (copyin(nump, &dlcount, sizeof (dlcount)) != 0)
		return (set_errno(EFAULT));
	if ((zone = zone_find_by_id(zoneid)) == NULL)
		return (set_errno(ENXIO));

	num = 0;
	mutex_enter(&zone->zone_lock);
	for (zdl = list_head(&zone->zone_dl_list); zdl != NULL;
	    zdl = list_next(&zone->zone_dl_list, zdl)) {
		/*
		 * If the list is bigger than what the caller supplied, just
		 * count, don't do copyout.
		 */
		if (++num > dlcount)
			continue;
		if (copyout(&zdl->zdl_id, idptr, sizeof (*idptr)) != 0) {
			mutex_exit(&zone->zone_lock);
			zone_rele(zone);
			return (set_errno(EFAULT));
		}
		idptr++;
	}
	mutex_exit(&zone->zone_lock);
	zone_rele(zone);

	/* Increased or decreased, caller should be notified. */
	if (num != dlcount) {
		if (copyout(&num, nump, sizeof (num)) != 0)
			return (set_errno(EFAULT));
	}
	return (0);
}

/*
 * Walk the datalinks for a given zone. Please note this walker
 * does not work for the global zone.
 */
int
zone_datalink_walk(zoneid_t zoneid, int (*cb)(datalink_id_t, void *),
    void *data)
{
	zone_t		*zone;
	zone_dl_t	*zdl;
	datalink_id_t	*idarray;
	uint_t		idcount = 0;
	int		i, ret = 0;

	ASSERT(zoneid != GLOBAL_ZONEID);

	/* Don't report information about other zones */
	if (!INGLOBALZONE(curproc) && zoneid != getzoneid())
		return (ENOENT);
	if ((zone = zone_find_by_id(zoneid)) == NULL)
		return (ENOENT);

	/*
	 * We first build an array of linkid's so that we can walk these and
	 * execute the callback with the zone_lock dropped.
	 */
	mutex_enter(&zone->zone_lock);
	for (zdl = list_head(&zone->zone_dl_list); zdl != NULL;
	    zdl = list_next(&zone->zone_dl_list, zdl)) {
		idcount++;
	}

	if (idcount == 0) {
		mutex_exit(&zone->zone_lock);
		zone_rele(zone);
		return (0);
	}

	idarray = kmem_alloc(sizeof (datalink_id_t) * idcount, KM_NOSLEEP);
	if (idarray == NULL) {
		mutex_exit(&zone->zone_lock);
		zone_rele(zone);
		return (ENOMEM);
	}

	for (i = 0, zdl = list_head(&zone->zone_dl_list); zdl != NULL;
	    i++, zdl = list_next(&zone->zone_dl_list, zdl)) {
		idarray[i] = zdl->zdl_id;
	}

	mutex_exit(&zone->zone_lock);

	for (i = 0; i < idcount && ret == 0; i++) {
		if ((ret = (*cb)(idarray[i], data)) != 0)
			break;
	}

	zone_rele(zone);
	kmem_free(idarray, sizeof (datalink_id_t) * idcount);
	return (ret);
}

static char *
zone_net_type2name(int type)
{
	switch (type) {
	case ZONE_NETWORK_ADDRESS:
		return (ZONE_NET_ADDRNAME);
	case ZONE_NETWORK_DEFROUTER:
		return (ZONE_NET_RTRNAME);
	default:
		return (NULL);
	}
}

static int
zone_set_network(zoneid_t zoneid, zone_net_data_t *znbuf)
{
	zone_t *zone;
	zone_dl_t *zdl;
	nvlist_t *nvl;
	int err = 0;
	uint8_t *new = NULL;
	char *nvname;
	int bufsize;
	datalink_id_t linkid = znbuf->zn_linkid;

	if (secpolicy_zone_config(CRED()) != 0)
		return (set_errno(EPERM));

	if (zoneid == GLOBAL_ZONEID)
		return (set_errno(EINVAL));

	nvname = zone_net_type2name(znbuf->zn_type);
	bufsize = znbuf->zn_len;
	new = znbuf->zn_val;
	if (nvname == NULL)
		return (set_errno(EINVAL));

	if ((zone = zone_find_by_id(zoneid)) == NULL) {
		return (set_errno(EINVAL));
	}

	mutex_enter(&zone->zone_lock);
	if ((zdl = zone_find_dl(zone, linkid)) == NULL) {
		err = ENXIO;
		goto done;
	}
	if ((nvl = zdl->zdl_net) == NULL) {
		if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP)) {
			err = ENOMEM;
			goto done;
		} else {
			zdl->zdl_net = nvl;
		}
	}
	if (nvlist_exists(nvl, nvname)) {
		err = EINVAL;
		goto done;
	}
	err = nvlist_add_uint8_array(nvl, nvname, new, bufsize);
	ASSERT(err == 0);
done:
	mutex_exit(&zone->zone_lock);
	zone_rele(zone);
	if (err != 0)
		return (set_errno(err));
	else
		return (0);
}

static int
zone_get_network(zoneid_t zoneid, zone_net_data_t *znbuf)
{
	zone_t *zone;
	zone_dl_t *zdl;
	nvlist_t *nvl;
	uint8_t *ptr;
	uint_t psize;
	int err = 0;
	char *nvname;
	int bufsize;
	void *buf;
	datalink_id_t linkid = znbuf->zn_linkid;

	if (zoneid == GLOBAL_ZONEID)
		return (set_errno(EINVAL));

	nvname = zone_net_type2name(znbuf->zn_type);
	bufsize = znbuf->zn_len;
	buf = znbuf->zn_val;

	if (nvname == NULL)
		return (set_errno(EINVAL));
	if ((zone = zone_find_by_id(zoneid)) == NULL)
		return (set_errno(EINVAL));

	mutex_enter(&zone->zone_lock);
	if ((zdl = zone_find_dl(zone, linkid)) == NULL) {
		err = ENXIO;
		goto done;
	}
	if ((nvl = zdl->zdl_net) == NULL || !nvlist_exists(nvl, nvname)) {
		err = ENOENT;
		goto done;
	}
	err = nvlist_lookup_uint8_array(nvl, nvname, &ptr, &psize);
	ASSERT(err == 0);

	if (psize > bufsize) {
		err = ENOBUFS;
		goto done;
	}
	znbuf->zn_len = psize;
	bcopy(ptr, buf, psize);
done:
	mutex_exit(&zone->zone_lock);
	zone_rele(zone);
	if (err != 0)
		return (set_errno(err));
	else
		return (0);
}
