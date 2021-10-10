#ifdef __LINUX
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/crc32.h>
#endif

#include "bcmtype.h"
#include "utils.h"
#include "ecore_sp_verbs.h"
#include "lm5710.h"
#include "command.h"
#include "debug.h"
#include "ecore_common.h"

/************************** raw_obj functions *************************/
static BOOL ecore_raw_check_pending(struct ecore_raw_obj *o)
{
	return ECORE_TEST_BIT(o->state, o->pstate);
}

static void ecore_raw_clear_pending(struct ecore_raw_obj *o)
{
	smp_mb__before_clear_bit();
	ECORE_CLEAR_BIT(o->state, o->pstate);
	smp_mb__after_clear_bit();
}

static void ecore_raw_set_pending(struct ecore_raw_obj *o)
{
	smp_mb__before_clear_bit();
	ECORE_SET_BIT(o->state, o->pstate);
	smp_mb__after_clear_bit();
}

/**
 * Waits until the given bit(state) is cleared in a state memory
 * buffer
 *
 * @param pdev
 * @param state state which is to be cleared
 * @param state_p state buffer
 *
 * @return int
 */
static INLINE int ecore_state_wait(struct _lm_device_t *pdev, int state,
					unsigned long *pstate)
{
	/* can take a while if any port is running */
    int cnt = 5000;

#ifndef ECORE_ERASE
    int delay_us = 1000;

    /* In VBD We'll wait 10,000 times 100us (1 second) +
     * 2360 times 25000us (59sec) = total 60 sec
     * (Winodws only note) the 25000 wait will cause wait
     * to be without CPU stall (look in win_util.c)
     */
    cnt = 10000 + 2360;
#endif

	if (CHIP_REV_IS_EMUL(pdev))
		cnt*=20;

	//DP(NETIF_MSG_IFUP, "waiting for state to become %d\n", state);

	ECORE_MIGHT_SLEEP();
	while (cnt--) {
		if (!ECORE_TEST_BIT(state, pstate)) {
#ifdef ECORE_STOP_ON_ERROR
	//  	DP(NETIF_MSG_IFUP, "exit  (cnt %d)\n", 5000 - cnt);
#endif
			return 0;
		}

#ifndef ECORE_ERASE // erased by ecorize script...
        // in case reset is in progress we won't get completion
        if( lm_reset_is_inprogress(pdev) )
            return 0;

        delay_us = (cnt >= 2360) ? 100 : 25000 ;
#endif

		mm_wait(pdev, delay_us);

		if (pdev->panic)
			return ECORE_IO;

	}

	/* timeout! */
	ECORE_ERR1("timeout waiting for state %d\n", state);
#ifdef ECORE_STOP_ON_ERROR
	ecore_panic();
#endif

	return ECORE_TIMEOUT;
}

static int ecore_raw_wait(struct _lm_device_t *pdev, struct ecore_raw_obj *raw)
{
	return ecore_state_wait(pdev, raw->state, raw->pstate);
}

/***************** Classification verbs: Set/Del MAC/VLAN/VLAN-MAC ************/
/* credit handling callbacks */
static BOOL ecore_get_credit_mac(struct ecore_vlan_mac_obj *o, u8 * cam_offset)
{
	struct ecore_credit_pool_obj *mp = o->macs_pool;

	DbgBreakIf(!mp);

	return mp->get(mp, cam_offset);
}

static BOOL ecore_get_credit_vlan(struct ecore_vlan_mac_obj *o, u8 * cam_offset)
{
	struct ecore_credit_pool_obj *vp = o->vlans_pool;

	DbgBreakIf(!vp);

	return vp->get(vp, cam_offset);
}

static BOOL ecore_get_credit_vlan_mac(struct ecore_vlan_mac_obj *o, u8 * cam_offset)
{
	struct ecore_credit_pool_obj *mp = o->macs_pool;
	struct ecore_credit_pool_obj *vp = o->vlans_pool;

	if (!mp->get(mp, cam_offset))
		return FALSE;

	if (!vp->get(vp, cam_offset)) {
		mp->put(mp, *cam_offset);
		return FALSE;
	}

	return TRUE;
}

static BOOL ecore_put_credit_mac(struct ecore_vlan_mac_obj *o, u8 cam_offset)
{
	struct ecore_credit_pool_obj *mp = o->macs_pool;

	return mp->put(mp, cam_offset);
}

static BOOL ecore_put_credit_vlan(struct ecore_vlan_mac_obj *o, u8 cam_offset)
{
	struct ecore_credit_pool_obj *vp = o->vlans_pool;

	return vp->put(vp, cam_offset);
}

static BOOL ecore_put_credit_vlan_mac(struct ecore_vlan_mac_obj *o, u8 cam_offset)
{
	struct ecore_credit_pool_obj *mp = o->macs_pool;
	struct ecore_credit_pool_obj *vp = o->vlans_pool;

	if (!mp->put(mp, cam_offset))
		return FALSE;

	if (!vp->put(vp, cam_offset)) {
		mp->get(mp, &cam_offset);
		return FALSE;
	}

	return TRUE;
}

/* check_add() callbacks */
static BOOL ecore_check_mac_add(struct ecore_vlan_mac_ramrod_params *p)
{
	struct ecore_list_elem *pos;
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;

	/* Check if a requested MAC already exists */
	ecore_list_for_each_entry(pos, &o->head, link, struct ecore_list_elem)
		if (!memcmp(p->data.mac.mac, pos->data.mac.mac, ETH_ALEN))
			return FALSE;

	return TRUE;
}

static BOOL ecore_check_vlan_add(struct ecore_vlan_mac_ramrod_params *p)
{
	struct ecore_list_elem *pos;
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;

	ecore_list_for_each_entry(pos, &o->head, link, struct ecore_list_elem)
		if (p->data.vlan.vlan == pos->data.vlan.vlan)
			return FALSE;

	return TRUE;
}

static BOOL
	ecore_check_vlan_mac_add(struct ecore_vlan_mac_ramrod_params *p)
{
	struct ecore_list_elem *pos;
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;

	ecore_list_for_each_entry(pos, &o->head, link, struct ecore_list_elem)
		if ((p->data.vlan_mac.vlan == pos->data.vlan_mac.vlan) &&
			(!memcmp(p->data.vlan_mac.mac, pos->data.vlan_mac.mac,
				 ETH_ALEN)))
			return FALSE;

	return TRUE;
}


/* check_del() callbacks */
static struct ecore_list_elem *
	ecore_check_mac_del(struct ecore_vlan_mac_ramrod_params *p)
{
	struct ecore_list_elem *pos;
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;

	ecore_list_for_each_entry(pos, &o->head, link, struct ecore_list_elem)
		if (!memcmp(p->data.mac.mac, pos->data.mac.mac, ETH_ALEN))
			return pos;

	return NULL;
}

static struct ecore_list_elem *
	ecore_check_vlan_del(struct ecore_vlan_mac_ramrod_params *p)
{
	struct ecore_list_elem *pos;
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;

	ecore_list_for_each_entry(pos, &o->head, link, struct ecore_list_elem)
		if (p->data.vlan.vlan == pos->data.vlan.vlan)
			return pos;

	return NULL;
}

static struct ecore_list_elem *
	ecore_check_vlan_mac_del(struct ecore_vlan_mac_ramrod_params *p)
{
	struct ecore_list_elem *pos;
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;

	ecore_list_for_each_entry(pos, &o->head, link, struct ecore_list_elem)
		if ((p->data.vlan_mac.vlan == pos->data.vlan_mac.vlan) &&
			(!memcmp(p->data.vlan_mac.mac, pos->data.vlan_mac.mac,
				 ETH_ALEN)))
			return pos;

	return NULL;
}

static INLINE u8 ecore_vlan_mac_get_rx_tx_flag(struct ecore_vlan_mac_obj *o)
{
	struct ecore_raw_obj *raw = &o->raw;
	u8 rx_tx_flag = 0;

	if ((raw->obj_type == ECORE_OBJ_TYPE_TX) ||
		(raw->obj_type == ECORE_OBJ_TYPE_RX_TX))
		rx_tx_flag |= ETH_CLASSIFY_CMD_HEADER_TX_CMD;

	if ((raw->obj_type == ECORE_OBJ_TYPE_RX) ||
		(raw->obj_type == ECORE_OBJ_TYPE_RX_TX))
		rx_tx_flag |= ETH_CLASSIFY_CMD_HEADER_RX_CMD;

	return rx_tx_flag;
}

#if 0 // Not used by VBD yet. FIXME: replace lm_set_mac_in_nig with this one...
/* LLH CAM line allocations */
enum {
	LLH_CAM_ISCSI_ETH_LINE = 0,
	LLH_CAM_ETH_LINE,
	LLH_CAM_MAX_PF_LINE = NIG_REG_LLH1_FUNC_MEM_SIZE / 2
};

static void ecore_set_mac_in_nig(struct _lm_device_t *pdev,
			  int add,
			  unsigned char* dev_addr,
			  int index)
{
	u32 wb_data[2];
	u32 reg_offset = PORT_ID(pdev)? NIG_REG_LLH1_FUNC_MEM :
				 NIG_REG_LLH0_FUNC_MEM;

	if (!IS_MF_SI(pdev) || index > LLH_CAM_MAX_PF_LINE)
		return;

	if (add) {
		/* LLH_FUNC_MEM is a u64 WB register */
		reg_offset += 8*index;

		wb_data[0] = ((dev_addr[2] << 24) | (dev_addr[3] << 16) |
				  (dev_addr[4] <<  8) |  dev_addr[5]);
		wb_data[1] = ((dev_addr[0] <<  8) |  dev_addr[1]);

		REG_WR_DMAE(pdev, reg_offset, wb_data, 2);
	}

	REG_WR(pdev, (PORT_ID(pdev) ? NIG_REG_LLH1_FUNC_MEM_ENABLE :
				  NIG_REG_LLH0_FUNC_MEM_ENABLE ) +
			4*index, add);
}
#endif
/**
 * Set a header in a single classify ramrod command according to
 * the provided parameters.
 *
 * @param pdev
 * @param p
 * @param add if TRUE the command is an ADD command, otherwise
 *  		  it's a DEL command
 * @param hdr Pointer to a header to setup
 * @param opcode CLASSIFY_RULE_OPCODE_XXX
 */
static INLINE void ecore_vlan_mac_set_cmd_hdr_e2(struct _lm_device_t *pdev,
				struct ecore_vlan_mac_obj *o, BOOL add,
				int opcode, struct eth_classify_cmd_header *hdr)
{
	struct ecore_raw_obj *raw = &o->raw;

	hdr->client_id = raw->cl_id;
	hdr->func_id = raw->func_id;

	/* Rx or/and Tx (internal switching) configuration ? */
	hdr->cmd_general_data |=
		ecore_vlan_mac_get_rx_tx_flag(o);

	if (add)
		hdr->cmd_general_data |= ETH_CLASSIFY_CMD_HEADER_IS_ADD;

	hdr->cmd_general_data |=
		(opcode << ETH_CLASSIFY_CMD_HEADER_OPCODE_SHIFT);
}

/**
 * Set the classify ramrod data header: currently we always
 * configure one rule and echo field to contain a CID and an
 * opcode type.
 *
 * @param cid
 * @param type ECORE_FILTER_XXX_PENDING
 * @param hdr Header to setup
 */
static INLINE void ecore_vlan_mac_set_rdata_hdr_e2(u32 cid, int type,
						struct eth_classify_header *hdr, u8 rule_cnt)
{
	hdr->echo = (cid & ECORE_SWCID_MASK) | (type << ECORE_SWCID_SHIFT);
	hdr->rule_cnt = rule_cnt;
}


/* hw_config() callbacks */
static int ecore_setup_mac_e2(struct _lm_device_t *pdev,
				  struct ecore_vlan_mac_ramrod_params *p,
				  struct ecore_list_elem *pos,
				  BOOL add)
{
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;
	struct ecore_raw_obj *raw = &o->raw;
	struct eth_classify_rules_ramrod_data *data =
		(struct eth_classify_rules_ramrod_data *)(raw->rdata);

	/* Update a list element */
	memcpy(pos->data.mac.mac, p->data.mac.mac, ETH_ALEN);

	/* Set LLH CAM entry: currently only iSCSI and ETH macs are
	 * relevant. In addition, current implementation is tuned for a
	 * single ETH MAC.
	 *
	 * When multiple unicast ETH MACs PF configuration in switch
	 * independent mode is required (NetQ, multiple netdev MACs,
	 * etc.), consider better utilisation of 8 per function MAC
	 * entries in the LLH register. There is also
	 * NIG_REG_P[01]_LLH_FUNC_MEM2 registers that complete the
	 * total number of CAM entries to 16.
	 */
#if 0 // FIXME: replace lm_set_cam_in_nig and use this functionality.
	if (ECORE_TEST_BIT(ECORE_ISCSI_ETH_MAC, &p->vlan_mac_flags))
		ecore_set_mac_in_nig(pdev, add, p->data.mac.mac,
					 LLH_CAM_ISCSI_ETH_LINE);
	else
		ecore_set_mac_in_nig(pdev, add, p->data.mac.mac,
					 LLH_CAM_ETH_LINE);
#endif

	/* Reset the ramrod data buffer */
	memset(data, 0, sizeof(*data));

	/* Setup ramrod data */
	ecore_vlan_mac_set_rdata_hdr_e2(raw->cid, raw->state, &data->header, 1);

	ecore_vlan_mac_set_cmd_hdr_e2(pdev, o, add, CLASSIFY_RULE_OPCODE_MAC,
					  &data->rules[0].mac.header);

	//DP(NETIF_MSG_IFUP, "About to %s MAC "ECORE_MAC_FMT" for Client %d\n",
	//   (add ? "add" : "delete"), ECORE_MAC_PRN_LIST(p->data.mac.mac),
	//   raw->cl_id);

	/* Set a MAC itself */
	ecore_set_fw_mac_addr(&data->rules[0].mac.mac_msb,
				  &data->rules[0].mac.mac_mid,
				  &data->rules[0].mac.mac_lsb, p->data.mac.mac);

	/* Commit writes towards the memory before sending a ramrod */
	mb();

	/* Send a ramrod */
	return ecore_sp_post(pdev,
		RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES,
		raw->cid,
		raw->rdata_mapping,
		ETH_CONNECTION_TYPE);
}

static int ecore_setup_move_mac_e2(struct _lm_device_t *pdev,
				   struct ecore_vlan_mac_ramrod_params *p,
				   struct ecore_vlan_mac_obj *dest_o,
				   struct ecore_list_elem *pos)
{
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;
	struct ecore_raw_obj *raw = &o->raw;
	struct eth_classify_rules_ramrod_data *data =
	  (struct eth_classify_rules_ramrod_data *)(raw->rdata);

	/* Update a list element */
	memcpy(pos->data.mac.mac, p->data.mac.mac, ETH_ALEN);

	/* Reset the ramrod data buffer */
	memset(data, 0, sizeof(*data));

	/* Setup ramrod data */
	ecore_vlan_mac_set_rdata_hdr_e2(raw->cid, ECORE_FILTER_MAC_PENDING,
					&data->header, 2);

	ecore_vlan_mac_set_cmd_hdr_e2(pdev, o, 0, CLASSIFY_RULE_OPCODE_MAC,
					  &data->rules[0].mac.header);

	ecore_vlan_mac_set_cmd_hdr_e2(pdev, dest_o, 1, CLASSIFY_RULE_OPCODE_MAC,
					  &data->rules[1].mac.header);

	/* Set VLAN and MAC themselvs (need to set on both rules same content */
	ecore_set_fw_mac_addr(&data->rules[0].mac.mac_msb,
				  &data->rules[0].mac.mac_mid,
				  &data->rules[0].mac.mac_lsb,
				  p->data.mac.mac);

	ecore_set_fw_mac_addr(&data->rules[1].mac.mac_msb,
				  &data->rules[1].mac.mac_mid,
				  &data->rules[1].mac.mac_lsb,
				  p->data.mac.mac);

	/* Commit writes towards the memory before sending a ramrod */
	mb();

	/* Send a ramrod */
	return ecore_sp_post(pdev,
					 RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES ,
					 raw->cid,
					 raw->rdata_mapping,
					 ETH_CONNECTION_TYPE);
}


/**
 * Set a header in a single classify ramrod command according to
 * the provided parameters.
 *
 * @param pdev
 * @param p
 * @param add if TRUE the command is an ADD command, otherwise
 *  		  it's a DEL command
 * @param hdr Pointer to a header to setup
 * @param opcode CLASSIFY_RULE_OPCODE_XXX
 */
static INLINE void ecore_vlan_mac_set_rdata_hdr_e1x(struct _lm_device_t *pdev,
			struct ecore_vlan_mac_obj *o, int type, u8 cam_offset,
			struct mac_configuration_hdr *hdr)
{
	struct ecore_raw_obj *r = &o->raw;

	hdr->length = 1;
	hdr->offset = cam_offset;
	hdr->client_id = 0xff;
	hdr->echo = ((r->cid & ECORE_SWCID_MASK) | (type << ECORE_SWCID_SHIFT));
}

static INLINE void ecore_vlan_mac_set_cfg_entry_e1x(struct _lm_device_t *pdev,
				struct ecore_vlan_mac_obj *o, BOOL add,
				int opcode, u8 *mac, u16 vlan_id,
				struct mac_configuration_entry *cfg_entry)
{
	struct ecore_raw_obj *r = &o->raw;
	u32 cl_bit_vec = (1 << r->cl_id);

	cfg_entry->clients_bit_vector = mm_cpu_to_le32(cl_bit_vec);
	cfg_entry->pf_id = r->func_id;

	cfg_entry->vlan_id = mm_cpu_to_le16(vlan_id);

	if (add) {
		ECORE_SET_FLAG(cfg_entry->flags, MAC_CONFIGURATION_ENTRY_ACTION_TYPE,
			 T_ETH_MAC_COMMAND_SET);
		ECORE_SET_FLAG(cfg_entry->flags,
			 MAC_CONFIGURATION_ENTRY_VLAN_FILTERING_MODE,
			 opcode);

		/* Set a MAC in a ramrod data */
		ecore_set_fw_mac_addr(&cfg_entry->msb_mac_addr,
					  &cfg_entry->middle_mac_addr,
					  &cfg_entry->lsb_mac_addr, mac);
	} else
		ECORE_SET_FLAG(cfg_entry->flags, MAC_CONFIGURATION_ENTRY_ACTION_TYPE,
			 T_ETH_MAC_COMMAND_INVALIDATE);
}

static INLINE void ecore_vlan_mac_set_rdata_e1x(struct _lm_device_t *pdev,
		struct ecore_vlan_mac_obj *o, int type, u8 cam_offset, BOOL add,
		u8 *mac, u16 vlan_id, int opcode, struct mac_configuration_cmd *config)
{
	struct mac_configuration_entry *cfg_entry = &config->config_table[0];
	// struct ecore_raw_obj *raw = &o->raw;

	ecore_vlan_mac_set_rdata_hdr_e1x(pdev, o, type, cam_offset, &config->hdr);
	ecore_vlan_mac_set_cfg_entry_e1x(pdev, o, add, opcode, mac, vlan_id, cfg_entry);

	//DP(NETIF_MSG_IFUP, "%s MAC "ECORE_MAC_FMT" PF_ID %d  CLID %d\n",
	//   (add ? "setting" : "clearing"), ECORE_MAC_PRN_LIST(mac),
	//   raw->func_id, raw->cl_id);
}


static int ecore_setup_mac_e1h(struct _lm_device_t *pdev,
				   struct ecore_vlan_mac_ramrod_params *p,
				   struct ecore_list_elem *pos,
				   BOOL add)
{
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;
	struct ecore_raw_obj *raw = &o->raw;
	struct mac_configuration_cmd *config =
		(struct mac_configuration_cmd *)(raw->rdata);

	/* Update a list element */
	memcpy(pos->data.mac.mac, p->data.mac.mac, ETH_ALEN);

	/* Reset the ramrod data buffer */
	memset(config, 0, sizeof(*config));

	ecore_vlan_mac_set_rdata_e1x(pdev, o, ECORE_FILTER_MAC_PENDING,
					 pos->cam_offset, add, p->data.mac.mac, 0,
					 ETH_VLAN_FILTER_ANY_VLAN, config);

	mb();

	return ecore_sp_post(pdev,
		RAMROD_CMD_ID_ETH_SET_MAC,
		raw->cid,
		raw->rdata_mapping,
		ETH_CONNECTION_TYPE);

}

static int ecore_setup_mac_e1(struct _lm_device_t *pdev,
				   struct ecore_vlan_mac_ramrod_params *p,
				   struct ecore_list_elem *pos,
				   BOOL add)
{
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;
	struct ecore_raw_obj *raw = &o->raw;
	struct mac_configuration_cmd *config =
		(struct mac_configuration_cmd *)(raw->rdata);

	/* Update a list element */
	memcpy(pos->data.mac.mac, p->data.mac.mac, ETH_ALEN);

	/* Reset the ramrod data buffer */
	memset(config, 0, sizeof(*config));

	ecore_vlan_mac_set_rdata_e1x(pdev, o, ECORE_FILTER_MAC_PENDING,
					 pos->cam_offset, add, p->data.mac.mac, 0,
					 ETH_VLAN_FILTER_ANY_VLAN, config);

	mb();

	return ecore_sp_post(pdev,
		RAMROD_CMD_ID_ETH_SET_MAC,
		raw->cid,
		raw->rdata_mapping,
		ETH_CONNECTION_TYPE);
}
// not supported for E1 and E1H
static int ecore_setup_move_e1x_always_err(struct _lm_device_t *pdev,
				   struct ecore_vlan_mac_ramrod_params *p,
				   struct ecore_vlan_mac_obj *dest_o,
				   struct ecore_list_elem *pos)
{
	return ECORE_INVAL;
}

static int ecore_setup_vlan_e2(struct _lm_device_t *pdev,
					  struct ecore_vlan_mac_ramrod_params *p,
					  struct ecore_list_elem *pos,
					  BOOL add)
{
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;
	struct ecore_raw_obj *raw = &o->raw;
	struct eth_classify_rules_ramrod_data *data =
	  (struct eth_classify_rules_ramrod_data *)(raw->rdata);

	/* Update a list element */
	pos->data.vlan.vlan = p->data.vlan.vlan;

	/* Reset the ramrod data buffer */
	memset(data, 0, sizeof(*data));

	/* Setup ramrod data */
	ecore_vlan_mac_set_rdata_hdr_e2(raw->cid, ECORE_FILTER_VLAN_PENDING,
					&data->header, 1);

	ecore_vlan_mac_set_cmd_hdr_e2(pdev, o, add, CLASSIFY_RULE_OPCODE_VLAN,
					  &data->rules[0].vlan.header);

	//DP(NETIF_MSG_IFUP, "About to %s VLAN %d\n", (add ? "add" : "delete"),
	//    p->data.vlan.vlan);

	/* Set a VLAN itself */
	data->rules[0].vlan.vlan = mm_cpu_to_le16(p->data.vlan.vlan);

	/* Commit writes towards the memory before sending a ramrod */
	mb();

	/* Send a ramrod */
	return ecore_sp_post(pdev,
		RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES,
		raw->cid,
		raw->rdata_mapping,
		ETH_CONNECTION_TYPE);
}

static int ecore_setup_move_vlan_e2(struct _lm_device_t *pdev,
				   struct ecore_vlan_mac_ramrod_params *p,
				   struct ecore_vlan_mac_obj *dest_o,
				   struct ecore_list_elem *pos)
{
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;
	struct ecore_raw_obj *raw = &o->raw;
	struct eth_classify_rules_ramrod_data *data =
	  (struct eth_classify_rules_ramrod_data *)(raw->rdata);

	/* Update a list element */
	pos->data.vlan.vlan = p->data.vlan_mac.vlan;

	/* Reset the ramrod data buffer */
	memset(data, 0, sizeof(*data));

	/* Setup ramrod data */
	ecore_vlan_mac_set_rdata_hdr_e2(raw->cid, ECORE_FILTER_VLAN_PENDING,
					&data->header, 2);

	ecore_vlan_mac_set_cmd_hdr_e2(pdev, o, 0, CLASSIFY_RULE_OPCODE_VLAN,
					  &data->rules[0].vlan.header);

	ecore_vlan_mac_set_cmd_hdr_e2(pdev, dest_o, 1, CLASSIFY_RULE_OPCODE_VLAN,
					  &data->rules[1].vlan.header);

	/* Set VLAN and MAC themselvs (need to set on both rules same content */
	data->rules[0].vlan.vlan = mm_cpu_to_le16(p->data.vlan_mac.vlan);
	data->rules[1].vlan.vlan = mm_cpu_to_le16(p->data.vlan_mac.vlan);

	/* Commit writes towards the memory before sending a ramrod */
	mb();

	/* Send a ramrod */
	return ecore_sp_post(pdev,
					 RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES,
					 raw->cid,
					 raw->rdata_mapping,
					 ETH_CONNECTION_TYPE);
}

static int ecore_setup_vlan_e1x(struct _lm_device_t *pdev,
					  struct ecore_vlan_mac_ramrod_params *p,
					  struct ecore_list_elem *pos,
					  BOOL add)
{
	/* Do nothing for 57710 and 57711 */
	p->vlan_mac_obj->raw.clear_pending(&p->vlan_mac_obj->raw);
	return 0;
}

static int ecore_setup_vlan_mac_e2(struct _lm_device_t *pdev,
				   struct ecore_vlan_mac_ramrod_params *p,
				   struct ecore_list_elem *pos,
				   BOOL add)
{
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;
	struct ecore_raw_obj *raw = &o->raw;
	struct eth_classify_rules_ramrod_data *data =
	  (struct eth_classify_rules_ramrod_data *)(raw->rdata);

	/* Update a list element */
	pos->data.vlan_mac.vlan = p->data.vlan_mac.vlan;
	memcpy(pos->data.vlan_mac.mac, p->data.vlan_mac.mac, ETH_ALEN);

	/* Reset the ramrod data buffer */
	memset(data, 0, sizeof(*data));

	/* Setup ramrod data */
	ecore_vlan_mac_set_rdata_hdr_e2(raw->cid, ECORE_FILTER_VLAN_MAC_PENDING,
					&data->header, 1);

	ecore_vlan_mac_set_cmd_hdr_e2(pdev, o, add, CLASSIFY_RULE_OPCODE_PAIR,
					  &data->rules[0].pair.header);

	/* Set VLAN and MAC themselvs */
	data->rules[0].pair.vlan = mm_cpu_to_le16(p->data.vlan_mac.vlan);
	ecore_set_fw_mac_addr(&data->rules[0].pair.mac_msb,
				  &data->rules[0].pair.mac_mid,
				  &data->rules[0].pair.mac_lsb,
				  p->data.vlan_mac.mac);

	/* Commit writes towards the memory before sending a ramrod */
	mb();

	/* Send a ramrod */
	return ecore_sp_post(pdev,
		RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES,
		raw->cid,
		raw->rdata_mapping,
		ETH_CONNECTION_TYPE);
}

static int ecore_setup_move_vlan_mac_e2(struct _lm_device_t *pdev,
				   struct ecore_vlan_mac_ramrod_params *p,
				   struct ecore_vlan_mac_obj *dest_o,
				   struct ecore_list_elem *pos)
{
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;
	struct ecore_raw_obj *raw = &o->raw;
	struct eth_classify_rules_ramrod_data *data =
	  (struct eth_classify_rules_ramrod_data *)(raw->rdata);

	/* Update a list element */
	pos->data.vlan_mac.vlan = p->data.vlan_mac.vlan;
	memcpy(pos->data.vlan_mac.mac, p->data.vlan_mac.mac, ETH_ALEN);

	/* Reset the ramrod data buffer */
	memset(data, 0, sizeof(*data));

	/* Setup ramrod data */
	ecore_vlan_mac_set_rdata_hdr_e2(raw->cid, ECORE_FILTER_VLAN_MAC_PENDING,
					&data->header, 2);

	ecore_vlan_mac_set_cmd_hdr_e2(pdev, o, 0, CLASSIFY_RULE_OPCODE_PAIR,
					  &data->rules[0].pair.header);

	ecore_vlan_mac_set_cmd_hdr_e2(pdev, dest_o, 1, CLASSIFY_RULE_OPCODE_PAIR,
					  &data->rules[1].pair.header);

	/* Set VLAN and MAC themselvs (need to set on both rules same content */
	data->rules[0].pair.vlan = mm_cpu_to_le16(p->data.vlan_mac.vlan);
	data->rules[1].pair.vlan = mm_cpu_to_le16(p->data.vlan_mac.vlan);

	ecore_set_fw_mac_addr(&data->rules[0].pair.mac_msb,
				  &data->rules[0].pair.mac_mid,
				  &data->rules[0].pair.mac_lsb,
				  p->data.vlan_mac.mac);

	ecore_set_fw_mac_addr(&data->rules[1].pair.mac_msb,
				  &data->rules[1].pair.mac_mid,
				  &data->rules[1].pair.mac_lsb,
				  p->data.vlan_mac.mac);

	/* Commit writes towards the memory before sending a ramrod */
	mb();

	/* Send a ramrod */
	return ecore_sp_post(pdev,
					 RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES ,
					 raw->cid,
					 raw->rdata_mapping,
					 ETH_CONNECTION_TYPE);
}

static int ecore_setup_vlan_mac_e1h(struct _lm_device_t *pdev,
				   struct ecore_vlan_mac_ramrod_params *p,
				   struct ecore_list_elem *pos,
				   BOOL add)
{
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;
	struct ecore_raw_obj *raw = &o->raw;
	struct mac_configuration_cmd *config =
		(struct mac_configuration_cmd *)(raw->rdata);

	/* Update a list element */
	/* Update a list element */
	pos->data.vlan_mac.vlan = p->data.vlan_mac.vlan;
	memcpy(pos->data.vlan_mac.mac, p->data.vlan_mac.mac, ETH_ALEN);

	/* Reset the ramrod data buffer */
	memset(config, 0, sizeof(*config));

	ecore_vlan_mac_set_rdata_e1x(pdev, o, ECORE_FILTER_VLAN_MAC_PENDING,
					 pos->cam_offset, add, p->data.vlan_mac.mac, p->data.vlan_mac.vlan,
					 ETH_VLAN_FILTER_CLASSIFY, config);
	mb();

	return ecore_sp_post(pdev,
		RAMROD_CMD_ID_ETH_SET_MAC,
		raw->cid,
		raw->rdata_mapping,
		ETH_CONNECTION_TYPE);

}

#define list_next_entry(pos, member) \
	list_entry((pos)->member.next, typeof(*(pos)), member)

static INLINE void ecore_set_vlan_mac_data_flags(unsigned long *from,
						 unsigned long *to)
{
	if (ECORE_TEST_BIT(ECORE_ETH_MAC, from))
		ECORE_SET_BIT(ECORE_ETH_MAC, to);

	if (ECORE_TEST_BIT(ECORE_ISCSI_ETH_MAC, from))
		ECORE_SET_BIT(ECORE_ISCSI_ETH_MAC, to);

	if (ECORE_TEST_BIT(ECORE_NETQ_ETH_MAC, from))
		ECORE_SET_BIT(ECORE_NETQ_ETH_MAC, to);
}

/**
 * Reconfigures the next MAC/VLAN/VLAN-MAC element from the previously
 * configured elements list.
 *
 * @param pdev
 * @param p Command parameters (RAMROD_COMP_WAIT bit in ramrod_flags is only
 *  		taken into an account)
 * @param ppos a pointer to the cooky that should be given back in the next call
 *  		  to make function handle the next element. If *ppos is set to NULL
 *  		  it will restart the iterator. If returned *ppos == NULL this means
 *  		  that the last element has been handled.
 *
 * @return int
 */
int ecore_vlan_mac_restore(struct _lm_device_t *pdev,
			   struct ecore_vlan_mac_ramrod_params *p,
			   struct ecore_list_elem **ppos)
{
	struct ecore_list_elem *pos;
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;
	struct ecore_raw_obj *raw = &p->vlan_mac_obj->raw;
	unsigned long orig_vlan_mac_flags = p->vlan_mac_flags;
	int rc;

	/* If list is empty - there is nothing to do here */
	if (d_list_is_empty(&o->head)) {
		*ppos = NULL;
		return 0;
	}

	/* make a step... */
	if (*ppos == NULL)
		*ppos = (struct ecore_list_elem *)d_list_peek_head(&o->head);
	else
		*ppos = (struct ecore_list_elem *)d_list_next_entry(&((*ppos)->link));

	pos = *ppos;

	/* If it's the last step - return NULL */
	if (ecore_list_is_last(&pos->link, &o->head))
		*ppos = NULL;

	/* Set 'pending' state */
	raw->set_pending(raw);

	/* Take an element data */
	memcpy(&p->data, &pos->data, sizeof(p->data));

	/* Restore the data related flags */
	ecore_set_vlan_mac_data_flags(&pos->vlan_mac_flags, &p->vlan_mac_flags);

	/* Configure the new classification in the chip */
	rc = o->config_rule(pdev, p, pos, TRUE);
	if (rc)
		raw->clear_pending(raw);

	else if (ECORE_TEST_BIT(RAMROD_COMP_WAIT, &p->ramrod_flags))
		/** Wait for a ramrod completion if was requested.
		 *
		 *  If there was a timeout we don't want to clear a pending
		 *  state.
		 */
		rc = raw->wait_comp(pdev, raw);

	p->vlan_mac_flags = orig_vlan_mac_flags;
	return rc;
}

/**
 *
 * @param pdev
 * @param p
 *
 * @return 0 in case of success or negative value in case of
 *  	   failure: ECORE_EXISTS - if the requested MAC has already
 *  	   been configured, ECORE_INVAL if credit consumption has
 *  	   been requested and there was no credit avaliable for
 *  	   this operation, ECORE_NOMEM if memory allocation failed,
 *  	   ECORE_TIMEOUT if there was a timeout waiting for a ramrod
 *  	   completion. If ecore_sp_post() failed, it's exit
 *  	   status would be returned
 *
 */
static INLINE int ecore_vlan_mac_add(struct _lm_device_t *pdev,
				 struct ecore_vlan_mac_ramrod_params *p)
{
	struct ecore_list_elem *pos;
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;
	struct ecore_raw_obj *raw = &p->vlan_mac_obj->raw;
	int rc;
	u8 cam_offset = 0;

	/* If this classification can not be added (is already set)
	 * - return an error.
	 */
	if (!o->check_add(p))
		return ECORE_EXISTS;

	/* Consume the credit if not requested not to */
	if (!(ECORE_TEST_BIT(ECORE_DONT_CONSUME_CAM_CREDIT,
			&p->vlan_mac_flags) || o->get_credit(o, &cam_offset)))
		return ECORE_INVAL;

	/* Add a new list entry */
	pos = ECORE_ZALLOC(pdev, sizeof(*pos));
	if (!pos) {
		ECORE_ERR("Failed to allocate memory for a new list entry\n");
		rc = ECORE_NOMEM;
		goto error_exit3;
	}

	/* Set 'pending' state */
	raw->set_pending(raw);

	/* Copy vlan_mac_flags to the list_entry */
	pos->vlan_mac_flags = p->vlan_mac_flags;

	/* Save cam_offset */
	pos->cam_offset = cam_offset;

	/* Configure the new classification in the chip */
	rc = o->config_rule(pdev, p, pos, TRUE);
	if (rc)
		goto error_exit1;

	/* Wait for a ramrod completion if was requested */
	if (ECORE_TEST_BIT(RAMROD_COMP_WAIT, &p->ramrod_flags)) {
		rc = raw->wait_comp(pdev, raw);
		if (rc)
			/** If there was a timeout we don't want to clear a
			 *  pending state but we also don't want to record this
			 *  operation as completed. Timeout should never happen.
			 *  If it does it means that there is something wrong
			 *  with either the FW/HW or the system. Both cases are
			 *  fatal.
			 */
			goto error_exit2;
	}

	/* Now when we are done record the operation as completed */
	d_list_push_tail( &o->head, &pos->link);

	return 0;

error_exit1:
	raw->clear_pending(raw);
error_exit2:
	ECORE_FREE(pdev, pos, sizeof(*pos));
error_exit3:
	/* Roll back a credit change */
	if (!ECORE_TEST_BIT(ECORE_DONT_CONSUME_CAM_CREDIT, &p->vlan_mac_flags))
		o->put_credit(o, cam_offset);

	return rc;
}

/**
 *
 *
 * @param pdev
 * @param p
 *
 * @return 0 in case of success or negative value in case of
 *  	   failure: ECORE_EXISTS - if the requested MAC has already
 *  	   been configured, ECORE_INVAL if credit consumption has
 *  	   been requested and there was no credit avaliable for
 *  	   this operation, ECORE_NOMEM if memory allocation failed,
 *  	   ECORE_TIMEOUT if there was a timeout waiting for a ramrod
 *  	   completion. If ecore_sp_post() failed, it's exit
 *  	   status would be returned.
 */
static INLINE int ecore_vlan_mac_del(struct _lm_device_t *pdev,
					 struct ecore_vlan_mac_ramrod_params *p)
{
	struct ecore_list_elem *pos = NULL;
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;
	struct ecore_raw_obj *raw = &p->vlan_mac_obj->raw;
	int rc;

	/* If this classification can not be delete (doesn't exist)
	 * - return a SUCCESS.
	 */
	pos = o->check_del(p);
	if (!pos)
		return ECORE_EXISTS;

	/* Return the credit to the credit pool if not requested not to */
	if (!(ECORE_TEST_BIT(ECORE_DONT_CONSUME_CAM_CREDIT,
			   &p->vlan_mac_flags) || o->put_credit(o, pos->cam_offset)))
			return ECORE_INVAL;

	if (ECORE_TEST_BIT(RAMROD_DRV_CLR_ONLY, &p->ramrod_flags))
		goto clr_only;

	/* Set 'pending' state */
	raw->set_pending(raw);

	/* Configure the new classification in the chip */
	rc = o->config_rule(pdev, p, pos, FALSE);
	if (rc)
		goto error_exit1;

	/* Wait for a ramrod completion if was requested */
	if (ECORE_TEST_BIT(RAMROD_COMP_WAIT, &p->ramrod_flags)) {
		rc = raw->wait_comp(pdev, raw);
		if (rc)
			/* See the same case comment in ecore_vlan_mac_add() */
			goto error_exit2;
	}

clr_only:
	/* Now when we are done we may delete the entry from our records */
	d_list_remove_entry(&o->head, &pos->link);
	ECORE_FREE(pdev, pos, sizeof(*pos));
	return 0;

error_exit1:
	raw->clear_pending(raw);
error_exit2:
	/* Roll back a credit change */
	if (!ECORE_TEST_BIT(ECORE_DONT_CONSUME_CAM_CREDIT, &p->vlan_mac_flags))
		o->get_credit(o, &pos->cam_offset);

	return rc;
}

int ecore_vlan_mac_move(struct _lm_device_t *pdev,
				 struct ecore_vlan_mac_ramrod_params *p,
				 struct ecore_vlan_mac_obj * dest_o)
{
	struct ecore_list_elem *pos = NULL;
	struct ecore_list_elem *pos_dest = NULL;
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;
	struct ecore_raw_obj *raw = &p->vlan_mac_obj->raw;
	BOOL check_add;
	u8 cam_offset;
	int rc;

	/* Check if we can delete from the first object and add to the
	 * second.
	 */
	pos = o->check_del(p);

	p->vlan_mac_obj = dest_o; /* override source to check if mac can be added */
	check_add = o->check_add(p);

	/* If this classification can not be added (is already set)
	 * AND can't be deleted - return a SUCCESS.
	 */
	if (!check_add || !pos)
		return ECORE_INVAL;

	/* bring back to original state. */
	p->vlan_mac_obj = o;

	/* Consume the credit if not requested not to */
	if (!(ECORE_TEST_BIT(ECORE_DONT_CONSUME_CAM_CREDIT_DEST,
			&p->vlan_mac_flags) || dest_o->get_credit(dest_o, &cam_offset)))
		return ECORE_INVAL;

	if (!(ECORE_TEST_BIT(ECORE_DONT_CONSUME_CAM_CREDIT,
			   &p->vlan_mac_flags) || o->put_credit(o, cam_offset)))
	{
		dest_o->put_credit(dest_o, cam_offset); /* return the credit taken from dest... */
		return ECORE_INVAL;
	}

	/* Add a new list entry */
	pos_dest = ECORE_ZALLOC(pdev, sizeof(*pos));
	if (!pos_dest) {
		ECORE_ERR("Failed to allocate memory for a new list entry\n");
		rc = -ECORE_NOMEM;
		goto error_exit3;
	}


	/* Set 'pending' state */
	raw->set_pending(raw);

	/* Copy vlan_mac_flags to the list_entry */
	pos->vlan_mac_flags = p->vlan_mac_flags;
	pos_dest->vlan_mac_flags = p->vlan_mac_flags;

	/* Configure the new classification in the chip */
	rc = o->config_move_rules(pdev, p, dest_o, pos_dest );
	if (rc)
		goto error_exit1;

	/* Wait for a ramrod completion if was requested */
	if (ECORE_TEST_BIT(RAMROD_COMP_WAIT, &p->ramrod_flags)) {
		rc = raw->wait_comp(pdev, raw);
		if (rc)
			/** If there was a timeout we don't want to clear a
			 *  pending state but we also don't want to record this
			 *  operation as completed. Timeout should never happen.
			 *  If it does it means that there is something wrong
			 *  with either the FW/HW or the system. Both cases are
			 *  fatal.
			 */
			goto error_exit2;
	}

	/* Now when we are done record the operation as completed */
	d_list_push_tail( &dest_o->head, &pos_dest->link);

	/* Now when we are done we may delete the entry from our records */
	d_list_remove_entry(&o->head, &pos->link);

	return 0;

error_exit1:
	raw->clear_pending(raw);
error_exit2:
	ECORE_FREE(pdev, pos_dest, sizeof(*pos_dest));
error_exit3:
	/* Roll back a credit change */
	if (!ECORE_TEST_BIT(ECORE_DONT_CONSUME_CAM_CREDIT, &p->vlan_mac_flags))
		o->get_credit(o, &cam_offset);
	if (!ECORE_TEST_BIT(ECORE_DONT_CONSUME_CAM_CREDIT, &p->vlan_mac_flags))
		dest_o->put_credit(dest_o, cam_offset);
	return rc;
}

/**
 * Delete the next configured element.
 *
 * @param pdev
 * @param p Command parameters (same as for a regular DELETE command).
 *
 * @return 0 if the last operation has completed successfully and there are no
 *  	   more elements left, positive value if the last operation has
 *  	   completed successfully and there are more previously configured
 *  	   elements, negative value is current operation has failed.
 */
static int ecore_vlan_mac_del_next(struct _lm_device_t *pdev,
				   struct ecore_vlan_mac_ramrod_params *p)
{
	struct ecore_list_elem *pos;
	struct ecore_vlan_mac_obj *o = p->vlan_mac_obj;
	int rc = 0;
	unsigned long orig_vlan_mac_flags = p->vlan_mac_flags;

	/* There are no configured elements already */
	if (d_list_is_empty(&o->head)) {
	//  DP(NETIF_MSG_IFDOWN, "Hmmm, u shouldn't get here! "
	//  			 "Extra beat maybe? Check your flow.\n");
		return 0;
	}

	/* Get the first configured element */
	pos = ( struct ecore_list_elem *)d_list_peek_head(&o->head);

	/* Set the data */
	memcpy(&p->data, &pos->data, sizeof(p->data));

	/* Restore the data related flags */
	ecore_set_vlan_mac_data_flags(&pos->vlan_mac_flags, &p->vlan_mac_flags);

	rc = ecore_vlan_mac_del(pdev, p);
	if (rc)
		goto done;

	/* If we are here, means that the current operation has successfully
	 * completed.
	 */
	if (d_list_is_empty(&o->head))
		/* We are done */
		rc = 0;
	else
		/* Come again... */
		rc = ECORE_PENDING;

done:
	p->vlan_mac_flags = orig_vlan_mac_flags;
	return rc;
}

int ecore_config_vlan_mac(struct _lm_device_t *pdev,
			  struct ecore_vlan_mac_ramrod_params *p, BOOL add)
{
	if (add)
		return ecore_vlan_mac_add(pdev, p);
	else
		return ecore_vlan_mac_del(pdev, p);
}


static INLINE void ecore_init_raw_obj(struct ecore_raw_obj *raw, u8 cl_id,
	u32 cid, u8 func_id, void *rdata, lm_address_t rdata_mapping, int state,
	unsigned long *pstate, ecore_obj_type type)
{
	raw->func_id = func_id;
	raw->cid = cid;
	raw->cl_id = cl_id;
	raw->rdata = rdata;
	raw->rdata_mapping = rdata_mapping;
	raw->state = state;
	raw->pstate = pstate;
	raw->obj_type = type;
	raw->check_pending = ecore_raw_check_pending;
	raw->clear_pending = ecore_raw_clear_pending;
	raw->set_pending = ecore_raw_set_pending;
	raw->wait_comp = ecore_raw_wait;
}

static INLINE void ecore_init_vlan_mac_common(struct ecore_vlan_mac_obj *o,
			u8 cl_id, u32 cid, u8 func_id, void *rdata,
			lm_address_t rdata_mapping, int state,
			unsigned long *pstate, ecore_obj_type type,
			struct ecore_credit_pool_obj *macs_pool,
			struct ecore_credit_pool_obj *vlans_pool)
{
	d_list_clear(&o->head);

	o->macs_pool = macs_pool;
	o->vlans_pool = vlans_pool;

	o->del_next = ecore_vlan_mac_del_next;
	o->restore = ecore_vlan_mac_restore;

	ecore_init_raw_obj(&o->raw, cl_id, cid, func_id, rdata, rdata_mapping,
			   state, pstate, type);
}


void ecore_init_mac_obj(struct _lm_device_t *pdev,
			struct ecore_vlan_mac_obj *mac_obj,
			u8 cl_id, u32 cid, u8 func_id, void *rdata,
			lm_address_t rdata_mapping, int state,
			unsigned long *pstate, ecore_obj_type type,
			struct ecore_credit_pool_obj *macs_pool)
{
	ecore_init_vlan_mac_common(mac_obj, cl_id, cid, func_id, rdata,
				   rdata_mapping, state, pstate, type, macs_pool,
				   NULL);

	mac_obj->get_credit = ecore_get_credit_mac;
	mac_obj->put_credit = ecore_put_credit_mac;

	if (CHIP_IS_E2E3(pdev)) {
		mac_obj->config_rule = ecore_setup_mac_e2;
		mac_obj->config_move_rules = ecore_setup_move_mac_e2;
		mac_obj->check_del = ecore_check_mac_del;
		mac_obj->check_add = ecore_check_mac_add;
	} else if (CHIP_IS_E1(pdev)) {
		mac_obj->config_rule = ecore_setup_mac_e1;
		mac_obj->config_move_rules = ecore_setup_move_e1x_always_err;
		mac_obj->check_del = ecore_check_mac_del;
		mac_obj->check_add = ecore_check_mac_add;
	} else if (CHIP_IS_E1H(pdev)) {
		mac_obj->config_rule = ecore_setup_mac_e1h;
		mac_obj->config_move_rules = ecore_setup_move_e1x_always_err;
		mac_obj->check_del = ecore_check_mac_del;
		mac_obj->check_add = ecore_check_mac_add;
	} else {
		ECORE_ERR("Do not support chips others than E2\n");
		BUG();
	}
}

void ecore_init_vlan_obj(struct _lm_device_t *pdev,
			 struct ecore_vlan_mac_obj *vlan_obj,
			 u8 cl_id, u32 cid, u8 func_id, void *rdata,
			 lm_address_t rdata_mapping, int state,
			 unsigned long *pstate, ecore_obj_type type,
			 struct ecore_credit_pool_obj *vlans_pool)
{
	ecore_init_vlan_mac_common(vlan_obj, cl_id, cid, func_id, rdata,
				   rdata_mapping, state, pstate, type, NULL,
				   vlans_pool);

	vlan_obj->get_credit = ecore_get_credit_vlan;
	vlan_obj->put_credit = ecore_put_credit_vlan;

	if (CHIP_IS_E2E3(pdev)) {
		vlan_obj->config_rule = ecore_setup_vlan_e2;
		vlan_obj->config_move_rules = ecore_setup_move_vlan_e2;
		vlan_obj->check_del = ecore_check_vlan_del;
		vlan_obj->check_add = ecore_check_vlan_add;
	} else if (CHIP_IS_E1(pdev)) {
		vlan_obj->config_rule = ecore_setup_vlan_e1x;
		vlan_obj->config_move_rules = ecore_setup_move_e1x_always_err;
		vlan_obj->check_del = ecore_check_vlan_del;
		vlan_obj->check_add = ecore_check_vlan_add;
	} else if (CHIP_IS_E1H(pdev)) {
		vlan_obj->config_rule = ecore_setup_vlan_e1x;
		vlan_obj->config_move_rules = ecore_setup_move_e1x_always_err;
		vlan_obj->check_del = ecore_check_vlan_del;
		vlan_obj->check_add = ecore_check_vlan_add;
	} else {
		ECORE_ERR("Do not support chips others than E1X and E2\n");
		BUG();
	}
}

void ecore_init_vlan_mac_obj(struct _lm_device_t *pdev,
				 struct ecore_vlan_mac_obj *vlan_mac_obj,
				 u8 cl_id, u32 cid, u8 func_id, void *rdata,
				 lm_address_t rdata_mapping, int state,
				 unsigned long *pstate, ecore_obj_type type,
				 struct ecore_credit_pool_obj *macs_pool,
				 struct ecore_credit_pool_obj *vlans_pool)
{
	ecore_init_vlan_mac_common(vlan_mac_obj, cl_id, cid, func_id, rdata,
				   rdata_mapping, state, pstate, type, macs_pool,
				   vlans_pool);

	vlan_mac_obj->get_credit = ecore_get_credit_vlan_mac;
	vlan_mac_obj->put_credit = ecore_put_credit_vlan_mac;

	if (CHIP_IS_E2E3(pdev)) {
		vlan_mac_obj->config_rule = ecore_setup_vlan_mac_e2;
		vlan_mac_obj->config_move_rules = ecore_setup_move_vlan_mac_e2;
		vlan_mac_obj->check_del = ecore_check_vlan_mac_del;
		vlan_mac_obj->check_add = ecore_check_vlan_mac_add;
	} else if (CHIP_IS_E1H(pdev)) {
		vlan_mac_obj->config_rule = ecore_setup_vlan_mac_e1h;
		vlan_mac_obj->config_move_rules = ecore_setup_move_e1x_always_err;
		vlan_mac_obj->check_del = ecore_check_vlan_mac_del;
		vlan_mac_obj->check_add = ecore_check_vlan_mac_add;
	}
	else {
		ECORE_ERR("Do not support chips others than E2\n");
		BUG();
	}
}

/* RX_MODE verbs: DROP_ALL/ACCEPT_ALL/ACCEPT_ALL_MULTI/ACCEPT_ALL_VLAN/NORMAL */
static INLINE void __storm_memset_mac_filters(struct _lm_device_t *pdev,
			struct tstorm_eth_mac_filter_config *mac_filters,
			u16 pf_id)
{
	size_t size = sizeof(struct tstorm_eth_mac_filter_config);

	u32 addr = BAR_TSTRORM_INTMEM +
			TSTORM_MAC_FILTER_CONFIG_OFFSET(pf_id);

	__storm_memset_struct(pdev, addr, size, (u32*)mac_filters);
}

static int ecore_set_rx_mode_e1x(struct _lm_device_t *pdev,
				 struct ecore_rx_mode_ramrod_params *p)
{
	/* update the pdev MAC filter structure  */
	u32 mask = (1 << p->cl_id);

	struct tstorm_eth_mac_filter_config *mac_filters =
		(struct tstorm_eth_mac_filter_config *)p->rdata;

	/* initial seeting is drop-all */
	u8 drop_all_ucast = 1, drop_all_mcast = 1;
	u8 accp_all_ucast = 0, accp_all_bcast = 0, accp_all_mcast = 0;
	u8 unmatched_unicast = 0;

    /* In e1x there we only take into account rx acceot flag since tx switching
     * isn't enabled. */
	if (ECORE_TEST_BIT(ECORE_ACCEPT_UNICAST, &p->rx_accept_flags))
		/* accept matched ucast */
		drop_all_ucast = 0;

	if (ECORE_TEST_BIT(ECORE_ACCEPT_MULTICAST, &p->rx_accept_flags))
		/* accept matched mcast */
		drop_all_mcast = 0;

	if (ECORE_TEST_BIT(ECORE_ACCEPT_ALL_UNICAST, &p->rx_accept_flags)) {
		/* accept all mcast */
		drop_all_ucast = 0;
		accp_all_ucast = 1;
	}
	if (ECORE_TEST_BIT(ECORE_ACCEPT_ALL_MULTICAST, &p->rx_accept_flags)) {
		/* accept all mcast */
		drop_all_mcast = 0;
		accp_all_mcast = 1;
	}
	if (ECORE_TEST_BIT(ECORE_ACCEPT_BROADCAST, &p->rx_accept_flags)) {
		/* accept (all) bcast */
		accp_all_bcast = 1;
	}
	if (ECORE_TEST_BIT(ECORE_ACCEPT_UNMATCHED, &p->rx_accept_flags))
		/* accept unmatched unicasts */
		unmatched_unicast = 1;

	mac_filters->ucast_drop_all = drop_all_ucast ?
		mac_filters->ucast_drop_all | mask :
		mac_filters->ucast_drop_all & ~mask;

	mac_filters->mcast_drop_all = drop_all_mcast ?
		mac_filters->mcast_drop_all | mask :
		mac_filters->mcast_drop_all & ~mask;

	mac_filters->ucast_accept_all = accp_all_ucast ?
		mac_filters->ucast_accept_all | mask :
		mac_filters->ucast_accept_all & ~mask;

	mac_filters->mcast_accept_all = accp_all_mcast ?
		mac_filters->mcast_accept_all | mask :
		mac_filters->mcast_accept_all & ~mask;

	mac_filters->bcast_accept_all = accp_all_bcast ?
		mac_filters->bcast_accept_all | mask :
		mac_filters->bcast_accept_all & ~mask;

	mac_filters->unmatched_unicast = unmatched_unicast ?
		mac_filters->unmatched_unicast | mask :
		mac_filters->unmatched_unicast & ~mask;

	//DP(NETIF_MSG_IFUP, "drop_ucast 0x%x\ndrop_mcast 0x%x\ndrop_bcast 0x%x\n"
	//  "accp_ucast 0x%x\naccp_mcast 0x%x\naccp_bcast 0x%x\n",
	//  mac_filters->ucast_drop_all,
	//  mac_filters->mcast_drop_all,
	//  mac_filters->bcast_drop_all,
	//  mac_filters->ucast_accept_all,
	//  mac_filters->mcast_accept_all,
	//  mac_filters->bcast_accept_all
	//);

	/* write the MAC filter structure*/
	__storm_memset_mac_filters(pdev, mac_filters, p->func_id);

	/* The operation is completed */
	ECORE_CLEAR_BIT(p->state, p->pstate);
	smp_mb__after_clear_bit();

	return 0;
}

/* Setup ramrod data */
static INLINE void ecore_rx_mode_set_rdata_hdr_e2(u32 cid,
				struct eth_classify_header *hdr,
				u8 rule_cnt)
{
	hdr->echo = cid;
	hdr->rule_cnt = rule_cnt;
}

static INLINE void ecore_rx_mode_set_cmd_state_e2(struct _lm_device_t *pdev,
				unsigned long accept_flags,
				struct eth_filter_rules_cmd *cmd,
				BOOL clear_accept_all)
{
	u16 state;

	/* start with 'drop-all' */
	state = ETH_FILTER_RULES_CMD_UCAST_DROP_ALL |
		ETH_FILTER_RULES_CMD_MCAST_DROP_ALL;

	if (accept_flags) {
		if (ECORE_TEST_BIT(ECORE_ACCEPT_UNICAST, &accept_flags))
			state &= ~ETH_FILTER_RULES_CMD_UCAST_DROP_ALL;

		if (ECORE_TEST_BIT(ECORE_ACCEPT_MULTICAST, &accept_flags))
			state &= ~ETH_FILTER_RULES_CMD_MCAST_DROP_ALL;

		if (ECORE_TEST_BIT(ECORE_ACCEPT_ALL_UNICAST, &accept_flags)) {
			state &= ~ETH_FILTER_RULES_CMD_UCAST_DROP_ALL;
			state |= ETH_FILTER_RULES_CMD_UCAST_ACCEPT_ALL;
		}

		if (ECORE_TEST_BIT(ECORE_ACCEPT_ALL_MULTICAST, &accept_flags)) {
			state |= ETH_FILTER_RULES_CMD_MCAST_ACCEPT_ALL;
			state &= ~ETH_FILTER_RULES_CMD_MCAST_DROP_ALL;
		}
		if (ECORE_TEST_BIT(ECORE_ACCEPT_BROADCAST, &accept_flags))
			state |= ETH_FILTER_RULES_CMD_BCAST_ACCEPT_ALL;

		if (ECORE_TEST_BIT(ECORE_ACCEPT_UNMATCHED, &accept_flags)) {
			state &= ~ETH_FILTER_RULES_CMD_UCAST_DROP_ALL;
			state |= ETH_FILTER_RULES_CMD_UCAST_ACCEPT_UNMATCHED;
		}
		if (ECORE_TEST_BIT(ECORE_ACCEPT_ANY_VLAN, &accept_flags))
			state |= ETH_FILTER_RULES_CMD_ACCEPT_ANY_VLAN;
	}

	/* Clear ACCEPT_ALL_XXX flags for FCoE L2 Queue */
	if (clear_accept_all) {
		state &= ~ETH_FILTER_RULES_CMD_MCAST_ACCEPT_ALL;
		state &= ~ETH_FILTER_RULES_CMD_BCAST_ACCEPT_ALL;
		state &= ~ETH_FILTER_RULES_CMD_UCAST_ACCEPT_ALL;
		state &= ~ETH_FILTER_RULES_CMD_UCAST_ACCEPT_UNMATCHED;
	}

	cmd->state = mm_cpu_to_le16(state);

}

static int ecore_set_rx_mode_e2(struct _lm_device_t *pdev,
				struct ecore_rx_mode_ramrod_params *p)
{
	struct eth_filter_rules_ramrod_data *data = p->rdata;
	int rc;
	int rule_idx = 0;

	/* Reset the ramrod data buffer */
	memset(data, 0, sizeof(*data));

	/* Setup ramrod data */

	/* Rx or/and Tx (internal switching) ? */
	if (ECORE_TEST_BIT(RAMROD_TX, &p->ramrod_flags)) {
		data->rules[rule_idx].client_id = p->cl_id;
		data->rules[rule_idx].func_id = p->func_id;

		data->rules[rule_idx].cmd_general_data =
			ETH_FILTER_RULES_CMD_TX_CMD;

		ecore_rx_mode_set_cmd_state_e2(pdev, p->tx_accept_flags,
			&(data->rules[rule_idx++]), FALSE);
	}

	if (ECORE_TEST_BIT(RAMROD_RX, &p->ramrod_flags)) {
		data->rules[rule_idx].client_id = p->cl_id;
		data->rules[rule_idx].func_id = p->func_id;

		data->rules[rule_idx].cmd_general_data =
			ETH_FILTER_RULES_CMD_RX_CMD;

		ecore_rx_mode_set_cmd_state_e2(pdev, p->rx_accept_flags,
			&(data->rules[rule_idx++]), FALSE);
	}


	/* If FCoE Client configuration has been requested */
	if (ECORE_TEST_BIT(ECORE_RX_MODE_FCOE_ETH, &p->rx_mode_flags)) {
		data->rules[rule_idx].client_id = FCOE_CID(pdev);
		data->rules[rule_idx].func_id = p->func_id;

		/* Rx or Tx (internal switching) ? */
		if (ECORE_TEST_BIT(RAMROD_TX, &p->ramrod_flags))
			data->rules[rule_idx].cmd_general_data =
				ETH_FILTER_RULES_CMD_TX_CMD;

		if (ECORE_TEST_BIT(RAMROD_RX, &p->ramrod_flags))
			data->rules[rule_idx].cmd_general_data |=
				ETH_FILTER_RULES_CMD_RX_CMD;

		ecore_rx_mode_set_cmd_state_e2(pdev, p->rx_accept_flags,
			&(data->rules[rule_idx++]), TRUE);
	}

	ecore_rx_mode_set_rdata_hdr_e2(p->cid, &data->header, (u8)rule_idx);

	//DP(NETIF_MSG_IFUP, "About to configure %d rules, "
	//  	   "accept_flags 0x%lx, state[0] 0x%x "
	//  	   "cmd_general_data[0] 0x%x\n",
	//   data->header.rule_cnt, p->accept_flags,
	//   data->rules[0].state, data->rules[0].cmd_general_data);

	/* Commit writes towards the memory before sending a ramrod */
	mb();

	/* Send a ramrod */
	rc = ecore_sp_post(pdev,
					 RAMROD_CMD_ID_ETH_FILTER_RULES,
					 p->cid,
					 p->rdata_mapping,
					 ETH_CONNECTION_TYPE);
	if (rc)
		return rc;


	/* Ramrod completion is pending */
	return ECORE_PENDING;
}

static int ecore_wait_rx_mode_comp_e2(struct _lm_device_t *pdev,
					  struct ecore_rx_mode_ramrod_params *p)
{
	return ecore_state_wait(pdev, p->state, p->pstate);
}

static int ecore_empty_rx_mode_wait(struct _lm_device_t *pdev,
					struct ecore_rx_mode_ramrod_params *p)
{
	/* Do nothing */
	return 0;
}

int ecore_config_rx_mode(struct _lm_device_t *pdev,
			 struct ecore_rx_mode_ramrod_params *p)
{
	int rc;

	/* Configure the new classification in the chip */
	rc = p->rx_mode_obj->config_rx_mode(pdev, p);
	if (rc < 0)
		return rc;

	/* Wait for a ramrod completion if was requested */
	if (ECORE_TEST_BIT(RAMROD_COMP_WAIT, &p->ramrod_flags)) {
		rc = p->rx_mode_obj->wait_comp(pdev, p);
		if (rc)
			return rc;
	}

	return rc;
}

void ecore_init_rx_mode_obj(struct _lm_device_t *pdev, struct ecore_rx_mode_obj *o)
{
	if (CHIP_IS_E2E3(pdev)) {
		o->wait_comp = ecore_wait_rx_mode_comp_e2;
		o->config_rx_mode = ecore_set_rx_mode_e2;
	} else if (CHIP_IS_E1x(pdev)) {
		o->wait_comp = ecore_empty_rx_mode_wait;
		o->config_rx_mode = ecore_set_rx_mode_e1x;
	} else {
		ECORE_ERR("Do not support chips others than E1x and E2\n");
		BUG();
	}
}

/********************* Multicast verbs: SET, CLEAR ****************************/
#ifndef ECORE_ERASE
u32_t calc_crc32( u8_t* crc32_packet, u32_t crc32_length, u32_t crc32_seed, u8_t complement) ;
#endif

static INLINE u8 ecore_mcast_bin_from_mac(u8 *mac)
{
#ifndef ECORE_ERASE
	u32_t crc32 = 0, packet_buf[2] = {0};

	memcpy(((u8_t *)(&packet_buf[0]))+2, &mac[0], 2);
	memcpy(&packet_buf[1], &mac[2], 4);
	crc32 = calc_crc32((u8_t*)packet_buf, 8, 0 /*seed*/, 0 /*complement*/);

	return crc32 & 0xff;
#else

	return (crc32c_le(0, mac, ETH_ALEN) >> 24) & 0xff;
#endif
}

struct ecore_mcast_mac_elem {
	d_list_entry_t link;
	u8 mac[ETH_ALEN];
	u8 pad[2]; /* For a natural alignment of the following buffer */
};

struct ecore_pending_mcast_cmd {
	d_list_entry_t link;
	int type; /* ECORE_MCAST_CMD_X */
	union {
		d_list_t macs_head;
		u32 macs_num; /* Needed for DEL command */
		int next_bin; /* Needed for RESTORE flow with aprox match */
	} data;

	BOOL done; /* set to TRUE, when the command has been handled,
			* practically used in 57712 handling only, where one pending
			* command may be handled in a few operations. As long as for
			* other chips every operation handling is completed in a
			* single ramrod, there is no need to utilize this field.
			*/
#ifndef ECORE_ERASE
	u32 alloc_len; /* passed to ECORE_FREE */
#endif
};

static int ecore_mcast_wait(struct _lm_device_t *pdev, struct ecore_mcast_obj *o)
{
	if (ecore_state_wait(pdev, o->sched_state, o->raw.pstate) ||
			o->raw.wait_comp(pdev,&o->raw))
		return ECORE_TIMEOUT;

	return 0;
}

static int ecore_mcast_enqueue_cmd(struct _lm_device_t *pdev,
				   struct ecore_mcast_obj *o,
				   struct ecore_mcast_ramrod_params *p,
				   int cmd)
{
	int total_sz;
	struct ecore_pending_mcast_cmd *new_cmd;
	struct ecore_mcast_mac_elem *cur_mac = NULL;
	struct ecore_mcast_list_elem *pos;
	int macs_list_len = ((cmd == ECORE_MCAST_CMD_ADD) ?
				 p->mcast_list_len : 0);

	/* If the command is empty ("handle pending commands only"), break */
	if (!p->mcast_list_len)
		return 0;

	total_sz = sizeof(*new_cmd) +
		macs_list_len * sizeof(struct ecore_mcast_mac_elem);

	/* Add mcast is called under spin_lock, thus calling with GFP_ATOMIC */
	new_cmd = ECORE_ZALLOC(pdev, total_sz);

	if (!new_cmd)
		return ECORE_NOMEM;

	//DP(NETIF_MSG_IFUP, "About to enqueue a new %d command. "
	//  	 "macs_list_len=%d\n", cmd, macs_list_len);

	d_list_clear(&new_cmd->data.macs_head);

	new_cmd->type = cmd;
	new_cmd->done = FALSE;
#ifndef ECORE_ERASE
	new_cmd->alloc_len = total_sz;
#endif

	switch (cmd) {
	case ECORE_MCAST_CMD_ADD:
		cur_mac = (struct ecore_mcast_mac_elem *)
			((u8*)new_cmd + sizeof(*new_cmd));

		/* Push the MACs of the current command into the pendig command
		 * MACs list: FIFO
		 */
		ecore_list_for_each_entry(pos, &p->mcast_list, link, struct ecore_mcast_list_elem) {
			memcpy(cur_mac->mac, pos->mac, ETH_ALEN);
			d_list_push_tail(&new_cmd->data.macs_head, &cur_mac->link);
			cur_mac++;
		}

		break;

	case ECORE_MCAST_CMD_DEL:
		new_cmd->data.macs_num = p->mcast_list_len;
		break;

	case ECORE_MCAST_CMD_RESTORE:
		new_cmd->data.next_bin = 0;
		break;

	default:
		ECORE_ERR1("Unknown command: %d\n", cmd);
		return ECORE_INVAL;
	}

	/* Push the new pending command to the tail of the pending list: FIFO */
	d_list_push_tail(&o->pending_cmds_head, &new_cmd->link);

	o->set_sched(o);

	return ECORE_PENDING;
}

/**
 * Get the next set bin (index).
 *
 * @param o
 * @param last index to start looking from (including).
 *
 * @return The next found (set) bin or a negative value if none is found.
 */
static INLINE int ecore_mcast_get_next_bin(struct ecore_mcast_obj *o, int last)
{
	int i, j, inner_start = last % BIT_VEC64_ELEM_SZ;

	for (i = last / BIT_VEC64_ELEM_SZ; i < ECORE_MCAST_VEC_SZ; i++) {
		if (o->registry.aprox_match.vec[i])
			for (j = inner_start; j < BIT_VEC64_ELEM_SZ; j++) {
				int cur_bit = j + BIT_VEC64_ELEM_SZ * i;
				if (BIT_VEC64_TEST_BIT(o->registry.aprox_match.
							   vec, cur_bit)) {
					return cur_bit;
				}
			}
		inner_start = 0;
	}

	/* None found */
	return -1;
}

/**
 * Finds the first set bin and clears it.
 *
 * @param o
 *
 * @return The index of the found bin or -1 if none is found
 */
static INLINE int ecore_mcast_clear_first_bin(struct ecore_mcast_obj *o)
{
	int cur_bit = ecore_mcast_get_next_bin(o, 0);

	if (cur_bit >= 0)
		BIT_VEC64_CLEAR_BIT(o->registry.aprox_match.vec, cur_bit);

	return cur_bit;
}

static INLINE u8 ecore_mcast_get_rx_tx_flag(struct ecore_mcast_obj *o)
{
	struct ecore_raw_obj *raw = &o->raw;
	u8 rx_tx_flag = 0;

	if ((raw->obj_type == ECORE_OBJ_TYPE_TX) ||
		(raw->obj_type == ECORE_OBJ_TYPE_RX_TX))
		rx_tx_flag |= ETH_MULTICAST_RULES_CMD_TX_CMD;

	if ((raw->obj_type == ECORE_OBJ_TYPE_RX) ||
		(raw->obj_type == ECORE_OBJ_TYPE_RX_TX))
		rx_tx_flag |= ETH_MULTICAST_RULES_CMD_RX_CMD;

	return rx_tx_flag;
}

static void ecore_mcast_set_one_rule_e2(struct _lm_device_t *pdev,
					struct ecore_mcast_obj *o, int idx,
					union ecore_mcast_config_data *cfg_data,
					int cmd)
{
	struct ecore_raw_obj *r = &o->raw;
	struct eth_multicast_rules_ramrod_data *data =
		(struct eth_multicast_rules_ramrod_data *)(r->rdata);
	u8 func_id = r->func_id;
	u8 rx_tx_add_flag = ecore_mcast_get_rx_tx_flag(o);
	int bin;

	if ((cmd == ECORE_MCAST_CMD_ADD) || (cmd == ECORE_MCAST_CMD_RESTORE))
		rx_tx_add_flag |= ETH_MULTICAST_RULES_CMD_IS_ADD;

	data->rules[idx].cmd_general_data |= rx_tx_add_flag;

	/* Get a bin and update a bins' vector */
	switch (cmd) {
	case ECORE_MCAST_CMD_ADD:
		bin = ecore_mcast_bin_from_mac(cfg_data->mac);
		BIT_VEC64_SET_BIT(o->registry.aprox_match.vec, bin);
		break;

	case ECORE_MCAST_CMD_DEL:
		/* If there were no more bins to clear
		 * (ecore_mcast_clear_first_bin() returns -1) then we would
		 * clear any (0xff) bin.
		 * See ecore_mcast_preamble_e2() for explanation when it may
		 * happen.
		 */
		bin = ecore_mcast_clear_first_bin(o);
		break;

	case ECORE_MCAST_CMD_RESTORE:
		bin = cfg_data->bin;
		break;

	default:
		ECORE_ERR1("Unknown command: %d\n", cmd);
		return;
	}

	//DP(NETIF_MSG_IFUP, "%s bin %d\n",
	//   ((rx_tx_add_flag & ETH_MULTICAST_RULES_CMD_IS_ADD) ?
	//    "Setting"  : "Clearing"), bin);

	data->rules[idx].bin_id = (u8)bin;
	data->rules[idx].func_id = func_id;
	data->rules[idx].engine_id = o->engine_id;
}

/**
 * Resotre the configuration from the registry.
 *
 * @param pdev
 * @param o
 * @param start_bin index in the registry to start from (including)
 * @param rdata_idx index in the ramrod data to start from
 *
 * @return Last handled bin index or -1 if all bins have been handled
 */
static INLINE int ecore_mcast_handle_restore_cmd_e2(
	struct _lm_device_t *pdev, struct ecore_mcast_obj *o , int start_bin,
	int *rdata_idx)
{
	int cur_bin, cnt = *rdata_idx;
	union ecore_mcast_config_data cfg_data = {0};

	/* go through the registry and configure the bins from it */
	for (cur_bin = ecore_mcast_get_next_bin(o, start_bin); cur_bin >= 0;
		 cur_bin = ecore_mcast_get_next_bin(o, cur_bin + 1)) {

		cfg_data.bin = (u8)cur_bin;
		o->set_one_rule(pdev, o, cnt, &cfg_data,
				ECORE_MCAST_CMD_RESTORE);

		cnt++;

	//  DP(NETIF_MSG_IFUP, "About to configure a bin %d\n", cur_bin);

		/* Break if we reached the maximum number
		 * of rules.
		 */
		if (cnt >= o->max_cmd_len)
			break;
	}

	*rdata_idx = cnt;

	return cur_bin;
}

static INLINE void ecore_mcast_hdl_pending_add_e2(struct _lm_device_t *pdev,
	struct ecore_mcast_obj *o, struct ecore_pending_mcast_cmd *cmd_pos,
	int *line_idx)
{
	struct ecore_mcast_mac_elem *pmac_pos, *pmac_pos_n;
	int cnt = *line_idx;
	union ecore_mcast_config_data cfg_data = {0};

	ecore_list_for_each_entry_safe(pmac_pos, pmac_pos_n,
					&cmd_pos->data.macs_head, link, struct ecore_mcast_mac_elem) {

		cfg_data.mac = &pmac_pos->mac[0];
		o->set_one_rule(pdev, o, cnt, &cfg_data, cmd_pos->type);

		cnt++;

	//  DP(NETIF_MSG_IFUP, "About to configure "ECORE_MAC_FMT
	//     " mcast MAC\n", ECORE_MAC_PRN_LIST(pmac_pos->mac));

		d_list_remove_entry(&cmd_pos->data.macs_head, &pmac_pos->link);

		/* Break if we reached the maximum number
		 * of rules.
		 */
		if (cnt >= o->max_cmd_len)
			break;
	}

	*line_idx = cnt;

	/* if no more MACs to configure - we are done */
	if (d_list_is_empty(&cmd_pos->data.macs_head))
		cmd_pos->done = TRUE;
}

static INLINE void ecore_mcast_hdl_pending_del_e2(struct _lm_device_t *pdev,
	struct ecore_mcast_obj *o, struct ecore_pending_mcast_cmd *cmd_pos,
	int *line_idx)
{
	int cnt = *line_idx;

	while(cmd_pos->data.macs_num) {
		o->set_one_rule(pdev, o, cnt, NULL, cmd_pos->type);

		cnt++;

		cmd_pos->data.macs_num--;

	//  DP(NETIF_MSG_IFUP, "Deleting MAC. %d left,cnt is %d\n",
	//     cmd_pos->data.macs_num, cnt);

		/* Break if we reached the maximum
		 * number of rules.
		 */
		if (cnt >= o->max_cmd_len)
			break;
	}

	*line_idx = cnt;

	/* If we cleared all bins - we are done */
	if (!cmd_pos->data.macs_num)
		cmd_pos->done = TRUE;
}

static INLINE void ecore_mcast_hdl_pending_restore_e2(struct _lm_device_t *pdev,
	struct ecore_mcast_obj *o, struct ecore_pending_mcast_cmd *cmd_pos,
	int *line_idx)
{
	cmd_pos->data.next_bin = o->hdl_restore(pdev, o, cmd_pos->data.next_bin,
						line_idx);

	if (cmd_pos->data.next_bin < 0)
		/* If o->set_restore returned -1 we are done */
		cmd_pos->done = TRUE;
	else
		/* Start from the next bin next time */
		cmd_pos->data.next_bin++;
}

static INLINE int ecore_mcast_handle_pending_cmds_e2(struct _lm_device_t *pdev,
				struct ecore_mcast_ramrod_params *p)
{
	struct ecore_pending_mcast_cmd *cmd_pos, *cmd_pos_n;
	int cnt = 0;
	struct ecore_mcast_obj *o = p->mcast_obj;

	ecore_list_for_each_entry_safe(cmd_pos, cmd_pos_n, &o->pending_cmds_head,
				 link, struct ecore_pending_mcast_cmd) {
		switch (cmd_pos->type) {
		case ECORE_MCAST_CMD_ADD:
			ecore_mcast_hdl_pending_add_e2(pdev, o, cmd_pos, &cnt);
			break;

		case ECORE_MCAST_CMD_DEL:
			ecore_mcast_hdl_pending_del_e2(pdev, o, cmd_pos, &cnt);
			break;

		case ECORE_MCAST_CMD_RESTORE:
			ecore_mcast_hdl_pending_restore_e2(pdev, o, cmd_pos,
							   &cnt);
			break;

		default:
			ECORE_ERR1("Unknown command: %d\n", cmd_pos->type);
			return ECORE_INVAL;
		}

		/* If the command has been completed - remove it from the list
		 * and free the memory
		 */
		if (cmd_pos->done) {
			d_list_remove_entry(&o->pending_cmds_head, &cmd_pos->link);
			ECORE_FREE(pdev, cmd_pos, cmd_pos->alloc_len);
		}

		/* Break if we reached the maximum number of rules */
		if (cnt >= o->max_cmd_len)
			break;
	}

	return cnt;
}

static INLINE void ecore_mcast_hdl_add(struct _lm_device_t *pdev,
	struct ecore_mcast_obj *o, struct ecore_mcast_ramrod_params *p,
	int *line_idx)
{
	struct ecore_mcast_list_elem *mlist_pos;
	union ecore_mcast_config_data cfg_data = {0};
	int cnt = *line_idx;

		ecore_list_for_each_entry(mlist_pos, &p->mcast_list, link, struct ecore_mcast_list_elem) {
		cfg_data.mac = mlist_pos->mac;
		o->set_one_rule(pdev, o, cnt, &cfg_data, ECORE_MCAST_CMD_ADD);

		cnt++;

	//  DP(NETIF_MSG_IFUP, "About to configure "ECORE_MAC_FMT
	//     " mcast MAC\n", ECORE_MAC_PRN_LIST(mlist_pos->mac));
	}

	*line_idx = cnt;
}

static INLINE void ecore_mcast_hdl_del(struct _lm_device_t *pdev,
	struct ecore_mcast_obj *o, struct ecore_mcast_ramrod_params *p,
	int *line_idx)
{
	int cnt = *line_idx, i;

	for (i = 0; i < p->mcast_list_len; i++) {
		o->set_one_rule(pdev, o, cnt, NULL, ECORE_MCAST_CMD_DEL);

		cnt++;

	//  DP(NETIF_MSG_IFUP, "Deleting MAC. %d left\n",
	//     p->mcast_list_len - i - 1);
	}

	*line_idx = cnt;
}

/**
 * This function is called iff there is enough place for the current command
 * in the ramrod data.
 *
 * @param pdev
 * @param p
 * @param cmd
 * @param start_cnt first line in the ramrod data that may be used by the
 *  				current command.
 *
 * @return Number of lines filled in the ramrod data in total.
 */
static INLINE int ecore_mcast_handle_current_cmd(struct _lm_device_t *pdev,
			struct ecore_mcast_ramrod_params *p, int cmd,
			int start_cnt)
{
	struct ecore_mcast_obj *o = p->mcast_obj;
	int cnt = start_cnt;

	//DP(NETIF_MSG_IFUP, "p->mcast_list_len=%d\n", p->mcast_list_len);

	switch (cmd) {
	case ECORE_MCAST_CMD_ADD:
		ecore_mcast_hdl_add(pdev, o, p, &cnt);
		break;

	case ECORE_MCAST_CMD_DEL:
		ecore_mcast_hdl_del(pdev, o, p, &cnt);
		break;

	case ECORE_MCAST_CMD_RESTORE:
		o->hdl_restore(pdev, o, 0, &cnt);
		break;

	default:
		ECORE_ERR1("Unknown command: %d\n", cmd);
		return ECORE_INVAL;
	}

	/* The current command has been handled */
	p->mcast_list_len = 0;

	return cnt;
}

static int ecore_mcast_preamble_e2(struct _lm_device_t *pdev,
				   struct ecore_mcast_ramrod_params *p,
				   int cmd)
{
	struct ecore_mcast_obj *o = p->mcast_obj;
	int reg_sz = o->get_registry_size(o);

	switch (cmd) {
	/* DEL command deletes all currently configured MACs */
	case ECORE_MCAST_CMD_DEL:
		o->set_registry_size(o, 0);
		/* Don't break */

	/* RESTORE command will restore the entire multicast configuration */
	case ECORE_MCAST_CMD_RESTORE:
		/* Here we set the approximate amount of work to do, which in
		 * fact may be only less as some MACs in postponed ADD
		 * command(s) scheduled before this command may fall into
		 * the same bin and the actual number of bins set in the
		 * registry would be less than we estimated here. See
		 * ecore_mcast_set_one_rule_e2() for further details.
		 */
		p->mcast_list_len = reg_sz;
		break;

	case ECORE_MCAST_CMD_ADD:
	case ECORE_MCAST_CMD_CONT:
		/* Here we assume that all new MACs will fall into new bins.
		 * However we will correct the real registry size after we
		 * handle all pending commands.
		 */
		o->set_registry_size(o, reg_sz + p->mcast_list_len);
		break;

	default:
		ECORE_ERR1("Unknown command: %d\n", cmd);
		return ECORE_INVAL;

	}

	/* Increase the total number of MACs pending to be configured */
	o->total_pending_num += p->mcast_list_len;

	return 0;
}

static void ecore_mcast_postmortem_e2(struct _lm_device_t *pdev,
					  struct ecore_mcast_ramrod_params *p,
					  int old_num_bins)
{
	struct ecore_mcast_obj *o = p->mcast_obj;

	o->set_registry_size(o, old_num_bins);
	o->total_pending_num -= p->mcast_list_len;
}

/**
 * Sets a header values in struct eth_multicast_rules_ramrod_data
 *
 * @param pdev
 * @param p
 * @param len number of rules to handle
 */
static INLINE void ecore_mcast_set_rdata_hdr_e2(struct _lm_device_t *pdev,
					struct ecore_mcast_ramrod_params *p,
					u8 len)
{
	struct ecore_raw_obj *r = &p->mcast_obj->raw;
	struct eth_multicast_rules_ramrod_data *data =
		(struct eth_multicast_rules_ramrod_data *)(r->rdata);

	data->header.echo = ((r->cid & ECORE_SWCID_MASK) |
			  (ECORE_FILTER_MCAST_PENDING << ECORE_SWCID_SHIFT));
	data->header.rule_cnt = len;
}

/**
 * Recalculate the actual number of set bins in the registry using
 * Brian Kernighan's algorithm: it's execution complexity is as a
 * number of set bins.
 *
 * @param pdev
 * @param o
 *
 * @return 0 - for the compliance with ecore_mcast_refresh_registry_e1().
 */
static INLINE int ecore_mcast_refresh_registry_e2(struct _lm_device_t *pdev,
					   struct ecore_mcast_obj *o)
{
	int i, cnt = 0;
	u64 elem;

	for (i = 0; i < ECORE_MCAST_VEC_SZ; i++) {
		elem = o->registry.aprox_match.vec[i];
		for (; elem; cnt++)
			elem &= elem - 1;
	}

	o->set_registry_size(o, cnt);

	return 0;
}

static int ecore_mcast_setup_e2(struct _lm_device_t *pdev,
				struct ecore_mcast_ramrod_params *p,
				int cmd)
{
	struct ecore_raw_obj *raw = &p->mcast_obj->raw;
	struct ecore_mcast_obj *o = p->mcast_obj;
	struct eth_multicast_rules_ramrod_data *data =
		(struct eth_multicast_rules_ramrod_data *)(raw->rdata);
	int cnt = 0, rc;

	/* Reset the ramrod data buffer */
	memset(data, 0, sizeof(*data));

	cnt = ecore_mcast_handle_pending_cmds_e2(pdev, p);

	/* If there are no more pending commands - clear SCHEDULED state */
	if (d_list_is_empty(&o->pending_cmds_head))
		o->clear_sched(o);

	/* The below may be TRUE iff there was enough room in ramrod
	 * data for all pending commands and for the current
	 * command. Otherwise the current command would have been added
	 * to the pending commands and p->mcast_list_len would have been
	 * zeroed.
	 */
	if (p->mcast_list_len > 0)
		cnt = ecore_mcast_handle_current_cmd(pdev, p, cmd, cnt);

	/* We've pulled out some MACs - update the total number of
	 * outstanding.
	 */
	o->total_pending_num -= cnt;

	/* send a ramrod */
	DbgBreakIf(o->total_pending_num < 0);
	DbgBreakIf(cnt > o->max_cmd_len);

	ecore_mcast_set_rdata_hdr_e2(pdev, p, (u8)cnt);

	/* Update a registry size if there are no more pending operations.
	 *
	 * We don't want to change the value of the registry size if there are
	 * pending operations because we want it to always be equal to the
	 * exact or the approximate number (see ecore_mcast_preamble_e2()) of
	 * set bins after the last requested operation in order to properly
	 * evaluate the size of the next DEL/RESTORE operation.
	 *
	 * Note that we update the registry itself during command(s) handling
	 * - see ecore_mcast_set_one_rule_e2(). That's because for 57712 we
	 * aggregate multiple commands (ADD/DEL/RESTORE) into one ramrod but
	 * with a limited amount of update commands (per MAC/bin) and we don't
	 * know in this scope what the actual state of bins configuration is
	 * going to be after this ramrod.
	 */
	if (!o->total_pending_num)
		ecore_mcast_refresh_registry_e2(pdev, o);

	/* Commit writes towards the memory before sending a ramrod */
	mb();

	/* If CLEAR_ONLY was requested - don't send a ramrod and clear
	 * RAMROD_PENDING status immediately.
	 */
	if (ECORE_TEST_BIT(RAMROD_DRV_CLR_ONLY, &p->ramrod_flags)) {
		raw->clear_pending(raw);
		return 0;
	} else {
		/* Send a ramrod */
		rc = ecore_sp_post( pdev,
							RAMROD_CMD_ID_ETH_MULTICAST_RULES,
							raw->cid,
							raw->rdata_mapping,
							ETH_CONNECTION_TYPE);
		if (rc)
			return rc;

		/* Ramrod completion is pending */
		return ECORE_PENDING;
	}
}

static int ecore_mcast_preamble_e1h(struct _lm_device_t *pdev,
					struct ecore_mcast_ramrod_params *p,
					int cmd)
{
	/* Mark, that there is a work to do */
	if ((cmd == ECORE_MCAST_CMD_DEL) || (cmd == ECORE_MCAST_CMD_RESTORE))
		p->mcast_list_len = 1;

	return 0;
}

static void ecore_mcast_postmortem_e1h(struct _lm_device_t *pdev,
					   struct ecore_mcast_ramrod_params *p,
					   int old_num_bins)
{
	/* Do nothing */
}

#define ECORE_57711_SET_MC_FILTER(filter, bit) \
	(filter)[(bit) >> 5] |= (1 << ((bit) & 0x1f))

static INLINE void ecore_mcast_hdl_add_e1h(struct _lm_device_t *pdev,
	struct ecore_mcast_obj *o, struct ecore_mcast_ramrod_params *p,
	u32 *mc_filter)
{
	struct ecore_mcast_list_elem *mlist_pos;
	int bit;

	ecore_list_for_each_entry(mlist_pos, &p->mcast_list, link, struct ecore_mcast_list_elem) {
		bit = ecore_mcast_bin_from_mac(mlist_pos->mac);
		ECORE_57711_SET_MC_FILTER(mc_filter, bit);

	//  DP(NETIF_MSG_IFUP, "About to configure "
	//     ECORE_MAC_FMT" mcast MAC, bin %d\n",
	//     ECORE_MAC_PRN_LIST(mlist_pos->mac), bit);

		/* bookkeeping... */
		BIT_VEC64_SET_BIT(o->registry.aprox_match.vec,
				  bit);
	}
}

static INLINE void ecore_mcast_hdl_restore_e1h(struct _lm_device_t *pdev,
	struct ecore_mcast_obj *o, struct ecore_mcast_ramrod_params *p,
	u32 *mc_filter)
{
	int bit;

	for (bit = ecore_mcast_get_next_bin(o, 0);
		  bit >= 0;
		  bit = ecore_mcast_get_next_bin(o, bit + 1)) {
		ECORE_57711_SET_MC_FILTER(mc_filter, bit);
	//  DP(NETIF_MSG_IFUP, "About to set bin %d\n",bit);
	}
}

/* On 57711 we write the multicast MACs' aproximate match
 * table by directly into the TSTORM's internal RAM. So we don't
 * really need to handle any tricks to make it work.
 */
static int ecore_mcast_setup_e1h(struct _lm_device_t *pdev,
				 struct ecore_mcast_ramrod_params *p,
				 int cmd)
{
	int i;
	struct ecore_mcast_obj *o = p->mcast_obj;
	struct ecore_raw_obj *r = &o->raw;

	/* If CLEAR_ONLY has been requested - clear the registry
	 * and clear a pending bit.
	 */
	if (!ECORE_TEST_BIT(RAMROD_DRV_CLR_ONLY, &p->ramrod_flags)) {
		u32 mc_filter[MC_HASH_SIZE] = {0};

		/* Set the multicast filter bits before writing it into
		 * the internal memory.
		 */
		switch (cmd) {
		case ECORE_MCAST_CMD_ADD:
			ecore_mcast_hdl_add_e1h(pdev, o, p, mc_filter);
			break;

		case ECORE_MCAST_CMD_DEL:
	//  	DP(NETIF_MSG_IFUP, "Invalidating multicast "
	//  			   "MACs configuration\n");

			/* clear the registry */
			memset(o->registry.aprox_match.vec, 0,
				   sizeof(o->registry.aprox_match.vec));
			break;

		case ECORE_MCAST_CMD_RESTORE:
			ecore_mcast_hdl_restore_e1h(pdev, o, p, mc_filter);
			break;

		default:
			ECORE_ERR1("Unknown command: %d\n", cmd);
			return ECORE_INVAL;
			}

		/* Set the mcast filter in the internal memory */
		for (i = 0; i < MC_HASH_SIZE; i++)
			REG_WR(pdev, MC_HASH_OFFSET(pdev, i), mc_filter[i]);
	} else
		/* clear the registry */
		memset(o->registry.aprox_match.vec, 0,
			   sizeof(o->registry.aprox_match.vec));

	/* We are done */
	r->clear_pending(r);

	return 0;
}

static int ecore_mcast_preamble_e1(struct _lm_device_t *pdev,
				   struct ecore_mcast_ramrod_params *p,
				   int cmd)
{
	struct ecore_mcast_obj *o = p->mcast_obj;
	int reg_sz = o->get_registry_size(o);

	switch (cmd) {
	/* DEL command deletes all currently configured MACs */
	case ECORE_MCAST_CMD_DEL:
		o->set_registry_size(o, 0);
		/* Don't break */

	/* RESTORE command will restore the entire multicast configuration */
	case ECORE_MCAST_CMD_RESTORE:
		p->mcast_list_len = reg_sz;
	//  DP(NETIF_MSG_IFUP, "Command %d, p->mcast_list_len=%d\n",
	//     cmd, p->mcast_list_len);
		break;

	case ECORE_MCAST_CMD_ADD:
	case ECORE_MCAST_CMD_CONT:
		/* Multicast MACs on 57710 are configured as unicast MACs and
		 * there is only a limited number of CAM entries for that
		 * matter.
		 */
		if (p->mcast_list_len > o->max_cmd_len) {
			ECORE_ERR1("Can't configure more than %d multicast MACs"
				  "on 57710\n", o->max_cmd_len);
			return ECORE_INVAL;
		}
		/* Every configured MAC should be cleared if DEL command is
		 * called. Only the last ADD command is relevant as long as
		 * every ADD commands overrides the previous configuration.
		 */
	//  DP(NETIF_MSG_IFUP, "p->mcast_list_len=%d\n", p->mcast_list_len);
		if (p->mcast_list_len > 0)
			o->set_registry_size(o, p->mcast_list_len);

		break;

	default:
		ECORE_ERR1("Unknown command: %d\n", cmd);
		return ECORE_INVAL;

	}

	/* We want to ensure that commands are executed one by one for 57710.
	 * Therefore each none-empty command will consume o->max_cmd_len.
	 */
	if (p->mcast_list_len)
		o->total_pending_num += o->max_cmd_len;

	return 0;
}

static void ecore_mcast_postmortem_e1(struct _lm_device_t *pdev,
					  struct ecore_mcast_ramrod_params *p,
					  int old_num_macs)
{
	struct ecore_mcast_obj *o = p->mcast_obj;

	o->set_registry_size(o, old_num_macs);

	/* If current command hasn't been handled yet and we are
	 * here means that it's meant to be dropped and we have to
	 * update the number of outstandling MACs accordingly.
	 */
	if (p->mcast_list_len)
		o->total_pending_num -= o->max_cmd_len;
}

static void ecore_mcast_set_one_rule_e1(struct _lm_device_t *pdev,
					struct ecore_mcast_obj *o, int idx,
					union ecore_mcast_config_data *cfg_data,
					int cmd)
{
	struct ecore_raw_obj *r = &o->raw;
	struct mac_configuration_cmd *data =
		(struct mac_configuration_cmd *)(r->rdata);

	/* copy mac */
	if ((cmd == ECORE_MCAST_CMD_ADD) || (cmd == ECORE_MCAST_CMD_RESTORE)) {
		ecore_set_fw_mac_addr(&data->config_table[idx].msb_mac_addr,
				&data->config_table[idx].middle_mac_addr,
				&data->config_table[idx].lsb_mac_addr,
				cfg_data->mac);

		data->config_table[idx].vlan_id = 0;
		data->config_table[idx].pf_id = r->func_id;
		data->config_table[idx].clients_bit_vector =
			mm_cpu_to_le32(1 << r->cl_id);

		ECORE_SET_FLAG(data->config_table[idx].flags,
			MAC_CONFIGURATION_ENTRY_ACTION_TYPE,
			T_ETH_MAC_COMMAND_SET);
	}
}

/**
 * Sets a header values in struct mac_configuration_cmd.
 *
 * @param pdev
 * @param p
 * @param len number of rules to handle
 */
static INLINE void ecore_mcast_set_rdata_hdr_e1(struct _lm_device_t *pdev,
					struct ecore_mcast_ramrod_params *p,
					u8 len)
{
	struct ecore_raw_obj *r = &p->mcast_obj->raw;
	struct mac_configuration_cmd *data =
		(struct mac_configuration_cmd *)(r->rdata);

	u8 offset = (CHIP_REV_IS_SLOW(pdev) ?
			 ECORE_MAX_EMUL_MULTI*(1 + r->func_id) :
			 ECORE_MAX_MULTICAST*(1 + r->func_id));

	data->hdr.offset = offset;
	data->hdr.client_id = 0xff;
	data->hdr.echo = ((r->cid & ECORE_SWCID_MASK) |
			  (ECORE_FILTER_MCAST_PENDING << ECORE_SWCID_SHIFT));
	data->hdr.length = len;
}

/**
 *  RESTORE command for 57710 is like all other commands - always a stand alone
 * command - start_idx and rdata_idx will always be 0. This function will always
 * succeed.
 *
 * @param pdev
 * @param o
 * @param start_idx index in the registry to start from
 * @param rdata_idx index in the ramrod data to start from
 *
 * @return -1 to comply with 57712 variant.
 */
static INLINE int ecore_mcast_handle_restore_cmd_e1(
	struct _lm_device_t *pdev, struct ecore_mcast_obj *o , int start_idx,
	int *rdata_idx)
{
	struct ecore_mcast_mac_elem *elem;
	int i = 0;
	union ecore_mcast_config_data cfg_data = {0};

	/* go through the registry and configure the MACs from it. */
	ecore_list_for_each_entry(elem, &o->registry.exact_match.macs, link, struct ecore_mcast_mac_elem) {
		cfg_data.mac = &elem->mac[0];
		o->set_one_rule(pdev, o, i, &cfg_data, ECORE_MCAST_CMD_RESTORE);

		i++;

	//  DP(NETIF_MSG_IFUP, "About to configure "ECORE_MAC_FMT
	//     " mcast MAC\n", ECORE_MAC_PRN_LIST(cfg_data.mac));
	}

	*rdata_idx = i;

	return -1;
}


static INLINE int ecore_mcast_handle_pending_cmds_e1(
	struct _lm_device_t *pdev, struct ecore_mcast_ramrod_params *p)
{
	struct ecore_pending_mcast_cmd *cmd_pos;
	struct ecore_mcast_mac_elem *pmac_pos;
	struct ecore_mcast_obj *o = p->mcast_obj;
	union ecore_mcast_config_data cfg_data = {0};
	int cnt = 0;


	/* If nothing to be done - return */
	if (d_list_is_empty(&o->pending_cmds_head))
		return 0;

	/* Handle the first command */
	cmd_pos = (struct ecore_pending_mcast_cmd *)d_list_peek_head(&o->pending_cmds_head);

	switch (cmd_pos->type) {
	case ECORE_MCAST_CMD_ADD:
		ecore_list_for_each_entry(pmac_pos, &cmd_pos->data.macs_head, link, struct ecore_mcast_mac_elem) {
			cfg_data.mac = &pmac_pos->mac[0];
			o->set_one_rule(pdev, o, cnt, &cfg_data, cmd_pos->type);

			cnt++;

	//  	DP(NETIF_MSG_IFUP, "About to configure "ECORE_MAC_FMT
	//  	   " mcast MAC\n", ECORE_MAC_PRN_LIST(pmac_pos->mac));
		}
		break;

	case ECORE_MCAST_CMD_DEL:
		cnt = cmd_pos->data.macs_num;
	//  DP(NETIF_MSG_IFUP, "About to delete %d multicast MACs\n", cnt);
		break;

	case ECORE_MCAST_CMD_RESTORE:
		o->hdl_restore(pdev, o, 0, &cnt);
		break;

	default:
		ECORE_ERR1("Unknown command: %d\n", cmd_pos->type);
		return ECORE_INVAL;
	}

	d_list_remove_entry(&o->pending_cmds_head, &cmd_pos->link);
	ECORE_FREE(pdev, cmd_pos, cmd_pos->alloc_len);

	return cnt;
}

/**
 * Revert the ecore_get_fw_mac_addr().
 *
 * @param fw_hi
 * @param fw_mid
 * @param fw_lo
 * @param mac
 */
static INLINE void ecore_get_fw_mac_addr(u16 *fw_hi, u16 *fw_mid, u16 *fw_lo,
					 u8 *mac)
{
	mac[1] = ((u8 *)fw_hi)[0];
	mac[0] = ((u8 *)fw_hi)[1];
	mac[3] = ((u8 *)fw_mid)[0];
	mac[2] = ((u8 *)fw_mid)[1];
	mac[5] = ((u8 *)fw_lo)[0];
	mac[4] = ((u8 *)fw_lo)[1];
}

/**
 * Check the ramrod data first entry flag to see if it's a DELETE or ADD command
 * and update the registry correspondingly: if ADD - allocate a memory and add
 * the entries to the registry (list), if DELETE - clear the registry and free
 * the memory.
 *
 * @param pdev
 * @param cnt
 *
 * @return int
 */
static INLINE int ecore_mcast_refresh_registry_e1(struct _lm_device_t *pdev,
					   struct ecore_mcast_obj *o)
{
	struct ecore_raw_obj *raw = &o->raw;
	struct ecore_mcast_mac_elem *elem;
	struct mac_configuration_cmd *data =
			(struct mac_configuration_cmd *)(raw->rdata);

	/* If first entry contains a SET bit - the command was ADD,
	 * otherwise - DEL_ALL
	 */
	if (ECORE_GET_FLAG(data->config_table[0].flags,
			MAC_CONFIGURATION_ENTRY_ACTION_TYPE)) {
		int i, len = data->hdr.length;

		/* Break if it was a RESTORE command */
		if (!d_list_is_empty(&o->registry.exact_match.macs))
			return 0;
		elem = ECORE_ZALLOC(pdev, sizeof(*elem)*len);
		if (!elem) {
			ECORE_ERR("Failed to allocate registry memory\n");
			return ECORE_NOMEM;
		}

		for (i = 0; i < len; i++, elem++) {
			ecore_get_fw_mac_addr(
				&data->config_table[i].msb_mac_addr,
				&data->config_table[i].middle_mac_addr,
				&data->config_table[i].lsb_mac_addr,
				elem->mac);
	//  	DP(NETIF_MSG_IFUP, "Adding registry entry for ["
	//  	   ECORE_MAC_FMT"]\n", elem->mac);
			d_list_push_tail(&o->registry.exact_match.macs, &elem->link);
		}
	} else {
		elem = (struct ecore_mcast_mac_elem *)d_list_peek_head(&o->registry.exact_match.macs);
	//  DP(NETIF_MSG_IFUP, "Deleting a registry\n");
		ECORE_FREE(pdev, elem, sizeof(*elem));
		d_list_clear(&o->registry.exact_match.macs);
	}

	return 0;
}

static int ecore_mcast_setup_e1(struct _lm_device_t *pdev,
				struct ecore_mcast_ramrod_params *p,
				int cmd)
{
	struct ecore_mcast_obj *o = p->mcast_obj;
	struct ecore_raw_obj *raw = &o->raw;
	struct mac_configuration_cmd *data =
		(struct mac_configuration_cmd *)(raw->rdata);
	int cnt = 0, i, rc;

	/* Reset the ramrod data buffer */
	memset(data, 0, sizeof(*data));

	/* First set all entries as invalid */
	for (i = 0; i < o->max_cmd_len ; i++)
		ECORE_SET_FLAG(data->config_table[i].flags,
			MAC_CONFIGURATION_ENTRY_ACTION_TYPE,
			T_ETH_MAC_COMMAND_INVALIDATE);

	/* Handle pending commands first */
	cnt = ecore_mcast_handle_pending_cmds_e1(pdev, p);

	/* If there are no more pending commands - clear SCHEDULED state */
	if (d_list_is_empty(&o->pending_cmds_head))
		o->clear_sched(o);

	/* The below may be TRUE iff there were no pending commands */
	if (!cnt)
		cnt = ecore_mcast_handle_current_cmd(pdev, p, cmd, 0);

	/* For 57710 every command has o->max_cmd_len length to ensure that
	 * commands are done one at a time.
	 */
	o->total_pending_num -= o->max_cmd_len;

	/* send a ramrod */

	DbgBreakIf(cnt > o->max_cmd_len);

	/* Set ramrod header (in particular, a number of entries to update) */
	ecore_mcast_set_rdata_hdr_e1(pdev, p, (u8)cnt);

	/* update a registry: we need the registry contents to be always up
	 * to date in order to be able to execute a RESTORE opcode. Here
	 * we use the fact that for 57710 we sent one command at a time
	 * hence we may take the registry update out of the command handling
	 * and do it in a simpler way here.
	 */
	rc = ecore_mcast_refresh_registry_e1(pdev, o);
	if (rc)
		return rc;

	/* Commit writes towards the memory before sending a ramrod */
	mb();

	/* If CLEAR_ONLY was requested - don't send a ramrod and clear
	 * RAMROD_PENDING status immediately.
	 */
	if (ECORE_TEST_BIT(RAMROD_DRV_CLR_ONLY, &p->ramrod_flags)) {
		raw->clear_pending(raw);
		return 0;
	} else {
		/* Send a ramrod */
		rc = ecore_sp_post( pdev,
							RAMROD_CMD_ID_ETH_SET_MAC,
							raw->cid,
							raw->rdata_mapping,
							ETH_CONNECTION_TYPE);
		if (rc)
			return rc;

		/* Ramrod completion is pending */
		return ECORE_PENDING;
	}

}

static int ecore_mcast_get_registry_size_exact(struct ecore_mcast_obj *o)
{
	return o->registry.exact_match.num_macs_set;
}

static int ecore_mcast_get_registry_size_aprox(struct ecore_mcast_obj *o)
{
	return o->registry.aprox_match.num_bins_set;
}

static void ecore_mcast_set_registry_size_exact(struct ecore_mcast_obj *o,
						int n)
{
	o->registry.exact_match.num_macs_set = n;
}

static void ecore_mcast_set_registry_size_aprox(struct ecore_mcast_obj *o,
						int n)
{
	o->registry.aprox_match.num_bins_set = n;
}

int ecore_config_mcast(struct _lm_device_t *pdev,
			   struct ecore_mcast_ramrod_params *p,
			   int cmd)
{
	struct ecore_mcast_obj *o = p->mcast_obj;
	struct ecore_raw_obj *r = &o->raw;
	int rc = 0, old_reg_size;

	/* This is needed to recover number of currently configured mcast macs
	 * in case of failure.
	 */
	old_reg_size = o->get_registry_size(o);

	/* Do some calculations and checks */
	rc = o->preamble(pdev, p, cmd);
	if (rc)
		return rc;

	/* Return if there is no work to do */
	if ((!p->mcast_list_len) && (!o->check_sched(o)))
		return 0;

	//DP(NETIF_MSG_IFUP, "o->total_pending_num=%d p->mcast_list_len=%d "
	//  	   "o->max_cmd_len=%d\n", o->total_pending_num,
	//  	p->mcast_list_len, o->max_cmd_len);

	/* Enqueue the current command to the pending list if we can't complete
	 * it in the current iteration
	 */
	if (r->check_pending(r) ||
		((o->max_cmd_len > 0) && (o->total_pending_num > o->max_cmd_len))) {
		rc = o->enqueue_cmd(pdev, p->mcast_obj, p, cmd);
		if (rc < 0)
			goto error_exit1;

		/* As long as the current command is in a command list we
		 * don't need to handle it separately.
		 */
		p->mcast_list_len = 0;
	}

	if (!r->check_pending(r)) {

		/* Set 'pending' state */
		r->set_pending(r);

		/* Configure the new classification in the chip */
		rc = o->config_mcast(pdev, p, cmd);
		if (rc < 0)
			goto error_exit2;

		/* Wait for a ramrod completion if was requested */
		if (ECORE_TEST_BIT(RAMROD_COMP_WAIT, &p->ramrod_flags)) {
			rc = o->wait_comp(pdev, o);
			if (rc)
				return rc;
		}
	}

	return rc;

error_exit2:
	r->clear_pending(r);

error_exit1:
	o->postmortem(pdev, p, old_reg_size);

	return rc;
}

static void ecore_mcast_clear_sched(struct ecore_mcast_obj *o)
{
	smp_mb__before_clear_bit();
	ECORE_CLEAR_BIT(o->sched_state, o->raw.pstate);
	smp_mb__after_clear_bit();
}

static void ecore_mcast_set_sched(struct ecore_mcast_obj *o)
{
	smp_mb__before_clear_bit();
	ECORE_SET_BIT(o->sched_state, o->raw.pstate);
	smp_mb__after_clear_bit();
}

static BOOL ecore_mcast_check_sched(struct ecore_mcast_obj *o)
{
	return ECORE_TEST_BIT(o->sched_state, o->raw.pstate);
}

static BOOL ecore_mcast_check_pending(struct ecore_mcast_obj *o)
{
	return (o->raw.check_pending(&o->raw) || o->check_sched(o));
}

void ecore_init_mcast_obj(struct _lm_device_t *pdev,
			  struct ecore_mcast_obj *mcast_obj,
			  u8 mcast_cl_id, u32 mcast_cid, u8 func_id,
			  u8 engine_id, void *rdata, lm_address_t rdata_mapping,
			  int state, unsigned long *pstate, ecore_obj_type type)
{
	memset(mcast_obj, 0, sizeof(*mcast_obj));

	ecore_init_raw_obj(&mcast_obj->raw, mcast_cl_id, mcast_cid, func_id,
			   rdata, rdata_mapping, state, pstate, type);

	mcast_obj->engine_id = engine_id;

	d_list_clear(&mcast_obj->pending_cmds_head);

	mcast_obj->sched_state = ECORE_FILTER_MCAST_SCHED;
	mcast_obj->check_sched = ecore_mcast_check_sched;
	mcast_obj->set_sched = ecore_mcast_set_sched;
	mcast_obj->clear_sched = ecore_mcast_clear_sched;

	if (CHIP_IS_E2E3(pdev)) {
		mcast_obj->config_mcast = ecore_mcast_setup_e2;
		mcast_obj->enqueue_cmd = ecore_mcast_enqueue_cmd;
		mcast_obj->hdl_restore = ecore_mcast_handle_restore_cmd_e2;
		mcast_obj->check_pending = ecore_mcast_check_pending;
	/* TODO: There should be a proper HSI define for this number!!! */
		mcast_obj->max_cmd_len = 16;
		mcast_obj->wait_comp = ecore_mcast_wait;
		mcast_obj->set_one_rule = ecore_mcast_set_one_rule_e2;
		mcast_obj->preamble = ecore_mcast_preamble_e2;
		mcast_obj->postmortem = ecore_mcast_postmortem_e2;
		mcast_obj->get_registry_size =
			ecore_mcast_get_registry_size_aprox;
		mcast_obj->set_registry_size =
			ecore_mcast_set_registry_size_aprox;
	} else if (CHIP_IS_E1(pdev)) {
		mcast_obj->config_mcast = ecore_mcast_setup_e1;
		mcast_obj->enqueue_cmd = ecore_mcast_enqueue_cmd;
		mcast_obj->hdl_restore = ecore_mcast_handle_restore_cmd_e1;
		mcast_obj->check_pending = ecore_mcast_check_pending;

		if (CHIP_REV_IS_SLOW(pdev))
			mcast_obj->max_cmd_len = ECORE_MAX_EMUL_MULTI;
		else
			mcast_obj->max_cmd_len = ECORE_MAX_MULTICAST;

		mcast_obj->wait_comp = ecore_mcast_wait;
		mcast_obj->set_one_rule = ecore_mcast_set_one_rule_e1;
		mcast_obj->preamble = ecore_mcast_preamble_e1;
		mcast_obj->postmortem = ecore_mcast_postmortem_e1;
		mcast_obj->get_registry_size =
			ecore_mcast_get_registry_size_exact;
		mcast_obj->set_registry_size =
			ecore_mcast_set_registry_size_exact;

		/* 57710 is the only chip that uses the exact match for mcast
		 * at the moment.
		 */
		d_list_clear(&mcast_obj->registry.exact_match.macs);

	} else if (CHIP_IS_E1H(pdev)) {
		mcast_obj->config_mcast = ecore_mcast_setup_e1h;
		mcast_obj->enqueue_cmd = (enqueue_cmd_func) NULL;
		mcast_obj->hdl_restore = (hdl_restore_func) NULL;
		mcast_obj->check_pending = ecore_mcast_check_pending;

		/* 57711 doesn't send a ramrod, so it has unlimited credit
		 * for one command.
		 */
		mcast_obj->max_cmd_len = -1;
		mcast_obj->wait_comp = ecore_mcast_wait;
		mcast_obj->set_one_rule = (set_one_rule_func) NULL;
		mcast_obj->preamble = ecore_mcast_preamble_e1h;
		mcast_obj->postmortem = ecore_mcast_postmortem_e1h;
		mcast_obj->get_registry_size =
			ecore_mcast_get_registry_size_aprox;
		mcast_obj->set_registry_size =
			ecore_mcast_set_registry_size_aprox;
	} else {
		ECORE_ERR("Do not support chips others than E1X and E2\n");
		BUG();
	}
}

/*************************** Credit handling **********************************/

/**
 * atomic_add_ifless - adds if the result is less than a given
 * value.
 * @param v pointer of type atomic_t
 * @param a the amount to add to v...
 * @param u ...if (v + a) is less than u.
 *
 * @return TRUE if (v + a) was less than u, and FALSE
 * otherwise.
 */
static INLINE BOOL __atomic_add_ifless(atomic_t *v, int a, int u)
{
	int c, old;

	c = mm_atomic_read(v);
	for (;;) {
		if (c + a >= u)
			return FALSE;

		old = mm_atomic_cmpxchg((v), c, c + a);
		if (old == c)
			break;
		c = old;
	}

	return TRUE;
}

/**
 * atomic_dec_ifmoe - dec if the result is more or equal than a
 * given value.
 * @param v pointer of type atomic_t
 * @param a the amount to dec from v...
 * @param u ...if (v - a) is more or equal than u.
 *
 * @return TRUE if (v - a) was more or equal than u, and FALSE
 * otherwise.
 */
static INLINE BOOL __atomic_dec_ifmoe(atomic_t *v, int a, int u)
{
	int c, old;

	c = mm_atomic_read(v);
	for (;;) {
		if (c - a < u)
			return FALSE;

		old = mm_atomic_cmpxchg((v), c, c - a);
		if (old == c)
			break;
		c = old;
	}

	return TRUE;
}

static BOOL ecore_credit_pool_get(struct ecore_credit_pool_obj *o, int cnt)
{
	BOOL rc;

	smp_mb();
	rc = __atomic_dec_ifmoe((atomic_t *)&o->credit, cnt, 0);
	smp_mb();

	return rc;
}

static BOOL ecore_credit_pool_put(struct ecore_credit_pool_obj *o, int cnt)
{
	BOOL rc;

	smp_mb();

	/* Don't let to refill if credit + cnt > pool_sz */
	rc = __atomic_add_ifless((atomic_t *)&o->credit, cnt, o->cam_sz + 1);

	smp_mb();

	return rc;
}

static int ecore_credit_pool_check(struct ecore_credit_pool_obj *o)
{
	int cur_credit;

	smp_mb();
	cur_credit = mm_atomic_read((atomic_t *)&o->credit);

	return cur_credit;
}

static BOOL ecore_credit_pool_always_TRUE(struct ecore_credit_pool_obj *o,
					  int cnt)
{
	return TRUE;
}


static INLINE BOOL ecore_credit_pool_get_offset(
	struct ecore_credit_pool_obj * o, u8 * cam_offset)
{
	BOOL rc;
	u8 idx;

	*cam_offset = 0xff;

	rc = ecore_credit_pool_get(o, 1);
	if (!rc)
	{
		return rc;
	}

	/* Find "internal cam-offset" then add to base for this function... */
	for (idx = 0; idx < o->cam_sz; idx++) {
		if (o->cam_mirror[idx] == FALSE) {
			/* This entry is now ours...*/
			o->cam_mirror[idx] = TRUE;
			*cam_offset = idx;
			break;
		}
	}

	if (*cam_offset == 0xff)
		return FALSE;

	*cam_offset = o->base_cam_offset + *cam_offset;

	return TRUE;
}

static INLINE BOOL ecore_credit_pool_put_offset(
	struct ecore_credit_pool_obj * o, u8 cam_offset)
{
	BOOL rc;

	rc = ecore_credit_pool_put(o, 1);
	if (!rc)
	{
		return rc;
	}

	if (cam_offset < o->base_cam_offset)
		return FALSE;

	cam_offset -= o->base_cam_offset;

	if (cam_offset >= o->cam_sz)
		return FALSE;

	o->cam_mirror[cam_offset] = FALSE;
	return TRUE;
}

static INLINE BOOL ecore_credit_pool_put_offset_always_TRUE(
	struct ecore_credit_pool_obj * o, u8 cam_offset)
{
	return TRUE;
}

static INLINE BOOL ecore_credit_pool_get_offset_always_TRUE(
	struct ecore_credit_pool_obj * o, u8 * cam_offset)
{
	return TRUE;
}
/**
 * Initialize credit pool internals.
 *
 * @param p
 * @param credit Pool size - if negative pool operations will
 *  			 always succeed (unlimited pool)
 */
static INLINE void ecore_init_credit_pool(struct ecore_credit_pool_obj *p,
					  u8 func_id, int credit)
{
	/* Zero the object first */
	memset(p, 0, sizeof(*p));

	mm_atomic_set((u32_t *)&p->credit, credit);
	p->cam_sz = credit;

	p->base_cam_offset = func_id * p->cam_sz;

	/* Commit the change */
	smp_mb();

	p->check = ecore_credit_pool_check;

	/* if pool credit is negative - disable the checks */
	if (credit >= 0) {
		p->put = ecore_credit_pool_put_offset;
		p->get = ecore_credit_pool_get_offset;
		p->put_mult = ecore_credit_pool_put;
		p->get_mult = ecore_credit_pool_get;
	} else {
		p->put = ecore_credit_pool_put_offset_always_TRUE;
		p->get = ecore_credit_pool_get_offset_always_TRUE;
		p->put_mult = ecore_credit_pool_always_TRUE;
		p->get_mult = ecore_credit_pool_always_TRUE;
	}
}

void ecore_init_mac_credit_pool(struct _lm_device_t *pdev,
				struct ecore_credit_pool_obj *p, u8 func_id,
				u8 func_num)
{
	#define ECORE_CAM_SIZE_EMUL 5 /* TODO: this will be define in consts as well... */

	int cam_sz;

	if (CHIP_IS_E1(pdev)) {
		/* In E1, Multicast is saved in cam... */
		if (CHIP_REV_IS_ASIC(pdev))
			cam_sz = (MAX_MAC_CREDIT_E1 / 2) - ECORE_MAX_MULTICAST;
		else
			cam_sz = ECORE_CAM_SIZE_EMUL - ECORE_MAX_EMUL_MULTI;

		ecore_init_credit_pool(p, func_id, cam_sz);

	} else if (CHIP_IS_E1H(pdev)) {
		/** CAM credit is equaly divided between all active functions
		 *  on the PORT!.
		 */
		if ((func_num > 0)) {
			if (CHIP_REV_IS_ASIC(pdev))
				cam_sz = (MAX_MAC_CREDIT_E1H / (2*func_num));
			else
				cam_sz = ECORE_CAM_SIZE_EMUL;
			ecore_init_credit_pool(p, func_id, cam_sz);
		} else {
			/* this should never happen! Block MAC operations. */
			ecore_init_credit_pool(p, func_id, 0);
		}

	} else {

		/** CAM credit is equaly divided between all active functions
		 *  on the PATH.
		 */
		if ((func_num > 0)) {
			if (CHIP_REV_IS_ASIC(pdev))
				cam_sz = (MAX_MAC_CREDIT_E2 / func_num);
			else
				cam_sz = ECORE_CAM_SIZE_EMUL;

			ecore_init_credit_pool(p, func_id, cam_sz);
		} else {
			/* this should never happen! Block MAC operations. */
			ecore_init_credit_pool(p, func_id, 0);
		}

	}
}

void ecore_init_vlan_credit_pool(struct _lm_device_t *pdev,
				 struct ecore_credit_pool_obj *p, u8 func_id, u8 func_num)
{
	if (CHIP_IS_E1x(pdev)) {
		/* There is no VLAN credit in HW on 57710 and 57711 only
		 * MAC / MAC-VLAN can be set
		 */
		ecore_init_credit_pool(p, func_id, -1);
	} else {
		/** CAM credit is equaly divided between all active functions
		 *  on the PATH.
		 */
		if (func_num > 0)
			ecore_init_credit_pool(p, func_id, MAX_VLAN_CREDIT_E2 / func_num);
		else
			 /* this should never happen! Block VLAN operations. */
			ecore_init_credit_pool(p, func_id, 0);
	}
}

/**
 * Configure RSS for 57712: we will send on UPDATE ramrod for
 * that matter.
 *
 * @param pdev
 * @param p
 *
 * @return int
 */
static int ecore_setup_rss(struct _lm_device_t *pdev,
				  struct ecore_config_rss_params *p)
{
	struct ecore_rss_config_obj *o = p->rss_obj;
	struct ecore_raw_obj *r = &o->raw;
	struct eth_rss_update_ramrod_data *data =
		(struct eth_rss_update_ramrod_data *)(r->rdata);
	u8 rss_mode = 0;

	memset(data, 0, sizeof(*data));

	//DP(NETIF_MSG_IFUP, "Configuring RSS\n");

	/* Set an echo field TODO is this right for E2 as well?*/
	data->echo = (r->cid & ECORE_SWCID_MASK) |
			(r->state << ECORE_SWCID_SHIFT);

	/* RSS mode */
	if (ECORE_TEST_BIT(ECORE_RSS_MODE_DISABLED, &p->rss_flags))
		rss_mode = ETH_RSS_MODE_DISABLED;
	else if (ECORE_TEST_BIT(ECORE_RSS_MODE_REGULAR, &p->rss_flags))
		rss_mode = ETH_RSS_MODE_REGULAR;
	else if (ECORE_TEST_BIT(ECORE_RSS_MODE_VLAN_PRI, &p->rss_flags))
		rss_mode = ETH_RSS_MODE_VLAN_PRI;
	else if (ECORE_TEST_BIT(ECORE_RSS_MODE_E1HOV_PRI, &p->rss_flags))
		  rss_mode = ETH_RSS_MODE_E1HOV_PRI;
	else if (ECORE_TEST_BIT(ECORE_RSS_MODE_IP_DSCP, &p->rss_flags))
		  rss_mode = ETH_RSS_MODE_IP_DSCP;

	data->rss_mode = rss_mode;

	//DP(NETIF_MSG_IFUP, "rss_mode=%d\n", rss_mode);

	/* RSS capabilities */
	if (ECORE_TEST_BIT(ECORE_RSS_IPV4, &p->rss_flags))
		data->capabilities |=
			ETH_RSS_UPDATE_RAMROD_DATA_IPV4_CAPABILITY;

	if (ECORE_TEST_BIT(ECORE_RSS_IPV4_TCP, &p->rss_flags))
		data->capabilities |=
			ETH_RSS_UPDATE_RAMROD_DATA_IPV4_TCP_CAPABILITY;

	if (ECORE_TEST_BIT(ECORE_RSS_IPV6, &p->rss_flags))
		data->capabilities |=
			ETH_RSS_UPDATE_RAMROD_DATA_IPV6_CAPABILITY;

	if (ECORE_TEST_BIT(ECORE_RSS_IPV6_TCP, &p->rss_flags))
		data->capabilities |=
			ETH_RSS_UPDATE_RAMROD_DATA_IPV6_TCP_CAPABILITY;

	if (ECORE_TEST_BIT(ECORE_RSS_SET_SRCH, &p->rss_flags))
		data->capabilities |=
			ETH_RSS_UPDATE_RAMROD_DATA_UPDATE_RSS_KEY;

	/* Hashing mask */
	data->rss_result_mask = p->rss_result_mask;

	/* RSS engine ID - will not be used on E1x*/
	data->rss_engine_id = o->engine_id;

	/* Indirection table */
	memcpy(&data->indirection_table[0], &p->ind_table[0],
		   T_ETH_INDIRECTION_TABLE_SIZE);

	/* RSS keys */
	memcpy(&data->rss_key[0], &p->rss_key[0], sizeof(data->rss_key));

	/* Commit writes towards the memory before sending a ramrod */
	mb();

	/* Send a ramrod */
	return ecore_sp_post(pdev,
		RAMROD_CMD_ID_ETH_RSS_UPDATE,
		r->cid,
		r->rdata_mapping,
		ETH_CONNECTION_TYPE);
}

int ecore_config_rss(struct _lm_device_t *pdev, struct ecore_config_rss_params *p)
{
	int rc;
	struct ecore_rss_config_obj *o = p->rss_obj;
	struct ecore_raw_obj *r = &o->raw;

	r->set_pending(r);

	rc = o->config_rss(pdev, p);
	if (rc) {
		r->clear_pending(r);
		return rc;
	}

	if (ECORE_TEST_BIT(RAMROD_COMP_WAIT, &p->ramrod_flags)) {
		rc = r->wait_comp(pdev, r);
		if (rc)
			return rc;
	}

	return 0;
}


void ecore_init_rss_config_obj(struct _lm_device_t *pdev,
				   struct ecore_rss_config_obj *rss_obj,
				   u8 cl_id, u32 cid, u8 func_id, u8 engine_id,
				   void *rdata, lm_address_t rdata_mapping, int state,
				   unsigned long *pstate, ecore_obj_type type)
{
	ecore_init_raw_obj(&rss_obj->raw, cl_id, cid, func_id, rdata,
			   rdata_mapping, state, pstate, type);

	rss_obj->engine_id = engine_id;

	rss_obj->config_rss = ecore_setup_rss;
}

/********************** Client state update ***********************************/
#if 0 // not used yet...
int ecore_fw_cl_update(struct _lm_device_t *pdev,
			   struct ecore_client_update_params *params)
{
	struct client_update_ramrod_data *data =
		(struct client_update_ramrod_data *)params->rdata;
	lm_address_t data_mapping = params->rdata_mapping;
	int rc;

	memset(data, 0, sizeof(*data));

	/* Client ID of the client to update */
	data->client_id = params->cl_id;

	/* Default VLAN value */
	data->default_vlan = mm_cpu_to_le16(params->def_vlan);

	/* Inner VLAN stripping */
	data->inner_vlan_removal_enable_flg =
		ECORE_TEST_BIT(ECORE_CL_UPDATE_IN_VLAN_REM, &params->update_flags);
	data->inner_vlan_removal_change_flg =
		ECORE_TEST_BIT(ECORE_CL_UPDATE_IN_VLAN_REM_CHNG,
			 &params->update_flags);

	/* Outer VLAN sripping */
	data->outer_vlan_removal_enable_flg =
		ECORE_TEST_BIT(ECORE_CL_UPDATE_OUT_VLAN_REM, &params->update_flags);
	data->outer_vlan_removal_change_flg =
		ECORE_TEST_BIT(ECORE_CL_UPDATE_OUT_VLAN_REM_CHNG,
			 &params->update_flags);

	/* Drop packets that have source MAC that doesn't belong to this
	 * Client.
	 */
	data->anti_spoofing_enable_flg =
		ECORE_TEST_BIT(ECORE_CL_UPDATE_ANTI_SPOOF, &params->update_flags);
	data->anti_spoofing_change_flg =
		ECORE_TEST_BIT(ECORE_CL_UPDATE_ANTI_SPOOF_CHNG,
			 &params->update_flags);

	/* Activate/Deactivate */
	data->activate_flg =
		ECORE_TEST_BIT(ECORE_CL_UPDATE_ACTIVATE, &params->update_flags);
	data->activate_change_flg =
		ECORE_TEST_BIT(ECORE_CL_UPDATE_ACTIVATE_CHNG, &params->update_flags);

	/* Enable default VLAN */
	data->default_vlan_enable_flg =
		ECORE_TEST_BIT(ECORE_CL_UPDATE_DEF_VLAN_EN, &params->update_flags);
	data->default_vlan_change_flg =
		ECORE_TEST_BIT(ECORE_CL_UPDATE_DEF_VLAN_EN_CHNG,
			 &params->update_flags);

	/* Set "pending" bit */
	ECORE_SET_BIT(ECORE_FILTER_CL_UPDATE_PENDING, &pdev->sp_state);

	/* Send a ramrod */
	rc = ecore_sp_post(pdev,
		RAMROD_CMD_ID_ETH_CLIENT_UPDATE,
		params->cid,
		data_mapping,
		0);
	if (rc)
		return rc;

	/* Wait for completion */
	if (!ecore_wait_sp_comp(pdev))
		return -EAGAIN;

	return 0;
}

#endif
