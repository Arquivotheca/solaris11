/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2007-2010 Broadcom Corporation, ALL RIGHTS RESERVED.
 ******************************************************************************/


#include "bnxtmr.h"
#include "bnxrcv.h"
#include "bnxgld.h"


/* 1.5 seconds */
#define BNX_LINK_CHECK_INTERVAL 10

/* Approximately every second. */
#define BNX_LINK_CHECK_INTERVAL2 7

/* 500 msecs */
#define BNX_TIMER_INTERVAL      500000


typedef struct _bnx_fw_t {
	u32_t shmemaddr;
	u32_t length;
	u32_t nvramaddr;
} bnx_fw_t;


/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
static void bnx_link_check( lm_device_t * const lmdevice )
{
	if( lmdevice->vars.link_status == LM_STATUS_LINK_ACTIVE )
	{
		/*
		 * If we have link and we are in the fallback (1gb forced),
		 * mode, we need to see if our link partner is sending us
		 * configs.  If this is the case, we'll switch back to autoneg.
		 */
		if( lmdevice->vars.serdes_fallback_status )
		{
			u32_t intr_exp_status;

			lm_mwrite( lmdevice, lmdevice->params.phy_addr, 0x17, 0x0f01 );
			lm_mread( lmdevice, lmdevice->params.phy_addr, 0x15, &intr_exp_status );
			lm_mread( lmdevice, lmdevice->params.phy_addr, 0x15, &intr_exp_status );

			if( intr_exp_status & 0x20 )
			{
				lm_mwrite( lmdevice, lmdevice->params.phy_addr, PHY_CTRL_REG,
				           PHY_CTRL_AUTO_NEG_ENABLE | PHY_CTRL_RESTART_AUTO_NEG );
			}
		}
	}
	else
	{
		lm_service_phy_int( lmdevice, TRUE );
	}
} /* bnx_link_check */



/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
static void bnx_link_check2( lm_device_t * const lmdevice )
{
	if( lmdevice->vars.link_status == LM_STATUS_LINK_ACTIVE )
	{
		u32_t val;
		u32_t phy_addr;

		phy_addr = lmdevice->params.phy_addr;

		/* Is the link really up? */
		lm_mwrite( lmdevice, phy_addr, 0x1c, 0x6800 );
		lm_mread( lmdevice, phy_addr, 0x1c, &val );
		lm_mread( lmdevice, phy_addr, 0x1c, &val );

		if( val & 2 )
		{
			/* Nope.  Force the link down. */
			lm_mwrite( lmdevice, phy_addr, 0x17, 0x0f03 );
			lm_mread( lmdevice, phy_addr, 0x15, &val );
			lm_mwrite( lmdevice, phy_addr, 0x15, val & 0xff0f );

			lmdevice->vars.bcm5706s_tx_drv_cur = (u16_t) val;
		}
	}
} /* bnx_link_check */



/****************************************************************************
 * Name:    bnx_timer
 *
 * Input:   ptr to device structure
 *
 * Return:  None
 *
 * Description: bnx_timer is the periodic timer callback funtion.
 ****************************************************************************/
static void bnx_timer( void * arg )
{
	lm_device_t * lmdevice;
	um_device_t * umdevice;

	umdevice = (um_device_t *)arg;
	lmdevice = &(umdevice->lm_dev);

	mutex_enter( &umdevice->tmr_mutex );

	if( umdevice->timer_enabled != B_TRUE )
	{
		goto done;
	}

	um_send_driver_pulse( umdevice );

	/*
	 * Take this opportunity to replenish any unused Rx Bds.  Don't
	 * wait around for the rcv_mutex though.  We share the
	 * responsibility of replenishing the rx buffers with the ISR.
	 */
	if( mutex_tryenter(&umdevice->os_param.rcv_mutex) )
	{
		/* This function does an implicit *_fill(). */
		bnx_rxpkts_post( umdevice );

		mutex_exit( &umdevice->os_param.rcv_mutex );
	}

	if( umdevice->timer_link_check_interval2 )
	{
		/*
		 * If enabled, check to see if the serdes
		 * PHY can fallback to a forced mode.
		 */
		if( umdevice->timer_link_check_interval )
		{
			if( umdevice->timer_link_check_counter )
			{
				if( umdevice->timer_link_check_counter == 1 )
				{
					mutex_enter( &umdevice->os_param.phy_mutex );
					bnx_link_check( lmdevice );
					mutex_exit( &umdevice->os_param.phy_mutex );
				}
				umdevice->timer_link_check_counter--;
			}
		}

		umdevice->timer_link_check_counter2--;
		if( umdevice->timer_link_check_counter2 == 0 )
		{
			mutex_enter( &umdevice->os_param.phy_mutex );
			bnx_link_check2( lmdevice );
			mutex_exit( &umdevice->os_param.phy_mutex );

			umdevice->timer_link_check_counter2 = umdevice->timer_link_check_interval2;
		}
	}

	FLUSHPOSTEDWRITES( lmdevice );

	umdevice->tmrtid = timeout( bnx_timer, (void *)umdevice,
	                            drv_usectohz(BNX_TIMER_INTERVAL) );

done:
	mutex_exit( &umdevice->tmr_mutex );
} /* bnx_timer */



/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void bnx_timer_start( um_device_t * const umdevice )
{
	lm_device_t * lmdevice;

	lmdevice = &(umdevice->lm_dev);

	umdevice->timer_enabled = B_TRUE;

	if( CHIP_NUM(lmdevice) == CHIP_NUM_5706 &&
	    umdevice->dev_var.isfiber == B_TRUE )
	{
	    if( lmdevice->vars.serdes_fallback_select != SERDES_FALLBACK_NONE )
		{
			umdevice->timer_link_check_interval = BNX_LINK_CHECK_INTERVAL;
		}
		else
		{
			umdevice->timer_link_check_interval = 0;
		}

		umdevice->timer_link_check_interval2 = BNX_LINK_CHECK_INTERVAL2;
		umdevice->timer_link_check_counter2 = umdevice->timer_link_check_interval2;
	}
	else
	{
		umdevice->timer_link_check_interval2 = 0;
	}

	umdevice->tmrtid = timeout( bnx_timer, (void *)umdevice,
	                            drv_usectohz(BNX_TIMER_INTERVAL) );
} /* bnx_timer_start */



/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void bnx_timer_stop( um_device_t * const umdevice )
{
	umdevice->timer_enabled = B_FALSE;

	mutex_enter( &umdevice->tmr_mutex );
	mutex_exit( &umdevice->tmr_mutex );

	untimeout( umdevice->tmrtid );
	umdevice->tmrtid = 0;
} /* bnx_timer_stop */



/****************************************************************************
 * Name:	bnx_link_timer_restart
 *
 * Input:	ptr to device structure
 *
 * Return:	None
 *
 * Description:	This function restarts the link poll timer
 *
 ****************************************************************************/
void bnx_link_timer_restart( um_device_t * const umdevice )
{
	/* FIXME -- Make timer_link_check_counter atomic */
	umdevice->timer_link_check_counter = umdevice->timer_link_check_interval;
} /* bnx_link_timer_restart */



/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void bnx_timer_init( um_device_t * const umdevice )
{
	mutex_init( &umdevice->tmr_mutex, NULL, MUTEX_DRIVER,
                DDI_INTR_PRI(umdevice->intrPriority) );
} /* bnx_timer_init */



/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void bnx_timer_fini( um_device_t * const umdevice )
{
	mutex_destroy( &umdevice->tmr_mutex );
} /* bnx_timer_fini */
