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
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/stream.h>
#include <sys/kstat.h>
#include <sys/mac.h>
#include <sys/mac_impl.h>
#include <sys/mac_client_impl.h>
#include <sys/mac_stat.h>
#include <sys/vlan.h>

#define	MAC_PHYS_KSTAT_NAME "phys"
#define	MAC_LINK_KSTAT_NAME "link"
#define	MAC_KSTAT_CLASS	"net"

enum mac_stat {
	MAC_STAT_RXLCL,
	MAC_STAT_RXLCLBYTES,
	MAC_STAT_TXLCL,
	MAC_STAT_TXLCLBYTES,
	MAC_STAT_INTRS,
	MAC_STAT_INTRBYTES,
	MAC_STAT_POLLS,
	MAC_STAT_POLLBYTES,
	MAC_STAT_IDROPS,
	MAC_STAT_IDROPBYTES,
	MAC_STAT_CHU10,
	MAC_STAT_CH10T50,
	MAC_STAT_CHO50,
	MAC_STAT_BLOCK,
	MAC_STAT_UNBLOCK,
	MAC_STAT_ODROPS,
	MAC_STAT_ODROPBYTES,
	MAC_STAT_TX_ERRORS,
	MAC_STAT_MACSPOOFED,
	MAC_STAT_IPSPOOFED,
	MAC_STAT_DHCPSPOOFED,
	MAC_STAT_RESTRICTED,
	MAC_STAT_DHCPDROPPED,
	MAC_STAT_MULTIRCVBYTES,
	MAC_STAT_BCSTRCVBYTES,
	MAC_STAT_MULTIXMTBYTES,
	MAC_STAT_BCSTXMTBYTES
};

static mac_stat_info_t	i_mac_si[] = {
	{ MAC_STAT_IFSPEED,	"ifspeed",	KSTAT_DATA_UINT64,	0 },
	{ MAC_STAT_MULTIRCV,	"multircv",	KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_BRDCSTRCV,	"brdcstrcv",	KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_MULTIXMT,	"multixmt",	KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_BRDCSTXMT,	"brdcstxmt",	KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_NORCVBUF,	"norcvbuf",	KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_IERRORS,	"ierrors",	KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_UNKNOWNS,	"unknowns",	KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_NOXMTBUF,	"noxmtbuf",	KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_OERRORS,	"oerrors",	KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_COLLISIONS,	"collisions",	KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_UNDERFLOWS,	"uflo",		KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_OVERFLOWS,	"oflo",		KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_RBYTES,	"rbytes",	KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_IPACKETS,	"ipackets",	KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_OBYTES,	"obytes",	KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_OPACKETS,	"opackets",	KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_RBYTES,	"rbytes64",	KSTAT_DATA_UINT64,	0 },
	{ MAC_STAT_IPACKETS,	"ipackets64",	KSTAT_DATA_UINT64,	0 },
	{ MAC_STAT_OBYTES,	"obytes64",	KSTAT_DATA_UINT64,	0 },
	{ MAC_STAT_OPACKETS,	"opackets64",	KSTAT_DATA_UINT64,	0 }
};
#define	MAC_NKSTAT \
	(sizeof (i_mac_si) / sizeof (mac_stat_info_t))

static mac_stat_info_t	i_mac_mod_si[] = {
	{ MAC_STAT_LINK_STATE,	"link_state",	KSTAT_DATA_UINT32,
	    (uint64_t)LINK_STATE_UNKNOWN },
	{ MAC_STAT_LINK_UP,	"link_up",	KSTAT_DATA_UINT32,	0 },
	{ MAC_STAT_PROMISC,	"promisc",	KSTAT_DATA_UINT32,	0 }
};
#define	MAC_MOD_NKSTAT \
	(sizeof (i_mac_mod_si) / sizeof (mac_stat_info_t))

#define	MAC_MOD_KSTAT_OFFSET	0
#define	MAC_KSTAT_OFFSET	MAC_MOD_KSTAT_OFFSET + MAC_MOD_NKSTAT
#define	MAC_TYPE_KSTAT_OFFSET	MAC_KSTAT_OFFSET + MAC_NKSTAT


typedef struct {
	uint_t		mse_stat;
	const char	*mse_name;
	uint_t		mse_offset;
} mac_stats_entry_t;

/*
 * Definitions for per rx ring statistics
 */
static mac_stats_entry_t  mac_rx_ring_list[] = {
	{ MAC_STAT_RBYTES,	"rbytes",	0},
	{ MAC_STAT_IPACKETS,	"ipackets",	0}
};
#define	MAC_RX_RING_SIZE \
	(sizeof (mac_rx_ring_list) / sizeof (mac_stats_entry_t))

/*
 * Definitions for per tx ring statistics
 */
static mac_stats_entry_t  mac_tx_ring_list[] = {
	{ MAC_STAT_OBYTES,	"obytes",	0},
	{ MAC_STAT_OPACKETS,	"opackets",	0}
};
#define	MAC_TX_RING_SIZE \
	(sizeof (mac_tx_ring_list) / sizeof (mac_stats_entry_t))

/*
 * Definitions for per hardware lane rx statistics
 */
#define	MRS_OFF(r)	(offsetof(mac_rx_stats_t, r))
static mac_stats_entry_t mac_rx_hwlane_list[] = {
	{ MAC_STAT_RBYTES,	"rbytes",	MRS_OFF(mrs_rbytes)},
	{ MAC_STAT_IPACKETS,	"ipackets",	MRS_OFF(mrs_ipackets)},
	{ MAC_STAT_INTRS,	"intrs",	MRS_OFF(mrs_intrcnt)},
	{ MAC_STAT_INTRBYTES,	"intrbytes",	MRS_OFF(mrs_intrbytes)},
	{ MAC_STAT_POLLS,	"polls",	MRS_OFF(mrs_pollcnt)},
	{ MAC_STAT_POLLBYTES,	"pollbytes",	MRS_OFF(mrs_pollbytes)},
	{ MAC_STAT_IDROPS,	"idrops",	MRS_OFF(mrs_idropcnt)},
	{ MAC_STAT_IDROPBYTES,	"idropbytes",	MRS_OFF(mrs_idropbytes)}
};
#define	MAC_RX_HWLANE_SIZE \
	(sizeof (mac_rx_hwlane_list) / sizeof (mac_stats_entry_t))

/*
 * Definitions for per hardware lane tx statistics
 */
#define	MTS_OFF(t)	(offsetof(mac_tx_stats_t, t))
static mac_stats_entry_t  mac_tx_hwlane_list[] = {
	{ MAC_STAT_OBYTES,	"obytes",	MTS_OFF(mts_obytes)},
	{ MAC_STAT_OPACKETS,	"opackets",	MTS_OFF(mts_opackets)},
	{ MAC_STAT_OERRORS,	"oerrors",	MTS_OFF(mts_oerrors)},
	{ MAC_STAT_BLOCK,	"blockcnt",	MTS_OFF(mts_driver_blockcnt)},
	{ MAC_STAT_UNBLOCK,	"unblockcnt",	MTS_OFF(mts_driver_unblockcnt)},
	{ MAC_STAT_ODROPS,	"odrops",	MTS_OFF(mts_odropcnt)},
	{ MAC_STAT_ODROPBYTES,	"odropbytes",	MTS_OFF(mts_odropbytes)}
};
#define	MAC_TX_HWLANE_SIZE \
	(sizeof (mac_tx_hwlane_list) / sizeof (mac_stats_entry_t))

/*
 * Definitions for misc statistics
 */
#define	MMS_OFF(m)	(offsetof(mac_misc_stats_t, m))
static mac_stats_entry_t  mac_misc_list[] = {
	{ MAC_STAT_RXLCL,	"rxlocal",	MMS_OFF(mms_rxlocalcnt)},
	{ MAC_STAT_RXLCLBYTES,	"rxlocalbytes",	MMS_OFF(mms_rxlocalbytes)},
	{ MAC_STAT_TXLCL,	"txlocal",	MMS_OFF(mms_txlocalcnt)},
	{ MAC_STAT_TXLCLBYTES,	"txlocalbytes",	MMS_OFF(mms_txlocalbytes)},
	{ MAC_STAT_MULTIRCV,	"multircv",	MMS_OFF(mms_multircv)},
	{ MAC_STAT_BRDCSTRCV,	"brdcstrcv",	MMS_OFF(mms_brdcstrcv)},
	{ MAC_STAT_MULTIXMT,	"multixmt",	MMS_OFF(mms_multixmt)},
	{ MAC_STAT_BRDCSTXMT,	"brdcstxmt",	MMS_OFF(mms_brdcstxmt)},
	{ MAC_STAT_MULTIRCVBYTES, "multircvbytes", MMS_OFF(mms_multircvbytes)},
	{ MAC_STAT_BCSTRCVBYTES, "bcstrcvbytes", MMS_OFF(mms_brdcstrcvbytes)},
	{ MAC_STAT_MULTIXMTBYTES, "multixmtbytes", MMS_OFF(mms_multixmtbytes)},
	{ MAC_STAT_BCSTXMTBYTES, "bcstxmtbytes", MMS_OFF(mms_brdcstxmtbytes)},
	{ MAC_STAT_TX_ERRORS,	"txerrors",	MMS_OFF(mms_txerrors)},
	{ MAC_STAT_MACSPOOFED,	"macspoofed",	MMS_OFF(mms_macspoofed)},
	{ MAC_STAT_IPSPOOFED,	"ipspoofed",	MMS_OFF(mms_ipspoofed)},
	{ MAC_STAT_DHCPSPOOFED,	"dhcpspoofed",	MMS_OFF(mms_dhcpspoofed)},
	{ MAC_STAT_RESTRICTED,	"restricted",	MMS_OFF(mms_restricted)},
	{ MAC_STAT_DHCPDROPPED,	"dhcpdropped",	MMS_OFF(mms_dhcpdropped)}
};
#define	MAC_MISC_SIZE \
	(sizeof (mac_misc_list) / sizeof (mac_stats_entry_t))

/*
 * Private functions.
 */

static int
i_mac_driver_stat_update(kstat_t *ksp, int rw)
{
	mac_impl_t	*mip = ksp->ks_private;
	kstat_named_t	*knp = ksp->ks_data;
	uint_t		i;
	uint64_t	val;
	mac_stat_info_t	*msi;
	uint_t		msi_index;

	if (rw != KSTAT_READ)
		return (EACCES);

	for (i = 0; i < mip->mi_kstat_count; i++, msi_index++) {
		if (i == MAC_MOD_KSTAT_OFFSET) {
			msi_index = 0;
			msi = i_mac_mod_si;
		} else if (i == MAC_KSTAT_OFFSET) {
			msi_index = 0;
			msi = i_mac_si;
		} else if (i == MAC_TYPE_KSTAT_OFFSET) {
			msi_index = 0;
			msi = mip->mi_type->mt_stats;
		}

		val = mac_stat_get((mac_handle_t)mip, msi[msi_index].msi_stat);
		switch (msi[msi_index].msi_type) {
		case KSTAT_DATA_UINT64:
			knp->value.ui64 = val;
			break;
		case KSTAT_DATA_UINT32:
			knp->value.ui32 = (uint32_t)val;
			break;
		default:
			ASSERT(B_FALSE);
			break;
		}

		knp++;
	}

	return (0);
}

static void
i_mac_kstat_init(kstat_named_t *knp, mac_stat_info_t *si, uint_t count)
{
	int i;
	for (i = 0; i < count; i++) {
		kstat_named_init(knp, si[i].msi_name, si[i].msi_type);
		knp++;
	}
}

/*
 * Create kstat with given name - statname, update function - fn
 * and initialize it with given names - init_stat_info
 */
static kstat_t *
i_mac_stat_create(void *handle, const char *modname, int instance,
    const char *statname, int (*fn) (kstat_t *, int),
    mac_stats_entry_t *init_stat_info, uint_t size, zoneid_t zoneid)
{
	kstat_t		*ksp;
	kstat_named_t	*knp;
	int		i;

	ksp = kstat_create_zone(modname, instance, statname, MAC_KSTAT_CLASS,
	    KSTAT_TYPE_NAMED, size, 0, zoneid);

	if (ksp == NULL)
		return (NULL);

	ksp->ks_update = fn;
	ksp->ks_private = handle;

	knp = (kstat_named_t *)ksp->ks_data;

	for (i = 0; i < size; i++) {
		kstat_named_init(knp, init_stat_info[i].mse_name,
		    KSTAT_DATA_UINT64);
		knp++;
	}

	kstat_install(ksp);

	return (ksp);
}

/*
 * Per rx ring statistics
 */
uint64_t
mac_rx_ring_stat_get(void *handle, uint_t stat)
{
	mac_ring_t		*ring = (mac_ring_t *)handle;
	uint64_t		val = 0;

	ASSERT(ring->mr_stat != NULL);
	ring->mr_stat(ring->mr_driver, stat, &val);

	return (val);
}

static int
i_mac_rx_ring_stat_update(kstat_t *ksp, int rw)
{
	kstat_named_t	*knp = ksp->ks_data;
	int		i;

	if (rw != KSTAT_READ)
		return (EACCES);

	for (i = 0; i < MAC_RX_RING_SIZE; i++, knp++) {
		knp->value.ui64 = mac_rx_ring_stat_get(ksp->ks_private,
		    mac_rx_ring_list[i].mse_stat);
	}

	return (0);
}

static void
i_mac_rx_ring_stat_create(mac_ring_t *ring, const char *modname,
    const char *statname)
{
	kstat_t		*ksp;
	zoneid_t	zoneid = ring->mr_mip->mi_zoneid;

	ksp = i_mac_stat_create(ring, modname, 0, statname,
	    i_mac_rx_ring_stat_update, mac_rx_ring_list,
	    MAC_RX_RING_SIZE, zoneid);
	ring->mr_ksp = ksp;

	/* Create one for GZ using NGZ's zoneid as the instance */
	if (zoneid == GLOBAL_ZONEID)
		return;

	ksp = i_mac_stat_create(ring, modname, zoneid, statname,
	    i_mac_rx_ring_stat_update, mac_rx_ring_list,
	    MAC_RX_RING_SIZE, GLOBAL_ZONEID);
	ring->mr_gz_ksp = ksp;
}

/*
 * Per tx ring statistics
 */
uint64_t
mac_tx_ring_stat_get(void *handle, uint_t stat)
{
	mac_ring_t		*ring = (mac_ring_t *)handle;
	uint64_t		val = 0;

	ASSERT(ring->mr_stat != NULL);
	ring->mr_stat(ring->mr_driver, stat, &val);

	return (val);
}

static int
i_mac_tx_ring_stat_update(kstat_t *ksp, int rw)
{
	kstat_named_t	*knp = ksp->ks_data;
	int		i;

	if (rw != KSTAT_READ)
		return (EACCES);

	for (i = 0; i < MAC_TX_RING_SIZE; i++, knp++) {
		knp->value.ui64 = mac_tx_ring_stat_get(ksp->ks_private,
		    mac_tx_ring_list[i].mse_stat);
	}
	return (0);
}

static void
i_mac_tx_ring_stat_create(mac_ring_t *ring, const char *modname,
    const char *statname)
{
	kstat_t		*ksp;
	zoneid_t	zoneid = ring->mr_mip->mi_zoneid;

	ksp = i_mac_stat_create(ring, modname, 0, statname,
	    i_mac_tx_ring_stat_update, mac_tx_ring_list,
	    MAC_TX_RING_SIZE, zoneid);
	ring->mr_ksp = ksp;

	if (zoneid == GLOBAL_ZONEID)
		return;

	ksp = i_mac_stat_create(ring, modname, zoneid, statname,
	    i_mac_tx_ring_stat_update, mac_tx_ring_list,
	    MAC_TX_RING_SIZE, GLOBAL_ZONEID);
	ring->mr_gz_ksp = ksp;
}

/*
 * Per hardware lane rx statistics
 */
/* ARGSUSED */
static int
i_mac_rx_hwlane_stat_update(kstat_t *ksp, int rw)
{
	kstat_named_t	*knp = ksp->ks_data;
	mac_ring_t	*ring = ksp->ks_private;
	mac_rx_stats_t	*mac_rx_stat = &ring->mr_rx_stat;
	uint_t		i;
	uint64_t	*statp;

	if (rw != KSTAT_READ)
		return (EACCES);

	mac_rx_stat->mrs_ipackets = mac_rx_stat->mrs_intrcnt +
	    mac_rx_stat->mrs_pollcnt;

	mac_rx_stat->mrs_rbytes = mac_rx_stat->mrs_intrbytes +
	    mac_rx_stat->mrs_pollbytes;

	for (i = 0; i < MAC_RX_HWLANE_SIZE; i++, knp++) {
		statp = (uint64_t *)
		    ((uchar_t *)mac_rx_stat + mac_rx_hwlane_list[i].mse_offset);

		knp->value.ui64 = *statp;
	}
	return (0);
}

static void
i_mac_rx_hwlane_stat_create(mac_ring_t *ring, const char *modname,
    const char *statname)
{
	kstat_t		*ksp;
	zoneid_t	zoneid = ring->mr_mip->mi_zoneid;

	ksp = i_mac_stat_create(ring, modname, 0, statname,
	    i_mac_rx_hwlane_stat_update, mac_rx_hwlane_list,
	    MAC_RX_HWLANE_SIZE, zoneid);
	ring->mr_hwlane_ksp = ksp;

	if (zoneid == GLOBAL_ZONEID)
		return;

	ksp = i_mac_stat_create(ring, modname, zoneid, statname,
	    i_mac_rx_hwlane_stat_update, mac_rx_hwlane_list,
	    MAC_RX_HWLANE_SIZE, GLOBAL_ZONEID);
	ring->mr_hwlane_gz_ksp = ksp;
}

/*
 * Per hardware lane tx statistics
 */
/* ARGSUSED */
static int
i_mac_tx_hwlane_stat_update(kstat_t *ksp, int rw)
{
	kstat_named_t	*knp = ksp->ks_data;
	mac_ring_t	*ring = ksp->ks_private;
	mac_tx_stats_t	*mac_tx_stat = &ring->mr_tx_stat;
	uint_t		i;
	uint64_t	*statp;

	if (rw != KSTAT_READ)
		return (EACCES);

	for (i = 0; i < MAC_TX_HWLANE_SIZE; i++, knp++) {
		statp = (uint64_t *)
		    ((uchar_t *)mac_tx_stat + mac_tx_hwlane_list[i].mse_offset);

		knp->value.ui64 = *statp;
	}
	return (0);
}

static void
i_mac_tx_hwlane_stat_create(mac_ring_t *ring, const char *modname,
    const char *statname)
{
	kstat_t		*ksp;
	zoneid_t	zoneid = ring->mr_mip->mi_zoneid;

	ksp = i_mac_stat_create(ring, modname, 0, statname,
	    i_mac_tx_hwlane_stat_update, mac_tx_hwlane_list,
	    MAC_TX_HWLANE_SIZE, zoneid);
	ring->mr_hwlane_ksp = ksp;

	if (zoneid == GLOBAL_ZONEID)
		return;

	ksp = i_mac_stat_create(ring, modname, zoneid, statname,
	    i_mac_tx_hwlane_stat_update, mac_tx_hwlane_list,
	    MAC_TX_HWLANE_SIZE, GLOBAL_ZONEID);
	ring->mr_hwlane_gz_ksp = ksp;
}

/*
 * This routine must be called before reading the TX stats -
 * mts_obytes and/or mts_opackets of a mac client. Note that
 * these are different from the per-ring TX stats with the same
 * names which don't have this requirement.
 */
void
mac_tx_update_obytes_pkts(mac_client_impl_t *mcip)
{
	int		i;
	uint64_t	obytes, opackets;
	mac_tx_stats_t	*mac_tx_stat;

	obytes = 0;
	opackets = 0;
	for (i = 0; i <= mac_tx_percpu_cnt; i++) {
		obytes += mcip->mci_tx_pcpu[i].pcpu_tx_obytes;
		opackets += mcip->mci_tx_pcpu[i].pcpu_tx_opackets;
	}

	mac_tx_stat = &mcip->mci_stat.ms_tx;
	mac_tx_stat->mts_obytes = obytes;
	mac_tx_stat->mts_opackets = opackets;
}

/*
 * Per link statistics
 */
/* ARGSUSED */
static int
i_mac_link_stat_update(kstat_t *ksp, int rw)
{
	kstat_named_t		*knp = ksp->ks_data;
	mac_client_impl_t	*mcip = (mac_client_impl_t *)ksp->ks_private;
	mac_rx_stats_t		*mac_rx_stat = &mcip->mci_stat.ms_rx;
	mac_tx_stats_t		*mac_tx_stat = &mcip->mci_stat.ms_tx;
	mac_misc_stats_t	*mac_misc_stat = &mcip->mci_stat.ms_misc;
	uint_t			i;
	uint64_t		*statp;

	if (rw != KSTAT_READ)
		return (EACCES);

	mac_tx_update_obytes_pkts(mcip);

	mac_rx_stat->mrs_ipackets = mac_rx_stat->mrs_intrcnt +
	    mac_rx_stat->mrs_pollcnt + mac_misc_stat->mms_rxlocalcnt +
	    mac_misc_stat->mms_multircv + mac_misc_stat->mms_brdcstrcv;

	mac_rx_stat->mrs_rbytes = mac_rx_stat->mrs_intrbytes +
	    mac_rx_stat->mrs_pollbytes + mac_misc_stat->mms_rxlocalbytes +
	    mac_misc_stat->mms_multircvbytes +
	    mac_misc_stat->mms_brdcstrcvbytes;

	for (i = 0; i < MAC_RX_HWLANE_SIZE; i++, knp++) {
		statp = (uint64_t *)
		    ((uchar_t *)mac_rx_stat + mac_rx_hwlane_list[i].mse_offset);

		knp->value.ui64 = *statp;
	}

	for (i = 0; i < MAC_TX_HWLANE_SIZE; i++, knp++) {
		statp = (uint64_t *)
		    ((uchar_t *)mac_tx_stat + mac_tx_hwlane_list[i].mse_offset);

		knp->value.ui64 = *statp;
	}

	for (i = 0; i < MAC_MISC_SIZE; i++, knp++) {
		statp = (uint64_t *)
		    ((uchar_t *)mac_misc_stat + mac_misc_list[i].mse_offset);

		knp->value.ui64 = *statp;
	}

	return (0);
}

/*
 * Exported functions.
 */

/*
 * Create the "mac" kstat.  The "mac" kstat is comprised of three kinds of
 * statistics: statistics maintained by the mac module itself, generic mac
 * statistics maintained by the driver, and MAC-type specific statistics
 * also maintained by the driver.
 */
void
mac_driver_stat_create(mac_impl_t *mip)
{
	kstat_t		*ksp;
	kstat_named_t	*knp;
	uint_t		count;
	major_t		major = getmajor(mip->mi_phy_dev);

	count = MAC_MOD_NKSTAT + MAC_NKSTAT + mip->mi_type->mt_statcount;
	ksp = kstat_create((const char *)ddi_major_to_name(major),
	    getminor(mip->mi_phy_dev) - 1, MAC_PHYS_KSTAT_NAME,
	    MAC_KSTAT_CLASS, KSTAT_TYPE_NAMED, count, 0);
	if (ksp == NULL)
		return;

	ksp->ks_update = i_mac_driver_stat_update;
	ksp->ks_private = mip;
	mip->mi_ksp = ksp;
	mip->mi_kstat_count = count;

	knp = (kstat_named_t *)ksp->ks_data;
	i_mac_kstat_init(knp, i_mac_mod_si, MAC_MOD_NKSTAT);
	knp += MAC_MOD_NKSTAT;
	i_mac_kstat_init(knp, i_mac_si, MAC_NKSTAT);
	if (mip->mi_type->mt_statcount > 0) {
		knp += MAC_NKSTAT;
		i_mac_kstat_init(knp, mip->mi_type->mt_stats,
		    mip->mi_type->mt_statcount);
	}

	kstat_install(ksp);
}

/*ARGSUSED*/
void
mac_driver_stat_delete(mac_impl_t *mip)
{
	if (mip->mi_ksp != NULL) {
		kstat_delete(mip->mi_ksp);
		mip->mi_ksp = NULL;
		mip->mi_kstat_count = 0;
	}
}

uint64_t
mac_driver_stat_default(mac_impl_t *mip, uint_t stat)
{
	uint_t	stat_index;

	if (IS_MAC_STAT(stat)) {
		stat_index = stat - MAC_STAT_MIN;
		ASSERT(stat_index < MAC_NKSTAT);
		return (i_mac_si[stat_index].msi_default);
	}
	ASSERT(IS_MACTYPE_STAT(stat));
	stat_index = stat - MACTYPE_STAT_MIN;
	ASSERT(stat_index < mip->mi_type->mt_statcount);
	return (mip->mi_type->mt_stats[stat_index].msi_default);
}

void
mac_ring_stat_create(mac_ring_t *ring)
{
	mac_impl_t	*mip = ring->mr_mip;
	char		statname[MAXNAMELEN];
	char		modname[MAXNAMELEN];

	if (mip->mi_state_flags & MIS_IS_AGGR) {
		(void) strlcpy(modname, mip->mi_clients_list->mci_name,
		    MAXNAMELEN);
	} else
		(void) strlcpy(modname, mip->mi_name, MAXNAMELEN);

	switch (ring->mr_type) {
	case MAC_RING_TYPE_RX:
		(void) snprintf(statname, sizeof (statname), "mac_rx_ring%d",
		    ring->mr_index);
		i_mac_rx_ring_stat_create(ring, modname, statname);
		break;

	case MAC_RING_TYPE_TX:
		(void) snprintf(statname, sizeof (statname), "mac_tx_ring%d",
		    ring->mr_index);
		i_mac_tx_ring_stat_create(ring, modname, statname);
		break;

	default:
		ASSERT(B_FALSE);
		break;
	}
}

void
mac_hwlane_stat_create(mac_impl_t *mip, mac_ring_t *ring)
{
	char	statname[MAXNAMELEN];
	char	modname[MAXNAMELEN];

	if (mip->mi_state_flags & MIS_IS_AGGR) {
		(void) strlcpy(modname, mip->mi_clients_list->mci_name,
		    MAXNAMELEN);
	} else
		(void) strlcpy(modname, mip->mi_name, MAXNAMELEN);

	if (ring->mr_type == MAC_RING_TYPE_RX) {
		(void) snprintf(statname, sizeof (statname),
		    "mac_rx_hwlane%d", ring->mr_index);
		i_mac_rx_hwlane_stat_create(ring, modname, statname);
	} else if (ring->mr_type == MAC_RING_TYPE_TX) {
		(void) snprintf(statname, sizeof (statname),
		    "mac_tx_hwlane%d", ring->mr_index);
		i_mac_tx_hwlane_stat_create(ring, modname, statname);
	}
}

void
mac_link_stat_create(mac_client_impl_t *mcip)
{
	kstat_t		*ksp;
	kstat_named_t	*knp;
	uint_t		size;
	int		i;
	zoneid_t	zoneid;

	if ((mcip->mci_state_flags & MCIS_NO_UNICAST_ADDR) != 0 &&
	    (mcip->mci_state_flags & MCIS_USE_DATALINK_NAME) != 0)
		return;

	size = MAC_RX_HWLANE_SIZE + MAC_TX_HWLANE_SIZE + MAC_MISC_SIZE;
	zoneid = mcip->mci_flent->fe_flow_desc.fd_zoneid;

	ksp = kstat_create_zone(mcip->mci_name, 0, MAC_LINK_KSTAT_NAME,
	    MAC_KSTAT_CLASS, KSTAT_TYPE_NAMED, size, 0, zoneid);

	if (ksp == NULL)
		return;

	ksp->ks_update = i_mac_link_stat_update;
	ksp->ks_private = mcip;
	mcip->mci_ksp = ksp;

	knp = (kstat_named_t *)ksp->ks_data;

	for (i = 0; i < MAC_RX_HWLANE_SIZE; i++) {
		kstat_named_init(knp, mac_rx_hwlane_list[i].mse_name,
		    KSTAT_DATA_UINT64);
		knp++;
	}
	for (i = 0; i < MAC_TX_HWLANE_SIZE; i++) {
		kstat_named_init(knp, mac_tx_hwlane_list[i].mse_name,
		    KSTAT_DATA_UINT64);
		knp++;
	}
	for (i = 0; i < MAC_MISC_SIZE; i++) {
		kstat_named_init(knp, mac_misc_list[i].mse_name,
		    KSTAT_DATA_UINT64);
		knp++;
	}
	kstat_install(ksp);
}

void
mac_ring_stat_delete(mac_ring_t *ring)
{
	if (ring->mr_ksp != NULL) {
		kstat_delete(ring->mr_ksp);
		ring->mr_ksp = NULL;
	}
	if (ring->mr_gz_ksp != NULL) {
		kstat_delete(ring->mr_gz_ksp);
		ring->mr_gz_ksp = NULL;
	}
}

void
mac_hwlane_stat_delete(mac_ring_t *ring)
{
	if (ring->mr_hwlane_ksp != NULL) {
		kstat_delete(ring->mr_hwlane_ksp);
		ring->mr_hwlane_ksp = NULL;
	}
	if (ring->mr_hwlane_gz_ksp != NULL) {
		kstat_delete(ring->mr_hwlane_gz_ksp);
		ring->mr_hwlane_gz_ksp = NULL;
	}
}

void
mac_link_stat_delete(mac_client_impl_t *mcip)
{
	if (mcip->mci_ksp != NULL) {
		kstat_delete(mcip->mci_ksp);
		mcip->mci_ksp = NULL;
	}
}

void
mac_pseudo_ring_stat_rename(mac_impl_t *mip)
{
	mac_group_t	*group;
	mac_ring_t	*ring;

	/* Recreate pseudo rx ring kstats */
	for (group = mip->mi_rx_groups; group != NULL;
	    group = group->mrg_next) {
		for (ring = group->mrg_rings; ring != NULL;
		    ring = ring->mr_next) {
			mac_ring_stat_delete(ring);
			mac_ring_stat_create(ring);
			mac_hwlane_stat_delete(ring);
			mac_hwlane_stat_create(mip, ring);
		}
	}

	/* Recreate pseudo tx ring kstats */
	for (group = mip->mi_tx_groups; group != NULL;
	    group = group->mrg_next) {
		for (ring = group->mrg_rings; ring != NULL;
		    ring = ring->mr_next) {
			mac_ring_stat_delete(ring);
			mac_ring_stat_create(ring);
			mac_hwlane_stat_delete(ring);
			mac_hwlane_stat_create(mip, ring);
		}
	}
}

void
mac_link_stat_rename(mac_client_impl_t *mcip)
{
	mac_link_stat_delete(mcip);
	mac_link_stat_create(mcip);
}
