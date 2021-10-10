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

#include <sys/types.h>
#include <sys/cred.h>
#include <sys/sysmacros.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/list.h>
#include <sys/ksynch.h>
#include <sys/kmem.h>
#include <sys/stream.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/atomic.h>
#include <sys/stat.h>
#include <sys/modhash.h>
#include <sys/strsubr.h>
#include <sys/strsun.h>
#include <sys/dlpi.h>
#include <sys/mac.h>
#include <sys/mac_provider.h>
#include <sys/mac_client.h>
#include <sys/mac_client_priv.h>
#include <sys/mac_ether.h>
#include <sys/dls.h>
#include <sys/pattr.h>
#include <sys/time.h>
#include <sys/vlan.h>
#include <sys/vnic.h>
#include <sys/vnic_impl.h>
#include <sys/mac_flow_impl.h>
#include <sys/zone.h>

/*
 * Note that for best performance, the VNIC is a passthrough design.
 * For each VNIC corresponds a MAC client of the underlying MAC (lower MAC).
 * This MAC client is opened by the VNIC driver at VNIC creation,
 * and closed when the VNIC is deleted.
 * When a MAC client of the VNIC itself opens a VNIC, the MAC layer
 * (upper MAC) detects that the MAC being opened is a VNIC. Instead
 * of allocating a new MAC client, it asks the VNIC driver to return
 * the lower MAC client handle associated with the VNIC, and that handle
 * is returned to the upper MAC client directly. This allows access
 * by upper MAC clients of the VNIC to have direct access to the lower
 * MAC client for the control path and data path.
 *
 * Due to this passthrough, some of the entry points exported by the
 * VNIC driver are never directly invoked. These entry points include
 * vnic_m_start, vnic_m_stop, vnic_m_promisc, vnic_m_multicst, etc.
 */

static int vnic_m_start(void *);
static void vnic_m_stop(void *);
static int vnic_m_promisc(void *, boolean_t);
static int vnic_m_multicst(void *, boolean_t, const uint8_t *);
static int vnic_m_unicst(void *, const uint8_t *);
static int vnic_m_stat(void *, uint_t, uint64_t *);
static void vnic_m_ioctl(void *, queue_t *, mblk_t *);
static int vnic_m_setprop(void *, const char *, mac_prop_id_t, uint_t,
    const void *);
static void vnic_m_propinfo(void *, const char *, mac_prop_id_t,
    mac_prop_info_handle_t);
static mblk_t *vnic_m_tx(void *, mblk_t *);
static boolean_t vnic_m_capab_get(void *, mac_capab_t, void *);
static void vnic_notify_cb(void *, mac_notify_type_t);
static boolean_t vnic_capab_get(vnic_t *);

boolean_t vnic_do_lso = B_TRUE;

static kmem_cache_t	*vnic_cache;
static krwlock_t	vnic_lock;
static uint_t		vnic_count;

#define	ANCHOR_VNIC_MIN_MTU	576
#define	ANCHOR_VNIC_MAX_MTU	9000

/* hash of VNICs (vnic_t's), keyed by VNIC id */
static mod_hash_t	*vnic_hash;
#define	VNIC_HASHSZ	64
#define	VNIC_HASH_KEY(vnic_id)	((mod_hash_key_t)(uintptr_t)vnic_id)

#define	VNIC_M_CALLBACK_FLAGS	\
	(MC_IOCTL | MC_GETCAPAB | MC_SETPROP | MC_PROPINFO)

static mac_callbacks_t vnic_m_callbacks = {
	VNIC_M_CALLBACK_FLAGS,
	vnic_m_stat,
	vnic_m_start,
	vnic_m_stop,
	vnic_m_promisc,
	vnic_m_multicst,
	vnic_m_unicst,
	vnic_m_tx,
	NULL,
	vnic_m_ioctl,
	vnic_m_capab_get,
	NULL,
	NULL,
	vnic_m_setprop,
	NULL,
	vnic_m_propinfo
};

void
vnic_dev_init(void)
{
	vnic_cache = kmem_cache_create("vnic_cache",
	    sizeof (vnic_t), 0, NULL, NULL, NULL, NULL, NULL, 0);

	vnic_hash = mod_hash_create_idhash("vnic_hash",
	    VNIC_HASHSZ, mod_hash_null_valdtor);

	rw_init(&vnic_lock, NULL, RW_DEFAULT, NULL);

	vnic_count = 0;
}

void
vnic_dev_fini(void)
{
	ASSERT(vnic_count == 0);

	rw_destroy(&vnic_lock);
	mod_hash_destroy_idhash(vnic_hash);
	kmem_cache_destroy(vnic_cache);
}

uint_t
vnic_dev_count(void)
{
	return (vnic_count);
}

static vnic_ioc_diag_t
vnic_mac2vnic_diag(mac_diag_t diag)
{
	switch (diag) {
	case MAC_DIAG_MACADDR_NIC:
		return (VNIC_IOC_DIAG_MACADDR_NIC);
	case MAC_DIAG_MACADDR_INUSE:
		return (VNIC_IOC_DIAG_MACADDR_INUSE);
	case MAC_DIAG_MACADDR_INVALID:
		return (VNIC_IOC_DIAG_MACADDR_INVALID);
	case MAC_DIAG_MACADDRLEN_INVALID:
		return (VNIC_IOC_DIAG_MACADDRLEN_INVALID);
	case MAC_DIAG_MACFACTORYSLOTINVALID:
		return (VNIC_IOC_DIAG_MACFACTORYSLOTINVALID);
	case MAC_DIAG_MACFACTORYSLOTUSED:
		return (VNIC_IOC_DIAG_MACFACTORYSLOTUSED);
	case MAC_DIAG_MACFACTORYSLOTALLUSED:
		return (VNIC_IOC_DIAG_MACFACTORYSLOTALLUSED);
	case MAC_DIAG_MACFACTORYNOTSUP:
		return (VNIC_IOC_DIAG_MACFACTORYNOTSUP);
	case MAC_DIAG_MACPREFIX_INVALID:
		return (VNIC_IOC_DIAG_MACPREFIX_INVALID);
	case MAC_DIAG_MACPREFIXLEN_INVALID:
		return (VNIC_IOC_DIAG_MACPREFIXLEN_INVALID);
	case MAC_DIAG_MACNO_HWRINGS:
		return (VNIC_IOC_DIAG_NO_HWRINGS);
	default:
		return (VNIC_IOC_DIAG_NONE);
	}
}

/*
 * Default random MAC address prefix.
 */
static char vnic_def_prefix[] = VNIC_DEF_PREFIX;

static int
vnic_addr_handler(vnic_t *vnic, vnic_mac_addr_type_t *vnic_addr_type,
    int *addr_slot, uint_t prefix_len, int *addr_len_ptr_arg,
    uint8_t *mac_addr_arg, vnic_ioc_diag_t *diag, boolean_t req_hwgrp_flag)
{
	mac_diag_t mac_diag;
	int err;
	uint_t addr_len;

	switch (*vnic_addr_type) {
	case VNIC_MAC_ADDR_TYPE_FIXED:
	case VNIC_MAC_ADDR_TYPE_VRID:
		/*
		 * The MAC address value to assign to the VNIC
		 * is already provided in mac_addr_arg. addr_len_ptr_arg
		 * already contains the MAC address length.
		 */
		break;

	case VNIC_MAC_ADDR_TYPE_RANDOM:
		/*
		 * Random MAC address. There are two sub-cases:
		 *
		 * 1 - If *addr_len_ptr_arg == 0, new MAC address is generated.
		 *	The length of the MAC address to be generated depends
		 *	on the type of MAC used. The prefix to use for the MAC
		 *	address is stored in the most significant bytes
		 *	of the mac_addr_arg argument, and its length is
		 *	specified by the prefix_len argument. This prefix can
		 *	correspond to a IEEE OUI in the case of Ethernet,
		 *	for example.
		 *
		 * 2 - If *addr_len_ptr_arg > 0, the address was already picked
		 *	randomly, and is now passed back during VNIC
		 *	re-creation. The mac_addr_arg argument contains the MAC
		 *	address that was generated. We distinguish this
		 *	case from the fixed MAC address case, since we
		 *	want the user consumers to know, when they query
		 *	the list of VNICs, that a VNIC was assigned a
		 *	random MAC address vs assigned a fixed address
		 *	specified by the user.
		 */

		/*
		 * If it's a pre-generated address, we're done. mac_addr_arg
		 * and addr_len_ptr_arg already contain the MAC address
		 * value and length.
		 */
		if (*addr_len_ptr_arg > 0)
			break;

		/*
		 * If a random address might be generated, but no prefix
		 * was specified by the caller, use the default MAC address
		 * prefix.
		 */
		if (prefix_len == 0) {
			prefix_len = sizeof (vnic_def_prefix);
			bcopy(vnic_def_prefix, mac_addr_arg, prefix_len);
		}

		/* generate a new random MAC address */
		if ((err = mac_addr_random(vnic->vn_mch,
		    prefix_len, mac_addr_arg, &mac_diag)) != 0) {
			*diag = vnic_mac2vnic_diag(mac_diag);
			return (err);
		}
		*addr_len_ptr_arg = mac_addr_len(vnic->vn_lower_mh);
		break;

	case VNIC_MAC_ADDR_TYPE_FACTORY:
		err = mac_addr_factory_reserve(vnic->vn_mch, addr_slot);
		if (err != 0) {
			if (err == EINVAL)
				*diag = VNIC_IOC_DIAG_MACFACTORYSLOTINVALID;
			if (err == EBUSY)
				*diag = VNIC_IOC_DIAG_MACFACTORYSLOTUSED;
			if (err == ENOSPC)
				*diag = VNIC_IOC_DIAG_MACFACTORYSLOTALLUSED;
			return (err);
		}

		mac_addr_factory_value(vnic->vn_lower_mh, *addr_slot,
		    mac_addr_arg, &addr_len, NULL, NULL);
		*addr_len_ptr_arg = addr_len;
		break;

	case VNIC_MAC_ADDR_TYPE_AUTO:
		/* first try to allocate a factory MAC address */
		err = mac_addr_factory_reserve(vnic->vn_mch, addr_slot);
		if (err == 0) {
			mac_addr_factory_value(vnic->vn_lower_mh, *addr_slot,
			    mac_addr_arg, &addr_len, NULL, NULL);
			*vnic_addr_type = VNIC_MAC_ADDR_TYPE_FACTORY;
			*addr_len_ptr_arg = addr_len;
			break;
		}

		/* See comments for VNIC_MAC_ADDR_TYPE_RANDOM above */
		if (prefix_len == 0) {
			prefix_len = sizeof (vnic_def_prefix);
			bcopy(vnic_def_prefix, mac_addr_arg, prefix_len);
		}
		/*
		 * Allocating a factory MAC address failed, generate a
		 * random MAC address instead.
		 */
		if ((err = mac_addr_random(vnic->vn_mch,
		    prefix_len, mac_addr_arg, &mac_diag)) != 0) {
			*diag = vnic_mac2vnic_diag(mac_diag);
			return (err);
		}
		*addr_len_ptr_arg = mac_addr_len(vnic->vn_lower_mh);
		*vnic_addr_type = VNIC_MAC_ADDR_TYPE_RANDOM;
		break;

	case VNIC_MAC_ADDR_TYPE_PRIMARY:
		/*
		 * We get the address here since we copy it in the
		 * vnic's vn_addr.
		 * We can't ask for hardware resources since we
		 * don't currently support hardware classification
		 * for these MAC clients.
		 */
		if (req_hwgrp_flag) {
			*diag = VNIC_IOC_DIAG_NO_HWRINGS;
			return (ENOTSUP);
		}
		mac_unicast_primary_get(vnic->vn_lower_mh, mac_addr_arg);
		*addr_len_ptr_arg = mac_addr_len(vnic->vn_lower_mh);
		break;
	}

	vnic->vn_addr_type = *vnic_addr_type;
	if (vnic->vn_addr_type == VNIC_MAC_ADDR_TYPE_FACTORY)
		vnic->vn_slot_id = *addr_slot;

	return (0);
}

static int
vnic_unicast_add(vnic_t *vnic, vnic_mac_addr_type_t vnic_addr_type,
    int *addr_slot, uint_t prefix_len, int *addr_len_ptr_arg,
    uint8_t *mac_addr_arg, uint16_t flags, vnic_ioc_diag_t *diag,
    uint16_t vid, boolean_t req_hwgrp_flag)
{
	mac_diag_t mac_diag;
	uint16_t mac_flags = 0;
	int err;

	err = vnic_addr_handler(vnic, &vnic_addr_type, addr_slot,
	    prefix_len, addr_len_ptr_arg, mac_addr_arg, diag, req_hwgrp_flag);
	if (err != 0) {
		return (err);
	}

	if (flags & VNIC_IOC_CREATE_NODUPCHECK)
		mac_flags |= MAC_UNICAST_NODUPCHECK;
	if (vnic_addr_type == VNIC_MAC_ADDR_TYPE_PRIMARY)
		mac_flags |= MAC_UNICAST_VNIC_PRIMARY;

	err = mac_unicast_add(vnic->vn_mch, mac_addr_arg, mac_flags,
	    &vnic->vn_muh, vid, &mac_diag);
	if (err != 0) {
		if (vnic_addr_type == VNIC_MAC_ADDR_TYPE_FACTORY) {
			/* release factory MAC address */
			mac_addr_factory_release(vnic->vn_mch, *addr_slot);
		}
		*diag = vnic_mac2vnic_diag(mac_diag);
	}

	return (err);
}

/*
 * Create a new VNIC upon request from administrator.
 * Returns 0 on success, an errno on failure.
 */
/* ARGSUSED */
int
vnic_dev_create(datalink_id_t vnic_id,
    datalink_id_t linkid, zoneid_t target_zoneid,
    vnic_mac_addr_type_t *vnic_addr_type, int *mac_len, uchar_t *mac_addr,
    int *mac_slot, uint_t mac_prefix_len, uint16_t vid, vrid_t vrid,
    int af, mac_resource_props_t *mrp, uint32_t flags, vnic_ioc_diag_t *diag,
    cred_t *credp)
{
	vnic_t *vnic;
	mac_register_t *mac;
	int err;
	boolean_t is_anchor = ((flags & VNIC_IOC_CREATE_ANCHOR) != 0);
	char vnic_name[MAXNAMELEN];
	const mac_info_t *minfop;
	uint32_t req_hwgrp_flag = B_FALSE;

	*diag = VNIC_IOC_DIAG_NONE;

	rw_enter(&vnic_lock, RW_WRITER);

	/* does a VNIC with the same id already exist? */
	err = mod_hash_find(vnic_hash, VNIC_HASH_KEY(vnic_id),
	    (mod_hash_val_t *)&vnic);
	if (err == 0) {
		rw_exit(&vnic_lock);
		return (EEXIST);
	}

	vnic = kmem_cache_alloc(vnic_cache, KM_NOSLEEP);
	if (vnic == NULL) {
		rw_exit(&vnic_lock);
		return (ENOMEM);
	}

	bzero(vnic, sizeof (*vnic));

	vnic->vn_id = vnic_id;
	vnic->vn_link_id = linkid;
	vnic->vn_vrid = vrid;
	vnic->vn_af = af;

	/* Set the owner's zoneid */
	vnic->vn_owner_zone_id = crgetzoneid(credp);

	/*
	 * Set the target zone's zoneid. If the target zoneid is not
	 * explicitly set then we assume that the owner is creating this
	 * link for itself.
	 * Today for vnics other than autovnics, GZ is the owner and target
	 * zone at creation time. For autovnics, the owner is the GZ and
	 * targe zonet is the NGZ for which this has been created for.
	 */
	if (target_zoneid == (zoneid_t)-1)
		target_zoneid = vnic->vn_owner_zone_id;
	vnic->vn_zone_id = target_zoneid;

	if (!is_anchor) {
		if (linkid == DATALINK_INVALID_LINKID) {
			err = EINVAL;
			goto bail;
		}

		/*
		 * Open the lower MAC and assign its initial bandwidth and
		 * MAC address. We do this here during VNIC creation and
		 * do not wait until the upper MAC client open so that we
		 * can validate the VNIC creation parameters (bandwidth,
		 * MAC address, etc) and reserve a factory MAC address if
		 * one was requested.
		 */
		err = mac_open_by_linkid(linkid, &vnic->vn_lower_mh);
		if (err != 0)
			goto bail;

		/*
		 * VNIC(vlan) over VNICs(vlans) is not supported.
		 */
		if (mac_is_vnic(vnic->vn_lower_mh)) {
			err = EINVAL;
			goto bail;
		}

		/* only ethernet support for now */
		minfop = mac_info(vnic->vn_lower_mh);
		if (minfop->mi_nativemedia != DL_ETHER) {
			err = ENOTSUP;
			goto bail;
		}

		(void) dls_mgmt_get_linkinfo(vnic_id, vnic_name, NULL, NULL,
		    NULL);
		err = mac_client_open_zone(vnic->vn_lower_mh, &vnic->vn_mch,
		    vnic_name, MAC_OPEN_FLAGS_IS_VNIC, target_zoneid);
		if (err != 0)
			goto bail;

		if (mrp != NULL) {
			if ((mrp->mrp_mask & MRP_RX_RINGS) != 0 ||
			    (mrp->mrp_mask & MRP_TX_RINGS) != 0) {
				req_hwgrp_flag = B_TRUE;
			}
			err = mac_client_set_resources(vnic->vn_mch, mrp);
			if (err != 0)
				goto bail;
		}

		/* Assign a MAC address to the VNIC */
		err = vnic_unicast_add(vnic, *vnic_addr_type, mac_slot,
		    mac_prefix_len, mac_len, mac_addr, flags, diag, vid,
		    req_hwgrp_flag);
		if (err != 0) {
			vnic->vn_muh = NULL;
			if (diag != NULL && req_hwgrp_flag)
				*diag = VNIC_IOC_DIAG_NO_HWRINGS;
			goto bail;
		}

		/* Register to receive notification from underlying MAC */
		vnic->vn_mnh = mac_notify_add(vnic->vn_lower_mh, vnic_notify_cb,
		    vnic);

		*vnic_addr_type = vnic->vn_addr_type;
		vnic->vn_addr_len = *mac_len;
		vnic->vn_vid = vid;

		bcopy(mac_addr, vnic->vn_addr, vnic->vn_addr_len);

		if (vnic->vn_addr_type == VNIC_MAC_ADDR_TYPE_FACTORY)
			vnic->vn_slot_id = *mac_slot;

		/*
		 * Set the initial VNIC capabilities. If the VNIC is created
		 * over MACs which does not support nactive vlan, disable
		 * VNIC's hardware checksum capability if its VID is not 0,
		 * since the underlying MAC would get the hardware checksum
		 * offset wrong in case of VLAN packets.
		 */
		(void) vnic_capab_get(vnic);
	}

	/* register with the MAC module */
	if ((mac = mac_alloc(MAC_VERSION)) == NULL)
		goto bail;

	mac->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	mac->m_driver = vnic;
	mac->m_dip = vnic_get_dip();
	mac->m_instance = (uint_t)-1;
	mac->m_src_addr = vnic->vn_addr;
	mac->m_callbacks = &vnic_m_callbacks;

	if (!is_anchor) {
		mac_capab_lso_t	mac_lso;

		/*
		 * If this is a VNIC based VLAN, then we check for the
		 * margin unless it has been created with the force
		 * flag. If we are configuring a VLAN over an etherstub,
		 * we don't check the margin even if force is not set.
		 */
		if (vid == 0 || (flags & VNIC_IOC_CREATE_FORCE) != 0) {
			if (vid != VLAN_ID_NONE)
				vnic->vn_force = B_TRUE;
			/*
			 * As the current margin size of the underlying mac is
			 * used to determine the margin size of the VNIC
			 * itself, request the underlying mac not to change
			 * to a smaller margin size.
			 */
			err = mac_margin_add(vnic->vn_lower_mh,
			    &vnic->vn_margin, B_TRUE);
			ASSERT(err == 0);
		} else {
			vnic->vn_margin = VLAN_TAGSZ;
			err = mac_margin_add(vnic->vn_lower_mh,
			    &vnic->vn_margin, B_FALSE);
			if (err != 0) {
				mac_free(mac);
				if (diag != NULL)
					*diag = VNIC_IOC_DIAG_MACMARGIN_INVALID;
				goto bail;
			}
		}

		mac_sdu_get(vnic->vn_lower_mh, &mac->m_min_sdu,
		    &mac->m_max_sdu);

		/*
		 * Check if the underlying NIC is LSO capable, if so
		 * we'll advertise this capab as well.
		 */
		if (vnic_do_lso &&
		    mac_capab_get(vnic->vn_lower_mh, MAC_CAPAB_LSO, &mac_lso)) {
			vnic->vn_lso_flags = mac_lso.lso_flags;
			vnic->vn_lso_mss = mac_lso.lso_basic_tcp_ipv4.lso_max;
		}
	} else {
		vnic->vn_margin = VLAN_TAGSZ;
		mac->m_min_sdu = ANCHOR_VNIC_MIN_MTU;
		mac->m_max_sdu = ANCHOR_VNIC_MAX_MTU;
	}

	mac->m_margin = vnic->vn_margin;

	err = mac_register_zone(mac, &vnic->vn_mh, target_zoneid);
	mac_free(mac);
	if (err != 0) {
		VERIFY(is_anchor || mac_margin_remove(vnic->vn_lower_mh,
		    vnic->vn_margin) == 0);
		goto bail;
	}

	/* Set the VNIC's MAC in the client */
	if (!is_anchor)
		mac_set_upper_mac(vnic->vn_mch, vnic->vn_mh, mrp);

	err = dls_devnet_create_zone(vnic->vn_mh, vnic->vn_id,
	    vnic->vn_owner_zone_id, target_zoneid);
	if (err != 0) {
		VERIFY(is_anchor || mac_margin_remove(vnic->vn_lower_mh,
		    vnic->vn_margin) == 0);
		(void) mac_unregister(vnic->vn_mh);
		goto bail;
	}

	/* Update the datalink management with this new virtual link */
	if (!is_anchor) {
		dls_mgmt_update_vlink(vnic->vn_link_id, vnic->vn_id,
		    vnic->vn_vid,
		    vnic->vn_addr_type == VNIC_MAC_ADDR_TYPE_PRIMARY, B_TRUE);
	}

	/* add new VNIC to hash table */
	err = mod_hash_insert(vnic_hash, VNIC_HASH_KEY(vnic_id),
	    (mod_hash_val_t)vnic);
	ASSERT(err == 0);
	vnic_count++;

	vnic->vn_enabled = B_TRUE;
	rw_exit(&vnic_lock);

	return (0);

bail:
	rw_exit(&vnic_lock);
	if (!is_anchor) {
		if (vnic->vn_mnh != NULL)
			(void) mac_notify_remove(vnic->vn_mnh, B_TRUE);
		if (vnic->vn_muh != NULL)
			(void) mac_unicast_remove(vnic->vn_mch, vnic->vn_muh);
		if (vnic->vn_mch != NULL)
			mac_client_close(vnic->vn_mch, MAC_CLOSE_FLAGS_IS_VNIC);
		if (vnic->vn_lower_mh != NULL)
			mac_close(vnic->vn_lower_mh);
	}

	kmem_cache_free(vnic_cache, vnic);
	return (err);
}

/*
 * Modify the attributes of an existing VNIC.
 */
/* ARGSUSED */
int
vnic_dev_modify(datalink_id_t vnic_id, uint_t modify_mask,
    vnic_mac_addr_type_t *vnic_addr_type, int *mac_len, uchar_t *mac_addr,
    int *mac_slot, uint_t mac_prefix_len, mac_resource_props_t *mrp,
    vnic_ioc_diag_t *diag)
{
	vnic_t *vnic = NULL;
	int err;
	mac_perim_handle_t mph;

	/* Only MAC address modify flag is supported for now */
	if (!(modify_mask & VNIC_IOC_MODIFY_ADDR)) {
		return (EINVAL);
	}

	/* Modify to a VNIC based VLAN is not supported */
	if (*vnic_addr_type == VNIC_MAC_ADDR_TYPE_PRIMARY) {
		return (EINVAL);
	}

	rw_enter(&vnic_lock, RW_WRITER);

	if (mod_hash_find(vnic_hash, VNIC_HASH_KEY(vnic_id),
	    (mod_hash_val_t *)&vnic) != 0) {
		rw_exit(&vnic_lock);
		return (ENOENT);
	}

	/*
	 * Hold the perimeter so that the VNIC won't be modified
	 * by a set-linkprop thread.
	 */
	mac_perim_enter_by_mh(vnic->vn_mh, &mph);

	ASSERT(vnic->vn_lower_mh != NULL);

	/* return if the address did not change */
	if ((*mac_len > 0) &&
	    (bcmp(vnic->vn_addr, mac_addr, *mac_len) == 0)) {
		mac_perim_exit(mph);
		rw_exit(&vnic_lock);
		return (0);
	}

	/*
	 * Check if MAC address for the VNIC was obtained from the
	 * factory MAC addresses. If yes, release it.
	 */
	if (vnic->vn_addr_type == VNIC_MAC_ADDR_TYPE_FACTORY) {
		(void) mac_addr_factory_release(vnic->vn_mch,
		    vnic->vn_slot_id);
	}

	err = vnic_addr_handler(vnic, vnic_addr_type, mac_slot,
	    mac_prefix_len, mac_len, mac_addr, diag, B_FALSE);
	if (err != 0) {
		mac_perim_exit(mph);
		rw_exit(&vnic_lock);
		return (err);
	}

	/*
	 * mac_unicast_primary_set() indirectly calls
	 * mac_vnic_unicast_set() in the case of a VNIC.
	 */
	err = mac_unicast_primary_set(vnic->vn_mh, mac_addr);
	if (err != 0) {
		if (*vnic_addr_type == VNIC_MAC_ADDR_TYPE_FACTORY) {
			/* release factory MAC address */
			mac_addr_factory_release(vnic->vn_mch, *mac_slot);
		}
		mac_perim_exit(mph);
		rw_exit(&vnic_lock);
		return (err);
	}

	ASSERT(vnic->vn_addr_len == *mac_len);
	bcopy(mac_addr, vnic->vn_addr, vnic->vn_addr_len);
	vnic->vn_addr_type = *vnic_addr_type;
	(void) mac_unicast_default_set(vnic->vn_mh, mac_addr);
	mac_perim_exit(mph);
	rw_exit(&vnic_lock);

	return (0);
}

/* ARGSUSED */
int
vnic_dev_delete(datalink_id_t vnic_id, uint32_t flags, cred_t *credp)
{
	vnic_t *vnic = NULL;
	mod_hash_val_t val;
	datalink_id_t tmpid;
	int rc;

	rw_enter(&vnic_lock, RW_WRITER);

	if (mod_hash_find(vnic_hash, VNIC_HASH_KEY(vnic_id),
	    (mod_hash_val_t *)&vnic) != 0) {
		rw_exit(&vnic_lock);
		return (ENOENT);
	}

	/* Update the datalink management with this new virtual link */
	if (vnic->vn_id != DATALINK_INVALID_LINKID) {
		dls_mgmt_update_vlink(vnic->vn_link_id, vnic->vn_id,
		    vnic->vn_vid,
		    vnic->vn_addr_type == VNIC_MAC_ADDR_TYPE_PRIMARY, B_FALSE);
	}

	if ((rc = dls_devnet_destroy(vnic->vn_mh, &tmpid, B_TRUE)) != 0) {
		rw_exit(&vnic_lock);
		return (rc);
	}

	ASSERT(vnic_id == tmpid);

	/*
	 * We cannot unregister the MAC yet. Unregistering would
	 * free up mac_impl_t which should not happen at this time.
	 * So disable mac_impl_t by calling mac_disable(). This will prevent
	 * any new claims on mac_impl_t.
	 */
	if ((rc = mac_disable(vnic->vn_mh)) != 0) {
		(void) dls_devnet_create(vnic->vn_mh, vnic_id,
		    crgetzoneid(credp));
		rw_exit(&vnic_lock);
		return (rc);
	}

	vnic->vn_enabled = B_FALSE;
	(void) mod_hash_remove(vnic_hash, VNIC_HASH_KEY(vnic_id), &val);
	ASSERT(vnic == (vnic_t *)val);
	vnic_count--;
	rw_exit(&vnic_lock);

	if (vnic->vn_lower_mh != NULL)
		mac_set_upper_mac(vnic->vn_mch, NULL, NULL);
	/*
	 * XXX-nicolas shouldn't have a void cast here, if it's
	 * expected that the function will never fail, then we should
	 * have an ASSERT().
	 */
	(void) mac_unregister(vnic->vn_mh);

	if (vnic->vn_lower_mh != NULL) {
		/*
		 * Check if MAC address for the vnic was obtained from the
		 * factory MAC addresses. If yes, release it.
		 */
		if (vnic->vn_addr_type == VNIC_MAC_ADDR_TYPE_FACTORY) {
			(void) mac_addr_factory_release(vnic->vn_mch,
			    vnic->vn_slot_id);
		}
		(void) mac_margin_remove(vnic->vn_lower_mh, vnic->vn_margin);
		(void) mac_notify_remove(vnic->vn_mnh, B_TRUE);
		(void) mac_unicast_remove(vnic->vn_mch, vnic->vn_muh);

		mac_client_close(vnic->vn_mch, MAC_CLOSE_FLAGS_IS_VNIC);
		mac_close(vnic->vn_lower_mh);
	}

	kmem_cache_free(vnic_cache, vnic);
	return (0);
}

/* ARGSUSED */
mblk_t *
vnic_m_tx(void *arg, mblk_t *mp_chain)
{
	/*
	 * This function could be invoked for an anchor VNIC when sending
	 * broadcast and multicast packets, and unicast packets which did
	 * not match any local known destination.
	 */
	freemsgchain(mp_chain);
	return (NULL);
}

/*ARGSUSED*/
static void
vnic_m_ioctl(void *arg, queue_t *q, mblk_t *mp)
{
	miocnak(q, mp, 0, ENOTSUP);
}

/*
 * This entry point cannot be passed-through, since it is invoked
 * for the per-VNIC kstats which must be exported independently
 * of the existence of VNIC MAC clients.
 */
static int
vnic_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	vnic_t *vnic = arg;
	int rval = 0;

	if (vnic->vn_lower_mh == NULL) {
		/*
		 * It's an anchor VNIC, which does not have any
		 * statistics in itself.
		 */
		return (ENOTSUP);
	}

	/*
	 * ENOTSUP must be reported for unsupported stats, the VNIC
	 * driver reports a subset of the stats that would
	 * be returned by a real piece of hardware.
	 */

	switch (stat) {
	case MAC_STAT_LINK_STATE:
	case MAC_STAT_LINK_UP:
	case MAC_STAT_PROMISC:
	case MAC_STAT_IFSPEED:
	case MAC_STAT_MULTIRCV:
	case MAC_STAT_MULTIXMT:
	case MAC_STAT_BRDCSTRCV:
	case MAC_STAT_BRDCSTXMT:
	case MAC_STAT_OPACKETS:
	case MAC_STAT_OBYTES:
	case MAC_STAT_IERRORS:
	case MAC_STAT_OERRORS:
	case MAC_STAT_RBYTES:
	case MAC_STAT_IPACKETS:
		*val = mac_client_stat_get(vnic->vn_mch, stat);
		break;
	default:
		rval = ENOTSUP;
	}

	return (rval);
}

/*
 * Invoked by the upper MAC to retrieve the lower MAC client handle
 * corresponding to a VNIC. A pointer to this function is obtained
 * by the upper MAC via capability query.
 *
 * XXX-nicolas Note: this currently causes all VNIC MAC clients to
 * receive the same MAC client handle for the same VNIC. This is ok
 * as long as we have only one VNIC MAC client which sends and
 * receives data, but we don't currently enforce this at the MAC layer.
 */
static void *
vnic_mac_client_handle(void *vnic_arg)
{
	vnic_t *vnic = vnic_arg;

	return (vnic->vn_mch);
}


/*
 * Return information about the specified capability.
 */
/* ARGSUSED */
static boolean_t
vnic_m_capab_get(void *arg, mac_capab_t cap, void *cap_data)
{
	vnic_t *vnic = arg;

	switch (cap) {
	case MAC_CAPAB_HCKSUM: {
		uint32_t *hcksum_txflags = cap_data;

		*hcksum_txflags = vnic->vn_hcksum_txflags &
		    (HCKSUM_INET_FULL_V4 | HCKSUM_IPHDRCKSUM |
		    HCKSUM_INET_PARTIAL);
		break;
	}
	case MAC_CAPAB_VNIC: {
		mac_capab_vnic_t *vnic_capab = cap_data;

		if (vnic->vn_lower_mh == NULL) {
			/*
			 * It's an anchor VNIC, we don't have an underlying
			 * NIC and MAC client handle.
			 */
			return (B_FALSE);
		}

		if (vnic_capab != NULL) {
			vnic_capab->mcv_arg = vnic;
			vnic_capab->mcv_mac_client_handle =
			    vnic_mac_client_handle;
		}
		break;
	}
	case MAC_CAPAB_ANCHOR_VNIC: {
		/* since it's an anchor VNIC we don't have lower mac handle */
		if (vnic->vn_lower_mh == NULL) {
			ASSERT(vnic->vn_link_id == 0);
			return (B_TRUE);
		}
		return (B_FALSE);
	}
	case MAC_CAPAB_NO_NATIVEVLAN:
		return (B_FALSE);
	case MAC_CAPAB_NO_ZCOPY:
		return (B_TRUE);
	case MAC_CAPAB_VRRP: {
		mac_capab_vrrp_t *vrrp_capab = cap_data;

		if (vnic->vn_vrid != 0) {
			if (vrrp_capab != NULL)
				vrrp_capab->mcv_af = vnic->vn_af;
			return (B_TRUE);
		}
		return (B_FALSE);
	}
	case MAC_CAPAB_LSO: {
		mac_capab_lso_t	*cap_lso = cap_data;

		if (vnic->vn_lso_flags != 0) {
			cap_lso->lso_flags = vnic->vn_lso_flags;
			cap_lso->lso_basic_tcp_ipv4.lso_max = vnic->vn_lso_mss;
			return (B_TRUE);
		}
		return (B_FALSE);
	}
	case MAC_CAPAB_LINKID: {
		mac_capab_linkid_t *cap_lid = cap_data;

		cap_lid->mpl_linkid = vnic->vn_id;
		break;
	}
	default:
		return (B_FALSE);
	}
	return (B_TRUE);
}

/* ARGSUSED */
static int
vnic_m_start(void *arg)
{
	return (0);
}

/* ARGSUSED */
static void
vnic_m_stop(void *arg)
{
}

/* ARGSUSED */
static int
vnic_m_promisc(void *arg, boolean_t on)
{
	return (0);
}

/* ARGSUSED */
static int
vnic_m_multicst(void *arg, boolean_t add, const uint8_t *addrp)
{
	return (0);
}

static int
vnic_m_unicst(void *arg, const uint8_t *macaddr)
{
	vnic_t *vnic = arg;
	size_t addrlen;
	int err;

	err =  mac_vnic_unicast_set(vnic->vn_mch, macaddr, &addrlen);
	if (err == 0) {
		vnic->vn_addr_type = VNIC_MAC_ADDR_TYPE_FIXED;
		vnic->vn_addr_len = addrlen;
		bcopy(macaddr, vnic->vn_addr, vnic->vn_addr_len);
	}
	return (err);
}

/*
 * For non-etherstub VNICs, maximum possible MTU is bounded above by the
 * current lower MAC MTU (retrieved via mac_sdu_get()).  The lower MAC may
 * advertise a set of allowable MTU ranges, and all these ranges are valid for
 * the VNIC up to the current value of the lower MAC MTU.  For example, if the
 * lower MAC advertises 1500-1800, 8192-9000 as MTU ranges, but is currently
 * configured with an MTU of 8500, the valid ranges are 1500-1800, 8192-8500.
 *
 * If the mac prop info handle "prh" is NULL, this function just ensures that
 * the "mtu" argument falls into one of the valid ranges while also being less
 * than the lower MAC MTU.
 *
 * If prh is non-NULL, we ignore the "mtu" argument and simply add the valid
 * ranges to the mac propinfo via the propinfo handle "prh".
 */
static int
vnic_mtu_range_validate_or_set(vnic_t *vn, const char *prop_name, uint32_t mtu,
    mac_prop_info_handle_t prh)
{
	mac_propval_range_t		*ranges;
	mac_perim_handle_t		mph;
	int				merr, err = 0;
	uint_t				numranges, i;
	uint32_t			max_mtu;
	size_t				size = sizeof (mac_propval_range_t);
	boolean_t			mtu_in_valid_range = B_FALSE;

	if (vn->vn_link_id == DATALINK_INVALID_LINKID) {
		if (prh == NULL) {
			if (mtu > ANCHOR_VNIC_MAX_MTU ||
			    mtu < ANCHOR_VNIC_MIN_MTU)
				err = EINVAL;
		} else {
			mac_prop_info_set_range_uint32(prh, ANCHOR_VNIC_MIN_MTU,
			    ANCHOR_VNIC_MAX_MTU);
		}
		return (err);
	}

	mac_sdu_get(vn->vn_lower_mh, NULL, &max_mtu);

	ranges = kmem_zalloc(size, KM_SLEEP);

	mac_perim_enter_by_mh(vn->vn_lower_mh, &mph);
	merr = mac_prop_info(vn->vn_lower_mh, MAC_PROP_MTU,
	    (char *)prop_name, NULL, 0, ranges, NULL);
	mac_perim_exit(mph);

	switch (merr) {
	case 0:
		break;
	case ENOSPC:
		/*
		 * More than one range.  Allocate extra space and retry.
		 */
		numranges = ranges->mpr_count;
		kmem_free(ranges, size);
		size = sizeof (mac_propval_range_t) +
		    ((numranges - 1) * sizeof (mac_propval_uint32_range_t));
		ranges = kmem_zalloc(size, KM_SLEEP);
		ranges->mpr_count = numranges;
		mac_perim_enter_by_mh(vn->vn_lower_mh, &mph);
		merr = mac_prop_info(vn->vn_lower_mh, MAC_PROP_MTU,
		    (char *)prop_name, NULL, 0, ranges, NULL);
		mac_perim_exit(mph);
		ASSERT(merr != ENOSPC);
		break;
	default:
		/* Fall back to MTU range consisting of max SDU only. */
		kmem_free(ranges, size);
		if (prh == NULL) {
			if (mtu != max_mtu)
				err = EINVAL;
		} else {
			mac_prop_info_set_range_uint32(prh, max_mtu, max_mtu);
		}
		return (err);
	}

	/*
	 * Traverse set of ranges, either determining if mtu falls in the
	 * range or setting the range in the prop info.  If the upper bound of
	 * the range is greater than the lower MAC MTU, clamp the range maximum
	 * at the lower MAC MTU.
	 */
	for (i = 0; i < ranges->mpr_count; i++) {
		uint32_t range_min = ranges->mpr_range_uint32[i].mpur_min;
		uint32_t range_max = ranges->mpr_range_uint32[i].mpur_max;

		if (max_mtu < range_min)
			continue;

		if (max_mtu <= range_max)
			range_max = max_mtu;

		if (prh == NULL) {
			/* If MTU is in this range, we're done */
			if (mtu >= range_min && mtu <= range_max) {
				mtu_in_valid_range = B_TRUE;
				break;
			}
		} else {
			mac_prop_info_set_range_uint32(prh, range_min,
			    range_max);
		}
	}

	if (prh == NULL) {
		if (!mtu_in_valid_range)
			err = EINVAL;
	}
	kmem_free(ranges, size);

	return (err);
}

/*
 * Callback functions for set/get of properties
 */
static int
vnic_m_setprop(void *m_driver, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, const void *pr_val)
{
	int 		err = ENOTSUP;
	vnic_t		*vn = m_driver;
	uint32_t	mtu;

	switch (pr_num) {
	case MAC_PROP_MTU:
		if (pr_valsize < sizeof (mtu)) {
			err = EINVAL;
			break;
		}
		bcopy(pr_val, &mtu, sizeof (mtu));
		/* Validate MTU against lower MAC range(s), max SDU. */
		err = vnic_mtu_range_validate_or_set(vn, pr_name, mtu, NULL);
		if (err == 0)
			err = mac_maxsdu_update(vn->vn_mh, mtu);
		break;
	}
	return (err);
}

static void
vnic_m_propinfo(void *m_driver, const char *pr_name, mac_prop_id_t pr_num,
    mac_prop_info_handle_t prh)
{
	vnic_t		*vn = m_driver;

	switch (pr_num) {
	case MAC_PROP_MTU:
		/* Set MTU range(s) based on lower MAC, bounded by max SDU. */
		(void) vnic_mtu_range_validate_or_set(vn, pr_name, 0, prh);
		break;
	}
}

int
vnic_info(vnic_info_t *info, cred_t *credp)
{
	vnic_t		*vnic;
	int		err;

	/* Make sure that the VNIC link is visible from the caller's zone. */
	if (!dls_devnet_islinkvisible(info->vn_vnic_id, crgetzoneid(credp)))
		return (ENOENT);

	rw_enter(&vnic_lock, RW_WRITER);

	err = mod_hash_find(vnic_hash, VNIC_HASH_KEY(info->vn_vnic_id),
	    (mod_hash_val_t *)&vnic);
	if (err != 0) {
		rw_exit(&vnic_lock);
		return (ENOENT);
	}

	info->vn_link_id = vnic->vn_link_id;
	info->vn_owner_zone_id = vnic->vn_owner_zone_id;
	info->vn_zone_id = vnic->vn_zone_id;
	info->vn_mac_addr_type = vnic->vn_addr_type;
	info->vn_mac_len = vnic->vn_addr_len;
	bcopy(vnic->vn_addr, info->vn_mac_addr, MAXMACADDRLEN);
	info->vn_mac_slot = vnic->vn_slot_id;
	info->vn_mac_prefix_len = 0;
	info->vn_vid = vnic->vn_vid;
	info->vn_force = vnic->vn_force;
	info->vn_vrid = vnic->vn_vrid;
	info->vn_af = vnic->vn_af;

	bzero(&info->vn_resource_props, sizeof (mac_resource_props_t));
	if (vnic->vn_mch != NULL)
		mac_resource_ctl_get(vnic->vn_mch, &info->vn_resource_props);

	/* Check if the vnic is on loan from owner zone to the current zone */
	if (info->vn_owner_zone_id != info->vn_zone_id &&
	    info->vn_zone_id != GLOBAL_ZONEID) {
		err = dls_devnet_get_onloan(vnic->vn_mh, &info->vn_onloan);
		if (err != 0) {
			rw_exit(&vnic_lock);
			return (ENOENT);
		}
	}

	rw_exit(&vnic_lock);
	return (0);
}

static void
vnic_notify_cb(void *arg, mac_notify_type_t type)
{
	vnic_t *vnic = arg;

	/*
	 * Do not deliver notifications if the vnic is not fully initialized
	 * or is in process of being torn down.
	 */
	if (!vnic->vn_enabled)
		return;

	switch (type) {
	case MAC_NOTE_UNICST:
		/*
		 * Only the VLAN VNIC needs to be notified with primary MAC
		 * address change.
		 */
		if (vnic->vn_addr_type != VNIC_MAC_ADDR_TYPE_PRIMARY)
			return;

		/*  the unicast MAC address value */
		mac_unicast_primary_get(vnic->vn_lower_mh, vnic->vn_addr);

		/* notify its upper layer MAC about MAC address change */
		mac_unicst_update(vnic->vn_mh, (const uint8_t *)vnic->vn_addr);
		break;

	case MAC_NOTE_LINK:
		mac_link_update(vnic->vn_mh,
		    mac_client_stat_get(vnic->vn_mch, MAC_STAT_LINK_STATE));
		break;

	case MAC_NOTE_CAPAB_CHG:
		if (vnic_capab_get(vnic))
			mac_capab_update(vnic->vn_mh);
		break;

	default:
		break;
	}
}

#define	NO_SLOT	-2

/*
 * Create a new VNIC/VLAN upon request from a kernel client.
 * Returns 0 on success, an errno on failure.
 */
int
vnic_create(const char *vnic_name, const char *link_name,
    vnic_mac_addr_type_t *vnic_addr_type, int *mac_len,
    uchar_t *mac_addr, int *mac_slot, uint_t mac_prefix_len,
    uint16_t vid, uint32_t flags, datalink_id_t *vnic_linkid_out,
    vnic_ioc_diag_t *diag, void *reserved)
{
	_NOTE(ARGUNUSED(reserved));
	int rv;
	dev_t pdevt;
	datalink_id_t plinkid;
	datalink_id_t vnic_linkid;
	dev_info_t *dip;
	datalink_class_t class;
	cred_t *credp;

	if ((flags & MAC_VLAN) && (vid < 1 || vid > 4094)) {
		*diag = VNIC_IOC_DIAG_VLANID_INVALID;
		return (EINVAL);
	}

	rv = vnic_check_args(*vnic_addr_type, *mac_len,
	    ((mac_slot != NULL) ? *mac_slot : NO_SLOT),
	    mac_prefix_len, diag);
	if (rv != 0) {
		return (rv);
	}

	if (dls_mgmt_get_linkid(vnic_name, &vnic_linkid) == 0) {
		*diag = VNIC_IOC_DIAG_VNIC_EXISTS;
		return (EEXIST);
	}

	if ((rv = dls_mgmt_get_linkid(link_name, &plinkid)) != 0) {
		return (rv);
	}

	if ((rv = dls_devnet_phydev(plinkid, &pdevt)) != 0) {
		return (rv);
	}

	/*
	 * Make sure the VNIC pseudo device node is attached. This is needed
	 * since there could be no VNICs created before to trigger an attach.
	 * Only instance 0 is allowed to attach for VNIC device.
	 */
	dip = ddi_hold_devi_by_instance(ddi_name_to_major("vnic"), 0, 0);
	if (dip == NULL) {
		return (ENODEV);
	}

	ASSERT((flags & MAC_VLAN) == 0 ||
	    *vnic_addr_type == VNIC_MAC_ADDR_TYPE_PRIMARY);
	class = ((flags & MAC_VLAN) != 0 ?
	    DATALINK_CLASS_VLAN : DATALINK_CLASS_VNIC);

	/* Obtain a datalink id for the VNIC/VLAN */
	rv = dls_mgmt_create(vnic_name, pdevt, class,
	    DL_ETHER, B_FALSE, B_FALSE, &vnic_linkid);
	if (rv != 0) {
		ddi_release_devi(dip);
		return (rv);
	}

	credp = zone_kcred();
	rv = vnic_dev_create(vnic_linkid, plinkid, crgetzoneid(credp),
	    vnic_addr_type, mac_len, mac_addr, mac_slot, mac_prefix_len,
	    vid, 0, 0, NULL, flags, diag, credp);
	if (rv != 0) {
		if (dls_mgmt_destroy(vnic_linkid, B_FALSE) != 0) {
			cmn_err(CE_WARN,
			    "vnic_create: failed to destroy link %s",
			    vnic_name);
		}
		ddi_release_devi(dip);
		return (rv);
	}

	*vnic_linkid_out = vnic_linkid;
	ddi_release_devi(dip);

	return (0);
}

int
vnic_delete(datalink_id_t vnic_linkid, uint32_t flags)
{
	int rv;

	rv = vnic_dev_delete(vnic_linkid, flags, zone_kcred());
	if (rv != 0) {
		return (rv);
	}

	rv = dls_mgmt_destroy(vnic_linkid, B_FALSE);
	if (rv != 0) {
		return (rv);
	}

	return (0);
}

/*
 * Modify the MAC address of a VNIC.
 */
int
vnic_modify_addr(datalink_id_t vnic_linkid,
    vnic_mac_addr_type_t *vnic_addr_type, int *mac_len,
    uchar_t *mac_addr, int *mac_slot, uint_t mac_prefix_len,
    vnic_ioc_diag_t *diag)
{
	int rv;

	rv = vnic_check_args(*vnic_addr_type, *mac_len,
	    ((mac_slot != NULL) ? *mac_slot : NO_SLOT),
	    mac_prefix_len, diag);
	if (rv != 0) {
		return (rv);
	}

	rv = vnic_dev_modify(vnic_linkid, VNIC_IOC_MODIFY_ADDR,
	    vnic_addr_type, mac_len, mac_addr, mac_slot, mac_prefix_len,
	    NULL, diag);
	if (rv != 0) {
		return (rv);
	}

	return (0);
}

/*
 * Update VNIC's capabilities. Returns B_TRUE if any of the capabilities
 * have changed, B_FALSE otherwise.
 */
boolean_t
vnic_capab_get(vnic_t *vnic)
{
	uint32_t hcksum_txflags = vnic->vn_hcksum_txflags;

	if (vnic->vn_vid == 0 || !mac_capab_get(vnic->vn_lower_mh,
	    MAC_CAPAB_NO_NATIVEVLAN, NULL)) {
		if (!mac_capab_get(vnic->vn_lower_mh, MAC_CAPAB_HCKSUM,
		    &vnic->vn_hcksum_txflags))
			vnic->vn_hcksum_txflags = 0;
	} else {
		vnic->vn_hcksum_txflags = 0;
	}

	if (vnic->vn_hcksum_txflags != hcksum_txflags)
		return (B_TRUE);

	return (B_FALSE);
}
