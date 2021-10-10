/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2007-2010 Broadcom Corporation, ALL RIGHTS RESERVED.
 ******************************************************************************/


#include "bnxgld.h"
#include "bnxhwi.h"
#include "bnxsnd.h"
#include "bnxndd.h"
#include "bnxcfg.h"

#ifdef __10u7
#include <sys/mac.h>
#else
#include <sys/mac_provider.h>
#endif
#include <sys/mac_ether.h>
#include <sys/dlpi.h>


/*
 * Reconfiguring the network devices parameters require net_config
 * privilege starting Solaris 10.  Only root user is allowed to
 * update device parameter in Solaris 9 and earlier version. Following
 * declaration allows single binary image to run on all OS versions.
 */
extern int secpolicy_net_config(const cred_t *, boolean_t);
extern int drv_priv(cred_t *);
#pragma weak secpolicy_net_config
#pragma weak drv_priv



/****************************************************************************
 * Name:    bnx_m_start
 *
 * Input:   ptr to driver device structure.
 *
 * Return:  DDI_SUCCESS or DDI_FAILURE
 *
 * Description:
 *          This routine is called by GLD to enable device for
 *          packet reception and enable interrupts.
 ****************************************************************************/
static int
bnx_m_start( void * arg )
{
	int rc;
	um_device_t * umdevice;

	umdevice = (um_device_t *)arg;

	mutex_enter( &umdevice->os_param.gld_mutex );

	if( umdevice->dev_start == B_TRUE )
	{
		/* We're already started.  Success! */
		rc = 0;
		goto done;
	}

	/* Always report the initial link state as unknown. */
	bnx_gld_link( umdevice, LINK_STATE_UNKNOWN );

    umdevice->link_updates_ok = B_TRUE;

	if( bnx_hdwr_acquire(umdevice) )
	{
		rc = EIO;
		goto done;
	}

	drv_usecwait(1000000);

	umdevice->dev_start = B_TRUE;

	rc = 0;

done:
	mutex_exit( &umdevice->os_param.gld_mutex );

	return rc;
} /* bnx_m_start */



/****************************************************************************
 * Name:    bnx_m_stop
 *
 * Input:   ptr to driver device structure.
 *
 * Return:  DDI_SUCCESS or DDI_FAILURE
 *
 * Description:
 *          This routine stops packet reception by clearing RX MASK
 *          register. Also interrupts are disabled for this device.
 ****************************************************************************/
static void
bnx_m_stop( void * arg )
{
	um_device_t * umdevice;

	umdevice = (um_device_t *)arg;

	mutex_enter( &umdevice->os_param.gld_mutex );

	if( umdevice->dev_start == B_TRUE )
	{
		umdevice->dev_start = B_FALSE;
        umdevice->link_updates_ok = B_FALSE;

		bnx_hdwr_release( umdevice );

        /* Report the link state back to unknown. */
        bnx_gld_link( umdevice, LINK_STATE_UNKNOWN );

        umdevice->dev_var.indLink   = 0;
        umdevice->dev_var.indMedium = 0;
	}

	mutex_exit( &umdevice->os_param.gld_mutex );
} /* bnx_m_stop */



/****************************************************************************
 * Name:    bnx_m_unicast
 *
 * Input:   ptr to driver device structure,
 *          pointer to buffer containing MAC address.
 *
 * Return:  DDI_SUCCESS or DDI_FAILURE
 *
 * Description:
 ****************************************************************************/
static int
bnx_m_unicast( void * arg, const uint8_t * macaddr )
{
	int rc;
	um_device_t * umdevice;
	lm_device_t * lmdevice;

	umdevice = (um_device_t *)arg;
	lmdevice = &(umdevice->lm_dev);

	mutex_enter( &umdevice->os_param.gld_mutex );

	/* Validate MAC address */
	if( IS_ETH_MULTICAST(macaddr) )
	{
		cmn_err( CE_WARN,
		         "%s: Attempt to program a multicast / broadcast address as a MAC address.",
		         umdevice->dev_name );
		rc = EINVAL;
		goto done;
	}

	bcopy( macaddr, &(lmdevice->params.mac_addr[0]), ETHERADDRL );

	if( umdevice->dev_start == B_TRUE )
	{
		lm_set_mac_addr( lmdevice, 0, &(lmdevice->params.mac_addr[0]) );
	}

	rc = 0;

done:
	mutex_exit( &umdevice->os_param.gld_mutex );

	return rc;
} /* bnx_m_unicast */



/*******************************************************************************
 * Name:    bnx_mc_add
 *
 * Input:   ptr to driver device structure,
 *          pointer to buffer containing multicast address.
 *
 * Return:
 *
 * Description:
 ******************************************************************************/
static int
bnx_mc_add( um_device_t * umdevice, const uint8_t * const mc_addr )
{
	int rc;
	int index;
	lm_status_t   lmstatus;
	lm_device_t * lmdevice;

	lmdevice = &(umdevice->lm_dev);

	index = bnx_find_mchash_collision( &(lmdevice->mc_table), mc_addr );
	if( index == -1 )
	{
		lmstatus = lm_add_mc( lmdevice, (u8_t *)mc_addr );
		if( lmstatus == LM_STATUS_SUCCESS )
		{
			umdevice->dev_var.rx_filter_mask |= LM_RX_MASK_ACCEPT_MULTICAST;
			rc = 0;
		}
		else
		{
			rc = ENOMEM;
		}
	}
	else
	{
		lmdevice->mc_table.addr_arr[index].ref_cnt++;
		rc = 0;
	}

	return rc;
} /* bnx_mc_add */



/*******************************************************************************
 * Name:    bnx_mc_del
 *
 * Input:   ptr to driver device structure,
 *          pointer to buffer containing multicast address.
 *
 * Return:
 *
 * Description:
 ******************************************************************************/
static int
bnx_mc_del( um_device_t * umdevice, const uint8_t * const mc_addr )
{
	int rc;
	int index;
	lm_status_t   lmstatus;
	lm_device_t * lmdevice;

	lmdevice = &(umdevice->lm_dev);

	index = bnx_find_mchash_collision( &(lmdevice->mc_table), mc_addr );
	if( index == -1 )
	{
		rc = ENXIO;
	}
	else
	{
		lmstatus = lm_del_mc( lmdevice, lmdevice->mc_table.addr_arr[index].mc_addr );
		if( lmstatus == LM_STATUS_SUCCESS )
		{
			if( lmdevice->mc_table.entry_cnt == 0 )
				umdevice->dev_var.rx_filter_mask &= ~LM_RX_MASK_ACCEPT_MULTICAST;

			rc = 0;
		}
		else
		{
			rc = ENXIO;
		}
	}

	return rc;
} /* bnx_mc_del */



/****************************************************************************
 * Name:    bnx_m_multicast
 *
 * Input:   ptr to driver device structure,
 *          boolean describing whether to enable or disable this address,
 *          pointer to buffer containing multicast address.
 *
 * Return:  DDI_SUCCESS or DDI_FAILURE
 *
 * Description:
 *          This function is used to enable or disable multicast packet
 *          reception for particular multicast addresses.
 ****************************************************************************/
static int
bnx_m_multicast( void * arg,
                 boolean_t multiflag, const uint8_t *multicastaddr )
{
	um_device_t * umdevice;
	int rc;

	umdevice = (um_device_t *)arg;

	mutex_enter( &umdevice->os_param.gld_mutex );

	if( umdevice->dev_start != B_TRUE )
	{
		rc = EAGAIN;
		goto done;
	}

	switch(multiflag)
	{
		case B_TRUE:
			rc = bnx_mc_add( umdevice, multicastaddr );
			break;

		case B_FALSE:
			rc = bnx_mc_del( umdevice, multicastaddr );
			break;

		default:
			rc = EINVAL;
			break;
	}

done:
	mutex_exit( &umdevice->os_param.gld_mutex );

	return rc;
} /* bnx_m_multicast */



/****************************************************************************
 * Name:    bnx_m_promiscuous
 *
 * Input:   ptr to driver device structure,
 *          boolean describing whether to enable or disable promiscuous mode.
 *
 * Return:  DDI_SUCCESS or DDI_FAILURE
 *
 * Description:
 *          This function enables promiscuous mode for this device.
 *		'flags' argument determines the type of mode being set,
 *		"PROMISC_PHY" enables reception of all packet types including
 *		bad/error packets. "PROMISC_MULTI" mode will enable all
 *		multicast packets, unicasts and broadcast packets to be
 *		received. "PROMISC_NONE" will enable only broadcast and
 *		unicast packets.
 ****************************************************************************/
static int
bnx_m_promiscuous( void * arg, boolean_t promiscflag )
{
	int rc;
	um_device_t * umdevice;

	umdevice = (um_device_t *)arg;

	mutex_enter( &umdevice->os_param.gld_mutex );

	if( umdevice->dev_start != B_TRUE )
	{
		rc = EAGAIN;
		goto done;
	}

	switch( promiscflag )
	{
		case B_TRUE:
			umdevice->dev_var.rx_filter_mask |= LM_RX_MASK_PROMISCUOUS_MODE;
			break;

		case B_FALSE:
			umdevice->dev_var.rx_filter_mask &= ~LM_RX_MASK_PROMISCUOUS_MODE;
			break;

		default:
			rc = EINVAL;
			goto done;
	}

	lm_set_rx_mask( &(umdevice->lm_dev), RX_FILTER_USER_IDX0,
	                umdevice->dev_var.rx_filter_mask );

	rc = 0;

done:
	mutex_exit( &umdevice->os_param.gld_mutex );

	return rc;
} /* bnx_m_promiscuous */



/*******************************************************************************
 * Name:    bnx_m_tx
 *
 * Description:
 *
 * Return:
 ******************************************************************************/
static mblk_t *
bnx_m_tx( void * arg, mblk_t * mp )
{
	int rc;
	mblk_t * nmp;
	um_device_t * umdevice;

	umdevice = (um_device_t *)arg;

	rw_enter( &umdevice->os_param.gld_snd_mutex, RW_READER );

	if( umdevice->dev_start != B_TRUE ||
	    umdevice->nddcfg.link_speed == 0 )
	{
		freemsgchain( mp );
		mp = NULL;
		goto done;
	}

	nmp = NULL;

	while( mp )
	{
		/* Save the next pointer, in case we do double copy. */
		nmp = mp->b_next;
		mp->b_next = NULL;

		rc = bnx_xmit_ring_xmit_mblk( umdevice, 0, mp );

		if ( rc == BNX_SEND_GOODXMIT )
		{
			mp = nmp;
			continue;
		}

		if( rc == BNX_SEND_DEFERPKT )
			mp = nmp;
		else
			mp->b_next = nmp;

		break;
	}

done:
	rw_exit( &umdevice->os_param.gld_snd_mutex );

	return mp;
} /* bnx_m_tx */



/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
static u64_t
shift_left32(u32_t val)
{
	lm_u64_t tmp;

	/* FIXME -- Get rid of shift_left32() */

	tmp.as_u32.low = 0;
	tmp.as_u32.high = val;

	return (tmp.as_u64);
} /* shift_left32 */



/*******************************************************************************
 * Name:    bnx_m_stats
 *
 * Input:   ptr to mac info structure, ptr to gld_stats struct
 *
 * Return:  DDI_SUCCESS or DDI_FAILURE
 *
 * Description: bnx_m_stats() populates gld_stats structure elements
 *              from latest data from statistic block.
 ******************************************************************************/
static int
bnx_m_stats( void * arg, uint_t stat, uint64_t *val)
{
	int rc;
	um_device_t * umdevice;
	lm_device_t * lmdevice;
	const bnx_lnk_cfg_t * linkconf;

	umdevice = (um_device_t *)arg;

	if( umdevice == NULL || val == NULL )
	{
		return EINVAL;
	}

	lmdevice = &(umdevice->lm_dev);

	/* FIXME -- Fix STATS collections */

	if( umdevice->dev_var.isfiber )
	{
		linkconf = &bnx_serdes_config;
	}
	else
	{
		linkconf = &bnx_copper_config;
	}

	mutex_enter( &umdevice->os_param.gld_mutex );

	if( umdevice->dev_start != B_TRUE )
	{
		rc = EAGAIN;
		goto done;
	}

	*val = 0;
	switch (stat) {
	case MAC_STAT_IFSPEED:
		*val = umdevice->nddcfg.link_speed * 1000000ull;
		break;
	case MAC_STAT_MULTIRCV:
		*val += shift_left32(
		    lmdevice->vars.stats_virt->stat_IfHCInMulticastPkts_hi);
		*val +=
		    lmdevice->vars.stats_virt->stat_IfHCInMulticastPkts_lo;
		break;
	case MAC_STAT_BRDCSTRCV:
		*val += shift_left32(
		    lmdevice->vars.stats_virt->stat_IfHCInBroadcastPkts_hi);
		*val +=
		    lmdevice->vars.stats_virt->stat_IfHCInBroadcastPkts_lo;
		break;
	case MAC_STAT_MULTIXMT:
		*val += shift_left32(
		    lmdevice->vars.stats_virt->stat_IfHCOutMulticastPkts_hi);
		*val +=
		    lmdevice->vars.stats_virt->stat_IfHCOutMulticastPkts_lo;
		break;
	case MAC_STAT_BRDCSTXMT:
		*val += shift_left32(
		    lmdevice->vars.stats_virt->stat_IfHCOutBroadcastPkts_hi);
		*val +=
		    lmdevice->vars.stats_virt->stat_IfHCOutBroadcastPkts_lo;
		break;
	case MAC_STAT_NORCVBUF:
		*val = lmdevice->vars.stats_virt->stat_IfInMBUFDiscards;
		break;
	case ETHER_STAT_MACRCV_ERRORS:
	case MAC_STAT_IERRORS:
		*val = lmdevice->vars.stats_virt->stat_Dot3StatsFCSErrors +
		    lmdevice->vars.stats_virt->stat_Dot3StatsAlignmentErrors +
		    lmdevice->vars.stats_virt->stat_EtherStatsUndersizePkts +
		    lmdevice->vars.stats_virt->stat_EtherStatsOverrsizePkts;
		break;
	case MAC_STAT_OERRORS:
		*val = lmdevice->vars.stats_virt->
		    stat_emac_tx_stat_dot3statsinternalmactransmiterrors;
		break;
	case MAC_STAT_COLLISIONS:
		*val = lmdevice->vars.stats_virt->stat_EtherStatsCollisions;
		break;
	case MAC_STAT_RBYTES:
		*val += shift_left32(
		    lmdevice->vars.stats_virt->stat_IfHCInOctets_hi);
		*val +=
		    lmdevice->vars.stats_virt->stat_IfHCInOctets_lo;
		break;
	case MAC_STAT_IPACKETS:
		*val += shift_left32(lmdevice->vars.stats_virt->
		    stat_IfHCInUcastPkts_hi);
		*val += lmdevice->vars.stats_virt->stat_IfHCInUcastPkts_lo;

		*val += shift_left32(lmdevice->vars.stats_virt->
		    stat_IfHCInMulticastPkts_hi);
		*val += lmdevice->vars.stats_virt->stat_IfHCInMulticastPkts_lo;

		*val += shift_left32(lmdevice->vars.stats_virt->
		    stat_IfHCInBroadcastPkts_hi);
		*val += lmdevice->vars.stats_virt->stat_IfHCInBroadcastPkts_lo;
		break;
	case MAC_STAT_OBYTES:
		*val += shift_left32(
		    lmdevice->vars.stats_virt->stat_IfHCOutOctets_hi);
		*val +=
		    lmdevice->vars.stats_virt->stat_IfHCOutOctets_lo;
		break;
	case MAC_STAT_OPACKETS:
		*val += shift_left32(lmdevice->vars.stats_virt->
		    stat_IfHCOutUcastPkts_hi);
		*val += lmdevice->vars.stats_virt->stat_IfHCOutUcastPkts_lo;

		*val += shift_left32(lmdevice->vars.stats_virt->
		    stat_IfHCOutMulticastPkts_hi);
		*val += lmdevice->vars.stats_virt->stat_IfHCOutMulticastPkts_lo;

		*val += shift_left32(lmdevice->vars.stats_virt->
		    stat_IfHCOutBroadcastPkts_hi);
		*val += lmdevice->vars.stats_virt->stat_IfHCOutBroadcastPkts_lo;
		break;
	case ETHER_STAT_ALIGN_ERRORS:
		*val = lmdevice->vars.stats_virt->stat_Dot3StatsAlignmentErrors;
		break;
	case ETHER_STAT_FCS_ERRORS:
		*val = lmdevice->vars.stats_virt->stat_Dot3StatsFCSErrors;
		break;
	case ETHER_STAT_FIRST_COLLISIONS:
		*val = lmdevice->vars.stats_virt->
		    stat_Dot3StatsSingleCollisionFrames;
		break;
	case ETHER_STAT_MULTI_COLLISIONS:
		*val = lmdevice->vars.stats_virt->
		    stat_Dot3StatsMultipleCollisionFrames;
		break;
	case ETHER_STAT_DEFER_XMTS:
		*val = lmdevice->vars.stats_virt->
		    stat_Dot3StatsDeferredTransmissions;
		break;
	case ETHER_STAT_TX_LATE_COLLISIONS:
		*val = lmdevice->vars.stats_virt->
		    stat_Dot3StatsLateCollisions;
		break;
	case ETHER_STAT_EX_COLLISIONS:
		*val = lmdevice->vars.stats_virt->
		    stat_Dot3StatsExcessiveCollisions;
		break;
	case ETHER_STAT_MACXMT_ERRORS:
		*val = lmdevice->vars.stats_virt->
		    stat_emac_tx_stat_dot3statsinternalmactransmiterrors;
		break;
	case ETHER_STAT_CARRIER_ERRORS:
		*val = lmdevice->vars.stats_virt->
		    stat_Dot3StatsCarrierSenseErrors;
		break;
	case ETHER_STAT_TOOLONG_ERRORS:
		*val = lmdevice->vars.stats_virt->
		    stat_EtherStatsOverrsizePkts;
		break;
	case ETHER_STAT_TOOSHORT_ERRORS:
		*val = lmdevice->vars.stats_virt->
		    stat_EtherStatsUndersizePkts;
		break;
	case ETHER_STAT_XCVR_ADDR:
		*val = lmdevice->params.phy_addr;
		break;
	case ETHER_STAT_XCVR_ID:
		*val = lmdevice->hw_info.phy_id;
		break;
	case ETHER_STAT_XCVR_INUSE:
		switch (umdevice->nddcfg.link_speed) {
		case 1000:
			*val = (umdevice->dev_var.isfiber) ?
			    XCVR_1000X : XCVR_1000T;
			break;
		case 100:
			*val = XCVR_100X;
			break;
		case 10:
			*val = XCVR_10;
			break;
		default:
			*val = XCVR_NONE;
			break;
		}
		break;
	case ETHER_STAT_CAP_1000FDX:
		*val = 1;
		break;
	case ETHER_STAT_CAP_1000HDX:
		*val = linkconf->param_1000hdx;
		break;
	case ETHER_STAT_CAP_100FDX:
		*val = linkconf->param_100fdx;
		break;
	case ETHER_STAT_CAP_100HDX:
		*val = linkconf->param_100hdx;
		break;
	case ETHER_STAT_CAP_10FDX:
		*val = linkconf->param_10fdx;
		break;
	case ETHER_STAT_CAP_10HDX:
		*val = linkconf->param_10hdx;
		break;
	case ETHER_STAT_CAP_ASMPAUSE:
		*val = 1;
		break;
	case ETHER_STAT_CAP_PAUSE:
		*val = 1;
		break;
	case ETHER_STAT_CAP_AUTONEG:
		*val = 1;
		break;
	case ETHER_STAT_CAP_REMFAULT:
		*val = 1;
		break;
	case ETHER_STAT_ADV_CAP_1000FDX:
		*val = umdevice->curcfg.lnkcfg.param_1000fdx;
		break;
	case ETHER_STAT_ADV_CAP_1000HDX:
		*val = umdevice->curcfg.lnkcfg.param_1000hdx;
		break;
	case ETHER_STAT_ADV_CAP_100FDX:
		*val = umdevice->curcfg.lnkcfg.param_100fdx;
		break;
	case ETHER_STAT_ADV_CAP_100HDX:
		*val = umdevice->curcfg.lnkcfg.param_100hdx;
		break;
	case ETHER_STAT_ADV_CAP_10FDX:
		*val = umdevice->curcfg.lnkcfg.param_10fdx;
		break;
	case ETHER_STAT_ADV_CAP_10HDX:
		*val = umdevice->curcfg.lnkcfg.param_10hdx;
		break;
	case ETHER_STAT_ADV_CAP_ASMPAUSE:
		*val = 1;
		break;
	case ETHER_STAT_ADV_CAP_PAUSE:
		*val = 1;
		break;
	case ETHER_STAT_ADV_CAP_AUTONEG:
		*val = umdevice->curcfg.lnkcfg.link_autoneg;
		break;
	case ETHER_STAT_ADV_REMFAULT:
		*val = 1;
		break;
	case ETHER_STAT_LP_CAP_1000FDX:
		*val = umdevice->remote.param_1000fdx;
		break;
	case ETHER_STAT_LP_CAP_1000HDX:
		*val = umdevice->remote.param_1000hdx;
		break;
	case ETHER_STAT_LP_CAP_100FDX:
		*val = umdevice->remote.param_100fdx;
		break;
	case ETHER_STAT_LP_CAP_100HDX:
		*val = umdevice->remote.param_100hdx;
		break;
	case ETHER_STAT_LP_CAP_10FDX:
		*val = umdevice->remote.param_10fdx;
		break;
	case ETHER_STAT_LP_CAP_10HDX:
		*val = umdevice->remote.param_10hdx;
		break;
	case ETHER_STAT_LP_CAP_ASMPAUSE:
		/* FIXME -- Implement LP_ASYM_PAUSE stat */
		break;
	case ETHER_STAT_LP_CAP_PAUSE:
		/* FIXME -- Implement LP_PAUSE stat */
		break;
	case ETHER_STAT_LP_CAP_AUTONEG:
		*val = umdevice->remote.link_autoneg;
		break;
	case ETHER_STAT_LP_REMFAULT:
		/* FIXME -- Implement LP_REMFAULT stat */
		break;
	case ETHER_STAT_LINK_ASMPAUSE:
		/* FIXME -- Implement ASMPAUSE stat */
		break;
	case ETHER_STAT_LINK_PAUSE:
		/* FIXME -- Implement PAUSE stat */
		break;
	case ETHER_STAT_LINK_AUTONEG:
		*val = umdevice->curcfg.lnkcfg.link_autoneg;
		break;
	case ETHER_STAT_LINK_DUPLEX:
		if (umdevice->nddcfg.link_duplex)
			*val = umdevice->nddcfg.link_duplex == B_TRUE ?
			    LINK_DUPLEX_FULL: LINK_DUPLEX_HALF;
		else
			*val = LINK_DUPLEX_UNKNOWN;
		break;
	default:
		rc = ENOTSUP;
	}

	rc = 0;

done:
	mutex_exit( &umdevice->os_param.gld_mutex );

	return rc;
} /* bnx_m_stats */



#ifdef BNX_ENABLE_IOCTL

/****************************************************************************
 * Name:	bnx_process_ioctl
 *
 * Input:	ptr to driver device structure,
 *              queue ptr,
 *              ptr to ioctl msg block
 *
 * Return:	void
 *
 * Description: This wrapper funtion checks if the request ioctl code is
 *		is supported or not. If the ioctl is supported by the driver,
 *		corresponding SET/GET function is called by looking up the
 *		table and ioctl is ACK'ed with appropriate status.
 ****************************************************************************/
static void
bnx_process_ioctl( void * arg, queue_t *q, mblk_t * mp )
{
	int rc;
	um_device_t * umdevice;
	struct iocblk * ioctlp;

	ioctlp = (struct iocblk *)mp->b_rptr;

	umdevice = (um_device_t *)arg;

	mutex_enter( &umdevice->os_param.gld_mutex );

	switch( ioctlp->ioc_cmd )
	{
		case ND_SET:
			rc = 0;
			if( secpolicy_net_config != NULL )
			{
				rc = secpolicy_net_config( ioctlp->ioc_cr, B_FALSE );
			}
			else if( drv_priv != NULL )
			{
				rc = drv_priv( ioctlp->ioc_cr );
			}
			if( rc != 0 )
			{
				miocnak( q, mp, 0, rc );
				return;
			}
			/* FALLTHROUGH */

		case ND_GET:
			rc = bnx_nd_ioctl(umdevice, q, mp, ioctlp);
			if( rc == IOC_RESTART_REPLY )
			{
				bnx_update_phy(umdevice);
			}
			break;

		default:
			rc = IOC_INVAL;
			break;
	}

	mutex_exit( &umdevice->os_param.gld_mutex );

	/*
	 * Now that IOCTL processing is done, check how
	 * we need to respond to the administrator!!
	 */

	switch(rc)
	{
		case IOC_DONE:
			break;

		case IOC_ACK:
		case IOC_RESTART_ACK:
			/* All went well, reply with an ACK */
			miocack(q, mp, 0, 0);
			break;

		case IOC_RESTART_REPLY:
		case IOC_REPLY:
			/* Send previously prepared reponse as ACK or NACK */
			if( ioctlp->ioc_error == 0 )
			{
				rc = M_IOCACK;
			}
			else
			{
				rc = M_IOCNAK;
			}
			mp->b_datap->db_type = (unsigned char)rc;
			qreply(q, mp);
			break;

		case IOC_INVAL:
		default:
			/*
			 * Error encountered, return either NAK,
			 * EINVAL or the specific error seen.
			 */
			if( ioctlp->ioc_error == 0 )
			{
				rc = EINVAL;
			}
			else
			{
				rc = ioctlp->ioc_error;
			}
			miocnak(q, mp, rc, 0);
			break;
	}
} /* bnx_process_ioctl */



/****************************************************************************
 * Name:    bnx_m_ioctl
 *
 * Input:   ptr to mac info structure, ptr to message block.
 *
 * Return:  DDI_SUCCESS or DDI_FAILURE
 *
 * Description: bnx_m_ioctl calls bnx_process_ioctl() to process the request.
 *
 ****************************************************************************/
static void
bnx_m_ioctl( void * arg, queue_t *q, mblk_t * mp )
{
	if( q == NULL || mp == NULL )
	{
		return;
	}

	switch( mp->b_datap->db_type )
	{
		case M_IOCTL:
			bnx_process_ioctl( arg, q, mp );
			break;

		default:
			miocnak(q, mp, EINVAL, 0);
			break;
	}
} /* bnx_m_ioctl */

#endif /* BNX_ENABLE_IOCTL */



/*******************************************************************************
 * Name:    bnx_blank
 *
 * Return:
 ******************************************************************************/
static void
bnx_blank( void * arg, time_t tick_cnt, uint_t pkt_cnt )
{
	u32_t ticks, frames;
	um_device_t * umdevice;
	lm_device_t * lmdevice;

	umdevice = (um_device_t *)arg;
	lmdevice = &(umdevice->lm_dev);

	if( umdevice->dev_start != B_TRUE )
	{
		return;
	}

	mutex_enter( &umdevice->os_param.gld_mutex );

	frames =  MIN((pkt_cnt & 0xffff0000), (LM_HC_RX_QUICK_CONS_TRIP_INT_MAX << 16));
	frames |= MIN((pkt_cnt & 0x0000ffff),  LM_HC_RX_QUICK_CONS_TRIP_VAL_MAX);

	ticks =  MIN((tick_cnt & 0xffff0000), (LM_HC_RX_TICKS_INT_MAX << 16));
	ticks |= MIN((tick_cnt & 0x0000ffff),  LM_HC_RX_TICKS_VAL_MAX);

	REG_WR( lmdevice, hc.hc_rx_quick_cons_trip, frames );
	REG_WR( lmdevice, hc.hc_rx_ticks, ticks );

	mutex_exit( &umdevice->os_param.gld_mutex );
} /* bnx_blank */


#ifdef MC_RESOURCES

/****************************************************************************
 * Name:    bnx_m_resources
 *
 * Input:   ptr to driver device structure.
 *
 * Return:  void
 ****************************************************************************/
static void
bnx_m_resources( void * arg )
{
	int i;
	mac_rx_fifo_t mrf;
	um_device_t * umdevice;
	lm_device_t * lmdevice;

	umdevice = (um_device_t *)arg;
	lmdevice = &(umdevice->lm_dev);

#if 0
	if( umdevice->dev_start == B_TRUE )
	{
/*
 * FIXME -- Verify m_resources is always called when dev_start == B_FALSE.
 * There is nothing we can do about this, but if this condition occurs, then
 * the driver API is racey.  By the time the driver sets dev_start to B_TRUE,
 * the rx engine is already running and could potentially call mac_rx().  If
 * anything in the upper layers truely needs the resource handles, undefined
 * things can happen.
 */
		return;
	}
#endif

	mrf.mrf_type  = MAC_RX_FIFO;
	mrf.mrf_blank = bnx_blank;
	mrf.mrf_arg   = (void *)umdevice;
	mrf.mrf_normal_blank_time = (lmdevice->params.rx_ticks_int << 16)
	                          | lmdevice->params.rx_ticks;
	mrf.mrf_normal_pkt_count =
	    (lmdevice->params.rx_quick_cons_trip_int << 16) |
	    lmdevice->params.rx_quick_cons_trip;

	for( i = 0; i < NUM_RX_CHAIN; i++ )
	{
		umdevice->os_param.rx_resc_handle[i] =
		    mac_resource_add(umdevice->os_param.macp,
			(mac_resource_t *)&mrf);
	}
} /* bnx_m_resources */

#endif

/*******************************************************************************
 * Name:    bnx_m_getcapab
 *
 * Return:
 ******************************************************************************/
static boolean_t
bnx_m_getcapab( void * arg, mac_capab_t cap, void * cap_data )
{
	um_device_t * umdevice;

	umdevice = (um_device_t *)arg;

	switch (cap) {
	case MAC_CAPAB_HCKSUM: {
		uint32_t *txflags = cap_data;

		*txflags = 0;

		if (umdevice->dev_var.enabled_oflds &
		    (LM_OFFLOAD_TX_IP_CKSUM | LM_OFFLOAD_RX_IP_CKSUM)) {
			*txflags |= HCKSUM_IPHDRCKSUM;
		}

		if (umdevice->dev_var.enabled_oflds &
		    (LM_OFFLOAD_TX_TCP_CKSUM | LM_OFFLOAD_TX_UDP_CKSUM |
		    LM_OFFLOAD_RX_TCP_CKSUM | LM_OFFLOAD_RX_UDP_CKSUM)) {
			*txflags |= HCKSUM_INET_FULL_V4;
		}
		break;
	}

#ifdef MAC_CAPAB_POLL

	case MAC_CAPAB_POLL:
		/*
		 * There's nothing for us to fill in, simply returning
		 * B_TRUE stating that we support polling is sufficient.
		 */
		break;

#endif

	default:
		return B_FALSE;
	}

	return B_TRUE;
} /* bnx_m_getcapab */



static mac_callbacks_t bnx_callbacks = {
#ifdef MC_RESOURCES
	(MC_RESOURCES | MC_IOCTL | MC_GETCAPAB),
#else
	(MC_IOCTL | MC_GETCAPAB),
#endif
	bnx_m_stats,
	bnx_m_start,
	bnx_m_stop,
	bnx_m_promiscuous,
	bnx_m_multicast,
	bnx_m_unicast,
	bnx_m_tx,
#ifdef MC_RESOURCES
	bnx_m_resources,
#else
	NULL,
#endif
	bnx_m_ioctl,
	bnx_m_getcapab
};



/****************************************************************************
 * Name:    bnx_gld_init
 *
 * Input:   ptr to device structure.
 *
 * Return:  DDI_SUCCESS or DDI_FAILURE
 *
 * Description:
 *          This routine populates mac info structure for this device
 *          instance and registers device with GLD.
 ****************************************************************************/
int bnx_gld_init( um_device_t * const umdevice )
{
	mac_register_t * macp;
	int rc;

	umdevice->dev_start = B_FALSE;

    mutex_init( &umdevice->os_param.gld_mutex, NULL,
                MUTEX_DRIVER, DDI_INTR_PRI(umdevice->intrPriority) );

	rw_init( &umdevice->os_param.gld_snd_mutex, NULL, RW_DRIVER, NULL );

	macp = mac_alloc( MAC_VERSION );
	if( macp == NULL )
	{
		cmn_err( CE_WARN,
		         "%s: Failed to allocate GLD MAC memory.\n",
		         umdevice->dev_name );
		goto error;
	}

	macp->m_driver     = umdevice;
	macp->m_dip        = umdevice->os_param.dip;
	macp->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	macp->m_callbacks  = &bnx_callbacks;
	macp->m_min_sdu    = 0;
	macp->m_max_sdu    = umdevice->dev_var.mtu;
	macp->m_src_addr   = &(umdevice->lm_dev.params.mac_addr[0]);

#ifdef MC_OPEN
	macp->m_margin = VLAN_TAG_SIZE;
#endif

	/*
	 * Call mac_register() after initializing all
	 * the required elements of mac_t struct.
	 */
	rc = mac_register( macp, &umdevice->os_param.macp );

	mac_free( macp );

	if( rc != 0 )
	{
		cmn_err( CE_WARN,
		         "%s: Failed to register with GLD.\n",
		         umdevice->dev_name );
		goto error;
	}

    /* Always report the initial link state as unknown. */
    bnx_gld_link( umdevice, LINK_STATE_UNKNOWN );

	return 0;

error:
	rw_destroy( &umdevice->os_param.gld_snd_mutex );
	mutex_destroy( &umdevice->os_param.gld_mutex );

	return -1;
} /* bnx_gld_init */



/*******************************************************************************
 * Name:    bnx_gld_link
 *
 * Return:
 ******************************************************************************/
void bnx_gld_link( um_device_t * const umdevice, const link_state_t linkup )
{
	mac_link_update( umdevice->os_param.macp, linkup );
} /* bnx_gld_link */



/*******************************************************************************
 * Name:    bnx_gld_fini
 *
 * Description:
 *
 * Return:
 ******************************************************************************/
int bnx_gld_fini( um_device_t * const umdevice )
{
	if( umdevice->dev_start != B_FALSE )
	{
		cmn_err( CE_WARN,
		         "%s: Detaching device from GLD that is still started!!!\n",
		         umdevice->dev_name );
		return -1;
	}

	if( mac_unregister(umdevice->os_param.macp) )
	{
		cmn_err( CE_WARN,
		         "%s: Failed to unregister with the GLD.\n",
		         umdevice->dev_name );
		return -1;
	}

	rw_destroy( &umdevice->os_param.gld_snd_mutex );
	mutex_destroy( &umdevice->os_param.gld_mutex );

	return 0;
} /* bnx_gld_fini */
