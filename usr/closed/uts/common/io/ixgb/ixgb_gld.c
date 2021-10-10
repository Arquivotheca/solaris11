/*
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include "ixgb.h"

/*
 * This is the string displayed by modinfo, etc.
 */
static char ixgb_gld_ident[] = "Intel 10Gb Ethernet";

/*
 * Global variable to control the bridge setup or not
 */
boolean_t ixgb_br_setup_enable;

/*
 * Property names
 */
static char subven_id_flag[]		= "setid";
static char instance_no[]		= "instance";
static char subven_id[]			= "subsystem";
static char debug_propname[]		= "ixgb-debug-flags";
static char clsize_propname[]		= "cache-line-size";
static char latency_propname[]		= "latency-timer";
static char mtu_propname[]		= "default_mtu";
static char br_setup_propname[]		= "br_setup_enable";
static char tx_chksum_propname[]	= "tx_chksum";
static char rx_chksum_propname[]	= "rx_chksum";
static char lso_enable_propname[]	= "lso_enable";
static char rdelay_propname[]		= "rdelay";
static char adaptive_int_propname[]	= "adaptive_int";
static char pause_probname[]		= "pause-time";
static char flow_probnane[]		= "flow-control";
static char xon_probname[]		= "xon";
static char rxhighwater_probname[]	= "rx-highwater";
static char rxlowwater_probname[]	= "rx-lowwater";
static char coalesce_probname[]		= "coalesce-num";

/*
 * PIO access attributes for registers
 */
static ddi_device_acc_attr_t ixgb_reg_accattr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};

static int		ixgb_m_stat(void *, uint_t, uint64_t *);
static int		ixgb_m_start(void *);
static void		ixgb_m_stop(void *);
static int		ixgb_m_promisc(void *, boolean_t);
static int		ixgb_m_multicst(void *, boolean_t, const uint8_t *);
static int		ixgb_m_unicst(void *, const uint8_t *);
static void		ixgb_m_ioctl(void *, queue_t *, mblk_t *);
static boolean_t	ixgb_m_getcapab(void *, mac_capab_t, void *);

#define	IXGB_M_CALLBACK_FLAGS	(MC_IOCTL | MC_GETCAPAB)

static mac_callbacks_t ixgb_m_callbacks = {
	IXGB_M_CALLBACK_FLAGS,
	ixgb_m_stat,
	ixgb_m_start,
	ixgb_m_stop,
	ixgb_m_promisc,
	ixgb_m_multicst,
	ixgb_m_unicst,
	ixgb_m_tx,
	NULL,
	ixgb_m_ioctl,
	ixgb_m_getcapab
};

#undef	IXGB_DBG
#define	IXGB_DBG	IXGB_DBG_NEMO

/*
 *	ixgb_m_stop() -- stop transmitting/receiving
 */
static void
ixgb_m_stop(void *arg)
{
	ixgb_t *ixgbp = arg;		/* private device info	*/

	IXGB_TRACE(("ixgb_m_stop($%p)", arg));

	mutex_enter(ixgbp->genlock);
	/* already stoped */
	if (ixgbp->suspended) {
		ASSERT(ixgbp->ixgb_mac_state == IXGB_MAC_STOPPED);
		mutex_exit(ixgbp->genlock);
		return;
	}

	/*
	 * Just stop processing, then record new MAC state
	 */
	ixgb_stop(ixgbp);
	ixgbp->ixgb_mac_state = IXGB_MAC_STOPPED;
	ixgbp->link_state = LINK_STATE_UNKNOWN;
	(void) ixgb_reset(ixgbp);
	IXGB_DEBUG(("ixgb_m_stop($%p) done", arg));
	mutex_exit(ixgbp->genlock);
}

/*
 *	ixgb_m_start() -- start transmitting/receiving
 */

static int
ixgb_m_start(void *arg)
{
	ixgb_t *ixgbp = arg;		/* private device info	*/

	IXGB_TRACE(("ixgb_m_start($%p)", arg));

	mutex_enter(ixgbp->genlock);
	if (ixgbp->suspended) {
		mutex_exit(ixgbp->genlock);
		return (DDI_FAILURE);
	}
	mutex_enter(ixgbp->recv->rx_lock);
	rw_enter(ixgbp->errlock, RW_WRITER);
	mutex_enter(ixgbp->send->tc_lock);

	ixgb_start(ixgbp);
	ixgbp->ixgb_mac_state = IXGB_MAC_STARTED;

	IXGB_DEBUG(("ixgb_m_start($%p) done", arg));
	mutex_exit(ixgbp->send->tc_lock);
	rw_exit(ixgbp->errlock);
	mutex_exit(ixgbp->recv->rx_lock);
	mutex_exit(ixgbp->genlock);

	return (0);
}

/*
 *	ixgb_m_unicst() -- set the physical network address
 */
static int
ixgb_m_unicst(void *arg, const uint8_t *macaddr)
{
	ixgb_t *ixgbp = arg;		/* private device info	*/

	IXGB_TRACE(("ixgb_m_unicst_set($%p, %s)", arg,
	    ether_sprintf((void *)macaddr)));

	/*
	 * Remember the new current address in the driver state
	 * Sync the chip's idea of the address too ...
	 */
	mutex_enter(ixgbp->genlock);
	ethaddr_copy(macaddr, ixgbp->curr_addr.addr);
	ixgbp->curr_addr.set = 1;

	/*
	 * Since the chip is stopped, ixgb_chip_sync() will not
	 * be called. Once we are resumed, the unicast address
	 * will be installed.
	 */
	if (ixgbp->suspended) {
		mutex_exit(ixgbp->genlock);
		return (DDI_SUCCESS);
	}

	ixgb_chip_sync(ixgbp);

	IXGB_DEBUG(("ixgb_m_unicst($%p) done", arg));
	mutex_exit(ixgbp->genlock);

	return (0);
}

/*
 *	ixgb_m_multicst() -- enable/disable a multicast address
 */

static int
ixgb_m_multicst(void *arg, boolean_t add, const uint8_t *mca)
{
	ixgb_t *ixgbp = arg;		/* private device info	*/
	uint16_t hash;
	uint16_t index;
	uint16_t shift;
	uint16_t *refp;

	IXGB_TRACE(("ixgb_m_multicst($%p, %s, %s)", arg,
	    (add) ? "add" : "remove", ether_sprintf((void *)mca)));

	hash = ixgb_mca_hash_index(ixgbp, mca);
	index = (hash >> 5) & HASH_REG_MASK;
	shift = hash & HASH_BIT_MASK;
	refp = &ixgbp->mcast_refs[hash];

	/*
	 * We must set the appropriate bit in the hash map (and the
	 * corresponding h/w register) when the refcount goes from 0
	 * to >0, and clear it when the last ref goes away (refcount
	 * goes from >0 back to 0).  If we change the hash map, we
	 * must also update the chip's hardware map registers.
	 */
	mutex_enter(ixgbp->genlock);
	if (add) {
		if ((*refp)++ == 0) {
			ixgbp->mcast_hash[index] |= 1 << shift;
			if (ixgbp->suspended) {
				mutex_exit(ixgbp->genlock);
				return (DDI_SUCCESS);
			}
			ixgb_chip_sync(ixgbp);
		}
	} else {
		if (--(*refp) == 0) {
			ixgbp->mcast_hash[index] &= ~(1 << shift);
			if (ixgbp->suspended) {
				mutex_exit(ixgbp->genlock);
				return (DDI_SUCCESS);
			}
			ixgb_chip_sync(ixgbp);
		}
	}

	IXGB_DEBUG(("ixgb_m_multicst($%p) done", arg));

	mutex_exit(ixgbp->genlock);

	return (0);
}

/*
 * ixgb_m_promisc() -- set or reset promiscuous mode on the board
 *	Program the hardware to enable/disable promiscuous mode.
 */

static int
ixgb_m_promisc(void *arg, boolean_t on)
{
	ixgb_t *ixgbp = arg;

	IXGB_TRACE(("ixgb_m_promisc set($%p, %d)", arg, on));

	/*
	 * Store MAC layer specified mode and pass to chip layer to update h/w
	 */
	mutex_enter(ixgbp->genlock);
	ixgbp->promisc = on;

	if (ixgbp->suspended) {
		mutex_exit(ixgbp->genlock);
		return (DDI_SUCCESS);
	}

	ixgb_chip_sync(ixgbp);

	IXGB_DEBUG(("ixgb_m_promisc($%p) done", arg));
	mutex_exit(ixgbp->genlock);

	return (0);
}

/*
 * ixgb_m_stat() -- MAC statistics update handler
 */
static int
ixgb_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	ixgb_t *ixgbp = arg;
	ixgb_hw_statistics_t *hw_stp;
	ixgb_sw_statistics_t *sw_stp;
	const ixgb_ksindex_t *ksip;
	uint32_t regno;
	uint32_t low_val, high_val;

	hw_stp = &ixgbp->statistics.hw_statistics;
	sw_stp = &ixgbp->statistics.sw_statistics;
	IXGB_DEBUG(("ixgb_m_stat($%p): stat = %d", arg, stat));

	/*
	 * if not suspended, update the h/w statistics
	 */
	mutex_enter(ixgbp->genlock);
	if (!ixgbp->suspended) {
		for (ksip = ixgb_statistics; ksip->name != NULL; ++ksip) {
			regno = KS_BASE + ksip->index * sizeof (uint64_t);
			low_val = ixgb_reg_get32(ixgbp, regno);
			high_val = ixgb_reg_get32(ixgbp,
			    (regno + sizeof (uint32_t)));
			hw_stp->a[ksip->index] += (uint64_t)high_val << 32 |
			    (uint64_t)low_val;
		}
	}
	mutex_exit(ixgbp->genlock);

	switch (stat) {
	case ETHER_STAT_LINK_DUPLEX:
		*val = LINK_DUPLEX_FULL;
		break;

	case MAC_STAT_IFSPEED:
		*val = ixgbp->link_state == LINK_STATE_UP ?
		    10000000000ull : 0;
		break;

	case MAC_STAT_MULTIRCV:
		*val = hw_stp->s.InMulticastPkts;
		break;

	case MAC_STAT_BRDCSTRCV:
		*val = hw_stp->s.InBroadcastPkts;
		break;

	case MAC_STAT_MULTIXMT:
		*val = hw_stp->s.OutMulticastPkts;
		break;

	case MAC_STAT_BRDCSTXMT:
		*val = hw_stp->s.OutBroadcastPkts;
		break;

	case MAC_STAT_NORCVBUF:
		*val = hw_stp->s.NoMoreRxBDs;
		break;

	case MAC_STAT_IERRORS:
		*val = sw_stp->rcv_err;
		break;

	case MAC_STAT_NOXMTBUF:
		*val = hw_stp->s.OutDefer;
		break;

	case MAC_STAT_OERRORS:
		*val = sw_stp->xmt_err;
		break;

	case MAC_STAT_RBYTES:
		*val = hw_stp->s.InOctets;
		break;

	case MAC_STAT_IPACKETS:
		*val = hw_stp->s.InPkts;
		break;

	case MAC_STAT_OBYTES:
		*val = hw_stp->s.OutOctets;
		break;

	case MAC_STAT_OPACKETS:
		*val = hw_stp->s.OutPkts;
		break;

	default:
		return (ENOTSUP);
	}

	return (0);
}

static boolean_t
ixgb_m_getcapab(void *arg, mac_capab_t cap, void *cap_data)
{
	ixgb_t *ixgbp = (ixgb_t *)arg;

	switch (cap) {
	case MAC_CAPAB_HCKSUM: {
		uint32_t *hcksum_txflags = cap_data;

		if (!ixgbp->tx_hw_chksum)
			return (B_FALSE);
		*hcksum_txflags = HCKSUM_INET_PARTIAL;
		break;
	}
	case MAC_CAPAB_LSO: {
		mac_capab_lso_t *cap_lso = cap_data;

		if (ixgbp->lso_enable) {
			cap_lso->lso_flags = LSO_TX_BASIC_TCP_IPV4;
			cap_lso->lso_basic_tcp_ipv4.lso_max = IXGB_LSO_MAXLEN;
			break;
		} else {
			return (B_FALSE);
		}
	}
	default:
		return (B_FALSE);
	}
	return (B_TRUE);
}

/*
 * ========== ioctl handlers & subfunctions ==========
 */
#undef	IXGB_DBG
#define	IXGB_DBG	IXGB_DBG_IOCTL	/* debug flag for this code	*/

/*
 * Specific ixgb IOCTLs, the gld module handles the generic ones.
 */
static void
ixgb_m_ioctl(void *arg, queue_t *wq, mblk_t *mp)
{
	ixgb_t *ixgbp = arg;
	struct iocblk *iocp;
	enum ioc_reply status;
	boolean_t need_privilege;
	int err;
	int cmd;

	/*
	 * If suspended, we might actually be able to do some of
	 * these ioctls, but it is harder to make sure they occur
	 * without actually putting the hardware in an undesireable
	 * state.  So just NAK it.
	 */
	mutex_enter(ixgbp->genlock);
	if (ixgbp->suspended) {
		mutex_exit(ixgbp->genlock);
		miocnak(wq, mp, 0, EINVAL);
		return;
	}
	mutex_exit(ixgbp->genlock);

	/*
	 * Validate the command before bothering with the mutex ...
	 */
	iocp = (struct iocblk *)mp->b_rptr;
	iocp->ioc_error = 0;
	need_privilege = B_TRUE;
	cmd = iocp->ioc_cmd;

	IXGB_DEBUG(("ixgb_m_ioctl:  cmd 0x%x", cmd));

	switch (cmd) {
	default:
		IXGB_LDB(IXGB_DBG_BADIOC,
		    ("ixgb_m_ioctl: unknown cmd 0x%x", cmd));
		miocnak(wq, mp, 0, EINVAL);
		return;

	case IXGB_MII_READ:
	case IXGB_MII_WRITE:
	case IXGB_SEE_READ:
	case IXGB_SEE_WRITE:
	case IXGB_DIAG:
	case IXGB_PEEK:
	case IXGB_POKE:
	case IXGB_PHY_RESET:
	case IXGB_SOFT_RESET:
	case IXGB_HARD_RESET:
		break;
	case LB_GET_INFO_SIZE:
	case LB_GET_INFO:
	case LB_GET_MODE:
		need_privilege = B_FALSE;
		/* FALLTHRU */
	case LB_SET_MODE:
		break;
	case ND_GET:
		need_privilege = B_FALSE;
		/* FALLTHRU */
	case ND_SET:
		break;
	}

	if (need_privilege) {
		/*
		 * Check for specific net_config privilege on Solaris 10+.
		 */
		err = secpolicy_net_config(iocp->ioc_cr, B_FALSE);
		if (err != 0) {
			IXGB_DEBUG(("ixgb_m_ioctl: rejected cmd 0x%x, err %d",
			    cmd, err));
			miocnak(wq, mp, 0, err);
			return;
		}
	}

	mutex_enter(ixgbp->genlock);

	switch (cmd) {
	default:
		_NOTE(NOTREACHED)
		status = IOC_INVAL;
		break;

	case IXGB_MII_READ:
	case IXGB_MII_WRITE:
	case IXGB_SEE_READ:
	case IXGB_SEE_WRITE:
	case IXGB_DIAG:
	case IXGB_PEEK:
	case IXGB_POKE:
	case IXGB_PHY_RESET:
	case IXGB_SOFT_RESET:
	case IXGB_HARD_RESET:
		status = ixgb_chip_ioctl(ixgbp, mp, iocp);
		break;
	case LB_GET_INFO_SIZE:
	case LB_GET_INFO:
	case LB_GET_MODE:
	case LB_SET_MODE:
		status = ixgb_loop_ioctl(ixgbp, wq, mp, iocp);
		break;
	case ND_GET:
	case ND_SET:
		status = ixgb_nd_ioctl(ixgbp, wq, mp, iocp);
		break;
	}

	/*
	 * Do we need to reprogram the PHY and/or the MAC?
	 * Do it now, while we still have the mutex.
	 *
	 * Note: update the PHY first, 'cos it controls the
	 * speed/duplex parameters that the MAC code uses.
	 */

	IXGB_DEBUG(("ixgb_m_ioctl: cmd 0x%x status %d", cmd, status));

	switch (status) {
	case IOC_RESTART_REPLY:
	case IOC_RESTART_ACK:
		(*ixgbp->phys_restart)(ixgbp);
		ixgb_chip_sync(ixgbp);
		break;

	default:
		break;
	}

	mutex_exit(ixgbp->genlock);

	/*
	 * Finally, decide how to reply
	 */
	switch (status) {

	default:
	case IOC_INVAL:
		/*
		 * Error, reply with a NAK and EINVAL or the specified error
		 */
		miocnak(wq, mp, 0, iocp->ioc_error == 0 ?
		    EINVAL : iocp->ioc_error);
		break;

	case IOC_DONE:
		/*
		 * OK, reply already sent
		 */
		break;

	case IOC_RESTART_ACK:
	case IOC_ACK:
		/*
		 * OK, reply with an ACK
		 */
		miocack(wq, mp, 0, 0);
		break;

	case IOC_RESTART_REPLY:
	case IOC_REPLY:
		/*
		 * OK, send prepared reply as ACK or NAK
		 */
		mp->b_datap->db_type = iocp->ioc_error == 0 ?
		    M_IOCACK : M_IOCNAK;
		qreply(wq, mp);
		break;
	}
}

/*
 *	ixgb_intr -- handle chip interrupts
 */
static uint_t
ixgb_intr(caddr_t arg)
{
	ixgb_t *ixgbp = (ixgb_t *)arg;		/* private device info	*/
	uint32_t reg;

	/*
	 * Check whether chip's says it's asserting #INTA;
	 * if not, don't process or claim the interrupt.
	 */
	reg = ixgb_reg_get32(ixgbp, IXGB_ICR);
	if (reg == 0)
		return (DDI_INTR_UNCLAIMED);
			/* indicate it wasn't our interrupt */

	mutex_enter(ixgbp->genlock);
	ixgbp->statistics.sw_statistics.intr_count ++;

	/* link interrupt, check the link state */
	if (reg & IXGB_INT_LSC)
		ixgb_wake_factotum(ixgbp);

	/*
	 * check this rx's adaptive interrupt
	 * if so, restart rx's adaptive timer again
	 * per intel's PDM, page 143
	 */
	if (ixgbp->adaptive_int && (reg & IXGB_INT_RXDMT0)) {
		ixgb_reg_set32(ixgbp, IXGB_IMC, IXGB_INT_RXDMT0);
		ixgb_reg_set32(ixgbp, IXGB_IMS, IXGB_INT_RXDMT0);
	}
	mutex_exit(ixgbp->genlock);

	/*
	 * This function will examine each tx's bd in the tx's ring
	 * if the chipset is done with it then the associated resources
	 * (Tx BDS) will be "freed" and the tx's data buffer will be
	 * returned to the 'free' state.
	 */
	if (reg & IXGB_INT_TX)
		(void) ixgb_tx_recycle(ixgbp);

	/*
	 * check the rx's ring
	 * to find whether packets arrive from wired line
	 */
	ixgb_receive(ixgbp);

	return (DDI_INTR_CLAIMED);
}

/*
 * ixgb_get_props -- get the parameters to tune the driver
 */
static void
ixgb_get_props(ixgb_t *ixgbp)
{
	int instance;
	boolean_t set_id;
	chip_info_t *infop;
	dev_info_t *devinfo;
	uint32_t mtu;
	int32_t rdelay;

	instance = 0xffff;
	set_id = B_FALSE;
	devinfo = ixgbp->devinfo;
	infop = (chip_info_t *)&ixgbp->chipinfo;

	set_id = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, subven_id_flag, B_FALSE);

	instance = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, instance_no, 0);

	if (instance != ixgbp->instance)
		set_id = B_FALSE;

	if (set_id)
		infop->subven = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
		    DDI_PROP_DONTPASS, subven_id, SUBVENDOR_ID_SUN);

	infop->clsize = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, clsize_propname, 32);

	infop->latency = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, latency_propname, 64);

	ixgbp->chip_flow = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, flow_probnane, FLOW_NONE);

	ixgbp->rx_highwater = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, rxhighwater_probname,
	    RX_HIGH_WATER_DEFAULT);

	ixgbp->rx_lowwater = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, rxlowwater_probname,
	    RX_LOW_WATER_DEFAULT);

	ixgbp->xon = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, xon_probname, B_FALSE);

	ixgbp->pause_time = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, pause_probname,
	    PAUSE_TIME_DEFAULT);

	ixgbp->coalesce_num = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, coalesce_probname, RX_COALESCE_NUM_DEFAULT);

	mtu =  ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, mtu_propname, IXGB_MTU_DEFAULT);
	if (mtu > IXGB_MTU_MAX || mtu < ETHERMTU)
		mtu = IXGB_MTU_DEFAULT;

	if (mtu <= IXGB_MTU_2000)
		ixgbp->buf_size = IXGB_BUF_SIZE_2K;
	else if (mtu <= IXGB_MTU_4000)
		ixgbp->buf_size = IXGB_BUF_SIZE_4K;
	else if (mtu <= IXGB_MTU_8000)
		ixgbp->buf_size = IXGB_BUF_SIZE_8K;
	else
		ixgbp->buf_size = IXGB_BUF_SIZE_16K;

	ixgbp->mtu_size = mtu;
	ixgbp->max_frame = mtu + sizeof (struct ether_header);
	ixgbp->max_frame += VLAN_TAGSZ;

	ixgbp->tx_hw_chksum = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, tx_chksum_propname, B_TRUE);
	ixgbp->rx_hw_chksum = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, rx_chksum_propname, B_TRUE);
	ixgbp->adaptive_int = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, adaptive_int_propname, B_FALSE);

	ixgb_br_setup_enable = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, br_setup_propname, B_FALSE);

	ixgbp->lso_enable = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, lso_enable_propname, B_TRUE);
	if (ixgbp->tx_hw_chksum == B_FALSE)
		ixgbp->lso_enable = B_FALSE;

	rdelay = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, rdelay_propname, IXGB_RDELAY_DEFAULT);
	ixgbp->rdelay  = rdelay;
	if (rdelay < IXGB_RDELAY_MIN)
		ixgbp->rdelay = IXGB_RDELAY_MIN;
	if (rdelay > IXGB_RDELAY_MAX)
		ixgbp->rdelay = IXGB_RDELAY_MAX;
}

static void
ixgb_unattach(ixgb_t *ixgbp)
{
	IXGB_TRACE(("ixgb_unattach($%p)", (void *)ixgbp));

	/*
	 * Flag that no more activity may be initiated
	 */
	ixgbp->progress &= ~PROGRESS_READY;
	ixgbp->ixgb_mac_state = IXGB_MAC_UNATTACHED;

	/*
	 * Quiesce the PHY and MAC (leave it reset but still powered).
	 * Clean up and free all IXGB data structures
	 */
	if (ixgbp->periodic_id != NULL) {
		ddi_periodic_delete(ixgbp->periodic_id);
		ixgbp->periodic_id = NULL;
	}

	if (ixgbp->progress & PROGRESS_KSTATS)
		ixgb_fini_kstats(ixgbp);

	if (ixgbp->progress & PROGRESS_NDD)
		ixgb_nd_cleanup(ixgbp);

	if (ixgbp->progress & PROGRESS_HWRESET) {
		mutex_enter(ixgbp->genlock);
		ixgb_chip_stop(ixgbp, B_FALSE);
		mutex_exit(ixgbp->genlock);
	}

	if (ixgbp->progress & PROGRESS_INT) {
		ddi_remove_intr(ixgbp->devinfo, 0, ixgbp->iblk);
		mutex_destroy(ixgbp->send->tx_lock);
		mutex_destroy(ixgbp->send->txhdl_lock);
		mutex_destroy(ixgbp->send->freetxhdl_lock);
		mutex_destroy(ixgbp->send->tc_lock);
		mutex_destroy(ixgbp->recv->rx_lock);
		mutex_destroy(ixgbp->buff->rc_lock);
		rw_destroy(ixgbp->errlock);
		mutex_destroy(ixgbp->softintr_lock);
		mutex_destroy(ixgbp->genlock);
	}

	if (ixgbp->progress & PROGRESS_FACTOTUM)
		ddi_remove_softintr(ixgbp->factotum_id);

	if (ixgbp->progress & PROGRESS_RESCHED)
		ddi_remove_softintr(ixgbp->resched_id);

	if (ixgbp->progress & PROGRESS_RINGINT)
		(void) ixgb_fini_rings(ixgbp);

	ixgb_free_bufs(ixgbp);

	if (ixgbp->progress & PROGRESS_REGS)
		ddi_regs_map_free(&ixgbp->io_handle);

	if (ixgbp->progress & PROGRESS_CFG)
		pci_config_teardown(&ixgbp->cfg_handle);

	ddi_remove_minor_node(ixgbp->devinfo, NULL);
	kmem_free(ixgbp, sizeof (ixgb_t));
}

static int
ixgb_resume(dev_info_t *devinfo)
{
	ixgb_t *ixgbp;

	ASSERT(devinfo != NULL);

	ixgbp = ddi_get_driver_private(devinfo);
	if (ixgbp == NULL)
		return (DDI_FAILURE);

	if (ixgbp->devinfo != devinfo)
		return (DDI_FAILURE);

	mutex_enter(ixgbp->genlock);
	mutex_enter(ixgbp->recv->rx_lock);
	rw_enter(ixgbp->errlock, RW_WRITER);
	mutex_enter(ixgbp->send->tc_lock);

	(void) ixgb_chip_reset(ixgbp);

	/*
	 * Start chip processing, including enabling interrupts
	 */
	ixgb_chip_start(ixgbp);
	ixgbp->ixgb_mac_state = IXGB_MAC_STARTED;
	ixgbp->suspended = B_FALSE;

	mutex_exit(ixgbp->send->tc_lock);
	rw_exit(ixgbp->errlock);
	mutex_exit(ixgbp->recv->rx_lock);
	mutex_exit(ixgbp->genlock);

	return (DDI_SUCCESS);
}

/*
 * attach(9E) -- Attach a device to the system
 *
 * Called once for each board successfully probed.
 */
static int
ixgb_attach(dev_info_t *devinfo, ddi_attach_cmd_t cmd)
{
	ixgb_t *ixgbp;			/* Our private data	*/
	mac_register_t *macp;
	int instance;
	caddr_t regs;
	chip_info_t *infop;
	int err;

	instance = ddi_get_instance(devinfo);

	switch (cmd) {
	default:
		return (DDI_FAILURE);

	case DDI_RESUME:
		return (ixgb_resume(devinfo));

	case DDI_ATTACH:
		break;
	}

	ixgbp = kmem_zalloc(sizeof (*ixgbp), KM_SLEEP);
	ddi_set_driver_private(devinfo, ixgbp);
	ixgbp->devinfo = devinfo;

	/*
	 * Initialize more fields in IXGB private data
	 */
	(void) snprintf(ixgbp->ifname, sizeof (ixgbp->ifname), "%s%d",
	    IXGB_DRIVER_NAME, instance);
	ixgbp->debug = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, debug_propname, IXGB_DBG_CHIP);
	ixgb_get_props(ixgbp);

	ixgbp->pagesize = ddi_ptob(devinfo, (ulong_t)1);
	ixgbp->pagemask = -ixgbp->pagesize;

	err = pci_config_setup(devinfo, &ixgbp->cfg_handle);
	if (err != DDI_SUCCESS) {
		ixgb_problem(ixgbp, "pci_config_setup() failed");
		goto attach_fail;
	}
	infop = (chip_info_t *)&ixgbp->chipinfo;
	ixgb_chip_cfg_init(ixgbp, infop, B_FALSE);
	ixgbp->progress |= PROGRESS_CFG;

	/*
	 * Map operating registers
	 */
	err = ddi_regs_map_setup(devinfo, IXGB_PCI_OPREGS_RNUMBER,
	    &regs, 0, 0, &ixgb_reg_accattr, &ixgbp->io_handle);
	if (err != DDI_SUCCESS) {
		ixgb_problem(ixgbp, "ddi_regs_map_setup() failed");
		goto attach_fail;
	}
	ixgbp->io_regs = regs;
	ixgbp->progress |= PROGRESS_REGS;

	ixgb_chip_info_get(ixgbp);
	err = ixgb_alloc_bufs(ixgbp);
	if (err != DDI_SUCCESS) {
		ixgb_problem(ixgbp, "DMA buffer allocation failed");
		goto attach_fail;
	}
	ixgbp->progress |= PROGRESS_BUFS;

	err = ixgb_init_rings(ixgbp);
	if (err != DDI_SUCCESS) {
		ixgb_problem(ixgbp, "ixgb_init_rings() failed");
		goto attach_fail;
	}
	ixgbp->progress |= PROGRESS_RINGINT;

	/*
	 * Add the softint handlers:
	 *
	 * Both of these handlers are used to avoid restrictions on the
	 * context and/or mutexes required for some operations.  In
	 * particular, the hardware interrupt handler and its subfunctions
	 * can detect a number of conditions that we don't want to handle
	 * in that context or with that set of mutexes held.  So, these
	 * softints are triggered instead:
	 *
	 * the <resched> softint is triggered if if we have previously
	 * had to refuse to send a packet because of resource shortage
	 * (we've run out of transmit buffers), but the send completion
	 * interrupt handler has now detected that more buffers have
	 * become available.  Its only purpose is to call mac_tx_update()
	 * to retry the pending transmits.
	 *
	 * the <factotum> is triggered if the h/w interrupt handler
	 * sees the <link state changed> or <error> bits in the status
	 * block.  It's also triggered periodically to poll the link
	 * state, just in case we aren't getting link status change
	 * interrupts ...
	 */
	err = ddi_add_softintr(devinfo, DDI_SOFTINT_LOW, &ixgbp->resched_id,
	    NULL, NULL, ixgb_reschedule, (caddr_t)ixgbp);
	if (err != DDI_SUCCESS) {
		ixgb_problem(ixgbp, "ddi_add_softintr() failed");
		goto attach_fail;
	}
	ixgbp->progress |= PROGRESS_RESCHED;

	err = ddi_add_softintr(devinfo, DDI_SOFTINT_LOW, &ixgbp->factotum_id,
	    NULL, NULL, ixgb_chip_factotum, (caddr_t)ixgbp);
	if (err != DDI_SUCCESS) {
		ixgb_problem(ixgbp, "ddi_add_softintr() failed");
		goto attach_fail;
	}
	ixgbp->progress |= PROGRESS_FACTOTUM;

	/*
	 * Add the h/w interrupt handler
	 * Initialise the ring buffers (includes mutexen, so it has
	 *	to come after the interrupt registration).
	 */
	err = ddi_add_intr(devinfo, 0, &ixgbp->iblk, NULL, ixgb_intr,
	    (caddr_t)ixgbp);
	if (err != DDI_SUCCESS) {
		ixgb_problem(ixgbp, "ddi_add_intr() failed,err=%x");
		goto attach_fail;
	}
	mutex_init(ixgbp->genlock, NULL, MUTEX_DRIVER, ixgbp->iblk);
	mutex_init(ixgbp->softintr_lock, NULL, MUTEX_DRIVER, ixgbp->iblk);
	rw_init(ixgbp->errlock, NULL, RW_DRIVER, ixgbp->iblk);
	mutex_init(ixgbp->send->tx_lock, NULL, MUTEX_DRIVER, ixgbp->iblk);
	mutex_init(ixgbp->send->txhdl_lock, NULL, MUTEX_DRIVER, ixgbp->iblk);
	mutex_init(ixgbp->send->freetxhdl_lock, NULL, MUTEX_DRIVER,
	    ixgbp->iblk);
	mutex_init(ixgbp->send->tc_lock, NULL, MUTEX_DRIVER, ixgbp->iblk);
	mutex_init(ixgbp->buff->rc_lock, NULL, MUTEX_DRIVER, ixgbp->iblk);
	mutex_init(ixgbp->recv->rx_lock, NULL, MUTEX_DRIVER, ixgbp->iblk);
	ixgbp->progress |= PROGRESS_INT;

	/*
	 * Initialise link state variables
	 * Stop, reset & reinitialise the chip.
	 * Initialise the (internal) PHY.
	 */
	ixgbp->link_state = LINK_STATE_UNKNOWN;
	mutex_enter(ixgbp->genlock);
	err = ixgb_reset(ixgbp);
	mutex_exit(ixgbp->genlock);
	if (err == IXGB_FAILURE) {
		ixgb_problem(ixgbp, "ixgb_reset() failed");
		goto attach_fail;
	}
	ixgbp->progress |= PROGRESS_HWRESET;

	/*
	 * Register NDD-tweakable parameters
	 */
	if (ixgb_nd_init(ixgbp)) {
		ixgb_problem(ixgbp, "ixgb_nd_init() failed");
		goto attach_fail;
	}
	ixgbp->progress |= PROGRESS_NDD;

	/*
	 * Create & initialise named kstats
	 */
	ixgb_init_kstats(ixgbp, instance);
	ixgbp->progress |= PROGRESS_KSTATS;

	/*
	 * Determine whether to override the chip's own MAC address
	 */
	(void) ixgb_find_mac_address(ixgbp, infop);
	ethaddr_copy(infop->vendor_addr.addr, ixgbp->curr_addr.addr);
	ixgbp->curr_addr.set = 1;

	if ((macp = mac_alloc(MAC_VERSION)) == NULL)
		goto attach_fail;
	macp->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	macp->m_driver = ixgbp;
	macp->m_dip = devinfo;
	macp->m_src_addr = ixgbp->curr_addr.addr;
	macp->m_callbacks = &ixgb_m_callbacks;
	macp->m_min_sdu = 0;
	macp->m_max_sdu = ixgbp->mtu_size;
	macp->m_margin = VLAN_TAGSZ;

	/*
	 * Finally, we're ready to register ourselves with the MAC layer
	 * interface; if this succeeds, we're all ready to start()
	 */
	err = mac_register(macp, &ixgbp->mh);
	mac_free(macp);
	if (err != 0)
		goto attach_fail;

	/*
	 * Register a periodical handler.
	 * ixgb_chip_cyclic() is invoked in kernel context.
	 */
	ixgbp->periodic_id = ddi_periodic_add(ixgb_chip_cyclic, ixgbp,
	    IXGB_CYCLIC_PERIOD, DDI_IPL_0);

	ixgbp->progress |= PROGRESS_READY;
	mac_link_update(ixgbp->mh, ixgbp->link_state);
	ixgbp->ixgb_mac_state = IXGB_MAC_STOPPED;
	return (DDI_SUCCESS);

attach_fail:
	ixgb_unattach(ixgbp);
	return (DDI_FAILURE);
}

static int
ixgb_suspend(ixgb_t *ixgbp)
{
	mutex_enter(ixgbp->genlock);
	ixgbp->suspended = B_TRUE;
	ixgb_chip_stop(ixgbp, B_FALSE);
	ixgbp->ixgb_mac_state = IXGB_MAC_STOPPED;
	mutex_exit(ixgbp->genlock);

	return (DDI_SUCCESS);
}

/*
 * detach(9E) -- Detach a device from the system
 */
static int
ixgb_detach(dev_info_t *devinfo, ddi_detach_cmd_t cmd)
{
	ixgb_t *ixgbp;

	IXGB_GTRACE(("ixgb_detach($%p, %d)", (void *)devinfo, cmd));

	ixgbp = ddi_get_driver_private(devinfo);

	switch (cmd) {
	default:
		return (DDI_FAILURE);
	case DDI_SUSPEND:
		return (ixgb_suspend(ixgbp));
	case DDI_DETACH:
		break;
	}

	/*
	 * If there is any posted buffer, the driver should reject to be
	 * detached. Need notice upper layer to release them.
	 */
	if (ixgbp->buff->rx_free != IXGB_RECV_SLOTS_BUFF)
		return (DDI_FAILURE);

	/*
	 * Unregister from the MAC subsystem.  This can fail, in
	 * particular if there are DLPI style-2 streams still open -
	 * in which case we just return failure without shutting
	 * down chip operations.
	 */
	if (mac_unregister(ixgbp->mh) != 0)
		return (DDI_FAILURE);

	/*
	 * All activity stopped, so we can clean up & exit
	 */
	ixgb_unattach(ixgbp);
	return (DDI_SUCCESS);
}

static int
ixgb_quiesce(dev_info_t *devinfo)
{
	ixgb_t *ixgbp = ddi_get_driver_private(devinfo);

	if (ixgbp == NULL)
		return (DDI_SUCCESS);

	/*
	 * Turn off debugging.
	 */
	ixgbp->debug = 0;
	ixgb_debug = 0;

	ixgb_chip_stop_unlocked(ixgbp, B_FALSE);

	return (DDI_SUCCESS);
}

/*
 * ========== Module Loading Data & Entry Points ==========
 */

#undef	IXGB_DBG
#define	IXGB_DBG	IXGB_DBG_INIT	/* debug flag for this code	*/

DDI_DEFINE_STREAM_OPS(ixgb_dev_ops, nulldev, nulldev, ixgb_attach, ixgb_detach,
    nodev, NULL, D_MP, NULL, ixgb_quiesce);

static struct modldrv ixgb_modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	ixgb_gld_ident,		/* short description */
	&ixgb_dev_ops		/* driver specific ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&ixgb_modldrv, NULL
};

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * These functions were modeled after the IXGB driver which initialized
 * a mutex.  I suspect that there might be a few locks or a soft-state
 * structure that will also need initializing, so I left the calls here
 * so I can remember them.
 */

int
_init(void)
{
	int status;

	mac_init_ops(&ixgb_dev_ops, "ixgb");
	status = mod_install(&modlinkage);
	if (status == DDI_SUCCESS)
		mutex_init(ixgb_log_mutex, NULL, MUTEX_DRIVER, NULL);
	else
		mac_fini_ops(&ixgb_dev_ops);
	return (status);
}

int
_fini(void)
{
	int status;

	status = mod_remove(&modlinkage);
	if (status == DDI_SUCCESS) {
		mac_fini_ops(&ixgb_dev_ops);
		mutex_destroy(ixgb_log_mutex);
	}
	return (status);
}
