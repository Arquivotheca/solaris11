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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * MAC Services Module
 *
 * The GLDv3 framework locking -  The MAC layer
 * --------------------------------------------
 *
 * The MAC layer is central to the GLD framework and can provide the locking
 * framework needed for itself and for the use of MAC clients. MAC end points
 * are fairly disjoint and don't share a lot of state. So a coarse grained
 * multi-threading scheme is to single thread all create/modify/delete or set
 * type of control operations on a per mac end point while allowing data threads
 * concurrently.
 *
 * Control operations (set) that modify a mac end point are always serialized on
 * a per mac end point basis, We have at most 1 such thread per mac end point
 * at a time.
 *
 * All other operations that are not serialized are essentially multi-threaded.
 * For example a control operation (get) like getting statistics which may not
 * care about reading values atomically or data threads sending or receiving
 * data. Mostly these type of operations don't modify the control state. Any
 * state these operations care about are protected using traditional locks.
 *
 * The perimeter only serializes serial operations. It does not imply there
 * aren't any other concurrent operations. However a serialized operation may
 * sometimes need to make sure it is the only thread. In this case it needs
 * to use reference counting mechanisms to cv_wait until any current data
 * threads are done.
 *
 * The mac layer itself does not hold any locks across a call to another layer.
 * The perimeter is however held across a down call to the driver to make the
 * whole control operation atomic with respect to other control operations.
 * Also the data path and get type control operations may proceed concurrently.
 * These operations synchronize with the single serial operation on a given mac
 * end point using regular locks. The perimeter ensures that conflicting
 * operations like say a mac_multicast_add and a mac_multicast_remove on the
 * same mac end point don't interfere with each other and also ensures that the
 * changes in the mac layer and the call to the underlying driver to say add a
 * multicast address are done atomically without interference from a thread
 * trying to delete the same address.
 *
 * For example, consider
 * mac_multicst_add()
 * {
 *	mac_perimeter_enter();	serialize all control operations
 *
 *	grab list lock		protect against access by data threads
 *	add to list
 *	drop list lock
 *
 *	call driver's mi_multicst
 *
 *	mac_perimeter_exit();
 * }
 *
 * To lessen the number of serialization locks and simplify the lock hierarchy,
 * we serialize all the control operations on a per mac end point by using a
 * single serialization lock called the perimeter. We allow recursive entry into
 * the perimeter to facilitate use of this mechanism by both the mac client and
 * the MAC layer itself.
 *
 * MAC client means an entity that does an operation on a mac handle
 * obtained from a mac_open/mac_client_open. Similarly MAC driver means
 * an entity that does an operation on a mac handle obtained from a
 * mac_register. An entity could be both client and driver but on different
 * handles eg. aggr. and should only make the corresponding mac interface calls
 * i.e. mac driver interface or mac client interface as appropriate for that
 * mac handle.
 *
 * General rules.
 * -------------
 *
 * R1. The lock order of upcall threads is natually opposite to downcall
 * threads. Hence upcalls must not hold any locks across layers for fear of
 * recursive lock enter and lock order violation. This applies to all layers.
 *
 * R2. The perimeter is just another lock. Since it is held in the down
 * direction, acquiring the perimeter in an upcall is prohibited as it would
 * cause a deadlock. This applies to all layers.
 *
 * Note that upcalls that need to grab the mac perimeter (for example
 * mac_notify upcalls) can still achieve that by posting the request to a
 * thread, which can then grab all the required perimeters and locks in the
 * right global order. Note that in the above example the mac layer iself
 * won't grab the mac perimeter in the mac_notify upcall, instead the upcall
 * to the client must do that. Please see the aggr code for an example.
 *
 * MAC client rules
 * ----------------
 *
 * R3. A MAC client may use the MAC provided perimeter facility to serialize
 * control operations on a per mac end point. It does this by by acquring
 * and holding the perimeter across a sequence of calls to the mac layer.
 * This ensures atomicity across the entire block of mac calls. In this
 * model the MAC client must not hold any client locks across the calls to
 * the mac layer. This model is the preferred solution.
 *
 * R4. However if a MAC client has a lot of global state across all mac end
 * points the per mac end point serialization may not be sufficient. In this
 * case the client may choose to use global locks or use its own serialization.
 * To avoid deadlocks, these client layer locks held across the mac calls
 * in the control path must never be acquired by the data path for the reason
 * mentioned below.
 *
 * (Assume that a control operation that holds a client lock blocks in the
 * mac layer waiting for upcall reference counts to drop to zero. If an upcall
 * data thread that holds this reference count, tries to acquire the same
 * client lock subsequently it will deadlock).
 *
 * A MAC client may follow either the R3 model or the R4 model, but can't
 * mix both. In the former, the hierarchy is Perim -> client locks, but in
 * the latter it is client locks -> Perim.
 *
 * R5. MAC clients must make MAC calls (excluding data calls) in a cv_wait'able
 * context since they may block while trying to acquire the perimeter.
 * In addition some calls may block waiting for upcall refcnts to come down to
 * zero.
 *
 * R6. MAC clients must make sure that they are single threaded and all threads
 * from the top (in particular data threads) have finished before calling
 * mac_client_close. The MAC framework does not track the number of client
 * threads using the mac client handle. Also mac clients must make sure
 * they have undone all the control operations before calling mac_client_close.
 * For example mac_unicast_remove/mac_multicast_remove to undo the corresponding
 * mac_unicast_add/mac_multicast_add.
 *
 * MAC framework rules
 * -------------------
 *
 * R7. The mac layer itself must not hold any mac layer locks (except the mac
 * perimeter) across a call to any other layer from the mac layer. The call to
 * any other layer could be via mi_* entry points, classifier entry points into
 * the driver or via upcall pointers into layers above. The mac perimeter may
 * be acquired or held only in the down direction, for e.g. when calling into
 * a mi_* driver enty point to provide atomicity of the operation.
 *
 * R8. Since it is not guaranteed (see R14) that drivers won't hold locks across
 * mac driver interfaces, the MAC layer must provide a cut out for control
 * interfaces like upcall notifications and start them in a separate thread.
 *
 * R9. Note that locking order also implies a plumbing order. For example
 * VNICs are allowed to be created over aggrs, but not vice-versa. An attempt
 * to plumb in any other order must be failed at mac_open time, otherwise it
 * could lead to deadlocks due to inverse locking order.
 *
 * R10. MAC driver interfaces must not block since the driver could call them
 * in interrupt context.
 *
 * R11. Walkers must preferably not hold any locks while calling walker
 * callbacks. Instead these can operate on reference counts. In simple
 * callbacks it may be ok to hold a lock and call the callbacks, but this is
 * harder to maintain in the general case of arbitrary callbacks.
 *
 * R12. The MAC layer must protect upcall notification callbacks using reference
 * counts rather than holding locks across the callbacks.
 *
 * R13. Given the variety of drivers, it is preferable if the MAC layer can make
 * sure that any pointers (such as mac ring pointers) it passes to the driver
 * remain valid until mac unregister time. Currently the mac layer achieves
 * this by using generation numbers for rings and freeing the mac rings only
 * at unregister time.  The MAC layer must provide a layer of indirection and
 * must not expose underlying driver rings or driver data structures/pointers
 * directly to MAC clients.
 *
 * MAC driver rules
 * ----------------
 *
 * R14. It would be preferable if MAC drivers don't hold any locks across any
 * mac call. However at a minimum they must not hold any locks across data
 * upcalls. They must also make sure that all references to mac data structures
 * are cleaned up and that it is single threaded at mac_unregister time.
 *
 * R15. MAC driver interfaces don't block and so the action may be done
 * asynchronously in a separate thread as for example handling notifications.
 * The driver must not assume that the action is complete when the call
 * returns.
 *
 * R16. Drivers must maintain a generation number per Rx ring, and pass it
 * back to mac_rx_ring(); They are expected to increment the generation
 * number whenever the ring's stop routine is invoked.
 * See comments in mac_rx_ring();
 *
 * R17 Similarly mi_stop is another synchronization point and the driver must
 * ensure that all upcalls are done and there won't be any future upcall
 * before returning from mi_stop.
 *
 * R18. The driver may assume that all set/modify control operations via
 * the mi_* entry points are single threaded on a per mac end point.
 *
 * Lock and Perimeter hierarchy scenarios
 * ---------------------------------------
 *
 * ft_lock -> fe_lock [mac_flow_lookup]
 *
 * mi_rw_lock -> fe_lock [mac_bcast_send]
 *
 * i_dls_devnet_lock -> mac layer locks [dls_devnet_rename]
 *
 * Perimeters are ordered P1 -> P2 -> P3 from top to bottom in order of mac
 * client to driver. In the case of clients that explictly use the mac provided
 * perimeter mechanism for its serialization, the hierarchy is
 * Perimeter -> mac layer locks, since the client never holds any locks across
 * the mac calls. In the case of clients that use its own locks the hierarchy
 * is Client locks -> Mac Perim -> Mac layer locks. The client never explicitly
 * calls mac_perim_enter/exit in this case.
 *
 * Subflow creation rules
 * ---------------------------
 * o In case of a user specified cpulist present on underlying link and flows,
 * the flows cpulist must be a subset of the underlying link.
 * o In case of a user specified fanout mode present on link and flow, the
 * subflow fanout count has to be less than or equal to that of the
 * underlying link. The cpu-bindings for the subflows will be a subset of
 * the underlying link.
 * o In case if no cpulist specified on both underlying link and flow, the
 * underlying link relies on a  MAC tunable to provide out of box fanout.
 * The subflow will have no cpulist (the subflow will be unbound)
 * o In case if no cpulist is specified on the underlying link, a subflow can
 * carry  either a user-specified cpulist or fanout count. The cpu-bindings
 * for the subflow will not adhere to restriction that they need to be subset
 * of the underlying link.
 * o In case where the underlying link is carrying either a user specified
 * cpulist or fanout mode and for a unspecified subflow, the subflow will be
 * created unbound.
 * o While creating unbound subflows, bandwidth mode changes attempt to
 * figure a right fanout count. In such cases the fanout count will override
 * the unbound cpu-binding behavior.
 * o In addition to this, while cycling between flow and link properties, we
 * impose a restriction that if a link property has a subflow with
 * user-specified attributes, we will not allow changing the link property.
 * The administrator needs to reset all the user specified properties for the
 * subflows before attempting a link property change.
 * Some of the above rules can be overridden by specifying additional command
 * line options while creating or modifying link or subflow properties.
 */

#include <sys/vlan.h>
#include <sys/dlpi.h>
#include <sys/list.h>
#include <sys/modhash.h>
#include <sys/mac_provider.h>
#include <sys/mac_client_impl.h>
#include <sys/mac_client_priv.h>
#include <sys/mac_cpu_impl.h>
#include <sys/mac_stat.h>
#include <sys/mac_impl.h>
#include <sys/mac.h>
#include <sys/dls.h>
#include <sys/dld.h>
#include <sys/callb.h>
#include <sys/pool.h>
#include <sys/pool_pset.h>
#include <sys/cpupart.h>
#include <inet/wifi_ioctl.h>
#include <inet/ip.h>
#include <inet/ip6.h>
#include <net/wpa.h>
#include <sys/callo.h>

#define	IMPL_HASHSZ	67	/* prime */

kmem_cache_t		*i_mac_impl_cachep;
mod_hash_t		*i_mac_impl_hash;
krwlock_t		i_mac_impl_lock;
uint_t			i_mac_impl_count;
static kmem_cache_t	*mac_ring_cache;
static id_space_t	*minor_ids;
static uint32_t		minor_count;
static pool_event_cb_t	mac_pool_event_reg;

/*
 * Logging stuff. Perhaps mac_logging_interval could be broken into
 * mac_flow_log_interval and mac_link_log_interval if we want to be
 * able to schedule them differently.
 */
uint_t			mac_logging_interval;
boolean_t		mac_flow_log_enable;
boolean_t		mac_link_log_enable;
timeout_id_t		mac_logging_timer;

/* for debugging, see MAC_DBG_PRT() in mac_impl.h */
int mac_dbg = 0;

#define	MACTYPE_KMODDIR	"mac"
#define	MACTYPE_HASHSZ	67
static mod_hash_t	*i_mactype_hash;
/*
 * i_mactype_lock synchronizes threads that obtain references to mactype_t
 * structures through i_mactype_getplugin().
 */
static kmutex_t		i_mactype_lock;

/*
 * mac_tx_percpu_cnt
 *
 * Number of per cpu locks per mac_client_impl_t. Used by the transmit side
 * in mac_tx to reduce lock contention. This is sized at boot time in mac_init.
 * mac_tx_percpu_cnt_max is settable in /etc/system and must be a power of 2.
 * Per cpu locks may be disabled by setting mac_tx_percpu_cnt_max to 1.
 */
int mac_tx_percpu_cnt;
int mac_tx_percpu_cnt_max = 512;

/*
 * Call back functions for the bridge module.  These are guaranteed to be valid
 * when holding a reference on a link or when holding mip->mi_bridge_lock and
 * mi_bridge_link is non-NULL.
 */
mac_bridge_tx_t mac_bridge_tx_cb;
mac_bridge_rx_t mac_bridge_rx_cb;
mac_bridge_ref_t mac_bridge_ref_cb;
mac_bridge_ls_t mac_bridge_ls_cb;

static int i_mac_constructor(void *, void *, int);
static void i_mac_destructor(void *, void *);
static int i_mac_ring_ctor(void *, void *, int);
static void i_mac_ring_dtor(void *, void *);
static void mac_ring_wait(mac_ring_t *);
static void mac_ring_wait_all(mac_impl_t *, mac_ring_type_t);
static void mac_pool_event_cb(pool_event_t, int, void *);
static void mac_group_update_rings_cache(mac_group_t *);

typedef struct netinfo_s {
	list_node_t	ni_link;
	void		*ni_record;
	int		ni_size;
	int		ni_type;
} netinfo_t;

/*
 * Module initialization functions.
 */

void
mac_init(void)
{
	mac_tx_percpu_cnt = ((boot_max_ncpus == -1) ? max_ncpus :
	    boot_max_ncpus);

	/* Upper bound is mac_tx_percpu_cnt_max */
	if (mac_tx_percpu_cnt > mac_tx_percpu_cnt_max)
		mac_tx_percpu_cnt = mac_tx_percpu_cnt_max;

	if (mac_tx_percpu_cnt < 1) {
		/* Someone set max_tx_percpu_cnt_max to 0 or less */
		mac_tx_percpu_cnt = 1;
	}

	ASSERT(mac_tx_percpu_cnt >= 1);
	mac_tx_percpu_cnt = (1 << highbit(mac_tx_percpu_cnt - 1));
	/*
	 * Make it of the form 2**N - 1 in the range
	 * [0 .. mac_tx_percpu_cnt_max - 1]
	 */
	mac_tx_percpu_cnt--;

	i_mac_impl_cachep = kmem_cache_create("mac_impl_cache",
	    sizeof (mac_impl_t), 64, i_mac_constructor, i_mac_destructor,
	    NULL, NULL, NULL, 0);
	ASSERT(i_mac_impl_cachep != NULL);

	mac_ring_cache = kmem_cache_create("mac_ring_cache",
	    sizeof (mac_ring_t), 64, i_mac_ring_ctor, i_mac_ring_dtor, NULL,
	    NULL, NULL, 0);
	ASSERT(mac_ring_cache != NULL);

	i_mac_impl_hash = mod_hash_create_extended("mac_impl_hash",
	    IMPL_HASHSZ, mod_hash_null_keydtor, mod_hash_null_valdtor,
	    mod_hash_bystr, NULL, mod_hash_strkey_cmp, KM_SLEEP);

	/*
	 * Initialize mac buffer management.
	 */
	mac_buffermgmt_init();

	rw_init(&i_mac_impl_lock, NULL, RW_DEFAULT, NULL);

	mac_flow_init();
	mac_bcast_init();
	mac_client_init();
	mac_vlan_announce_init();

	i_mac_impl_count = 0;

	i_mactype_hash = mod_hash_create_extended("mactype_hash",
	    MACTYPE_HASHSZ,
	    mod_hash_null_keydtor, mod_hash_null_valdtor,
	    mod_hash_bystr, NULL, mod_hash_strkey_cmp, KM_SLEEP);

	/*
	 * Allocate an id space to manage minor numbers. The range of the
	 * space will be from MAC_MAX_MINOR+1 to MAC_PRIVATE_MINOR-1.  This
	 * leaves half of the 32-bit minors available for driver private use.
	 */
	minor_ids = id_space_create("mac_minor_ids", MAC_MAX_MINOR+1,
	    MAC_PRIVATE_MINOR-1);
	ASSERT(minor_ids != NULL);
	minor_count = 0;

	/* Let's default to 20 seconds */
	mac_logging_interval = 20;
	mac_flow_log_enable = B_FALSE;
	mac_link_log_enable = B_FALSE;
	mac_logging_timer = 0;

	/* Register to be notified of noteworthy pools events */
	mac_pool_event_reg.pec_func =  mac_pool_event_cb;
	mac_pool_event_reg.pec_arg = NULL;
	pool_event_cb_register(&mac_pool_event_reg);
}

int
mac_fini(void)
{

	if (i_mac_impl_count > 0 || minor_count > 0)
		return (EBUSY);

	mac_vlan_announce_fini();
	pool_event_cb_unregister(&mac_pool_event_reg);

	id_space_destroy(minor_ids);
	mac_flow_fini();

	mod_hash_destroy_hash(i_mac_impl_hash);
	rw_destroy(&i_mac_impl_lock);

	mac_client_fini();
	kmem_cache_destroy(mac_ring_cache);

	mod_hash_destroy_hash(i_mactype_hash);

	/*
	 * Cleanup MAC Buffer Management.
	 */
	mac_buffermgmt_fini();
	return (0);
}

/*
 * Initialize a GLDv3 driver's device ops.  A driver that manages its own ops
 * (e.g. softmac) may pass in a NULL ops argument.
 */
void
mac_init_ops(struct dev_ops *ops, const char *name)
{
	major_t major = ddi_name_to_major((char *)name);

	/*
	 * By returning on error below, we are not letting the driver continue
	 * in an undefined context.  The mac_register() function will faill if
	 * DN_GLDV3_DRIVER isn't set.
	 */
	if (major == DDI_MAJOR_T_NONE)
		return;
	LOCK_DEV_OPS(&devnamesp[major].dn_lock);
	devnamesp[major].dn_flags |= (DN_GLDV3_DRIVER | DN_NETWORK_DRIVER);
	UNLOCK_DEV_OPS(&devnamesp[major].dn_lock);
	if (ops != NULL)
		dld_init_ops(ops, name);
}

void
mac_fini_ops(struct dev_ops *ops)
{
	dld_fini_ops(ops);
}

/*ARGSUSED*/
static int
i_mac_constructor(void *buf, void *arg, int kmflag)
{
	mac_impl_t	*mip = buf;

	bzero(buf, sizeof (mac_impl_t));

	mip->mi_linkstate = LINK_STATE_UNKNOWN;

	rw_init(&mip->mi_rw_lock, NULL, RW_DRIVER, NULL);
	mutex_init(&mip->mi_notify_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&mip->mi_promisc_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&mip->mi_ring_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&mip->mi_pdata_lock, NULL, MUTEX_DEFAULT, NULL);

	mip->mi_notify_cb_info.mcbi_lockp = &mip->mi_notify_lock;
	cv_init(&mip->mi_notify_cb_info.mcbi_cv, NULL, CV_DRIVER, NULL);
	mip->mi_promisc_cb_info.mcbi_lockp = &mip->mi_promisc_lock;
	cv_init(&mip->mi_promisc_cb_info.mcbi_cv, NULL, CV_DRIVER, NULL);

	mutex_init(&mip->mi_bridge_lock, NULL, MUTEX_DEFAULT, NULL);

	mip->mi_gvrp_timeout = MAC_GVRP_TIMEOUT_DEFAULT;

	return (0);
}

/*ARGSUSED*/
static void
i_mac_destructor(void *buf, void *arg)
{
	mac_impl_t	*mip = buf;
	mac_cb_info_t	*mcbi;

	ASSERT(mip->mi_ref == 0);
	ASSERT(mip->mi_mem_ref_cnt == 0);
	ASSERT(mip->mi_active == 0);
	ASSERT(mip->mi_linkstate == LINK_STATE_UNKNOWN);
	ASSERT(mip->mi_devpromisc == 0);
	ASSERT(mip->mi_ksp == NULL);
	ASSERT(mip->mi_kstat_count == 0);
	ASSERT(mip->mi_nclients == 0);
	ASSERT(mip->mi_nactiveclients == 0);
	ASSERT(mip->mi_single_active_client == NULL);
	ASSERT(mip->mi_state_flags == 0);
	ASSERT(mip->mi_factory_addr == NULL);
	ASSERT(mip->mi_factory_addr_num == 0);
	ASSERT(mip->mi_default_tx_ring == NULL);
	mac_gvrp_disable((mac_handle_t)mip);
	mcbi = &mip->mi_notify_cb_info;
	ASSERT(mcbi->mcbi_del_cnt == 0 && mcbi->mcbi_walker_cnt == 0);
	ASSERT(mip->mi_notify_bits == 0);
	ASSERT(mip->mi_notify_thread == NULL);
	ASSERT(mcbi->mcbi_lockp == &mip->mi_notify_lock);
	mcbi->mcbi_lockp = NULL;

	mcbi = &mip->mi_promisc_cb_info;
	ASSERT(mcbi->mcbi_del_cnt == 0 && mip->mi_promisc_list == NULL);
	ASSERT(mip->mi_promisc_list == NULL);
	ASSERT(mcbi->mcbi_lockp == &mip->mi_promisc_lock);
	mcbi->mcbi_lockp = NULL;

	ASSERT(mip->mi_bcast_ngrps == 0 && mip->mi_bcast_grp == NULL);
	ASSERT(mip->mi_perim_owner == NULL && mip->mi_perim_ocnt == 0);

	ASSERT(mip->mi_pmh_tx_drv == NULL);
	ASSERT(mip->mi_pmh_tx == NULL);

	rw_destroy(&mip->mi_rw_lock);

	mutex_destroy(&mip->mi_promisc_lock);
	cv_destroy(&mip->mi_promisc_cb_info.mcbi_cv);
	mutex_destroy(&mip->mi_notify_lock);
	cv_destroy(&mip->mi_notify_cb_info.mcbi_cv);
	mutex_destroy(&mip->mi_ring_lock);
	mutex_destroy(&mip->mi_pdata_lock);

	ASSERT(mip->mi_bridge_link == NULL);
}

/* ARGSUSED */
static int
i_mac_ring_ctor(void *buf, void *arg, int kmflag)
{
	mac_ring_t *ring = (mac_ring_t *)buf;

	bzero(ring, sizeof (mac_ring_t));
	cv_init(&ring->mr_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&ring->mr_ref_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&ring->mr_lock, NULL, MUTEX_DEFAULT, NULL);
	ring->mr_state = MR_FREE;
	return (0);
}

/* ARGSUSED */
static void
i_mac_ring_dtor(void *buf, void *arg)
{
	mac_ring_t *ring = (mac_ring_t *)buf;

	ASSERT(ring->mr_state == MR_FREE);
	ASSERT(ring->mr_packet_pool == NULL);
	ASSERT(ring->mr_packet_descriptor == NULL);

	cv_destroy(&ring->mr_cv);
	cv_destroy(&ring->mr_ref_cv);
	mutex_destroy(&ring->mr_lock);
}

/*
 * Common functions to do mac callback addition and deletion. Currently this is
 * used by promisc callbacks and notify callbacks. List addition and deletion
 * need to take care of list walkers. List walkers in general, can't hold list
 * locks and make upcall callbacks due to potential lock order and recursive
 * reentry issues. Instead list walkers increment the list walker count to mark
 * the presence of a walker thread. Addition can be carefully done to ensure
 * that the list walker always sees either the old list or the new list.
 * However the deletion can't be done while the walker is active, instead the
 * deleting thread simply marks the entry as logically deleted. The last walker
 * physically deletes and frees up the logically deleted entries when the walk
 * is complete.
 */
void
mac_callback_add(mac_cb_info_t *mcbi, mac_cb_t **mcb_head,
    mac_cb_t *mcb_elem)
{
	mac_cb_t	*p;
	mac_cb_t	**pp;

	/* Verify it is not already in the list */
	for (pp = mcb_head; (p = *pp) != NULL; pp = &p->mcb_nextp) {
		if (p == mcb_elem)
			break;
	}
	VERIFY(p == NULL);

	/*
	 * Add it to the head of the callback list. The membar ensures that
	 * the following list pointer manipulations reach global visibility
	 * in exactly the program order below.
	 */
	ASSERT(MUTEX_HELD(mcbi->mcbi_lockp));

	mcb_elem->mcb_nextp = *mcb_head;
	membar_producer();
	*mcb_head = mcb_elem;
}

/*
 * Mark the entry as logically deleted. If there aren't any walkers unlink
 * from the list. In either case return the corresponding status.
 */
boolean_t
mac_callback_remove(mac_cb_info_t *mcbi, mac_cb_t **mcb_head,
    mac_cb_t *mcb_elem)
{
	mac_cb_t	*p;
	mac_cb_t	**pp;

	ASSERT(MUTEX_HELD(mcbi->mcbi_lockp));
	/*
	 * Search the callback list for the entry to be removed
	 */
	for (pp = mcb_head; (p = *pp) != NULL; pp = &p->mcb_nextp) {
		if (p == mcb_elem)
			break;
	}
	VERIFY(p != NULL);

	/*
	 * If there are walkers just mark it as deleted and the last walker
	 * will remove from the list and free it.
	 */
	if (mcbi->mcbi_walker_cnt != 0) {
		p->mcb_flags |= MCB_CONDEMNED;
		mcbi->mcbi_del_cnt++;
		return (B_FALSE);
	}

	ASSERT(mcbi->mcbi_del_cnt == 0);
	*pp = p->mcb_nextp;
	p->mcb_nextp = NULL;
	return (B_TRUE);
}

/*
 * Wait for all pending callback removals to be completed
 */
void
mac_callback_remove_wait(mac_cb_info_t *mcbi)
{
	ASSERT(MUTEX_HELD(mcbi->mcbi_lockp));
	while (mcbi->mcbi_del_cnt != 0) {
		DTRACE_PROBE1(need_wait, mac_cb_info_t *, mcbi);
		cv_wait(&mcbi->mcbi_cv, mcbi->mcbi_lockp);
	}
}

/*
 * The last mac callback walker does the cleanup. Walk the list and unlik
 * all the logically deleted entries and construct a temporary list of
 * removed entries. Return the list of removed entries to the caller.
 */
mac_cb_t *
mac_callback_walker_cleanup(mac_cb_info_t *mcbi, mac_cb_t **mcb_head)
{
	mac_cb_t	*p;
	mac_cb_t	**pp;
	mac_cb_t	*rmlist = NULL;		/* List of removed elements */
	int	cnt = 0;

	ASSERT(MUTEX_HELD(mcbi->mcbi_lockp));
	ASSERT(mcbi->mcbi_del_cnt != 0 && mcbi->mcbi_walker_cnt == 0);

	pp = mcb_head;
	while (*pp != NULL) {
		if ((*pp)->mcb_flags & MCB_CONDEMNED) {
			p = *pp;
			*pp = p->mcb_nextp;
			p->mcb_nextp = rmlist;
			rmlist = p;
			cnt++;
			continue;
		}
		pp = &(*pp)->mcb_nextp;
	}

	ASSERT(mcbi->mcbi_del_cnt == cnt);
	mcbi->mcbi_del_cnt = 0;
	return (rmlist);
}

boolean_t
mac_callback_lookup(mac_cb_t **mcb_headp, mac_cb_t *mcb_elem)
{
	mac_cb_t	*mcb;

	/* Verify it is not already in the list */
	for (mcb = *mcb_headp; mcb != NULL; mcb = mcb->mcb_nextp) {
		if (mcb == mcb_elem)
			return (B_TRUE);
	}

	return (B_FALSE);
}

boolean_t
mac_callback_find(mac_cb_info_t *mcbi, mac_cb_t **mcb_headp, mac_cb_t *mcb_elem)
{
	boolean_t	found;

	mutex_enter(mcbi->mcbi_lockp);
	found = mac_callback_lookup(mcb_headp, mcb_elem);
	mutex_exit(mcbi->mcbi_lockp);

	return (found);
}

/* Free the list of removed callbacks */
void
mac_callback_free(mac_cb_t *rmlist)
{
	mac_cb_t	*mcb;
	mac_cb_t	*mcb_next;

	for (mcb = rmlist; mcb != NULL; mcb = mcb_next) {
		mcb_next = mcb->mcb_nextp;
		kmem_free(mcb->mcb_objp, mcb->mcb_objsize);
	}
}

/*
 * The promisc callbacks are in 2 lists, one off the 'mip' and another off the
 * 'mcip' threaded by mpi_mi_link and mpi_mci_link respectively. However there
 * is only a single shared total walker count, and an entry can't be physically
 * unlinked if a walker is active on either list. The last walker does this
 * cleanup of logically deleted entries.
 */
void
i_mac_promisc_walker_cleanup(mac_impl_t *mip)
{
	mac_cb_t	*rmlist;
	mac_cb_t	*mcb;
	mac_cb_t	*mcb_next;
	mac_promisc_impl_t	*mpip;

	/*
	 * Construct a temporary list of deleted callbacks by walking the
	 * the mi_promisc_list. Then for each entry in the temporary list,
	 * remove it from the mci_promisc_list and free the entry.
	 */
	rmlist = mac_callback_walker_cleanup(&mip->mi_promisc_cb_info,
	    &mip->mi_promisc_list);

	for (mcb = rmlist; mcb != NULL; mcb = mcb_next) {
		mcb_next = mcb->mcb_nextp;
		mpip = (mac_promisc_impl_t *)mcb->mcb_objp;
		VERIFY(mac_callback_remove(&mip->mi_promisc_cb_info,
		    &mpip->mpi_mcip->mci_promisc_list, &mpip->mpi_mci_link));
		mcb->mcb_flags = 0;
		mcb->mcb_nextp = NULL;
		kmem_cache_free(mac_promisc_impl_cache, mpip);
	}
}

void
i_mac_notify(mac_impl_t *mip, mac_notify_type_t type)
{
	mac_cb_info_t	*mcbi;

	/*
	 * Signal the notify thread even after mi_ref has become zero and
	 * mi_disabled is set. The synchronization with the notify thread
	 * happens in mac_unregister and that implies the driver must make
	 * sure it is single-threaded (with respect to mac calls) and that
	 * all pending mac calls have returned before it calls mac_unregister
	 */
	rw_enter(&i_mac_impl_lock, RW_READER);
	if (mip->mi_state_flags & MIS_DISABLED)
		goto exit;

	/*
	 * Guard against incorrect notifications.  (Running a newer
	 * mac client against an older implementation?)
	 */
	if (type >= MAC_NNOTE)
		goto exit;

	mcbi = &mip->mi_notify_cb_info;
	mutex_enter(mcbi->mcbi_lockp);
	mip->mi_notify_bits |= (1 << type);
	cv_broadcast(&mcbi->mcbi_cv);
	mutex_exit(mcbi->mcbi_lockp);

exit:
	rw_exit(&i_mac_impl_lock);
}

/*
 * Mac serialization primitives. Please see the block comment at the
 * top of the file.
 */
void
i_mac_perim_enter(mac_impl_t *mip)
{
	mac_client_impl_t	*mcip;

	if (mip->mi_state_flags & MIS_IS_VNIC) {
		/*
		 * This is a VNIC. Return the lower mac since that is what
		 * we want to serialize on.
		 */
		mcip = mac_vnic_lower(mip);
		mip = mcip->mci_mip;
	}

	mutex_enter(&mip->mi_perim_lock);
	if (mip->mi_perim_owner == curthread) {
		mip->mi_perim_ocnt++;
		mutex_exit(&mip->mi_perim_lock);
		return;
	}

	while (mip->mi_perim_owner != NULL)
		cv_wait(&mip->mi_perim_cv, &mip->mi_perim_lock);

	mip->mi_perim_owner = curthread;
	ASSERT(mip->mi_perim_ocnt == 0);
	mip->mi_perim_ocnt++;
#ifdef DEBUG
	mip->mi_perim_stack_depth = getpcstack(mip->mi_perim_stack,
	    MAC_PERIM_STACK_DEPTH);
#endif
	mutex_exit(&mip->mi_perim_lock);
}

int
i_mac_perim_enter_nowait(mac_impl_t *mip)
{
	/*
	 * The vnic is a special case, since the serialization is done based
	 * on the lower mac. If the lower mac is busy, it does not imply the
	 * vnic can't be unregistered. But in the case of other drivers,
	 * a busy perimeter or open mac handles implies that the mac is busy
	 * and can't be unregistered.
	 */
	if (mip->mi_state_flags & MIS_IS_VNIC) {
		i_mac_perim_enter(mip);
		return (0);
	}

	mutex_enter(&mip->mi_perim_lock);
	if (mip->mi_perim_owner != NULL) {
		mutex_exit(&mip->mi_perim_lock);
		return (EBUSY);
	}
	ASSERT(mip->mi_perim_ocnt == 0);
	mip->mi_perim_owner = curthread;
	mip->mi_perim_ocnt++;
	mutex_exit(&mip->mi_perim_lock);

	return (0);
}

void
i_mac_perim_exit(mac_impl_t *mip)
{
	mac_client_impl_t *mcip;

	if (mip->mi_state_flags & MIS_IS_VNIC) {
		/*
		 * This is a VNIC. Return the lower mac since that is what
		 * we want to serialize on.
		 */
		mcip = mac_vnic_lower(mip);
		mip = mcip->mci_mip;
	}

	ASSERT(mip->mi_perim_owner == curthread && mip->mi_perim_ocnt != 0);

	mutex_enter(&mip->mi_perim_lock);
	if (--mip->mi_perim_ocnt == 0) {
		mip->mi_perim_owner = NULL;
		cv_signal(&mip->mi_perim_cv);
	}
	mutex_exit(&mip->mi_perim_lock);
}

/*
 * Returns whether the current thread holds the mac perimeter. Used in making
 * assertions.
 */
boolean_t
mac_perim_held(mac_handle_t mh)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;
	mac_client_impl_t *mcip;

	if (mip->mi_state_flags & MIS_IS_VNIC) {
		/*
		 * This is a VNIC. Return the lower mac since that is what
		 * we want to serialize on.
		 */
		mcip = mac_vnic_lower(mip);
		mip = mcip->mci_mip;
	}
	return (mip->mi_perim_owner == curthread);
}

/*
 * mac client interfaces to enter the mac perimeter of a mac end point, given
 * its mac handle, or macname or linkid.
 */
void
mac_perim_enter_by_mh(mac_handle_t mh, mac_perim_handle_t *mphp)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;

	i_mac_perim_enter(mip);
	/*
	 * The mac_perim_handle_t returned encodes the 'mip' and whether a
	 * mac_open has been done internally while entering the perimeter.
	 * This information is used in mac_perim_exit
	 */
	MAC_ENCODE_MPH(*mphp, mip, 0);
}

int
mac_perim_enter_by_macname(const char *name, mac_perim_handle_t *mphp)
{
	int	err;
	mac_handle_t	mh;

	if ((err = mac_open(name, &mh)) != 0)
		return (err);

	mac_perim_enter_by_mh(mh, mphp);
	MAC_ENCODE_MPH(*mphp, mh, 1);
	return (0);
}

int
mac_perim_enter_by_linkid(datalink_id_t linkid, mac_perim_handle_t *mphp)
{
	int	err;
	mac_handle_t	mh;

	if ((err = mac_open_by_linkid(linkid, &mh)) != 0)
		return (err);

	mac_perim_enter_by_mh(mh, mphp);
	MAC_ENCODE_MPH(*mphp, mh, 1);
	return (0);
}

void
mac_perim_exit(mac_perim_handle_t mph)
{
	mac_impl_t	*mip;
	boolean_t	need_close;

	MAC_DECODE_MPH(mph, mip, need_close);
	i_mac_perim_exit(mip);
	if (need_close)
		mac_close((mac_handle_t)mip);
}

int
mac_hold(const char *macname, mac_impl_t **pmip)
{
	mac_impl_t	*mip;
	int		err;

	/*
	 * Check the device name length to make sure it won't overflow our
	 * buffer.
	 */
	if (strlen(macname) >= MAXNAMELEN)
		return (EINVAL);

	/*
	 * Look up its entry in the global hash table.
	 */
	rw_enter(&i_mac_impl_lock, RW_WRITER);
	err = mod_hash_find(i_mac_impl_hash, (mod_hash_key_t)macname,
	    (mod_hash_val_t *)&mip);

	if (err != 0) {
		rw_exit(&i_mac_impl_lock);
		return (ENOENT);
	}

	if (mip->mi_state_flags & MIS_DISABLED) {
		rw_exit(&i_mac_impl_lock);
		return (ENOENT);
	}

	if (mip->mi_state_flags & MIS_EXCLUSIVE_HELD) {
		rw_exit(&i_mac_impl_lock);
		return (EBUSY);
	}

	mip->mi_ref++;
	rw_exit(&i_mac_impl_lock);

	*pmip = mip;
	return (0);
}

void
mac_rele(mac_impl_t *mip)
{
	rw_enter(&i_mac_impl_lock, RW_WRITER);
	ASSERT(mip->mi_ref != 0);
	if (--mip->mi_ref == 0) {
		ASSERT(mip->mi_nactiveclients == 0 &&
		    !(mip->mi_state_flags & MIS_EXCLUSIVE));
	}
	rw_exit(&i_mac_impl_lock);
}

/*
 * Private GLDv3 function to start a MAC instance.
 */
int
mac_start(mac_handle_t mh)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;
	int		err = 0;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));
	ASSERT(mip->mi_start != NULL);

	/*
	 * Check whether the device is already started.
	 */
	if (mip->mi_active++ == 0) {
		mac_ring_t	*def_txring = NULL;
		mac_group_t	*def_txgrp = NULL, *def_rxgrp = NULL;

		/*
		 * If enabled, create the buffer management pools for
		 * the device.
		 */
		err = mac_bm_create_pools(mip);
		if (err != 0) {
			mip->mi_active--;
			return (err);
		}

		/*
		 * Start the device.
		 */
		err = mip->mi_start(mip->mi_driver);
		if (err != 0) {
			mip->mi_active--;
			mac_bm_destroy_pools(mip);
			return (err);
		}

		/*
		 * Start the default tx group or ring.
		 */
		if ((def_txgrp = MAC_DEFAULT_TX_GROUP(mip)) != NULL ||
		    (def_txring =
		    (mac_ring_t *)mip->mi_default_tx_ring) != NULL) {
			if (def_txgrp != NULL) {
				ASSERT(def_txgrp->mrg_state ==
				    MAC_GROUP_STATE_REGISTERED);
				err = mac_start_group_and_rings(def_txgrp);
			} else if (def_txring != NULL) {
				err = mac_start_ring(def_txring);
			}
			if (err != 0) {
				mip->mi_active--;
				mip->mi_stop(mip->mi_driver);
				mac_bm_destroy_pools(mip);
				return (err);
			}
		}

		if ((def_rxgrp = MAC_DEFAULT_RX_GROUP(mip)) != NULL) {
			/*
			 * Start the default ring, since it will be needed
			 * to receive broadcast and multicast traffic for
			 * both primary and non-primary MAC clients.
			 */
			ASSERT(def_rxgrp->mrg_state ==
			    MAC_GROUP_STATE_REGISTERED);
			err = mac_start_group_and_rings(def_rxgrp);
			if (err != 0) {
				mip->mi_active--;
				if (def_txgrp != NULL)
					mac_stop_group_and_rings(def_txgrp);
				else if (def_txring != NULL)
					mac_stop_ring(def_txring);

				mip->mi_stop(mip->mi_driver);
				mac_bm_destroy_pools(mip);
				return (err);
			}
			mac_set_group_state(def_rxgrp, MAC_GROUP_STATE_SHARED);
		}
	}
	return (err);
}

/*
 * Private GLDv3 function to stop a MAC instance.
 */
void
mac_stop(mac_handle_t mh)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;
	mac_group_t	*grp;

	ASSERT(mip->mi_stop != NULL);
	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));

	/*
	 * Check whether the device is still needed.
	 */
	ASSERT(mip->mi_active != 0);
	if (--mip->mi_active == 0) {
		if ((grp = MAC_DEFAULT_RX_GROUP(mip)) != NULL) {
			/*
			 * There should be no more active clients since the
			 * MAC is being stopped. Stop the default RX group
			 * and transition it back to registered state.
			 *
			 * When clients are torn down, the groups
			 * are release via mac_release_rx_group which
			 * knows the the default group is always in
			 * started mode since broadcast uses it. So
			 * we can assert that their are no clients
			 * (since mac_bcast_add doesn't register itself
			 * as a client) and group is in SHARED state.
			 */
			ASSERT(grp->mrg_state == MAC_GROUP_STATE_SHARED);
			ASSERT(MAC_GROUP_NO_CLIENT(grp) &&
			    mip->mi_nactiveclients == 0);
			mac_stop_group_and_rings(grp);
			mac_set_group_state(grp, MAC_GROUP_STATE_REGISTERED);
		}
		if ((grp = MAC_DEFAULT_TX_GROUP(mip)) != NULL) {
			mac_stop_group_and_rings(grp);
		} else if (mip->mi_default_tx_ring != NULL) {
			mac_stop_ring((mac_ring_t *)mip->mi_default_tx_ring);
		}

		/*
		 * Stop the device.
		 */
		mip->mi_stop(mip->mi_driver);

		/*
		 * Destroy the buffer management pools for the device.
		 */
		mac_bm_destroy_pools(mip);
	}
}

int
i_mac_promisc_set(mac_impl_t *mip, boolean_t on)
{
	int		err = 0;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));
	ASSERT(mip->mi_setpromisc != NULL);
	if (mip->mi_setpromisc == NULL)
		return (ENOTSUP);

	if (on) {
		/*
		 * Enable promiscuous mode on the device if not yet enabled.
		 */
		if (mip->mi_devpromisc++ == 0) {
			err = mip->mi_setpromisc(mip->mi_driver, B_TRUE);
			if (err != 0) {
				mip->mi_devpromisc--;
				return (err);
			}
			i_mac_notify(mip, MAC_NOTE_DEVPROMISC);
		}
	} else {
		if (mip->mi_devpromisc == 0)
			return (EPROTO);

		/*
		 * Disable promiscuous mode on the device if this is the last
		 * enabling.
		 */
		if (--mip->mi_devpromisc == 0) {
			err = mip->mi_setpromisc(mip->mi_driver, B_FALSE);
			if (err != 0) {
				mip->mi_devpromisc++;
				return (err);
			}
			i_mac_notify(mip, MAC_NOTE_DEVPROMISC);
		}
	}

	return (0);
}

/*
 * The promiscuity state can change any time. If the caller needs to take
 * actions that are atomic with the promiscuity state, then the caller needs
 * to bracket the entire sequence with mac_perim_enter/exit
 */
boolean_t
mac_promisc_get(mac_handle_t mh)
{
	mac_impl_t		*mip = (mac_impl_t *)mh;

	/*
	 * Return the current promiscuity.
	 */
	return (mip->mi_devpromisc != 0);
}

/*
 * Get the pre-mapped buffer cache handle. May be used by mac clients and
 * mac providers
 */
pm_handle_t
mac_pmh_tx_get(mac_handle_t mh)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;

	return (mip->mi_pmh_tx);
}

/*
 * Invoked at MAC instance attach time to initialize the list
 * of factory MAC addresses supported by a MAC instance. This function
 * builds a local cache in the mac_impl_t for the MAC addresses
 * supported by the underlying hardware. The MAC clients themselves
 * use the mac_addr_factory*() functions to query and reserve
 * factory MAC addresses.
 */
void
mac_addr_factory_init(mac_impl_t *mip)
{
	mac_capab_multifactaddr_t capab;
	uint8_t *addr;
	int i;

	/*
	 * First round to see how many factory MAC addresses are available.
	 */
	bzero(&capab, sizeof (capab));
	if (!i_mac_capab_get((mac_handle_t)mip, MAC_CAPAB_MULTIFACTADDR,
	    &capab) || (capab.mcm_naddr == 0)) {
		/*
		 * The MAC instance doesn't support multiple factory
		 * MAC addresses, we're done here.
		 */
		return;
	}

	/*
	 * Allocate the space and get all the factory addresses.
	 */
	addr = kmem_alloc(capab.mcm_naddr * MAXMACADDRLEN, KM_SLEEP);
	capab.mcm_getaddr(mip->mi_driver, capab.mcm_naddr, addr);

	mip->mi_factory_addr_num = capab.mcm_naddr;
	mip->mi_factory_addr = kmem_zalloc(mip->mi_factory_addr_num *
	    sizeof (mac_factory_addr_t), KM_SLEEP);

	for (i = 0; i < capab.mcm_naddr; i++) {
		bcopy(addr + i * MAXMACADDRLEN,
		    mip->mi_factory_addr[i].mfa_addr,
		    mip->mi_type->mt_addr_length);
		mip->mi_factory_addr[i].mfa_in_use = B_FALSE;
	}

	kmem_free(addr, capab.mcm_naddr * MAXMACADDRLEN);
}

void
mac_addr_factory_fini(mac_impl_t *mip)
{
	if (mip->mi_factory_addr == NULL) {
		ASSERT(mip->mi_factory_addr_num == 0);
		return;
	}

	kmem_free(mip->mi_factory_addr, mip->mi_factory_addr_num *
	    sizeof (mac_factory_addr_t));

	mip->mi_factory_addr = NULL;
	mip->mi_factory_addr_num = 0;
}

/*
 * Reserve a factory MAC address. If *slot is set to -1, the function
 * attempts to reserve any of the available factory MAC addresses and
 * returns the reserved slot id. If no slots are available, the function
 * returns ENOSPC. If *slot is not set to -1, the function reserves
 * the specified slot if it is available, or returns EBUSY is the slot
 * is already used. Returns ENOTSUP if the underlying MAC does not
 * support multiple factory addresses. If the slot number is not -1 but
 * is invalid, returns EINVAL.
 */
int
mac_addr_factory_reserve(mac_client_handle_t mch, int *slot)
{
	mac_client_impl_t *mcip = (mac_client_impl_t *)mch;
	mac_impl_t *mip = mcip->mci_mip;
	int i, ret = 0;

	i_mac_perim_enter(mip);
	/*
	 * Protect against concurrent readers that may need a self-consistent
	 * view of the factory addresses
	 */
	rw_enter(&mip->mi_rw_lock, RW_WRITER);

	if (mip->mi_factory_addr_num == 0) {
		ret = ENOTSUP;
		goto bail;
	}

	if (*slot != -1) {
		/* check the specified slot */
		if (*slot < 1 || *slot > mip->mi_factory_addr_num) {
			ret = EINVAL;
			goto bail;
		}
		if (mip->mi_factory_addr[*slot-1].mfa_in_use) {
			ret = EBUSY;
			goto bail;
		}
	} else {
		/* pick the next available slot */
		for (i = 0; i < mip->mi_factory_addr_num; i++) {
			if (!mip->mi_factory_addr[i].mfa_in_use)
				break;
		}

		if (i == mip->mi_factory_addr_num) {
			ret = ENOSPC;
			goto bail;
		}
		*slot = i+1;
	}

	mip->mi_factory_addr[*slot-1].mfa_in_use = B_TRUE;
	mip->mi_factory_addr[*slot-1].mfa_client = mcip;

bail:
	rw_exit(&mip->mi_rw_lock);
	i_mac_perim_exit(mip);
	return (ret);
}

/*
 * Release the specified factory MAC address slot.
 */
void
mac_addr_factory_release(mac_client_handle_t mch, uint_t slot)
{
	mac_client_impl_t *mcip = (mac_client_impl_t *)mch;
	mac_impl_t *mip = mcip->mci_mip;

	i_mac_perim_enter(mip);
	/*
	 * Protect against concurrent readers that may need a self-consistent
	 * view of the factory addresses
	 */
	rw_enter(&mip->mi_rw_lock, RW_WRITER);

	ASSERT(slot > 0 && slot <= mip->mi_factory_addr_num);
	ASSERT(mip->mi_factory_addr[slot-1].mfa_in_use);

	mip->mi_factory_addr[slot-1].mfa_in_use = B_FALSE;

	rw_exit(&mip->mi_rw_lock);
	i_mac_perim_exit(mip);
}

/*
 * Stores in mac_addr the value of the specified MAC address. Returns
 * 0 on success, or EINVAL if the slot number is not valid for the MAC.
 * The caller must provide a string of at least MAXNAMELEN bytes.
 */
void
mac_addr_factory_value(mac_handle_t mh, int slot, uchar_t *mac_addr,
    uint_t *addr_len, char *client_name, boolean_t *in_use_arg)
{
	mac_impl_t *mip = (mac_impl_t *)mh;
	boolean_t in_use;

	ASSERT(slot > 0 && slot <= mip->mi_factory_addr_num);

	/*
	 * Readers need to hold mi_rw_lock. Writers need to hold mac perimeter
	 * and mi_rw_lock
	 */
	rw_enter(&mip->mi_rw_lock, RW_READER);
	bcopy(mip->mi_factory_addr[slot-1].mfa_addr, mac_addr, MAXMACADDRLEN);
	*addr_len = mip->mi_type->mt_addr_length;
	in_use = mip->mi_factory_addr[slot-1].mfa_in_use;
	if (in_use && client_name != NULL) {
		bcopy(mip->mi_factory_addr[slot-1].mfa_client->mci_name,
		    client_name, MAXNAMELEN);
	}
	if (in_use_arg != NULL)
		*in_use_arg = in_use;
	rw_exit(&mip->mi_rw_lock);
}

/*
 * Returns the number of factory MAC addresses (in addition to the
 * primary MAC address), 0 if the underlying MAC doesn't support
 * that feature.
 */
uint_t
mac_addr_factory_num(mac_handle_t mh)
{
	mac_impl_t *mip = (mac_impl_t *)mh;

	return (mip->mi_factory_addr_num);
}

/*
 * The following mac_hwrings_xxx() functions are private mac client functions
 * used by the aggr driver to access and control the underlying HW Rx group
 * and rings. In this case, the aggr driver has exclusive control of the
 * underlying HW Rx group/rings, it calls the following functions to
 * start/stop the HW Rx rings, disable/enable polling, add/remove mac'
 * addresses, or set up the Rx callback.
 */
/* ARGSUSED */
static void
mac_hwrings_rx_process(void *arg1, void *arg2, mblk_t *mp_chain, uint_t flags)
{
	mac_client_impl_t	*mcip = arg1;
	mac_ring_t		*mr = arg2;

	mcip->mci_rx_fn(mcip->mci_rx_arg, mr->mr_mrh, mp_chain, B_FALSE);
}

int
mac_deftxring_get(mac_client_handle_t mch, mac_ring_handle_t *hwrh)
{
	mac_impl_t	*mip = ((mac_client_impl_t *)mch)->mci_mip;

	if (mip->mi_default_tx_ring == NULL)
		return (EINVAL);

	*hwrh = mip->mi_default_tx_ring;
	return (0);
}

/*
 * This function is called to get the list of HW rings that are reserved by
 * an exclusive mac client.
 *
 * Return value: the number of HW rings.
 */
int
mac_hwrings_get(mac_client_handle_t mch, mac_group_handle_t *hwgh,
    mac_ring_handle_t *hwrh, mac_ring_type_t rtype)
{
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;
	flow_entry_t		*flent = mcip->mci_flent;
	mac_group_t		*grp;
	mac_ring_t		*ring;
	int			cnt = 0;

	if (rtype == MAC_RING_TYPE_RX) {
		grp = flent->fe_rx_ring_group;
	} else if (rtype == MAC_RING_TYPE_TX) {
		grp = flent->fe_tx_ring_group;
	} else {
		ASSERT(B_FALSE);
		return (-1);
	}
	/*
	 * The mac client did not reserve any RX group, return directly.
	 * This is probably because the underlying MAC does not support
	 * any groups.
	 */
	if (hwgh != NULL)
		*hwgh = NULL;
	if (grp == NULL)
		return (0);
	/*
	 * This group must be reserved by this mac client.
	 */
	ASSERT((grp->mrg_state == MAC_GROUP_STATE_RESERVED) &&
	    (mcip == MAC_GROUP_ONLY_CLIENT(grp)));

	for (ring = grp->mrg_rings; ring != NULL; ring = ring->mr_next, cnt++) {
		ASSERT(cnt < MAX_RINGS_PER_GROUP);
		hwrh[cnt] = (mac_ring_handle_t)ring;
	}
	if (hwgh != NULL)
		*hwgh = (mac_group_handle_t)grp;

	return (cnt);
}

/*
 * This function is called to get info about Tx/Rx rings.
 *
 * Return value: returns uint_t which will have various bits set
 * that indicates different properties of the ring.
 */
mac_ring_info_t *
mac_hwring_getinfo(mac_ring_handle_t rh)
{
	mac_ring_t *ring = (mac_ring_t *)rh;

	return (&ring->mr_info);
}

/*
 * Export ddi interrupt handles from the HW ring to the pseudo ring and
 * setup the RX callback of the mac client which exclusively controls
 * HW ring.
 */
/* ARGSUSED */
void
mac_hwring_setup(mac_ring_handle_t hwrh, mac_resource_handle_t prh,
    mac_ring_handle_t pseudo_rh)
{
	mac_ring_t		*hw_ring = (mac_ring_t *)hwrh;
	mac_group_t		*group = (mac_group_t *)hw_ring->mr_gh;
	mac_ring_t		*pseudo_ring;

	if (pseudo_rh != NULL) {
		pseudo_ring = (mac_ring_t *)pseudo_rh;
		pseudo_ring->mr_hwrh = hwrh;
		/*
		 * Save a pointer to pseudo ring in the hw ring. This
		 * is done for 2 reasons:
		 * 1) If interrupt handle changes, the hw ring will
		 * be notified of the change (see mac_ring_intr_set())
		 * and the appropriate change will be made to the
		 * pseudo ring that has exported the ddi handle.
		 * 2) For tx flow control.
		 */
		hw_ring->mr_prh = pseudo_rh;
	}

	if (hw_ring->mr_type == MAC_RING_TYPE_RX) {
		flow_entry_t	*flent = group->mrg_flent;

		/*
		 * flent cannot be NULL because aggr opens the port
		 * in exclusive mode.
		 */
		ASSERT(flent != NULL);
		hw_ring->mr_mrh = prh;
		flent->fe_cb_fn = mac_hwrings_rx_process;
	}
}

void
mac_hwring_teardown(mac_ring_handle_t hwrh)
{
	mac_ring_t		*hw_ring = (mac_ring_t *)hwrh;

	if (hw_ring == NULL)
		return;

	hw_ring->mr_prh = NULL;
	hw_ring->mr_mrh = NULL;
}

int
mac_hwring_disable_intr(mac_ring_handle_t rh)
{
	mac_ring_t *rr_ring = (mac_ring_t *)rh;
	mac_intr_t *intr = &rr_ring->mr_info.mri_intr;

	return (intr->mi_disable(rr_ring->mr_driver));
}

int
mac_hwring_enable_intr(mac_ring_handle_t rh)
{
	mac_ring_t *rr_ring = (mac_ring_t *)rh;
	mac_intr_t *intr = &rr_ring->mr_info.mri_intr;

	return (intr->mi_enable(rr_ring->mr_driver));
}

/* ARGSUSED */
int
mac_hwring_start(mac_ring_handle_t rh)
{
	return (0);
}

/* ARGSUSED */
void
mac_hwring_stop(mac_ring_handle_t rh)
{
}

mblk_t *
mac_hwring_poll(mac_ring_handle_t rh, int bytes_to_pickup,
    int pkts_to_pickup)
{
	mac_ring_t *rr_ring = (mac_ring_t *)rh;
	mac_ring_info_t *info = &rr_ring->mr_info;

	return (info->mri_poll(info->mri_driver, bytes_to_pickup,
	    pkts_to_pickup));
}

/*
 * Send packets through a selected tx ring.
 */
mblk_t *
mac_hwring_tx(mac_ring_handle_t rh, mblk_t *mp)
{
	mac_ring_t 	*ring = (mac_ring_t *)rh;
	mac_ring_info_t *info = &ring->mr_info;

	ASSERT(ring->mr_type == MAC_RING_TYPE_TX &&
	    ring->mr_state >= MR_INUSE);
	return (info->mri_tx(info->mri_driver, mp));
}

/*
 * Query stats for a particular rx/tx ring
 */
int
mac_hwring_getstat(mac_ring_handle_t rh, uint_t stat, uint64_t *val)
{
	mac_ring_t	*ring = (mac_ring_t *)rh;
	mac_ring_info_t *info = &ring->mr_info;

	return (info->mri_stat(info->mri_driver, stat, val));
}

/*
 * Private function that is only used by aggr to send packets through
 * a port/Tx ring. Since aggr exposes a pseudo Tx ring even for ports
 * that does not expose Tx rings, aggr_ring_tx() entry point needs
 * access to mac_impl_t to send packets through m_tx() entry point.
 * It accomplishes this by calling mac_hwring_send_priv() function.
 */
mblk_t *
mac_hwring_send_priv(mac_client_handle_t mch, mac_ring_handle_t rh, mblk_t *mp)
{
	mac_client_impl_t *mcip = (mac_client_impl_t *)mch;
	mac_impl_t *mip = mcip->mci_mip;

	MAC_TX(mip, rh, mp, mcip);
	return (mp);
}

int
mac_hwgroup_addmac(mac_group_handle_t gh, const uint8_t *addr)
{
	mac_group_t *group = (mac_group_t *)gh;

	return (mac_group_addmac(group, addr, B_FALSE));
}

int
mac_hwgroup_remmac(mac_group_handle_t gh, const uint8_t *addr)
{
	mac_group_t *group = (mac_group_t *)gh;

	return (mac_group_remmac(group, addr));
}

/*
 * Set the RX group to be shared/reserved. Note that the group must be
 * started/stopped outside of this function.
 */
void
mac_set_group_state(mac_group_t *grp, mac_group_state_t state)
{
	/*
	 * If there is no change in the group state, just return.
	 */
	if (grp->mrg_state == state)
		return;

	switch (state) {
	case MAC_GROUP_STATE_RESERVED:
		break;

	case MAC_GROUP_STATE_SHARED:
		/* The ring is not available for reservations any more */
		grp->mrg_flent = NULL;
		break;

	case MAC_GROUP_STATE_REGISTERED:
		/* Also callable from mac_register, perim is not held */
		break;

	default:
		ASSERT(B_FALSE);
		break;
	}

	grp->mrg_state = state;
}

/*
 * Wait for ring refcnt to drop to 0
 */
static void
mac_ring_wait(mac_ring_t *ring)
{
	mutex_enter(&ring->mr_lock);
	ring->mr_flag |= MR_QUIESCE;
	while (ring->mr_refcnt != 0)
		cv_wait(&ring->mr_ref_cv, &ring->mr_lock);

	ring->mr_flag &= ~MR_QUIESCE;
	mutex_exit(&ring->mr_lock);
}

static void
mac_ring_wait_all(mac_impl_t *mip, mac_ring_type_t type)
{
	mac_ring_t	*ring;

	for (ring = mip->mi_ring_list; ring != NULL;
	    ring = ring->mr_list_next) {
		if (ring->mr_type == type)
			mac_ring_wait(ring);
	}
}

/*
 * Please see mac_tx for details about the per cpu locking scheme
 */
static void
mac_tx_lock_all(mac_client_impl_t *mcip)
{
	int	i;

	for (i = 0; i <= mac_tx_percpu_cnt; i++)
		mutex_enter(&mcip->mci_tx_pcpu[i].pcpu_tx_lock);
}

static void
mac_tx_unlock_all(mac_client_impl_t *mcip)
{
	int	i;

	for (i = mac_tx_percpu_cnt; i >= 0; i--)
		mutex_exit(&mcip->mci_tx_pcpu[i].pcpu_tx_lock);
}

static void
mac_tx_unlock_allbutzero(mac_client_impl_t *mcip)
{
	int	i;

	for (i = mac_tx_percpu_cnt; i > 0; i--)
		mutex_exit(&mcip->mci_tx_pcpu[i].pcpu_tx_lock);
}

static int
mac_tx_sum_refcnt(mac_client_impl_t *mcip)
{
	int	i;
	int	refcnt = 0;

	for (i = 0; i <= mac_tx_percpu_cnt; i++)
		refcnt += mcip->mci_tx_pcpu[i].pcpu_tx_refcnt;

	return (refcnt);
}

/*
 * Stop future Tx packets coming down from the client in preparation for
 * quiescing the Tx side. This is needed for dynamic reclaim and reassignment
 * of rings between clients
 */
static void
mac_tx_client_block(mac_client_impl_t *mcip)
{
	mac_tx_lock_all(mcip);
	mcip->mci_tx_flag |= MCI_TX_QUIESCE;
	while (mac_tx_sum_refcnt(mcip) != 0) {
		mac_tx_unlock_allbutzero(mcip);
		cv_wait(&mcip->mci_tx_cv, &mcip->mci_tx_pcpu[0].pcpu_tx_lock);
		mutex_exit(&mcip->mci_tx_pcpu[0].pcpu_tx_lock);
		mac_tx_lock_all(mcip);
	}
	mac_tx_unlock_all(mcip);
}

static void
mac_tx_client_unblock(mac_client_impl_t *mcip)
{
	mac_tx_lock_all(mcip);
	mcip->mci_tx_flag &= ~MCI_TX_QUIESCE;
	mac_tx_unlock_all(mcip);
	mac_tx_invoke_callbacks(mcip, (mac_tx_cookie_t)-1);
}

static void
mac_rx_block(mac_impl_t *mip)
{
	mip->mi_state_flags |= MIS_RX_BLOCK;
	mac_ring_wait_all(mip, MAC_RING_TYPE_RX);
}

static void
mac_rx_unblock(mac_impl_t *mip)
{
	mip->mi_state_flags &= ~MIS_RX_BLOCK;
}

static void
mac_tx_block(mac_impl_t *mip)
{
	mac_client_impl_t	*mcip;

	mip->mi_state_flags |= MIS_TX_BLOCK;
	for (mcip = mip->mi_clients_list; mcip != NULL;
	    mcip = mcip->mci_client_next) {
		if ((mcip->mci_state_flags & (MCIS_SHARE_BOUND |
		    MCIS_EXCLUSIVE | MCIS_NO_UNICAST_ADDR)) != 0 ||
		    (mcip->mci_tx_flag & MCI_TX_QUIESCE) != 0 ||
		    !MCIP_DATAPATH_SETUP(mcip))
			continue;

		mac_tx_client_block(mcip);
		mcip->mci_state_flags |= MCIS_TX_BLOCK;
	}
	mac_ring_wait_all(mip, MAC_RING_TYPE_TX);
}

static void
mac_tx_unblock(mac_impl_t *mip)
{
	mac_client_impl_t	*mcip;

	for (mcip = mip->mi_clients_list; mcip != NULL;
	    mcip = mcip->mci_client_next) {
		if ((mcip->mci_state_flags & MCIS_TX_BLOCK) != 0) {
			mac_tx_client_unblock(mcip);
			mcip->mci_state_flags &= ~MCIS_TX_BLOCK;
		}
	}
	mip->mi_state_flags &= ~MIS_TX_BLOCK;
}

void
i_mac_setup_enter(mac_impl_t *mip)
{
	mac_rx_block(mip);
	mac_tx_block(mip);
}

void
i_mac_setup_exit(mac_impl_t *mip)
{
	mac_rx_unblock(mip);
	mac_tx_unblock(mip);
}

void
mac_setup_enter(mac_handle_t mh)
{
	i_mac_setup_enter((mac_impl_t *)mh);
}

void
mac_setup_exit(mac_handle_t mh)
{
	i_mac_setup_exit((mac_impl_t *)mh);
}

void
mac_rx_client_quiesce(mac_client_handle_t mch)
{
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;
	mac_impl_t		*mip = mcip->mci_mip;
	flow_entry_t		*flent = mcip->mci_flent;
	mac_group_t		*group = flent->fe_rx_ring_group;
	mac_ring_t		*ring;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));
	if (group != NULL) {
		group->mrg_flags |= MRG_QUIESCE;
		for (ring = group->mrg_rings; ring != NULL;
		    ring = ring->mr_next)
			mac_ring_wait(ring);
	}
	FLOW_MARK(flent, FE_QUIESCE);
	mac_flow_wait(flent, FLOW_DRIVER_UPCALL);
	mac_rx_fanout_wait(mcip);
}

void
mac_rx_client_restart(mac_client_handle_t mch)
{
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;
	mac_impl_t		*mip = mcip->mci_mip;
	flow_entry_t		*flent = mcip->mci_flent;
	mac_group_t		*group = flent->fe_rx_ring_group;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));
	FLOW_UNMARK(flent, FE_QUIESCE);
	if (group != NULL)
		group->mrg_flags &= ~MRG_QUIESCE;
}

void
mac_tx_client_quiesce(mac_client_handle_t mch)
{
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mcip->mci_mip));
	mac_tx_client_block(mcip);
}

void
mac_tx_client_restart(mac_client_handle_t mch)
{
	mac_client_impl_t *mcip = (mac_client_impl_t *)mch;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mcip->mci_mip));
	mac_tx_client_unblock(mcip);
}

void
mac_client_quiesce(mac_client_impl_t *mcip)
{
	mac_rx_client_quiesce((mac_client_handle_t)mcip);
	mac_tx_client_quiesce((mac_client_handle_t)mcip);
}

void
mac_client_restart(mac_client_impl_t *mcip)
{
	mac_rx_client_restart((mac_client_handle_t)mcip);
	mac_tx_client_restart((mac_client_handle_t)mcip);
}

/*
 * Allocate a minor number.
 */
minor_t
mac_minor_hold(boolean_t sleep)
{
	minor_t	minor;

	/*
	 * Grab a value from the arena.
	 */
	atomic_add_32(&minor_count, 1);

	if (sleep)
		minor = (uint_t)id_alloc(minor_ids);
	else
		minor = (uint_t)id_alloc_nosleep(minor_ids);

	if (minor == 0) {
		atomic_add_32(&minor_count, -1);
		return (0);
	}

	return (minor);
}

/*
 * Release a previously allocated minor number.
 */
void
mac_minor_rele(minor_t minor)
{
	/*
	 * Return the value to the arena.
	 */
	id_free(minor_ids, minor);
	atomic_add_32(&minor_count, -1);
}

uint32_t
mac_no_notification(mac_handle_t mh)
{
	mac_impl_t *mip = (mac_impl_t *)mh;

	return (((mip->mi_state_flags & MIS_LEGACY) != 0) ?
	    mip->mi_capab_legacy.ml_unsup_note : 0);
}

/*
 * Prevent any new opens of this mac in preparation for unregister
 */
int
i_mac_disable(mac_impl_t *mip)
{
	mac_client_impl_t	*mcip;

	rw_enter(&i_mac_impl_lock, RW_WRITER);
	if (mip->mi_state_flags & MIS_DISABLED) {
		/* Already disabled, return success */
		rw_exit(&i_mac_impl_lock);
		return (0);
	}
	/*
	 * See if there are any other references to this mac_t (e.g., VLAN's).
	 * If so return failure. If all the other checks below pass, then
	 * set mi_disabled atomically under the i_mac_impl_lock to prevent
	 * any new VLAN's from being created or new mac client opens of this
	 * mac end point.
	 */
	if (mip->mi_ref > 0) {
		rw_exit(&i_mac_impl_lock);
		return (EBUSY);
	}

	/*
	 * mac clients must delete all multicast groups they join before
	 * closing. bcast groups are reference counted, the last client
	 * to delete the group will wait till the group is physically
	 * deleted. Since all clients have closed this mac end point
	 * mi_bcast_ngrps must be zero at this point
	 */
	ASSERT(mip->mi_bcast_ngrps == 0);

	/*
	 * Don't let go of this if it has some flows.
	 * All other code guarantees no flows are added to a disabled
	 * mac, therefore it is sufficient to check for the flow table
	 * only here.
	 */
	mcip = mac_primary_client_handle(mip);
	if ((mcip != NULL) && mac_link_has_flows((mac_client_handle_t)mcip)) {
		rw_exit(&i_mac_impl_lock);
		return (ENOTEMPTY);
	}

	mip->mi_state_flags |= MIS_DISABLED;
	rw_exit(&i_mac_impl_lock);
	return (0);
}

int
mac_disable_nowait(mac_handle_t mh)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;
	int err;

	if ((err = i_mac_perim_enter_nowait(mip)) != 0)
		return (err);
	err = i_mac_disable(mip);
	i_mac_perim_exit(mip);
	return (err);
}

int
mac_disable(mac_handle_t mh)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;
	int err;

	i_mac_perim_enter(mip);
	err = i_mac_disable(mip);
	i_mac_perim_exit(mip);

	/*
	 * Clean up notification thread and wait for it to exit.
	 */
	if (err == 0)
		i_mac_notify_exit(mip);

	return (err);
}

/* ARGSUSED */
void
mac_multicast_refresh(mac_handle_t mh, mac_multicst_t refresh, void *arg,
    boolean_t add)
{
	mac_impl_t *mip = (mac_impl_t *)mh;

	i_mac_perim_enter((mac_impl_t *)mh);
	/*
	 * If no specific refresh function was given then default to the
	 * driver's m_multicst entry point.
	 */
	if (refresh == NULL) {
		refresh = mip->mi_multicst;
		arg = mip->mi_driver;
	}

	mac_bcast_refresh(mip, refresh, arg, add);
	i_mac_perim_exit((mac_impl_t *)mh);
}

void
mac_promisc_refresh(mac_handle_t mh, mac_setpromisc_t refresh, void *arg)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;

	/*
	 * If no specific refresh function was given then default to the
	 * driver's m_promisc entry point.
	 */
	if (refresh == NULL) {
		refresh = mip->mi_setpromisc;
		arg = mip->mi_driver;
	}
	ASSERT(refresh != NULL);

	/*
	 * Call the refresh function with the current promiscuity.
	 */
	refresh(arg, (mip->mi_devpromisc != 0));
}

/*
 * The mac client requests that the mac not to change its margin size to
 * be less than the specified value.  If "current" is B_TRUE, then the client
 * requests the mac not to change its margin size to be smaller than the
 * current size. Further, return the current margin size value in this case.
 *
 * We keep every requested size in an ordered list from largest to smallest.
 */
int
mac_margin_add(mac_handle_t mh, uint32_t *marginp, boolean_t current)
{
	mac_impl_t		*mip = (mac_impl_t *)mh;
	mac_margin_req_t	**pp, *p;
	int			err = 0;

	rw_enter(&(mip->mi_rw_lock), RW_WRITER);
	if (current)
		*marginp = mip->mi_margin;

	/*
	 * If the current margin value cannot satisfy the margin requested,
	 * return ENOTSUP directly.
	 */
	if (*marginp > mip->mi_margin) {
		err = ENOTSUP;
		goto done;
	}

	/*
	 * Check whether the given margin is already in the list. If so,
	 * bump the reference count.
	 */
	for (pp = &mip->mi_mmrp; (p = *pp) != NULL; pp = &p->mmr_nextp) {
		if (p->mmr_margin == *marginp) {
			/*
			 * The margin requested is already in the list,
			 * so just bump the reference count.
			 */
			p->mmr_ref++;
			goto done;
		}
		if (p->mmr_margin < *marginp)
			break;
	}


	p = kmem_zalloc(sizeof (mac_margin_req_t), KM_SLEEP);
	p->mmr_margin = *marginp;
	p->mmr_ref++;
	p->mmr_nextp = *pp;
	*pp = p;

done:
	rw_exit(&(mip->mi_rw_lock));
	return (err);
}

/*
 * The mac client requests to cancel its previous mac_margin_add() request.
 * We remove the requested margin size from the list.
 */
int
mac_margin_remove(mac_handle_t mh, uint32_t margin)
{
	mac_impl_t		*mip = (mac_impl_t *)mh;
	mac_margin_req_t	**pp, *p;
	int			err = 0;

	rw_enter(&(mip->mi_rw_lock), RW_WRITER);
	/*
	 * Find the entry in the list for the given margin.
	 */
	for (pp = &(mip->mi_mmrp); (p = *pp) != NULL; pp = &(p->mmr_nextp)) {
		if (p->mmr_margin == margin) {
			if (--p->mmr_ref == 0)
				break;

			/*
			 * There is still a reference to this address so
			 * there's nothing more to do.
			 */
			goto done;
		}
	}

	/*
	 * We did not find an entry for the given margin.
	 */
	if (p == NULL) {
		err = ENOENT;
		goto done;
	}

	ASSERT(p->mmr_ref == 0);

	/*
	 * Remove it from the list.
	 */
	*pp = p->mmr_nextp;
	kmem_free(p, sizeof (mac_margin_req_t));
done:
	rw_exit(&(mip->mi_rw_lock));
	return (err);
}

boolean_t
mac_margin_update(mac_handle_t mh, uint32_t margin)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;
	uint32_t	margin_needed = 0;

	rw_enter(&(mip->mi_rw_lock), RW_WRITER);

	if (mip->mi_mmrp != NULL)
		margin_needed = mip->mi_mmrp->mmr_margin;

	if (margin_needed <= margin)
		mip->mi_margin = margin;

	rw_exit(&(mip->mi_rw_lock));

	if (margin_needed <= margin)
		i_mac_notify(mip, MAC_NOTE_MARGIN);

	return (margin_needed <= margin);
}

/*
 * MAC Type Plugin functions.
 */

mactype_t *
mactype_getplugin(const char *pname)
{
	mactype_t	*mtype = NULL;
	boolean_t	tried_modload = B_FALSE;

	mutex_enter(&i_mactype_lock);

find_registered_mactype:
	if (mod_hash_find(i_mactype_hash, (mod_hash_key_t)pname,
	    (mod_hash_val_t *)&mtype) != 0) {
		if (!tried_modload) {
			/*
			 * If the plugin has not yet been loaded, then
			 * attempt to load it now.  If modload() succeeds,
			 * the plugin should have registered using
			 * mactype_register(), in which case we can go back
			 * and attempt to find it again.
			 */
			if (modload(MACTYPE_KMODDIR, (char *)pname) != -1) {
				tried_modload = B_TRUE;
				goto find_registered_mactype;
			}
		}
	} else {
		/*
		 * Note that there's no danger that the plugin we've loaded
		 * could be unloaded between the modload() step and the
		 * reference count bump here, as we're holding
		 * i_mactype_lock, which mactype_unregister() also holds.
		 */
		atomic_inc_32(&mtype->mt_ref);
	}

	mutex_exit(&i_mactype_lock);
	return (mtype);
}

mactype_register_t *
mactype_alloc(uint_t mactype_version)
{
	mactype_register_t *mtrp;

	/*
	 * Make sure there isn't a version mismatch between the plugin and
	 * the framework.  In the future, if multiple versions are
	 * supported, this check could become more sophisticated.
	 */
	if (mactype_version != MACTYPE_VERSION)
		return (NULL);

	mtrp = kmem_zalloc(sizeof (mactype_register_t), KM_SLEEP);
	mtrp->mtr_version = mactype_version;
	return (mtrp);
}

void
mactype_free(mactype_register_t *mtrp)
{
	kmem_free(mtrp, sizeof (mactype_register_t));
}

int
mactype_register(mactype_register_t *mtrp)
{
	mactype_t	*mtp;
	mactype_ops_t	*ops = mtrp->mtr_ops;

	/* Do some sanity checking before we register this MAC type. */
	if (mtrp->mtr_ident == NULL || ops == NULL)
		return (EINVAL);

	/*
	 * Verify that all mandatory callbacks are set in the ops
	 * vector.
	 */
	if (ops->mtops_unicst_verify == NULL ||
	    ops->mtops_multicst_verify == NULL ||
	    ops->mtops_sap_verify == NULL ||
	    ops->mtops_header == NULL ||
	    ops->mtops_header_info == NULL) {
		return (EINVAL);
	}

	mtp = kmem_zalloc(sizeof (*mtp), KM_SLEEP);
	mtp->mt_ident = mtrp->mtr_ident;
	mtp->mt_ops = *ops;
	mtp->mt_type = mtrp->mtr_mactype;
	mtp->mt_nativetype = mtrp->mtr_nativetype;
	mtp->mt_addr_length = mtrp->mtr_addrlen;
	if (mtrp->mtr_brdcst_addr != NULL) {
		mtp->mt_brdcst_addr = kmem_alloc(mtrp->mtr_addrlen, KM_SLEEP);
		bcopy(mtrp->mtr_brdcst_addr, mtp->mt_brdcst_addr,
		    mtrp->mtr_addrlen);
	}

	mtp->mt_stats = mtrp->mtr_stats;
	mtp->mt_statcount = mtrp->mtr_statcount;

	mtp->mt_mapping = mtrp->mtr_mapping;
	mtp->mt_mappingcount = mtrp->mtr_mappingcount;

	if (mod_hash_insert(i_mactype_hash,
	    (mod_hash_key_t)mtp->mt_ident, (mod_hash_val_t)mtp) != 0) {
		kmem_free(mtp->mt_brdcst_addr, mtp->mt_addr_length);
		kmem_free(mtp, sizeof (*mtp));
		return (EEXIST);
	}
	return (0);
}

int
mactype_unregister(const char *ident)
{
	mactype_t	*mtp;
	mod_hash_val_t	val;
	int 		err;

	/*
	 * Let's not allow MAC drivers to use this plugin while we're
	 * trying to unregister it.  Holding i_mactype_lock also prevents a
	 * plugin from unregistering while a MAC driver is attempting to
	 * hold a reference to it in i_mactype_getplugin().
	 */
	mutex_enter(&i_mactype_lock);

	if ((err = mod_hash_find(i_mactype_hash, (mod_hash_key_t)ident,
	    (mod_hash_val_t *)&mtp)) != 0) {
		/* A plugin is trying to unregister, but it never registered. */
		err = ENXIO;
		goto done;
	}

	if (mtp->mt_ref != 0) {
		err = EBUSY;
		goto done;
	}

	err = mod_hash_remove(i_mactype_hash, (mod_hash_key_t)ident, &val);
	ASSERT(err == 0);
	if (err != 0) {
		/* This should never happen, thus the ASSERT() above. */
		err = EINVAL;
		goto done;
	}
	ASSERT(mtp == (mactype_t *)val);

	if (mtp->mt_brdcst_addr != NULL)
		kmem_free(mtp->mt_brdcst_addr, mtp->mt_addr_length);
	kmem_free(mtp, sizeof (mactype_t));
done:
	mutex_exit(&i_mactype_lock);
	return (err);
}

/*
 * Checks the size of the value size specified for a property as
 * part of a property operation. Returns B_TRUE if the size is
 * correct, B_FALSE otherwise.
 */
boolean_t
mac_prop_check_size(mac_prop_id_t id, uint_t valsize, boolean_t is_range)
{
	uint_t minsize = 0;

	if (is_range)
		return (valsize >= sizeof (mac_propval_range_t));

	switch (id) {
	case MAC_PROP_ZONE:
		minsize = sizeof (dld_ioc_zid_t);
		break;
	case MAC_PROP_AUTOPUSH:
		if (valsize != 0)
			minsize = sizeof (struct dlautopush);
		break;
	case MAC_PROP_TAGMODE:
		minsize = sizeof (link_tagmode_t);
		break;
	case MAC_PROP_RESOURCE:
	case MAC_PROP_RESOURCE_EFF:
		minsize = sizeof (mac_resource_props_t);
		break;
	case MAC_PROP_DUPLEX:
		minsize = sizeof (link_duplex_t);
		break;
	case MAC_PROP_SPEED:
		minsize = sizeof (uint64_t);
		break;
	case MAC_PROP_STATUS:
		minsize = sizeof (link_state_t);
		break;
	case MAC_PROP_AUTONEG:
	case MAC_PROP_EN_AUTONEG:
		minsize = sizeof (uint8_t);
		break;
	case MAC_PROP_MTU:
	case MAC_PROP_LLIMIT:
	case MAC_PROP_LDECAY:
	case MAC_PROP_GVRP_TIMEOUT:
		minsize = sizeof (uint32_t);
		break;
	case MAC_PROP_FLOWCTRL:
	case MAC_PROP_FLOWCTRL_EFFECTIVE:
		minsize = sizeof (link_flowctrl_t);
		break;
	case MAC_PROP_ADV_10GFDX_CAP:
	case MAC_PROP_EN_10GFDX_CAP:
	case MAC_PROP_ADV_1000HDX_CAP:
	case MAC_PROP_EN_1000HDX_CAP:
	case MAC_PROP_ADV_100FDX_CAP:
	case MAC_PROP_EN_100FDX_CAP:
	case MAC_PROP_ADV_100HDX_CAP:
	case MAC_PROP_EN_100HDX_CAP:
	case MAC_PROP_ADV_10FDX_CAP:
	case MAC_PROP_EN_10FDX_CAP:
	case MAC_PROP_ADV_10HDX_CAP:
	case MAC_PROP_EN_10HDX_CAP:
	case MAC_PROP_ADV_100T4_CAP:
	case MAC_PROP_EN_100T4_CAP:
		minsize = sizeof (uint8_t);
		break;
	case MAC_PROP_PVID:
		minsize = sizeof (uint16_t);
		break;
	case MAC_PROP_VLAN_ANNOUNCE:
		minsize = sizeof (mac_vlan_announce_t);
		break;
	case MAC_PROP_IPTUN_HOPLIMIT:
		minsize = sizeof (uint32_t);
		break;
	case MAC_PROP_IPTUN_ENCAPLIMIT:
		minsize = sizeof (uint32_t);
		break;
	case MAC_PROP_MAX_TX_RINGS_AVAIL:
	case MAC_PROP_MAX_RX_RINGS_AVAIL:
	case MAC_PROP_MAX_RXHWCLNT_AVAIL:
	case MAC_PROP_MAX_TXHWCLNT_AVAIL:
		minsize = sizeof (uint_t);
		break;
	case MAC_PROP_WL_ESSID:
		minsize = sizeof (wl_linkstatus_t);
		break;
	case MAC_PROP_WL_BSSID:
		minsize = sizeof (wl_bssid_t);
		break;
	case MAC_PROP_WL_BSSTYPE:
		minsize = sizeof (wl_bss_type_t);
		break;
	case MAC_PROP_WL_LINKSTATUS:
		minsize = sizeof (wl_linkstatus_t);
		break;
	case MAC_PROP_WL_DESIRED_RATES:
		minsize = sizeof (wl_rates_t);
		break;
	case MAC_PROP_WL_SUPPORTED_RATES:
		minsize = sizeof (wl_rates_t);
		break;
	case MAC_PROP_WL_AUTH_MODE:
		minsize = sizeof (wl_authmode_t);
		break;
	case MAC_PROP_WL_ENCRYPTION:
		minsize = sizeof (wl_encryption_t);
		break;
	case MAC_PROP_WL_RSSI:
		minsize = sizeof (wl_rssi_t);
		break;
	case MAC_PROP_WL_PHY_CONFIG:
		minsize = sizeof (wl_phy_conf_t);
		break;
	case MAC_PROP_WL_CAPABILITY:
		minsize = sizeof (wl_capability_t);
		break;
	case MAC_PROP_WL_WPA:
		minsize = sizeof (wl_wpa_t);
		break;
	case MAC_PROP_WL_SCANRESULTS:
		minsize = sizeof (wl_wpa_ess_t);
		break;
	case MAC_PROP_WL_POWER_MODE:
		minsize = sizeof (wl_ps_mode_t);
		break;
	case MAC_PROP_WL_RADIO:
		minsize = sizeof (wl_radio_t);
		break;
	case MAC_PROP_WL_ESS_LIST:
		minsize = sizeof (wl_ess_list_t);
		break;
	case MAC_PROP_WL_KEY_TAB:
		minsize = sizeof (wl_wep_key_tab_t);
		break;
	case MAC_PROP_WL_CREATE_IBSS:
		minsize = sizeof (wl_create_ibss_t);
		break;
	case MAC_PROP_WL_SETOPTIE:
		minsize = sizeof (wl_wpa_ie_t);
		break;
	case MAC_PROP_WL_DELKEY:
		minsize = sizeof (wl_del_key_t);
		break;
	case MAC_PROP_WL_KEY:
		minsize = sizeof (wl_key_t);
		break;
	case MAC_PROP_WL_MLME:
		minsize = sizeof (wl_mlme_t);
		break;
	case MAC_PROP_MAC_ADDR:
		if (valsize != 0)
			minsize = sizeof (dld_mac_addr_t);
		break;
	case MAC_PROP_NTCS:
	case MAC_PROP_PFC:
		minsize = sizeof (uint8_t);
		break;
	}

	return (valsize >= minsize);
}

/*
 * mac_set_prop() sets MAC or hardware driver properties:
 *
 * - MAC-managed properties such as resource properties include maxbw,
 *   priority, and cpu binding list, as well as the default port VID
 *   used by bridging. These properties are consumed by the MAC layer
 *   itself and not passed down to the driver. For resource control
 *   properties, this function invokes mac_set_resources() which will
 *   cache the property value in mac_impl_t and may call
 *   mac_client_set_resource() to update property value of the primary
 *   mac client, if it exists.
 *
 * - Properties which act on the hardware and must be passed to the
 *   driver, such as MTU, through the driver's mc_setprop() entry point.
 */
int
mac_set_prop(mac_handle_t mh, mac_prop_id_t id, char *name, void *val,
    uint_t valsize)
{
	int err = ENOTSUP;
	mac_impl_t *mip = (mac_impl_t *)mh;

	ASSERT(MAC_PERIM_HELD(mh));

	switch (id) {
	case MAC_PROP_RESOURCE: {
		mac_resource_props_t *mrp;

		/* call mac_set_resources() for MAC properties */
		ASSERT(valsize >= sizeof (mac_resource_props_t));
		mrp = kmem_zalloc(sizeof (*mrp), KM_SLEEP);
		bcopy(val, mrp, sizeof (*mrp));
		err = mac_set_resources(mh, mrp);
		kmem_free(mrp, sizeof (*mrp));
		break;
	}

	case MAC_PROP_PVID:
		ASSERT(valsize >= sizeof (uint16_t));
		if (mip->mi_state_flags & MIS_IS_VNIC)
			return (EINVAL);
		err = mac_set_pvid(mh, *(uint16_t *)val);
		break;

	case MAC_PROP_VLAN_ANNOUNCE: {
		mac_vlan_announce_t t;

		ASSERT(valsize >= sizeof (mac_vlan_announce_t));
		if (mip->mi_state_flags & MIS_IS_VNIC)
			return (EINVAL);
		bcopy(val, &t, sizeof (mac_vlan_announce_t));
		err = mac_set_vlan_announce(mh, t);
		break;
	}
	case MAC_PROP_GVRP_TIMEOUT: {
		uint32_t to;

		ASSERT(valsize >= sizeof (uint32_t));
		if (mip->mi_state_flags & MIS_IS_VNIC)
			return (EINVAL);
		bcopy(val, &to, sizeof (to));

		if (to < mac_gvrp_get_timeout_min() ||
		    to > mac_gvrp_get_timeout_max())
			return (EINVAL);

		err = mac_gvrp_set_timeout(mh, to);
		break;
	}

	case MAC_PROP_MTU: {
		uint32_t mtu;

		ASSERT(valsize >= sizeof (uint32_t));
		bcopy(val, &mtu, sizeof (mtu));
		err = mac_set_mtu(mh, mtu, NULL);
		break;
	}

	case MAC_PROP_LLIMIT:
	case MAC_PROP_LDECAY: {
		uint32_t learnval;

		if (valsize < sizeof (learnval) ||
		    (mip->mi_state_flags & MIS_IS_VNIC))
			return (EINVAL);
		bcopy(val, &learnval, sizeof (learnval));
		if (learnval == 0 && id == MAC_PROP_LDECAY)
			return (EINVAL);
		if (id == MAC_PROP_LLIMIT)
			mip->mi_llimit = learnval;
		else
			mip->mi_ldecay = learnval;
		err = 0;
		break;
	}

	default:
		/* For other driver properties, call driver's callback */
		if (mip->mi_callbacks->mc_callbacks & MC_SETPROP) {
			err = mip->mi_callbacks->mc_setprop(mip->mi_driver,
			    name, id, valsize, val);
		}
	}

	/*
	 * If we modified the flow control mode, send a notification to
	 * interested clients. We directly use the mip instead of checking
	 * for a vnic and using its upper mip. Currently, setting a
	 * Flow control property for a VNIC fails (see vnic_m_setprop),
	 * but if add the supports then we could pass the notification for
	 * the VNIC's mip as well.
	 */
	if ((id == MAC_PROP_FLOWCTRL || id == MAC_PROP_PFC) && err == 0)
		i_mac_notify(mip, MAC_NOTE_FC_MODE);

	return (err);
}

void
mac_get_ringids(mac_client_handle_t mch, uint_t rxringids[], uint_t *nrxrings,
    boolean_t *rxdedicated, uint_t txringids[], uint_t *ntxrings,
    boolean_t *txdedicated)
{
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;
	mac_group_t		*rgroup, *tgroup;
	flow_entry_t		*flent = mcip->mci_flent;
	mac_ring_t		*ring;
	int			i;

	*nrxrings = *ntxrings = 0;

	if (flent != NULL) {
		rgroup = flent->fe_rx_ring_group;
		tgroup = flent->fe_tx_ring_group;

		if (rgroup != NULL) {
			*rxdedicated =
			    (rgroup->mrg_state == MAC_GROUP_STATE_RESERVED);
			*nrxrings = rgroup->mrg_cur_count;
			for (i = 0, ring = rgroup->mrg_rings; ring != NULL;
			    ring = ring->mr_next, i++) {
				rxringids[i] = ring->mr_index;
			}
		}
		if (tgroup != NULL) {
			*txdedicated =
			    (tgroup->mrg_state == MAC_GROUP_STATE_RESERVED);
			*ntxrings = tgroup->mrg_cur_count;
			for (i = 0, ring = tgroup->mrg_rings; ring != NULL;
			    ring = ring->mr_next, i++) {
				txringids[i] = ring->mr_index;
			}
		}
	}
}

/*
 * mac_get_prop() gets MAC or device driver properties.
 *
 * If the property is a driver property, mac_get_prop() calls driver's callback
 * entry point to get it.
 * If the property is a MAC property, mac_get_prop() invokes mac_get_resources()
 * which returns the cached value in mac_impl_t.
 */
int
mac_get_prop(mac_handle_t mh, mac_prop_id_t id, char *name, void *val,
    uint_t valsize)
{
	int err = ENOTSUP;
	mac_impl_t *mip = (mac_impl_t *)mh;
	uint_t	rings;
	uint_t	vlinks;

	bzero(val, valsize);

	switch (id) {
	case MAC_PROP_RESOURCE: {
		mac_resource_props_t *mrp;

		/* If mac property, read from cache */
		ASSERT(valsize >= sizeof (mac_resource_props_t));
		mrp = kmem_zalloc(sizeof (*mrp), KM_SLEEP);
		mac_get_resources(mh, mrp);
		bcopy(mrp, val, sizeof (*mrp));
		kmem_free(mrp, sizeof (*mrp));
		return (0);
	}
	case MAC_PROP_RESOURCE_EFF: {
		mac_resource_props_t *mrp;

		/* If mac effective property, read from client */
		ASSERT(valsize >= sizeof (mac_resource_props_t));
		mrp = kmem_zalloc(sizeof (*mrp), KM_SLEEP);
		mac_get_effective_resources(mh, mrp);
		bcopy(mrp, val, sizeof (*mrp));
		kmem_free(mrp, sizeof (*mrp));
		return (0);
	}

	case MAC_PROP_PVID:
		ASSERT(valsize >= sizeof (uint16_t));
		if (mip->mi_state_flags & MIS_IS_VNIC)
			return (EINVAL);
		*(uint16_t *)val = mac_get_pvid(mh);
		return (0);

	case MAC_PROP_LLIMIT:
	case MAC_PROP_LDECAY:
		ASSERT(valsize >= sizeof (uint32_t));
		if (mip->mi_state_flags & MIS_IS_VNIC)
			return (EINVAL);
		if (id == MAC_PROP_LLIMIT)
			bcopy(&mip->mi_llimit, val, sizeof (mip->mi_llimit));
		else
			bcopy(&mip->mi_ldecay, val, sizeof (mip->mi_ldecay));
		return (0);

	case MAC_PROP_MTU: {
		uint32_t sdu;

		ASSERT(valsize >= sizeof (uint32_t));
		mac_sdu_get2(mh, NULL, &sdu, NULL);
		bcopy(&sdu, val, sizeof (sdu));

		return (0);
	}
	case MAC_PROP_STATUS: {
		link_state_t link_state;

		if (valsize < sizeof (link_state))
			return (EINVAL);
		link_state = mac_link_get(mh);
		bcopy(&link_state, val, sizeof (link_state));

		return (0);
	}

	case MAC_PROP_MAX_RX_RINGS_AVAIL:
	case MAC_PROP_MAX_TX_RINGS_AVAIL:
		ASSERT(valsize >= sizeof (uint_t));
		rings = id == MAC_PROP_MAX_RX_RINGS_AVAIL ?
		    mac_rxavail_get(mh) : mac_txavail_get(mh);
		bcopy(&rings, val, sizeof (uint_t));
		return (0);

	case MAC_PROP_MAX_RXHWCLNT_AVAIL:
	case MAC_PROP_MAX_TXHWCLNT_AVAIL:
		ASSERT(valsize >= sizeof (uint_t));
		vlinks = id == MAC_PROP_MAX_RXHWCLNT_AVAIL ?
		    mac_rxhwlnksavail_get(mh) : mac_txhwlnksavail_get(mh);
		bcopy(&vlinks, val, sizeof (uint_t));
		return (0);

	case MAC_PROP_RXRINGSRANGE:
	case MAC_PROP_TXRINGSRANGE:
		/*
		 * The value for these properties are returned through
		 * the MAC_PROP_RESOURCE property.
		 */
		return (0);

	case MAC_PROP_VLAN_ANNOUNCE: {
		mac_vlan_announce_t t;

		ASSERT(valsize >= sizeof (mac_vlan_announce_t));
		if (mip->mi_state_flags & MIS_IS_VNIC)
			return (EINVAL);
		t = mac_get_vlan_announce(mh);
		bcopy(&t, val, sizeof (mac_vlan_announce_t));
		return (0);
	}
	case MAC_PROP_GVRP_TIMEOUT:
		ASSERT(valsize >= sizeof (uint32_t));
		if (mip->mi_state_flags & MIS_IS_VNIC)
			return (EINVAL);
		bcopy(&mip->mi_gvrp_timeout, val, sizeof (uint32_t));
		return (0);

	case MAC_PROP_NTCS: {
		ASSERT(valsize >= sizeof (uint8_t));
		bcopy(&mip->mi_ntcs, val, sizeof (uint8_t));
		return (0);
	}

	default:
		break;

	}

	/* If driver property, request from driver */
	if (mip->mi_callbacks->mc_callbacks & MC_GETPROP) {
		err = mip->mi_callbacks->mc_getprop(mip->mi_driver, name, id,
		    valsize, val);
	}

	return (err);
}

/*
 * Helper function to initialize the range structure for use in
 * mac_get_prop. If the type can be other than uint32, we can
 * pass that as an arg.
 */
static void
_mac_set_range(mac_propval_range_t *range, uint32_t min, uint32_t max)
{
	range->mpr_count = 1;
	range->mpr_type = MAC_PROPVAL_UINT32;
	range->mpr_range_uint32[0].mpur_min = min;
	range->mpr_range_uint32[0].mpur_max = max;
}

/*
 * Returns information about the specified property, such as default
 * values or permissions.
 */
int
mac_prop_info(mac_handle_t mh, mac_prop_id_t id, char *name,
    void *default_val, uint_t default_size, mac_propval_range_t *range,
    uint_t *perm)
{
	mac_prop_info_state_t state;
	mac_impl_t *mip = (mac_impl_t *)mh;
	uint_t	max;

	/*
	 * A property is read/write by default unless the driver says
	 * otherwise.
	 */
	if (perm != NULL)
		*perm = MAC_PROP_PERM_RW;

	if (default_val != NULL)
		bzero(default_val, default_size);

	/*
	 * First, handle framework properties for which we don't need to
	 * involve the driver.
	 */
	switch (id) {
	case MAC_PROP_RESOURCE: {
		if (default_val != NULL) {
			mac_resource_props_t	*mrp;
			mac_client_impl_t	*mcip;

			/*
			 * For now, only the default value of rxfanout is
			 * returned from the mac. The default values of other
			 * mac resource props are hard coded in the library
			 * itself. In future, in order to return the default
			 * values of other mac resource props from mac, one
			 * needs to set those values in following mrp and also
			 * set the corresponding mask.
			 */
			mrp = kmem_zalloc(sizeof (*mrp), KM_SLEEP);
			mrp->mrp_mask = MRP_RXFANOUT;
			mcip = mac_primary_client_handle(mip);
			if (mcip != NULL) {
				mrp->mrp_rxfanout =
				    mac_default_rx_fanout_cnt(mcip);
			}
			if (default_size >= sizeof (*mrp))
				bcopy(mrp, default_val, sizeof (*mrp));
			kmem_free(mrp, sizeof (*mrp));
		}
		return (0);
	}

	case MAC_PROP_PVID:
	case MAC_PROP_LLIMIT:
	case MAC_PROP_LDECAY:
		return (0);

	case MAC_PROP_MAX_RX_RINGS_AVAIL:
	case MAC_PROP_MAX_TX_RINGS_AVAIL:
	case MAC_PROP_MAX_RXHWCLNT_AVAIL:
	case MAC_PROP_MAX_TXHWCLNT_AVAIL:
		if (perm != NULL)
			*perm = MAC_PROP_PERM_READ;
		return (0);

	case MAC_PROP_USRPRI:
		if (perm != NULL)
			*perm = MAC_PROP_PERM_RW;
		if (mip->mi_dcb_flags == 0) {
			range->mpr_count = 0;
			return (0);
		}
		_mac_set_range(range, 0, mip->mi_ntcs - 1);
		return (0);

	case MAC_PROP_RXRINGSRANGE:
	case MAC_PROP_TXRINGSRANGE:
		/*
		 * Currently, we support range for RX and TX rings properties.
		 * When we extend this support to maxbw, cpus and priority,
		 * we should move this to mac_get_resources.
		 * There is no default value for RX or TX rings.
		 */
		if ((mip->mi_state_flags & MIS_IS_VNIC) &&
		    mac_is_vnic_primary(mh)) {
			/*
			 * We don't support setting rings for a VLAN
			 * data link because it shares its ring with the
			 * primary MAC client.
			 */
			if (perm != NULL)
				*perm = MAC_PROP_PERM_READ;
			if (range != NULL)
				range->mpr_count = 0;
		} else if (range != NULL) {
			if (mip->mi_state_flags & MIS_IS_VNIC)
				mh = mac_get_lower_mac_handle(mh);
			mip = (mac_impl_t *)mh;
			if ((id == MAC_PROP_RXRINGSRANGE &&
			    mip->mi_rx_group_type == MAC_GROUP_TYPE_STATIC) ||
			    (id == MAC_PROP_TXRINGSRANGE &&
			    mip->mi_tx_group_type == MAC_GROUP_TYPE_STATIC)) {
				if (id == MAC_PROP_RXRINGSRANGE) {
					if ((mac_rxhwlnksavail_get(mh) +
					    mac_rxhwlnksrsvd_get(mh)) <= 1) {
						/*
						 * doesn't support groups or
						 * rings
						 */
						range->mpr_count = 0;
					} else {
						/*
						 * supports specifying groups,
						 * but not rings
						 */
						_mac_set_range(range, 0, 0);
					}
				} else {
					if ((mac_txhwlnksavail_get(mh) +
					    mac_txhwlnksrsvd_get(mh)) <= 1) {
						/*
						 * doesn't support groups or
						 * rings
						 */
						range->mpr_count = 0;
					} else {
						/*
						 * supports specifying groups,
						 * but not rings
						 */
						_mac_set_range(range, 0, 0);
					}
				}
			} else {
				max = id == MAC_PROP_RXRINGSRANGE ?
				    mac_rxavail_get(mh) + mac_rxrsvd_get(mh) :
				    mac_txavail_get(mh) + mac_txrsvd_get(mh);
				if (max <= 1) {
					/*
					 * doesn't support groups or
					 * rings
					 */
					range->mpr_count = 0;
				} else  {
					/*
					 * -1 because we have to leave out the
					 * default ring.
					 */
					_mac_set_range(range, 1, max - 1);
				}
			}
		}
		return (0);

	case MAC_PROP_STATUS:
		if (perm != NULL)
			*perm = MAC_PROP_PERM_READ;
		return (0);

	case MAC_PROP_GVRP_TIMEOUT:
		if (range != NULL) {
			uint32_t min = mac_gvrp_get_timeout_min();
			uint32_t max = mac_gvrp_get_timeout_max();
			_mac_set_range(range, min, max);
		}
		return (0);
	}

	/*
	 * Get the property info from the driver if it implements the
	 * property info entry point.
	 */
	bzero(&state, sizeof (state));

	if (mip->mi_callbacks->mc_callbacks & MC_PROPINFO) {
		state.pr_default = default_val;
		state.pr_default_size = default_size;

		/*
		 * The caller specifies the maximum number of ranges
		 * it can accomodate using mpr_count. We don't touch
		 * this value until the driver returns from its
		 * mc_propinfo() callback, and ensure we don't exceed
		 * this number of range as the driver defines
		 * supported range from its mc_propinfo().
		 *
		 * pr_range_cur_count keeps track of how many ranges
		 * were defined by the driver from its mc_propinfo()
		 * entry point.
		 *
		 * On exit, the user-specified range mpr_count returns
		 * the number of ranges specified by the driver on
		 * success, or the number of ranges it wanted to
		 * define if that number of ranges could not be
		 * accomodated by the specified range structure.  In
		 * the latter case, the caller will be able to
		 * allocate a larger range structure, and query the
		 * property again.
		 */
		state.pr_range_cur_count = 0;
		state.pr_range = range;

		mip->mi_callbacks->mc_propinfo(mip->mi_driver, name, id,
		    (mac_prop_info_handle_t)&state);

		if (state.pr_flags & MAC_PROP_INFO_RANGE)
			range->mpr_count = state.pr_range_cur_count;

		/*
		 * The operation could fail if the buffer supplied by
		 * the user was too small for the range or default
		 * value of the property.
		 */
		if (state.pr_errno != 0)
			return (state.pr_errno);

		if (perm != NULL && state.pr_flags & MAC_PROP_INFO_PERM)
			*perm = state.pr_perm;
	}

	/*
	 * The MAC layer may want to provide default values or allowed
	 * ranges for properties if the driver does not provide a
	 * property info entry point, or that entry point exists, but
	 * it did not provide a default value or allowed ranges for
	 * that property.
	 */
	switch (id) {
	case MAC_PROP_MTU: {
		uint32_t sdu;

		mac_sdu_get2(mh, NULL, &sdu, NULL);

		if (range != NULL && !(state.pr_flags &
		    MAC_PROP_INFO_RANGE)) {
			/* MTU range */
			_mac_set_range(range, sdu, sdu);
		}

		if (default_val != NULL && !(state.pr_flags &
		    MAC_PROP_INFO_DEFAULT)) {
			if (mip->mi_info.mi_media == DL_ETHER)
				sdu = ETHERMTU;
			/* default MTU value */
			bcopy(&sdu, default_val, sizeof (sdu));
		}
	}
	}

	return (0);
}

int
mac_fastpath_disable(mac_handle_t mh)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;

	if ((mip->mi_state_flags & MIS_LEGACY) == 0)
		return (0);

	return (mip->mi_capab_legacy.ml_fastpath_disable(mip->mi_driver));
}

void
mac_fastpath_enable(mac_handle_t mh)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;

	if ((mip->mi_state_flags & MIS_LEGACY) == 0)
		return;

	mip->mi_capab_legacy.ml_fastpath_enable(mip->mi_driver);
}

void
mac_register_priv_prop(mac_impl_t *mip, char **priv_props)
{
	uint_t nprops, i;

	if (priv_props == NULL)
		return;

	nprops = 0;
	while (priv_props[nprops] != NULL)
		nprops++;
	if (nprops == 0)
		return;


	mip->mi_priv_prop = kmem_zalloc(nprops * sizeof (char *), KM_SLEEP);

	for (i = 0; i < nprops; i++) {
		mip->mi_priv_prop[i] = kmem_zalloc(MAXLINKPROPNAME, KM_SLEEP);
		(void) strlcpy(mip->mi_priv_prop[i], priv_props[i],
		    MAXLINKPROPNAME);
	}

	mip->mi_priv_prop_count = nprops;
}

void
mac_unregister_priv_prop(mac_impl_t *mip)
{
	uint_t i;

	if (mip->mi_priv_prop_count == 0) {
		ASSERT(mip->mi_priv_prop == NULL);
		return;
	}

	for (i = 0; i < mip->mi_priv_prop_count; i++)
		kmem_free(mip->mi_priv_prop[i], MAXLINKPROPNAME);
	kmem_free(mip->mi_priv_prop, mip->mi_priv_prop_count *
	    sizeof (char *));

	mip->mi_priv_prop = NULL;
	mip->mi_priv_prop_count = 0;
}

static void
mac_ring_list_add(mac_impl_t *mip, mac_ring_t *ring)
{
	ASSERT(ring->mr_list_next == NULL);
	ring->mr_list_next = mip->mi_ring_list;
	mip->mi_ring_list = ring;
}

static void
mac_ring_list_remove(mac_impl_t *mip, mac_ring_t *ring)
{
	mac_ring_t	**ringp;

	ringp = &mip->mi_ring_list;
	while (*ringp != NULL) {
		if (*ringp == ring)
			break;
		ringp = &(*ringp)->mr_list_next;
	}
	ASSERT(*ringp == ring);
	*ringp = ring->mr_list_next;
	ring->mr_list_next = NULL;
}

/*
 * mac_ring_t 'mr' macros. Some rogue drivers may access ring structure
 * (by invoking mac_rx()) even after processing mac_stop_ring(). In such
 * cases if MAC free's the ring structure after mac_stop_ring(), any
 * illegal access to the ring structure coming from the driver will panic
 * the system. In order to protect the system from such inadverent access,
 * we maintain a cache of rings in the mac_impl_t after they get free'd up.
 * When packets are received on free'd up rings, MAC (through the generation
 * count mechanism) will drop such packets.
 */
static mac_ring_t *
mac_ring_alloc(mac_impl_t *mip)
{
	mac_ring_t *ring;

	mutex_enter(&mip->mi_ring_lock);
	if (mip->mi_ring_freelist != NULL) {
		ring = mip->mi_ring_freelist;
		mip->mi_ring_freelist = ring->mr_next;

		/*
		 * ring->mr_next needs to be cleared because we are taking
		 * the ring off the freelist. This is taken care of by the
		 * bzero, which also clears other fields. The mr_state
		 * assignment is needed to mark the structure as initialized.
		 */
		bzero(ring, sizeof (mac_ring_t));
		ring->mr_state = MR_FREE;
	} else {
		ring = kmem_cache_alloc(mac_ring_cache, KM_SLEEP);
	}
	mac_ring_list_add(mip, ring);
	mutex_exit(&mip->mi_ring_lock);
	ASSERT((ring != NULL) && (ring->mr_state == MR_FREE));
	return (ring);
}

static void
mac_ring_free(mac_impl_t *mip, mac_ring_t *ring)
{
	ASSERT(ring->mr_state == MR_FREE);

	mutex_enter(&ring->mr_lock);
	ring->mr_worker_state = 0;
	cv_signal(&ring->mr_cv);
	while (ring->mr_worker != NULL)
		cv_wait(&ring->mr_cv, &ring->mr_lock);

	if (ring->mr_worker_obj != NULL) {
		numaio_object_destroy(ring->mr_worker_obj);
		ring->mr_worker_obj = NULL;
	}
	if (ring->mr_intr_obj != NULL) {
		numaio_object_destroy(ring->mr_intr_obj);
		ring->mr_intr_obj = NULL;
	}
	if (ring->mr_type == MAC_RING_TYPE_TX) {
		freemsgchain(ring->mr_tx_queue);
		ring->mr_tx_queue = NULL;
		ring->mr_tx_tailp = &ring->mr_tx_queue;
		ring->mr_tx_cnt = 0;
	}
	ring->mr_state = MR_FREE;
	ring->mr_flag = 0;
	ring->mr_mip = NULL;
	mac_ring_stat_delete(ring);
	mac_hwlane_stat_delete(ring);
	mutex_exit(&ring->mr_lock);

	mutex_enter(&mip->mi_ring_lock);
	ring->mr_next = mip->mi_ring_freelist;
	mip->mi_ring_freelist = ring;
	mac_ring_list_remove(mip, ring);
	mutex_exit(&mip->mi_ring_lock);
}

static void
mac_ring_freeall(mac_impl_t *mip)
{
	mac_ring_t *ring_next;
	mutex_enter(&mip->mi_ring_lock);
	mac_ring_t *ring = mip->mi_ring_freelist;
	while (ring != NULL) {
		ASSERT(ring->mr_state == MR_FREE);
		ring_next = ring->mr_next;
		kmem_cache_free(mac_ring_cache, ring);
		ring = ring_next;
	}
	mip->mi_ring_freelist = NULL;
	mutex_exit(&mip->mi_ring_lock);
}

int
mac_start_ring(mac_ring_t *ring)
{
	int rv = 0;

	ASSERT(ring->mr_state == MR_FREE);

	if (ring->mr_start != NULL) {
		rv = ring->mr_start(ring->mr_driver, ring->mr_gen_num);
		if (rv != 0)
			return (rv);
	}

	ring->mr_state = MR_INUSE;
	ring->mr_flag = 0;

	if (ring->mr_type == MAC_RING_TYPE_RX) {
		bzero(&ring->mr_rx_stat, sizeof (mac_rx_stats_t));
	} else if (ring->mr_type == MAC_RING_TYPE_TX) {
		bzero(&ring->mr_tx_stat, sizeof (mac_tx_stats_t));
	}
	return (rv);
}

void
mac_stop_ring(mac_ring_t *ring)
{
	ASSERT(ring->mr_state == MR_INUSE);

	if (ring->mr_stop != NULL)
		ring->mr_stop(ring->mr_driver);

	ring->mr_state = MR_FREE;
	ring->mr_flag = 0;

	/*
	 * Increment the ring generation number for this ring.
	 */
	ring->mr_gen_num++;

	if (ring->mr_type == MAC_RING_TYPE_RX) {
		bzero(&ring->mr_rx_stat, sizeof (mac_rx_stats_t));
	} else if (ring->mr_type == MAC_RING_TYPE_TX) {
		bzero(&ring->mr_tx_stat, sizeof (mac_tx_stats_t));
	}
}

int
mac_start_group(mac_group_t *group)
{
	int rv = 0;

	ASSERT(!MRG_SRIOV(group));
	if (MRG_SRIOV(group))
		return (EIO);

	if (group->mrg_start != NULL)
		rv = group->mrg_start(group->mrg_driver);

	return (rv);
}

void
mac_stop_group(mac_group_t *group)
{
	ASSERT(!MRG_SRIOV(group));

	if (group->mrg_stop != NULL)
		group->mrg_stop(group->mrg_driver);
}

/*
 * boolean_t
 * mac_sriov_ready() -- Check to see if the NIC is SRIOV ready.
 *
 * Return(s): B_TRUE, if sriov ready.
 */
boolean_t
mac_sriov_ready(mac_impl_t *mip)
{
	ASSERT(mip != NULL);
	return ((mip->mi_rx_vf_groups != NULL) &&
	    (mip->mi_tx_vf_groups != NULL));
}

/*
 * mac_group_t *
 * mac_search_group() -- Searches for the HW groups that are associated
 *	with VF.
 *
 * Return(s):
 *	NULL, if no groups are found.
 */
mac_group_t *
mac_search_group(mac_impl_t *mip, uint16_t vf_index, uint32_t type)
{
	mac_group_t *grp;

	ASSERT(mip != NULL);

	switch (type) {
	case MAC_GROUP_RX:
		grp = mip->mi_rx_vf_groups;
		break;
	case MAC_GROUP_TX:
		grp = mip->mi_tx_vf_groups;
		break;
	}
	ASSERT(grp != NULL);

	/*
	 * Find the group that matches the vf index.
	 */
	while (grp != NULL) {
		if (grp->mrg_vf_index == vf_index)
			break;
		grp = grp->mrg_next;
	}

	ASSERT(grp != NULL);
	return (grp);
}

/*
 * int
 * mac_bind_group() -- Bind the groups assoicated with a VF to the
 *	mac client controlling them.
 *
 * Returns(s):
 *	0, on success
 *	EIO, if the enable function, if present, returned and error.
 */
int
mac_bind_group(mac_group_t *groups, mac_client_handle_t mch)
{
	mac_group_t	*grp;

	ASSERT(groups != NULL);
	ASSERT(mch != NULL);

	/*
	 * NOTE: A future project will handle the case where multiple groups
	 * will be assigned to a VF.  For now, it is assumed that one group
	 * per VF.
	 *
	 * Walk through the groups associated with this VF and enable
	 * them.
	 */
	grp = groups;
	while (grp != NULL) {
		/*
		 * mac of the mac client that is using the VF
		 * enabled group.
		 */
		grp->mrg_mch = mch;
		grp = grp->mrg_next;
	}

	return (0);
}

/*
 * Called from mac_start() on the default Rx group. Broadcast and multicast
 * packets are received only on the default group. Hence the default group
 * needs to be up even if the primary client is not up, for the other groups
 * to be functional. We do this by calling this function at mac_start time
 * itself. However the broadcast packets that are received can't make their
 * way beyond mac_rx until a mac client creates a broadcast flow.
 */
int
mac_start_group_and_rings(mac_group_t *group)
{
	mac_ring_t	*ring;
	int		rv = 0;

	ASSERT(group->mrg_state == MAC_GROUP_STATE_REGISTERED);
	ASSERT(!MRG_SRIOV(group));
	if (MRG_SRIOV(group))
		return (ENXIO);

	if ((rv = mac_start_group(group)) != 0)
		return (rv);

	for (ring = group->mrg_rings; ring != NULL; ring = ring->mr_next) {
		if (ring->mr_state == MR_FREE &&
		    (rv = mac_start_ring(ring)) != 0)
			goto error;
	}
	return (0);

error:
	mac_stop_group_and_rings(group);
	return (rv);
}

/* Called from mac_stop on the default Rx group */
void
mac_stop_group_and_rings(mac_group_t *group)
{
	mac_ring_t	*ring;

	for (ring = group->mrg_rings; ring != NULL; ring = ring->mr_next) {
		if (ring->mr_state != MR_FREE) {
			mac_stop_ring(ring);
		}
	}
	mac_stop_group(group);
}

void
mac_init_fake_tx_ring(mac_impl_t *mip)
{
	mac_ring_t	*ring = &mip->mi_fake_tx_ring;

	bzero(ring, sizeof (*ring));
	cv_init(&ring->mr_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&ring->mr_ref_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&ring->mr_lock, NULL, MUTEX_DEFAULT, NULL);

	ring->mr_index = 0;
	ring->mr_type = MAC_RING_TYPE_TX;
	ring->mr_gh = NULL;
	ring->mr_next = NULL;
	ring->mr_state = MR_FREE;
	ring->mr_flag = 0;
	ring->mr_info.mri_tx = mip->mi_tx;
	ring->mr_info.mri_stat = (mac_ring_stat_t)mip->mi_getstat;
	ring->mr_info.mri_driver = mip->mi_driver;
	ring->mr_mip = mip;

	ring->mr_tx_queue = NULL;
	ring->mr_tx_tailp = &ring->mr_tx_queue;
	ring->mr_tx_cnt = 0;
	ring->mr_worker_state = MR_TX_READY;
	ring->mr_worker = thread_create(NULL, 0,
	    mac_tx_ring_worker, ring, 0, &p0, TS_RUN, maxclsyspri);
	ring->mr_worker_obj =
	    numaio_object_create_thread(ring->mr_worker, "TX RING", 0);

	mip->mi_default_tx_ring = (mac_ring_handle_t)ring;
}

void
mac_fini_fake_tx_ring(mac_impl_t *mip)
{
	mac_ring_t	*ring = &mip->mi_fake_tx_ring;

	mutex_enter(&ring->mr_lock);
	ring->mr_info.mri_tx = NULL;
	ring->mr_info.mri_stat = NULL;
	ring->mr_info.mri_driver = NULL;
	ring->mr_mip = NULL;

	ring->mr_worker_state = 0;
	cv_signal(&ring->mr_cv);
	while (ring->mr_worker != NULL)
		cv_wait(&ring->mr_cv, &ring->mr_lock);

	freemsgchain(ring->mr_tx_queue);
	ring->mr_tx_queue = NULL;
	ring->mr_tx_tailp = &ring->mr_tx_queue;
	ring->mr_tx_cnt = 0;
	mutex_exit(&ring->mr_lock);
	cv_destroy(&ring->mr_cv);
	cv_destroy(&ring->mr_ref_cv);
	mutex_destroy(&ring->mr_lock);

	mip->mi_default_tx_ring = NULL;
}

static mac_ring_t *
mac_init_ring(mac_impl_t *mip, mac_group_t *group, int index,
    mac_capab_rings_t *cap_rings)
{
	mac_ring_t *ring, *rnext;
	mac_ring_info_t ring_info;
	ddi_intr_handle_t ddi_handle;

	ring = mac_ring_alloc(mip);

	/* Prepare basic information of ring */

	/*
	 * Ring index is numbered to be unique across a particular device.
	 * Ring index computation makes following assumptions:
	 *	- For drivers with static grouping (e.g. ixgbe, bge),
	 *	ring index exchanged with the driver (e.g. during mr_rget)
	 *	is unique only across the group the ring belongs to.
	 *	- Drivers with dynamic grouping (e.g. nxge), start
	 *	with single group (mrg_index = 0).
	 */
	ring->mr_index = group->mrg_index * group->mrg_info.mgi_count + index;
	ring->mr_type = group->mrg_type;
	ring->mr_gh = (mac_group_handle_t)group;

	/* Insert the new ring to the list. */
	ring->mr_next = group->mrg_rings;
	group->mrg_rings = ring;

	/* Update the ring count of the group */
	group->mrg_cur_count++;

	/* Zero to reuse the info data structure */
	bzero(&ring_info, sizeof (ring_info));

	/* Query ring information from driver */
	cap_rings->mr_rget(mip->mi_driver, group->mrg_type, group->mrg_index,
	    index, &ring_info, (mac_ring_handle_t)ring);

	/*
	 * The driver must implement statistics entry point for every ring it
	 * supports.
	 */
	if (ring_info.mri_stat == NULL) {
		cmn_err(CE_WARN, "%s failed to register. If a driver advertises"
		    " ring capability, it must implement ring stat entry point "
		    "for every ring it supports\n", mip->mi_linkname);
		/*
		 * This ring is linked into mrg_ring list maintained by the
		 * group. When we return NULL here, ring initialization fails
		 * and memory allocated for this ring is freed when subsequent
		 * cleanup walks every group and frees every ring in the group.
		 */
		return (NULL);
	}

	ring->mr_info = ring_info;

	/*
	 * The interrupt handle could be shared among multiple rings.
	 * Thus if there is a bunch of rings that are sharing an
	 * interrupt, then only one ring among the bunch will be made
	 * available for interrupt re-targeting; the rest will have
	 * ddi_shared flag set to TRUE and would not be available for
	 * be interrupt re-targeting.
	 */
	ddi_handle = ring_info.mri_intr.mi_ddi_handle;
	if (ddi_handle != NULL) {
		rnext = ring->mr_next;
		while (rnext != NULL) {
			if (rnext->mr_info.mri_intr.mi_ddi_handle ==
			    ddi_handle) {
				ring->mr_info.mri_intr.mi_ddi_shared = B_TRUE;
				break;
			}
			rnext = rnext->mr_next;
		}
		/*
		 * If rnext is NULL, then no matching ddi_handle was found.
		 * Rx rings get registered first. So if this is a Tx ring,
		 * then go through all the Rx rings and see if there is a
		 * matching ddi handle.
		 */
		if ((rnext == NULL) && (ring->mr_type == MAC_RING_TYPE_TX)) {
			mac_compare_ddi_handle(mip->mi_rx_groups, ring);
		}
	}

	/* Update ring's status */
	ring->mr_state = MR_FREE;
	ring->mr_flag = 0;
	ring->mr_mip = mip;

	/* Create per ring kstats (driver kstats) */
	mac_ring_stat_create(ring);

	/* create per lane kstats (mac kstats) */
	mac_hwlane_stat_create(mip, ring);

	mutex_enter(&ring->mr_lock);
	if (ring->mr_type == MAC_RING_TYPE_TX) {
		ring->mr_tx_queue = NULL;
		ring->mr_tx_tailp = &ring->mr_tx_queue;
		ring->mr_tx_cnt = 0;
		ring->mr_worker_state = MR_TX_READY;
		ring->mr_worker = thread_create(NULL, 0,
		    mac_tx_ring_worker, ring, 0, &p0, TS_RUN, maxclsyspri);
	}
	if (ring->mr_type == MAC_RING_TYPE_RX) {
		ring->mr_poll_pending = B_FALSE;
		ring->mr_worker_state = MR_RX_READY;
		ring->mr_worker = thread_create(NULL, 0,
		    mac_poll_worker, ring, 0, &p0, TS_RUN, maxclsyspri);
	}
	ring->mr_worker_obj = numaio_object_create_thread(ring->mr_worker,
	    ring->mr_type == MAC_RING_TYPE_TX ? "TX WRKR" : "RX POLL", 0);
	if (ddi_handle != NULL && !ring->mr_info.mri_intr.mi_ddi_shared) {
		ring->mr_intr_obj = numaio_object_create_interrupt(ddi_handle,
		    ring->mr_type == MAC_RING_TYPE_TX ?
		    "TX intr" : "RX intr", 0);
	}
	mutex_exit(&ring->mr_lock);
	return (ring);
}

/*
 * Rings are chained together for easy regrouping.
 */
static int
mac_init_group(mac_impl_t *mip, mac_group_t *group, int size,
    mac_capab_rings_t *cap_rings)
{
	int index;

	/*
	 * This group should not be SRIOV capable.
	 */
	ASSERT(!MRG_SRIOV(group));
	if (MRG_SRIOV(group))
		return (0);

	/*
	 * Initialize all ring members of this group. Size of zero will not
	 * enter the loop, so it's safe for initializing an empty group.
	 */
	for (index = size - 1; index >= 0; index--) {
		if (mac_init_ring(mip, group, index, cap_rings) == NULL) {
			/*
			 * The rings that were initialized till this point will
			 * be freed later by the caller.
			 */
			return (EINVAL);
		}
	}

	mac_group_update_rings_cache(group);
	return (0);
}

#define	MAC_GROUP_BUILD_LIST(head, tail, g) { \
	if ((head) == NULL) \
		(head) = g; \
	else \
		(tail)->mrg_next = g; \
	(tail) = g; \
}

int
mac_init_rings(mac_impl_t *mip, mac_capab_rings_t *cap_rings)
{
	mac_ring_type_t		rtype = cap_rings->mr_type;
	mac_group_t		*group, *gtail = NULL;
	mac_group_t		*groups, *defaultgrp = NULL;
	mac_group_t		**gheadp;
	mac_group_info_t	group_info;
	mac_sriov_info_t	sriov_info;
	uint_t			group_free = 0;
	uint_t			ring_left;
	mac_ring_t		*ring;
	boolean_t		sriov_groups = B_FALSE;
	int			g;
	int			err = 0;
	uint_t			grpcnt, ngroups = 0;
	boolean_t		pseudo_txgrp = B_FALSE;

	switch (rtype) {
	case MAC_RING_TYPE_RX:
		gheadp = &mip->mi_rx_groups;
		break;

	case MAC_RING_TYPE_TX:
		gheadp = &mip->mi_tx_groups;
		break;

	default:
		ASSERT(B_FALSE);
	}

	*gheadp = NULL;

	/*
	 * At least one group must be present for RX and TX.  The driver
	 * may choose not to implement a group get function used later
	 * on in mac_init_rings().
	 */
	grpcnt = cap_rings->mr_gnum;
	ASSERT(cap_rings->mr_gnum > 0);

	/*
	 * Only version implemented at this time.
	 */
	ASSERT(cap_rings->mr_version == MAC_RINGS_VERSION_1);
	if (cap_rings->mr_version != MAC_RINGS_VERSION_1)
		return (EIO);

	if (cap_rings->mr_flags & MAC_RINGS_VLAN_TRANSPARENT)
		mip->mi_rx_vlan_transparent = B_TRUE;

	/*
	 * If we have multiple TX rings, but only one TX group, we can
	 * create pseudo TX groups (one per TX ring) in the MAC layer,
	 * except for an aggr. For an aggr currently we maintain only
	 * one group with all the rings (for all its ports), going
	 * forwards we might change this. For DCB mode, we don't
	 * create pseudo groups, in the future we could create groups
	 * based on priority and assign them to clients based on
	 * priority.
	 */
	if (rtype == MAC_RING_TYPE_TX &&
	    cap_rings->mr_gnum == 0 && cap_rings->mr_rnum >  0 &&
	    (mip->mi_state_flags & MIS_IS_AGGR) == 0 &&
	    mip->mi_dcb_flags == 0) {
		/*
		 * The -1 here is because we create a default TX group
		 * with all the rings in it.
		 */
		grpcnt = cap_rings->mr_rnum - 1;
		pseudo_txgrp = B_TRUE;
	}

	/*
	 * Allocate a contiguous buffer for all groups.
	 */
	groups = kmem_zalloc(sizeof (mac_group_t) * grpcnt, KM_SLEEP);
	if (rtype == MAC_RING_TYPE_TX) {
		mip->mi_tx_groups_alloc = groups;
		mip->mi_tx_groups_alloc_count = grpcnt;
	} else {
		mip->mi_rx_groups_alloc = groups;
		mip->mi_rx_groups_alloc_count = grpcnt;
	}

	ring_left = cap_rings->mr_rnum;

	/*
	 * Get all ring groups if any, and get their ring members
	 * if any.
	 */
	for (g = 0; g < grpcnt; g++) {
		group = groups + g;

		/*
		 * Prepare basic information of the group
		 */
		group->mrg_index = g;
		group->mrg_type = rtype;
		group->mrg_state = MAC_GROUP_STATE_UNINIT;
		group->mrg_mh = (mac_handle_t)mip;
		group->mrg_next = NULL;
		group->mrg_vf_index = (uint32_t)-1;
		group->mrg_rings = NULL;
		group->mrg_cur_count = 0;

		/* Zero to reuse the info data structure */
		bzero(&group_info, sizeof (group_info));

		if (pseudo_txgrp) {
			/*
			 * This is a pseudo group that we created, apart
			 * from setting the state there is nothing to be
			 * done.
			 */
			group->mrg_state = MAC_GROUP_STATE_REGISTERED;
			group_free++;
			continue;
		}

		/* Query group information from driver */
		if (cap_rings->mr_gget != NULL) {
			cap_rings->mr_gget(mip->mi_driver, rtype, g,
			    &group_info, (mac_group_handle_t)group);
		}

		switch (cap_rings->mr_group_type) {
		case MAC_GROUP_TYPE_DYNAMIC:
			if (cap_rings->mr_gaddring == NULL ||
			    cap_rings->mr_gremring == NULL) {
				DTRACE_PROBE3(
				    mac__init__rings_no_addremring,
				    char *, mip->mi_linkname,
				    mac_group_add_ring_t,
				    cap_rings->mr_gaddring,
				    mac_group_add_ring_t,
				    cap_rings->mr_gremring);
				err = EINVAL;
				goto bail;
			}

			switch (rtype) {
			case MAC_RING_TYPE_RX:
				/*
				 * The first RX group must have non-zero
				 * rings, and the following groups must
				 * have zero rings.
				 */
				if ((g == 0) && (group_info.mgi_count == 0)) {
					DTRACE_PROBE1(
					    mac__init__rings__rx__def__zero,
					    char *, mip->mi_linkname);
					err = EINVAL;
					goto bail;
				}
				if ((g > 0) && (group_info.mgi_count != 0)) {
					DTRACE_PROBE3(
					    mac__init__rings__rx__nonzero,
					    char *, mip->mi_linkname,
					    int, g, int, group_info.mgi_count);
					err = EINVAL;
					goto bail;
				}
				break;
			case MAC_RING_TYPE_TX:
				/*
				 * All TX ring groups must have zero rings.
				 */
				if (group_info.mgi_count != 0) {
					DTRACE_PROBE3(
					    mac__init__rings__tx__nonzero,
					    char *, mip->mi_linkname,
					    int, g, int, group_info.mgi_count);
					err = EINVAL;
					goto bail;
				}
				break;
			}
			break;
		case MAC_GROUP_TYPE_STATIC:
			/*
			 * Note that an empty group is allowed, e.g., an aggr
			 * would start with an empty group.
			 */
			break;
		default:
			/* unknown group type */
			DTRACE_PROBE2(mac__init__rings__unknown__type,
			    char *, mip->mi_linkname,
			    int, cap_rings->mr_group_type);
			err = EINVAL;
			goto bail;
		}

		/*
		 * Driver registers various group entry points
		 * for proper operation of multiple MAC addresses,
		 * HW support for bandwidth management, VLAN filtering
		 * and mangement, and per group MTU enforcement.
		 */
		if (rtype == MAC_RING_TYPE_RX) {
			if (group_info.mgi_addmac == NULL ||
			    group_info.mgi_remmac == NULL) {
				goto bail;
			}
			/*
			 * Examine the mgi_flags.
			 */
			if ((group_info.mgi_flags & MAC_GROUP_DEFAULT) != 0) {
				if (defaultgrp != NULL)
					goto bail;
				group->mrg_flags |= MRG_DEFAULT;
				defaultgrp = group;
			}
			if ((group_info.mgi_flags &
			    MAC_GROUP_VLAN_TRANSPARENT_ALL) != 0)
				group->mrg_flags |= MRG_VLAN_ALL;
			if ((group_info.mgi_flags &
			    MAC_GROUP_VLAN_TRANSPARENT_ONE) != 0)
				group->mrg_flags |= MRG_VLAN_TRANSPARENT;
			if ((group_info.mgi_flags & MAC_GROUP_POOL) != 0)
				group->mrg_flags |= MRG_POOL;

			/*
			 * RX group must have addmac and remmac
			 * entry point.
			 */
			ASSERT(group_info.mgi_addmac != NULL);
			ASSERT(group_info.mgi_remmac != NULL);

			/*
			 * Group VLAN Filter capable?
			 */
			if (group_info.mgi_addvlan != NULL) {
				if (group_info.mgi_remvlan == NULL)
					goto bail;
				group->mrg_capab |= MRG_CAPAB_VLAN_FILTER;
			}

			/*
			 * Group capable of having independent MTU?
			 */
			if (group_info.mgi_setmtu != NULL)
				group->mrg_capab |= MRG_CAPAB_MTU;

			/*
			 * Are we SR-IOV capable group.
			 */
			if (group_info.mgi_getsriov_info != NULL) {
				/*
				 * Call the SR-IOV entry point to get
				 * SR-IOV information for the group.
				 */
				if (group_info.mgi_getsriov_info(
				    group_info.mgi_driver, &sriov_info) != 0)
					goto bail;
				group->mrg_capab |= MRG_CAPAB_SRIOV;
				group->mrg_vf_index = sriov_info.msi_vf_index;
				group->mrg_next = mip->mi_rx_vf_groups;
				mip->mi_rx_vf_groups = group;
				sriov_groups = B_TRUE;
			} else {
				MAC_GROUP_BUILD_LIST(*gheadp, gtail, group);
				ngroups++;
			}
		}

		if (rtype == MAC_RING_TYPE_TX) {
			ASSERT(group_info.mgi_addmac == NULL);
			ASSERT(group_info.mgi_remmac == NULL);
			ASSERT(group_info.mgi_addvlan == NULL);
			ASSERT(group_info.mgi_remvlan == NULL);
			ASSERT(group_info.mgi_setmtu == NULL);

			/*
			 * Examine the mgi_flags.
			 */
			if ((group_info.mgi_flags & MAC_GROUP_DEFAULT) != 0) {
				if (defaultgrp != NULL)
					goto bail;
				group->mrg_flags |= MRG_DEFAULT;
				defaultgrp = group;
			}
			if ((group_info.mgi_flags &
			    MAC_GROUP_VLAN_TRANSPARENT_ALL) != 0)
				group->mrg_flags |= MRG_VLAN_ALL;
			if ((group_info.mgi_flags &
			    MAC_GROUP_VLAN_TRANSPARENT_ONE) != 0)
				group->mrg_flags |= MRG_VLAN_TRANSPARENT;
			if ((group_info.mgi_flags & MAC_GROUP_POOL) != 0)
				group->mrg_flags |= MRG_POOL;

			/*
			 * Group implement the SR-IOV entry point?
			 */
			if (group_info.mgi_getsriov_info != NULL) {
				/*
				 * Call the SR-IOV entry point to get
				 * SR-IOV information for the group.
				 */
				if (group_info.mgi_getsriov_info(
				    group_info.mgi_driver, &sriov_info) != 0)
					goto bail;
				group->mrg_capab |= MRG_CAPAB_SRIOV;
				group->mrg_vf_index = sriov_info.msi_vf_index;
				group->mrg_next = mip->mi_tx_vf_groups;
				mip->mi_tx_vf_groups = group;
				sriov_groups = B_TRUE;
			} else {
				MAC_GROUP_BUILD_LIST(*gheadp, gtail, group);
				ngroups++;
			}
		}

		/*
		 * Cache driver-supplied information
		 */
		group->mrg_info = group_info;

		/*
		 * Update the group's status and group count.
		 */
		mac_set_group_state(group, MAC_GROUP_STATE_REGISTERED);
		if (!MRG_SRIOV(group))
			group_free++;

		/*
		 * Initialize the group, if it not an SRIOV capable
		 * group.
		 */
		if (group_info.mgi_count != 0) {
			if (mac_init_group(mip, group,
			    group_info.mgi_count, cap_rings) != 0)
				return (EINVAL);

			ring_left -= group_info.mgi_count;

			/*
			 * The current group size should be equal to
			 * default value
			 */
			ASSERT(group->mrg_cur_count == group_info.mgi_count);
		}
	}

	/*
	 * In the case the driver does not provide a group get entry point,
	 * the mac layer will mark the group as the default group.
	 */
	if ((cap_rings->mr_gnum == 1) && (defaultgrp == NULL)) {
		defaultgrp = groups;
		defaultgrp->mrg_flags |= MAC_GROUP_DEFAULT;
	}

	if (ring_left != 0) {
		/*
		 * Need to populate the default group.  This can happen
		 * for one of two reasons:
		 *
		 * 1) The groups are static and there is not a mr_gget()
		 *    entry point.
		 * 2) Or, the default group is dynamic group and no rings
		 *    have been addeded to the group.
		 */
		group = defaultgrp;

		ASSERT(group->mrg_cur_count == 0);
		ASSERT(group->mrg_rings == NULL);

		if (mac_init_group(mip, group, ring_left, cap_rings) != 0)
			return (EINVAL);

		/* The current group size should be equal to ring_left */
		ASSERT(group->mrg_cur_count == ring_left);

		ring_left = 0;

		/* Update this group's status */
		mac_set_group_state(group, MAC_GROUP_STATE_REGISTERED);
	}

	/*
	 * No rings should be left unaccounted for at this point.
	 */
	ASSERT(ring_left == 0);
	ASSERT(*gheadp != NULL);
	ASSERT(defaultgrp != NULL);
	ASSERT(ngroups != 0);
bail:
	/*
	 * Cache other important information to finalize the initialization
	 */
	switch (rtype) {
	case MAC_RING_TYPE_RX:
		mip->mi_rx_group_type = cap_rings->mr_group_type;
		mip->mi_rx_group_count = ngroups;
		mip->mi_rx_donor_grp = defaultgrp;
		mip->mi_rx_defaultgrp = defaultgrp;

		if (mip->mi_rx_group_type == MAC_GROUP_TYPE_DYNAMIC) {
			/*
			 * The default ring is reserved since it is
			 * used for sending the broadcast etc. packets.
			 */
			mip->mi_rxrings_avail =
			    mip->mi_rx_groups->mrg_cur_count - 1;
			mip->mi_rxrings_rsvd = 1;
		}
		/*
		 * The default group cannot be reserved. It is used by
		 * all the clients that do not have an exclusive group.
		 */
		mip->mi_rxhwclnt_avail = mip->mi_rx_group_count - 1;
		mip->mi_rxhwclnt_used = 1;
		break;

	case MAC_RING_TYPE_TX:
		mip->mi_tx_group_type = pseudo_txgrp ? MAC_GROUP_TYPE_DYNAMIC :
		    cap_rings->mr_group_type;
		mip->mi_tx_group_count = ngroups;
		mip->mi_tx_group_free = group_free;
		mip->mi_tx_defaultgrp = defaultgrp;

		group = *gheadp;
		ring = group->mrg_rings;

		/*
		 * The ring can be NULL in the case of aggr. Aggr will
		 * have an empty Tx group which will get populated
		 * later when pseudo Tx rings are added after
		 * mac_register() is done.
		 */
		if (ring == NULL) {
			ASSERT(mip->mi_state_flags & MIS_IS_AGGR);

			/*
			 * Even though there are no rings at this time
			 * (rings will come later), set the group
			 * state to registered.
			 */
			group->mrg_state = MAC_GROUP_STATE_REGISTERED;
		} else {
			/*
			 * Ring 0 is used as the default one and it could be
			 * assigned to a client as well.
			 *
			 * If there is sriov groups present, the the first
			 * ring of the default group will be the default
			 * ring regardless of the ring index.
			 */
			ring = defaultgrp->mrg_rings;
			ASSERT(ring != NULL);

			if (!sriov_groups) {
				while ((ring->mr_index != 0) &&
				    (ring->mr_next != NULL)) {
					ring = ring->mr_next;
				}
				ASSERT(ring->mr_index == 0);
			}
			mip->mi_default_tx_ring = (mac_ring_handle_t)ring;
		}

		if (mip->mi_tx_group_type == MAC_GROUP_TYPE_DYNAMIC)
			mip->mi_txrings_avail = group->mrg_cur_count - 1;

			/*
			 * The default ring cannot be reserved.
			 */
			mip->mi_txrings_rsvd = 1;

		/*
		 * The default group cannot be reserved. It will be shared
		 * by clients that do not have an exclusive group.
		 */
		mip->mi_txhwclnt_avail = mip->mi_tx_group_count;
		mip->mi_txhwclnt_used = 1;
		break;

	default:
		ASSERT(B_FALSE);
	}

	if (err != 0)
		mac_free_rings(mip, rtype);
	return (err);
}

mac_handle_t
mac_ring_get_lower_mip(mac_ring_handle_t mrh)
{
	mac_impl_t *mip = NULL;
	mac_ring_t *ring = (mac_ring_t *)mrh;

	if (ring->mr_hwrh != NULL)
		mip = ((mac_ring_t *)ring->mr_hwrh)->mr_mip;

	return ((mac_handle_t)mip);
}

static boolean_t
mac_ring_compare_ddi_handle(mac_ring_t *ring, mac_ring_t *cring,
    ddi_intr_handle_t ddi_handle)
{
	ASSERT(ring != NULL);
	ASSERT(cring != NULL);

	if (ring == cring)
		return (B_FALSE);

	if (ring->mr_info.mri_intr.mi_ddi_handle == ddi_handle) {
		cring->mr_info.mri_intr.mi_ddi_shared = B_TRUE;
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * The ddi interrupt handle could be shared amoung rings. If so, compare
 * the new ring's ddi handle with the existing ones and set ddi_shared
 * flag.
 */
void
mac_compare_ddi_handle(mac_group_t *groups, mac_ring_t *cring)
{
	mac_group_t *group;
	mac_ring_t *ring;
	ddi_intr_handle_t ddi_handle;
	boolean_t done = B_FALSE;

	ASSERT(groups != NULL);
	ASSERT(cring != NULL);

	/*
	 * Get the handle to compare.
	 */
	ddi_handle = cring->mr_info.mri_intr.mi_ddi_handle;

	/*
	 * Compare the handle to the handle of each ring of each group.
	 */
	for (group = groups; group != NULL; group = group->mrg_next) {
		/*
		 * Get the set of rings for this group.
		 */
		ring = group->mrg_rings;

		/*
		 * Walk the rings of this group and compare the handles.
		 */
		while (ring != NULL) {
			done = mac_ring_compare_ddi_handle(ring, cring,
			    ddi_handle);
			if (done)
				return;
			ring = ring->mr_next;
		}
	}
}

/* Free the cached list of rings */
static void
mac_free_ring_cache(mac_group_t *group)
{
	mac_rings_cache_t	*mrc;
	int			i;

	ASSERT(group->mrg_cache_count != 0);
	for (i = 0; i < group->mrg_cache_count; i++) {
		mrc = &group->mrg_rings_cache[i];
		if (mrc->mrc_rings == NULL) {
			ASSERT(mrc->mrc_count == 0);
			continue;
		}
		kmem_free(mrc->mrc_rings, sizeof (mac_ring_t *) *
		    mrc->mrc_count);
		mrc->mrc_count = 0;
	}
	kmem_free(group->mrg_rings_cache, sizeof (mac_rings_cache_t) *
	    group->mrg_cache_count);

	group->mrg_rings_cache = NULL;
	group->mrg_cache_count = 0;
}

/*
 * Called to free all groups of particular type (RX or TX). It's assumed that
 * no clients are using these groups.
 */
void
mac_free_rings(mac_impl_t *mip, mac_ring_type_t rtype)
{
	mac_group_t *group, *groups;

	switch (rtype) {
	case MAC_RING_TYPE_RX:
		if (mip->mi_rx_groups == NULL)
			return;
		groups = mip->mi_rx_groups;
		mip->mi_rx_groups = NULL;
		mip->mi_rx_donor_grp = NULL;
		mip->mi_rx_defaultgrp = NULL;
		mip->mi_rx_group_count = 0;
		break;
	case MAC_RING_TYPE_TX:
		ASSERT(mip->mi_tx_group_count == mip->mi_tx_group_free);
		if (mip->mi_tx_groups == NULL)
			return;
		groups = mip->mi_tx_groups;
		mip->mi_tx_groups = NULL;
		mip->mi_tx_defaultgrp = NULL;
		mip->mi_tx_group_count = 0;
		mip->mi_tx_group_free = 0;
		mip->mi_default_tx_ring = NULL;
		break;
	default:
		ASSERT(B_FALSE);
	}

	for (group = groups; group != NULL; group = group->mrg_next) {
		mac_ring_t *ring;

		if (group->mrg_cur_count == 0)
			continue;

		ASSERT(group->mrg_rings != NULL);

		while ((ring = group->mrg_rings) != NULL) {
			group->mrg_rings = ring->mr_next;
			mac_ring_free(mip, ring);
		}
		if (group->mrg_rings_cache != NULL)
			mac_free_ring_cache(group);
	}

	/* Free all the cached rings */
	mac_ring_freeall(mip);

	/* Free the block of group data strutures */
	if (rtype == MAC_RING_TYPE_RX) {
		ASSERT(mip->mi_rx_groups_alloc_count != 0);
		kmem_free(mip->mi_rx_groups_alloc,
		    sizeof (mac_group_t) * (mip->mi_rx_groups_alloc_count));
		mip->mi_rx_groups_alloc = NULL;
	} else {
		ASSERT(mip->mi_tx_groups_alloc_count != 0);
		kmem_free(mip->mi_tx_groups_alloc,
		    sizeof (mac_group_t) * (mip->mi_tx_groups_alloc_count));
		mip->mi_tx_groups_alloc = NULL;
	}
}

/*
 * Associate a MAC address with a receive group.
 *
 * The return value of this function should always be checked properly, because
 * any type of failure could cause unexpected results. A group can be added
 * or removed with a MAC address only after it has been reserved. Ideally,
 * a successful reservation always leads to calling mac_group_addmac() to
 * steer desired traffic. Failure of adding an unicast MAC address doesn't
 * always imply that the group is functioning abnormally.
 *
 * Currently this function is called everywhere, and it reflects assumptions
 * about MAC addresses in the implementation. CR 6735196.
 */
int
mac_group_addmac(mac_group_t *group, const uint8_t *addr, boolean_t primary)
{
	uint64_t flags = 0;

	ASSERT(group != NULL);
	ASSERT(group->mrg_type == MAC_RING_TYPE_RX);
	ASSERT(group->mrg_info.mgi_addmac != NULL);
	ASSERT(group->mrg_mh != NULL);

	if (primary) {
		flags = MAC_GROUP_MACADDR_PRIMARY;
	}

	if (group->mrg_info.mgi_addmac != NULL) {
		return (group->mrg_info.mgi_addmac(group->mrg_info.mgi_driver,
		    addr, flags));
	}

	return (ENOTSUP);
}

/*
 * Remove the association between MAC address and receive group.
 */
int
mac_group_remmac(mac_group_t *group, const uint8_t *addr)
{
	ASSERT(group != NULL);
	ASSERT(group->mrg_type == MAC_RING_TYPE_RX);
	ASSERT(group->mrg_info.mgi_remmac != NULL);

	if (group->mrg_info.mgi_remmac != NULL) {
		return (group->mrg_info.mgi_remmac(group->mrg_info.mgi_driver,
		    addr));
	}

	return (ENOTSUP);
}

/*
 * int
 * mac_group_add_vlan() -- Program a vlan filter for this group.
 *
 * Return(s):
 *	0, success
 *	EINVAL, for invalid arguments.
 *	ENOTSUP, not supported.
 *	ENOSPC, hardware ran of vlan filters for the group.
 */
int
mac_group_add_vlan(mac_group_t *group, uint16_t vid, uint32_t flags)
{
	ASSERT(group != NULL);
	ASSERT(group->mrg_type == MAC_RING_TYPE_RX);
	ASSERT(group->mrg_info.mgi_addvlan != NULL);
	ASSERT(vid != VLAN_ID_NONE);

	if (vid == VLAN_ID_NONE)
		return (EINVAL);

	if (group->mrg_info.mgi_addvlan != NULL) {
		return (group->mrg_info.mgi_addvlan(
		    group->mrg_info.mgi_driver, vid, flags));
	}

	return (ENOTSUP);
}

/*
 * int
 * mac_group_rem_vlan() -- remove a vlan filter from this group.
 *
 * Return(s):
 *	ENOTSUP, not supported.
 *	EINVAL, invalid arguements.
 *	0 for success.
 */
int
mac_group_rem_vlan(mac_group_t *group, uint16_t vid)
{
	ASSERT(group != NULL);
	ASSERT(group->mrg_type == MAC_RING_TYPE_RX);
	ASSERT(group->mrg_info.mgi_remvlan != NULL);
	ASSERT(vid != VLAN_ID_NONE);

	if (vid == VLAN_ID_NONE)
		return (EINVAL);

	if (group->mrg_info.mgi_remvlan != NULL) {
		return (group->mrg_info.mgi_remvlan(
		    group->mrg_info.mgi_driver, vid));
	}

	return (ENOTSUP);
}

/*
 * int
 * mac_group_set_mtu() -- set the mtu for a specific group.  This is not
 *	the link mtu.
 *
 * Return(s):
 *	0, success
 *	ENOTSUPP, not supported for this group.
 */
int
mac_group_set_mtu(mac_group_t *group, uint32_t new_mtu, uint32_t *old_mtu)
{
	int	err = 0;

	ASSERT(group != NULL);
	ASSERT(old_mtu != NULL);
	ASSERT(new_mtu != NULL);

	if (group->mrg_info.mgi_setmtu != NULL) {
		err = (group->mrg_info.mgi_setmtu)(
		    group->mrg_info.mgi_driver, new_mtu);
		if (err == 0) {
			*old_mtu = group->mrg_mtu;
			group->mrg_mtu = new_mtu;
		}
		return (err);
	}

	/*
	 * Group does not support this feature.
	 */
	return (ENOTSUP);
}

/*
 * This is the entry point for packets transmitted through the bridging code.
 * If no bridge is in place, MAC_RING_TX transmits using tx ring. The 'rh'
 * pointer may be NULL to select the default ring.
 */
boolean_t
mac_tx_bridge(mac_impl_t *mip, mac_ring_handle_t rh, mblk_t *mp,
    uint16_t flags, mblk_t **ret_mp)
{
	mac_handle_t	mh;
	boolean_t	blocked;

	/*
	 * Once we take a reference on the bridge link, the bridge
	 * module itself can't unload, so the callback pointers are
	 * stable.
	 */
	mutex_enter(&mip->mi_bridge_lock);
	if ((mh = mip->mi_bridge_link) != NULL)
		mac_bridge_ref_cb(mh, B_TRUE);
	mutex_exit(&mip->mi_bridge_lock);
	if (mh == NULL) {
		blocked = mac_tx_ring(rh, mp, flags, ret_mp);
	} else {
		blocked = mac_bridge_tx_cb(mh, rh, mp, flags, ret_mp);
		mac_bridge_ref_cb(mh, B_FALSE);
	}
	return (blocked);
}

/*
 * Find a ring from its index.
 */
mac_ring_handle_t
mac_find_ring(mac_group_handle_t gh, int index)
{
	mac_group_t *group = (mac_group_t *)gh;
	mac_ring_t *ring = group->mrg_rings;

	for (ring = group->mrg_rings; ring != NULL; ring = ring->mr_next)
		if (ring->mr_index == index)
			break;

	return ((mac_ring_handle_t)ring);
}

/*
 * We create an array of ring caches. Normally, there'd be only one
 * list in the array. However, in the DCB mode we create a list for
 * each Traffic Class for Tx rings, i.e. rings are cached based on their
 * Traffic Class. When packets are transmitted for a MAC client only the
 * rings belonging to the MAC client's priority, hence Traffic
 * Class, are used.
 */
static void
mac_group_update_rings_cache(mac_group_t *group)
{
	mac_impl_t		*mip = (mac_impl_t *)group->mrg_mh;
	mac_ring_t		*ring;
	int			i;
	int			cnt = 1;
	mac_rings_cache_t	*mrc;
	int			tc = 0;

	if (group->mrg_cur_count == 0)
		return;

	/* For Tx in the DCB mode, we need to create per-TC caches */
	if (mip->mi_dcb_flags != 0 && group->mrg_type == MAC_RING_TYPE_TX)
		cnt  = mip->mi_ntcs;

	if (group->mrg_rings_cache != NULL)
		mac_free_ring_cache(group);

	group->mrg_cache_count = cnt;
	group->mrg_rings_cache = kmem_zalloc(sizeof (mac_rings_cache_t) *
	    group->mrg_cache_count, KM_SLEEP);

	/* Get ring count for each Traffic Class */
	for (ring = group->mrg_rings, i = 0; ring != NULL;
	    ring = ring->mr_next) {
		ring->mr_gindex = i;
		if (cnt > 1) {
			tc = mip->mi_tx_rings_cap.mr_ggetringtc(ring->mr_driver,
			    MAC_RING_TYPE_TX);
		}
		ASSERT(tc < group->mrg_cache_count);
		mrc = &group->mrg_rings_cache[tc];
		mrc->mrc_count++;
		i++;
	}

	/* Allocate cache for each Traffic Class */
	for (i = 0; i < group->mrg_cache_count; i++) {
		mrc = &group->mrg_rings_cache[i];
		if (mrc->mrc_count == 0)
			continue;
		mrc->mrc_rings = kmem_zalloc(sizeof (mac_ring_t *) *
		    mrc->mrc_count, KM_SLEEP);
		mrc->mrc_count = 0;
	}

	/* Cache the ring per Traffic Class */
	for (ring = group->mrg_rings; ring != NULL; ring = ring->mr_next) {
		if (cnt > 1) {
			tc = mip->mi_tx_rings_cap.mr_ggetringtc(ring->mr_driver,
			    MAC_RING_TYPE_TX);
		}
		mrc = &group->mrg_rings_cache[tc];
		mrc->mrc_rings[mrc->mrc_count] = ring;
		mrc->mrc_count++;
	}
}

/*
 * Add a ring to an existing group.
 *
 * The ring must be either passed directly (for example if the ring
 * movement is initiated by the framework), or specified through a driver
 * index (for example when the ring is added by the driver.
 *
 * The caller needs to call mac_perim_enter() before calling this function.
 */
int
i_mac_group_add_ring(mac_group_t *group, mac_ring_t *ring, int index)
{
	mac_impl_t *mip = (mac_impl_t *)group->mrg_mh;
	mac_capab_rings_t *cap_rings;
	boolean_t driver_call = (ring == NULL);
	mac_group_type_t group_type;
	int ret = 0;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));

	switch (group->mrg_type) {
	case MAC_RING_TYPE_RX:
		cap_rings = &mip->mi_rx_rings_cap;
		group_type = mip->mi_rx_group_type;
		break;
	case MAC_RING_TYPE_TX:
		cap_rings = &mip->mi_tx_rings_cap;
		group_type = mip->mi_tx_group_type;
		break;
	default:
		ASSERT(B_FALSE);
	}

	/*
	 * There should be no ring with the same ring index in the target
	 * group.
	 */
	ASSERT(mac_find_ring((mac_group_handle_t)group,
	    driver_call ? index : ring->mr_index) == NULL);

	if (driver_call) {
		/*
		 * The function is called as a result of a request from
		 * a driver to add a ring to an existing group, for example
		 * from the aggregation driver. Allocate a new mac_ring_t
		 * for that ring.
		 */
		if ((ring = mac_init_ring(mip, group, index, cap_rings))
		    == NULL) {
			return (EINVAL);
		}

		ASSERT(group->mrg_state > MAC_GROUP_STATE_UNINIT);
		ASSERT(mac_find_ring((mac_group_handle_t)group, index) != NULL);
	} else {
		/*
		 * The function is called as a result of a MAC layer request
		 * to add a ring to an existing group. In this case the
		 * ring is being moved between groups, which requires
		 * the underlying driver to support dynamic grouping,
		 * and the mac_ring_t already exists.
		 */
		ASSERT(group_type == MAC_GROUP_TYPE_DYNAMIC);
		ASSERT(group->mrg_driver == NULL ||
		    cap_rings->mr_gaddring != NULL);
		ASSERT(ring->mr_gh == NULL);
	}

	/*
	 * At this point the ring should not be in use, and it should be
	 * of the right for the target group.
	 */
	ASSERT(ring->mr_state < MR_INUSE);
	ASSERT(ring->mr_type == group->mrg_type);

	if (!driver_call) {
		/*
		 * Add the driver level hardware ring if the process was not
		 * initiated by the driver, and the target group is not the
		 * group.
		 */
		if (group->mrg_driver != NULL) {
			cap_rings->mr_gaddring(group->mrg_driver,
			    ring->mr_driver, ring->mr_type);
		}
		/*
		 * Insert the ring ahead existing rings.
		 */
		ring->mr_next = group->mrg_rings;
		group->mrg_rings = ring;
		ring->mr_gh = (mac_group_handle_t)group;
		group->mrg_cur_count++;
	}
	mac_group_update_rings_cache(group);

	/*
	 * For aggr, the default ring will be NULL to begin with. If it
	 * is NULL, then pick the first ring that gets added as the
	 * default ring. Any ring in an aggregation can be removed at
	 * any time (by the user action of removing a link) and if the
	 * current default ring gets removed, then a new one gets
	 * picked (see i_mac_group_rem_ring()).
	 */
	if ((mip->mi_state_flags & MIS_IS_AGGR) &&
	    (mip->mi_default_tx_ring == NULL) &&
	    (ring->mr_type == MAC_RING_TYPE_TX)) {
		mip->mi_default_tx_ring = (mac_ring_handle_t)ring;
	}

	/*
	 * If the group has not been actively used, we're done.
	 */
	if (!MRG_DEFAULT(group) && group->mrg_state < MAC_GROUP_STATE_RESERVED)
		return (0);

	/*
	 * Start the ring if needed. Failure causes to undo the grouping action.
	 */
	if (ring->mr_state != MR_INUSE) {
		if ((ret = mac_start_ring(ring)) != 0) {
			if (!driver_call) {
				cap_rings->mr_gremring(group->mrg_driver,
				    ring->mr_driver, ring->mr_type);
			}
			group->mrg_cur_count--;
			group->mrg_rings = ring->mr_next;

			ring->mr_gh = NULL;

			if (driver_call)
				mac_ring_free(mip, ring);

			mac_group_update_rings_cache(group);
			return (ret);
		}
	}

	switch (ring->mr_type) {
	case MAC_RING_TYPE_RX: {
		mac_grp_client_t	*mgcp = group->mrg_clients;
		mac_client_impl_t	*mcip;

		while (mgcp != NULL) {
			mcip = mgcp->mgc_client;
			mac_rx_group_setup(mcip);
			mac_cpu_setup(mcip);
			mgcp = mgcp->mgc_next;
		}
		break;
	}
	case MAC_RING_TYPE_TX:
		break;
	default:
		ASSERT(B_FALSE);
	}
	return (0);
}

static void
i_mac_group_rem_tx_ring(mac_group_t *group, mac_ring_t *ring,
    boolean_t driver_call)
{
	mac_impl_t *mip = (mac_impl_t *)group->mrg_mh;

	if (driver_call) {
		if ((mip->mi_state_flags & MIS_IS_AGGR) != 0 &&
		    mip->mi_default_tx_ring == (mac_ring_handle_t)ring) {
			mip->mi_default_tx_ring = (group->mrg_rings != ring) ?
			    (mac_ring_handle_t)group->mrg_rings :
			    (mac_ring_handle_t)ring->mr_next;
		}
	}
}

/*
 * Remove a ring from it's current group. MAC internal function for dynamic
 * grouping.
 *
 * The caller needs to call mac_perim_enter() before calling this function.
 */
void
i_mac_group_rem_ring(mac_group_t *group, mac_ring_t *ring,
    boolean_t driver_call)
{
	mac_impl_t *mip = (mac_impl_t *)group->mrg_mh;
	mac_capab_rings_t *cap_rings = NULL;
	mac_group_type_t group_type;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));

	ASSERT(mac_find_ring((mac_group_handle_t)group,
	    ring->mr_index) == (mac_ring_handle_t)ring);
	ASSERT((mac_group_t *)ring->mr_gh == group);
	ASSERT(ring->mr_type == group->mrg_type);

	if (ring->mr_state == MR_INUSE)
		mac_stop_ring(ring);

	switch (ring->mr_type) {
	case MAC_RING_TYPE_RX:
		group_type = mip->mi_rx_group_type;
		cap_rings = &mip->mi_rx_rings_cap;
		break;
	case MAC_RING_TYPE_TX:
		group_type = mip->mi_tx_group_type;
		cap_rings = &mip->mi_tx_rings_cap;
		i_mac_group_rem_tx_ring(group, ring, driver_call);
		break;
	default:
		ASSERT(B_FALSE);
	}

	/*
	 * Remove the ring from the group.
	 */
	mac_cpu_modify(NULL, MAC_CPU_RING_REMOVE, ring);

	if (ring == group->mrg_rings) {
		group->mrg_rings = ring->mr_next;
	} else {
		mac_ring_t *pre;

		pre = group->mrg_rings;
		while (pre->mr_next != ring)
			pre = pre->mr_next;
		pre->mr_next = ring->mr_next;
	}
	group->mrg_cur_count--;
	mac_group_update_rings_cache(group);

	if (!driver_call) {
		ASSERT(group_type == MAC_GROUP_TYPE_DYNAMIC);
		ASSERT(group->mrg_driver == NULL ||
		    cap_rings->mr_gremring != NULL);

		/*
		 * Remove the driver level hardware ring.
		 */
		if (group->mrg_driver != NULL) {
			cap_rings->mr_gremring(group->mrg_driver,
			    ring->mr_driver, ring->mr_type);
		}
	}

	ring->mr_gh = NULL;
	if (driver_call)
		mac_ring_free(mip, ring);
}

/*
 * Move a ring to the target group. If needed, remove the ring from the group
 * that it currently belongs to.
 *
 * The caller need to enter MAC's perimeter by calling mac_perim_enter().
 */
static int
mac_group_mov_ring(mac_impl_t *mip, mac_group_t *d_group, mac_ring_t *ring)
{
	mac_group_t *s_group = (mac_group_t *)ring->mr_gh;
	int rv;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));
	ASSERT(d_group != NULL);
	ASSERT(s_group->mrg_mh == d_group->mrg_mh);

	if (s_group == d_group)
		return (0);

	/*
	 * Remove it from current group first.
	 */
	if (s_group != NULL)
		i_mac_group_rem_ring(s_group, ring, B_FALSE);

	/*
	 * Add it to the new group.
	 */
	rv = i_mac_group_add_ring(d_group, ring, 0);
	if (rv != 0) {
		/*
		 * Failed to add ring back to source group. If
		 * that fails, the ring is stuck in limbo, log message.
		 */
		if (i_mac_group_add_ring(s_group, ring, 0)) {
			cmn_err(CE_WARN, "%s: failed to move ring %p\n",
			    mip->mi_linkname, (void *)ring);
		}
	}

	return (rv);
}

/*
 * Find a MAC address according to its value.
 */
mac_address_t *
mac_find_macaddr(mac_impl_t *mip, uint8_t *mac_addr)
{
	mac_address_t *map;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));

	for (map = mip->mi_addresses; map != NULL; map = map->ma_next) {
		if (bcmp(mac_addr, map->ma_addr, map->ma_len) == 0)
			break;
	}

	return (map);
}

/*
 * Check whether the MAC address is shared by multiple clients.
 */
boolean_t
mac_check_macaddr_shared(mac_address_t *map)
{
	ASSERT(MAC_PERIM_HELD((mac_handle_t)map->ma_mip));

	return (map->ma_nusers > 1);
}

/*
 * Remove the specified MAC address from the MAC address list and free it.
 */
static void
mac_free_macaddr(mac_address_t *map)
{
	mac_impl_t *mip = map->ma_mip;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));
	ASSERT(mip->mi_addresses != NULL);

	map = mac_find_macaddr(mip, map->ma_addr);

	ASSERT(map != NULL);
	ASSERT(map->ma_nusers == 0);

	if (map == mip->mi_addresses) {
		mip->mi_addresses = map->ma_next;
	} else {
		mac_address_t *pre;

		pre = mip->mi_addresses;
		while (pre->ma_next != map)
			pre = pre->ma_next;
		pre->ma_next = map->ma_next;
	}

	kmem_free(map, sizeof (mac_address_t));
}

/*
 * Add a MAC address reference for a client. If the desired MAC address
 * exists, add a reference to it. Otherwise, add the new address by adding
 * it to a reserved group or setting promiscuous mode. Won't try different
 * group is the group is non-NULL, so the caller must explictly share
 * default group when needed.
 *
 * Note, the primary MAC address is initialized at registration time, so
 * to add it to default group only need to activate it if its reference
 * count is still zero. Also, some drivers may not have advertised RINGS
 * capability.
 *
 * Additional note, if the driver return ENOSUP for promiscuous mode,
 * then the addition for the mac address should fail.  An example of
 * this would be the VNICs created onto of a driver instance in the
 * guest domain.
 */
int
mac_add_macaddr(mac_impl_t *mip, mac_group_t *group, uint8_t *mac_addr,
    boolean_t use_hw, boolean_t isprimary)
{
	mac_address_t *map;
	int err = 0;
	boolean_t allocated_map = B_FALSE;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));

	map = mac_find_macaddr(mip, mac_addr);

	/*
	 * If the new MAC address has not been added. Allocate a new one
	 * and set it up.
	 */
	if (map == NULL) {
		map = kmem_zalloc(sizeof (mac_address_t), KM_SLEEP);
		map->ma_len = mip->mi_type->mt_addr_length;
		bcopy(mac_addr, map->ma_addr, map->ma_len);
		map->ma_nusers = 0;
		map->ma_group = group;
		map->ma_mip = mip;

		/* add the new MAC address to the head of the address list */
		map->ma_next = mip->mi_addresses;
		mip->mi_addresses = map;

		allocated_map = B_TRUE;
	}

	if (map->ma_group == NULL)
		map->ma_group = group;
	if (map->ma_group != group)
		return (EIO);

	/*
	 * If the MAC address is already in use, simply account for the
	 * new client.
	 */
	if (map->ma_nusers++ > 0)
		return (0);

	/*
	 * Activate this MAC address by adding it to the reserved group.
	 */
	if (group != NULL) {
		err = mac_group_addmac(group,
		    (const uint8_t *)mac_addr, isprimary);
		if (err == 0) {
			map->ma_type = MAC_ADDRESS_TYPE_UNICAST_CLASSIFIED;
			return (0);
		}
	}

	/*
	 * The MAC address addition failed. If the client requires a
	 * hardware classified MAC address, fail the operation.
	 */
	if (use_hw) {
		err = ENOSPC;
		goto bail;
	}

	/*
	 * Try promiscuous mode.
	 *
	 * For drivers that don't advertise RINGS capability, do
	 * nothing for the primary address.
	 */
	if ((group == NULL) &&
	    (bcmp(map->ma_addr, mip->mi_addr, map->ma_len) == 0)) {
		map->ma_type = MAC_ADDRESS_TYPE_UNICAST_CLASSIFIED;
		return (0);
	}

	/*
	 * If the interface does not implement full promiscuous mode,
	 * then, we need to fail the addition of the MAC address.
	 */
	if (mip->mi_state_flags & MIS_MULTICAST_ONLY) {
		err = ENXIO;
		goto bail;
	}

	/*
	 * Enable promiscuous mode in order to receive traffic
	 * to the new MAC address.
	 */
	if ((err = i_mac_promisc_set(mip, B_TRUE)) == 0) {
		map->ma_type = MAC_ADDRESS_TYPE_UNICAST_PROMISC;
		return (0);
	}

	/*
	 * Free the MAC address that could not be added. Don't free
	 * a pre-existing address, it could have been the entry
	 * for the primary MAC address which was pre-allocated by
	 * mac_init_macaddr(), and which must remain on the list.
	 */
bail:
	map->ma_nusers--;
	if (allocated_map)
		mac_free_macaddr(map);
	return (err);
}

/*
 * Remove a reference to a MAC address. This may cause to remove the MAC
 * address from an associated group or to turn off promiscuous mode.
 * The caller needs to handle the failure properly.
 */
int
mac_remove_macaddr(mac_impl_t *mip, uint8_t *mac_addr)
{
	mac_address_t *map;
	int err = 0;

	ASSERT(mip != NULL);
	ASSERT(mac_addr != NULL);
	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));
	map = mac_find_macaddr(mip, mac_addr);
	ASSERT(map != NULL);

	/*
	 * If it's not the last client using this MAC address, only update
	 * the MAC clients count.
	 */
	if (--map->ma_nusers > 0)
		return (0);

	/*
	 * The MAC address is no longer used by any MAC client, so remove
	 * it from its associated group, or turn off promiscuous mode
	 * if it was enabled for the MAC address.
	 */
	switch (map->ma_type) {
	case MAC_ADDRESS_TYPE_UNICAST_CLASSIFIED:
		/*
		 * Don't free the preset primary address for drivers that
		 * don't advertise RINGS capability.
		 */
		if (map->ma_group == NULL)
			return (0);

		err = mac_group_remmac(map->ma_group, map->ma_addr);
		if (err == 0)
			map->ma_group = NULL;
		break;
	case MAC_ADDRESS_TYPE_UNICAST_PROMISC:
		err = i_mac_promisc_set(mip, B_FALSE);
		break;
	default:
		ASSERT(B_FALSE);
	}

	if (err != 0)
		return (err);

	/*
	 * We created MAC address for the primary one at registration, so we
	 * won't free it here. mac_fini_macaddr() will take care of it.
	 */
	if (bcmp(map->ma_addr, mip->mi_addr, map->ma_len) != 0)
		mac_free_macaddr(map);

	return (0);
}

/*
 * Update an existing MAC address. The caller need to make sure that the new
 * value has not been used.
 */
int
mac_update_macaddr(mac_address_t *map, uint8_t *mac_addr)
{
	mac_impl_t *mip = map->ma_mip;
	int err = 0;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));
	ASSERT(mac_find_macaddr(mip, mac_addr) == NULL);

	switch (map->ma_type) {
	case MAC_ADDRESS_TYPE_UNICAST_CLASSIFIED:
		/*
		 * Update the primary address for drivers that are not
		 * RINGS capable.
		 */
		if (mip->mi_rx_groups == NULL) {
			err = mip->mi_unicst(mip->mi_driver, (const uint8_t *)
			    mac_addr);
			if (err != 0)
				return (err);
			break;
		}

		/*
		 * If this MAC address is not currently in use,
		 * simply break out and update the value.
		 */
		if (map->ma_nusers == 0)
			break;

		/*
		 * Need to replace the MAC address associated with a group.
		 */
		err = mac_group_remmac(map->ma_group, map->ma_addr);
		if (err != 0)
			return (err);

		err = mac_group_addmac(map->ma_group, mac_addr, B_FALSE);

		/*
		 * Failure hints hardware error. The MAC layer needs to
		 * have error notification facility to handle this.
		 * Now, simply try to restore the value.
		 */
		if (err != 0) {
			(void) mac_group_addmac(map->ma_group,
			    map->ma_addr, B_FALSE);
		}
		break;
	case MAC_ADDRESS_TYPE_UNICAST_PROMISC:
		/*
		 * Need to do nothing more if in promiscuous mode.
		 */
		break;
	default:
		/*
		 * If this MAC address is not currently in use,
		 * simply break out and update the value.
		 */
		if (map->ma_nusers == 0)
			break;

		ASSERT(B_FALSE);
	}

	/*
	 * Successfully replaced the MAC address.
	 */
	if (err == 0)
		bcopy(mac_addr, map->ma_addr, map->ma_len);

	return (err);
}

/*
 * Freshen the MAC address with new value. Its caller must have updated the
 * hardware MAC address before calling this function.
 * This funcitons is supposed to be used to handle the MAC address change
 * notification from underlying drivers.
 */
void
mac_freshen_macaddr(mac_address_t *map, uint8_t *mac_addr)
{
	mac_impl_t *mip = map->ma_mip;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));
	ASSERT(mac_find_macaddr(mip, mac_addr) == NULL);

	/*
	 * Freshen the MAC address with new value.
	 */
	bcopy(mac_addr, map->ma_addr, map->ma_len);
	bcopy(mac_addr, mip->mi_addr, map->ma_len);

	/*
	 * Update all MAC clients that share this MAC address.
	 */
	mac_unicast_update_clients(mip, map);
}

/*
 * Set up the primary MAC address.
 */
void
mac_init_macaddr(mac_impl_t *mip)
{
	mac_address_t *map;

	/*
	 * The reference count is initialized to zero, until it's really
	 * activated.
	 */
	map = kmem_zalloc(sizeof (mac_address_t), KM_SLEEP);
	map->ma_len = mip->mi_type->mt_addr_length;
	bcopy(mip->mi_addr, map->ma_addr, map->ma_len);

	/*
	 * If driver advertises RINGS capability, it shouldn't have initialized
	 * its primary MAC address. For other drivers, including VNIC, the
	 * primary address must work after registration.
	 */
	if (mip->mi_rx_groups == NULL)
		map->ma_type = MAC_ADDRESS_TYPE_UNICAST_CLASSIFIED;

	map->ma_mip = mip;

	mip->mi_addresses = map;
}

/*
 * Clean up the primary MAC address. Note, only one primary MAC address
 * is allowed. All other MAC addresses must have been freed appropriately.
 */
void
mac_fini_macaddr(mac_impl_t *mip)
{
	mac_address_t *map = mip->mi_addresses;

	if (map == NULL)
		return;

	/*
	 * If mi_addresses is initialized, there should be exactly one
	 * entry left on the list with no users.
	 */
	ASSERT(map->ma_nusers == 0);
	ASSERT(map->ma_next == NULL);

	kmem_free(map, sizeof (mac_address_t));
	mip->mi_addresses = NULL;
}

static mac_vlan_t *
mac_find_macvlan(mac_group_t *grp, uint16_t vid)
{
	mac_vlan_t *mvp;

	mvp = grp->mrg_vlans;

	while (mvp != NULL) {
		if (mvp->mv_vid == vid)
			return (mvp);
		else
			mvp = mvp->mv_next;
	}

	return (NULL);
}

int
mac_add_macvlan(mac_impl_t *mip, mac_group_t *grp, uint16_t vid, uint16_t flags)
{
	mac_vlan_t *mvp = NULL;
	uint32_t gflags = 0;
	int err = 0;

	ASSERT(mip != NULL);
	ASSERT(grp != NULL);
	ASSERT(vid != VLAN_ID_NONE);

	mvp = mac_find_macvlan(grp, vid);
	if (mvp == NULL) {
		/*
		 * If the HW implements vlan tagging/stripping for all
		 * VLANs and we desire a transparent vlan and we have
		 * vlans programmed, then we fail to program the
		 * transparaent vlan for the group. Only one vlan for
		 * this type of device can be programmed to offer
		 * the transparent vlan.
		 */
		if (((flags & MAC_UNICAST_VLAN_TRANSPARENT) != 0) &&
		    ((grp->mrg_flags & MAC_GROUP_VLAN_TRANSPARENT_ALL) != 0) &&
		    !mip->mi_rx_vlan_transparent && (grp->mrg_nvlans > 0)) {
			return (EIO);
		}

		if (flags & MAC_UNICAST_VLAN_TRANSPARENT)
			gflags = MAC_GROUP_VLAN_TRANSPARENT_ENABLE;

		/*
		 * Program the vlan into the H/W.
		 */
		if ((err = mac_group_add_vlan(grp, vid, gflags)) != 0)
			return (err);

		mvp = kmem_zalloc(sizeof (mac_vlan_t), KM_SLEEP);
		mvp->mv_nusers = 0;
		mvp->mv_group = grp;
		mvp->mv_mip = mip;
		mvp->mv_vid = vid;

		if ((flags & MAC_UNICAST_VLAN_TRANSPARENT) != 0)
			mvp->mv_transparent = B_TRUE;

		/*
		 * Add the new MAC address to the head of the address list
		 */
		mvp->mv_next = grp->mrg_vlans;
		grp->mrg_vlans = mvp;
		grp->mrg_nvlans++;
	}

	/*
	 * Account for the new client using the vlan.
	 */
	mvp->mv_nusers++;
	return (0);
}

static void
mac_free_macvlan(mac_vlan_t *mvp)
{
	mac_group_t *grp;

	ASSERT(mvp != NULL);
	ASSERT(mvp->mv_nusers == 0);
	ASSERT(mvp->mv_group != NULL);
	ASSERT(MAC_PERIM_HELD((mac_handle_t)mvp->mv_mip));

	grp = mvp->mv_group;
	if (mvp == grp->mrg_vlans) {
		grp->mrg_vlans = mvp->mv_next;
	} else {
		mac_vlan_t *pre;

		pre = grp->mrg_vlans;
		while (pre->mv_next != mvp)
			pre = pre->mv_next;
		pre->mv_next = mvp->mv_next;
	}

	kmem_free(mvp, sizeof (mac_vlan_t));
	grp->mrg_nvlans--;
}

int
mac_remove_macvlan(mac_impl_t *mip, mac_group_t *grp, uint16_t vid)
{
	mac_vlan_t *mvp;

	ASSERT(mip != NULL);
	ASSERT(grp != NULL);
	ASSERT(vid != VLAN_ID_NONE);
	mvp = mac_find_macvlan(grp, vid);
	ASSERT(mvp != NULL);

	mvp->mv_nusers--;
	if (mvp->mv_nusers == 0) {
		mac_free_macvlan(mvp);
		return (mac_group_rem_vlan(grp, vid));
	}

	return (0);
}

/*
 * Logging related functions.
 *
 * Note that Kernel statistics have been extended to maintain fine
 * granularity of statistics viz. hardware lane, software lane, fanout
 * stats etc. However, extended accounting continues to support only
 * aggregate statistics like before.
 */

/* Write the flow description to a netinfo_t record */
static netinfo_t *
mac_write_flow_desc(flow_entry_t *flent, mac_client_impl_t *mcip)
{
	netinfo_t		*ninfo;
	net_desc_t		*ndesc;
	flow_desc_t		*fdesc;
	mac_resource_props_t	*mrp;

	ninfo = kmem_zalloc(sizeof (netinfo_t), KM_NOSLEEP);
	if (ninfo == NULL)
		return (NULL);
	ndesc = kmem_zalloc(sizeof (net_desc_t), KM_NOSLEEP);
	if (ndesc == NULL) {
		kmem_free(ninfo, sizeof (netinfo_t));
		return (NULL);
	}

	/*
	 * Grab the fe_lock to see a self-consistent fe_flow_desc.
	 * Updates to the fe_flow_desc are done under the fe_lock
	 */
	mutex_enter(&flent->fe_lock);
	fdesc = &flent->fe_flow_desc;
	mrp = &flent->fe_resource_props;

	(void) strlcpy(ndesc->nd_name, flent->fe_flow_name,
	    sizeof (ndesc->nd_name));
	(void) strlcpy(ndesc->nd_devname, mcip->mci_name,
	    sizeof (ndesc->nd_devname));

	bcopy(fdesc->fd_src_mac, ndesc->nd_ehost, ETHERADDRL);
	bcopy(fdesc->fd_dst_mac, ndesc->nd_edest, ETHERADDRL);
	ndesc->nd_sap = htonl(fdesc->fd_sap);
	ndesc->nd_isv4 = (uint8_t)fdesc->fd_ipversion == IPV4_VERSION;
	ndesc->nd_bw_limit = mrp->mrp_maxbw;
	if (ndesc->nd_isv4) {
		ndesc->nd_saddr[3] = htonl(fdesc->fd_local_addr.s6_addr32[3]);
		ndesc->nd_daddr[3] = htonl(fdesc->fd_remote_addr.s6_addr32[3]);
	} else {
		bcopy(&fdesc->fd_local_addr, ndesc->nd_saddr, IPV6_ADDR_LEN);
		bcopy(&fdesc->fd_remote_addr, ndesc->nd_daddr, IPV6_ADDR_LEN);
	}
	ndesc->nd_sport = htons(fdesc->fd_local_port);
	ndesc->nd_dport = htons(fdesc->fd_remote_port);
	ndesc->nd_protocol = (uint8_t)fdesc->fd_protocol;
	mutex_exit(&flent->fe_lock);

	ninfo->ni_record = ndesc;
	ninfo->ni_size = sizeof (net_desc_t);
	ninfo->ni_type = EX_NET_FLDESC_REC;

	return (ninfo);
}

/* Write the flow statistics to a netinfo_t record */
/* ARGSUSED */
static netinfo_t *
mac_write_flow_stats(flow_entry_t *flent)
{
	netinfo_t	*ninfo;
	net_stat_t	*nstat;
	flow_stats_t	*flow_stats = &flent->fe_stat;

	ninfo = kmem_zalloc(sizeof (netinfo_t), KM_NOSLEEP);
	if (ninfo == NULL)
		return (NULL);
	nstat = kmem_zalloc(sizeof (net_stat_t), KM_NOSLEEP);
	if (nstat == NULL) {
		kmem_free(ninfo, sizeof (netinfo_t));
		return (NULL);
	}

	(void) strlcpy(nstat->ns_name, flent->fe_flow_name,
	    sizeof (nstat->ns_name));

	nstat->ns_ibytes = flow_stats->fs_ibytes;
	nstat->ns_ipackets = flow_stats->fs_ipackets;
	nstat->ns_ierrors = flow_stats->fs_ierrors;
	nstat->ns_obytes = flow_stats->fs_obytes;
	nstat->ns_opackets = flow_stats->fs_opackets;
	nstat->ns_oerrors = flow_stats->fs_oerrors;

	ninfo->ni_record = nstat;
	ninfo->ni_size = sizeof (net_stat_t);
	ninfo->ni_type = EX_NET_FLSTAT_REC;

	return (ninfo);
}

/* Write the link description to a netinfo_t record */
static netinfo_t *
mac_write_link_desc(mac_client_impl_t *mcip)
{
	netinfo_t		*ninfo;
	net_desc_t		*ndesc;
	flow_entry_t		*flent = mcip->mci_flent;

	ninfo = kmem_zalloc(sizeof (netinfo_t), KM_NOSLEEP);
	if (ninfo == NULL)
		return (NULL);
	ndesc = kmem_zalloc(sizeof (net_desc_t), KM_NOSLEEP);
	if (ndesc == NULL) {
		kmem_free(ninfo, sizeof (netinfo_t));
		return (NULL);
	}

	(void) strlcpy(ndesc->nd_name, mcip->mci_name,
	    sizeof (ndesc->nd_name));
	(void) strlcpy(ndesc->nd_devname, mcip->mci_name,
	    sizeof (ndesc->nd_devname));
	ndesc->nd_isv4 = B_TRUE;
	/*
	 * Grab the fe_lock to see a self-consistent fe_flow_desc.
	 * Updates to the fe_flow_desc are done under the fe_lock
	 * after removing the flent from the flow table.
	 */
	mutex_enter(&flent->fe_lock);
	bcopy(flent->fe_flow_desc.fd_src_mac, ndesc->nd_ehost, ETHERADDRL);
	mutex_exit(&flent->fe_lock);

	ninfo->ni_record = ndesc;
	ninfo->ni_size = sizeof (net_desc_t);
	ninfo->ni_type = EX_NET_LNDESC_REC;

	return (ninfo);
}

/* Write the link statistics to a netinfo_t record */
/* ARGSUSED */
static netinfo_t *
mac_write_link_stats(mac_client_impl_t *mcip)
{
	netinfo_t		*ninfo;
	net_stat_t		*nstat;

	ninfo = kmem_zalloc(sizeof (netinfo_t), KM_NOSLEEP);
	if (ninfo == NULL)
		return (NULL);
	nstat = kmem_zalloc(sizeof (net_stat_t), KM_NOSLEEP);
	if (nstat == NULL) {
		kmem_free(ninfo, sizeof (netinfo_t));
		return (NULL);
	}

	(void) strlcpy(nstat->ns_name, mcip->mci_name,
	    sizeof (nstat->ns_name));

	nstat->ns_ibytes = mcip->mci_stat.ms_rx.mrs_intrbytes +
	    mcip->mci_stat.ms_rx.mrs_pollbytes;
	nstat->ns_ipackets = mcip->mci_stat.ms_rx.mrs_intrcnt +
	    mcip->mci_stat.ms_rx.mrs_pollcnt;
	nstat->ns_ierrors = mcip->mci_stat.ms_rx.mrs_ierrors;

	mac_tx_update_obytes_pkts(mcip);
	nstat->ns_obytes = mcip->mci_stat.ms_tx.mts_obytes;
	nstat->ns_opackets = mcip->mci_stat.ms_tx.mts_opackets;
	nstat->ns_oerrors = mcip->mci_stat.ms_tx.mts_oerrors;

	ninfo->ni_record = nstat;
	ninfo->ni_size = sizeof (net_stat_t);
	ninfo->ni_type = EX_NET_LNSTAT_REC;

	return (ninfo);
}

typedef struct i_mac_log_state_s {
	boolean_t	mi_last;
	int		mi_fenable;
	int		mi_lenable;
	list_t		*mi_list;
} i_mac_log_state_t;

/*
 * For a given flow, if the description has not been logged before, do it now.
 * If it is a VNIC, then we have collected information about it from the MAC
 * table, so skip it.
 *
 * Called through mac_flow_walk_nolock()
 *
 * Return 0 if successful.
 */
static int
mac_log_flowinfo(flow_entry_t *flent, void *arg)
{
	mac_client_impl_t	*mcip = flent->fe_mcip;
	i_mac_log_state_t	*lstate = arg;
	netinfo_t		*ninfo;

	if (mcip == NULL)
		return (0);

	/*
	 * If the name starts with "vnic", and fe_user_generated is true (to
	 * exclude the mcast and active flow entries created implicitly for
	 * a vnic, it is a VNIC flow.  i.e. vnic1 is a vnic flow,
	 * vnic/bge1/mcast1 is not and neither is vnic/bge1/active.
	 */
	if (strncasecmp(flent->fe_flow_name, "vnic", 4) == 0 &&
	    (flent->fe_type & FLOW_USER) != 0) {
		return (0);
	}

	if (!flent->fe_desc_logged) {
		/*
		 * We don't return error because we want to continue the
		 * walk in case this is the last walk which means we
		 * need to reset fe_desc_logged in all the flows.
		 */
		if ((ninfo = mac_write_flow_desc(flent, mcip)) == NULL)
			return (0);
		list_insert_tail(lstate->mi_list, ninfo);
		flent->fe_desc_logged = B_TRUE;
	}

	/*
	 * Regardless of the error, we want to proceed in case we have to
	 * reset fe_desc_logged.
	 */
	ninfo = mac_write_flow_stats(flent);
	if (ninfo == NULL)
		return (-1);

	list_insert_tail(lstate->mi_list, ninfo);

	if (mcip != NULL && !(mcip->mci_state_flags & MCIS_DESC_LOGGED))
		flent->fe_desc_logged = B_FALSE;

	return (0);
}

/*
 * Log the description for each mac client of this mac_impl_t, if it
 * hasn't already been done. Additionally, log statistics for the link as
 * well. Walk the flow table and log information for each flow as well.
 * If it is the last walk (mci_last), then we turn off mci_desc_logged (and
 * also fe_desc_logged, if flow logging is on) since we want to log the
 * description if and when logging is restarted.
 *
 * Return 0 upon success or -1 upon failure
 */
static int
i_mac_impl_log(mac_impl_t *mip, i_mac_log_state_t *lstate)
{
	mac_client_impl_t	*mcip;
	netinfo_t		*ninfo;

	i_mac_perim_enter(mip);
	/*
	 * Only walk the client list for NIC and etherstub
	 */
	if ((mip->mi_state_flags & MIS_DISABLED) ||
	    ((mip->mi_state_flags & MIS_IS_VNIC) &&
	    (mac_get_lower_mac_handle((mac_handle_t)mip) != NULL))) {
		i_mac_perim_exit(mip);
		return (0);
	}

	for (mcip = mip->mi_clients_list; mcip != NULL;
	    mcip = mcip->mci_client_next) {
		if (!MCIP_DATAPATH_SETUP(mcip))
			continue;
		if (lstate->mi_lenable) {
			if (!(mcip->mci_state_flags & MCIS_DESC_LOGGED)) {
				ninfo = mac_write_link_desc(mcip);
				if (ninfo == NULL) {
				/*
				 * We can't terminate it if this is the last
				 * walk, else there might be some links with
				 * mi_desc_logged set to true, which means
				 * their description won't be logged the next
				 * time logging is started (similarly for the
				 * flows within such links). We can continue
				 * without walking the flow table (i.e. to
				 * set fe_desc_logged to false) because we
				 * won't have written any flow stuff for this
				 * link as we haven't logged the link itself.
				 */
					i_mac_perim_exit(mip);
					if (lstate->mi_last)
						return (0);
					else
						return (-1);
				}
				mcip->mci_state_flags |= MCIS_DESC_LOGGED;
				list_insert_tail(lstate->mi_list, ninfo);
			}
		}

		ninfo = mac_write_link_stats(mcip);
		if (ninfo == NULL && !lstate->mi_last) {
			i_mac_perim_exit(mip);
			return (-1);
		}
		list_insert_tail(lstate->mi_list, ninfo);

		if (lstate->mi_last)
			mcip->mci_state_flags &= ~MCIS_DESC_LOGGED;

		if (lstate->mi_fenable) {
			if (mcip->mci_subflow_tab != NULL) {
				(void) mac_flow_walk_nolock(
				    mcip->mci_subflow_tab, mac_log_flowinfo,
				    lstate);
			}
		}
	}
	i_mac_perim_exit(mip);
	return (0);
}

struct mac_impl_list {
	uint_t		mil_cnt;
	uint_t		mil_idx;
	mac_impl_t	*mil_mac[1];
};

/*
 * modhash walker function to add a mac_impl_t to a list
 */
/*ARGSUSED*/
static uint_t
i_mac_impl_list_walker(mod_hash_key_t key, mod_hash_val_t *val, void *arg)
{
	struct mac_impl_list	*mlp = arg;
	mac_impl_t		*mip = (mac_impl_t *)val;

	ASSERT(mlp->mil_cnt > mlp->mil_idx);

	if ((mip->mi_state_flags & MIS_DISABLED) == 0) {
		mip->mi_ref++;
		mlp->mil_mac[mlp->mil_idx++] = mip;
	}

	return (MH_WALK_CONTINUE);
}

/*
 * Create a list of mac_impl_t's by walking i_mac_impl_hash.
 *  mil_cnt - number of elements in the hash table
 *  mil_idx - number of elements in the list
 */
static struct mac_impl_list *
i_mac_impl_list_create()
{
	struct mac_impl_list	*mlp;
	uint_t			nelems;
	uint_t			lsize;

	ASSERT(RW_ISWRITER(&i_mac_impl_lock));
	nelems = mod_hash_nelems(i_mac_impl_hash);
	lsize = sizeof (struct mac_impl_list) +
	    (nelems - 1) * sizeof (mac_impl_t *);
	mlp = kmem_zalloc(lsize, KM_SLEEP);
	mlp->mil_cnt = nelems;
	mod_hash_walk(i_mac_impl_hash, i_mac_impl_list_walker, mlp);
	ASSERT(mlp->mil_idx <= nelems);

	return (mlp);
}

/*
 * Destroy a list of mac_impl_t's that was created by i_mac_impl_list_create().
 */
static void
i_mac_impl_list_destroy(struct mac_impl_list *mlp)
{
	uint_t	lsize;
	int 	i;

	/* Decrement reference count and free list of mac_impls */
	rw_enter(&i_mac_impl_lock, RW_WRITER);
	for (i = 0; i < mlp->mil_idx; i++) {
		mlp->mil_mac[i]->mi_ref--;
	}
	rw_exit(&i_mac_impl_lock);
	lsize = sizeof (struct mac_impl_list) +
	    (mlp->mil_cnt - 1) * sizeof (mac_impl_t *);
	kmem_free(mlp, lsize);
}

void
i_mac_log_info(list_t *net_log_list, i_mac_log_state_t *lstate)
{
	struct mac_impl_list	*mlp;
	netinfo_t		*ninfo;
	int 			i;

	/*
	 * i_mac_impl_lock is acquired by the caller but released
	 * by this function.
	 */
	ASSERT(RW_ISWRITER(&i_mac_impl_lock));

	mlp = i_mac_impl_list_create();
	rw_exit(&i_mac_impl_lock);

	/* Create log entries for each mac_impl */
	for (i = 0; i < mlp->mil_idx; i++) {
		if (i_mac_impl_log(mlp->mil_mac[i], lstate) != 0)
			continue;
	}

	i_mac_impl_list_destroy(mlp);

	/*
	 * Write log entries to files outside of locks, free associated
	 * structures, and remove entries from the list.
	 */
	while ((ninfo = list_head(net_log_list)) != NULL) {
		(void) exacct_commit_netinfo(ninfo->ni_record, ninfo->ni_type);
		list_remove(net_log_list, ninfo);
		kmem_free(ninfo->ni_record, ninfo->ni_size);
		kmem_free(ninfo, sizeof (*ninfo));
	}
	list_destroy(net_log_list);
}

/*
 * The timer thread that runs every mac_logging_interval seconds and logs
 * link and/or flow information.
 */
/* ARGSUSED */
void
mac_log_linkinfo(void *arg)
{
	i_mac_log_state_t	lstate;
	list_t			net_log_list;

	list_create(&net_log_list, sizeof (netinfo_t),
	    offsetof(netinfo_t, ni_link));

	/* i_mac_impl_lock is released by i_mac_log_info() */
	rw_enter(&i_mac_impl_lock, RW_WRITER);
	if (!mac_flow_log_enable && !mac_link_log_enable) {
		rw_exit(&i_mac_impl_lock);
		return;
	}
	lstate.mi_fenable = mac_flow_log_enable;
	lstate.mi_lenable = mac_link_log_enable;
	lstate.mi_last = B_FALSE;
	lstate.mi_list = &net_log_list;

	/* Write log entries for each mac_impl in the list */
	i_mac_log_info(&net_log_list, &lstate);

	if (mac_flow_log_enable || mac_link_log_enable) {
		mac_logging_timer = timeout(mac_log_linkinfo, NULL,
		    SEC_TO_TICK(mac_logging_interval));
	}
}

typedef struct i_mac_fastpath_state_s {
	boolean_t	mf_disable;
	int		mf_err;
} i_mac_fastpath_state_t;

/* modhash walker function to enable or disable fastpath */
/*ARGSUSED*/
static uint_t
i_mac_fastpath_walker(mod_hash_key_t key, mod_hash_val_t *val,
    void *arg)
{
	i_mac_fastpath_state_t	*state = arg;
	mac_handle_t		mh = (mac_handle_t)val;

	if (state->mf_disable)
		state->mf_err = mac_fastpath_disable(mh);
	else
		mac_fastpath_enable(mh);

	return (state->mf_err == 0 ? MH_WALK_CONTINUE : MH_WALK_TERMINATE);
}

/*
 * Start the logging timer.
 */
int
mac_start_logusage(mac_logtype_t type, uint_t interval)
{
	i_mac_fastpath_state_t	dstate = {B_TRUE, 0};
	i_mac_fastpath_state_t	estate = {B_FALSE, 0};
	int			err;

	rw_enter(&i_mac_impl_lock, RW_WRITER);
	switch (type) {
	case MAC_LOGTYPE_FLOW:
		if (mac_flow_log_enable) {
			rw_exit(&i_mac_impl_lock);
			return (0);
		}
		/* FALLTHRU */
	case MAC_LOGTYPE_LINK:
		if (mac_link_log_enable) {
			rw_exit(&i_mac_impl_lock);
			return (0);
		}
		break;
	default:
		ASSERT(0);
	}

	/* Disable fastpath */
	mod_hash_walk(i_mac_impl_hash, i_mac_fastpath_walker, &dstate);
	if ((err = dstate.mf_err) != 0) {
		/* Reenable fastpath  */
		mod_hash_walk(i_mac_impl_hash, i_mac_fastpath_walker, &estate);
		rw_exit(&i_mac_impl_lock);
		return (err);
	}

	switch (type) {
	case MAC_LOGTYPE_FLOW:
		mac_flow_log_enable = B_TRUE;
		/* FALLTHRU */
	case MAC_LOGTYPE_LINK:
		mac_link_log_enable = B_TRUE;
		break;
	}

	mac_logging_interval = interval;
	rw_exit(&i_mac_impl_lock);
	mac_log_linkinfo(NULL);
	return (0);
}

/*
 * Stop the logging timer if both link and flow logging are turned off.
 */
void
mac_stop_logusage(mac_logtype_t type)
{
	i_mac_log_state_t	lstate;
	i_mac_fastpath_state_t	estate = {B_FALSE, 0};
	list_t			net_log_list;

	list_create(&net_log_list, sizeof (netinfo_t),
	    offsetof(netinfo_t, ni_link));

	rw_enter(&i_mac_impl_lock, RW_WRITER);

	lstate.mi_fenable = mac_flow_log_enable;
	lstate.mi_lenable = mac_link_log_enable;
	lstate.mi_list = &net_log_list;

	/* Last walk */
	lstate.mi_last = B_TRUE;

	switch (type) {
	case MAC_LOGTYPE_FLOW:
		if (lstate.mi_fenable) {
			ASSERT(mac_link_log_enable);
			mac_flow_log_enable = B_FALSE;
			mac_link_log_enable = B_FALSE;
			break;
		}
		/* FALLTHRU */
	case MAC_LOGTYPE_LINK:
		if (!lstate.mi_lenable || mac_flow_log_enable) {
			rw_exit(&i_mac_impl_lock);
			return;
		}
		mac_link_log_enable = B_FALSE;
		break;
	default:
		ASSERT(0);
	}

	/* Reenable fastpath */
	mod_hash_walk(i_mac_impl_hash, i_mac_fastpath_walker, &estate);

	(void) untimeout(mac_logging_timer);
	mac_logging_timer = 0;

	/* Write log entries for each mac_impl in the list */
	i_mac_log_info(&net_log_list, &lstate);
}

void
mac_flow_update_priority(mac_client_impl_t *mcip)
{
	mac_resource_props_t	*mrp = MCIP_RESOURCE_PROPS(mcip);
	mac_rx_fanout_t		*rf;
	pri_t			pri;
	uint_t			i;

	if ((mrp->mrp_mask & MRP_PRIORITY) == 0)
		return;

	mac_rx_client_quiesce((mac_client_handle_t)mcip);
	pri = mcip->mci_max_pri;

	for (i = 0; i < mcip->mci_rx_fanout_cnt; i++) {
		rf = &mcip->mci_rx_fanout[i];
		thread_lock(rf->rf_worker);
		(void) thread_change_pri(rf->rf_worker, pri, 0);
		thread_unlock(rf->rf_worker);
	}
	mac_rx_client_restart((mac_client_handle_t)mcip);
}

/*
 * RX and TX rings are reserved according to different semantics depending
 * on the requests from the MAC clients and type of rings:
 *
 * On the Tx side, by default we reserve individual rings, independently from
 * the groups.
 *
 * On the Rx side, the reservation is at the granularity of the group
 * of rings, and used for v12n level 1 only. It has a special case for the
 * primary client.
 *
 * If a share is allocated to a MAC client, we allocate a TX group and an
 * RX group to the client, and assign TX rings and RX rings to these
 * groups according to information gathered from the driver through
 * the share capability.
 *
 * The foreseable evolution of Rx rings will handle v12n level 2 and higher
 * to allocate individual rings out of a group and program the hw classifier
 * based on IP address or higher level criteria.
 */

/*
 * For a reserved group with multiple clients, return the primary client.
 */
mac_client_impl_t *
mac_get_grp_primary(mac_group_t *grp)
{
	mac_grp_client_t	*mgcp = grp->mrg_clients;
	mac_client_impl_t	*mcip;

	while (mgcp != NULL) {
		mcip = mgcp->mgc_client;
		if (mcip->mci_flent->fe_type & FLOW_PRIMARY_MAC)
			return (mcip);
		mgcp = mgcp->mgc_next;
	}
	return (NULL);
}

/*
 * Hybrid I/O specifies the ring that should be given to a share.
 * If the ring is already used by clients, then we need to release
 * the ring back to the default group so that we can give it to
 * the share. This means the clients using this ring now get a
 * replacement ring. If there aren't any replacement rings, this
 * function returns a failure.
 */
static int
mac_reclaim_ring_from_grp(mac_impl_t *mip, mac_ring_type_t ring_type,
    mac_ring_t *ring, mac_ring_t **rings, int nrings)
{
	mac_group_t		*group = (mac_group_t *)ring->mr_gh;
	mac_resource_props_t	*mrp;
	mac_client_impl_t	*mcip;
	mac_group_t		*defgrp;
	mac_ring_t		*tring;
	mac_group_t		*tgrp;
	int			i;
	int			j;

	mcip = MAC_GROUP_ONLY_CLIENT(group);
	if (mcip == NULL)
		mcip = mac_get_grp_primary(group);
	ASSERT(mcip != NULL);
	ASSERT(mcip->mci_share == NULL);

	mrp = MCIP_RESOURCE_PROPS(mcip);
	if (ring_type == MAC_RING_TYPE_RX) {
		defgrp = mip->mi_rx_donor_grp;
		if ((mrp->mrp_mask & MRP_RX_RINGS) == 0) {
			/* Need to put this mac client in the default group */
			if (mac_rx_switch_group(mcip, group, defgrp) != 0)
				return (ENOSPC);
		} else {
			/*
			 * Switch this ring with some other ring from
			 * the default group.
			 */
			for (tring = defgrp->mrg_rings; tring != NULL;
			    tring = tring->mr_next) {
				if (tring->mr_index == 0)
					continue;
				for (j = 0; j < nrings; j++) {
					if (rings[j] == tring)
						break;
				}
				if (j >= nrings)
					break;
			}
			if (tring == NULL)
				return (ENOSPC);
			if (mac_group_mov_ring(mip, group, tring) != 0)
				return (ENOSPC);
			if (mac_group_mov_ring(mip, defgrp, ring) != 0) {
				(void) mac_group_mov_ring(mip, defgrp, tring);
				return (ENOSPC);
			}
		}
		ASSERT(ring->mr_gh == (mac_group_handle_t)defgrp);
		return (0);
	}

	defgrp = MAC_DEFAULT_TX_GROUP(mip);
	if (ring == (mac_ring_t *)mip->mi_default_tx_ring) {
		/*
		 * See if we can get a spare ring to replace the default
		 * ring.
		 */
		if (defgrp->mrg_cur_count == 1) {
			/*
			 * Need to get a ring from another client, see if
			 * there are any clients that can be moved to
			 * the default group, thereby freeing some rings.
			 */
			for (tgrp = mip->mi_tx_groups, i = 0;
			    i < mip->mi_tx_group_count; i++) {
				if (tgrp->mrg_state ==
				    MAC_GROUP_STATE_REGISTERED) {
					continue;
				}

				mcip = MAC_GROUP_ONLY_CLIENT(tgrp);
				if (mcip == NULL)
					mcip = mac_get_grp_primary(tgrp);
				ASSERT(mcip != NULL);
				mrp = MCIP_RESOURCE_PROPS(mcip);
				if ((mrp->mrp_mask & MRP_TX_RINGS) == 0) {
					ASSERT(tgrp->mrg_cur_count == 1);
					/*
					 * If this ring is part of the
					 * rings asked by the share we cannot
					 * use it as the default ring.
					 */
					for (j = 0; j < nrings; j++) {
						if (rings[j] == tgrp->mrg_rings)
							break;
					}
					if (j < nrings)
						continue;
					mac_tx_switch_group(mcip, tgrp,
					    defgrp);
					break;
				}

				tgrp = tgrp->mrg_next;
			}
			/*
			 * All the rings are reserved, can't give up the
			 * default ring.
			 */
			if (defgrp->mrg_cur_count <= 1)
				return (ENOSPC);
		}
		/*
		 * Swap the default ring with another.
		 */
		for (tring = defgrp->mrg_rings; tring != NULL;
		    tring = tring->mr_next) {
			/*
			 * If this ring is part of the rings asked by the
			 * share we cannot use it as the default ring.
			 */
			for (j = 0; j < nrings; j++) {
				if (rings[j] == tring)
					break;
			}
			if (j >= nrings)
				break;
		}
		ASSERT(tring != NULL);
		mip->mi_default_tx_ring = (mac_ring_handle_t)tring;
		return (0);
	}
	/*
	 * The Tx ring is with a group reserved by a MAC client. See if
	 * we can swap it.
	 */
	ASSERT(group->mrg_state == MAC_GROUP_STATE_RESERVED);
	mcip = MAC_GROUP_ONLY_CLIENT(group);
	if (mcip == NULL)
		mcip = mac_get_grp_primary(group);
	ASSERT(mcip !=  NULL);
	mrp = MCIP_RESOURCE_PROPS(mcip);
	if ((mrp->mrp_mask & MRP_TX_RINGS) == 0) {
		ASSERT(group->mrg_cur_count == 1);
		/* Put this mac client in the default group */
		mac_tx_switch_group(mcip, group, defgrp);
	} else {
		/*
		 * Switch this ring with some other ring from
		 * the default group.
		 */
		for (tring = defgrp->mrg_rings; tring != NULL;
		    tring = tring->mr_next) {
			if (tring == (mac_ring_t *)mip->mi_default_tx_ring)
				continue;
			/*
			 * If this ring is part of the rings asked by the
			 * share we cannot use it for swapping.
			 */
			for (j = 0; j < nrings; j++) {
				if (rings[j] == tring)
					break;
			}
			if (j >= nrings)
				break;
		}
		if (tring == NULL) {
			return (ENOSPC);
		}
		if (mac_group_mov_ring(mip, group, tring) != 0) {
			return (ENOSPC);
		}
		if (mac_group_mov_ring(mip, defgrp, ring) != 0) {
			(void) mac_group_mov_ring(mip, defgrp, tring);
			return (ENOSPC);
		}
	}
	ASSERT(ring->mr_gh == (mac_group_handle_t)defgrp);
	return (0);
}

/*
 * Populate a zero-ring group with rings. If the share is non-NULL,
 * the rings are chosen according to that share.
 * Invoked after allocating a new RX or TX group through
 * mac_reserve_rx_group() or mac_reserve_tx_group(), respectively.
 * Returns zero on success, an errno otherwise.
 */
int
i_mac_group_allocate_rings(mac_impl_t *mip, mac_ring_type_t ring_type,
    mac_group_t *src_group, mac_group_t *new_group, mac_share_handle_t share,
    uint32_t ringcnt)
{
	mac_ring_t **rings, *ring;
	uint_t nrings, ringcount = ringcnt;
	int rv = 0, i = 0, j;

	ASSERT((ring_type == MAC_RING_TYPE_RX &&
	    mip->mi_rx_group_type == MAC_GROUP_TYPE_DYNAMIC) ||
	    (ring_type == MAC_RING_TYPE_TX &&
	    mip->mi_tx_group_type == MAC_GROUP_TYPE_DYNAMIC));

	/*
	 * First find the rings to allocate to the group.
	 */
	if (share != NULL) {
		/* get rings through ms_squery() */
		mip->mi_share_capab.ms_squery(share, ring_type, NULL, &nrings);
		ASSERT(nrings != 0);
		rings = kmem_alloc(nrings * sizeof (mac_ring_handle_t),
		    KM_SLEEP);
		ringcount = nrings;
		mip->mi_share_capab.ms_squery(share, ring_type,
		    (mac_ring_handle_t *)rings, &nrings);
		for (i = 0; i < nrings; i++) {
			/*
			 * If we have given this ring to a non-default
			 * group, we need to check if we can get this
			 * ring.
			 */
			ring = rings[i];
			if (ring->mr_gh != (mac_group_handle_t)src_group ||
			    ring == (mac_ring_t *)mip->mi_default_tx_ring) {
				if (mac_reclaim_ring_from_grp(mip, ring_type,
				    ring, rings, nrings) != 0) {
					rv = ENOSPC;
					goto bail;
				}
			}
		}
	} else {
		/*
		 * Pick one ring from default group.
		 *
		 * for now pick the second ring which requires the first ring
		 * at index 0 to stay in the default group, since it is the
		 * ring which carries the multicast traffic.
		 * We need a better way for a driver to indicate this,
		 * for example a per-ring flag.
		 */
		rings = kmem_alloc(ringcnt * sizeof (mac_ring_handle_t),
		    KM_SLEEP);
		for (ring = src_group->mrg_rings; ring != NULL;
		    ring = ring->mr_next) {
			if (ring_type == MAC_RING_TYPE_RX &&
			    ring->mr_index == 0) {
				continue;
			}
			if (ring_type == MAC_RING_TYPE_TX &&
			    ring == (mac_ring_t *)mip->mi_default_tx_ring) {
				continue;
			}
			rings[i++] = ring;
			if (i == ringcnt)
				break;
		}
		nrings = i;
		/* Not enough rings as required */
		if (nrings != ringcnt) {
			rv = ENOSPC;
			goto bail;
		}
	}

	switch (ring_type) {
	case MAC_RING_TYPE_RX:
		if (src_group->mrg_cur_count - nrings < 1) {
			/* we ran out of rings */
			rv = ENOSPC;
			goto bail;
		}

		/* move receive rings to new group */
		for (i = 0; i < nrings; i++) {
			rv = mac_group_mov_ring(mip, new_group, rings[i]);
			if (rv != 0) {
				/* move rings back on failure */
				for (j = 0; j < i; j++) {
					(void) mac_group_mov_ring(mip,
					    src_group, rings[j]);
				}
				goto bail;
			}
		}
		break;

	case MAC_RING_TYPE_TX: {
		/* move the TX rings to the new group */
		for (i = 0; i < nrings; i++) {
			rv = mac_group_mov_ring(mip, new_group, rings[i]);
			if (rv != 0) {
				/* cleanup on failure */
				for (j = 0; j < i; j++) {
					(void) mac_group_mov_ring(mip,
					    MAC_DEFAULT_TX_GROUP(mip),
					    rings[j]);
				}
				goto bail;
			}
		}
		break;
	}
	}

	/* add group to share */
	if (share != NULL)
		mip->mi_share_capab.ms_sadd(share, new_group->mrg_driver);

bail:
	/* free temporary array of rings */
	kmem_free(rings, ringcount * sizeof (mac_ring_handle_t));
	return (rv);
}

mac_group_t *
mac_group_find(mac_impl_t *mip, mac_ring_type_t type, int grp_index)
{
	mac_group_t *grp, *grps = NULL;
	int grpcnt, i;

	ASSERT(mip != NULL);

	switch (type) {
	case MAC_RING_TYPE_TX:
		grps = mip->mi_tx_groups;
		grpcnt = mip->mi_tx_group_count;
		break;
	case MAC_RING_TYPE_RX:
		grps = mip->mi_rx_groups;
		grpcnt = mip->mi_rx_group_count;
		break;
	}

	ASSERT(grps != NULL);

	for (grp = grps, i = 0; i < grpcnt; i++) {
		if (grp->mrg_index == grp_index)
			return (grp);
		grp = grp->mrg_next;
	}

	return (NULL);
}

void
mac_group_add_client(mac_group_t *grp, mac_client_impl_t *mcip)
{
	mac_grp_client_t *mgcp;

	ASSERT(grp != NULL);
	ASSERT(mcip != NULL);

	for (mgcp = grp->mrg_clients; mgcp != NULL; mgcp = mgcp->mgc_next) {
		if (mgcp->mgc_client == mcip)
			break;
	}

	VERIFY(mgcp == NULL);

	mgcp = kmem_zalloc(sizeof (mac_grp_client_t), KM_SLEEP);
	mgcp->mgc_client = mcip;
	mgcp->mgc_next = grp->mrg_clients;
	grp->mrg_clients = mgcp;

}

void
mac_group_remove_client(mac_group_t *grp, mac_client_impl_t *mcip)
{
	mac_grp_client_t *mgcp, **pprev;

	ASSERT(grp != NULL);
	ASSERT(mcip != NULL);

	for (pprev = &grp->mrg_clients, mgcp = *pprev; mgcp != NULL;
	    pprev = &mgcp->mgc_next, mgcp = *pprev) {
		if (mgcp->mgc_client == mcip)
			break;
	}

	ASSERT(mgcp != NULL);

	*pprev = mgcp->mgc_next;
	kmem_free(mgcp, sizeof (mac_grp_client_t));
}

/*
 * mac_reserve_rx_group()
 *
 * Finds an available group and exclusively reserves it for a client.
 * The group is chosen to suit the flow's resource controls (bandwidth and
 * fanout requirements) and the address type.
 * If the requestor is the pimary MAC then return the group with the
 * largest number of rings, otherwise the default ring when available.
 */
mac_group_t *
mac_reserve_rx_group(mac_client_impl_t *mcip, uint8_t *mac_addr, boolean_t move)
{
	mac_share_handle_t	share = mcip->mci_share;
	mac_impl_t		*mip = mcip->mci_mip;
	mac_group_t		*grp = NULL;
	int			i;
	int			err = 0;
	mac_address_t		*map;
	mac_resource_props_t	*mrp = MCIP_RESOURCE_PROPS(mcip);
	int			nrings;
	int			donor_grp_rcnt;
	boolean_t		need_exclgrp = B_FALSE;
	int			need_rings = 0;
	mac_group_t		*candidate_grp = NULL;
	mac_client_impl_t	*gclient;
	mac_resource_props_t	*gmrp;
	mac_group_t		*donorgrp = NULL;
	boolean_t		rxhw = mrp->mrp_mask & MRP_RX_RINGS;
	boolean_t		unspec = mrp->mrp_mask & MRP_RXRINGS_UNSPEC;
	boolean_t		isprimary;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mip));

	isprimary = mcip->mci_flent->fe_type & FLOW_PRIMARY_MAC;

	/*
	 * Check if a group already has this mac address (case of VLANs)
	 * unless we are moving this MAC client from one group to another.
	 */
	if (!move && (map = mac_find_macaddr(mip, mac_addr)) != NULL) {
		if (map->ma_group != NULL)
			return (map->ma_group);
	}
	if (mip->mi_rx_groups == NULL || mip->mi_rx_group_count == 0)
		return (NULL);
	/*
	 * If exclusive open, return NULL which will enable the
	 * caller to use the default group.
	 */
	if (mcip->mci_state_flags & MCIS_EXCLUSIVE)
		return (NULL);

	/* For dynamic groups default unspecified to 1 */
	if (rxhw && unspec &&
	    mip->mi_rx_group_type == MAC_GROUP_TYPE_DYNAMIC) {
		mrp->mrp_nrxrings = 1;
	}
	/*
	 * For static grouping we allow only specifying rings=0 and
	 * unspecified
	 */
	if (rxhw && mrp->mrp_nrxrings > 0 &&
	    mip->mi_rx_group_type == MAC_GROUP_TYPE_STATIC) {
		return (NULL);
	}
	if (rxhw) {
		/*
		 * We have explicitly asked for a group (with nrxrings,
		 * if unspec).
		 */
		if (unspec || mrp->mrp_nrxrings > 0) {
			need_exclgrp = B_TRUE;
			need_rings = mrp->mrp_nrxrings;
		} else if (mrp->mrp_nrxrings == 0) {
			/*
			 * We have asked for a software group.
			 */
			return (NULL);
		}
	} else if (isprimary && mip->mi_nactiveclients == 1 &&
	    mip->mi_rx_group_type == MAC_GROUP_TYPE_DYNAMIC) {
		/*
		 * If the primary is the only active client on this
		 * mip and we have not asked for any rings, we give
		 * it the default group so that the primary gets to
		 * use all the rings.
		 */
		return (NULL);
	}

	/* The group that can donate rings */
	donorgrp = mip->mi_rx_donor_grp;

	/*
	 * The number of rings that the default group can donate.
	 * We need to leave at least one ring.
	 */
	donor_grp_rcnt = donorgrp->mrg_cur_count - 1;

	/*
	 * Try to exclusively reserve a RX group.
	 *
	 * For flows requiring HW_DEFAULT_RING (unicast flow of the primary
	 * client), try to reserve the a non-default RX group and give
	 * it all the rings from the donor group, except the default ring
	 *
	 * For flows requiring HW_RING (unicast flow of other clients), try
	 * to reserve non-default RX group with the specified number of
	 * rings, if available.
	 *
	 * For flows that have not asked for software or hardware ring,
	 * try to reserve a non-default group with 1 ring, if available.
	 */
	if (mip->mi_rx_group_count > 1) {
		ASSERT(mip->mi_rx_groups->mrg_next != NULL);

		for (grp = mip->mi_rx_groups->mrg_next, i = 1;
		    i < mip->mi_rx_group_count; grp = grp->mrg_next, i++)  {
			ASSERT(grp != NULL);

			DTRACE_PROBE3(rx__group__trying, char *,
			    mip->mi_linkname, int, grp->mrg_index,
			    mac_group_state_t, grp->mrg_state);

			/*
			 * Check if this group could be a candidate group for
			 * eviction if we need a group for this MAC client,
			 * but there aren't any. A candidate group is one
			 * that didn't ask for an exclusive group, but got
			 * one and it has enough rings (combined with what
			 * the donor group can donate) for the new MAC
			 * client
			 */
			if (grp->mrg_state >= MAC_GROUP_STATE_RESERVED) {
				/*
				 * If the primary/donor group is not the default
				 * group, don't bother looking for a
				 * candidate group. If we don't have enough
				 * rings we will check if the primary group
				 * can be vacated.
				 */
				if ((candidate_grp == NULL) &&
				    (donorgrp == MAC_DEFAULT_RX_GROUP(mip))) {
					ASSERT(!MAC_GROUP_NO_CLIENT(grp));
					gclient = MAC_GROUP_ONLY_CLIENT(grp);
					if (gclient == NULL)
						gclient =
						    mac_get_grp_primary(grp);
					ASSERT(gclient != NULL);
					gmrp = MCIP_RESOURCE_PROPS(gclient);
					if ((gclient->mci_share == NULL) &&
					    ((gmrp->mrp_mask & MRP_RX_RINGS) ==
					    0) && (unspec ||
					    (grp->mrg_cur_count +
					    donor_grp_rcnt >= need_rings))) {
						candidate_grp = grp;
					}
				}
				continue;
			}
			/*
			 * This group could already be SHARED by other multicast
			 * flows on this client. In that case, the group would
			 * be shared and has already been started.
			 */
			ASSERT(grp->mrg_state != MAC_GROUP_STATE_UNINIT);
			if (mip->mi_rx_group_type != MAC_GROUP_TYPE_DYNAMIC)
				break;
			ASSERT(grp->mrg_cur_count == 0);

			/*
			 * Populate the group. Rings should be taken
			 * from the donor group.
			 */
			nrings = rxhw ? need_rings : isprimary ?
			    donor_grp_rcnt: 1;

			/*
			 * If the donor group can't donate, let's just walk and
			 * see if someone can vacate a group, so that we have
			 * enough rings for this, unless we already have
			 * identified a candiate group..
			 */
			if (nrings <= donor_grp_rcnt) {
				err = i_mac_group_allocate_rings(mip,
				    MAC_RING_TYPE_RX, donorgrp, grp, share,
				    nrings);
				if (err == 0) {
					/*
					 * For a share
					 * i_mac_group_allocate_rings gets the
					 * rings from the driver, let's populate
					 * the property for the client now.
					 */
					if (share != NULL) {
						mac_client_set_rings(
						    (mac_client_handle_t)mcip,
						    grp->mrg_cur_count, -1);
					}
					if (mac_is_primary_client(mcip) &&
					    !rxhw)
						mip->mi_rx_donor_grp = grp;
					break;
				}
			}
		}
	} else {
		i = 1;
	}

	/* We didn't find an exclusive group for this MAC client */
	if (i >= mip->mi_rx_group_count) {
		if (!need_exclgrp)
			return (NULL);

		/*
		 * If we found a candidate group then we switch the
		 * MAC client from the candidate_group to the default
		 * group and give the group to this MAC client. If
		 * we didn't find a candidate_group, check if the
		 * primary is in its own group and if it can make way
		 * for this MAC client.
		 */
		if ((candidate_grp == NULL) &&
		    (donorgrp != MAC_DEFAULT_RX_GROUP(mip)) &&
		    (donorgrp->mrg_cur_count >= need_rings)) {
			candidate_grp = donorgrp;
		}
		if (candidate_grp != NULL) {
			boolean_t	prim_grp = B_FALSE;

			/*
			 * Switch the MAC client from the candidate group
			 * to the default group.. If this group was the
			 * donor group, then after the switch we need
			 * to update the donor group too.
			 */
			grp = candidate_grp;
			gclient = MAC_GROUP_ONLY_CLIENT(grp);
			if (gclient == NULL)
				gclient = mac_get_grp_primary(grp);
			if (grp == mip->mi_rx_donor_grp)
				prim_grp = B_TRUE;
			if (mac_rx_switch_group(gclient, grp,
			    MAC_DEFAULT_RX_GROUP(mip)) != 0) {
				return (NULL);
			}
			if (prim_grp) {
				mip->mi_rx_donor_grp =
				    MAC_DEFAULT_RX_GROUP(mip);
				donorgrp = MAC_DEFAULT_RX_GROUP(mip);
			}


			/*
			 * Now give this group with the required rings
			 * to this MAC client.
			 */
			ASSERT(grp->mrg_state == MAC_GROUP_STATE_REGISTERED);
			if (mip->mi_rx_group_type != MAC_GROUP_TYPE_DYNAMIC)
				goto done;

			donor_grp_rcnt = donorgrp->mrg_cur_count - 1;
			ASSERT(grp->mrg_cur_count == 0);
			ASSERT(donor_grp_rcnt >= need_rings);
			err = i_mac_group_allocate_rings(mip, MAC_RING_TYPE_RX,
			    donorgrp, grp, share, need_rings);
			if (err == 0) {
				/*
				 * For a share i_mac_group_allocate_rings gets
				 * the rings from the driver, let's populate
				 * the property for the client now.
				 */
				if (share != NULL) {
					mac_client_set_rings(
					    (mac_client_handle_t)mcip,
					    grp->mrg_cur_count, -1);
				}
				goto done;
			}
		}
		return (NULL);
	}
	ASSERT(grp != NULL);

done:
	if (grp->mrg_state == MAC_GROUP_STATE_REGISTERED &&
	    mac_start_group_and_rings(grp) != 0) {
		DTRACE_PROBE2(rx__group__start__failed,
		    char *, mip->mi_linkname, int, grp->mrg_index);
		return (NULL);
	}
	DTRACE_PROBE2(rx__group__reserved,
	    char *, mip->mi_linkname, int, grp->mrg_index);

	return (grp);
}

/*
 * mac_rx_release_group()
 *
 * This is called when there are no clients left for the group.
 * The group is stopped and marked MAC_GROUP_STATE_REGISTERED,
 * and if it is a non default group, the shares are removed and
 * all rings are assigned back to default group.
 */
void
mac_release_rx_group(mac_client_impl_t *mcip, mac_group_t *group)
{
	mac_impl_t		*mip = mcip->mci_mip;

	ASSERT(group != MAC_DEFAULT_RX_GROUP(mip));
	mac_stop_group_and_rings(group);

	if (mip->mi_rx_donor_grp == group)
		mip->mi_rx_donor_grp = MAC_DEFAULT_RX_GROUP(mip);

	/* remove group from share */
	if (mcip->mci_share != NULL) {
		mip->mi_share_capab.ms_sremove(mcip->mci_share,
		    group->mrg_driver);
	}

	if (mip->mi_rx_group_type == MAC_GROUP_TYPE_DYNAMIC) {
		mac_ring_t *ring;

		/*
		 * Rings were dynamically allocated to group.
		 * Move rings back to default group.
		 */
		while ((ring = group->mrg_rings) != NULL) {
			(void) mac_group_mov_ring(mip, mip->mi_rx_donor_grp,
			    ring);
		}
	}
	/*
	 * Possible improvement: See if we can assign the group just released
	 * to a another client of the mip
	 */
}

/*
 * When we move the primary's mac address between groups, we need to also
 * take all the clients sharing the same mac address along with it (VLANs)
 * We remove the mac address for such clients from the group after quiescing
 * them. When we add the mac address we restart the client. Note that
 * the primary's mac address is removed from the group after all the
 * other clients sharing the address are removed. Similarly, the primary's
 * mac address is added before all the other client's mac address are
 * added. While grp is the group where the clients reside, tgrp is
 * the group where the addresses have to be added.
 */
static void
mac_rx_move_macaddr_prim(mac_client_impl_t *mcip, mac_group_t *grp,
    mac_group_t *tgrp, uint8_t *maddr, boolean_t add)
{
	mac_impl_t		*mip = mcip->mci_mip;
	mac_grp_client_t	*mgcp = grp->mrg_clients;
	mac_client_impl_t	*gmcip;
	boolean_t		prim;

	prim = (mcip->mci_state_flags & MCIS_UNICAST_HW) != 0;

	/*
	 * If the clients are in a non-default group, we just have to
	 * walk the group's client list. If it is in the default group
	 * (which will be shared by other clients as well, we need to
	 * check if the unicast address matches mcip's unicast.
	 */
	while (mgcp != NULL) {
		gmcip = mgcp->mgc_client;
		if ((gmcip != mcip) && (grp != MAC_DEFAULT_RX_GROUP(mip) ||
		    (mcip->mci_unicast == gmcip->mci_unicast))) {
			if (!add) {
				(void) mac_remove_macaddr(mip,
				    mcip->mci_unicast->ma_addr);
			} else {
				(void) mac_add_macaddr(mip, tgrp,
				    maddr, prim, B_FALSE);
			}
		}
		mgcp = mgcp->mgc_next;
	}
}


/*
 * Move the MAC address from fgrp to tgrp. If this is the primary client,
 * we need to take any VLANs etc. together too.
 */
static int
mac_rx_move_macaddr(mac_client_impl_t *mcip, mac_group_t *fgrp,
    mac_group_t *tgrp)
{
	mac_impl_t		*mip = mcip->mci_mip;
	uint8_t			maddr[MAXMACADDRLEN];
	int			err = 0;
	boolean_t		prim;
	boolean_t		multiclnt = B_FALSE;

	ASSERT(mcip->mci_unicast != NULL);
	bcopy(mcip->mci_unicast->ma_addr, maddr, mcip->mci_unicast->ma_len);

	prim = (mcip->mci_state_flags & MCIS_UNICAST_HW) != 0;
	if (mcip->mci_unicast->ma_nusers > 1) {
		mac_rx_move_macaddr_prim(mcip, fgrp, NULL, maddr, B_FALSE);
		multiclnt = B_TRUE;
	}
	ASSERT(mcip->mci_unicast->ma_nusers == 1);
	err = mac_remove_macaddr(mip, mcip->mci_unicast->ma_addr);
	if (err != 0) {
		if (multiclnt) {
			mac_rx_move_macaddr_prim(mcip, fgrp, fgrp, maddr,
			    B_TRUE);
		}
		return (err);
	}
	/*
	 * Program the H/W Classifier first, if this fails we need
	 * not proceed with the other stuff.
	 */
	if ((err = mac_add_macaddr(mip, tgrp, maddr, prim, B_FALSE)) != 0) {
		/* Revert back the H/W Classifier */
		if ((err = mac_add_macaddr(mip, fgrp, maddr, prim,
		    B_FALSE)) != 0) {
			/*
			 * This should not fail now since it worked earlier,
			 * should we panic?
			 */
			cmn_err(CE_WARN,
			    "mac_rx_switch_group: switching %p back"
			    " to group %p failed!!", (void *)mcip,
			    (void *)fgrp);
		}
		if (multiclnt) {
			mac_rx_move_macaddr_prim(mcip, fgrp, fgrp, maddr,
			    B_TRUE);
		}
		return (err);
	}
	mcip->mci_unicast = mac_find_macaddr(mip, maddr);
	if (multiclnt)
		mac_rx_move_macaddr_prim(mcip, fgrp, tgrp, maddr, B_TRUE);
	return (err);
}

static void
mac_numa_ring_remove(mac_group_t *grp)
{
	mac_ring_t *ring;

	for (ring = grp->mrg_rings; ring != NULL; ring = ring->mr_next)
		mac_cpu_modify(NULL, MAC_CPU_RING_REMOVE, ring);
}

/*
 * Switch the MAC client from one group to another. This means we need
 * to remove the MAC address from the group, remove the MAC client,
 * and revert the group state. Then, we add the client to the destination
 * group, and add the MAC address to the group.
 */
int
mac_rx_switch_group(mac_client_impl_t *mcip, mac_group_t *fgrp,
    mac_group_t *tgrp)
{
	int			err;
	mac_group_state_t	next_state;
	mac_client_impl_t	*group_only_mcip;
	mac_client_impl_t	*gmcip;
	mac_impl_t		*mip = mcip->mci_mip;
	mac_grp_client_t	*mgcp;

	ASSERT(fgrp == mcip->mci_flent->fe_rx_ring_group);

	if ((err = mac_rx_move_macaddr(mcip, fgrp, tgrp)) != 0)
		return (err);

	/*
	 * During swicthing groups, make sure to remove
	 * numaio objects from numa groups if present.
	 */
	mac_numa_ring_remove(fgrp);
	mac_numa_ring_remove(tgrp);

	if (fgrp != MAC_DEFAULT_RX_GROUP(mip)) {
		mgcp = fgrp->mrg_clients;
		while (mgcp != NULL) {
			gmcip = mgcp->mgc_client;
			mgcp = mgcp->mgc_next;
			mac_group_remove_client(fgrp, gmcip);
			mac_group_add_client(tgrp, gmcip);
			gmcip->mci_flent->fe_rx_ring_group = tgrp;
		}
		mac_release_rx_group(mcip, fgrp);
		ASSERT(MAC_GROUP_NO_CLIENT(fgrp));
		mac_set_group_state(fgrp, MAC_GROUP_STATE_REGISTERED);
	} else {
		mac_group_remove_client(fgrp, mcip);
		mac_group_add_client(tgrp, mcip);
		mcip->mci_flent->fe_rx_ring_group = tgrp;
		/*
		 * If there are other clients (VLANs) sharing this address
		 * we should be here only for the primary.
		 */
		if (mcip->mci_unicast->ma_nusers > 1) {
			/*
			 * We need to move all the clients that are using
			 * this h/w address.
			 */
			mgcp = fgrp->mrg_clients;
			while (mgcp != NULL) {
				gmcip = mgcp->mgc_client;
				mgcp = mgcp->mgc_next;
				if (mcip->mci_unicast == gmcip->mci_unicast) {
					mac_group_remove_client(fgrp, gmcip);
					mac_group_add_client(tgrp, gmcip);
					gmcip->mci_flent->fe_rx_ring_group =
					    tgrp;
				}
			}
		}
		mac_set_group_state(fgrp, MAC_GROUP_STATE_SHARED);
	}
	next_state = mac_group_next_state(tgrp, &group_only_mcip,
	    MAC_DEFAULT_RX_GROUP(mip), B_TRUE);
	mac_set_group_state(tgrp, next_state);
	/*
	 * If the destination group is reserved, setup the group.
	 */
	mac_rx_group_setup(mcip);
	mac_cpu_setup(mcip);
	return (0);
}

/*
 * Reserves a TX group for the specified share.
 */
mac_group_t *
mac_reserve_tx_group(mac_client_impl_t *mcip, boolean_t move)
{
	mac_impl_t		*mip = mcip->mci_mip;
	mac_group_t		*grp = NULL;
	int			rv;
	int			i;
	int			err;
	mac_group_t		*defgrp;
	mac_share_handle_t	share = mcip->mci_share;
	mac_resource_props_t	*mrp = MCIP_RESOURCE_PROPS(mcip);
	int			nrings;
	int			defnrings;
	boolean_t		need_exclgrp = B_FALSE;
	int			need_rings = 0;
	mac_group_t		*candidate_grp = NULL;
	mac_client_impl_t	*gclient;
	mac_resource_props_t	*gmrp;
	boolean_t		txhw = mrp->mrp_mask & MRP_TX_RINGS;
	boolean_t		unspec = mrp->mrp_mask & MRP_TXRINGS_UNSPEC;
	boolean_t		isprimary;

	isprimary = mcip->mci_flent->fe_type & FLOW_PRIMARY_MAC;

	/*
	 * When we come here for a VLAN on the primary (dladm create-vlan),
	 * we need to pair it along with the primary (to keep it consistent
	 * with the RX side). So, we check if the primary is already assigned
	 * to a group and return the group if so. The other way is also
	 * true, i.e. the VLAN is already created and now we are plumbing
	 * the primary.
	 */
	if (!move && isprimary) {
		for (gclient = mip->mi_clients_list; gclient != NULL;
		    gclient = gclient->mci_client_next) {
			if ((gclient->mci_flent->fe_type & FLOW_PRIMARY_MAC) &&
			    (gclient->mci_flent->fe_tx_ring_group != NULL)) {
				return (gclient->mci_flent->fe_tx_ring_group);
			}
		}
	}

	if ((mip->mi_tx_groups == NULL) || (mip->mi_tx_group_count == 0))
		return (NULL);

	/* For dynamic groups, default unspec to 1 */
	if (txhw && unspec &&
	    mip->mi_tx_group_type == MAC_GROUP_TYPE_DYNAMIC) {
		mrp->mrp_ntxrings = 1;
	}
	/*
	 * For static grouping we allow only specifying rings=0 and
	 * unspecified
	 */
	if (txhw && mrp->mrp_ntxrings > 0 &&
	    (mip->mi_tx_group_type == MAC_GROUP_TYPE_STATIC)) {
		return (NULL);
	}

	if (txhw) {
		/*
		 * We have explicitly asked for a group (with ntxrings,
		 * if unspec).
		 */
		if (unspec || mrp->mrp_ntxrings > 0) {
			need_exclgrp = B_TRUE;
			need_rings = mrp->mrp_ntxrings;
		} else if (mrp->mrp_ntxrings == 0) {
			/*
			 * We have asked for a software group.
			 */
			return (NULL);
		}
	}
	defgrp = MAC_DEFAULT_TX_GROUP(mip);
	/*
	 * The number of rings that the default group can donate.
	 * We need to leave at least one ring - the default ring - in
	 * this group.
	 */
	defnrings = defgrp->mrg_cur_count - 1;

	/*
	 * Primary gets default group unless explicitly told not
	 * to  (i.e. rings > 0).
	 */
	if (isprimary && !need_exclgrp)
		return (NULL);

	nrings = (mrp->mrp_mask & MRP_TX_RINGS) != 0 ? mrp->mrp_ntxrings : 1;

	if (mip->mi_tx_group_count > 1) {
		ASSERT(mip->mi_tx_groups->mrg_next != NULL);
		for (grp = mip->mi_tx_groups, i = 0;
		    i <  mip->mi_tx_group_count; grp = grp->mrg_next, i++) {
			if (MRG_DEFAULT(grp))
				continue;
			if ((grp->mrg_state == MAC_GROUP_STATE_RESERVED) ||
			    (grp->mrg_state == MAC_GROUP_STATE_UNINIT)) {
				/*
				 * Select a candidate for replacement if we
				 * don't get an exclusive group. A candidate
				 * group is one that didn't ask for an
				 * exclusive group, but got one and it has
				 * enough rings (combined with what the
				 * default group can donate) for the new MAC
				 * client.
				 */
				if (grp->mrg_state ==
				    MAC_GROUP_STATE_RESERVED &&
				    (candidate_grp == NULL)) {
					gclient = MAC_GROUP_ONLY_CLIENT(grp);
					if (gclient == NULL) {
						gclient = mac_get_grp_primary(
						    grp);
					}
					gmrp = MCIP_RESOURCE_PROPS(gclient);
					if ((gclient->mci_share == NULL) &&
					    (gmrp->mrp_mask &
					    MRP_TX_RINGS) == 0 && (unspec ||
					    (grp->mrg_cur_count +
					    defnrings) >= need_rings)) {
						candidate_grp = grp;
					}
				}
				continue;
			}

			/*
			 * If the default can't donate let's just walk and
			 * see if someone can vacate a group, so that we have
			 * enough rings for this.
			 */
			if ((mip->mi_tx_group_type != MAC_GROUP_TYPE_DYNAMIC) ||
			    nrings <= defnrings) {
				break;
			}
		}
	} else {
		i = 1;
	}

	/* The default group */
	if (i >= mip->mi_tx_group_count) {
		/*
		 * If we need an exclusive group and have identified a
		 * candidate group we switch the MAC client from the
		 * candidate group to the default group and give the
		 * candidate group to this client.
		 */
		if (need_exclgrp && candidate_grp != NULL) {
			/*
			 * Switch the MAC client from the candidate group
			 * to the default group.
			 */
			grp = candidate_grp;
			gclient = MAC_GROUP_ONLY_CLIENT(grp);
			if (gclient == NULL)
				gclient = mac_get_grp_primary(grp);
			mac_tx_switch_group(gclient, grp, defgrp);

			/*
			 * Give the candidate group with the specified number
			 * of rings to this MAC client.
			 */
			ASSERT(grp->mrg_state == MAC_GROUP_STATE_REGISTERED);
			if (mip->mi_tx_group_type != MAC_GROUP_TYPE_DYNAMIC)
				goto done;

			ASSERT(grp->mrg_cur_count == 0);
			ASSERT(defgrp->mrg_cur_count > need_rings);

			err = i_mac_group_allocate_rings(mip, MAC_RING_TYPE_TX,
			    defgrp, grp, share, need_rings);
			if (err == 0) {
				/*
				 * For a share i_mac_group_allocate_rings gets
				 * the rings from the driver, let's populate
				 * the property for the client now.
				 */
				if (share != NULL) {
					mac_client_set_rings(
					    (mac_client_handle_t)mcip, -1,
					    grp->mrg_cur_count);
				}
				goto done;
			}
		}
		return (NULL);
	}

	/*
	 * We got an exclusive group, but it is not dynamic.
	 */
	if (mip->mi_tx_group_type != MAC_GROUP_TYPE_DYNAMIC)
		goto done;

	rv = i_mac_group_allocate_rings(mip, MAC_RING_TYPE_TX, defgrp, grp,
	    share, nrings);
	if (rv != 0) {
		DTRACE_PROBE3(tx__group__reserve__alloc__failed,
		    char *, mip->mi_linkname, int, grp->mrg_index, int, rv);
		return (NULL);
	}
	/*
	 * For a share i_mac_group_allocate_rings gets the rings from the
	 * driver, let's populate the property for the client now.
	 */
	if (share != NULL) {
		mac_client_set_rings((mac_client_handle_t)mcip, -1,
		    grp->mrg_cur_count);
	}
done:
	if (grp->mrg_state == MAC_GROUP_STATE_REGISTERED &&
	    mac_start_group_and_rings(grp) != 0) {
		DTRACE_PROBE2(tx__group__start__failed,
		    char *, mip->mi_linkname, int, grp->mrg_index);
		return (NULL);
	}
	mip->mi_tx_group_free--;
	DTRACE_PROBE2(tx__group__reserved,
	    char *, mip->mi_linkname, int, grp->mrg_index);

	return (grp);
}

void
mac_release_tx_group(mac_client_impl_t *mcip, mac_group_t *grp)
{
	mac_impl_t		*mip = mcip->mci_mip;
	mac_share_handle_t	share = mcip->mci_share;
	mac_ring_t		*ring;
	mac_group_t		*defgrp;

	mac_stop_group_and_rings(grp);
	defgrp = MAC_DEFAULT_TX_GROUP(mip);
	if (share != NULL)
		mip->mi_share_capab.ms_sremove(share, grp->mrg_driver);

	/* move the ring back to the pool */
	if (mip->mi_tx_group_type == MAC_GROUP_TYPE_DYNAMIC) {
		while ((ring = grp->mrg_rings) != NULL)
			(void) mac_group_mov_ring(mip, defgrp, ring);
	}
	mip->mi_tx_group_free++;
}

/*
 * Switch the MAC client from one group to another. This means we need
 * to remove the MAC client and revert the group state.
 * Then, we add the client to the destination group.
 */
void
mac_tx_switch_group(mac_client_impl_t *mcip, mac_group_t *fgrp,
    mac_group_t *tgrp)
{
	mac_client_impl_t	*group_only_mcip;
	mac_impl_t		*mip = mcip->mci_mip;
	flow_entry_t		*flent = mcip->mci_flent;
	mac_group_t		*defgrp;
	mac_grp_client_t	*mgcp;
	mac_client_impl_t	*gmcip;
	flow_entry_t		*gflent;

	defgrp = MAC_DEFAULT_TX_GROUP(mip);
	ASSERT(fgrp == flent->fe_tx_ring_group);

	/*
	 * During swicthing groups, make sure to remove
	 * numaio objects from numa groups if present.
	 */
	mac_numa_ring_remove(fgrp);
	mac_numa_ring_remove(tgrp);

	if (fgrp == defgrp) {
		/*
		 * If this is the primary we need to find any VLANs on
		 * the primary and move them too.
		 */
		mac_group_remove_client(fgrp, mcip);
		if (mcip->mci_unicast->ma_nusers > 1) {
			mgcp = fgrp->mrg_clients;
			while (mgcp != NULL) {
				gmcip = mgcp->mgc_client;
				mgcp = mgcp->mgc_next;
				if (mcip->mci_unicast != gmcip->mci_unicast)
					continue;

				gflent = gmcip->mci_flent;
				mac_group_remove_client(fgrp, gmcip);

				mac_group_add_client(tgrp, gmcip);
				gflent->fe_tx_ring_group = tgrp;
				/* We could directly set this to SHARED */
				tgrp->mrg_state = mac_group_next_state(tgrp,
				    &group_only_mcip, defgrp, B_FALSE);

				mac_tx_group_setup(gmcip);
				mac_cpu_setup(gmcip);
			}
		}
		if (MAC_GROUP_NO_CLIENT(fgrp)) {
			fgrp->mrg_state = MAC_GROUP_STATE_REGISTERED;
		} else if (MAC_GROUP_ONLY_CLIENT(fgrp) != NULL) {
			fgrp->mrg_state = MAC_GROUP_STATE_RESERVED;
		} else {
			ASSERT(fgrp->mrg_state == MAC_GROUP_STATE_SHARED);
		}
	} else {
		/*
		 * We could have VLANs sharing the non-default group with
		 * the primary.
		 */
		mgcp = fgrp->mrg_clients;
		while (mgcp != NULL) {
			gmcip = mgcp->mgc_client;
			mgcp = mgcp->mgc_next;
			if (gmcip == mcip)
				continue;
			gflent = gmcip->mci_flent;

			mac_group_remove_client(fgrp, gmcip);
			mac_group_add_client(tgrp, gmcip);
			gflent->fe_tx_ring_group = tgrp;
			/* We could directly set this to SHARED */
			tgrp->mrg_state = mac_group_next_state(tgrp,
			    &group_only_mcip, defgrp, B_FALSE);
			mac_tx_group_setup(gmcip);
			mac_cpu_setup(gmcip);
		}
		mac_group_remove_client(fgrp, mcip);
		mac_release_tx_group(mcip, fgrp);
		fgrp->mrg_state = MAC_GROUP_STATE_REGISTERED;
	}

	/* Add it to the tgroup */
	mac_group_add_client(tgrp, mcip);
	flent->fe_tx_ring_group = tgrp;
	tgrp->mrg_state = mac_group_next_state(tgrp, &group_only_mcip,
	    defgrp, B_FALSE);

	mac_tx_group_setup(mcip);
	mac_cpu_setup(mcip);
}

/*
 * This is a 1-time control path activity initiated by the client (IP).
 * The mac perimeter protects against other simultaneous control activities,
 * for example an ioctl that attempts to change the degree of fanout.
 */
static mac_tx_notify_cb_t *
mac_client_tx_notify_add(mac_client_impl_t *mcip,
    mac_tx_notify_t notify, void *arg)
{
	mac_cb_info_t *mcbi;
	mac_tx_notify_cb_t *mtnfp;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mcip->mci_mip));

	mtnfp = kmem_zalloc(sizeof (mac_tx_notify_cb_t), KM_SLEEP);
	mtnfp->mtnf_fn = notify;
	mtnfp->mtnf_arg = arg;
	mtnfp->mtnf_link.mcb_objp = mtnfp;
	mtnfp->mtnf_link.mcb_objsize = sizeof (mac_tx_notify_cb_t);
	mtnfp->mtnf_link.mcb_flags = MCB_TX_NOTIFY_CB_T;

	mcbi = &mcip->mci_tx_notify_cb_info;
	mutex_enter(mcbi->mcbi_lockp);
	mac_callback_add(mcbi, &mcip->mci_tx_notify_cb_list, &mtnfp->mtnf_link);
	mutex_exit(mcbi->mcbi_lockp);
	return (mtnfp);
}

static void
mac_client_tx_notify_remove(mac_client_impl_t *mcip, mac_tx_notify_cb_t *mtnfp)
{
	mac_cb_info_t	*mcbi;
	mac_cb_t	**cblist;

	ASSERT(MAC_PERIM_HELD((mac_handle_t)mcip->mci_mip));

	if (!mac_callback_find(&mcip->mci_tx_notify_cb_info,
	    &mcip->mci_tx_notify_cb_list, &mtnfp->mtnf_link)) {
		cmn_err(CE_WARN,
		    "mac_client_tx_notify_remove: callback not "
		    "found, mcip 0x%p mtnfp 0x%p", (void *)mcip, (void *)mtnfp);
		return;
	}

	mcbi = &mcip->mci_tx_notify_cb_info;
	cblist = &mcip->mci_tx_notify_cb_list;
	mutex_enter(mcbi->mcbi_lockp);
	if (mac_callback_remove(mcbi, cblist, &mtnfp->mtnf_link))
		kmem_free(mtnfp, sizeof (mac_tx_notify_cb_t));
	else
		mac_callback_remove_wait(&mcip->mci_tx_notify_cb_info);
	mutex_exit(mcbi->mcbi_lockp);
}

/*
 * mac_client_tx_notify():
 * call to add and remove flow control callback routine.
 */
mac_tx_notify_handle_t
mac_client_tx_notify(mac_client_handle_t mch, mac_tx_notify_t callb_func,
    void *ptr)
{
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;
	mac_tx_notify_cb_t	*mtnfp = NULL;

	i_mac_perim_enter(mcip->mci_mip);

	if (callb_func != NULL) {
		/* Add a notify callback */
		mtnfp = mac_client_tx_notify_add(mcip, callb_func, ptr);
	} else {
		mac_client_tx_notify_remove(mcip, (mac_tx_notify_cb_t *)ptr);
	}
	i_mac_perim_exit(mcip->mci_mip);

	return ((mac_tx_notify_handle_t)mtnfp);
}

void
mac_bridge_vectors(mac_bridge_tx_t txf, mac_bridge_rx_t rxf,
    mac_bridge_ref_t reff, mac_bridge_ls_t lsf)
{
	mac_bridge_tx_cb = txf;
	mac_bridge_rx_cb = rxf;
	mac_bridge_ref_cb = reff;
	mac_bridge_ls_cb = lsf;
}

int
mac_bridge_set(mac_handle_t mh, mac_handle_t link)
{
	mac_impl_t *mip = (mac_impl_t *)mh;
	int retv;

	mutex_enter(&mip->mi_bridge_lock);
	if (mip->mi_bridge_link == NULL) {
		mip->mi_bridge_link = link;
		mip->mi_rx = mac_rx_bridge;
		retv = 0;
	} else {
		retv = EBUSY;
	}
	mutex_exit(&mip->mi_bridge_lock);
	return (retv);
}

/*
 * Disable bridging on the indicated link.
 */
void
mac_bridge_clear(mac_handle_t mh, mac_handle_t link)
{
	mac_impl_t *mip = (mac_impl_t *)mh;

	mutex_enter(&mip->mi_bridge_lock);
	ASSERT(mip->mi_bridge_link == link);
	mip->mi_bridge_link = NULL;
	mip->mi_rx = mac_rx_common;
	mutex_exit(&mip->mi_bridge_lock);
}

void
mac_no_active(mac_handle_t mh)
{
	mac_impl_t *mip = (mac_impl_t *)mh;

	i_mac_perim_enter(mip);
	mip->mi_state_flags |= MIS_NO_ACTIVE;
	i_mac_perim_exit(mip);
}

/*
 * Walk the primary VLAN clients whenever the primary's rings property
 * changes and update the mac_resource_props_t for the VLAN's client.
 * We need to do this since we don't support setting these properties
 * on the primary's VLAN clients, but the VLAN clients have to
 * follow the primary w.r.t the rings property;
 */
void
mac_set_prim_vlan_rings(mac_impl_t  *mip, mac_resource_props_t *mrp)
{
	mac_client_impl_t	*vmcip;
	mac_resource_props_t	*vmrp;

	for (vmcip = mip->mi_clients_list; vmcip != NULL;
	    vmcip = vmcip->mci_client_next) {
		if (!(vmcip->mci_flent->fe_type & FLOW_PRIMARY_MAC) ||
		    mac_client_vid((mac_client_handle_t)vmcip) ==
		    VLAN_ID_NONE) {
			continue;
		}
		vmrp = MCIP_RESOURCE_PROPS(vmcip);

		vmrp->mrp_nrxrings =  mrp->mrp_nrxrings;
		if (mrp->mrp_mask & MRP_RX_RINGS)
			vmrp->mrp_mask |= MRP_RX_RINGS;
		else if (vmrp->mrp_mask & MRP_RX_RINGS)
			vmrp->mrp_mask &= ~MRP_RX_RINGS;

		vmrp->mrp_ntxrings =  mrp->mrp_ntxrings;
		if (mrp->mrp_mask & MRP_TX_RINGS)
			vmrp->mrp_mask |= MRP_TX_RINGS;
		else if (vmrp->mrp_mask & MRP_TX_RINGS)
			vmrp->mrp_mask &= ~MRP_TX_RINGS;

		if (mrp->mrp_mask & MRP_RXRINGS_UNSPEC)
			vmrp->mrp_mask |= MRP_RXRINGS_UNSPEC;
		else
			vmrp->mrp_mask &= ~MRP_RXRINGS_UNSPEC;

		if (mrp->mrp_mask & MRP_TXRINGS_UNSPEC)
			vmrp->mrp_mask |= MRP_TXRINGS_UNSPEC;
		else
			vmrp->mrp_mask &= ~MRP_TXRINGS_UNSPEC;
	}
}

/*
 * We are adding or removing ring(s) from a group. The source for taking
 * rings is the default group. The destination for giving rings back is
 * the default group.
 */
int
mac_group_ring_modify(mac_client_impl_t *mcip, mac_group_t *group,
    mac_group_t *defgrp)
{
	mac_resource_props_t	*mrp = MCIP_RESOURCE_PROPS(mcip);
	uint_t			modify;
	int			count;
	mac_ring_t		*ring;
	mac_ring_t		*next;
	mac_impl_t		*mip = mcip->mci_mip;
	mac_ring_t		**rings;
	uint_t			ringcnt;
	int			i = 0;
	boolean_t		rx_group = group->mrg_type == MAC_RING_TYPE_RX;
	mac_group_t		*tgrp;
	int			j;
	int			rv = 0;

	/*
	 * If we are asked for just a group, we give 1 ring, else
	 * the specified number of rings.
	 */
	if (rx_group) {
		ringcnt = (mrp->mrp_mask & MRP_RXRINGS_UNSPEC) ? 1:
		    mrp->mrp_nrxrings;
	} else {
		ringcnt = (mrp->mrp_mask & MRP_TXRINGS_UNSPEC) ? 1:
		    mrp->mrp_ntxrings;
	}

	/* don't allow modifying rings for a share for now. */
	ASSERT(mcip->mci_share == NULL);

	if (ringcnt == group->mrg_cur_count)
		return (0);

	if (group->mrg_cur_count > ringcnt) {
		modify = group->mrg_cur_count - ringcnt;
		if (rx_group) {
			if (mip->mi_rx_donor_grp == group) {
				ASSERT(mac_is_primary_client(mcip));
				mip->mi_rx_donor_grp = defgrp;
			} else {
				defgrp = mip->mi_rx_donor_grp;
			}
		}
		ring = group->mrg_rings;
		rings = kmem_alloc(modify * sizeof (mac_ring_handle_t),
		    KM_SLEEP);
		j = 0;
		for (count = 0; count < modify; count++) {
			next = ring->mr_next;
			rv = mac_group_mov_ring(mip, defgrp, ring);
			if (rv != 0) {
				/* cleanup on failure */
				for (j = 0; j < count; j++) {
					(void) mac_group_mov_ring(mip, group,
					    rings[j]);
				}
				break;
			}
			rings[j++] = ring;
			ring = next;
		}
		kmem_free(rings, modify * sizeof (mac_ring_handle_t));
		return (rv);
	}
	if (ringcnt >= MAX_RINGS_PER_GROUP)
		return (EINVAL);

	modify = ringcnt - group->mrg_cur_count;

	/*
	 * If the default doesn't have any rings, lets see if we can
	 * take rings given to an h/w client that doesn't need it.
	 * For now, we just see if there is  any one client that can donate
	 * all the required rings.
	 */
	if (rx_group) {
		if (defgrp != mip->mi_rx_donor_grp) {
			if (mip->mi_rx_donor_grp->mrg_cur_count ==
			    modify) {
				mcip = MAC_GROUP_ONLY_CLIENT(
				    mip->mi_rx_donor_grp);
				if (mcip == NULL)
					mcip = mac_get_grp_primary(
					    mip->mi_rx_donor_grp);
				ASSERT(mcip != NULL);
				if (mac_rx_switch_group(mcip,
				    mip->mi_rx_donor_grp, defgrp) != 0)
					return (ENOSPC);
			}
			/*
			 * This is the donor group with all the remaining
			 * rings. Default group now gets to be the donor.
			 */
			defgrp = mip->mi_rx_donor_grp;
		} else if (defgrp->mrg_cur_count < (modify + 1)) {
			for (i = 1; i < mip->mi_rx_group_count; i++) {
				tgrp = mac_group_find(mip, MAC_RING_TYPE_RX, i);
				if (tgrp == group || tgrp->mrg_state <
				    MAC_GROUP_STATE_RESERVED) {
					continue;
				}
				mcip = MAC_GROUP_ONLY_CLIENT(tgrp);
				if (mcip == NULL)
					mcip = mac_get_grp_primary(tgrp);
				ASSERT(mcip != NULL);
				mrp = MCIP_RESOURCE_PROPS(mcip);
				if ((mrp->mrp_mask & MRP_RX_RINGS) != 0)
					continue;
				if ((tgrp->mrg_cur_count +
				    defgrp->mrg_cur_count) < (modify + 1)) {
					continue;
				}
				if (mac_rx_switch_group(mcip, tgrp,
				    defgrp) != 0) {
					return (ENOSPC);
				}
				break;
			}
		}
	} else {
		if (defgrp->mrg_cur_count < (modify + 1)) {
			for (i = 1; i < mip->mi_tx_group_count; i++) {
				tgrp = mac_group_find(mip, MAC_RING_TYPE_TX, i);
				if (tgrp == group || tgrp->mrg_state <
				    MAC_GROUP_STATE_RESERVED) {
					continue;
				}
				mcip = MAC_GROUP_ONLY_CLIENT(tgrp);
				if (mcip == NULL)
					mcip = mac_get_grp_primary(tgrp);
				mrp = MCIP_RESOURCE_PROPS(mcip);
				if ((mrp->mrp_mask & MRP_TX_RINGS) != 0)
					continue;
				if ((tgrp->mrg_cur_count +
				    defgrp->mrg_cur_count) < (modify + 1)) {
					continue;
				}
				/* OK, we can switch this to s/w */
				mac_tx_switch_group(mcip, tgrp, defgrp);
				break;
			}
		}
	}
	if (defgrp->mrg_cur_count < (modify + 1))
		return (ENOSPC);

	if ((rv = i_mac_group_allocate_rings(mip, group->mrg_type, defgrp,
	    group, mcip->mci_share, modify)) != 0) {
		return (rv);
	}
	return (0);
}

/*
 * Given the poolname in mac_resource_props, find the cpupart
 * that is associated with this pool.  The cpupart will be used
 * later for finding the cpus to be bound to the networking threads.
 *
 * use_default is set B_TRUE if pools are enabled and pool_default
 * is returned.  This avoids a 2nd lookup to set the poolname
 * for pool-effective.
 *
 * returns:
 *
 *    NULL -   pools are disabled or if the 'cpus' property is set.
 *    cpupart of pool_default  - pools are enabled and the pool
 *             is not available or poolname is blank
 *    cpupart of named pool    - pools are enabled and the pool
 *             is available.
 */
cpupart_t *
mac_pset_find(mac_resource_props_t *mrp, boolean_t *use_default)
{
	pool_t		*pool = NULL;
	cpupart_t	*cpupart;

	*use_default = B_FALSE;

	/* CPUs property is set */
	if (mrp->mrp_mask & MRP_CPUS)
		return (NULL);

	ASSERT(pool_lock_held());

	/* Pools are disabled, no pset */
	if (pool_state == POOL_DISABLED)
		return (NULL);

	/* Pools property is set */
	if (mrp->mrp_mask & MRP_POOL) {
		if ((pool = pool_lookup_pool_by_name(mrp->mrp_pool)) == NULL) {
			/* Pool not found */
			DTRACE_PROBE1(mac_pset_find_no_pool, char *,
			    mrp->mrp_pool);
		}
	/* Pools property is not set */
	}

	if (pool == NULL) {
		*use_default = B_TRUE;
		pool = pool_default;
	}

	/* Find the CPU pset that corresponds to the pool */
	mutex_enter(&cpu_lock);
	if ((cpupart = cpupart_find(pool->pool_pset->pset_id)) == NULL) {
		DTRACE_PROBE1(mac_find_pset_no_pset, psetid_t,
		    pool->pool_pset->pset_id);
	}
	mutex_exit(&cpu_lock);

	return (cpupart);
}

void
mac_set_pool_effective(boolean_t use_default, cpupart_t *cpupart,
    mac_resource_props_t *mrp, mac_resource_props_t *emrp)
{
	ASSERT(pool_lock_held());

	if (cpupart != NULL) {
		emrp->mrp_mask |= MRP_POOL;
		if (use_default) {
			(void) strcpy(emrp->mrp_pool,
			    "pool_default");
		} else {
			ASSERT(strlen(mrp->mrp_pool) != 0);
			(void) strcpy(emrp->mrp_pool,
			    mrp->mrp_pool);
		}
	} else {
		emrp->mrp_mask &= ~MRP_POOL;
		bzero(emrp->mrp_pool, MAXPATHLEN);
	}
}

struct mac_pool_arg {
	char		mpa_poolname[MAXPATHLEN];
	pool_event_t	mpa_what;
};

/*
 * Update the cpu pool information for each mac_client of this
 * mac_impl_t.
 */
static void
mac_pool_link_update(mac_impl_t *mip, void *arg)
{
	struct mac_pool_arg	*mpa = arg;
	mac_client_impl_t	*mcip;
	mac_resource_props_t	*mrp, *emrp;
	boolean_t		pool_update = B_FALSE;
	boolean_t		pool_clear = B_FALSE;

	mrp = kmem_zalloc(sizeof (*mrp), KM_SLEEP);
	i_mac_perim_enter(mip);
	for (mcip = mip->mi_clients_list; mcip != NULL;
	    mcip = mcip->mci_client_next) {
		pool_update = B_FALSE;
		pool_clear = B_FALSE;
		mac_client_get_resources((mac_client_handle_t)mcip, mrp);
		emrp = MCIP_EFFECTIVE_PROPS(mcip);

		/*
		 * When pools are enabled
		 */
		if ((mpa->mpa_what == POOL_E_ENABLE) &&
		    ((mrp->mrp_mask & MRP_CPUS) == 0)) {
			mrp->mrp_mask |= MRP_POOL;
			pool_update = B_TRUE;
		}

		/*
		 * When pools are disabled
		 */
		if ((mpa->mpa_what == POOL_E_DISABLE) &&
		    ((mrp->mrp_mask & MRP_CPUS) == 0)) {
			mrp->mrp_mask |= MRP_POOL;
			pool_clear = B_TRUE;
		}

		/*
		 * Look for links with the pool property set and the poolname
		 * matching the one which is changing.
		 */
		if (strcmp(mrp->mrp_pool, mpa->mpa_poolname) == 0) {
			/*
			 * The pool associated with the link has changed.
			 */
			if (mpa->mpa_what == POOL_E_CHANGE) {
				mrp->mrp_mask |= MRP_POOL;
				pool_update = B_TRUE;
			}
		}

		/*
		 * This link is associated with pool_default and
		 * pool_default has changed.
		 */
		if ((mpa->mpa_what == POOL_E_CHANGE) &&
		    (strcmp(emrp->mrp_pool, "pool_default") == 0) &&
		    (strcmp(mpa->mpa_poolname, "pool_default") == 0)) {
			mrp->mrp_mask |= MRP_POOL;
			pool_update = B_TRUE;
		}

		/*
		 * Get new list of cpus for the pool, bind network
		 * threads to new list of cpus and update resources.
		 */
		if (pool_update) {
			if (MCIP_DATAPATH_SETUP(mcip)) {
				mac_cpu_pool_setup(mcip);
			}
			mac_update_resources(mrp, MCIP_RESOURCE_PROPS(mcip),
			    B_FALSE);
		}

		/*
		 * Clear the effective pool and bind network threads
		 * to any available CPU.
		 */
		if (pool_clear) {
			if (MCIP_DATAPATH_SETUP(mcip)) {
				emrp->mrp_mask &= ~MRP_POOL;
				bzero(emrp->mrp_pool, MAXPATHLEN);
				mac_cpu_setup(mcip);
			}
			mac_update_resources(mrp, MCIP_RESOURCE_PROPS(mcip),
			    B_FALSE);
		}
	}
	i_mac_perim_exit(mip);
	kmem_free(mrp, sizeof (*mrp));
}


static void
mac_pool_update(void *arg)
{
	struct mac_impl_list	*mlp;
	int			i;

	rw_enter(&i_mac_impl_lock, RW_WRITER);
	mlp = i_mac_impl_list_create();
	rw_exit(&i_mac_impl_lock);

	/* Make pool changes for each mac_impl */
	for (i = 0; i < mlp->mil_idx; i++)
		mac_pool_link_update(mlp->mil_mac[i], arg);

	i_mac_impl_list_destroy(mlp);

	kmem_free(arg, sizeof (struct mac_pool_arg));
}

/*
 * Callback function to be executed when a noteworthy pool event
 * takes place.
 */
/* ARGSUSED */
static void
mac_pool_event_cb(pool_event_t what, poolid_t id, void *arg)
{
	pool_t			*pool;
	char			*poolname = NULL;
	struct mac_pool_arg	*mpa;

	pool_lock();
	mpa = kmem_zalloc(sizeof (struct mac_pool_arg), KM_SLEEP);

	switch (what) {
	case POOL_E_ENABLE:
	case POOL_E_DISABLE:
		break;

	case POOL_E_CHANGE:
		pool = pool_lookup_pool_by_id(id);
		if (pool == NULL) {
			kmem_free(mpa, sizeof (struct mac_pool_arg));
			pool_unlock();
			return;
		}
		pool_get_name(pool, &poolname);
		(void) strlcpy(mpa->mpa_poolname, poolname,
		    sizeof (mpa->mpa_poolname));
		break;

	default:
		kmem_free(mpa, sizeof (struct mac_pool_arg));
		pool_unlock();
		return;
	}
	pool_unlock();

	mpa->mpa_what = what;

	mac_pool_update(mpa);
}

/*
 * Set effective rings property. This could be called from datapath_setup/
 * datapath_teardown or set-linkprop.
 * If the group is reserved we just go ahead and set the effective rings.
 * Additionally, for TX this could mean the default  group has lost/gained
 * some rings, so if the default group is reserved, we need to adjust the
 * effective rings for the default group clients. For RX, if we are working
 * with the non-default group, we just need * to reset the effective props
 * for the default group clients.
 */
void
mac_set_rings_effective(mac_client_impl_t *mcip)
{
	mac_impl_t		*mip = mcip->mci_mip;
	mac_group_t		*grp;
	mac_group_t		*defgrp;
	flow_entry_t		*flent = mcip->mci_flent;
	mac_resource_props_t	*emrp = MCIP_EFFECTIVE_PROPS(mcip);
	mac_grp_client_t	*mgcp;
	mac_client_impl_t	*gmcip;

	grp = flent->fe_rx_ring_group;
	if (grp != NULL) {
		defgrp = MAC_DEFAULT_RX_GROUP(mip);
		/*
		 * If we have reserved a group, set the effective rings
		 * to the ring count in the group.
		 */
		if (grp->mrg_state == MAC_GROUP_STATE_RESERVED) {
			emrp->mrp_mask |= MRP_RX_RINGS;
			emrp->mrp_nrxrings = grp->mrg_cur_count;
		}

		/*
		 * We go through the clients in the shared group and
		 * reset the effective properties. It is possible this
		 * might have already been done for some client (i.e.
		 * if some client is being moved to a group that is
		 * already shared). The case where the default group is
		 * RESERVED is taken care of above (note in the RX side if
		 * there is a non-default group, the default group is always
		 * SHARED).
		 */
		if (grp != defgrp || grp->mrg_state == MAC_GROUP_STATE_SHARED) {
			if (grp->mrg_state == MAC_GROUP_STATE_SHARED)
				mgcp = grp->mrg_clients;
			else
				mgcp = defgrp->mrg_clients;
			while (mgcp != NULL) {
				gmcip = mgcp->mgc_client;
				emrp = MCIP_EFFECTIVE_PROPS(gmcip);
				if (emrp->mrp_mask & MRP_RX_RINGS) {
					emrp->mrp_mask &= ~MRP_RX_RINGS;
					emrp->mrp_nrxrings = 0;
				}
				mgcp = mgcp->mgc_next;
			}
		}
	}

	/* Now the TX side */
	grp = flent->fe_tx_ring_group;
	if (grp != NULL) {
		defgrp = MAC_DEFAULT_TX_GROUP(mip);

		if (grp->mrg_state == MAC_GROUP_STATE_RESERVED) {
			emrp->mrp_mask |= MRP_TX_RINGS;
			emrp->mrp_ntxrings = grp->mrg_cur_count;
		} else if (grp->mrg_state == MAC_GROUP_STATE_SHARED) {
			mgcp = grp->mrg_clients;
			while (mgcp != NULL) {
				gmcip = mgcp->mgc_client;
				emrp = MCIP_EFFECTIVE_PROPS(gmcip);
				if (emrp->mrp_mask & MRP_TX_RINGS) {
					emrp->mrp_mask &= ~MRP_TX_RINGS;
					emrp->mrp_ntxrings = 0;
				}
				mgcp = mgcp->mgc_next;
			}
		}

		/*
		 * If the group is not the default group and the default
		 * group is reserved, the ring count in the default group
		 * might have changed, update it.
		 */
		if (grp != defgrp &&
		    defgrp->mrg_state == MAC_GROUP_STATE_RESERVED) {
			gmcip = MAC_GROUP_ONLY_CLIENT(defgrp);
			emrp = MCIP_EFFECTIVE_PROPS(gmcip);
			emrp->mrp_ntxrings = defgrp->mrg_cur_count;
		}
	}
	emrp = MCIP_EFFECTIVE_PROPS(mcip);
}

/*
 * Check if the primary is in the default group. If so, see if we
 * can give it a an exclusive group now that another client is
 * being configured. We take the primary out of the default group
 * because the multicast/broadcast packets for the all the clients
 * will land in the default ring in the default group which means
 * any client in the default group, even if it is the only on in
 * the group, will lose exclusive access to the rings, hence
 * polling.
 */
mac_client_impl_t *
mac_check_primary_relocation(mac_client_impl_t *mcip, boolean_t rxhw)
{
	mac_impl_t		*mip = mcip->mci_mip;
	mac_group_t		*defgrp = MAC_DEFAULT_RX_GROUP(mip);
	flow_entry_t		*flent = mcip->mci_flent;
	mac_resource_props_t	*mrp = MCIP_RESOURCE_PROPS(mcip);
	uint8_t			*mac_addr;
	mac_group_t		*ngrp;

	/*
	 * Check if the primary is in the default group, if not
	 * or if it is explicitly configured to be in the default
	 * group OR set the RX rings property, return.
	 */
	if (flent->fe_rx_ring_group != defgrp || mrp->mrp_mask & MRP_RX_RINGS)
		return (NULL);

	/*
	 * If the new client needs an exclusive group and we
	 * don't have another for the primary, return.
	 */
	if (rxhw && mip->mi_rxhwclnt_avail < 2)
		return (NULL);

	mac_addr = flent->fe_flow_desc.fd_dst_mac;
	/*
	 * We call this when we are setting up the datapath for
	 * the first non-primary.
	 */
	ASSERT(mip->mi_nactiveclients == 2);
	/*
	 * OK, now we have the primary that needs to be relocated.
	 */
	ngrp =  mac_reserve_rx_group(mcip, mac_addr, B_TRUE);
	if (ngrp == NULL)
		return (NULL);
	if (mac_rx_switch_group(mcip, defgrp, ngrp) != 0) {
		mac_release_rx_group(mcip, ngrp);
		return (NULL);
	}
	return (mcip);
}

/*
 * Make a upcall to get the vanity name for a link.
 */
int
mac_get_linkid2name(datalink_id_t linkid, char *linkname)
{
	return (dls_mgmt_get_linkinfo(linkid, linkname, NULL, NULL, NULL));
}

/*
 * Rename the mi_linkname field. Currently it is being used exclusively
 * for observability and not in any control operations.
 */
void
mac_rename_linkname(mac_handle_t mh, const char *newname)
{
	mac_impl_t *mip = (mac_impl_t *)mh;

	i_mac_perim_enter(mip);
	(void) strlcpy(mip->mi_linkname, newname, sizeof (mip->mi_linkname));
	i_mac_perim_exit(mip);
}
