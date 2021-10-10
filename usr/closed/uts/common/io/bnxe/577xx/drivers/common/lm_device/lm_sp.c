/*******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2001-2011 Broadcom Corporation, ALL RIGHTS RESERVED.
 *
 *
 * Module Description:
 *      This file contains the implementation of slow-path operations
 *      for L2 + Common. It uses ecore_sp_verbs in most cases. 
 *
 ******************************************************************************/

#include "lm5710.h"

#if !defined(__LINUX) && !defined(__SunOS)
// disable warning C4127 (conditional expression is constant)
// for this file (relevant when compiling with W4 warning level)
#pragma warning( disable : 4127 )
#endif /* __LINUX */

#if !defined(__LINUX) && !defined(__SunOS)
#pragma warning( default : 4127 )
#endif

#include "mm.h"
#include "context.h"
#include "command.h"
#include "bd_chain.h"
#include "ecore_common.h"
#include "ecore_sp_verbs.h"

/*********************** NIV **************************************/

/**lm_niv_post_command
 * Post a NIV ramrod and wait for its completion.
 * 
 * 
 * @param pdev the device
 * @param command the ramrod cmd_id (NONE_CONNECTION_TYPE is 
 *                assumed)
 * @param data the ramrod data
 * 
 * @return lm_status_t LM_STATUS_SUCCESS on success, some other 
 *         failure code on failure.
 */
static lm_status_t lm_niv_post_command( struct _lm_device_t *pdev,
                                        IN const u8_t       command,
                                        IN const u64_t      data)
{
    lm_status_t lm_status = LM_STATUS_FAILURE;

    DbgBreakIf(pdev->slowpath_info.niv_ramrod_state != NIV_RAMROD_NOT_POSTED);

    lm_status = lm_eq_ramrod_post_sync(pdev,command,data,CMD_PRIORITY_NORMAL,&pdev->slowpath_info.niv_ramrod_state, NIV_RAMROD_POSTED, NIV_RAMROD_COMPLETED);
    if (LM_STATUS_SUCCESS != lm_status)
    {
        DbgBreakIf(LM_STATUS_SUCCESS != lm_status);
        goto _exit;
    }

_exit:
    pdev->slowpath_info.niv_ramrod_state = NIV_RAMROD_NOT_POSTED;
    return lm_status;
}

lm_status_t lm_niv_vif_update(struct _lm_device_t *pdev,
                              IN const u16_t       vif_id,
                              IN const u16_t       default_vlan,
                              IN const u8_t        allowed_priorities)
{
    lm_status_t                         lm_status   = LM_STATUS_FAILURE;
    struct function_niv_update_data*    data        = LM_SLOWPATH(pdev, niv_function_update_data);
    lm_address_t                        data_phys   = LM_SLOWPATH_PHYS(pdev, niv_function_update_data);

    data->vif_id = vif_id;
    data->niv_default_vlan = default_vlan;
    data->allowed_priorities = allowed_priorities;

    lm_status = lm_niv_post_command(pdev,RAMROD_CMD_ID_COMMON_NIV_FUNCTION_UPDATE, data_phys.as_u64);

    return lm_status;
}

lm_status_t lm_niv_vif_list_update(struct _lm_device_t *pdev,
                                   IN const enum vif_list_rule_kind command,
                                   IN const u16_t                   list_index,
                                   IN const u8_t                    func_bit_map,
                                   IN const u8_t                    func_to_clear)
{
    struct niv_vif_list_ramrod_data data        = {0};
    lm_status_t                     lm_status   = LM_STATUS_FAILURE;

    data.func_bit_map = func_bit_map;
    data.func_to_clear = func_to_clear;
    data.niv_vif_list_command = command;
    data.vif_list_index = list_index;
    data.echo = command;

    lm_status = lm_niv_post_command(pdev,RAMROD_CMD_ID_COMMON_NIV_VIF_LISTS, *((u64_t*)(&data)));

    return lm_status;
}



/****************** CLASSIFICATION ********************************/
/** 
 * Set/Unset a mac-address or mac-vlan pair on a given chain. 
 * 
 * @param pdev
 * @param mac_addr  - array of size ETHERNET_ADDRESS_SIZE 
 *                    containing a valid mac addresses
 * @param vlan_tag  - vlan tag to be set with mac address
 * @param chain_idx - which chain to set the mac on. Chain_idx 
 *                    will be transformed to a l2 client-id
 * @param cookie    - will be returned to MM layer on completion
 * @param set       - set or remove mac address
 * 
 * @return lm_status_t SUCCESS on syncrounous success, PENDING 
 *         if completion will be called later, FAILURE o/w
 */
lm_status_t lm_set_mac_addr(struct _lm_device_t *pdev,
                            u8_t                *mac_addr, 
                            u16_t               vlan_tag,
                            u8_t                chain_idx,
                            void*               cookie,
                            u8_t                set )
{
    struct ecore_vlan_mac_ramrod_params ramrod_param       = { 0 };
    lm_status_t                         lm_status          = LM_STATUS_FAILURE;
    u8_t                                cli_cam_idx_offset = 0;
    lm_cli_idx_t                        lm_cli_idx         = LM_CLI_IDX_MAX;
    u8_t                                cid                = chain_idx; // FIXME!!! 

    if ERR_IF(!pdev || !mac_addr)
    {
        DbgBreakMsg("lm_set_mac_addr: invalid params\n");
        return LM_STATUS_INVALID_PARAMETER;
    }

    /* this is here for some reason due to basic vf but shouldn't be with a proper basic vf design... */
    //if (lm_reset_is_inprogress(pdev))
    //{
     //   DbgMessage(pdev, FATAL, "lm_set_mac_addr: Under FLR!!!\n");
      //  return  LM_STATUS_SUCCESS;
   // }

#ifdef VF_INVOLVED
    if (IS_CHANNEL_VFDEV(pdev))
    {
        lm_status = lm_vf_pf_set_q_filters(pdev, LM_CLI_IDX_NDIS, cookie, Q_FILTER_MAC, mac_addr, (mac_addr ? ETHERNET_ADDRESS_SIZE : 0),vlan_tag);
        return lm_status;
    }
#endif

    DbgMessage2(pdev, INFORMl2sp, "lm_set_mac_addr: set=%d chain_idx=%d!!!\n",
               set, chain_idx);
    DbgMessage7(pdev, INFORMl2sp, "lm_set_mac_addr: [%d]:[%d]:[%d]:[%d]:[%d]:[%d] set=%d chain_idx=%d!!!\n",
               mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5], mac_addr[6]);

    /* Prepare ramrod params to be sent to ecore layer... */
    mm_memset(&ramrod_param, 0, sizeof(ramrod_param));

    if (vlan_tag != LM_SET_CAM_NO_VLAN_FILTER)
    {
        DbgBreakIf(CHIP_IS_E1(pdev));
        mm_memcpy(ramrod_param.data.vlan_mac.mac, mac_addr, ETHERNET_ADDRESS_SIZE);
        ramrod_param.data.vlan_mac.vlan = vlan_tag;
        ramrod_param.vlan_mac_obj = &pdev->client_info[chain_idx].mac_vlan_obj;
    }
    else
    {
        mm_memcpy(ramrod_param.data.mac.mac, mac_addr, ETHERNET_ADDRESS_SIZE);
        ramrod_param.vlan_mac_obj = &pdev->client_info[chain_idx].mac_obj;
    }


    /* Set the cookie BEFORE sending the ramrod!!!! ramrod may complete in the mean time... */
    DbgBreakIf(pdev->client_info[cid].set_mac_cookie != NULL);
    pdev->client_info[cid].set_mac_cookie = cookie;

    lm_status = ecore_config_vlan_mac(pdev, &ramrod_param, set);
    if (lm_status == LM_STATUS_SUCCESS)
    {
        lm_cli_idx = LM_CHAIN_IDX_CLI(pdev, chain_idx);
        /* If set_cam succeeded in FW - set the mac in Nig */
        if (lm_set_mac_in_nig(pdev, set? mac_addr:NULL, lm_cli_idx, cli_cam_idx_offset) != LM_STATUS_SUCCESS)
        {
            lm_status = LM_STATUS_FAILURE;
            pdev->client_info[cid].set_mac_cookie = NULL; // rollback
        }
        
        lm_status = LM_STATUS_PENDING;
        
    }
    else
    {
        pdev->client_info[cid].set_mac_cookie = NULL; // rollback
    }
    return lm_status;
}


/** 
 *  Move a filter from one chain idx to another atomically
 * 
 * @param pdev
 * 
 * @param mac_addr       - array of size ETHERNET_ADDRESS_SIZE 
 *                         containing a valid mac addresses
 * @param vlan_tag       - vlan tag to be set with mac address
 * @param src_chain_idx  - which chain to remove the mac from
 * @param dest_chain_idx - which chain to set the mac on
 * @param cookie         - will be returned to MM layer on completion
 * 
 * @return lm_status_t
 */
lm_status_t lm_move_mac_addr(struct _lm_device_t *pdev, u8_t *mac_addr, u16_t vlan_tag, 
                             u8_t src_chain_idx,  u8_t dest_chain_idx, void * cookie)
{
    struct ecore_vlan_mac_ramrod_params ramrod_param       = { 0 };
    struct ecore_vlan_mac_obj          *dest_obj           = NULL;
    lm_status_t                         lm_status          = LM_STATUS_FAILURE;
    u8_t                                cid                = src_chain_idx; // FIXME!!! 

    if ERR_IF(!pdev || !mac_addr)
    {
        DbgBreakMsg("lm_set_mac_addr: invalid params\n");
        return LM_STATUS_INVALID_PARAMETER;
    }

#ifdef VF_INVOLVED
    if (IS_CHANNEL_VFDEV(pdev))
    {
        DbgBreakMsg("Move not expected on VF\n");        
        return lm_status;
    }
#endif

    DbgMessage2(pdev, INFORMl2sp, "lm_move_mac_addr: src_chain_idx=%d dest_chain_idx=%d!!!\n",
               src_chain_idx, dest_chain_idx);
    DbgMessage7(pdev, INFORMl2sp, "lm_move_mac_addr: [%d]:[%d]:[%d]:[%d]:[%d]:[%d] set=%d chain_idx=%d!!!\n",
               mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5], mac_addr[6]);

    /* Prepare ramrod params to be sent to ecore layer... */
    mm_memset(&ramrod_param, 0, sizeof(ramrod_param));

    if (vlan_tag != LM_SET_CAM_NO_VLAN_FILTER)
    {
        mm_memcpy(ramrod_param.data.vlan_mac.mac, mac_addr, ETHERNET_ADDRESS_SIZE);
        ramrod_param.data.vlan_mac.vlan = vlan_tag;
        ramrod_param.vlan_mac_obj = &pdev->client_info[src_chain_idx].mac_vlan_obj;
        dest_obj = &pdev->client_info[dest_chain_idx].mac_vlan_obj;
    }
    else
    {
        mm_memcpy(ramrod_param.data.mac.mac, mac_addr, ETHERNET_ADDRESS_SIZE);
        ramrod_param.vlan_mac_obj = &pdev->client_info[src_chain_idx].mac_obj;
        dest_obj = &pdev->client_info[dest_chain_idx].mac_obj;
    }


    /* Set the cookie BEFORE sending the ramrod!!!! ramrod may complete in the mean time... */
    DbgBreakIf(pdev->client_info[cid].set_mac_cookie != NULL);
    pdev->client_info[cid].set_mac_cookie = cookie;

    lm_status = ecore_vlan_mac_move(pdev, &ramrod_param, dest_obj);
    if (lm_status == LM_STATUS_SUCCESS)
    {
        /* FIXME: VF MACS in NIG stay??*/
        lm_status = LM_STATUS_PENDING;
    }
    else
    {
        pdev->client_info[cid].set_mac_cookie = NULL; // rollback
    }
    return lm_status;
}


/** 
 * @Description
 *      Waits for the last set-mac called to complete, could be
 *      set-mac or set-mac-vlan... 
 * @param pdev
 * @param chain_idx - the same chain-idx that the set-mac was 
 *                  called on
 * 
 * @return lm_status_t SUCCESS or TIMEOUT
 */
lm_status_t lm_wait_set_mac_done(struct _lm_device_t *pdev, u8_t chain_idx)
{
    struct ecore_raw_obj * raw;
    lm_status_t lm_status = LM_STATUS_FAILURE;

    raw = &pdev->client_info[chain_idx].mac_obj.raw;
    lm_status = raw->wait_comp(pdev, raw);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        DbgBreakIf(lm_status != LM_STATUS_SUCCESS);
        return lm_status;
    }

    if (!CHIP_IS_E1(pdev))
    {
        raw = &pdev->client_info[chain_idx].mac_vlan_obj.raw;
        lm_status = raw->wait_comp(pdev, raw);
        DbgBreakIf(lm_status != LM_STATUS_SUCCESS);
    }
 
    return lm_status;
}


/** 
 * Description
 *      Clears all the mac address that are set on a certain cid... 
 * @param pdev
 * @param chain_idx - which chain_idx to clear macs on... 
 * 
 * @assumptions: Called in PASSIVE_LEVEL!! function sleeps... 
 * @return lm_status_t
 */
lm_status_t lm_clear_all_mac_addr(struct _lm_device_t *pdev, u8_t chain_idx)
{
    struct ecore_vlan_mac_ramrod_params p    = {0};
    struct ecore_vlan_mac_obj * vlan_mac_obj = NULL;
	lm_status_t                 lm_status    = LM_STATUS_FAILURE;

    DbgMessage1(pdev, INFORMl2sp, "lm_clear_all_mac_addr chain_idx=%d\n", chain_idx);

    vlan_mac_obj = &pdev->client_info[chain_idx].mac_obj;
	p.vlan_mac_obj = vlan_mac_obj;
    
	ECORE_SET_BIT(RAMROD_COMP_WAIT, &p.ramrod_flags);

    lm_status = vlan_mac_obj->del_next(pdev, &p);
    while (lm_status == LM_STATUS_PENDING)
    {
        lm_status = vlan_mac_obj->del_next(pdev, &p);
    }

    if (lm_status != LM_STATUS_SUCCESS)
    {
        DbgBreakIf(lm_status != LM_STATUS_SUCCESS);
        return lm_status;
    }

    /* Take care of the pairs as well...except on E1 */
    if (!CHIP_IS_E1(pdev)) 
    {
        vlan_mac_obj = &pdev->client_info[chain_idx].mac_vlan_obj;
        p.vlan_mac_obj = vlan_mac_obj;
        ECORE_SET_BIT(RAMROD_COMP_WAIT, &p.ramrod_flags);
    
        lm_status = vlan_mac_obj->del_next(pdev, &p);
    
        while (lm_status == LM_STATUS_PENDING)
        {
            lm_status = vlan_mac_obj->del_next(pdev, &p);
        }
    }
	
    DbgBreakIf(lm_status != LM_STATUS_SUCCESS);
    return lm_status;
}



/** 
 * Description
 *      Restores all the mac address that are set on a certain
 *      cid (after sleep / hibernate...) 
 * @param pdev
 * @param chain_idx - which chain_idx to clear macs on... 
 * 
 * @assumptions: Called in PASSIVE_LEVEL!! function sleeps... 
 * @return lm_status_t
 */
lm_status_t lm_restore_all_mac_addr(struct _lm_device_t *pdev, u8_t chain_idx)
{
    struct ecore_vlan_mac_ramrod_params p    = {0};
    struct ecore_vlan_mac_obj * vlan_mac_obj = NULL;
	lm_status_t                 lm_status    = LM_STATUS_FAILURE;
    struct ecore_list_elem    * pos         = NULL;

    DbgMessage1(pdev, INFORMl2sp, "lm_clear_all_mac_addr chain_idx=%d\n", chain_idx);

    vlan_mac_obj = &pdev->client_info[chain_idx].mac_obj;
	p.vlan_mac_obj = vlan_mac_obj;
    
	ECORE_SET_BIT(RAMROD_COMP_WAIT, &p.ramrod_flags);

    do {
        lm_status = vlan_mac_obj->restore(pdev, &p, &pos);
        if (lm_status != LM_STATUS_SUCCESS)
        {
            DbgBreakIf(lm_status != LM_STATUS_SUCCESS);
            return lm_status;
        }
    } while (pos != NULL);

    /* Take care of the pairs as well... */
    if (!CHIP_IS_E1(pdev))
    {
        vlan_mac_obj = &pdev->client_info[chain_idx].mac_vlan_obj;
        p.vlan_mac_obj = vlan_mac_obj;
        ECORE_SET_BIT(RAMROD_COMP_WAIT, &p.ramrod_flags);
    
        pos = NULL;
        do {
            lm_status = vlan_mac_obj->restore(pdev, &p, &pos);
            if (lm_status != LM_STATUS_SUCCESS)
            {
                DbgBreakIf(lm_status != LM_STATUS_SUCCESS);
                return lm_status;
            }
        } while (pos != NULL);
      
    }
    
    DbgBreakIf(lm_status != LM_STATUS_SUCCESS);
    return lm_status;
}

/************************ RX FILTERING ***************************************/
 
/**
 * @Description
 *  - set/unset rx filtering for a client. The setting is done
 *    for RX + TX, since tx switching is enabled FW needs to
 *    know the configuration for tx filtering as well. The
 *    configuration is almost semmetric for rx / tx except for
 *    the case of promiscuous in which case rx is in
 *    accept_unmatched and Tx is in accept_all (meaning all
 *    traffic is sent to loopback channel)
 *  
 * @Assumptions
 *  - An inter client lock is taken by the caller
 * @Return
 *  - Success / Pending or Failure
 */
lm_status_t
lm_set_rx_mask(lm_device_t *pdev, u8_t chain_idx, lm_rx_mask_t rx_mask,  void * cookie)
{
    struct ecore_rx_mode_ramrod_params ramrod_param    = {0};
    lm_cli_idx_t                       lm_cli_idx      = LM_CLI_IDX_MAX;
    unsigned long                      rx_accept_flags = 0;
    unsigned long                      tx_accept_flags = 0;
    lm_status_t                        lm_status       = LM_STATUS_SUCCESS;

    DbgMessage2(pdev, INFORMl2sp, "lm_set_rx_mask chain_idx=%d rx_mask=%d\n", chain_idx, rx_mask);

    #ifdef VF_INVOLVED
    if (IS_CHANNEL_VFDEV(pdev))
    {
        return lm_vf_pf_set_q_filters(pdev, chain_idx, FALSE, Q_FILTER_RX_MASK, (u8_t*)&rx_mask, sizeof(lm_rx_mask_t), LM_SET_CAM_NO_VLAN_FILTER);
    }
    #endif

    if (pdev->client_info[chain_idx].last_set_rx_mask == rx_mask)
    {
        /* No need to send a filter that has already been set... 
           return immediately */
        DbgMessage(pdev, INFORMl2sp, "lm_set_rx_mask returning immediately: mask didn't change!\n");
        return LM_STATUS_SUCCESS;
    }

    /* initialize accept flags in ECORE language */
    ECORE_SET_BIT(ECORE_ACCEPT_ANY_VLAN, &rx_accept_flags);
    ECORE_SET_BIT(ECORE_ACCEPT_ANY_VLAN, &tx_accept_flags);

    /* find the desired filtering configuration */
    if GET_FLAGS(rx_mask ,LM_RX_MASK_PROMISCUOUS_MODE)
    {
        ECORE_SET_BIT(ECORE_ACCEPT_UNICAST, &rx_accept_flags);
        ECORE_SET_BIT(ECORE_ACCEPT_UNMATCHED, &rx_accept_flags);
        ECORE_SET_BIT(ECORE_ACCEPT_ALL_MULTICAST, &rx_accept_flags);
        ECORE_SET_BIT(ECORE_ACCEPT_BROADCAST, &rx_accept_flags);

        ECORE_SET_BIT(ECORE_ACCEPT_UNICAST, &tx_accept_flags);
        ECORE_SET_BIT(ECORE_ACCEPT_ALL_MULTICAST, &tx_accept_flags);
        ECORE_SET_BIT(ECORE_ACCEPT_BROADCAST, &tx_accept_flags);

        /* In NPAR + vm_switch_enable mode, we need to turn on the ACCEPT_ALL_UNICAST for TX to make
         * sure all traffic passes on the loopback channel to enable non-enlighted vms to communicate (vms that we don't
         * have their MAC set) . 
         * We turn it on once we're in promiscuous, which signals that there is probablly vms up that need
         * this feature. */
        if (IS_MF_SI_MODE(pdev) && pdev->params.npar_vm_switching_enable)
        {
            ECORE_SET_BIT(ECORE_ACCEPT_ALL_UNICAST, &tx_accept_flags);    
        }
        
    }

    if GET_FLAGS(rx_mask ,LM_RX_MASK_ACCEPT_UNICAST)
    {
        /* accept matched ucast */
        ECORE_SET_BIT(ECORE_ACCEPT_UNICAST, &rx_accept_flags);
        ECORE_SET_BIT(ECORE_ACCEPT_UNICAST, &tx_accept_flags);
    }

    if GET_FLAGS(rx_mask ,LM_RX_MASK_ACCEPT_MULTICAST)
    {
        /* accept matched mcast */
        ECORE_SET_BIT(ECORE_ACCEPT_MULTICAST, &rx_accept_flags);
        ECORE_SET_BIT(ECORE_ACCEPT_MULTICAST, &tx_accept_flags);
    }

    if GET_FLAGS(rx_mask ,LM_RX_MASK_ACCEPT_ALL_MULTICAST)
    {
        /* accept all mcast */
        ECORE_SET_BIT(ECORE_ACCEPT_ALL_MULTICAST, &rx_accept_flags);
        ECORE_SET_BIT(ECORE_ACCEPT_ALL_MULTICAST, &tx_accept_flags);
    }

    if GET_FLAGS(rx_mask ,LM_RX_MASK_ACCEPT_BROADCAST)
    {
        /* accept matched bcast */
        ECORE_SET_BIT(ECORE_ACCEPT_BROADCAST, &rx_accept_flags);
        ECORE_SET_BIT(ECORE_ACCEPT_BROADCAST, &tx_accept_flags);
    }

    if GET_FLAGS(rx_mask ,LM_RX_MASK_ACCEPT_ERROR_PACKET)
    {
        /* TBD: there is no usage in Miniport for this flag */
    }    

    /* Prepare ramrod parameters */
    ramrod_param.cid         = chain_idx; // echo.. 
    ramrod_param.cl_id       = LM_FW_CLI_ID(pdev, chain_idx);
    ramrod_param.rx_mode_obj = &pdev->slowpath_info.rx_mode_obj;
    ramrod_param.func_id     = FUNC_ID(pdev);

    ramrod_param.pstate      = (unsigned long *)&pdev->client_info[chain_idx].sp_rxmode_state;
    ramrod_param.state       = ECORE_FILTER_RX_MODE_PENDING;

    // We set in lm_cli_idx always 0 (LM_CLI_IDX_NDIS) for E1x and lm_cli_idx for e2.
    // LM_CLI_IDX_NDIS is an occasional choice and could be any of the LM_CLI_IDX
    // 
    // * rx_mode_rdata PER INDEX is problematic because:
    //      the rx filtering is same place in internal ram of e1.5/e1.0 and when we work with an array
    //      each client run over the bits of the previous client
    // 
    // * rx_mode_rdata NOT PER INDEX is problematic because:
    //      in e2.0 when we send a ramrod, the rdata is same memory for all
    //      clients and therefore in case of parallel run of rx_mask of clients 
    //      one of the ramrods actually won't be sent with the correct data
    // 
    // * Conclusion: we have here a problem which make a conflict that both E1.0/E1.5 and E2 work without issues.
    //               This issue should be resolved in a proper way which should be discussed.
    // 
    // This note is related to the following two CQ's:
    // CQ53609 - eVBD:57712: evbda!lm_sq_complete+7ca; Assert is seen while running ACPI S1 S3 sleep stress test
    // CQ53444 - OIS Certs: iSCSI Ping Test Fails

    lm_cli_idx = CHIP_IS_E1x(pdev) ? LM_CLI_IDX_NDIS : LM_CHAIN_IDX_CLI(pdev, chain_idx);

    ramrod_param.rdata = LM_SLOWPATH(pdev, rx_mode_rdata)[lm_cli_idx];
    ramrod_param.rdata_mapping = LM_SLOWPATH_PHYS(pdev, rx_mode_rdata)[lm_cli_idx];

    ECORE_SET_BIT(ECORE_FILTER_RX_MODE_PENDING, &pdev->client_info[chain_idx].sp_rxmode_state);
    ECORE_SET_BIT(RAMROD_RX, &ramrod_param.ramrod_flags);
    ECORE_SET_BIT(RAMROD_TX, &ramrod_param.ramrod_flags);

    ramrod_param.rx_mode_flags = 0; // FIXME ...
    ramrod_param.rx_accept_flags = rx_accept_flags;
    ramrod_param.tx_accept_flags = tx_accept_flags;

    /* Must be set before the ramrod... */
    pdev->client_info[chain_idx].last_set_rx_mask = rx_mask;
    DbgBreakIf(pdev->client_info[chain_idx].set_rx_mode_cookie != NULL);
    pdev->client_info[chain_idx].set_rx_mode_cookie = cookie;

    lm_status = ecore_config_rx_mode(pdev, &ramrod_param);
    DbgMessage1(pdev, INFORMl2sp, "Status returned from ecore_config_rx_mode: %d\n", lm_status);
    if (lm_status == LM_STATUS_SUCCESS)
    {
        pdev->client_info[chain_idx].set_rx_mode_cookie = NULL;
    }
    else if (lm_status == LM_STATUS_REQUEST_NOT_ACCEPTED)
    {
        /* Sq is blocked... meaning we're in error recovery, this is our one outstanding oid. 
         * mark ecore as done, return PENDING to UM, don't clear cookie. This means miniport
         * will eventually get a completion as part of the re-initialization of the chip... */
        ECORE_CLEAR_BIT(ECORE_FILTER_RX_MODE_PENDING, &pdev->client_info[chain_idx].sp_rxmode_state);
    }

    return lm_status;
} /* lm_set_rx_mask */

/* Waits for the set=-rx-mode to complete*/
lm_status_t lm_wait_set_rx_mask_done(struct _lm_device_t *pdev, u8_t chain_idx)
{
    struct ecore_rx_mode_ramrod_params params = {0};
    lm_status_t lm_status;

    params.pstate = (unsigned long *)&pdev->client_info[chain_idx].sp_rxmode_state;
    params.state = ECORE_FILTER_RX_MODE_PENDING;

    lm_status = pdev->slowpath_info.rx_mode_obj.wait_comp(pdev, &params);
    DbgBreakIf(lm_status != LM_STATUS_SUCCESS);

    return lm_status;
}


/*************************  MULTICAST  *****************************************/
static INLINE lm_status_t __init_mcast_macs_list(lm_device_t *pdev,
                                                 u8_t*        mc_addrs, 
                                                 u32_t        buf_len,  
                                                 struct ecore_mcast_ramrod_params *p)
{
    u8 mc_count = buf_len / ETHERNET_ADDRESS_SIZE;
    struct ecore_mcast_list_elem *mc_mac = NULL;


    mc_mac = mm_rt_alloc_mem(pdev, sizeof(*mc_mac) * mc_count, 0);
        
    if (!mc_addrs) {
        return LM_STATUS_INVALID_PARAMETER;
    }

    d_list_clear(&p->mcast_list);

    while(buf_len && mc_addrs)
    {
        mc_mac->mac = mc_addrs;
        DbgMessage6(pdev, INFORMl2sp, "mc_addrs[%d]:mc_addrs[%d]:mc_addrs[%d]:mc_addrs[%d]:mc_addrs[%d]:mc_addrs[%d]\n", 
                   mc_addrs[0],mc_addrs[1],mc_addrs[2],mc_addrs[3],mc_addrs[4],mc_addrs[5]);
        d_list_push_tail(&p->mcast_list, &mc_mac->link);
        /* move on to next mc addr */
        buf_len -= ETHERNET_ADDRESS_SIZE;
        mc_addrs += ETHERNET_ADDRESS_SIZE;
        mc_mac++;
    }

    p->mcast_list_len = mc_count;

    return LM_STATUS_SUCCESS;
}

static INLINE void __free_mcast_macs_list(lm_device_t *pdev,
                                          struct ecore_mcast_ramrod_params *p)
{
    struct ecore_mcast_list_elem *mc_mac = NULL;
    mc_mac = (struct ecore_mcast_list_elem *)d_list_peek_head(&p->mcast_list);

    if (mc_mac)
    {
        /* note that p->mcast_list_len is now set to 0 after processing */
        mm_rt_free_mem(pdev, mc_mac, sizeof(*mc_mac) * d_list_entry_cnt(&p->mcast_list), 0);
    }
}

/** 
 * @Description
 *      Function configures a list of multicast addresses. Or
 *      resets the list previously configured 
 *  
 * @param pdev
 * @param mc_addrs    - array of multicast addresses. NULL if unset is required
 * @param buf_len     - length of the buffer - 0 if unset is required
 * @param cookie      - will be returned on completion 
 * @param lm_cli_idx  - which lm client to send request on
 * 
 * @return lm_status_t - SUCCESS on syncrounous completion 
 *                       PENDING on asyncounous completion
 *                       FAILURE o/w
 */
lm_status_t lm_set_mc(struct _lm_device_t *pdev, 
                      u8_t*  mc_addrs, /* may be NULL (for unset) */
                      u32_t  buf_len,  /* may be 0 (for unset) */
                      void * cookie,  lm_cli_idx_t lm_cli_idx)
{
    struct ecore_mcast_ramrod_params rparam = {0};
    lm_status_t lm_status = LM_STATUS_SUCCESS;
    

#ifdef VF_INVOLVED
    if (IS_CHANNEL_VFDEV(pdev)) {
        return lm_vf_pf_set_q_filters(pdev, lm_cli_idx, cookie, Q_FILTER_MC, mc_addrs, buf_len, LM_SET_CAM_NO_VLAN_FILTER);
    }
#endif

    if(0 == LM_MC_TABLE_SIZE(pdev,lm_cli_idx))
    {
        DbgBreakMsg("size must be greater than zero for a valid client\n");
        return LM_STATUS_FAILURE;
    }

   
    /* Initialize params sent to ecore layer */
    /* Need to split to groups of 16 for E2... due to hsi restraint*/
    if (mc_addrs)
    {
        __init_mcast_macs_list(pdev, mc_addrs, buf_len, &rparam);
    }
    rparam.mcast_obj = &pdev->slowpath_info.mcast_obj[lm_cli_idx];

    /* Cookie must be set before sending the ramord, since completion could arrive before 
     * we return and the cookie must be in place*/
    DbgBreakIf(pdev->slowpath_info.set_mcast_cookie[lm_cli_idx] != NULL);
    pdev->slowpath_info.set_mcast_cookie[lm_cli_idx] = cookie;
    
    lm_status = ecore_config_mcast(pdev, &rparam, (mc_addrs != NULL)? ECORE_MCAST_CMD_ADD : ECORE_MCAST_CMD_DEL);
    if (lm_status == LM_STATUS_SUCCESS)
    {
        pdev->slowpath_info.set_mcast_cookie[lm_cli_idx] = NULL;
    }

    if (mc_addrs)
    {
        __free_mcast_macs_list(pdev, &rparam);
    }
   
    return lm_status;
} /* lm_set_mc */

/** 
 * Description
 *      This routine is called to wait for the multicast set
 *      completion. It must be called in passive level since it
 *      may sleep
 * @param pdev
 * @param lm_cli_idx the cli-idx that the multicast was sent on. 
 * 
 * @return lm_status SUCCESS on done, TIMEOUT o/w
 */
lm_status_t lm_wait_set_mc_done(struct _lm_device_t *pdev, lm_cli_idx_t lm_cli_idx)
{
    struct ecore_mcast_obj * mcast_obj;
    lm_status_t lm_status = LM_STATUS_FAILURE;
    
    mcast_obj = &pdev->slowpath_info.mcast_obj[lm_cli_idx];
    lm_status = mcast_obj->wait_comp(pdev, mcast_obj);

    return lm_status;
   
}

/*************************  RSS ***********************************************/

/**
 * Description: update RSS key in slowpath 
 * Assumptions: 
 *  - given key_size is promised to be either 40 or 16 (promised by NDIS)
 * Return:
 */

/** 
 * @Description: Update RSS key in driver rss_hash_key array and 
 *             check if it has changed from previous key.
 * 
 * @param pdev
 * @param hash_key  - hash_key received from NDIS
 * @param key_size
 * 
 * @return u8_t     TRUE if changed, FALSE o/w
 */
static u8_t lm_update_rss_key(struct _lm_device_t *pdev, u8_t *hash_key,
                                     u32_t key_size)
{
    u32_t val        = 0;
    u32_t i          = 0;
    s32_t rss_reg    = 0;
    u8_t key_changed = FALSE;

    /* check params */
    if ERR_IF(!(pdev && hash_key)) 
    {
        DbgBreak();
        return LM_STATUS_INVALID_PARAMETER;
    }

    /* Note: MSB (that is hash_key[0]) should be placed in MSB of register KEYRSS9, regardless the key size */
    /* GilR 4/4/2007 - assert on key_size==16/40? */
    for (rss_reg = 9, i = 0; rss_reg >= 0; rss_reg--) 
    {
        val = 0;
        if (i < key_size) 
        {
            val = ((hash_key[i] << 24) | (hash_key[i+1] << 16) | (hash_key[i+2] << 8) | hash_key[i+3]);
            DbgMessage4(pdev, INFORMl2sp,
                        "KEYRSS[%d:%d]=0x%x, written to RSS_REG=%d\n",
                        i, i+3, val, rss_reg);
            i += 4;
        } 
        else 
        {
            DbgMessage2(pdev, INFORMl2sp,
                        "OUT OF KEY size, writing 0x%x to RSS_REG=%d\n",
                        val, rss_reg);
        }
        if (pdev->slowpath_info.rss_hash_key[rss_reg] != val) 
        { /* key changed */
            pdev->slowpath_info.rss_hash_key[rss_reg] = val;
            key_changed = TRUE;
        }
    }

    if (key_changed) 
    {
        DbgMessage(pdev, WARNl2, "update rss: KEY CHANGED\n");
    }

    return key_changed;
}

/** 
 * @Description
 *      Enable RSS for Eth with given indirection table also updates the rss key
 *      in searcher (for previous chips...- done by sp-verbs)
 *  
 * @Assumptions 
 *  - given table_size is promised to be power of 2 (promised by NDIS),
 *    or 1 in case of RSS disabling
 *  - the indices in the given chain_indirection_table are chain
 *    indices converted by UM layer... 
 *  - given key_size is promised to be either 40 or 16 (promised by NDIS)
 *  
 * @param pdev
 * @param chain_indirection_table - array of size @table_size containing chain numbers
 * @param table_size - size of @indirection_table
 * @param hash_key - new hash_key to be configured. 0 means no key
 * @param key_size
 * @param hash_type
 * @param sync_with_toe - This field indicates that the completion to the mm layer 
 *                        should take into account the fact that toe rss update will 
 *                        be sent as well. A counter will be increased in lm for this purpose
 * @param cookie        - will be returned on completion 
 * 
 * @return lm_status_t - SUCCESS on syncrounous completion 
 *                       PENDING on asyncounous completion
 *                       FAILURE o/w
 */
lm_status_t lm_enable_rss(struct _lm_device_t *pdev, u8_t *chain_indirection_table,
                          u32_t table_size, u8_t *hash_key, u32_t key_size, lm_rss_hash_t hash_type,
                          u8 sync_with_toe, void * cookie)
{
    struct ecore_config_rss_params params      = {0};
    lm_status_t                    lm_status   = LM_STATUS_SUCCESS;
    u8_t                           value       = 0;
    u8_t                           reconfigure = FALSE;
    u8_t                           key_changed = FALSE;
    u8_t                           i           = 0;


    /* check params */
    if ERR_IF(!(pdev && chain_indirection_table)) 
    {
        DbgBreak();
        return LM_STATUS_INVALID_PARAMETER;
    }

    if (hash_type & 
        ~(LM_RSS_HASH_IPV4 | LM_RSS_HASH_TCP_IPV4 | LM_RSS_HASH_IPV6 | LM_RSS_HASH_TCP_IPV6))
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    params.rss_obj = &pdev->slowpath_info.rss_conf_obj;

    /* RSS mode */
    /* Fixme --> anything else ?*/
    ECORE_SET_BIT(ECORE_RSS_MODE_REGULAR, &params.rss_flags);

    /* Translate the hash type to "ecore" */
    if (GET_FLAGS(hash_type, LM_RSS_HASH_IPV4)) 
    {
        ECORE_SET_BIT(ECORE_RSS_IPV4, &params.rss_flags);
    } 
    if (GET_FLAGS(hash_type, LM_RSS_HASH_TCP_IPV4)) 
    {
        ECORE_SET_BIT(ECORE_RSS_IPV4_TCP, &params.rss_flags);
    }
    if (GET_FLAGS(hash_type, LM_RSS_HASH_IPV6)) 
    {
        ECORE_SET_BIT(ECORE_RSS_IPV6, &params.rss_flags);
    }
    if (GET_FLAGS(hash_type, LM_RSS_HASH_TCP_IPV6)) 
    {
        ECORE_SET_BIT(ECORE_RSS_IPV6_TCP, &params.rss_flags);   
    }

    if (pdev->slowpath_info.last_set_rss_flags != params.rss_flags) 
    {
        pdev->slowpath_info.last_set_rss_flags = params.rss_flags;
        reconfigure = TRUE;
    }

    /* set rss result mask according to table size
       (table_size is promised to be power of 2) */
    params.rss_result_mask = (u8_t)table_size - 1;
    if (pdev->slowpath_info.last_set_rss_result_mask != params.rss_result_mask) 
    {
        /* Hash bits */
        pdev->slowpath_info.last_set_rss_result_mask = params.rss_result_mask;
        reconfigure = TRUE;
    }
        
    for (i = 0; i < table_size; i++) 
    {

        value = LM_CHAIN_TO_FW_CLIENT(pdev,chain_indirection_table[i]);

        if (pdev->slowpath_info.last_set_indirection_table[i] != value) 
        {
            DbgMessage3(pdev, INFORMl2sp, "RssIndTable[%02d]=0x%x (Changed from 0x%x)\n", i, value, pdev->slowpath_info.last_set_indirection_table[i]);
            pdev->slowpath_info.last_set_indirection_table[i] = value;
            reconfigure = TRUE;
        }
    }
    mm_memcpy(params.ind_table, pdev->slowpath_info.last_set_indirection_table, sizeof(params.ind_table));

    if (hash_key) 
    {
        key_changed = lm_update_rss_key(pdev, hash_key, key_size);
        if (key_changed)
        {
            reconfigure = TRUE;
        }
        mm_memcpy(params.rss_key, pdev->slowpath_info.rss_hash_key, sizeof(params.rss_key));
        ECORE_SET_BIT(ECORE_RSS_SET_SRCH, &params.rss_flags);
    }

    DbgBreakIf(!reconfigure && sync_with_toe);
    /* Not expected, that toe will update and ETH not, but just to make sure, if sync_with_toe
     * is true it means toe reconfigured... so eth must to to take care of sync... */
    if (reconfigure || sync_with_toe)
    {
        /* If we're not syncing with toe, it means that these counters have not
         * been increased by toe, and need to be increased here. */
        if (!sync_with_toe) 
        {
            DbgBreakIf(pdev->params.update_comp_cnt);
            mm_atomic_inc(&pdev->params.update_comp_cnt);
            mm_atomic_inc(&pdev->params.update_suspend_cnt);
        }

        DbgBreakIf(pdev->slowpath_info.set_rss_cookie);
        pdev->slowpath_info.set_rss_cookie = cookie;

        lm_status = ecore_config_rss(pdev, &params);
        if (lm_status == LM_STATUS_SUCCESS)
        {
            lm_status = LM_STATUS_PENDING;
        }
    }

    return lm_status;
}


/** 
 * @Description 
 *      This routine disables rss functionality by sending a
 *      ramrod to FW. 
 * 
 * @param pdev
 * @param cookie - will be returned on completion
 * @param sync_with_toe - true means this call is synced with 
 *                      toe, and completion will be called only
 *                      when both toe + eth complete. Eth needs
 *                      to know this (reason in code) *               
 * 
 * @return lm_status_t - SUCCESS on syncrounous completion 
 *                       PENDING on asyncounous completion
 *                       FAILURE o/w
 */
lm_status_t lm_disable_rss(struct _lm_device_t *pdev, u8_t sync_with_toe, void * cookie)
{
    struct ecore_config_rss_params params    = {0};
    lm_status_t                    lm_status = LM_STATUS_FAILURE;
    u8_t                           value     = 0;
	u8_t                           i         = 0;

    DbgMessage1(pdev, INFORMl2sp, "lm_disable_rss sync_with_toe = %d\n", sync_with_toe);

    DbgBreakIf(pdev->slowpath_info.set_rss_cookie);
    pdev->slowpath_info.set_rss_cookie = cookie;

    params.rss_obj = &pdev->slowpath_info.rss_conf_obj;

    /* RSS mode */
    ECORE_SET_BIT(ECORE_RSS_MODE_DISABLED, &params.rss_flags);
    pdev->slowpath_info.last_set_rss_flags = params.rss_flags;

    /* If we're not syncing with toe, it means that these counters have not
     * been increased by toe, and need to be increased here. */
    if (!sync_with_toe)
    {
        mm_atomic_inc(&pdev->params.update_comp_cnt);
        mm_atomic_inc(&pdev->params.update_suspend_cnt);
    }

    value = LM_CHAIN_TO_FW_CLIENT(pdev,LM_SW_LEADING_RSS_CID(pdev));
    for (i = 0; i < ARRSIZE(params.ind_table); i++) 
    {
        pdev->slowpath_info.last_set_indirection_table[i] = value;
        params.ind_table[i] = value;
    }

    lm_status = ecore_config_rss(pdev, &params);
    if (lm_status == LM_STATUS_SUCCESS)
    {
        lm_status = LM_STATUS_PENDING;
    }
    return lm_status;

} /* lm_disable_rss */

/** 
 * @Description 
 *      Wait for the rss disable/enable configuration to
 *      complete
 * 
 * @param pdev
 * 
 * @return lm_status_t lm_status_t SUCCESS or TIMEOUT
 */
lm_status_t lm_wait_config_rss_done(struct _lm_device_t *pdev)
{
    struct ecore_raw_obj *r = &pdev->slowpath_info.rss_conf_obj.raw;
    lm_status_t lm_status;

    lm_status = r->wait_comp(pdev, r);

    return lm_status;
}


/************************** EQ HANDLING *******************************************/

static INLINE void lm_eq_handle_function_start_eqe(struct _lm_device_t * pdev, union event_ring_elem * elem)
{
    pdev->eq_info.function_state = FUNCTION_START_COMPLETED;
    lm_sq_complete(pdev, CMD_PRIORITY_NORMAL, RAMROD_CMD_ID_COMMON_FUNCTION_START, 
                   NONE_CONNECTION_TYPE, 0);
}
    
static INLINE void lm_eq_handle_function_stop_eqe(struct _lm_device_t * pdev, union event_ring_elem * elem)
{
    pdev->eq_info.function_state = FUNCTION_STOP_COMPLETED;
    lm_sq_complete(pdev, CMD_PRIORITY_NORMAL, RAMROD_CMD_ID_COMMON_FUNCTION_STOP, 
                   NONE_CONNECTION_TYPE, 0);

}

static INLINE void lm_eq_handle_cfc_del_eqe(struct _lm_device_t * pdev, union event_ring_elem * elem)
{
    u32_t cid   = 0;
    u8_t  error = 0;

    cid = mm_le32_to_cpu(elem->message.data.cfc_del_event.cid);
    cid = SW_CID(cid);

    error = elem->message.error;
    
    if (cid < (MAX_ETH_CONS + MAX_VF_ETH_CONS))
    {   /* cfc del completion for eth cid */
        DbgBreakIf(PFDEV(pdev)->vars.connections[cid].con_state != LM_CON_STATE_TERMINATE);
        PFDEV(pdev)->vars.connections[cid].con_state = LM_CON_STATE_CLOSE;
        DbgMessage(pdev, WARNeq, "lm_service_eq_intr: EVENT_RING_OPCODE_CFC_DEL_WB - calling lm_extract_ramrod_req!\n");
        lm_extract_ramrod_req(pdev, cid);
    }
    else
    {   /* cfc del completion for toe cid */
        if (error) {
            
            if (lm_map_cid_to_proto(pdev, cid) != TOE_CONNECTION_TYPE) 
            {
                DbgMessage1(pdev,FATAL,"ERROR completion is not valid for cid=0x%x\n",cid);
                DbgBreakIfAll(1);
            }
            pdev->toe_info.stats.total_cfc_delete_error++;
            if (pdev->context_info->array[cid].cfc_delete_cnt++ < LM_MAX_VALID_CFC_DELETIONS) 
            {
                DbgMessage2(pdev, WARNl4sp, "lm_eth_comp_cb: RAMROD_CMD_ID_ETH_CFC_DEL(0x%x) - %d resending!\n", cid,
                            pdev->context_info->array[cid].cfc_delete_cnt);
                lm_command_post(pdev,
                                cid,
                                RAMROD_CMD_ID_COMMON_CFC_DEL,
                                CMD_PRIORITY_NORMAL,
                                NONE_CONNECTION_TYPE,
                                0 );
            } 
            else 
            {
                DbgMessage(pdev,FATAL,"A number of CFC deletions exceeded valid number of attempts\n");
                DbgBreakIfAll(1);
            }
        } 
        else 
        {
            lm_recycle_cid(pdev, cid);
        }
    }

    lm_sq_complete(pdev, CMD_PRIORITY_NORMAL, 
                   (elem->message.opcode == EVENT_RING_OPCODE_CFC_DEL)? RAMROD_CMD_ID_COMMON_CFC_DEL : RAMROD_CMD_ID_COMMON_CFC_DEL_WB, 
                   NONE_CONNECTION_TYPE, cid);
}

static INLINE void lm_eq_handle_fwd_setup_eqe(struct _lm_device_t * pdev, union event_ring_elem * elem)
{
    DbgBreakIf(pdev->vars.connections[FWD_CID(pdev)].con_state != LM_CON_STATE_CLOSE);
    pdev->vars.connections[FWD_CID(pdev)].con_state = LM_CON_STATE_OPEN;

    DbgMessage(pdev, WARNl2sp, "comp of FWD SETUP -calling lm_extract_ramrod_req!\n");
    lm_extract_ramrod_req(pdev, FWD_CID(pdev));
    lm_sq_complete(pdev, CMD_PRIORITY_NORMAL, RAMROD_CMD_ID_ETH_FORWARD_SETUP,
                   ETH_CONNECTION_TYPE, FWD_CID(pdev));
            
}

static INLINE void lm_eq_handle_mcast_eqe(struct _lm_device_t * pdev, union event_ring_elem * elem)
{
    struct ecore_mcast_ramrod_params  rparam         = {0};
    struct ecore_mcast_obj          * obj            = NULL;
    void                            * cookie         = NULL;
    lm_status_t                       lm_status      = LM_STATUS_FAILURE;
    u32_t                             cid            = 0;
    u8_t                              lm_cli_idx     = 0;
    u8_t                              indicate_done  = TRUE;

    cid = elem->message.data.eth_event.echo & ECORE_SWCID_MASK;
    lm_cli_idx = LM_CHAIN_IDX_CLI(pdev, cid);

    obj = &pdev->slowpath_info.mcast_obj[lm_cli_idx];

    /* Clear pending state for the last command */
    obj->raw.clear_pending(&obj->raw);

    rparam.mcast_obj = obj;

    /* If there are pending mcast commands - send them */
    if (obj->check_pending(obj)) 
    {
        lm_status = ecore_config_mcast(pdev, &rparam, ECORE_MCAST_CMD_CONT);
        if (lm_status == LM_STATUS_PENDING)
        {
            indicate_done = FALSE;
        }
        else if (lm_status != LM_STATUS_SUCCESS)
        {
            DbgMessage1(pdev, FATAL, "Failed to send pending mcast commands: %d\n", lm_status);
            DbgBreakMsg("Unexpected pending mcast command failed\n");
        }
    }

    if (indicate_done)
    {
        if (pdev->slowpath_info.set_mcast_cookie[lm_cli_idx])
        {
            cookie = (void *)pdev->slowpath_info.set_mcast_cookie[lm_cli_idx];
            pdev->slowpath_info.set_mcast_cookie[lm_cli_idx] = NULL;
            mm_set_done(pdev, cid, cookie);
        }
    }

    if (CHIP_IS_E1(pdev))
    {
        lm_sq_complete(pdev, CMD_PRIORITY_NORMAL, RAMROD_CMD_ID_ETH_SET_MAC, 
                       ETH_CONNECTION_TYPE, cid);
    }
    else
    {
        lm_sq_complete(pdev, CMD_PRIORITY_NORMAL, RAMROD_CMD_ID_ETH_MULTICAST_RULES, 
                       ETH_CONNECTION_TYPE, cid);
    }
}

static INLINE void lm_eq_handle_classification_eqe(struct _lm_device_t * pdev, union event_ring_elem * elem)
{
    struct ecore_raw_obj  * raw    = NULL;
    void                  * cookie = NULL;
    u32_t                   cid    = 0;
    u8_t                    type   = 0;

    cid  = elem->message.data.eth_event.echo & ECORE_SWCID_MASK;
    type = elem->message.data.eth_event.echo >> ECORE_SWCID_SHIFT;

    /* Relevant to 57710, mcast is implemented as "set-macs"*/
    if (type == ECORE_FILTER_MCAST_PENDING) 
    {
        DbgBreakIf(!CHIP_IS_E1(pdev));
        lm_eq_handle_mcast_eqe(pdev, elem);
		return;
    }

    switch (type)
    {
    case ECORE_FILTER_MAC_PENDING:
        raw = &pdev->client_info[cid].mac_obj.raw;
        break;
    case ECORE_FILTER_VLAN_MAC_PENDING:
        raw = &pdev->client_info[cid].mac_vlan_obj.raw;
        break;
    default:
        /* unknown ER handling*/
        /* Special handling for case that type is unknown (error recovery flow)
         * check which object is pending, and clear the relevant one. */
        raw = &pdev->client_info[cid].mac_obj.raw;
        if (!raw->check_pending(raw))
        {
            raw = &pdev->client_info[cid].mac_vlan_obj.raw;
        }
    }
    
    raw->clear_pending(raw);

    if (pdev->client_info[cid].set_mac_cookie)
    {
        cookie = (void *)pdev->client_info[cid].set_mac_cookie;
        pdev->client_info[cid].set_mac_cookie = NULL;
        mm_set_done(pdev, cid, cookie);
    }

    if (CHIP_IS_E1x(pdev))
    {
        lm_sq_complete(pdev, CMD_PRIORITY_NORMAL, 
                       RAMROD_CMD_ID_ETH_SET_MAC, ETH_CONNECTION_TYPE, cid);
    }
    else
    {
        lm_sq_complete(pdev, CMD_PRIORITY_NORMAL, 
                       RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES, ETH_CONNECTION_TYPE, cid);
    }
}

static INLINE void lm_eq_handle_stats_eqe(struct _lm_device_t * pdev, union event_ring_elem * elem)
{
    /* Order is important!!!
     * stats use a predefined ramrod. We need to make sure that we first complete the ramrod, which will
     * take it out of sq-completed list, and only after that mark the ramrod as completed, so that a new
     * ramrod can be sent!.
     */
    lm_sq_complete(pdev, CMD_PRIORITY_HIGH, 
                   RAMROD_CMD_ID_COMMON_STAT_QUERY, NONE_CONNECTION_TYPE, 0);

    mm_write_barrier(); /* barrier to make sure command before this line completes before executing the next line! */
    pdev->vars.stats.stats_collect.stats_fw.b_ramrod_completed = TRUE;
    
}

static INLINE void lm_eq_handle_filter_rules_eqe(struct _lm_device_t * pdev, union event_ring_elem * elem)
{
    void  * cookie = NULL;
    u32_t   cid    = 0;

    cid = elem->message.data.eth_event.echo & ECORE_SWCID_MASK;

    DbgMessage(pdev, INFORMeq | INFORMl2sp, "Filter rule completion\n");

    // FIXME: pdev->client_info[cid].mac_obj.raw.clear_pending(&pdev->client_info[cid].mac_obj.raw);
    ECORE_CLEAR_BIT(ECORE_FILTER_RX_MODE_PENDING, &pdev->client_info[cid].sp_rxmode_state);

    if (pdev->client_info[cid].set_rx_mode_cookie)
    {
        cookie = (void *)pdev->client_info[cid].set_rx_mode_cookie;
        pdev->client_info[cid].set_rx_mode_cookie = NULL;
        DbgMessage(pdev, INFORMl2sp, "Filter rule calling mm_set_done... \n");
        mm_set_done(pdev, cid, cookie);
    }

    lm_sq_complete(pdev, CMD_PRIORITY_NORMAL, RAMROD_CMD_ID_ETH_FILTER_RULES, ETH_CONNECTION_TYPE, cid);
}

static INLINE void lm_eq_handle_rss_update_eqe(struct _lm_device_t * pdev, union event_ring_elem * elem)
{
    struct ecore_raw_obj  * raw    = NULL;
    void                  * cookie = NULL;
    u32_t                   cid    = LM_SW_LEADING_RSS_CID(pdev);

    DbgMessage(pdev, INFORMeq | INFORMl2sp, "lm_eth_comp_cb: EVENT_RING_OPCODE_RSS_UPDATE_RULES\n");
    raw = &pdev->slowpath_info.rss_conf_obj.raw;
    raw->clear_pending(raw);
    mm_atomic_dec(&pdev->params.update_comp_cnt);
    if (mm_atomic_dec(&pdev->params.update_suspend_cnt) == 0) 
    {
        if (pdev->slowpath_info.set_rss_cookie != NULL)
        {
            cookie = (void *)pdev->slowpath_info.set_rss_cookie;
            pdev->slowpath_info.set_rss_cookie = NULL;
            mm_set_done(pdev, cid, cookie);
        }
    }

    lm_sq_complete(pdev, CMD_PRIORITY_NORMAL, RAMROD_CMD_ID_ETH_RSS_UPDATE, ETH_CONNECTION_TYPE, cid);
}

/**lm_eq_handle_niv_function_update_eqe
 * handle a NIV function update completion. 
 * 
 * @param pdev the device
 * @param elem the CQE
 */
static INLINE void lm_eq_handle_niv_function_update_eqe(struct _lm_device_t * pdev, union event_ring_elem * elem)
{
    DbgBreakIf(pdev->slowpath_info.niv_ramrod_state != NIV_RAMROD_POSTED);
    pdev->slowpath_info.niv_ramrod_state = NIV_RAMROD_COMPLETED;

    lm_sq_complete(pdev, CMD_PRIORITY_NORMAL, RAMROD_CMD_ID_COMMON_NIV_FUNCTION_UPDATE,
                   NONE_CONNECTION_TYPE, 0);
}

/**lm_eq_handle_niv_function_update_eqe
 * handle a NIV lists update completion. 
 * 
 * @param pdev the device
 * @param elem the CQE
 */
static INLINE void lm_eq_handle_niv_vif_lists_eqe(struct _lm_device_t * pdev, union event_ring_elem * elem)
{
    DbgBreakIf(pdev->slowpath_info.niv_ramrod_state != NIV_RAMROD_POSTED);
    DbgBreakIf((elem->message.data.vif_list_event.echo != VIF_LIST_RULE_CLEAR_ALL) &&
               (elem->message.data.vif_list_event.echo != VIF_LIST_RULE_CLEAR_FUNC) &&
               (elem->message.data.vif_list_event.echo != VIF_LIST_RULE_GET) &&
               (elem->message.data.vif_list_event.echo != VIF_LIST_RULE_SET));

    if (elem->message.data.vif_list_event.echo == VIF_LIST_RULE_GET)
    {
        pdev->slowpath_info.last_vif_list_bitmap = (u8_t)elem->message.data.vif_list_event.func_bit_map;
    }

    pdev->slowpath_info.niv_ramrod_state = NIV_RAMROD_COMPLETED;

    lm_sq_complete(pdev, CMD_PRIORITY_NORMAL, RAMROD_CMD_ID_COMMON_NIV_VIF_LISTS,
                   NONE_CONNECTION_TYPE, 0);
}

static INLINE lm_status_t lm_service_eq_elem(struct _lm_device_t * pdev, union event_ring_elem * elem)
{
    /* handle eq element */
    switch(elem->message.opcode) 
    {
        case EVENT_RING_OPCODE_FUNCTION_START:
            lm_eq_handle_function_start_eqe(pdev, elem);
            break;

        case EVENT_RING_OPCODE_FUNCTION_STOP:
            lm_eq_handle_function_stop_eqe(pdev, elem);
            break;

        case EVENT_RING_OPCODE_CFC_DEL:
        case EVENT_RING_OPCODE_CFC_DEL_WB:
            lm_eq_handle_cfc_del_eqe(pdev, elem);
            break;

        case EVENT_RING_OPCODE_SET_MAC:
        case EVENT_RING_OPCODE_CLASSIFICATION_RULES:
            lm_eq_handle_classification_eqe(pdev, elem);
            break;

        case EVENT_RING_OPCODE_STAT_QUERY:
            lm_eq_handle_stats_eqe(pdev, elem);
            break;

        case EVENT_RING_OPCODE_STOP_TRAFFIC:
            pdev->dcbx_info.dcbx_ramrod_state = FUNCTION_DCBX_STOP_COMPLETED;
            lm_sq_complete(pdev, CMD_PRIORITY_MEDIUM, 
                       RAMROD_CMD_ID_COMMON_STOP_TRAFFIC, NONE_CONNECTION_TYPE, 0);
            break;

        case EVENT_RING_OPCODE_START_TRAFFIC:
            pdev->dcbx_info.dcbx_ramrod_state = FUNCTION_DCBX_START_COMPLETED;
            lm_sq_complete(pdev, CMD_PRIORITY_HIGH, 
                       RAMROD_CMD_ID_COMMON_START_TRAFFIC, NONE_CONNECTION_TYPE, 0);
            break;

            
        case EVENT_RING_OPCODE_FORWARD_SETUP:
            lm_eq_handle_fwd_setup_eqe(pdev, elem);
            break;


        case EVENT_RING_OPCODE_MULTICAST_RULES:
            lm_eq_handle_mcast_eqe(pdev, elem);
            break;

        case EVENT_RING_OPCODE_FILTERS_RULES:
            lm_eq_handle_filter_rules_eqe(pdev, elem);
			break;

        case EVENT_RING_OPCODE_RSS_UPDATE_RULES:
            lm_eq_handle_rss_update_eqe(pdev, elem);
            break;

        case EVENT_RING_OPCODE_NIV_FUNCTION_UPDATE:
            lm_eq_handle_niv_function_update_eqe(pdev, elem);
            break;

        case EVENT_RING_OPCODE_NIV_VIF_LISTS:
            lm_eq_handle_niv_vif_lists_eqe(pdev, elem);
            break;

        default:
            DbgBreakMsg("Unknown elem type received on eq\n");
            return LM_STATUS_FAILURE;
        }

    return LM_STATUS_SUCCESS;
}

/** 
 * @Description 
 *      handle cqes of the event-ring, should be called from dpc if index in status block was changed
 * @param pdev
 * 
 * @return lm_status_t SUCCESS or FAILURE (if unknown completion) 
 */
lm_status_t lm_service_eq_intr(struct _lm_device_t * pdev)
{
    union event_ring_elem * elem             = NULL;
    lm_eq_chain_t         * eq_chain         = &pdev->eq_info.eq_chain;
    lm_status_t             lm_status        = LM_STATUS_SUCCESS;
    u16_t                   cq_new_idx       = 0;
    u16_t                   cq_old_idx       = 0;

    cq_new_idx = mm_le16_to_cpu(*(eq_chain->hw_con_idx_ptr));
    if((cq_new_idx & lm_bd_chain_usable_bds_per_page(&eq_chain->bd_chain)) 
       == lm_bd_chain_usable_bds_per_page(&eq_chain->bd_chain))
    {
        cq_new_idx+=lm_bd_chain_bds_skip_eop(&eq_chain->bd_chain);
    }
    cq_old_idx = lm_bd_chain_cons_idx(&eq_chain->bd_chain);

    /* there is no change in the EQ consumer index so exit! */
    if (cq_old_idx == cq_new_idx)
    {
        DbgMessage(pdev, INFORMeq , "there is no change in the EQ consumer index so exit!\n");
        return LM_STATUS_SUCCESS;
    }

    while(cq_old_idx != cq_new_idx)
    {
        DbgBreakIfFastPath(S16_SUB(cq_new_idx, cq_old_idx) <= 0);
        /* get hold of the cqe, and find out what it's type corresponds to */
        elem = (union event_ring_elem *)lm_bd_chain_consume_bd(&eq_chain->bd_chain);

        if (elem == NULL)
        {
            DbgBreakIfFastPath(elem == NULL);
            return LM_STATUS_FAILURE;
        }

        cq_old_idx = lm_bd_chain_cons_idx(&eq_chain->bd_chain);

        lm_status = lm_service_eq_elem(pdev, elem);
        if (lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }
        
#ifdef __LINUX
        mm_common_ramrod_comp_cb(pdev, &elem->message);
#endif //__LINUX
        /* Recycle the cqe */
        lm_bd_chain_bd_produced(&eq_chain->bd_chain);
    } /* while */

    /* update producer */
    LM_INTMEM_WRITE16(pdev,
                      eq_chain->iro_prod_offset,
                      lm_bd_chain_prod_idx(&eq_chain->bd_chain),
                      BAR_CSTRORM_INTMEM);

    return LM_STATUS_SUCCESS;
} /* lm_service_eq_intr */

/** 
 * @Description 
 *     This function completes eq completions immediately
 *     (without fw completion).
 *  
 * @param pdev
 * @param spe
 */
void lm_eq_comp_cb(struct _lm_device_t *pdev, struct sq_pending_command * pending)
{
    union event_ring_elem elem = {{0}};
    u32_t                 cid  = pending->cid;
    u8_t                  cmd  = pending->cmd;
    
    
    /* We need to build the "elem" based on the spe */
    if (pending->type == ETH_CONNECTION_TYPE) /* Some Ethernets complete on Eq. */
    {
        switch (cmd)
        {
        case RAMROD_CMD_ID_ETH_SET_MAC:
            elem.message.opcode = EVENT_RING_OPCODE_SET_MAC;
            elem.message.data.eth_event.echo = (0xff << ECORE_SWCID_SHIFT | cid); /*unknown type*/
            
            break;
            
        case RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES:
            elem.message.opcode = EVENT_RING_OPCODE_CLASSIFICATION_RULES;
            elem.message.data.eth_event.echo = (0xff << ECORE_SWCID_SHIFT | cid); /*unknown type*/
            break;
    
        case RAMROD_CMD_ID_ETH_FORWARD_SETUP:
            elem.message.opcode = EVENT_RING_OPCODE_FORWARD_SETUP;
            break;
    
        case RAMROD_CMD_ID_ETH_MULTICAST_RULES:
            elem.message.opcode = EVENT_RING_OPCODE_MULTICAST_RULES;
            elem.message.data.eth_event.echo = cid; 
            break;
    
        case RAMROD_CMD_ID_ETH_FILTER_RULES:
            elem.message.opcode = EVENT_RING_OPCODE_FILTERS_RULES;
            elem.message.data.eth_event.echo = cid; 
            break;
    
        case RAMROD_CMD_ID_ETH_RSS_UPDATE:
            elem.message.opcode = EVENT_RING_OPCODE_RSS_UPDATE_RULES;
            break;
    
        default:
            DbgBreakMsg("Unknown elem type received on eq\n");
        }
    }
    else if (pending->type == NONE_CONNECTION_TYPE)
    {
        switch (cmd)
        {
        case RAMROD_CMD_ID_COMMON_FUNCTION_START:
            elem.message.opcode = EVENT_RING_OPCODE_FUNCTION_START;
            break;
    
        case RAMROD_CMD_ID_COMMON_FUNCTION_STOP:
            elem.message.opcode = EVENT_RING_OPCODE_FUNCTION_STOP;
            break;
    
        case RAMROD_CMD_ID_COMMON_CFC_DEL:
            elem.message.opcode = EVENT_RING_OPCODE_CFC_DEL;
            elem.message.data.cfc_del_event.cid = cid;
            break;
            
        case RAMROD_CMD_ID_COMMON_CFC_DEL_WB:
            elem.message.opcode = EVENT_RING_OPCODE_CFC_DEL_WB;
            elem.message.data.cfc_del_event.cid = cid;
            break;
    
        case RAMROD_CMD_ID_COMMON_STAT_QUERY:
            elem.message.opcode = EVENT_RING_OPCODE_STAT_QUERY;
            break;
    
        case RAMROD_CMD_ID_COMMON_STOP_TRAFFIC:
            elem.message.opcode = EVENT_RING_OPCODE_STOP_TRAFFIC;
            break;
    
        case RAMROD_CMD_ID_COMMON_START_TRAFFIC:
            elem.message.opcode = EVENT_RING_OPCODE_START_TRAFFIC;
            break;
    
        case RAMROD_CMD_ID_COMMON_NIV_FUNCTION_UPDATE:
            elem.message.opcode = EVENT_RING_OPCODE_NIV_FUNCTION_UPDATE;
            break;
    
        case RAMROD_CMD_ID_COMMON_NIV_VIF_LISTS:
            elem.message.opcode = EVENT_RING_OPCODE_NIV_VIF_LISTS;
            break;
    
        default:
            DbgBreakMsg("Unknown elem type received on eq\n");
        }
    }
    
    lm_service_eq_elem(pdev, &elem);
}

/*********************** SQ RELATED FUNCTIONS ***************************/
/* TODO: move more functions from command.h to here.                    */
void lm_cid_recycled_cb_register(struct _lm_device_t *pdev, u8_t type, lm_cid_recycled_cb_t cb)
{

    if ( CHK_NULL(pdev) ||
         CHK_NULL(cb) ||
         ERR_IF( type >= ARRSIZE( pdev->cid_recycled_callbacks ) ) ||
         ERR_IF( NULL != pdev->cid_recycled_callbacks[type] ) )
    {
        DbgBreakIfAll(!pdev);
        DbgBreakIfAll(!cb) ;
        DbgBreakIfAll( type >= ARRSIZE( pdev->cid_recycled_callbacks ) );
        DbgBreakIfAll( NULL != pdev->cid_recycled_callbacks[type] ) ;
        return;
    }
    pdev->cid_recycled_callbacks[type]= cb;
}

void lm_cid_recycled_cb_deregister(struct _lm_device_t *pdev, u8_t type)
{

    if ( CHK_NULL(pdev) ||
         ERR_IF( type >= ARRSIZE( pdev->cid_recycled_callbacks ) ) ||
         CHK_NULL(pdev->cid_recycled_callbacks[type]) )

    {
        DbgBreakIfAll(!pdev);
        DbgBreakIfAll( type >= ARRSIZE( pdev->cid_recycled_callbacks ) );
        return;
    }
    pdev->cid_recycled_callbacks[type] = (lm_cid_recycled_cb_t)NULL;
}

void lm_sq_change_state(struct _lm_device_t *pdev, lm_sq_state_t state)
{
    DbgMessage2(pdev, INFORM, "Changing sq state from %d to %d\n", pdev->sq_info.sq_state, state);
    
    MM_ACQUIRE_SPQ_LOCK(pdev);
    
    pdev->sq_info.sq_state = state;
    
    MM_RELEASE_SPQ_LOCK(pdev);
}

/** 
 * @Description 
 *     function completes pending slow path requests instead of
 *     FW. Used in error recovery flow.
 *  
 * @Assumptions: 
 *      interrupts at this point are disabled and dpcs are
 *      flushed, thus no one else can complete these... 
 * 
 * @param pdev
 */
void lm_sq_complete_pending_requests(struct _lm_device_t *pdev)
{
    u16_t                       type      = 0;
    struct sq_pending_command * pending   = NULL;

    DbgMessage(pdev, WARN, "lm_sq_complete_pending_requests\n");

    /* unexpected if not under error recovery */
    DbgBreakIf(!pdev->params.enable_error_recovery);
    
    MM_ACQUIRE_SPQ_LOCK(pdev);

    if (pdev->sq_info.sq_comp_scheduled)
    {
        pdev->sq_info.sq_comp_scheduled = FALSE;
    }
    
    MM_RELEASE_SPQ_LOCK(pdev);
    
    do
    {
        MM_ACQUIRE_SPQ_LOCK(pdev);

        /* Find the first entry that hasn't been handled yet. */
        /* We just peek and don't pop since completion of this pending request should contain removing
         * it from the completion list. However, it may not happen immediately */
        pending = (struct sq_pending_command *)d_list_peek_head(&pdev->sq_info.pending_complete);

        /* Look for the first entry that is "pending" but not completion_called yet. */
        while (pending && GET_FLAGS(pending->flags, SQ_PEND_COMP_CALLED))
        {
            pending = (struct sq_pending_command *)d_list_next_entry(&pending->list);
        }

        /* Mark pending completion as "handled" so that we don't handle it again...  */
        if (pending)
        {
            SET_FLAGS(pending->flags, SQ_PEND_COMP_CALLED);
        }
        
        MM_RELEASE_SPQ_LOCK(pdev);

        if (pending)
        {
            type = pending->type;
        
            if (pdev->sq_info.sq_comp_cb[type])
            {
                pdev->sq_info.sq_comp_cb[type](pdev, pending);
            }
            else
            {
                DbgBreakMsg("unsupported pending sq: Not implemented yet\n");
            }
        }
        
        lm_sq_post_pending(pdev);
    } while (!d_list_is_empty(&pdev->sq_info.pending_complete));
}


lm_status_t lm_sq_flush(struct _lm_device_t *pdev)
{
    lm_status_t lm_status   = LM_STATUS_SUCCESS;
    u8_t        schedule_wi = FALSE;

    MM_ACQUIRE_SPQ_LOCK(pdev);

    if ((pdev->sq_info.sq_comp_scheduled == FALSE) &&
        ((pdev->sq_info.num_pending_high != MAX_HIGH_PRIORITY_SPE) ||
        (pdev->sq_info.num_pending_normal != MAX_NORMAL_PRIORITY_SPE)))
    {
        schedule_wi = TRUE;
        pdev->sq_info.sq_comp_scheduled = TRUE;
    }

    MM_RELEASE_SPQ_LOCK(pdev);

    if (schedule_wi)
    {
        lm_status = MM_REGISTER_DPC(pdev, lm_sq_complete_pending_requests);
        /* Alternative: WorkItem... 
        lm_status = MM_REGISTER_LPME(pdev, lm_sq_complete_pending_requests, FALSE, FALSE);
        if (lm_status == LM_STATUS_SUCCESS)
        {
            return LM_STATUS_PENDING;
        }
        */
        if (lm_status == LM_STATUS_SUCCESS)
        {
            lm_status = LM_STATUS_PENDING;
        }
    }

	return lm_status;
}

lm_status_t lm_sq_comp_cb_register(struct _lm_device_t *pdev, u8_t type, lm_sq_comp_cb_t cb)
{
    if ( CHK_NULL(pdev) ||
         CHK_NULL(cb) ||
         ERR_IF( type >= ARRSIZE( pdev->sq_info.sq_comp_cb ) ) ||
         ERR_IF( NULL != pdev->sq_info.sq_comp_cb[type] ) )
    {
        return LM_STATUS_INVALID_PARAMETER;
    }
    pdev->sq_info.sq_comp_cb[type]= cb;
    return LM_STATUS_SUCCESS;
}

lm_status_t lm_sq_comp_cb_deregister(struct _lm_device_t *pdev, u8_t type)
{

    if ( CHK_NULL(pdev) ||
         ERR_IF( type >= ARRSIZE( pdev->sq_info.sq_comp_cb ) ) ||
         CHK_NULL(pdev->sq_info.sq_comp_cb[type]) )

    {
        return LM_STATUS_INVALID_PARAMETER;
    }
    pdev->sq_info.sq_comp_cb[type] = (lm_sq_comp_cb_t)NULL;

    return LM_STATUS_SUCCESS;
}

u8_t lm_sq_is_empty(struct _lm_device_t *pdev)
{
    u8_t empty = TRUE;
    
    MM_ACQUIRE_SPQ_LOCK(pdev);
    
    if ((pdev->sq_info.num_pending_high != MAX_HIGH_PRIORITY_SPE) ||
        (pdev->sq_info.num_pending_normal != MAX_NORMAL_PRIORITY_SPE))
    {
        empty = FALSE;
    }
    
    MM_RELEASE_SPQ_LOCK(pdev);    

    return empty;
}


/** 
 * @Description 
 *     Posts from the normal + high priority lists as much as it
 *     can towards the FW. 
 *  
 * @Assumptions 
 *     called under SQ_LOCK!!!
 *  
 * @param pdev
 * 
 * @return lm_status_t PENDING: if indeed requests were posted, 
 *         SUCCESS o/w
 */
static lm_status_t lm_sq_post_from_list(struct _lm_device_t *pdev)
{
    lm_status_t                 lm_status = LM_STATUS_SUCCESS;
    struct sq_pending_command * pending = NULL;
        
    while (pdev->sq_info.num_pending_normal)
    {
        pending = (void*)d_list_pop_head(&pdev->sq_info.pending_normal);

        if(!pending)
            break;

        pdev->sq_info.num_pending_normal --;

        DbgMessage5(pdev, WARN, "lm_sq_post: priority=%d, command=%d, type=%d, cid=%d num_pending_normal=%d\n",
               CMD_PRIORITY_NORMAL, pending->cmd, pending->type, pending->cid, pdev->sq_info.num_pending_normal);

        d_list_push_tail(&pdev->sq_info.pending_complete, &pending->list);
        
        _lm_sq_post(pdev,pending);

        lm_status = LM_STATUS_PENDING;

    }

    /* post high priority sp */
    while (pdev->sq_info.num_pending_high)
    {
        pending = (void*)d_list_pop_head(&pdev->sq_info.pending_high);

        if(!pending)
            break;

        pdev->sq_info.num_pending_high --;
        DbgMessage5(pdev, WARN, "lm_sq_post: priority=%d, command=%d, type=%d, cid=%d num_pending_normal=%d\n",
               CMD_PRIORITY_HIGH, pending->cmd, pending->type, pending->cid, pdev->sq_info.num_pending_normal);

        d_list_push_tail(&pdev->sq_info.pending_complete, &pending->list);
        
        _lm_sq_post(pdev, pending);

        lm_status = LM_STATUS_PENDING;
    }

    return lm_status;
}

/** 
 * Description
 *	Add the entry to the pending SP list.   
 *	Try to add entry's from the list to the sq_chain if possible.(there is are less then 8 ramrod commands pending) 
 * 
 * @param pdev
 * @param pending  - The pending list entry.
 * @param priority - (high or low) to witch list to insert the pending list entry.
 * 
 * @return lm_status_t: LM_STATUS_SUCCESS on success or 
 *         LM_STATUS_REQUEST_NOT_ACCEPTED if slowpath queue is
 *         in blocked state.
 */
lm_status_t lm_sq_post_entry(struct _lm_device_t       * pdev,
                             struct sq_pending_command * pending,
                             u8_t                        priority)
{
    lm_status_t lm_status = LM_STATUS_FAILURE;
    u8_t        sq_flush  = FALSE;
    
    DbgBreakIf(! pdev);

    MM_ACQUIRE_SPQ_LOCK(pdev);

    if (pdev->sq_info.sq_state == SQ_STATE_BLOCKED)
    {
        DbgBreakMsg("Unexpected slowpath command\n");
        MM_RELEASE_SPQ_LOCK(pdev);
        return LM_STATUS_REQUEST_NOT_ACCEPTED;
    }
    
    /* We shouldn't be posting any entries if the function-stop has already been posted... */
    if (((mm_le32_to_cpu(pending->command.hdr.conn_and_cmd_data) & SPE_HDR_T_CMD_ID)>>SPE_HDR_T_CMD_ID_SHIFT) != RAMROD_CMD_ID_COMMON_FUNCTION_STOP)
    {
        DbgBreakIf((pdev->eq_info.function_state == FUNCTION_STOP_POSTED) || (pdev->eq_info.function_state == FUNCTION_STOP_COMPLETED));
    }

    switch( priority )
    {
    case CMD_PRIORITY_NORMAL:
        /* add the request to the list tail*/
        d_list_push_tail(&pdev->sq_info.pending_normal, &pending->list);
        break;
    case CMD_PRIORITY_MEDIUM: 
        /* add the request to the list head*/
        d_list_push_head(&pdev->sq_info.pending_normal, &pending->list);
        break;
    case CMD_PRIORITY_HIGH:
        /* add the request to the list head*/
        d_list_push_head(&pdev->sq_info.pending_high, &pending->list);
        break;
    default:
        DbgBreakIf( 1 ) ;
        // TODO_ROLLBACK - free sq_pending_command
        MM_RELEASE_SPQ_LOCK(pdev);
        return LM_STATUS_INVALID_PARAMETER ;
    }

    if(!(pdev->sq_info.num_pending_normal)) 
    {        
        LM_COMMON_DRV_STATS_ATOMIC_INC_ETH(pdev, tx_no_sq_wqe);
    }

    lm_status = lm_sq_post_from_list(pdev);
    if (lm_status == LM_STATUS_PENDING)
    {
        /* New slowpath was posted in pending state... make sure to flush sq 
         * after this... */
        if (pdev->sq_info.sq_state == SQ_STATE_PENDING)
        {
            sq_flush = TRUE;
        }
        
        lm_status = LM_STATUS_SUCCESS;
    }

    MM_RELEASE_SPQ_LOCK(pdev);

    if (sq_flush)
    {
        lm_sq_flush(pdev);
    }
    return lm_status ;
}


/*  
    post a ramrod to the sq 
    takes the sq pending list spinlock and adds the request
    will not block
    but the actuall posting to the sq might be deffered until there is room
    MUST only have one request pending per CID (this is up to the caller to enforce)
*/
lm_status_t lm_sq_post(struct _lm_device_t *pdev,
                       u32_t                cid,
                       u8_t                 command,
                       u8_t                 priority,
                       u16_t                type,
                       u64_t                data)
{
    struct sq_pending_command * pending = NULL;
    lm_status_t lm_status = LM_STATUS_SUCCESS;
    DbgBreakIf(! pdev);
    DbgBreakIf(! command); /* todo: make this more detailed*/

    /* allocate a new command struct and fill it */
    pending = mm_get_sq_pending_command(pdev);
    if( !pending )
    {
        DbgBreakIf(1);
        return LM_STATUS_FAILURE ;
    }

    lm_sq_post_fill_entry(pdev,pending,cid,command,type,data,TRUE);

    lm_status = lm_sq_post_entry(pdev,pending,priority);

    return lm_status ;
}

/*  
    inform the sq mechanism of completed ramrods
    because the completions arrive on the fast-path rings
    the fast-path needs to inform the sq that the ramrod has been serviced
    will not block
    does not take any locks
*/
void lm_sq_complete(struct _lm_device_t *pdev, u8_t priority, 
                    u8_t command, u16_t type, u32_t cid )
{

    struct sq_pending_command * pending;
    
    DbgBreakIf(!pdev);
        
    MM_ACQUIRE_SPQ_LOCK(pdev);

    DbgMessage5(pdev, INFORM, "lm_sq_complete: priority=%d, command=%d, type=%d, cid=%d num_pending_normal=%d\n",
               priority, command, type, cid, pdev->sq_info.num_pending_normal);

    switch( priority )
    {
    case CMD_PRIORITY_NORMAL:
    case CMD_PRIORITY_MEDIUM:
        pdev->sq_info.num_pending_normal ++;
        DbgBreakIf(pdev->sq_info.num_pending_normal > MAX_NORMAL_PRIORITY_SPE);
        break;
    case CMD_PRIORITY_HIGH:
        pdev->sq_info.num_pending_high ++;
        DbgBreakIf(pdev->sq_info.num_pending_high > MAX_HIGH_PRIORITY_SPE);
        break;
    default:
        DbgBreakIf( 1 ) ;
        break;
    }

    /* update sq consumer */
    pdev->sq_info.sq_chain.con_idx ++;
    pdev->sq_info.sq_chain.bd_left ++;

    /* Search for the completion in the pending_complete list*/
    /* Currently only supported if error recovery is supported */
    pending = (void*)d_list_peek_head(&pdev->sq_info.pending_complete);

    if (pdev->params.validate_sq_complete)
    {
        DbgBreakIf(!pending); /* not expected, but will deal with it... just won't  */
    }

    if (pdev->params.validate_sq_complete)
    {
        while (pending)
        {
            if ((pending->type == type) && 
                (pending->cmd == command) && 
                (pending->cid == cid))
            {
                /* got it... remove from list and free it */
                d_list_remove_entry(&pdev->sq_info.pending_complete, &pending->list);
                if(GET_FLAGS(pending->flags, SQ_PEND_RELEASE_MEM))
                {
                    mm_return_sq_pending_command(pdev, pending);
                }
                break;
            }
            pending = (void*)d_list_next_entry(&pending->list);
        }
    }
    else
    {
        /* TODO_ER: on no validation, just take the head... Workaround for mc-diag */
        pending = (void*)d_list_pop_head(&pdev->sq_info.pending_complete);
        if(GET_FLAGS(pending->flags, SQ_PEND_RELEASE_MEM))
        {
            mm_return_sq_pending_command(pdev, pending);
        }
    }

    DbgBreakIf(!pending); /* means none were found, assert but can deal with it... */
    
    MM_RELEASE_SPQ_LOCK(pdev);
}

/** 
 * @description 
 *    do any deffered posting pending on the sq, will take the list spinlock
 *    will not block. Check sq state, if its pending (it means no hw...) call flush
 *    at the end, which will take care of completing these completions internally.
 * @param pdev
 * 
 * @return lm_status_t SUCCESS: is no pending requests were sent. PENDING if a
 *                              if pending request was sent. 
 */
lm_status_t lm_sq_post_pending(struct _lm_device_t *pdev)
{
    lm_status_t                 lm_status = LM_STATUS_SUCCESS;
    u8_t                        sq_flush  = FALSE;

    if ( CHK_NULL(pdev) )
    {
        DbgBreakIf(!pdev);
        return LM_STATUS_INVALID_PARAMETER;
    }

    MM_ACQUIRE_SPQ_LOCK(pdev);

    lm_status = lm_sq_post_from_list(pdev);

    if (lm_status == LM_STATUS_PENDING)
    {
        /* New slowpath was posted in pending state... make sure to flush sq 
         * after this... */
        if (pdev->sq_info.sq_state == SQ_STATE_PENDING)
        {
            sq_flush = TRUE;
        }
    }
    
    MM_RELEASE_SPQ_LOCK(pdev);

    if (sq_flush)
    {
        lm_sq_flush(pdev);
    }
    return lm_status;
}


/*********************** ETH SLOWPATH RELATED FUNCTIONS ***************************/

void lm_eth_init_command_comp(struct _lm_device_t *pdev, struct common_ramrod_eth_rx_cqe *cqe)
{
    u32_t conn_and_cmd_data = mm_le32_to_cpu(cqe->conn_and_cmd_data);
    u32_t cid               = SW_CID(conn_and_cmd_data);
    u8_t  command           = conn_and_cmd_data >> COMMON_RAMROD_ETH_RX_CQE_CMD_ID_SHIFT;
    u8_t  ramrod_type       = cqe->ramrod_type;
    u32_t empty_data        = 0;

    DbgBreakIf(!pdev);

    DbgMessage3(pdev, WARNl2sp,
                "lm_eth_comp_cb: completion for cid=%d, command %d(0x%x)\n", cid, command, command);

    DbgBreakIf(!command);
    DbgBreakIfAll(ramrod_type & COMMON_RAMROD_ETH_RX_CQE_ERROR);

    switch (command)
    {
        case RAMROD_CMD_ID_ETH_CLIENT_SETUP:
            DbgBreakIf(PFDEV(pdev)->vars.connections[cid].con_state != LM_CON_STATE_CLOSE);
            PFDEV(pdev)->vars.connections[cid].con_state = LM_CON_STATE_OPEN;
            DbgMessage1(pdev, WARNl2sp,
                        "lm_eth_comp_cb: RAMROD ETH SETUP completed for cid=%d, - calling lm_extract_ramrod_req!\n", cid);
            lm_extract_ramrod_req(pdev, cid);
            break;

        case RAMROD_CMD_ID_ETH_CLIENT_UPDATE:
            DbgBreakMsg("Not implemented\n");
            break;

        case RAMROD_CMD_ID_ETH_HALT:
            DbgBreakIf(PFDEV(pdev)->vars.connections[cid].con_state != LM_CON_STATE_OPEN);
            PFDEV(pdev)->vars.connections[cid].con_state = LM_CON_STATE_HALT;
            DbgMessage(pdev, WARNl2sp, "lm_eth_comp_cb:RAMROD_CMD_ID_ETH_HALT- calling lm_extract_ramrod_req!\n");
            lm_extract_ramrod_req(pdev, cid);
            break;

        case RAMROD_CMD_ID_ETH_EMPTY:
            empty_data        = mm_le32_to_cpu(cqe->protocol_data.data_lo);
            MM_EMPTY_RAMROD_RECEIVED(pdev,empty_data);
            DbgMessage(pdev, WARNl2sp, "lm_eth_comp_cb:RAMROD_CMD_ID_ETH_EMPTY- calling lm_extract_ramrod_req!\n");
            lm_extract_ramrod_req(pdev, cid);
            break;

        case RAMROD_CMD_ID_ETH_TERMINATE:
            DbgBreakIf(PFDEV(pdev)->vars.connections[cid].con_state != LM_CON_STATE_HALT);
            PFDEV(pdev)->vars.connections[cid].con_state = LM_CON_STATE_TERMINATE;
            DbgMessage(pdev, WARNl2sp, "lm_eth_comp_cb:RAMROD_CMD_ID_ETH_TERMINATE- calling lm_extract_ramrod_req!\n");
            lm_extract_ramrod_req(pdev, cid);
            break;

        default:
            DbgMessage1(pdev,FATAL,"lm_eth_init_command_comp_cb unhandled ramrod comp command=%d\n",command);
            DbgBreakIf(1); // unhandled ramrod!
            break;
    }
#ifdef __LINUX
    mm_eth_ramrod_comp_cb(pdev, cqe);
#endif //__LINUX

    lm_sq_complete(pdev, CMD_PRIORITY_NORMAL, command, ETH_CONNECTION_TYPE, cid);
}

/** 
 * @Description 
 *      Function is the callback function for completing eth
 *      completions when no chip access exists. Part of
 *      "complete-pending-sq" flow
 * @param pdev
 * @param spe
 */
void lm_eth_comp_cb(struct _lm_device_t *pdev, struct sq_pending_command * pending)
{
    struct common_ramrod_eth_rx_cqe cqe;
    
    /* The idea is to prepare a cqe and call: common_ramrod_eth_rx_cqe */
    cqe.conn_and_cmd_data = pending->command.hdr.conn_and_cmd_data;
    cqe.ramrod_type = RX_ETH_CQE_TYPE_ETH_RAMROD;
    cqe.protocol_data.data_hi = pending->command.protocol_data.hi;
    cqe.protocol_data.data_lo = pending->command.protocol_data.lo;

    switch (pending->cmd)
    {
        /* Ramrods that complete on the EQ */
    case RAMROD_CMD_ID_ETH_RSS_UPDATE:
    case RAMROD_CMD_ID_ETH_FILTER_RULES:
    case RAMROD_CMD_ID_ETH_MULTICAST_RULES:
    case RAMROD_CMD_ID_ETH_FORWARD_SETUP:
    case RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES:
    case RAMROD_CMD_ID_ETH_SET_MAC:
        lm_eq_comp_cb(pdev, pending);
        break;

        /* Ramrods that complete on the RCQ */
    case RAMROD_CMD_ID_ETH_CLIENT_SETUP:
    case RAMROD_CMD_ID_ETH_CLIENT_UPDATE:
    case RAMROD_CMD_ID_ETH_HALT:
    case RAMROD_CMD_ID_ETH_EMPTY:
    case RAMROD_CMD_ID_ETH_TERMINATE:
        lm_eth_init_command_comp(pdev, &cqe);
        break;

    default:
        DbgBreakMsg("Unknown cmd");
    }
}
