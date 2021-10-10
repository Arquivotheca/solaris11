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
 * Copyright (c) 2010 by Chelsio Communications, Inc.
 */

/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/dlpi.h>
#include <sys/strsubr.h>
#include <sys/mac_provider.h>
#include <sys/mac_ether.h>
#include <sys/vlan.h>

#include "cxge_common.h"
#include "cxgen.h"
#include "cxge.h"

#define	MH(x) (*((mac_handle_t *)(&x->handle)))
#define	CXGE_M_CALLS \
	(MC_IOCTL | MC_GETCAPAB | MC_GETPROP | MC_SETPROP | MC_PROPINFO)
#define	CXGE_M_DEFAULT		0
#define	CXGE_M_NON_DEFAULT	1

/*
 * GLDv3 callbacks.
 */
static int cxge_m_stat(void *, uint_t, uint64_t *);
static int cxge_m_start(void *);
static void cxge_m_stop(void *);
static int cxge_m_promisc(void *, boolean_t);
static int cxge_m_multicst(void *, boolean_t, const uint8_t *);
static void cxge_m_ioctl(void *, queue_t *, mblk_t *);
static boolean_t cxge_m_getcapab(void *, mac_capab_t, void *);
static int cxge_m_setprop(void *, const char *, mac_prop_id_t, uint_t,
    const void *);
static int cxge_m_getprop(void *, const char *, mac_prop_id_t, uint_t, void *);
static void cxge_m_propinfo(void *, const char *, mac_prop_id_t,
    mac_prop_info_handle_t);
static mblk_t *cxge_m_tx(void *, mblk_t *);
static mblk_t *cxge_m_poll(void *, int, int);
static int cxge_m_addmac(void *, const uint8_t *, uint64_t);
static int cxge_m_remmac(void *, const uint8_t *);

/* Helpers */
static void cxge_mr_rget(void *, mac_ring_type_t, const int, const int,
    mac_ring_info_t *, mac_ring_handle_t);
static void cxge_mr_gget(void *, mac_ring_type_t, const int, mac_group_info_t *,
    mac_group_handle_t);
static int cxge_get_priv_prop(p_cxge_t, const char *, uint_t, uint_t, void *);
static int cxge_set_priv_prop(p_cxge_t, const char *, const void *);

/*
 * GLDv3 callback structures
 */

static mac_callbacks_t cxge_mac_callbacks = {
	.mc_callbacks = CXGE_M_CALLS,	/* optional callbacks that we support */
	.mc_getstat = cxge_m_stat,	/* get the value of a statistic */
	.mc_start = cxge_m_start,	/* start the device */
	.mc_stop = cxge_m_stop,		/* stop the device */
	.mc_setpromisc = cxge_m_promisc, /* set/unset promiscuous mode */
	.mc_multicst = cxge_m_multicst,	/* enable/disable multicast addr */
	.mc_unicst = NULL,		/* we use mri_addmac instead */
	.mc_tx = NULL,			/* we use mri_tx instead */
	.mc_reserved = NULL,		/* Reserved */
	.mc_ioctl = cxge_m_ioctl,	/* process an unknown ioctl */
	.mc_getcapab = cxge_m_getcapab,	/* get capability info */
	.mc_open = NULL,		/* open the device */
	.mc_close = NULL,		/* close the device */
	.mc_setprop = cxge_m_setprop,	/* set property */
	.mc_getprop = cxge_m_getprop,	/* get property */
	.mc_propinfo = cxge_m_propinfo	/* get default property */
};

extern void *cxge_list;

/*
 * Tunables  Use dladm show-linkprop/set-linkprop to work with them.
 */
static char *cxge_private_props[] = {
	/*
	 * These affect cxge_m_getcapab's reply when the OS queries cxge about
	 * its hardware capabilities.  Disable them only if you suspect a
	 * hardware bug with checksumming or LSO.  Disabling checksumming
	 * disables LSO too.
	 */
	"_hw_csum",
	"_hw_lso",

	/* Intr coalescing time (microseconds) of all qsets for this port */
	"_intr_coalesce",

	/*
	 * Desc and frame budgets for sge_rx_data.  Completely ignored when the
	 * qset switches to polling (sge_rx_data only considers the byte budget
	 * imposed by the MAC when polling).
	 *
	 * All qsets for a particular port use the same budgets.
	 */
	"_desc_budget",
	"_frame_budget",
	NULL
};

/*
 * GLDv3 callback implementations
 */

static int
cxge_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	p_cxge_t cxgep = arg;
	struct port_info *pi = cxgep->pi;
	struct sge *sge = &pi->adapter->sge;
	struct mac_stats *mstats = &pi->mac.stats;
	struct link_config *lc = &pi->link_config;
	int rc = 0, i;
	hrtime_t ts;

	*val = 0;

	/* Driver isn't responsible for maintaining these */
	if (stat < MAC_STAT_MIN)
		ASSERT(0);

	mutex_enter(&pi->lock);

	/* Don't update the stats more frequently than every 97ms */
	ts = gethrtime();
	if (ts - cxgep->last_stats_update > 97 * 1000000) {
		(void) t3_mac_update_stats(&pi->mac);
		cxgep->last_stats_update = ts;
	}

	switch (stat) {
	case MAC_STAT_IFSPEED:
		if (lc->link_ok) {
			*val = lc->speed;
			*val *= 1000000;
		} else
			*val = 0;
		break;

	case MAC_STAT_MULTIRCV:
		*val = mstats->rx_mcast_frames;
		break;

	case MAC_STAT_BRDCSTRCV:
		*val = mstats->rx_bcast_frames;
		break;

	case MAC_STAT_MULTIXMT:
		*val = mstats->tx_mcast_frames;
		break;

	case MAC_STAT_BRDCSTXMT:
		*val = mstats->tx_bcast_frames;
		break;

	case MAC_STAT_IPACKETS:
		*val = mstats->rx_frames;
		break;

	case MAC_STAT_RBYTES:
		*val = mstats->rx_octets;
		break;

	case MAC_STAT_OPACKETS:
		*val = mstats->tx_frames;
		break;

	case MAC_STAT_OBYTES:
		*val = mstats->tx_octets;
		break;

	case MAC_STAT_NORCVBUF:
		*val = 0;
		for (i = pi->first_qset; i < pi->first_qset + pi->nqsets; i++)
			*val += sge->qs[i].rspq.stats.nomem;
		break;

	case MAC_STAT_NOXMTBUF:
		/*
		 * This is a bit of a stretch.  DMA mapping failure during tx is
		 * not the same as "no transmit buffer".  But then, we don't
		 * allocate any buffers during tx...
		 */
		*val = 0;
		for (i = pi->first_qset; i < pi->first_qset + pi->nqsets; i++)
			*val += sge->qs[i].txq[TXQ_ETH].stats.dma_map_failed;
		break;

	case MAC_STAT_COLLISIONS:
		*val = mstats->tx_total_collisions;
		break;

	case MAC_STAT_IERRORS:
		*val = (mstats->rx_fcs_errs + mstats->rx_align_errs +
		    mstats->rx_symbol_errs + mstats->rx_data_errs +
		    mstats->rx_sequence_errs + mstats->rx_runt +
		    mstats->rx_jabber + mstats->rx_short +
		    mstats->rx_too_long + mstats->rx_mac_internal_errs);
		break;

	case MAC_STAT_OERRORS:
		*val = (mstats->tx_underrun + mstats->tx_len_errs +
		    mstats->tx_mac_internal_errs +
		    mstats->tx_excess_deferral + mstats->tx_fcs_errs);
		break;

	case MAC_STAT_UNKNOWNS:
		break;

	case ETHER_STAT_ALIGN_ERRORS:
		*val = mstats->rx_align_errs;
		break;

	case ETHER_STAT_FCS_ERRORS:
		*val = mstats->tx_fcs_errs + mstats->rx_fcs_errs;
		break;

	case ETHER_STAT_SQE_ERRORS:
		*val = mstats->rx_sequence_errs;
		break;

	case ETHER_STAT_DEFER_XMTS:
		*val = mstats->tx_deferred;
		break;

	case ETHER_STAT_FIRST_COLLISIONS:
		break;

	case ETHER_STAT_MULTI_COLLISIONS:
		break;

	case ETHER_STAT_TX_LATE_COLLISIONS:
		*val = mstats->tx_late_collisions;
		break;

	case ETHER_STAT_EX_COLLISIONS:
		*val = mstats->tx_excess_collisions;
		break;

	case ETHER_STAT_MACXMT_ERRORS:
		*val = mstats->tx_mac_internal_errs;
		break;

	case ETHER_STAT_CARRIER_ERRORS:
		break;

	case ETHER_STAT_TOOLONG_ERRORS:
		*val = mstats->rx_too_long;
		break;

	case ETHER_STAT_MACRCV_ERRORS:
		*val = mstats->rx_mac_internal_errs;
		break;

	case MAC_STAT_OVERFLOWS:
		*val = mstats->rx_fifo_ovfl;
		break;

	case MAC_STAT_UNDERFLOWS:
		*val = mstats->tx_fifo_urun;
		break;

	case ETHER_STAT_TOOSHORT_ERRORS:
		*val = mstats->rx_runt + mstats->rx_short;
		break;

	case ETHER_STAT_CAP_REMFAULT:
	case ETHER_STAT_ADV_REMFAULT:
	case ETHER_STAT_LP_REMFAULT:
	case ETHER_STAT_CAP_100T4:
	case ETHER_STAT_ADV_CAP_100T4:
	case ETHER_STAT_LP_CAP_100T4:
		rc = ENOTSUP;
		break;

	case ETHER_STAT_JABBER_ERRORS:
		*val = mstats->rx_jabber;
		break;

	case ETHER_STAT_XCVR_ADDR:
		break;

	case ETHER_STAT_XCVR_ID:
		break;

	case ETHER_STAT_XCVR_INUSE:
		break;

	case ETHER_STAT_CAP_10GFDX:
		*val = !!(lc->supported & SUPPORTED_10000baseT_Full);
		break;

	case ETHER_STAT_CAP_1000FDX:
		*val = !!(lc->supported & SUPPORTED_1000baseT_Full);
		break;

	case ETHER_STAT_CAP_1000HDX:
		*val = !!(lc->supported & SUPPORTED_1000baseT_Half);
		break;

	case ETHER_STAT_CAP_100FDX:
		*val = !!(lc->supported & SUPPORTED_100baseT_Full);
		break;

	case ETHER_STAT_CAP_100HDX:
		*val = !!(lc->supported & SUPPORTED_100baseT_Half);
		break;

	case ETHER_STAT_CAP_10FDX:
		*val = !!(lc->supported & SUPPORTED_10baseT_Full);
		break;

	case ETHER_STAT_CAP_10HDX:
		*val = !!(lc->supported & SUPPORTED_10baseT_Half);
		break;

	/*
	 * ASMPAUSE = 0 and PAUSE = 1, meaning we have symmetric pause
	 * capabilities.
	 */
	case ETHER_STAT_CAP_ASMPAUSE:
		*val = 0;
		break;
	case ETHER_STAT_CAP_PAUSE:
		*val = 1;
		break;

	case ETHER_STAT_CAP_AUTONEG:
		*val = !!(lc->supported & SUPPORTED_Autoneg);
		break;

	case ETHER_STAT_ADV_CAP_10GFDX:
		*val = !!(lc->advertising & ADVERTISED_1000baseT_Full);
		break;

	case ETHER_STAT_ADV_CAP_1000FDX:
		*val = !!(lc->advertising & ADVERTISED_1000baseT_Full);
		break;

	case ETHER_STAT_ADV_CAP_1000HDX:
		*val = !!(lc->advertising & ADVERTISED_1000baseT_Half);
		break;

	case ETHER_STAT_ADV_CAP_100FDX:
		*val = !!(lc->advertising & ADVERTISED_100baseT_Full);
		break;

	case ETHER_STAT_ADV_CAP_100HDX:
		*val = !!(lc->advertising & ADVERTISED_100baseT_Half);
		break;

	case ETHER_STAT_ADV_CAP_10FDX:
		*val = !!(lc->advertising & ADVERTISED_10baseT_Full);
		break;

	case ETHER_STAT_ADV_CAP_10HDX:
		*val = !!(lc->advertising & ADVERTISED_10baseT_Half);
		break;

	case ETHER_STAT_ADV_CAP_ASMPAUSE:
		*val = !!(lc->advertising & ADVERTISED_Asym_Pause);
		break;

	case ETHER_STAT_ADV_CAP_PAUSE:
		*val = !!(lc->advertising & ADVERTISED_Pause);
		break;

	case ETHER_STAT_ADV_CAP_AUTONEG:
		*val = lc->autoneg == AUTONEG_ENABLE;
		break;

	case ETHER_STAT_LP_CAP_10GFDX:
	case ETHER_STAT_LP_CAP_1000FDX:
	case ETHER_STAT_LP_CAP_1000HDX:
	case ETHER_STAT_LP_CAP_100FDX:
	case ETHER_STAT_LP_CAP_100HDX:
	case ETHER_STAT_LP_CAP_10FDX:
	case ETHER_STAT_LP_CAP_10HDX:
	case ETHER_STAT_LP_CAP_ASMPAUSE:
	case ETHER_STAT_LP_CAP_PAUSE:
	case ETHER_STAT_LP_CAP_AUTONEG:
		/* Link Partner caps.  not readily available in lc or mstats */
		rc = ENOTSUP;
		break;

	case ETHER_STAT_LINK_ASMPAUSE:
	case ETHER_STAT_LINK_PAUSE:
		break;

	case ETHER_STAT_LINK_AUTONEG:
		*val = lc->autoneg == AUTONEG_ENABLE;
		break;

	case ETHER_STAT_LINK_DUPLEX:
		if (lc->link_ok)
			*val = lc->duplex ? LINK_DUPLEX_FULL : LINK_DUPLEX_HALF;
		else
			*val = LINK_DUPLEX_UNKNOWN;
		break;

	default:
#ifdef DEBUG
		/* Too noisy.  Disable in production. */
		cmn_err(CE_NOTE, "add support for statistic %d", stat);
#endif
		rc = ENOTSUP;
	}
	mutex_exit(&pi->lock);

	return (rc);
}

/* thin wrapper around cxge_init */
static int
cxge_m_start(void *arg)
{
	p_cxge_t cxgep = arg;

	/* returns errno values */
	return (cxge_init(cxgep));
}

/* thin wrapper around cxge_uninit */
static void
cxge_m_stop(void *arg)
{
	p_cxge_t cxgep = arg;

	cxge_uninit(cxgep);
}

static int
cxge_m_promisc(void *arg, boolean_t on)
{
	p_cxge_t cxgep = arg;
	struct port_info *pi = cxgep->pi;
	struct t3_rx_mode rm;
	int reinit;

	rw_enter(&pi->rxmode_lock, RW_WRITER);
	reinit = on ? pi->promisc++ == 0 : --pi->promisc == 0;
	ASSERT(pi->promisc == 0 || pi->promisc == 1);
	if (reinit) {
		rw_downgrade(&pi->rxmode_lock);
		t3_init_rx_mode(&rm, pi);
		(void) t3_mac_set_rx_mode(&pi->mac, &rm);
	}
	rw_exit(&pi->rxmode_lock);

	return (0);
}

static int
cxge_m_multicst(void *arg, boolean_t add, const uint8_t *addr)
{
	p_cxge_t cxgep = arg;
	struct port_info *pi = cxgep->pi;
	struct t3_rx_mode rm;
	int reinit;

	rw_enter(&pi->rxmode_lock, RW_WRITER);
	reinit = add ? cxge_add_multicast(cxgep, addr) :
	    cxge_del_multicast(cxgep, addr);
	if (reinit) {
		rw_downgrade(&pi->rxmode_lock);
		t3_init_rx_mode(&rm, pi);
		(void) t3_mac_set_rx_mode(&pi->mac, &rm);
	}
	rw_exit(&pi->rxmode_lock);
	return (0);
}

/* thin wrapper around cxge_ioctl */
static void
cxge_m_ioctl(void *arg, queue_t *wq, mblk_t *m)
{
	p_cxge_t cxgep = arg;

	cxge_ioctl(cxgep, wq, m);
}

static boolean_t
cxge_m_getcapab(void *arg, mac_capab_t cap, void *data)
{
	p_cxge_t cxgep = arg;
	struct port_info *pi = cxgep->pi;
	boolean_t status;
	mac_capab_rings_t *rings = data;

	status = B_TRUE;
	switch (cap) {
	case MAC_CAPAB_HCKSUM:
		if (cxgep->hw_csum) {
			*(uint32_t *)(data) =
			    HCKSUM_INET_FULL_V4 | HCKSUM_IPHDRCKSUM;
		} else {
			*(uint32_t *)(data) = 0;
			status = B_FALSE;
		}
		break;
	case MAC_CAPAB_LSO:
		/* Don't advertise LSO if hw checksumming is disabled */
		if (cxgep->lso && cxgep->hw_csum) {
			((mac_capab_lso_t *)data)->lso_flags =
			    LSO_TX_BASIC_TCP_IPV4;
			((mac_capab_lso_t *)data)->
			    lso_basic_tcp_ipv4.lso_max = 64 * 1024 - 1;
		} else
			status = B_FALSE;
		break;
	case MAC_CAPAB_RINGS:
		ASSERT(rings->mr_type == MAC_RING_TYPE_RX ||
		    rings->mr_type == MAC_RING_TYPE_TX);

		rings->mr_version = MAC_RINGS_VERSION_1;
		rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
		rings->mr_rnum = pi->nqsets;
		rings->mr_rget = cxge_mr_rget;
		rings->mr_gaddring = NULL;
		rings->mr_gremring = NULL;
		if (rings->mr_type == MAC_RING_TYPE_RX) {
			rings->mr_gnum = 1;
			rings->mr_gget = cxge_mr_gget;
		} else {
			rings->mr_gnum = 1;
			rings->mr_gget = NULL;
		}
		break;
	default:
		status = B_FALSE;
	}
	return (status);
}

/* ARGSUSED */
static int
cxge_m_setprop(void *arg, const char *name, mac_prop_id_t id, uint_t size,
	const void *val)
{
	int rc = 0;
	p_cxge_t cxgep = arg;
	link_flowctrl_t flow;
	struct link_config *lc = &cxgep->pi->link_config;
	uint32_t mtu = *(uint32_t *)val;
	uint32_t v8 = *(uint8_t *)val;

	/*
	 * NOTE:  While we update the parameters immediately, the actual change
	 * may not take place till a t3_link_start or t3_link_changed or
	 * something else runs.  It is best to set these props before plumbing
	 * the port, as that is certain to work.
	 */
	switch (id) {
	case MAC_PROP_AUTONEG:
		if ((lc->supported & SUPPORTED_Autoneg) == 0) {
			rc = ENOTSUP;
			break;
		}
		lc->autoneg = v8 ? AUTONEG_ENABLE : AUTONEG_DISABLE;
		break;
	case MAC_PROP_EN_10GFDX_CAP:
		if ((lc->supported & SUPPORTED_Autoneg) == 0) {
			rc = ENOTSUP;
			break;
		}

		if (v8)
			lc->advertising |= ADVERTISED_10000baseT_Full;
		else
			lc->advertising &= ~ADVERTISED_10000baseT_Full;
		break;

	case MAC_PROP_EN_1000FDX_CAP:
		if ((lc->supported & SUPPORTED_Autoneg) == 0) {
			rc = ENOTSUP;
			break;
		}
		if (v8)
			lc->advertising |= ADVERTISED_1000baseT_Full;
		else
			lc->advertising &= ~ADVERTISED_1000baseT_Full;
		break;

	case MAC_PROP_FLOWCTRL:
		bcopy(val, &flow, sizeof (flow));
		lc->requested_fc &= ~(PAUSE_TX | PAUSE_RX);

		if (flow == LINK_FLOWCTRL_BI)
			lc->requested_fc |= (PAUSE_TX | PAUSE_RX);
		else if (flow == LINK_FLOWCTRL_TX)
			lc->requested_fc |= PAUSE_TX;
		else if (flow == LINK_FLOWCTRL_RX)
			lc->requested_fc |= PAUSE_RX;
		break;

	case MAC_PROP_MTU:
		if (mtu < ETHERMTU || mtu > ETHERJUMBO_MTU)
			rc = EINVAL;
		else if (mtu != cxgep->mtu) {
			/*
			 * The MAC allows MTU to be set only when the interface
			 * is unplumbed.  So it is sufficient to just save the
			 * MTU in cxgep->mtu.  Later, when the interface is
			 * plumbed, cxge_init->cxge_link_start will program the
			 * MTU into the hardware.
			 */
			cxgep->mtu = mtu;
			rc = mac_maxsdu_update(MH(cxgep), mtu);
		}
		break;

	case MAC_PROP_PRIVATE:
		rc = cxge_set_priv_prop(cxgep, name, val);
		break;

	default:
		rc = ENOTSUP;
		break;
	}
	return (rc);
}

static int
cxge_m_getprop(void *arg, const char *name, mac_prop_id_t id, uint_t size,
	void *val)
{
	int rc = 0;
	p_cxge_t cxgep = arg;
	struct link_config *lc = &cxgep->pi->link_config;
	link_duplex_t duplex;
	uint64_t speed;
	link_state_t state;
	link_flowctrl_t flow;
	uint8_t *v8 = (uint8_t *)val;
	unsigned char fc;

	if (size == 0)
		return (EINVAL);

	/*
	 * See cxge_m_stat, most of these are pretty much the same.  The key
	 * difference is that we don't access anything in mac.stats so there is
	 * no need to hold the port's lock.
	 */
	switch (id) {
	case MAC_PROP_DUPLEX:
		if (lc->link_ok)
			duplex = lc->duplex ? LINK_DUPLEX_FULL :
			    LINK_DUPLEX_HALF;
		else
			duplex = LINK_DUPLEX_UNKNOWN;
		bcopy(&duplex, val, sizeof (duplex));
		break;

	case MAC_PROP_SPEED:
		speed = (lc->link_ok) ? lc->speed : 0;
		speed *= 1000000;
		bcopy(&speed, val, sizeof (speed));
		break;

	case MAC_PROP_STATUS:
		state = lc->link_ok ? LINK_STATE_UP : LINK_STATE_DOWN;
		bcopy(&state, val, sizeof (state));
		break;

	case MAC_PROP_FLOWCTRL:
		fc = lc->requested_fc;
		if ((fc & (PAUSE_TX | PAUSE_RX)) == (PAUSE_TX | PAUSE_RX))
			flow = LINK_FLOWCTRL_BI;
		else if (fc & PAUSE_TX)
			flow = LINK_FLOWCTRL_TX;
		else if (fc & PAUSE_RX)
			flow = LINK_FLOWCTRL_RX;
		else
			flow = LINK_FLOWCTRL_NONE;
		bcopy(&flow, val, sizeof (flow));
		break;

	case MAC_PROP_AUTONEG:
		*v8 = ((lc->supported & SUPPORTED_Autoneg) == 0) ?
		    0 : !!(lc->autoneg == AUTONEG_ENABLE);
		break;


	case MAC_PROP_EN_10GFDX_CAP:
		/* FALLTHRU */
	case MAC_PROP_ADV_10GFDX_CAP:
		if ((lc->supported & ADVERTISED_10000baseT_Full) == 0) {
			rc = ENOTSUP;
			break;
		}
		*v8 = !!(lc->advertising & ADVERTISED_10000baseT_Full);
		break;

	case MAC_PROP_EN_1000FDX_CAP:
		/* FALLTHRU */
	case MAC_PROP_ADV_1000FDX_CAP:
		if ((lc->supported & ADVERTISED_1000baseT_Full) == 0) {
			rc = ENOTSUP;
			break;
		}
		*v8 = !!(lc->advertising & ADVERTISED_1000baseT_Full);
		break;

	case MAC_PROP_PRIVATE:
		rc = cxge_get_priv_prop(cxgep, name, CXGE_M_NON_DEFAULT, size,
		    val);
		break;

	default:
		rc = ENOTSUP;
		break;
	}

	return (rc);
}

static void
cxge_m_propinfo(void *arg, const char *name, mac_prop_id_t id,
    mac_prop_info_handle_t pinfo)
{
	p_cxge_t cxgep = arg;
	struct link_config *lc = &cxgep->pi->link_config;

	switch (id) {
	case MAC_PROP_DUPLEX:
	case MAC_PROP_SPEED:
	case MAC_PROP_ADV_10GFDX_CAP:
	case MAC_PROP_ADV_1000FDX_CAP:
		mac_prop_info_set_perm(pinfo, MAC_PROP_PERM_READ);
		break;

	case MAC_PROP_EN_10GFDX_CAP:
		if ((lc->supported & ADVERTISED_10000baseT_Full) == 0) {
			mac_prop_info_set_perm(pinfo, MAC_PROP_PERM_READ);
		} else {
			mac_prop_info_set_default_uint8(pinfo,
			    lc->advertising & ADVERTISED_10000baseT_Full);
		}
		break;

	case MAC_PROP_EN_1000FDX_CAP:
		if ((lc->supported & ADVERTISED_1000baseT_Full) == 0) {
			mac_prop_info_set_perm(pinfo, MAC_PROP_PERM_READ);
		} else {
			mac_prop_info_set_default_uint8(pinfo,
			    lc->advertising & ADVERTISED_1000baseT_Full);
		}
		break;

	case MAC_PROP_MTU:
		mac_prop_info_set_range_uint32(pinfo, ETHERMTU, ETHERJUMBO_MTU);
		break;


	case MAC_PROP_PRIVATE: {
		char svalue[64];

		if (cxge_get_priv_prop(cxgep, name, CXGE_M_DEFAULT,
		    sizeof (svalue), svalue) == 0) {
			mac_prop_info_set_default_str(pinfo, svalue);
		}
	}

	} /* switch */
}

static mblk_t *
cxge_m_tx(void *arg, mblk_t *mp)
{
	struct sge_qset *qs = arg;
	struct port_info *pi = qs->port;
	mblk_t *nmp;
	int i = 0, db = 0;

	/* No tx possible if link down.  Drop the entire msg block */
	if (!pi->link_config.link_ok) {
		freemsgchain(mp);
		return (NULL);
	}

	while (mp) {
		nmp = mp->b_next;
		mp->b_next = NULL;

		/* TODO: Tune. */
		if (nmp == NULL || ++i >= 0x20) {
			db = 1;
			i = 0;
		} else
			db = 0;

		if (sge_tx_data(qs, mp, db) != 0) {
			mp->b_next = nmp;
			break;
		}
		mp = nmp;
	}

	return (mp);
}

static mblk_t *
cxge_m_poll(void *arg, int budget, int max_pkts)
{
	struct sge_qset *qs = arg;
	struct sge_rspq *q = &qs->rspq;
	mblk_t *mp;

	ASSERT(q->polling);

	mutex_enter(&q->lock);
	q->budget.bytes = budget;
	q->budget.frames = max_pkts;
	mp = sge_rx_data(qs);
	mutex_exit(&q->lock);

	return (mp);
}

static int
cxge_m_addmac(void *arg, const uint8_t *addr, uint64_t flags)
{
	_NOTE(ARGUNUSED(flags))

	p_cxge_t cxgep = arg;
	struct port_info *pi = cxgep->pi;
	int i, rc = 0;

	rw_enter(&pi->rxmode_lock, RW_WRITER);

	/* already there? */
	for (i = 0; i < pi->ucaddr_count; i++) {
		if (bcmp(addr, UCADDR(pi, i), ETHERADDRL) == 0) {
			rc = EEXIST;
			goto out;
		}
	}

	/* no space? */
	if (pi->ucaddr_count == EXACT_ADDR_FILTERS) {
		rc = ENOSPC;
		goto out;
	}

	bcopy(addr, UCADDR(pi, pi->ucaddr_count), ETHERADDRL);
	pi->ucaddr_count++;

	rw_downgrade(&pi->rxmode_lock);
	cxge_rx_mode(pi);
out:
	rw_exit(&pi->rxmode_lock);
	return (rc);
}

static int
cxge_m_remmac(void *arg, const uint8_t *addr)
{
	p_cxge_t cxgep = arg;
	struct port_info *pi = cxgep->pi;
	int i, j, rc = 0;

	rw_enter(&pi->rxmode_lock, RW_WRITER);

	for (i = 0; i < pi->ucaddr_count; i++) {
		if (bcmp(addr, UCADDR(pi, i), ETHERADDRL) == 0)
			break;
	}
	if (i == pi->ucaddr_count) {
		rc = EINVAL;
		goto out;
	}

	/*
	 * Found at index i.  Overwrite this addr with the last address in the
	 * list (if this isn't the last one itself) and decrement the count.
	 */
	j = --pi->ucaddr_count;
	if (i != j)
		bcopy(UCADDR(pi, j), UCADDR(pi, i), ETHERADDRL);

	rw_downgrade(&pi->rxmode_lock);
	cxge_rx_mode(pi);
out:
	rw_exit(&pi->rxmode_lock);
	return (rc);
}

int
cxge_mac_available()
{
	return (mac_alloc != NULL);
}

void
cxge_mac_init_ops(struct dev_ops *ops)
{
	mac_init_ops(ops, CXGE_DEVNAME);
}

void
cxge_mac_fini_ops(struct dev_ops *ops)
{
	mac_fini_ops(ops);
}

int
cxge_register_mac(p_cxge_t cxgep)
{
	int status = DDI_SUCCESS;
	mac_register_t *mac = NULL;

	mac = mac_alloc(MAC_VERSION);
	if (mac == NULL) {
		cmn_err(CE_WARN,
		    "mac_alloc failed - Driver built for mac version %d",
		    MAC_VERSION);
		return (DDI_FAILURE);
	}

	mac->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	mac->m_driver = cxgep;
	mac->m_dip = cxgep->dip;
	mac->m_src_addr = cxgep->pi->hw_addr;
	mac->m_callbacks = &cxge_mac_callbacks;
	mac->m_min_sdu = 0;
	mac->m_max_sdu = cxgep->mtu;

	if (cxgep->mtu != ETHERMTU) {
		cmn_err(CE_WARN, "Use dladm(1M) to set MTU.  "
		    "accept-jumbo in " CXGE_DEVNAME ".conf is "
		    "ignored on this OS release.");
		cxgep->mtu = ETHERMTU;
	}
	mac->m_max_sdu = ETHERMTU;
	mac->m_margin = VLAN_TAGSZ;
	mac->m_priv_props = cxge_private_props;

	status = mac_register(mac, &MH(cxgep)) ? DDI_FAILURE : DDI_SUCCESS;
	mac_free(mac);
	if (status == DDI_SUCCESS) {
		mac_link_update(MH(cxgep), LINK_STATE_UNKNOWN);
	}

	return (status);
}

int
cxge_unregister_mac(p_cxge_t cxgep)
{
	return (mac_unregister(MH(cxgep)));
}

/* ARGSUSED */
void
cxge_mac_link_changed(struct port_info *pi, int link_status, int speed,
	int duplex, int fc, int mac_was_reset)
{
	p_cxge_t cxgep = DIP_TO_CXGE(pi->dip);

	if (mac_was_reset) {
		/*
		 * t3_mac_set_mtu() adds Ethernet header size
		 */
		int mtu = cxgep->mtu + VLAN_TAGSZ;

		(void) t3_mac_set_mtu(&pi->mac, mtu);
		rw_enter(&pi->rxmode_lock, RW_READER);
		cxge_rx_mode(pi);
		rw_exit(&pi->rxmode_lock);
	}

	mac_link_update(MH(cxgep), link_status);
}

int
cxge_mac_rx(struct sge_qset *qs, mblk_t *mp)
{
	struct port_info *pi = qs->port;
	p_cxge_t cxgep = DIP_TO_CXGE(pi->dip);

	ASSERT(mp);
	mac_rx_ring(MH(cxgep), qs->rx_rh, mp, qs->rx_gen_num);

	return (0);
}

int
cxge_mac_tx_update(struct sge_qset *qs)
{
	struct port_info *pi = qs->port;
	p_cxge_t cxgep = DIP_TO_CXGE(pi->dip);

	mac_tx_ring_update(MH(cxgep), qs->tx_rh);

	return (0);
}

/*
 * Helpers
 */

/*
 * MAC calls this to get one of the statistics for a particular Rx ring
 */
int
cxge_mri_rx_stat(mac_ring_driver_t mih, uint_t stat, uint64_t *value)
{
	struct sge_qset *qs = (void *)mih;
	int status = 0, i;

	switch (stat) {
	case MAC_STAT_RBYTES:
		*value = qs->rspq.stats.rx_octets;
		break;

	case MAC_STAT_IPACKETS:
		*value = qs->rspq.stats.rx_frames;
		break;

	case MAC_STAT_IERRORS:
		*value = qs->rspq.stats.nomem + qs->rspq.stats.starved;
		for (i = 0; i < SGE_RXQ_PER_SET; i++) {
			*value += qs->fl[i].stats.nomem_kalloc +
			    qs->fl[i].stats.nomem_mblk +
			    qs->fl[i].stats.nomem_meta_hdl +
			    qs->fl[i].stats.nomem_meta_mem +
			    qs->fl[i].stats.nomem_meta_bind +
			    qs->fl[i].stats.nomem_meta_mblk;
		}
		break;

	default:
		*value = 0;
		status = ENOTSUP;
	}

	return (status);
}

/*
 * MAC calls this to get one of the statistics for a particular Tx ring
 */
int
cxge_mri_tx_stat(mac_ring_driver_t mih, uint_t stat, uint64_t *value)
{
	struct sge_qset *qs = (void *)mih;
	struct sge_txq *txq = &qs->txq[TXQ_ETH];
	int status = 0;

	switch (stat) {
	case MAC_STAT_OBYTES:
		*value = txq->stats.tx_octets;
		break;

	case MAC_STAT_OPACKETS:
		*value = txq->stats.tx_frames;
		break;

	case MAC_STAT_OERRORS:
		*value = txq->stats.pullup_failed + txq->stats.dma_map_failed;
		break;

	default:
		*value = 0;
		status = ENOTSUP;
	}

	return (status);
}

/*
 * MAC calls this to indicate the ring generation number for a
 * specific ring. Driver to pass this number along with the frame
 * through mac_rx_ring()
 */
static int
cxge_mri_start(mac_ring_driver_t mih, uint64_t gen_number)
{
	struct sge_qset *qs = (void *)mih;

	qs->rx_gen_num = gen_number;

	return (0);
}

/* Callback that helps MAC retrieve information about a ring */
static void
cxge_mr_rget(void *arg, mac_ring_type_t rt, const int gidx, const int ridx,
    mac_ring_info_t *rinfo, mac_ring_handle_t rh)
{
	p_cxge_t cxgep = arg;
	struct port_info *pi = cxgep->pi;
	struct sge_qset *qs = &pi->adapter->sge.qs[pi->first_qset + ridx];

	ASSERT(ridx < pi->nqsets);
	ASSERT(rt == MAC_RING_TYPE_RX || rt == MAC_RING_TYPE_TX);

	rinfo->mri_driver = (void *)qs;
	rinfo->mri_stop = NULL;
	if (rt == MAC_RING_TYPE_RX) {
		mac_intr_t *intr = &rinfo->mri_intr;

		ASSERT(gidx == 0);

		qs->rx_rh = rh;
		rinfo->mri_poll = cxge_m_poll;
		intr->mi_enable = sge_qs_intr_enable;
		intr->mi_disable = sge_qs_intr_disable;
		rinfo->mri_start = cxge_mri_start;
		rinfo->mri_stat = cxge_mri_rx_stat;
	} else {

		ASSERT(gidx == -1);

		qs->tx_rh = rh;
		rinfo->mri_tx = cxge_m_tx;
		rinfo->mri_start = NULL;
		rinfo->mri_stat = cxge_mri_tx_stat;
	}
}

/* Callback that helps MAC retrieve information about a ring group */
static void
cxge_mr_gget(void *arg, mac_ring_type_t rt, const int idx, mac_group_info_t
	*ginfo, mac_group_handle_t gh)
{
	p_cxge_t cxgep = arg;

	ASSERT(idx == 0);
	ASSERT(rt == MAC_RING_TYPE_RX);

	cxgep->rx_gh = gh;

	ginfo->mgi_driver = (void *)cxgep;
	ginfo->mgi_start = NULL;
	ginfo->mgi_stop = NULL;
	ginfo->mgi_addmac = cxge_m_addmac;
	ginfo->mgi_remmac = cxge_m_remmac;
	ginfo->mgi_count = cxgep->pi->nqsets;
	if (idx == 0)
		ginfo->mgi_flags = MAC_GROUP_DEFAULT;
}

static int
cxge_get_priv_prop(p_cxge_t cxgep, const char *name, uint_t flags, uint_t size,
    void *val)
{
	int rc = 0, v, def = flags & CXGE_M_DEFAULT;
	struct port_info *pi = cxgep->pi;
	struct qset_params *qp = &pi->adapter->params.sge.qset[pi->first_qset];
	struct sge_rspq *q = &pi->adapter->sge.qs[pi->first_qset].rspq;

	if (strcmp(name, "_hw_csum") == 0)
		v = def ? 1 : cxgep->hw_csum;
	else if (strcmp(name, "_hw_lso") == 0)
		v = def ? 1 : cxgep->lso;
	else if (strcmp(name, "_intr_coalesce") == 0)
		v = def ? qp->coalesce_usecs : q->holdoff_tmr / 10;
	else if (strcmp(name, "_desc_budget") == 0)
		v = def ? QS_DEFAULT_DESC_BUDGET(q) : q->budget.descs;
	else if (strcmp(name, "_frame_budget") == 0)
		v = def ? QS_DEFAULT_FRAME_BUDGET(q) : q->budget.frames;
	else {
		rc = ENOTSUP;
#ifdef DEBUG
		cmn_err(CE_NOTE, "%s: unknown property [%s]", __func__, name);
#endif
	}

	if (rc == 0)
		(void) snprintf(val, size, "%d", v);

	return (rc);
}

static int
cxge_set_priv_prop(p_cxge_t cxgep, const char *name, const void *val)
{
	int rc = 0, v = -1;
	long l = 0;

	/* All private props are int so this is common to all */
	if (val)
		v = ddi_strtol(val, NULL, 0, &l) == 0 ? (int)l : -1;

	if (strcmp(name, "_hw_csum") == 0) {
		if (v != 0 && v != 1)
			rc = EINVAL;
		else
			cxgep->hw_csum = v;
	} else if (strcmp(name, "_hw_lso") == 0) {
		if (v != 0 && v != 1)
			rc = EINVAL;
		else
			cxgep->lso = v;
	} else if (strcmp(name, "_intr_coalesce") == 0)
		rc = cxge_set_coalesce(cxgep, v);
	else if (strcmp(name, "_desc_budget") == 0)
		rc = cxge_set_desc_budget(cxgep, v);
	else if (strcmp(name, "_frame_budget") == 0)
		rc = cxge_set_frame_budget(cxgep, v);
	else {
		rc = ENOTSUP;
#ifdef DEBUG
		cmn_err(CE_NOTE, "%s: unknown property [%s]", __func__, name);
#endif
	}

	return (rc);
}
