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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/conf.h>
#include <sys/id_space.h>
#include <sys/esunddi.h>
#include <sys/stat.h>
#include <sys/mkdev.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/dlpi.h>
#include <sys/modhash.h>
#include <sys/mac.h>
#include <sys/mac_provider.h>
#include <sys/mac_impl.h>
#include <sys/mac_client_impl.h>
#include <sys/mac_client_priv.h>
#include <sys/mac_cpu_impl.h>
#include <sys/mac_stat.h>
#include <sys/dld.h>
#include <sys/modctl.h>
#include <sys/fs/dv_node.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/callb.h>
#include <sys/cpuvar.h>
#include <sys/atomic.h>
#include <sys/sdt.h>
#include <sys/mac_flow.h>
#include <sys/ddi_intr_impl.h>
#include <sys/disp.h>
#include <sys/sdt.h>
#include <sys/pattr.h>
#include <sys/strsun.h>
#include <sys/fm/io/ddi.h>

/*
 * MAC Provider Interface.
 *
 * Interface for GLDv3 compatible NIC drivers.
 */
static void i_mac_notify_thread(void *);

typedef void (*mac_notify_default_cb_fn_t)(mac_impl_t *);

static const mac_notify_default_cb_fn_t mac_notify_cb_list[MAC_NNOTE] = {
	mac_rx_fanout_recompute,	/* MAC_NOTE_LINK */
	NULL,				/* MAC_NOTE_UNICST */
	NULL,				/* MAC_NOTE_TX */
	NULL,				/* MAC_NOTE_DEVPROMISC */
	NULL,				/* MAC_NOTE_FASTPATH_FLUSH */
	NULL,				/* MAC_NOTE_SDU_SIZE */
	NULL,				/* MAC_NOTE_MARGIN */
	NULL,				/* MAC_NOTE_CAPAB_CHG */
	NULL				/* MAC_NOTE_LOWLINK */
};

/*
 * Driver support functions.
 */

/* REGISTRATION */

mac_register_t *
mac_alloc(uint_t mac_version)
{
	mac_register_t *mregp;

	/*
	 * Make sure there isn't a version mismatch between the driver and
	 * the framework.  In the future, if multiple versions are
	 * supported, this check could become more sophisticated.
	 */
	if (mac_version != MAC_VERSION)
		return (NULL);

	mregp = kmem_zalloc(sizeof (mac_register_t), KM_SLEEP);
	mregp->m_version = mac_version;
	return (mregp);
}

void
mac_free(mac_register_t *mregp)
{
	kmem_free(mregp, sizeof (mac_register_t));
}

/*
 * mac_register() is how drivers register new MACs with the GLDv3
 * framework.  The mregp argument is allocated by drivers using the
 * mac_alloc() function, and can be freed using mac_free() immediately upon
 * return from mac_register().  Upon success (0 return value), the mhp
 * opaque pointer becomes the driver's handle to its MAC interface, and is
 * the argument to all other mac module entry points.
 */
int
mac_register(mac_register_t *mregp, mac_handle_t *mhp)
{
	return (mac_register_zone(mregp, mhp, GLOBAL_ZONEID));
}

/* ARGSUSED */
int
mac_register_zone(mac_register_t *mregp, mac_handle_t *mhp, zoneid_t zoneid)
{
	mac_impl_t		*mip;
	mactype_t		*mtype;
	int			err = EINVAL;
	int			ret;
	struct devnames		*dnp = NULL;
	uint_t			instance;
	boolean_t		style1_created = B_FALSE;
	boolean_t		style2_created = B_FALSE;
	char			*driver;
	minor_t			minor = 0;
	mac_capab_dcb_t		dcbinfo;
	mac_capab_rings_t	*cap_rings;
	mac_capab_linkid_t	cap_lid;

	/* A successful call to mac_init_ops() sets the DN_GLDV3_DRIVER flag. */
	if (!GLDV3_DRV(ddi_driver_major(mregp->m_dip))) {
		cmn_err(CE_WARN, "MAC registration failed: not a GLDV3 driver");
		return (err);
	}

	/* Find the required MAC-Type plugin. */
	if ((mtype = mactype_getplugin(mregp->m_type_ident)) == NULL) {
		cmn_err(CE_WARN, "MAC registration failed: Unknown plugin "
		    "type");
		return (err);
	}

	/* Create a mac_impl_t to represent this MAC. */
	mip = kmem_cache_alloc(i_mac_impl_cachep, KM_SLEEP);

	list_create(&mip->mi_bm_rx_block_list, sizeof (mac_block_t),
	    offsetof(mac_block_t, mbm_list));
	list_create(&mip->mi_bm_tx_block_list, sizeof (mac_block_t),
	    offsetof(mac_block_t, mbm_list));

	/*
	 * Set the initial memory reference cnt.
	 */
	mip->mi_mem_ref_cnt = 1;

	/*
	 * The mac is not ready for open yet.
	 */
	mip->mi_state_flags |= MIS_DISABLED;

	/*
	 * Check the interface's m_flags.
	 */
	if (mregp->m_flags & MAC_FLAGS_PROMISCUOUS_MULTICAST)
		mip->mi_state_flags |= MIS_MULTICAST_ONLY;

	/*
	 * When a mac is registered, the m_instance field can be set to:
	 *
	 *  0:	Get the mac's instance number from m_dip.
	 *	This is usually used for physical device dips.
	 *
	 *  [1 .. MAC_MAX_MINOR-1]: Use the value as the mac's instance number.
	 *	For example, when an aggregation is created with the key option,
	 *	"key" will be used as the instance number.
	 *
	 *  -1: Assign an instance number from [MAC_MAX_MINOR .. MAXMIN-1].
	 *	This is often used when a MAC of a virtual link is registered
	 *	(e.g., aggregation when "key" is not specified, or vnic).
	 *
	 * Note that the instance number is used to derive the mi_minor field
	 * of mac_impl_t, which will then be used to derive the name of kstats
	 * and the devfs nodes.  The first 2 cases are needed to preserve
	 * backward compatibility.
	 */
	switch (mregp->m_instance) {
	case 0:
		instance = ddi_get_instance(mregp->m_dip);
		break;
	case ((uint_t)-1):
		minor = mac_minor_hold(B_TRUE);
		if (minor == 0) {
			err = ENOSPC;
			cmn_err(CE_WARN, "MAC registration failed: Ran out of "
			    "instance number to assign to MAC");
			goto fail;
		}
		instance = minor - 1;
		break;
	default:
		instance = mregp->m_instance;
		if (instance >= MAC_MAX_MINOR) {
			cmn_err(CE_WARN, "MAC registration failed: MAC's "
			    "instance number is >= max (%d)", MAC_MAX_MINOR);
			goto fail;
		}
		break;
	}

	mip->mi_minor = (minor_t)(instance + 1);
	mip->mi_dip = mregp->m_dip;
	mip->mi_clients_list = NULL;
	mip->mi_nclients = 0;
	mip->mi_zoneid = zoneid;

	/* Set the default IEEE Port VLAN Identifier */
	mip->mi_pvid = 1;

	/* Default bridge link learning protection values */
	mip->mi_llimit = 1000;
	mip->mi_ldecay = 200;

	driver = (char *)ddi_driver_name(mip->mi_dip);

	/* Construct the MAC name as <drvname><instance> */
	(void) snprintf(mip->mi_name, sizeof (mip->mi_name), "%s%d",
	    driver, instance);

	mip->mi_driver = mregp->m_driver;

	mip->mi_type = mtype;
	mip->mi_margin = mregp->m_margin;
	mip->mi_info.mi_media = mtype->mt_type;
	mip->mi_info.mi_nativemedia = mtype->mt_nativetype;
	if (mregp->m_max_sdu <= mregp->m_min_sdu) {
		cmn_err(CE_WARN, "MAC registration failed: max_sdu (%d) <= "
		    "min_sdu (%d)", mregp->m_max_sdu, mregp->m_min_sdu);
		goto fail;
	}
	if (mregp->m_multicast_sdu == 0)
		mregp->m_multicast_sdu = mregp->m_max_sdu;
	if (mregp->m_multicast_sdu < mregp->m_min_sdu ||
	    mregp->m_multicast_sdu > mregp->m_max_sdu) {
		cmn_err(CE_WARN, "MAC registration failed: multicast_sdu (%d) "
		    "is < min_sdu (%d) or > max_sdu (%d)",
		    mregp->m_multicast_sdu, mregp->m_min_sdu,
		    mregp->m_max_sdu);
		goto fail;
	}
	mip->mi_sdu_min = mregp->m_min_sdu;
	mip->mi_sdu_max = mregp->m_max_sdu;
	mip->mi_sdu_multicast = mregp->m_multicast_sdu;
	mip->mi_info.mi_addr_length = mip->mi_type->mt_addr_length;
	/*
	 * If the media supports a broadcast address, cache a pointer to it
	 * in the mac_info_t so that upper layers can use it.
	 */
	mip->mi_info.mi_brdcst_addr = mip->mi_type->mt_brdcst_addr;
	mip->mi_v12n_level = 0;

	/*
	 * Copy the unicast source address into the mac_info_t, but only if
	 * the MAC-Type defines a non-zero address length.  We need to
	 * handle MAC-Types that have an address length of 0
	 * (point-to-point protocol MACs for example).
	 */
	if (mip->mi_type->mt_addr_length > 0) {
		if (mregp->m_src_addr == NULL) {
			cmn_err(CE_WARN, "MAC registration failed: Unicast "
			    "address not specified even though length is "
			    "non-zero");
			goto fail;
		}
		mip->mi_info.mi_unicst_addr =
		    kmem_alloc(mip->mi_type->mt_addr_length, KM_SLEEP);
		bcopy(mregp->m_src_addr, mip->mi_info.mi_unicst_addr,
		    mip->mi_type->mt_addr_length);

		/*
		 * Copy the fixed 'factory' MAC address from the immutable
		 * info.  This is taken to be the MAC address currently in
		 * use. Also, initialize the "default" MAC address. For
		 * most MAC types, this will be identical to the immutable
		 * address. But some types (e.g., aggr) allow the default
		 * to be modified after MAC registration.
		 */
		bcopy(mip->mi_info.mi_unicst_addr, mip->mi_addr,
		    mip->mi_type->mt_addr_length);
		bcopy(mip->mi_info.mi_unicst_addr, mip->mi_defaddr,
		    mip->mi_type->mt_addr_length);

		/*
		 * At this point, we should set up the classification
		 * rules etc but we delay it till mac_open() so that
		 * the resource discovery has taken place and we
		 * know someone wants to use the device. Otherwise
		 * memory gets allocated for Rx ring structures even
		 * during probe.
		 */

		/* Copy the destination address if one is provided. */
		if (mregp->m_dst_addr != NULL) {
			bcopy(mregp->m_dst_addr, mip->mi_dstaddr,
			    mip->mi_type->mt_addr_length);
			mip->mi_dstaddr_set = B_TRUE;
		}
	} else if (mregp->m_src_addr != NULL) {
		cmn_err(CE_WARN, "MAC registration failed: Unicast address "
		    "provided but address length zero");
		goto fail;
	}

	/*
	 * The format of the m_pdata is specific to the plugin.  It is
	 * passed in as an argument to all of the plugin callbacks.  The
	 * driver can update this information by calling
	 * mac_pdata_update().
	 */
	if (mip->mi_type->mt_ops.mtops_ops & MTOPS_PDATA_VERIFY) {
		/*
		 * Verify if the supplied plugin data is valid.  Note that
		 * even if the caller passed in a NULL pointer as plugin data,
		 * we still need to verify if that's valid as the plugin may
		 * require plugin data to function.
		 */
		if (!mip->mi_type->mt_ops.mtops_pdata_verify(mregp->m_pdata,
		    mregp->m_pdata_size)) {
			cmn_err(CE_WARN, "MAC registration failed: Invalid "
			    "plugin data specified");
			goto fail;
		}
		if (mregp->m_pdata != NULL) {
			mip->mi_pdata =
			    kmem_alloc(mregp->m_pdata_size, KM_SLEEP);
			bcopy(mregp->m_pdata, mip->mi_pdata,
			    mregp->m_pdata_size);
			mip->mi_pdata_size = mregp->m_pdata_size;
		}
	} else if (mregp->m_pdata != NULL) {
		/*
		 * The caller supplied non-NULL plugin data, but the plugin
		 * does not recognize plugin data.
		 */
		cmn_err(CE_WARN, "MAC registration failed: Non-NULL plugin "
		    "data specified when NULL plugin data was expected");
		goto fail;
	}

	/*
	 * Register the private properties.
	 */
	mac_register_priv_prop(mip, mregp->m_priv_props);

	/*
	 * Stash the driver callbacks into the mac_impl_t, but first sanity
	 * check to make sure all mandatory callbacks are set.
	 */
	if (mregp->m_callbacks->mc_getstat == NULL ||
	    mregp->m_callbacks->mc_start == NULL ||
	    mregp->m_callbacks->mc_stop == NULL ||
	    mregp->m_callbacks->mc_multicst == NULL) {
		cmn_err(CE_WARN, "MAC registration failed: All or one of the "
		    "mandatory driver callbacks not set");
		goto fail;
	}
	mip->mi_callbacks = mregp->m_callbacks;
	mip->mi_rx = mac_rx_common;

	/*
	 * Populate mi_linkname with vanity name if pseudo driver,
	 * with m_instance -1, supports linkid capability. Currently some
	 * pseudo drivers have this private capability.
	 */
	bzero(&cap_lid, sizeof (mac_capab_linkid_t));
	if (mregp->m_instance == ((uint_t)-1) &&
	    i_mac_capab_get((mac_handle_t)mip, MAC_CAPAB_LINKID, &cap_lid) &&
	    cap_lid.mpl_linkid != DATALINK_INVALID_LINKID) {
		(void) mac_get_linkid2name(cap_lid.mpl_linkid,
		    mip->mi_linkname);
	} else {
		(void) strlcpy(mip->mi_linkname, mip->mi_name,
		    sizeof (mip->mi_linkname));
	}

	if (mac_capab_get((mac_handle_t)mip, MAC_CAPAB_LEGACY,
	    &mip->mi_capab_legacy)) {
		mip->mi_state_flags |= MIS_LEGACY;
		mip->mi_phy_dev = mip->mi_capab_legacy.ml_dev;
	} else {
		mip->mi_phy_dev = makedevice(ddi_driver_major(mip->mi_dip),
		    mip->mi_minor);
	}

	/*
	 * Allocate a notification thread. thread_create blocks for memory
	 * if needed, it never fails.
	 */
	mip->mi_notify_thread = thread_create(NULL, 0, i_mac_notify_thread,
	    mip, 0, &p0, TS_RUN, minclsyspri);

	/*
	 * Initialize the capabilities
	 */
	bzero(&mip->mi_rx_rings_cap, sizeof (mac_capab_rings_t));
	bzero(&mip->mi_tx_rings_cap, sizeof (mac_capab_rings_t));

	if (i_mac_capab_get((mac_handle_t)mip, MAC_CAPAB_VNIC, NULL))
		mip->mi_state_flags |= MIS_IS_VNIC;

	if (i_mac_capab_get((mac_handle_t)mip, MAC_CAPAB_AGGR, NULL))
		mip->mi_state_flags |= MIS_IS_AGGR;

	mac_addr_factory_init(mip);

	bzero(&dcbinfo, sizeof (mac_capab_dcb_t));
	(void) i_mac_capab_get((mac_handle_t)mip, MAC_CAPAB_DCB, &dcbinfo);
	if (dcbinfo.mcd_flags != 0) {
		mip->mi_dcb_flags = dcbinfo.mcd_flags;
		mip->mi_ntcs =  (uint8_t)dcbinfo.mcd_ntc;
		if (mip->mi_ntcs == 0 || mip->mi_ntcs > MAX_DCB_NTCS) {
			cmn_err(CE_WARN, "MAC registration failed: "
			    "DCB mode, Invalid number of Traffic Classes %d",
			    mip->mi_ntcs);
			goto fail;
		}
	}

	/*
	 * Check to see if the device implements rings and groups.
	 */
	i_mac_perim_enter(mip);

	/* Check if the device supports RX rings */
	cap_rings = &mip->mi_rx_rings_cap;
	cap_rings->mr_type = MAC_RING_TYPE_RX;
	if (i_mac_capab_get((mac_handle_t)mip, MAC_CAPAB_RINGS, cap_rings)) {
		/* Initialize the RX rings and groups */
		if ((ret = mac_init_rings(mip, cap_rings)) != 0) {
			err = ret;
			i_mac_perim_exit(mip);
			cmn_err(CE_WARN, "MAC registration failed: RX rings "
			    "initialization failed (error %d)", err);
			goto fail;
		}

		/*
		 * Initialize TX rings.
		 *
		 * NOTE: All drivers implementing rings capability should
		 * implement for both RX and TX.  For now, be sure
		 * RX is implemented.
		 */
		cap_rings = &mip->mi_tx_rings_cap;
		cap_rings->mr_type = MAC_RING_TYPE_TX;
		if (i_mac_capab_get((mac_handle_t)mip, MAC_CAPAB_RINGS,
		    cap_rings)) {
			/* Initialize the TX rings and groups */
			if ((ret = mac_init_rings(mip, cap_rings)) != 0) {
				err = ret;
				i_mac_perim_exit(mip);
				cmn_err(CE_WARN, "MAC registration failed: TX "
				    "rings initialization failed (error %d)",
				    err);
				goto fail;
			}
		} else if (mip->mi_dcb_flags != 0) {
			i_mac_perim_exit(mip);
			cmn_err(CE_WARN, "MAC registration failed: "
			    "DCB mode, TX rings must be supported");
			goto fail;
		}

		if (mip->mi_dcb_flags != 0 &&
		    mip->mi_tx_rings_cap.mr_ggetringtc == NULL) {
			i_mac_perim_exit(mip);
			cmn_err(CE_WARN, "MAC registration failed: "
			    "DCB mode, querying  TX ring's Traffic Class "
			    "must be supported");
			goto fail;
		}

		/*
		 * Set the virtualization level.
		 */
		mip->mi_v12n_level |= MI_VIRT_LEVEL1;

		i_mac_perim_exit(mip);

		/*
		 * The driver needs to register at least rx rings for this
		 * virtualization level.
		 */
		ASSERT(mip->mi_rx_groups != NULL);
		if (mip->mi_rx_groups == NULL) {
			cmn_err(CE_WARN, "MAC registration failed: For "
			    "virtualization level MI_VIRT_LEVEL1 at least RX "
			    "rings must be registered");
			goto fail;
		}

		/*
		 * Are we HIO capable?
		 */
		mip->mi_share_capab.ms_snum = 0;
		if (mac_capab_get((mac_handle_t)mip, MAC_CAPAB_SHARES,
		    &mip->mi_share_capab) == 0)
			mip->mi_v12n_level |= MI_VIRT_HIO;
	} else if (mip->mi_dcb_flags != 0) {
		i_mac_perim_exit(mip);
		cmn_err(CE_WARN, "MAC registration failed:  "
		    "DCB mode, TX rings must be supported");
		goto fail;
	} else {
		i_mac_perim_exit(mip);
	}
	if (mip->mi_tx != NULL)
		mac_init_fake_tx_ring(mip);

	/*
	 * The driver must set mc_unicst entry point to NULL when
	 * it advertises CAP_RINGS for rx groups.
	 */
	if (mip->mi_rx_groups != NULL) {
		ASSERT(mregp->m_callbacks->mc_unicst == NULL);
		if (mregp->m_callbacks->mc_unicst != NULL) {
			cmn_err(CE_WARN, "MAC registration failed: Cannot "
			    "register unicast (mc_unicst) callback if RX rings "
			    "are also supported");
			goto fail;
		}
	} else {
		ASSERT(mregp->m_callbacks->mc_unicst != NULL);
		if (mregp->m_callbacks->mc_unicst == NULL) {
			cmn_err(CE_WARN, "MAC registration failed: Must "
			    "register unicast (mc_unicst) callback if RX rings "
			    "are not supported");
			goto fail;
		}
	}

	/*
	 * Initialize MAC addresses. Must be called after mac_init_rings().
	 */
	mac_init_macaddr(mip);

	/*
	 * Initialize the kstats for this device.
	 */
	mac_driver_stat_create(mip);

	/* Zero out any properties. */
	bzero(&mip->mi_resource_props, sizeof (mac_resource_props_t));

	if (mip->mi_minor <= MAC_MAX_MINOR) {
		/* Create a style-2 DLPI device */
		if (ddi_create_minor_node(mip->mi_dip, driver, S_IFCHR, 0,
		    DDI_NT_NET, CLONE_DEV) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "MAC registration failed: Failed to "
			    "create style-2 DLPI device");
			goto fail;
		}
		style2_created = B_TRUE;

		/* Create a style-1 DLPI device */
		if (ddi_create_minor_node(mip->mi_dip, mip->mi_name, S_IFCHR,
		    mip->mi_minor, DDI_NT_NET, 0) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "MAC registration failed: Failed to "
			    "create style-1 DLPI device");
			goto fail;
		}
		style1_created = B_TRUE;
	}

	mac_flow_l2tab_create(mip, &mip->mi_flow_tab);

	rw_enter(&i_mac_impl_lock, RW_WRITER);
	if (mod_hash_insert(i_mac_impl_hash,
	    (mod_hash_key_t)mip->mi_name, (mod_hash_val_t)mip) != 0) {
		rw_exit(&i_mac_impl_lock);
		err = EEXIST;
		cmn_err(CE_WARN, "MAC registration failed: MAC already "
		    "registered");
		goto fail;
	}

	DTRACE_PROBE2(mac__register, struct devnames *, dnp,
	    (mac_impl_t *), mip);

	/*
	 * Mark the MAC to be ready for open.
	 */
	mip->mi_state_flags &= ~MIS_DISABLED;
	rw_exit(&i_mac_impl_lock);

	atomic_inc_32(&i_mac_impl_count);

	cmn_err(CE_NOTE, "!%s registered", mip->mi_linkname);
	*mhp = (mac_handle_t)mip;
	return (0);

fail:
	if (style1_created)
		ddi_remove_minor_node(mip->mi_dip, mip->mi_name);

	if (style2_created)
		ddi_remove_minor_node(mip->mi_dip, driver);

	mac_addr_factory_fini(mip);

	/* Clean up registered MAC addresses */
	mac_fini_macaddr(mip);

	/* Clean up registered rings */
	mac_free_rings(mip, MAC_RING_TYPE_RX);
	mac_free_rings(mip, MAC_RING_TYPE_TX);

	if (mip->mi_fake_tx_ring.mr_mip != NULL)
		mac_fini_fake_tx_ring(mip);

	/* Clean up notification thread */
	if (mip->mi_notify_thread != NULL)
		i_mac_notify_exit(mip);

	if (mip->mi_info.mi_unicst_addr != NULL) {
		kmem_free(mip->mi_info.mi_unicst_addr,
		    mip->mi_type->mt_addr_length);
		mip->mi_info.mi_unicst_addr = NULL;
	}

	mac_driver_stat_delete(mip);

	if (mip->mi_type != NULL) {
		atomic_dec_32(&mip->mi_type->mt_ref);
		mip->mi_type = NULL;
	}

	if (mip->mi_pdata != NULL) {
		kmem_free(mip->mi_pdata, mip->mi_pdata_size);
		mip->mi_pdata = NULL;
		mip->mi_pdata_size = 0;
	}

	if (minor != 0) {
		ASSERT(minor > MAC_MAX_MINOR);
		mac_minor_rele(minor);
	}

	bzero(mip->mi_addr, MAXMACADDRLEN);
	bzero(mip->mi_defaddr, MAXMACADDRLEN);
	bzero(mip->mi_dstaddr, MAXMACADDRLEN);
	mip->mi_dstaddr_set = B_FALSE;

	/*
	 * Clear the state before destroying the mac_impl_t
	 */
	mip->mi_state_flags = 0;
	mip->mi_mem_ref_cnt--;

	mac_unregister_priv_prop(mip);

	kmem_cache_free(i_mac_impl_cachep, mip);
	return (err);
}

/*
 * Unregister from the GLDv3 framework
 */
int
mac_unregister(mac_handle_t mh)
{
	int			err;
	mac_impl_t		*mip = (mac_impl_t *)mh;
	mod_hash_val_t		val;
	mac_margin_req_t	*mmr, *nextmmr;
	boolean_t		dofree = B_TRUE;

	/* Fail the unregister if there are any open references to this mac. */
	if ((err = mac_disable_nowait(mh)) != 0)
		return (err);

	/*
	 * The pools were offlined at mac_stop time.
	 */
	mac_bm_offline_pools_check(mip);

	/*
	 * Clean up notification thread and wait for it to exit.
	 */
	i_mac_notify_exit(mip);

	i_mac_perim_enter(mip);

	/*
	 * There is still resource properties configured over this mac.
	 */
	if (mip->mi_resource_props.mrp_mask != 0)
		mac_fastpath_enable((mac_handle_t)mip);

	if (mip->mi_minor < MAC_MAX_MINOR + 1) {
		ddi_remove_minor_node(mip->mi_dip, mip->mi_name);
		ddi_remove_minor_node(mip->mi_dip,
		    (char *)ddi_driver_name(mip->mi_dip));
	}

	ASSERT(mip->mi_nactiveclients == 0 && !(mip->mi_state_flags &
	    MIS_EXCLUSIVE));

	mac_driver_stat_delete(mip);

	(void) mod_hash_remove(i_mac_impl_hash,
	    (mod_hash_key_t)mip->mi_name, &val);
	ASSERT(mip == (mac_impl_t *)val);

	ASSERT(i_mac_impl_count > 0);
	atomic_dec_32(&i_mac_impl_count);

	if (mip->mi_pdata != NULL)
		kmem_free(mip->mi_pdata, mip->mi_pdata_size);
	mip->mi_pdata = NULL;
	mip->mi_pdata_size = 0;

	/*
	 * Free the list of margin request.
	 */
	for (mmr = mip->mi_mmrp; mmr != NULL; mmr = nextmmr) {
		nextmmr = mmr->mmr_nextp;
		kmem_free(mmr, sizeof (mac_margin_req_t));
	}
	mip->mi_mmrp = NULL;

	mip->mi_linkstate = mip->mi_lowlinkstate = LINK_STATE_UNKNOWN;
	kmem_free(mip->mi_info.mi_unicst_addr, mip->mi_type->mt_addr_length);
	mip->mi_info.mi_unicst_addr = NULL;

	atomic_dec_32(&mip->mi_type->mt_ref);
	mip->mi_type = NULL;

	/*
	 * Free the primary MAC address.
	 */
	mac_fini_macaddr(mip);

	/*
	 * free all rings
	 */
	mac_free_rings(mip, MAC_RING_TYPE_RX);
	mac_free_rings(mip, MAC_RING_TYPE_TX);

	if (mip->mi_fake_tx_ring.mr_mip != NULL)
		mac_fini_fake_tx_ring(mip);

	mac_addr_factory_fini(mip);

	bzero(mip->mi_addr, MAXMACADDRLEN);
	bzero(mip->mi_defaddr, MAXMACADDRLEN);
	bzero(mip->mi_dstaddr, MAXMACADDRLEN);
	mip->mi_dstaddr_set = B_FALSE;

	/* and the flows */
	mac_flow_tab_destroy(mip->mi_flow_tab, mip->mi_zoneid);
	mip->mi_flow_tab = NULL;

	if (mip->mi_minor > MAC_MAX_MINOR)
		mac_minor_rele(mip->mi_minor);

	cmn_err(CE_NOTE, "!%s unregistered", mip->mi_linkname);

	mac_unregister_priv_prop(mip);
	ASSERT(mip->mi_bridge_link == NULL);

	ASSERT(mip->mi_mem_ref_cnt != 0);
	mip->mi_mem_ref_cnt--;
	if (mip->mi_mem_ref_cnt != 0)
		dofree = B_FALSE;

	i_mac_perim_exit(mip);

	/*
	 * Free the mac_impl, if we are ready.
	 */
	if (dofree) {
		/*
		 * Reset to default values before kmem_cache_free
		 */
		mip->mi_state_flags = 0;
		mac_bm_reset_values(mip);
		kmem_cache_free(i_mac_impl_cachep, mip);
	}

	return (0);
}

/*
 * Special case function: this allows snooping of packets transmitted and
 * received by TRILL. By design, they go directly into the TRILL module.
 */
void
mac_trill_snoop(mac_handle_t mh, mblk_t *mp)
{
	mac_impl_t *mip = (mac_impl_t *)mh;

	if (mip->mi_promisc_list != NULL)
		mac_promisc_dispatch(mip, mp, NULL);
}

/* DATA TRANSMISSION */

/* LINK STATE */
/*
 * Notify the MAC layer about a link state change
 */
void
mac_link_update(mac_handle_t mh, link_state_t link)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;

	/*
	 * Save the link state.
	 */
	mip->mi_lowlinkstate = link;

	/*
	 * Send a MAC_NOTE_LOWLINK notification.  This tells the notification
	 * thread to deliver both lower and upper notifications.
	 */
	i_mac_notify(mip, MAC_NOTE_LOWLINK);
}

/*
 * Notify the MAC layer about a link state change due to bridging.
 */
void
mac_link_redo(mac_handle_t mh, link_state_t link)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;

	/*
	 * Save the link state.
	 */
	mip->mi_linkstate = link;

	/*
	 * Send a MAC_NOTE_LINK notification.  Only upper notifications are
	 * made.
	 */
	i_mac_notify(mip, MAC_NOTE_LINK);
}

/* MINOR NODE HANDLING */

/*
 * Given a dev_t, return the instance number (PPA) associated with it.
 * Drivers can use this in their getinfo(9e) implementation to lookup
 * the instance number (i.e. PPA) of the device, to use as an index to
 * their own array of soft state structures.
 *
 * Returns -1 on error.
 */
int
mac_devt_to_instance(dev_t devt)
{
	return (dld_devt_to_instance(devt));
}

/*
 * This function returns the first minor number that is available for
 * driver private use.  All minor numbers smaller than this are
 * reserved for GLDv3 use.
 */
minor_t
mac_private_minor(void)
{
	return (MAC_PRIVATE_MINOR);
}

/* OTHER CONTROL INFORMATION */

/*
 * A driver notified us that its primary MAC address has changed.
 */
void
mac_unicst_update(mac_handle_t mh, const uint8_t *addr)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;

	if (mip->mi_type->mt_addr_length == 0)
		return;

	i_mac_perim_enter(mip);

	/*
	 * If address changes, freshen the MAC address value and update
	 * all MAC clients that share this MAC address.
	 */
	if (bcmp(addr, mip->mi_addr, mip->mi_type->mt_addr_length) != 0) {
		mac_freshen_macaddr(mac_find_macaddr(mip, mip->mi_addr),
		    (uint8_t *)addr);
	}

	i_mac_perim_exit(mip);

	/*
	 * Send a MAC_NOTE_UNICST notification.
	 */
	i_mac_notify(mip, MAC_NOTE_UNICST);
}

void
mac_dst_update(mac_handle_t mh, const uint8_t *addr)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;

	if (mip->mi_type->mt_addr_length == 0)
		return;

	i_mac_perim_enter(mip);
	bcopy(addr, mip->mi_dstaddr, mip->mi_type->mt_addr_length);
	i_mac_perim_exit(mip);
	i_mac_notify(mip, MAC_NOTE_DEST);
}

/*
 * MAC plugin information changed.
 */
int
mac_pdata_update(mac_handle_t mh, void *mac_pdata, size_t dsize)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;

	/*
	 * Verify that the plugin supports MAC plugin data and that the
	 * supplied data is valid.
	 */
	if (!(mip->mi_type->mt_ops.mtops_ops & MTOPS_PDATA_VERIFY))
		return (EINVAL);
	if (!mip->mi_type->mt_ops.mtops_pdata_verify(mac_pdata, dsize))
		return (EINVAL);

	mutex_enter(&mip->mi_pdata_lock);

	if (mip->mi_pdata != NULL)
		kmem_free(mip->mi_pdata, mip->mi_pdata_size);

	mip->mi_pdata = kmem_alloc(dsize, KM_SLEEP);
	bcopy(mac_pdata, mip->mi_pdata, dsize);
	mip->mi_pdata_size = dsize;

	mutex_exit(&mip->mi_pdata_lock);
	/*
	 * Since the MAC plugin data is used to construct MAC headers that
	 * were cached in fast-path headers, we need to flush fast-path
	 * information for links associated with this mac.
	 */
	i_mac_notify(mip, MAC_NOTE_FASTPATH_FLUSH);
	return (0);
}

/*
 * Invoked by driver as well as the framework to notify its capability change.
 */
void
mac_capab_update(mac_handle_t mh)
{
	/* Send MAC_NOTE_CAPAB_CHG notification */
	i_mac_notify((mac_impl_t *)mh, MAC_NOTE_CAPAB_CHG);
}

/*
 * Used by normal drivers to update the max sdu size.
 * We need to handle the case of a smaller mi_sdu_multicast
 * since this is called by mac_set_mtu() even for drivers that
 * have differing unicast and multicast mtu and we don't want to
 * increase the multicast mtu by accident in that case.
 */
int
mac_maxsdu_update(mac_handle_t mh, uint_t sdu_max)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;

	if (sdu_max == 0 || sdu_max < mip->mi_sdu_min)
		return (EINVAL);
	mip->mi_sdu_max = sdu_max;
	if (mip->mi_sdu_multicast > mip->mi_sdu_max)
		mip->mi_sdu_multicast = mip->mi_sdu_max;

	/* Send a MAC_NOTE_SDU_SIZE notification. */
	i_mac_notify(mip, MAC_NOTE_SDU_SIZE);
	return (0);
}

/*
 * Version of the above function that is used by drivers that have a different
 * max sdu size for multicast/broadcast vs. unicast.
 */
int
mac_maxsdu_update2(mac_handle_t mh, uint_t sdu_max, uint_t sdu_multicast)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;

	if (sdu_max == 0 || sdu_max < mip->mi_sdu_min)
		return (EINVAL);
	if (sdu_multicast == 0)
		sdu_multicast = sdu_max;
	if (sdu_multicast > sdu_max || sdu_multicast < mip->mi_sdu_min)
		return (EINVAL);
	mip->mi_sdu_max = sdu_max;
	mip->mi_sdu_multicast = sdu_multicast;

	/* Send a MAC_NOTE_SDU_SIZE notification. */
	i_mac_notify(mip, MAC_NOTE_SDU_SIZE);
	return (0);
}

void
mac_ring_intr_modify(mac_ring_handle_t mrh,
    ddi_intr_handle_t ddh, boolean_t shared)
{
	mac_ring_t *ring = (mac_ring_t *)mrh;

	mutex_enter(&ring->mr_lock);
	if (ring->mr_intr_obj != NULL) {
		numaio_object_destroy(ring->mr_intr_obj);
		ring->mr_intr_obj = NULL;
	}
	if (!shared && ddh != NULL) {
		ring->mr_intr_obj = numaio_object_create_interrupt(ddh,
		    ring->mr_type == MAC_RING_TYPE_TX ?
		    "TX intr" : "RX intr", 0);
	}
	ring->mr_info.mri_intr.mi_ddi_shared = shared;
	ring->mr_info.mri_intr.mi_ddi_handle = ddh;
	mutex_exit(&ring->mr_lock);
}

static void
mac_ring_intr_retarget(mac_ring_t *ring)
{
	mac_group_t *group;
	mac_client_impl_t *mcip;

	/*
	 * if pseudo ring is present, then pseudo ring needs
	 * to be re-targeted if a mac client exists for it.
	 */
	if (ring->mr_prh != NULL || ring->mr_intr_obj == NULL)
		return;
	/*
	 * Get the groups clients find out if it is a 1) group-only
	 * client or 2) primary client. Only in the above 2 cases
	 * are the interrupted re-targeted.
	 */
	group = (mac_group_t *)ring->mr_gh;

	if (group != NULL && (((mcip = mac_get_grp_primary(group)) != NULL) ||
	    ((mcip = MAC_GROUP_ONLY_CLIENT(group)) != NULL))) {
		mac_cpu_modify(mcip, MAC_CPU_INTR, (void *)ring);
	}
}

/*
 * Clients like aggr create pseudo rings (mac_ring_t) and expose them to
 * their clients. There is a 1-1 mapping pseudo ring and the hardware
 * ring. ddi interrupt handles are exported from the hardware ring to
 * the pseudo ring. Thus when the interrupt handle changes, clients of
 * aggr that are using the handle need to use the new handle and
 * re-target their interrupts.
 */
static void
mac_pseudo_ring_intr_modify(mac_impl_t *mip,
    mac_ring_t *ring, ddi_intr_handle_t ddh, boolean_t shared)
{
	mac_ring_t *pring;
	mac_group_t *pgroup;
	mac_impl_t *pmip;
	char macname[MAXNAMELEN];
	mac_perim_handle_t p_mph;
	uint64_t saved_gen_num;

again:
	pring = (mac_ring_t *)ring->mr_prh;
	pgroup = (mac_group_t *)pring->mr_gh;
	pmip = (mac_impl_t *)pgroup->mrg_mh;
	saved_gen_num = ring->mr_gen_num;
	(void) strlcpy(macname, pmip->mi_name, MAXNAMELEN);
	/*
	 * We need to enter aggr's perimeter. The locking hierarchy
	 * dictates that aggr's perimeter should be entered first
	 * and then the port's perimeter. So drop the port's
	 * perimeter, enter aggr's and then re-enter port's
	 * perimeter.
	 */
	i_mac_perim_exit(mip);
	/*
	 * While we know pmip is the aggr's mip, there is a
	 * possibility that aggr could have unregistered by
	 * the time we exit port's perimeter (mip) and
	 * enter aggr's perimeter (pmip). To avoid that
	 * scenario, enter aggr's perimeter using its name.
	 */
	if (mac_perim_enter_by_macname(macname, &p_mph) != 0) {
		i_mac_perim_enter(mip);
		return;
	}
	i_mac_perim_enter(mip);
	/*
	 * Check if the ring got assigned to another aggregation before
	 * be could enter aggr's and the port's perimeter. When a ring
	 * gets deleted from an aggregation, it calls mac_stop_ring()
	 * which increments the generation number. So checking
	 * generation number will be enough.
	 */
	if (ring->mr_gen_num != saved_gen_num && ring->mr_prh != NULL) {
		i_mac_perim_exit(mip);
		mac_perim_exit(p_mph);
		i_mac_perim_enter(mip);
		goto again;
	}

	/* Check if pseudo ring is still present */
	if (ring->mr_prh != NULL) {
		if (ddh == NULL) {
			mac_ring_intr_modify(ring->mr_prh, NULL, B_FALSE);
		} else {
			mac_ring_intr_modify(ring->mr_prh, ddh, shared);
			mac_ring_intr_retarget(pring);
		}
	}
	/* exit the perimeters in the reverse order we entered */
	i_mac_perim_exit(mip);
	mac_perim_exit(p_mph);
	/* acquire mip perimeter before we exit */
	i_mac_perim_enter(mip);
}

/*
 * API called by driver to provide new interrupt handle for TX/RX rings.
 * This usually happens when IRM (Interrupt Resource Manangement)
 * framework either gives the driver more MSI-x interrupts or takes
 * away MSI-x interrupts from the driver.
 */
void
mac_ring_intr_set(mac_ring_handle_t mrh, ddi_intr_handle_t ddh)
{
	mac_ring_t	*ring = (mac_ring_t *)mrh;
	mac_group_t	*group = (mac_group_t *)ring->mr_gh;
	mac_impl_t	*mip = (mac_impl_t *)group->mrg_mh;

	i_mac_perim_enter(mip);
	/* first remove interrupt object if present */
	if (ring->mr_intr_obj != NULL) {
		if (ring->mr_prh != NULL)
			mac_pseudo_ring_intr_modify(mip, ring, NULL, B_FALSE);
		mac_ring_intr_modify(mrh, NULL, B_FALSE);
	}
	if (ddh != NULL) {
		/* New interrupt handle */
		ring->mr_info.mri_intr.mi_ddi_handle = ddh;
		mac_compare_ddi_handle(mip->mi_rx_groups, ring);
		if (!ring->mr_info.mri_intr.mi_ddi_shared) {
			mac_compare_ddi_handle(mip->mi_tx_groups, ring);
		}
		if (ring->mr_prh != NULL) {
			mac_pseudo_ring_intr_modify(mip,
			    ring, ddh, ring->mr_info.mri_intr.mi_ddi_shared);
		}
		mac_ring_intr_modify(mrh, ddh,
		    ring->mr_info.mri_intr.mi_ddi_shared);
		mac_ring_intr_retarget(ring);
	}
	i_mac_perim_exit(mip);
}

/* PRIVATE FUNCTIONS, FOR INTERNAL USE ONLY */

/*
 * Updates the mac_impl structure with the current state of the link
 */
static void
i_mac_log_link_state(mac_impl_t *mip)
{
	/*
	 * If no change, then it is not interesting.
	 */
	if (mip->mi_lastlowlinkstate == mip->mi_lowlinkstate)
		return;

	switch (mip->mi_lowlinkstate) {
	case LINK_STATE_UP:
		if (mip->mi_type->mt_ops.mtops_ops & MTOPS_LINK_DETAILS) {
			char det[200];

			mutex_enter(&mip->mi_pdata_lock);

			mip->mi_type->mt_ops.mtops_link_details(det,
			    sizeof (det), (mac_handle_t)mip, mip->mi_pdata);

			mutex_exit(&mip->mi_pdata_lock);

			cmn_err(CE_NOTE, "!%s link up, %s", mip->mi_linkname,
			    det);
		} else {
			cmn_err(CE_NOTE, "!%s link up", mip->mi_linkname);
		}
		break;

	case LINK_STATE_DOWN:
		/*
		 * Only transitions from UP to DOWN are interesting
		 */
		if (mip->mi_lastlowlinkstate != LINK_STATE_UNKNOWN)
			cmn_err(CE_NOTE, "!%s link down", mip->mi_linkname);
		break;

	case LINK_STATE_UNKNOWN:
		/*
		 * This case is normally not interesting.
		 */
		break;
	}
	mip->mi_lastlowlinkstate = mip->mi_lowlinkstate;
}

/*
 * Main routine for the callbacks notifications thread
 */
static void
i_mac_notify_thread(void *arg)
{
	mac_impl_t	*mip = arg;
	callb_cpr_t	cprinfo;
	mac_cb_t	*mcb;
	mac_cb_info_t	*mcbi;
	mac_notify_cb_t	*mncb;

	mcbi = &mip->mi_notify_cb_info;
	CALLB_CPR_INIT(&cprinfo, mcbi->mcbi_lockp, callb_generic_cpr,
	    "i_mac_notify_thread");

	mutex_enter(mcbi->mcbi_lockp);

	for (;;) {
		uint32_t	bits;
		uint32_t	type;

		bits = mip->mi_notify_bits;
		if (bits == 0) {
			CALLB_CPR_SAFE_BEGIN(&cprinfo);
			cv_wait(&mcbi->mcbi_cv, mcbi->mcbi_lockp);
			CALLB_CPR_SAFE_END(&cprinfo, mcbi->mcbi_lockp);
			continue;
		}
		mip->mi_notify_bits = 0;
		if ((bits & (1 << MAC_NNOTE)) != 0) {
			/* request to quit */
			ASSERT(mip->mi_state_flags & MIS_DISABLED);
			break;
		}

		mutex_exit(mcbi->mcbi_lockp);

		/*
		 * Log link changes on the actual link, but then do reports on
		 * synthetic state (if part of a bridge).
		 */
		if ((bits & (1 << MAC_NOTE_LOWLINK)) != 0) {
			link_state_t newstate;
			mac_handle_t mh;

			i_mac_log_link_state(mip);
			newstate = mip->mi_lowlinkstate;
			if (mip->mi_bridge_link != NULL) {
				mutex_enter(&mip->mi_bridge_lock);
				if ((mh = mip->mi_bridge_link) != NULL) {
					newstate = mac_bridge_ls_cb(mh,
					    newstate);
				}
				mutex_exit(&mip->mi_bridge_lock);
			}
			if (newstate != mip->mi_linkstate) {
				mip->mi_linkstate = newstate;
				bits |= 1 << MAC_NOTE_LINK;
			}
		}

		/*
		 * Do notification callbacks for each notification type.
		 */
		for (type = 0; type < MAC_NNOTE; type++) {
			if ((bits & (1 << type)) == 0) {
				continue;
			}

			if (mac_notify_cb_list[type] != NULL)
				(*mac_notify_cb_list[type])(mip);

			/*
			 * Walk the list of notifications.
			 */
			MAC_CALLBACK_WALKER_INC(&mip->mi_notify_cb_info);
			for (mcb = mip->mi_notify_cb_list; mcb != NULL;
			    mcb = mcb->mcb_nextp) {
				mncb = (mac_notify_cb_t *)mcb->mcb_objp;
				mncb->mncb_fn(mncb->mncb_arg, type);
			}
			MAC_CALLBACK_WALKER_DCR(&mip->mi_notify_cb_info,
			    &mip->mi_notify_cb_list);
		}

		mutex_enter(mcbi->mcbi_lockp);
	}

	mip->mi_state_flags |= MIS_NOTIFY_DONE;
	cv_broadcast(&mcbi->mcbi_cv);

	/* CALLB_CPR_EXIT drops the lock */
	CALLB_CPR_EXIT(&cprinfo);
	thread_exit();
}

/*
 * Signal the i_mac_notify_thread asking it to quit.
 * Then wait till it is done.
 */
void
i_mac_notify_exit(mac_impl_t *mip)
{
	mac_cb_info_t	*mcbi;

	mcbi = &mip->mi_notify_cb_info;

	mutex_enter(mcbi->mcbi_lockp);
	mip->mi_notify_bits = (1 << MAC_NNOTE);
	cv_broadcast(&mcbi->mcbi_cv);


	while ((mip->mi_notify_thread != NULL) &&
	    !(mip->mi_state_flags & MIS_NOTIFY_DONE)) {
		cv_wait(&mcbi->mcbi_cv, mcbi->mcbi_lockp);
	}

	/* Necessary clean up before doing kmem_cache_free */
	mip->mi_state_flags &= ~MIS_NOTIFY_DONE;
	mip->mi_notify_bits = 0;
	mip->mi_notify_thread = NULL;
	mutex_exit(mcbi->mcbi_lockp);
}

/*
 * Entry point invoked by drivers to dynamically add a ring to an
 * existing group.
 */
int
mac_group_add_ring(mac_group_handle_t gh, int index)
{
	mac_group_t *group = (mac_group_t *)gh;
	mac_impl_t *mip = (mac_impl_t *)group->mrg_mh;
	int ret;

	i_mac_perim_enter(mip);
	ret = i_mac_group_add_ring(group, NULL, index);
	i_mac_perim_exit(mip);
	return (ret);
}

/*
 * Entry point invoked by drivers to dynamically remove a ring
 * from an existing group. The specified ring handle must no longer
 * be used by the driver after a call to this function.
 */
void
mac_group_rem_ring(mac_group_handle_t gh, mac_ring_handle_t rh)
{
	mac_group_t *group = (mac_group_t *)gh;
	mac_impl_t *mip = (mac_impl_t *)group->mrg_mh;

	i_mac_perim_enter(mip);
	i_mac_group_rem_ring(group, (mac_ring_t *)rh, B_TRUE);
	i_mac_perim_exit(mip);
}

/*
 * mac_prop_info_*() callbacks called from the driver's prefix_propinfo()
 * entry points.
 */

void
mac_prop_info_set_default_uint8(mac_prop_info_handle_t ph, uint8_t val)
{
	mac_prop_info_state_t *pr = (mac_prop_info_state_t *)ph;

	/* nothing to do if the caller doesn't want the default value */
	if (pr->pr_default == NULL)
		return;

	ASSERT(pr->pr_default_size >= sizeof (uint8_t));

	*(uint8_t *)(pr->pr_default) = val;
	pr->pr_flags |= MAC_PROP_INFO_DEFAULT;
}

void
mac_prop_info_set_default_uint64(mac_prop_info_handle_t ph, uint64_t val)
{
	mac_prop_info_state_t *pr = (mac_prop_info_state_t *)ph;

	/* nothing to do if the caller doesn't want the default value */
	if (pr->pr_default == NULL)
		return;

	ASSERT(pr->pr_default_size >= sizeof (uint64_t));

	bcopy(&val, pr->pr_default, sizeof (val));

	pr->pr_flags |= MAC_PROP_INFO_DEFAULT;
}

void
mac_prop_info_set_default_uint32(mac_prop_info_handle_t ph, uint32_t val)
{
	mac_prop_info_state_t *pr = (mac_prop_info_state_t *)ph;

	/* nothing to do if the caller doesn't want the default value */
	if (pr->pr_default == NULL)
		return;

	ASSERT(pr->pr_default_size >= sizeof (uint32_t));

	bcopy(&val, pr->pr_default, sizeof (val));

	pr->pr_flags |= MAC_PROP_INFO_DEFAULT;
}

void
mac_prop_info_set_default_str(mac_prop_info_handle_t ph, const char *str)
{
	mac_prop_info_state_t *pr = (mac_prop_info_state_t *)ph;

	/* nothing to do if the caller doesn't want the default value */
	if (pr->pr_default == NULL)
		return;

	if (strlen(str) >= pr->pr_default_size)
		pr->pr_errno = ENOBUFS;
	else
		(void) strlcpy(pr->pr_default, str, pr->pr_default_size);
	pr->pr_flags |= MAC_PROP_INFO_DEFAULT;
}

void
mac_prop_info_set_default_link_flowctrl(mac_prop_info_handle_t ph,
    link_flowctrl_t val)
{
	mac_prop_info_state_t *pr = (mac_prop_info_state_t *)ph;

	/* nothing to do if the caller doesn't want the default value */
	if (pr->pr_default == NULL)
		return;

	ASSERT(pr->pr_default_size >= sizeof (link_flowctrl_t));

	bcopy(&val, pr->pr_default, sizeof (val));

	pr->pr_flags |= MAC_PROP_INFO_DEFAULT;
}

void
mac_prop_info_set_range_uint32(mac_prop_info_handle_t ph, uint32_t min,
    uint32_t max)
{
	mac_prop_info_state_t *pr = (mac_prop_info_state_t *)ph;
	mac_propval_range_t *range = pr->pr_range;
	mac_propval_uint32_range_t *range32;

	/* nothing to do if the caller doesn't want the range info */
	if (range == NULL)
		return;

	if (pr->pr_range_cur_count++ == 0) {
		/* first range */
		pr->pr_flags |= MAC_PROP_INFO_RANGE;
		range->mpr_type = MAC_PROPVAL_UINT32;
	} else {
		/* all ranges of a property should be of the same type */
		ASSERT(range->mpr_type == MAC_PROPVAL_UINT32);
		if (pr->pr_range_cur_count > range->mpr_count) {
			pr->pr_errno = ENOSPC;
			return;
		}
	}

	range32 = range->mpr_range_uint32;
	range32[pr->pr_range_cur_count - 1].mpur_min = min;
	range32[pr->pr_range_cur_count - 1].mpur_max = max;
}

void
mac_prop_info_set_perm(mac_prop_info_handle_t ph, uint8_t perm)
{
	mac_prop_info_state_t *pr = (mac_prop_info_state_t *)ph;

	pr->pr_perm = perm;
	pr->pr_flags |= MAC_PROP_INFO_PERM;
}

void mac_hcksum_get(mblk_t *mp, uint32_t *start, uint32_t *stuff,
    uint32_t *end, uint32_t *value, uint32_t *flags_ptr)
{
	uint32_t flags;

	ASSERT(DB_TYPE(mp) == M_DATA);

	flags = DB_CKSUMFLAGS(mp) & HCK_FLAGS;
	if ((flags & (HCK_PARTIALCKSUM | HCK_FULLCKSUM)) != 0) {
		if (value != NULL)
			*value = (uint32_t)DB_CKSUM16(mp);
		if ((flags & HCK_PARTIALCKSUM) != 0) {
			if (start != NULL)
				*start = (uint32_t)DB_CKSUMSTART(mp);
			if (stuff != NULL)
				*stuff = (uint32_t)DB_CKSUMSTUFF(mp);
			if (end != NULL)
				*end = (uint32_t)DB_CKSUMEND(mp);
		}
	}

	if (flags_ptr != NULL)
		*flags_ptr = flags;
}

void mac_hcksum_set(mblk_t *mp, uint32_t start, uint32_t stuff,
    uint32_t end, uint32_t value, uint32_t flags)
{
	ASSERT(DB_TYPE(mp) == M_DATA);

	DB_CKSUMSTART(mp) = (intptr_t)start;
	DB_CKSUMSTUFF(mp) = (intptr_t)stuff;
	DB_CKSUMEND(mp) = (intptr_t)end;
	DB_CKSUMFLAGS(mp) = (uint16_t)flags;
	DB_CKSUM16(mp) = (uint16_t)value;
}

void
mac_lso_get(mblk_t *mp, uint32_t *mss, uint32_t *flags)
{
	ASSERT(DB_TYPE(mp) == M_DATA);

	if (flags != NULL) {
		*flags = DB_CKSUMFLAGS(mp) & HW_LSO;
		if ((*flags != 0) && (mss != NULL))
			*mss = (uint32_t)DB_LSOMSS(mp);
	}
}

void
mac_fcoe_get(mblk_t *mp, mac_fcoe_tx_params_t *mftp, uint32_t *flags)
{
	ASSERT(DB_TYPE(mp) == M_DATA);

	if (flags != NULL) {
		*flags = DB_CKSUMFLAGS(mp) & HW_FCOE;
		if ((*flags != 0) && (mftp != NULL)) {
			bcopy(mp->b_datap->db_base, mftp,
			    sizeof (mac_fcoe_tx_params_t));
		}
	}
}
