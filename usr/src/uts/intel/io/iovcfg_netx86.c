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

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/sysmacros.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/pci_param.h>
#include <sys/ethernet.h>
#include <sys/vlan.h>
#include <sys/mac.h>
#include <sys/mac_client.h>
#include <sys/iovcfg.h>
#include <sys/iovcfg_net.h>

/* Exported functions */
int iovcfg_read_props_net(iov_vf_t *vfp, void *arg2, void *arg3);

static char vfid_propname[] = "vf-id";
static char pri_macaddr_propname[] = "primary-mac-addr";
static char pvid_propname[] = "port-vlan-id";
static char vid_propname[] = "vlan-id";
static char bandwidth_propname[] = "bandwidth";
static char alt_macaddr_propname[] = "alt-mac-addrs";
static char mtu_propname[] = "mtu";

/*
 * Register plat specific reconfig callbacks for all VFs under the given PF.
 * NOP in x86 for now.
 */
/* ARGSUSED */
void
iovcfg_reconfig_reg_pf_net(iov_pf_t *pfp)
{
}

/*
 * Read class specific properties of the VF.
 */
/* ARGSUSED */
int
iovcfg_read_props_net(iov_vf_t *vfp, void *arg2, void *arg3)
{
	int			i, j, nelem;
	int			rv;
	uint64_t		val;
	uint64_t		*val_array;
	struct ether_addr	ea;
	pci_plist_t		vfplist = NULL;
	iov_vf_net_t		*vnp;
	iov_vlan_t		*vidp;

	ASSERT(vfp != NULL);
	vnp = (iov_vf_net_t *)vfp->ivf_cl_data;
	/*
	 * Default values
	 */
	vnp->ivf_num_alt_macaddr = 0;
	vnp->ivf_nvids = 0;
	vnp->ivf_alt_macaddr = NULL;
	vnp->ivf_pvid_altp = NULL;
	vnp->ivf_bandwidth = 0;
	vnp->ivf_pvid = VLAN_ID_NONE;
	vnp->ivf_mtu = IOVCFG_MTU_UNSPECIFIED;
	/* Read primary macaddr */
	rv = pciv_plist_getvf(vfp->ivf_pfp->ipf_params, vfp->ivf_id,
	    &vfplist);
	if (vfplist == NULL)
		return (0);
	rv = pciv_plist_lookup(vfplist, pri_macaddr_propname,
	    PCI_PARAM_DATA_TYPE_UINT64, &val, NULL);
	if (rv == 0) {
		DBGx86("Found primary MAC address 0x%llx for VF device %s\n",
		    (long long unsigned)val, vfp->ivf_pathname);
		for (i = ETHERADDRL - 1; i >= 0; i--) {
			ea.ether_addr_octet[i] = val & 0xFF;
			val >>= 8;
		}
	}
	ether_copy(&ea, &vnp->ivf_macaddr);

	/* Read alternate macaddrs */
	nelem = 0;
	rv = pciv_plist_lookup(vfplist, alt_macaddr_propname,
	    PCI_PARAM_DATA_TYPE_UINT64_ARRAY, &val_array, &nelem, NULL);
	if ((rv == 0) && (nelem > 0)) {
		vnp->ivf_num_alt_macaddr = nelem;
		DBGx86("alternate MAC address for VF device %s:\n",
		    vfp->ivf_pathname);
		vnp->ivf_alt_macaddr = kmem_zalloc(sizeof (ea) * nelem,
		    KM_SLEEP);
		for (i = 0; i < nelem; i++) {
			DBGx86("0x%llx, ", (long long unsigned)val_array[i]);
			for (j = ETHERADDRL - 1; j >= 0; j--) {
				ea.ether_addr_octet[j]  = val_array[i] & 0xFF;
				val_array[i] >>= 8;
			}
			ether_copy(&ea, &vnp->ivf_alt_macaddr[i]);
		}
		iovcfg_alloc_def_alt_table(vfp);
	}

	/* Read PVID */
	rv = pciv_plist_lookup(vfplist, pvid_propname,
	    PCI_PARAM_DATA_TYPE_UINT64, &val, NULL);
	if (rv == 0) {
		vnp->ivf_pvid = val & 0xFFF;
		iovcfg_alloc_pvid_alt_table(vfp);
	}

	/* Read VLAN IDs */
	nelem = 0;
	rv = pciv_plist_lookup(vfplist, vid_propname,
	    PCI_PARAM_DATA_TYPE_UINT64_ARRAY, &val_array, &nelem, NULL);
	if ((rv == 0) && (nelem > 0)) {
		DBGx86("VLAN ids for VF device %s:\n",
		    vfp->ivf_pathname);
		vnp->ivf_vids =  kmem_zalloc(sizeof (iov_vlan_t) * nelem,
		    KM_SLEEP);
		for (i = 0; i < nelem; i++) {
			vidp = &vnp->ivf_vids[i];
			vidp->ivl_vid = val_array[i] & 0xFFFF;
			DBGx86("0x%x, ", val_array[i]);
		}
		vnp->ivf_nvids = nelem;
		iovcfg_alloc_vid_alt_table(vfp);
	}

	/* Read bandwidth limit */
	rv = pciv_plist_lookup(vfplist, bandwidth_propname,
	    PCI_PARAM_DATA_TYPE_UINT64, &val, NULL);
	if (rv == 0) {
		DBGx86("Found bandwidth 0x%x for VF device %s\n",
		    val, vfp->ivf_pathname);
		vnp->ivf_bandwidth = val;
	}

	/* Read MTU */
	rv = pciv_plist_lookup(vfplist, mtu_propname,
	    PCI_PARAM_DATA_TYPE_UINT64, &val, NULL);
	if (rv == 0) {
		DBGx86("Found mtu 0x%x for VF device %s\n",
		    (uint32_t)val, vfp->ivf_pathname);
		vnp->ivf_mtu = (uint32_t)val;
	}

	return (0);
}

#ifdef IOVCFG_UNCONFIG_SUPPORTED

/*
 * Unregister mdeg callbacks for all VFs under the given PF.
 * NOP in x86 for now.
 */
/* ARGSUSED */
void
iovcfg_reconfig_unreg_pf_net(iov_pf_t *pfp)
{
}

#endif
