/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2006-2010 Broadcom Corporation, ALL RIGHTS RESERVED.
 ******************************************************************************/


#include "bnx.h"
#include "bnxcfg.h"

/* For mi_* functions */
#include <inet/mi.h>


/*
 * NDD parameter indexes, divided into:
 *
 *	read-only parameters describing the hardware's capabilities
 *	read-write parameters controlling the advertised capabilities
 *	read-only parameters describing the partner's capabilities
 *	read-only parameters describing the link state
 */
enum {
	PARAM_AUTONEG_CAP,
	PARAM_2500FDX_CAP,
	PARAM_1000FDX_CAP,
	PARAM_1000HDX_CAP,
	PARAM_100FDX_CAP,
	PARAM_100HDX_CAP,
	PARAM_10FDX_CAP,
	PARAM_10HDX_CAP,
	PARAM_TX_PAUSE_CAP,
	PARAM_RX_PAUSE_CAP,

	PARAM_ADV_AUTONEG_CAP,
	PARAM_ADV_2500FDX_CAP,
	PARAM_ADV_1000FDX_CAP,
	PARAM_ADV_1000HDX_CAP,
	PARAM_ADV_100FDX_CAP,
	PARAM_ADV_100HDX_CAP,
	PARAM_ADV_10FDX_CAP,
	PARAM_ADV_10HDX_CAP,
	PARAM_ADV_TX_PAUSE_CAP,
	PARAM_ADV_RX_PAUSE_CAP,

	PARAM_LP_AUTONEG_CAP,
	PARAM_LP_2500FDX_CAP,
	PARAM_LP_1000FDX_CAP,
	PARAM_LP_1000HDX_CAP,
	PARAM_LP_100FDX_CAP,
	PARAM_LP_100HDX_CAP,
	PARAM_LP_10FDX_CAP,
	PARAM_LP_10HDX_CAP,
	PARAM_LP_TX_PAUSE_CAP,
	PARAM_LP_RX_PAUSE_CAP,

	PARAM_AUTONEG_FLOW,

	PARAM_LINK_STATUS,
	PARAM_LINK_SPEED,
	PARAM_LINK_DUPLEX,
	PARAM_LINK_TX_PAUSE,
	PARAM_LINK_RX_PAUSE,

	PARAM_DISP_HW_CAP,
	PARAM_DISP_ADV_CAP,
	PARAM_DISP_LP_CAP,

	TOTAL_PARAM_COUNT
};



static const char * bnx_ndd_cfgnames[] =
{
	/* Our hardware capabilities. */
	"autoneg_cap",
	"2500fdx_cap",
	"1000fdx_cap",
	"1000hdx_cap",
	"100fdx_cap",
	"100hdx_cap",
	"10fdx_cap",
	"10hdx_cap",
	"txpause_cap",
	"rxpause_cap",

	/* Our advertised capabilities. */
	"adv_autoneg_cap",
	"adv_2500fdx_cap",
	"adv_1000fdx_cap",
	"adv_1000hdx_cap",
	"adv_100fdx_cap",
	"adv_100hdx_cap",
	"adv_10fdx_cap",
	"adv_10hdx_cap",
	"adv_txpause_cap",
	"adv_rxpause_cap",

	/* Partner's advertised capabilities. */
	"lp_autoneg_cap",
	"lp_2500fdx_cap",
	"lp_1000fdx_cap",
	"lp_1000hdx_cap",
	"lp_100fdx_cap",
	"lp_100hdx_cap",
	"lp_10fdx_cap",
	"lp_10hdx_cap",
	"lp_txpause_cap",
	"lp_rxpause_cap",
};



/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
static int
bnx_ndd_get_bool( queue_t * q, mblk_t * mp, caddr_t cp, cred_t * credp )
{
	char val;
	boolean_t * param;

	param = (boolean_t *)cp;

	if( *param == B_FALSE )
	{
		val = '0';
	}
	else
	{
		val = '1';
	}

	(void) mi_mpprintf( mp, "%c", val );

	return 0;
} /* bnx_ndd_get_bool */



/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
static int
bnx_ndd_get_int( queue_t * q, mblk_t * mp, caddr_t cp, cred_t * credp )
{
	int * param;

	param = (int *)cp;

	(void) mi_mpprintf( mp, "%d", *param );

	return 0;
} /* bnx_ndd_get_int */



/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
static int
bnx_ndd_get_link( queue_t * q, mblk_t * mp, caddr_t cp, cred_t * credp )
{
	int i;
	bnx_ndd_lnk_tbl_t * nddlnk;
	const char      ** lnklbl;
	const boolean_t *  lnkval;

	nddlnk = (bnx_ndd_lnk_tbl_t *)cp;

	lnklbl = nddlnk->label;
	lnkval = nddlnk->value;

	for( i = 0; i < sizeof(bnx_lnk_cfg_t) / sizeof(boolean_t); i++ )
	{
		(void) mi_mpprintf( mp, "%s\t%d", *lnklbl, *lnkval );

		lnklbl++;
		lnkval++;
	}

	return 0;
} /* bnx_ndd_get_link */



/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
static int
bnx_ndd_set_bool( queue_t *  q,
                  mblk_t  * mp,
                  char    * value,
                  caddr_t   cp,
                  cred_t  * credp )
{
	boolean_t * param;

	if( value == NULL || value[1] != '\0' ||
	   (value[0] != '0' && value[0] != '1') )
	{
		return EINVAL;
	}

	param = (boolean_t *)cp;

	if( value[0] == '1' )
	{
		*param = B_TRUE;
	}
	else
	{
		*param = B_FALSE;
	}

	return 0;
} /* bnx_ndd_set_bool */



/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
static int
bnx_ndd_reg_bool_param( um_device_t * const umdevice,
                        const char  * const    label,
                        boolean_t           settable,
                        boolean_t   * const  valaddr )
{
	int rc;

	rc = nd_load( &(umdevice->nddcfg.ndd_data),
	              (char *)label,
	              bnx_ndd_get_bool,
	              (settable == B_TRUE) ? bnx_ndd_set_bool : NULL,
	              (caddr_t)valaddr );
	if( rc != B_TRUE )
	{
		cmn_err( CE_WARN,
		         "%s: Failed registering %s parameter with NDD.",
		         umdevice->dev_name, label );
	}

	return rc != B_TRUE;
} /* bnx_ndd_reg_bool_param */



/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
static int
bnx_ndd_reg_int_param( um_device_t * const umdevice,
                       const char  * const    label,
                       int         * const  valaddr )
{
	int rc;

	rc = nd_load( &(umdevice->nddcfg.ndd_data),
	              (char *)label,
	              bnx_ndd_get_int,
	              NULL,
	              (caddr_t)valaddr );
	if( rc != B_TRUE )
	{
		cmn_err( CE_WARN,
		         "%s: Failed registering %s parameter with NDD.",
		         umdevice->dev_name, label );
	}

	return rc != B_TRUE;
} /* bnx_ndd_reg_int_param */



/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
static int
bnx_ndd_reg_link_params( um_device_t   * const umdevice,
                         unsigned int          lblindex,
                         bnx_lnk_cfg_t * const umphycfg,
                         const bnx_lnk_cfg_t * const umphycap )
{
	int              i;
	int             rc;
	boolean_t * nddval;
	const boolean_t * nddcap;

	nddval = &(umphycfg->link_autoneg);
	if( umphycap != NULL )
	{
		nddcap = &(umphycap->link_autoneg);
	}
	else
	{
		nddcap = NULL;
	}

	for( i = 0; i < sizeof(bnx_lnk_cfg_t) / sizeof(boolean_t); i++ )
	{
		rc = bnx_ndd_reg_bool_param( umdevice,
		                             bnx_ndd_cfgnames[lblindex + i],
		                             nddcap ? *nddcap++ : NULL,
		                             nddval++ );
		if( rc != 0 )
		{
			goto error;
		}
	}

	return 0;

error:
	return -1;
} /* bnx_ndd_reg_link_params */



/****************************************************************************
 * Name:        bnx_nd_ioctl
 *
 * Input:       ptr to device structure
 *
 * Return:      None
 *
 * Description: Main IOCTL processing function, calls nd_getset() for
 *              actual parameter parsing, credential evaluation and
 *              request handling.
 ****************************************************************************/
enum ioc_reply
bnx_nd_ioctl( um_device_t   * const umdevice,
              queue_t       *             wq,
              mblk_t        *             mp,
              struct iocblk *           iocp )
{
	boolean_t ok;

	switch (iocp->ioc_cmd) {
	default:
		/* NOTREACHED */
		return (IOC_INVAL);

	case ND_GET:
		/*
		 * If nd_getset() returns B_FALSE, the command was
		 * not valid (e.g. unknown name), so we just tell the
		 * top-level ioctl code to send a NAK (with code EINVAL).
		 *
		 * Otherwise, nd_getset() will have built the reply to
		 * be sent (but not actually sent it), so we tell the
		 * caller to send the prepared reply.
		 */
		ok = nd_getset(wq, umdevice->nddcfg.ndd_data, mp);
		return (ok ? IOC_REPLY : IOC_INVAL);

	case ND_SET:
		ok = nd_getset(wq, umdevice->nddcfg.ndd_data, mp);

		/*
		 * If nd_getset() returns B_FALSE, the command was
		 * not valid (e.g. unknown name), so we just tell
		 * the top-level ioctl code to send a NAK (with code
		 * EINVAL by default).
		 *
		 * Otherwise, nd_getset() will have built the reply to
		 * be sent - but that doesn't imply success!  In some
		 * cases, the reply it's built will have a non-zero
		 * error code in it (e.g. EPERM if not superuser).
		 * So, we also drop out in that case ...
		 */

		if (!ok)
			return (IOC_INVAL);
		if (iocp->ioc_error)
			return (IOC_REPLY);

		return (IOC_RESTART_REPLY);
	}
}



/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
int
bnx_ndd_init( um_device_t * const umdevice )
{
	int rc;
	bnx_ndd_t * nddcfg;
	const bnx_lnk_cfg_t * linkconf;

	nddcfg = &(umdevice->nddcfg);

	if( umdevice->dev_var.isfiber )
	{
		linkconf = &bnx_serdes_config;
	}
	else
	{
		linkconf = &bnx_copper_config;
	}

	rc = bnx_ndd_reg_link_params( umdevice, PARAM_AUTONEG_CAP,
	                              (bnx_lnk_cfg_t *)linkconf, NULL );
	if( rc != 0 )
	{
		goto nd_fail;
	}

	rc = bnx_ndd_reg_link_params( umdevice, PARAM_ADV_AUTONEG_CAP,
	                              &(umdevice->curcfg.lnkcfg), linkconf );
	if( rc != 0 )
	{
		goto nd_fail;
	}

	rc = bnx_ndd_reg_link_params( umdevice, PARAM_LP_AUTONEG_CAP,
	                              &(umdevice->remote), NULL );
	if( rc != 0 )
	{
		goto nd_fail;
	}

	rc = bnx_ndd_reg_bool_param( umdevice,
	                             "autoneg_flow", B_TRUE,
	                             &(umdevice->curcfg.flow_autoneg) );
	if( rc != 0 )
	{
		goto nd_fail;
	}

	rc = bnx_ndd_reg_bool_param( umdevice,
	                             "wirespeed", B_TRUE,
	                             &(umdevice->curcfg.wirespeed) );
	if( rc != 0 )
	{
		goto nd_fail;
	}

	rc = bnx_ndd_reg_bool_param( umdevice,
	                             "link_status", B_FALSE,
	                             (boolean_t *)&(nddcfg->link_speed) );
	if( rc != 0 )
	{
		goto nd_fail;
	}

	rc = bnx_ndd_reg_int_param( umdevice,
	                            "link_speed",
	                            &(nddcfg->link_speed) );
	if( rc != 0 )
	{
		goto nd_fail;
	}

	rc = bnx_ndd_reg_bool_param( umdevice,
	                             "link_duplex", B_FALSE,
	                             &(nddcfg->link_duplex) );
	if( rc != 0 )
	{
		goto nd_fail;
	}

	rc = bnx_ndd_reg_bool_param( umdevice,
	                             "link_tx_pause", B_FALSE,
	                             &(nddcfg->link_tx_pause) );
	if( rc != 0 )
	{
		goto nd_fail;
	}

	rc = bnx_ndd_reg_bool_param( umdevice,
	                             "link_rx_pause", B_FALSE,
	                             &(nddcfg->link_rx_pause) );
	if( rc != 0 )
	{
		goto nd_fail;
	}

	nddcfg->lnktbl[0].label = &(bnx_ndd_cfgnames[PARAM_AUTONEG_CAP]);
	nddcfg->lnktbl[0].value = &(linkconf->link_autoneg);
	rc = nd_load( &(nddcfg->ndd_data), "hw_cap",
	              bnx_ndd_get_link, NULL,
	              (caddr_t)&(nddcfg->lnktbl[0]) );
	if( rc != B_TRUE )
	{
		goto nd_fail;
	}

	nddcfg->lnktbl[1].label = &(bnx_ndd_cfgnames[PARAM_ADV_AUTONEG_CAP]);
	nddcfg->lnktbl[1].value = &(umdevice->curcfg.lnkcfg.link_autoneg);
	rc = nd_load( &(nddcfg->ndd_data), "adv_cap",
	              bnx_ndd_get_link, NULL,
	              (caddr_t)&(nddcfg->lnktbl[1]) );
	if( rc != B_TRUE )
	{
		goto nd_fail;
	}

	nddcfg->lnktbl[2].label = &(bnx_ndd_cfgnames[PARAM_LP_AUTONEG_CAP]);
	nddcfg->lnktbl[2].value = &(umdevice->remote.link_autoneg);
	rc = nd_load( &(nddcfg->ndd_data), "lp_cap",
	              bnx_ndd_get_link, NULL,
	              (caddr_t) &(nddcfg->lnktbl[2]) );
	if( rc != B_TRUE )
	{
		goto nd_fail;
	}

	return 0;

nd_fail:

	nd_free( &(nddcfg->ndd_data) );

	return -1;
} /* bnx_ndd_init */



/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void
bnx_ndd_fini( um_device_t * const umdevice )
{
	if( umdevice->nddcfg.ndd_data != NULL )
	{
		nd_free(&umdevice->nddcfg.ndd_data);
		umdevice->nddcfg.ndd_data = NULL;
	}
} /* bnx_ndd_fini */
