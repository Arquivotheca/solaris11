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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This header file contains the data structures which the
 * virtual switch (vsw) uses to communicate with its clients and
 * the outside world.
 */

#ifndef	_VSW_H
#define	_VSW_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/vio_mailbox.h>
#include <sys/vnet_common.h>
#include <sys/ethernet.h>
#include <sys/mac_client.h>
#include <sys/vio_util.h>
#include <sys/vgen_stats.h>
#include <sys/vsw_ldc.h>
#include <sys/vsw_hio.h>
#include <sys/callb.h>

#define	DRV_NAME	"vsw"

/*
 * Only support ETHER mtu at moment.
 */
#define	VSW_MTU		ETHERMAX

/* ID of the source of a frame being switched */
#define	VSW_PHYSDEV		1	/* physical device associated */
#define	VSW_VNETPORT		2	/* port connected to vnet (over ldc) */
#define	VSW_LOCALDEV		4	/* vsw configured as an eth interface */

/*
 * Number of hash chains in the multicast forwarding database.
 */
#define		VSW_NCHAINS	8

/* Number of descriptors -  must be power of 2 */
#define		VSW_NUM_DESCRIPTORS	512

/*
 * State of interface if switch plumbed as network device.
 */
#define		VSW_IF_REG	0x1	/* interface was registered */
#define		VSW_IF_UP	0x2	/* Interface UP */
#define		VSW_IF_PROMISC	0x4	/* Interface in promiscious mode */

#define		VSW_U_P(state)	\
			(state == (VSW_IF_UP | VSW_IF_PROMISC))

/*
 * Switching modes.
 */
#define		VSW_LAYER2		0x1	/* Layer 2 - MAC switching */
#define		VSW_LAYER2_PROMISC	0x2	/* Layer 2 + promisc mode */
#define		VSW_LAYER3		0x4	/* Layer 3 - IP switching */

#define		NUM_SMODES	3	/* number of switching modes */

#define	VSW_PRI_ETH_DEFINED(vswp)	((vswp)->pri_num_types != 0)

typedef enum {
	VSW_SWTHR_STOP = 0x1
} sw_thr_flags_t;

typedef enum {
	PROG_init = 0x00,
	PROG_locks = 0x01,
	PROG_readmd = 0x02,
	PROG_fdb = 0x04,
	PROG_mfdb = 0x08,
	PROG_taskq = 0x10,
	PROG_rxp_taskq = 0x20,
	PROG_swmode = 0x40,
	PROG_macreg = 0x80,
	PROG_mdreg = 0x100
} vsw_attach_progress_t;

/*
 * vlan-id information.
 */
typedef struct vsw_vlanid {
	uint16_t		vl_vid;		/* vlan-id */
	mac_unicast_handle_t	vl_muh;		/* mac unicast handle */
	boolean_t		vl_set;		/* set? */
} vsw_vlanid_t;

/*
 * vsw instance state information.
 */
typedef struct	vsw {
	int			instance;	/* instance # */
	dev_info_t		*dip;		/* associated dev_info */
	uint64_t		regprop;	/* "reg" property */
	vsw_attach_progress_t	attach_progress; /* attach progress flags */
	struct vsw		*next;		/* next in list */
	char			physname[LIFNAMSIZ];	/* phys-dev */
	uint8_t			smode;		/* switching mode */
	kmutex_t		sw_thr_lock;	/* setup switching thr lock */
	kcondvar_t		sw_thr_cv;	/* cv for setup switching thr */
	kthread_t		*sw_thread;	/* setup switching thread */
	sw_thr_flags_t		sw_thr_flags; 	/* setup switching thr flags */
	uint32_t		switching_setup_done; /* setup switching done */
	int			mac_open_retries; /* mac_open() retry count */
	vsw_port_list_t		plist;		/* associated ports */
	ddi_taskq_t		*taskq_p;	/* VIO ctrl msg taskq */
	mod_hash_t		*fdb_hashp;	/* forwarding database */
	uint32_t		fdb_nchains;	/* # of hash chains in fdb */
	mod_hash_t		*vlan_hashp;	/* vlan hash table */
	uint32_t		vlan_nchains;	/* # of vlan hash chains */
	uint32_t		mtu;		/* mtu of the device */
	uint32_t		max_frame_size;	/* max frame size supported */
	uint32_t		mtu_physdev_orig; /* orig mtu of the physdev */

	mod_hash_t		*mfdb;		/* multicast FDB */
	krwlock_t		mfdbrw;		/* rwlock for mFDB */

	ddi_taskq_t		*rxp_taskq;	/* VIO rx pool taskq */
	void			(*vsw_switch_frame)
					(struct vsw *, mblk_t *, int,
					vsw_port_t *, mac_resource_handle_t);

	/* mac layer */
	kmutex_t		mac_lock;	/* protect mh */
	mac_handle_t		mh;
	krwlock_t		maccl_rwlock;	/* protect fields below */
	mac_client_handle_t	mch;		/* mac client handle */
	mac_unicast_handle_t	muh;		/* mac unicast handle */
	mac_notify_handle_t	mnh;		/* mac notify handle */

	boolean_t		recfg_reqd;	/* Reconfig of addrs needed */

	/* mac layer switching flag */
	boolean_t		mac_cl_switching;

	/* Machine Description updates  */
	mdeg_node_spec_t	*inst_spec;
	mdeg_handle_t		mdeg_hdl;
	mdeg_handle_t		mdeg_port_hdl;

	/* if configured as an ethernet interface */
	mac_handle_t		if_mh;		/* MAC handle */
	struct ether_addr	if_addr;	/* interface address */
	krwlock_t		if_lockrw;
	uint8_t			if_state;	/* interface state */

	boolean_t		addr_set;	/* is addr set to HW */

	/* multicast addresses when configured as eth interface */
	kmutex_t		mca_lock;	/* multicast lock */
	mcst_addr_t		*mcap;		/* list of multicast addrs */

	uint32_t		pri_num_types;	/* # of priority eth types */
	uint16_t		*pri_types;	/* priority eth types */
	vio_mblk_pool_t		*pri_tx_vmp;	/* tx priority mblk pool */
	uint16_t		default_vlan_id; /* default vlan id */
	uint16_t		pvid;	/* port vlan id (untagged) */
	vsw_vlanid_t		*vids;	/* vlan ids (tagged) */
	uint16_t		nvids;	/* # of vids */
	uint32_t		vids_size; /* size alloc'd for vids list */

	/* HybridIO related fields */
	boolean_t		hio_capable;	/* Phys dev HIO capable */
	vsw_hio_t		vhio;		/* HybridIO info */
	callb_id_t		hio_reboot_cb_id; /* Reboot callb ID */
	callb_id_t		hio_panic_cb_id; /* Panic callb ID */

	/* Link-state related fields */
	boolean_t		phys_no_link_update; /* no link-update supp */
	boolean_t		pls_update;	/* phys link state update ? */
	link_state_t		phys_link_state;    /* physical link state */

	/* bandwidth related fields */
	uint64_t		bandwidth;	/* bandwidth limit */
} vsw_t;

/*
 * The flags that are used by vsw_mac_rx().
 */
typedef enum {
	VSW_MACRX_PROMISC = 0x01,
	VSW_MACRX_COPYMSG = 0x02,
	VSW_MACRX_FREEMSG = 0x04
} vsw_macrx_flags_t;


#ifdef DEBUG

extern int vswdbg;
extern void vswdebug(vsw_t *vswp, const char *fmt, ...);

#define	D1(...)		\
if (vswdbg & 0x01)	\
	vswdebug(__VA_ARGS__)

#define	D2(...)		\
if (vswdbg & 0x02)	\
	vswdebug(__VA_ARGS__)

#define	D3(...)		\
if (vswdbg & 0x04)	\
	vswdebug(__VA_ARGS__)

#define	DWARN(...)	\
if (vswdbg & 0x08)	\
	vswdebug(__VA_ARGS__)

#define	DERR(...)	\
if (vswdbg & 0x10)	\
	vswdebug(__VA_ARGS__)

#else

#define	DERR(...)	if (0)	do { } while (0)
#define	DWARN(...)	if (0)	do { } while (0)
#define	D1(...)		if (0)	do { } while (0)
#define	D2(...)		if (0)	do { } while (0)
#define	D3(...)		if (0)	do { } while (0)

#endif	/* DEBUG */


#ifdef	__cplusplus
}
#endif

#endif	/* _VSW_H */
