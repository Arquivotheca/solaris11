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
 * Copyright(c) 2007-2011 Intel Corporation. All rights reserved.
 */

/*
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/* IntelVersion: 1.32 scm_011511_003853 */


#include "ixgbe_type.h"
#include "ixgbe_dcb.h"
#include "ixgbe_dcb_82598.h"
#include "ixgbe_dcb_82599.h"

/*
 * ixgbe_dcb_config - Struct containing DCB settings.
 * @dcb_config: Pointer to DCB config structure
 *
 * This function checks DCB rules for DCB settings.
 * The following rules are checked:
 * 1. The sum of bandwidth percentages of all Bandwidth Groups must total 100%.
 * 2. The sum of bandwidth percentages of all Traffic Classes within a Bandwidth
 *    Group must total 100.
 * 3. A Traffic Class should not be set to both Link Strict Priority
 *    and Group Strict Priority.
 * 4. Link strict Bandwidth Groups can only have link strict traffic classes
 *    with zero bandwidth.
 */
s32
ixgbe_dcb_check_config(struct ixgbe_dcb_config *dcb_config)
{
	struct tc_bw_alloc *p;
	s32 ret_val = IXGBE_SUCCESS;
	u8 i, j, bw = 0, bw_id;
	u8 bw_sum[2][MAX_BW_GROUP];
	bool link_strict[2][MAX_BW_GROUP];

	(void) memset(bw_sum, 0, sizeof (bw_sum));
	(void) memset(link_strict, 0, sizeof (link_strict));

	/* First Tx, then Rx */
	for (i = 0; i < 2; i++) {
		/* Check each traffic class for rule violation */
		for (j = 0; j < dcb_config->num_tcs.pg_tcs; j++) {
			p = &dcb_config->tc_config[j].path[i];

			bw = p->bwg_percent;
			bw_id = p->bwg_id;

			if (bw_id >= MAX_BW_GROUP) {
				ret_val = DCB_ERR_CONFIG;
				goto err_config;
			}
			if (p->prio_type == prio_link) {
				link_strict[i][bw_id] = true;
				/* Link strict should have zero bandwidth */
				if (bw) {
					ret_val = DCB_ERR_LS_BW_NONZERO;
					goto err_config;
				}
			} else if (!bw) {
				/*
				 * Traffic classes without link strict
				 * should have non-zero bandwidth.
				 */
				ret_val = DCB_ERR_TC_BW_ZERO;
				goto err_config;
			}
			bw_sum[i][bw_id] += bw;
		}

		bw = 0;

		/* Check each bandwidth group for rule violation */
		for (j = 0; j < MAX_BW_GROUP; j++) {
			bw += dcb_config->bw_percentage[i][j];
			/*
			 * Sum of bandwidth percentages of all traffic classes
			 * within a Bandwidth Group must total 100 except for
			 * link strict group (zero bandwidth).
			 */
			if (link_strict[i][j]) {
				if (bw_sum[i][j]) {
					/*
					 * Link strict group should have zero
					 * bandwidth.
					 */
					ret_val = DCB_ERR_LS_BWG_NONZERO;
					goto err_config;
				}
			} else if (bw_sum[i][j] != BW_PERCENT &&
			    bw_sum[i][j] != 0) {
				ret_val = DCB_ERR_TC_BW;
				goto err_config;
			}
		}

		if (bw != BW_PERCENT) {
			ret_val = DCB_ERR_BW_GROUP;
			goto err_config;
		}
	}

	return (DCB_SUCCESS);

err_config:
	DEBUGOUT2("DCB error code %d while checking %s settings.\n",
	    ret_val, (i == DCB_TX_CONFIG) ? "Tx" : "Rx");

	return (ret_val);
}

/*
 * ixgbe_dcb_calculate_tc_credits - Calculates traffic class credits
 * @ixgbe_dcb_config: Struct containing DCB settings.
 * @direction: Configuring either Tx or Rx.
 *
 * This function calculates the credits allocated to each traffic class.
 * It should be called only after the rules are checked by
 * ixgbe_dcb_check_config().
 */
s32
ixgbe_dcb_calculate_tc_credits(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config, u32 max_frame, u8 direction)
{
	struct tc_bw_alloc *p;
	s32 ret_val = IXGBE_SUCCESS;
	/* Initialization values default for Tx settings */
	u32 credit_refill    = 0;
	u32 credit_max    = 0;
	u32 min_credit    = 0;
	u32 min_percent    = 100;
	u16 min_multiplier    = 0;
	u16 link_percentage    = 0;
	u8  bw_percent    = 0;
	u8  i;

	if (dcb_config == NULL) {
		ret_val = DCB_ERR_CONFIG;
		goto out;
	}

	min_credit = ((max_frame / 2) + DCB_CREDIT_QUANTUM - 1) /
	    DCB_CREDIT_QUANTUM;

	/* Find smallest link percentage */
	for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
		p = &dcb_config->tc_config[i].path[direction];
		bw_percent = dcb_config->bw_percentage[direction][p->bwg_id];
		link_percentage = p->bwg_percent;

		link_percentage = (link_percentage * bw_percent) / 100;

		if (link_percentage && link_percentage < min_percent)
			min_percent = link_percentage;
	}

	/*
	 * The ratio between traffic classes will control the bandwidth
	 * percentages seen on the wire. To calculate this ratio we use
	 * a multiplier. It is required that the refill credits must be
	 * larger than the max frame size so here we find the smallest
	 * multiplier that will allow all bandwidth percentages to be
	 * greater than the max frame size.
	 */
	min_multiplier = (min_credit / min_percent) + 1;

	/* Find out the link percentage for each TC first */
	for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
		p = &dcb_config->tc_config[i].path[direction];
		bw_percent = dcb_config->bw_percentage[direction][p->bwg_id];

		link_percentage = p->bwg_percent;
		/* Must be careful of integer division for very small nums */
		link_percentage = (link_percentage * bw_percent) / 100;
		if (p->bwg_percent > 0 && link_percentage == 0)
			link_percentage = 1;

		/* Save link_percentage for reference */
		p->link_percent = (u8)link_percentage;

		/* Calculate credit refill and save it */
		credit_refill = min(link_percentage * min_multiplier,
		    MAX_CREDIT_REFILL);
		p->data_credits_refill = (u16)credit_refill;

		/* Calculate maximum credit for the TC */
		credit_max = (link_percentage * MAX_CREDIT) / 100;

		/*
		 * Adjustment based on rule checking, if the percentage
		 * of a TC is too small, the maximum credit may not be
		 * enough to send out a jumbo frame in data plane arbitration.
		 */
		if (credit_max && credit_max < min_credit)
			credit_max = min_credit;

		if (direction == DCB_TX_CONFIG) {
			/*
			 * Adjustment based on rule checking, if the
			 * percentage of a TC is too small, the maximum
			 * credit may not be enough to send out a TSO
			 * packet in descriptor plane arbitration.
			 */
			if (credit_max &&
			    (credit_max < MINIMUM_CREDIT_FOR_TSO) &&
			    (hw->mac.type == ixgbe_mac_82598EB))
				credit_max = MINIMUM_CREDIT_FOR_TSO;

			dcb_config->tc_config[i].desc_credits_max =
			    (u16)credit_max;
		}

		p->data_credits_max = (u16)credit_max;
	}

out:
	return (ret_val);
}

/*
 * ixgbe_dcb_get_tc_stats - Returns status of each traffic class
 * @hw: pointer to hardware structure
 * @stats: pointer to statistics structure
 * @tc_count:  Number of elements in bwg_array.
 *
 * This function returns the status data for each of the Traffic Classes in use.
 */
s32
ixgbe_dcb_get_tc_stats(struct ixgbe_hw *hw, struct ixgbe_hw_stats *stats,
    u8 tc_count)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_get_tc_stats_82598(hw, stats, tc_count);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		ret = ixgbe_dcb_get_tc_stats_82599(hw, stats, tc_count);
		break;
	default:
		break;
	}
	return (ret);
}

/*
 * ixgbe_dcb_get_pfc_stats - Returns CBFC status of each traffic class
 * @hw: pointer to hardware structure
 * @stats: pointer to statistics structure
 * @tc_count:  Number of elements in bwg_array.
 *
 * This function returns the CBFC status data for each of the Traffic Classes.
 */
s32
ixgbe_dcb_get_pfc_stats(struct ixgbe_hw *hw, struct ixgbe_hw_stats *stats,
    u8 tc_count)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_get_pfc_stats_82598(hw, stats, tc_count);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		ret = ixgbe_dcb_get_pfc_stats_82599(hw, stats, tc_count);
		break;
	default:
		break;
	}
	return (ret);
}

/*
 * ixgbe_dcb_config_rx_arbiter - Config Rx arbiter
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure Rx Data Arbiter and credits for each traffic class.
 */
s32
ixgbe_dcb_config_rx_arbiter(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_config_rx_arbiter_82598(hw, dcb_config);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		ret = ixgbe_dcb_config_rx_arbiter_82599(hw, dcb_config);
		break;
	default:
		break;
	}
	return (ret);
}

/*
 * ixgbe_dcb_config_tx_desc_arbiter - Config Tx Desc arbiter
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure Tx Descriptor Arbiter and credits for each traffic class.
 */
s32
ixgbe_dcb_config_tx_desc_arbiter(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_config_tx_desc_arbiter_82598(hw, dcb_config);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		ret = ixgbe_dcb_config_tx_desc_arbiter_82599(hw, dcb_config);
		break;
	default:
		break;
	}
	return (ret);
}

/*
 * ixgbe_dcb_config_tx_data_arbiter - Config Tx data arbiter
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure Tx Data Arbiter and credits for each traffic class.
 */
s32
ixgbe_dcb_config_tx_data_arbiter(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_config_tx_data_arbiter_82598(hw, dcb_config);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		ret = ixgbe_dcb_config_tx_data_arbiter_82599(hw, dcb_config);
		break;
	default:
		break;
	}
	return (ret);
}

/*
 * ixgbe_dcb_config_pfc - Config priority flow control
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure Priority Flow Control for each traffic class.
 */
s32
ixgbe_dcb_config_pfc(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_config_pfc_82598(hw, dcb_config);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		ret = ixgbe_dcb_config_pfc_82599(hw, dcb_config);
		break;
	default:
		break;
	}
	return (ret);
}

/*
 * ixgbe_dcb_config_tc_stats - Config traffic class statistics
 * @hw: pointer to hardware structure
 *
 * Configure queue statistics registers, all queues belonging to same traffic
 * class uses a single set of queue statistics counters.
 */
s32
ixgbe_dcb_config_tc_stats(struct ixgbe_hw *hw)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_config_tc_stats_82598(hw);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		ret = ixgbe_dcb_config_tc_stats_82599(hw);
		break;
	default:
		break;
	}
	return (ret);
}

/*
 * ixgbe_dcb_hw_config - Config and enable DCB
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure dcb settings and enable dcb mode.
 */
s32
ixgbe_dcb_hw_config(struct ixgbe_hw *hw,
    struct ixgbe_dcb_config *dcb_config)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_hw_config_82598(hw, dcb_config);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		ret = ixgbe_dcb_hw_config_82599(hw, dcb_config);
		break;
	default:
		break;
	}
	return (ret);
}
