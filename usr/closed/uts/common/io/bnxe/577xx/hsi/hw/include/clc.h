

#ifndef __ELINK_H
#define __ELINK_H

/***********************************************************/
/*                  CLC Call backs functions               */
/***********************************************************/
/* CLC device structure */
struct elink_dev;

extern u32 elink_cb_reg_read(struct elink_dev *cb, u32 reg_addr);
extern void elink_cb_reg_write(struct elink_dev *cb, u32 reg_addr, u32 val);
/* wb_write - pointer to 2 32 bits vars to be passed to the DMAE*/
extern void elink_cb_reg_wb_write(struct elink_dev *cb, u32 offset,
				u32 *wb_write, u16 len);
extern void elink_cb_reg_wb_read(struct elink_dev *cb, u32 offset,
			       u32 *wb_write, u16 len);

/* mode - 0( LOW ) /1(HIGH)*/
extern u8 elink_cb_gpio_write(struct elink_dev *cb,
			    u16 gpio_num,
			    u8 mode, u8 port);

extern u32 elink_cb_gpio_read(struct elink_dev *cb, u16 gpio_num, u8 port);
extern u8 elink_cb_gpio_int_write(struct elink_dev *cb,
				u16 gpio_num,
				u8 mode, u8 port);

extern u32 elink_cb_fw_command(struct elink_dev *cb, u32 command, u32 param);

/* Delay */
extern void elink_cb_udelay(struct elink_dev *cb, u32 microsecond);

/* This function is called every 1024 bytes downloading of phy firmware.
Driver can use it to print to screen indication for download progress */
extern void elink_cb_download_progress(struct elink_dev *cb, u32 cur, u32 total);

/* Each log type has its own parameters */
typedef enum elink_log_id {
	ELINK_LOG_ID_UNQUAL_IO_MODULE	= 0, /* u8 port, const char* vendor_name, const char* vendor_pn */
	ELINK_LOG_ID_OVER_CURRENT	= 1, /* u8 port */
	ELINK_LOG_ID_PHY_UNINITIALIZED	= 2, /* u8 port */
	ELINK_LOG_ID_MDIO_ACCESS_TIMEOUT= 3, /* No params */
}elink_log_id_t;

extern void elink_cb_event_log(struct elink_dev *cb, const elink_log_id_t log_id, ...);

extern u8 elink_cb_path_id(struct elink_dev *cb);
#define ELINK_EVENT_LOG_LEVEL_ERROR 	1
#define ELINK_EVENT_LOG_LEVEL_WARNING 	2
#define ELINK_EVENT_ID_SFP_UNQUALIFIED_MODULE 	1
#define ELINK_EVENT_ID_SFP_POWER_FAULT 		2

#ifdef ELINK_AUX_POWER
#define elink_cb_event_log(cb, log_id, ...)
#define elink_cb_get_friendly_name(cb) ''
#endif /*ELINK_AUX_POWER */

/* Debug prints */
#ifdef ELINK_DEBUG
extern void elink_cb_dbg(struct elink_dev *cb, char *fmt);
extern void elink_cb_dbg1(struct elink_dev *cb, char *fmt, u32 arg1);
extern void elink_cb_dbg2(struct elink_dev *cb, char *fmt, u32 arg1, u32 arg2);
extern void elink_cb_dbg3(struct elink_dev *cb, char *fmt, u32 arg1, u32 arg2,
			  u32 arg3);
#define ELINK_DEBUG_P0(cb, fmt) 		elink_cb_dbg(cb, fmt)
#define ELINK_DEBUG_P1(cb, fmt, arg1) 		elink_cb_dbg1(cb, fmt, arg1)
#define ELINK_DEBUG_P2(cb, fmt, arg1, arg2)	elink_cb_dbg2(cb, fmt, arg1, arg2)
#define ELINK_DEBUG_P3(cb, fmt, arg1, arg2, arg3) \
					elink_cb_dbg3(cb, fmt, arg1, arg2, arg3)
#else
#define ELINK_DEBUG_P0(cb, fmt)
#define ELINK_DEBUG_P1(cb, fmt, arg1)
#define ELINK_DEBUG_P2(cb, fmt, arg1, arg2)
#define ELINK_DEBUG_P3(cb, fmt, arg1, arg2, arg3)
#endif

/***********************************************************/
/*                         Defines                         */
/***********************************************************/
#define ELINK_DEFAULT_PHY_DEV_ADDR	3
#define ELINK_E2_DEFAULT_PHY_DEV_ADDR	5

#ifndef DUPLEX_FULL
#define DUPLEX_FULL			1
#endif
#ifndef DUPLEX_HALF
#define DUPLEX_HALF			2
#endif

#define ELINK_STATUS_OK 		0
#define ELINK_STATUS_ERROR		1
#define ELINK_STATUS_TIMEOUT		2
#define ELINK_STATUS_NO_LINK		3
#define ELINK_STATUS_INVALID_IMAGE	4

#define ELINK_FLOW_CTRL_AUTO		PORT_FEATURE_FLOW_CONTROL_AUTO
#define ELINK_FLOW_CTRL_TX		PORT_FEATURE_FLOW_CONTROL_TX
#define ELINK_FLOW_CTRL_RX		PORT_FEATURE_FLOW_CONTROL_RX
#define ELINK_FLOW_CTRL_BOTH		PORT_FEATURE_FLOW_CONTROL_BOTH
#define ELINK_FLOW_CTRL_NONE		PORT_FEATURE_FLOW_CONTROL_NONE

#define ELINK_SPEED_AUTO_NEG		0
#define ELINK_SPEED_10			10
#define ELINK_SPEED_100			100
#define ELINK_SPEED_1000		1000
#define ELINK_SPEED_2500		2500
#define ELINK_SPEED_10000		10000
#define ELINK_SPEED_12000		12000
#define ELINK_SPEED_12500		12500
#define ELINK_SPEED_13000		13000
#define ELINK_SPEED_15000		15000
#define ELINK_SPEED_16000		16000
#define ELINK_SPEED_20000		20000

#define ELINK_SFP_EEPROM_VENDOR_NAME_ADDR		0x14
#define ELINK_SFP_EEPROM_VENDOR_NAME_SIZE		16
#define ELINK_SFP_EEPROM_VENDOR_OUI_ADDR		0x25
#define ELINK_SFP_EEPROM_VENDOR_OUI_SIZE		3
#define ELINK_SFP_EEPROM_PART_NO_ADDR			0x28
#define ELINK_SFP_EEPROM_PART_NO_SIZE			16
#define ELINK_SFP_EEPROM_REVISION_ADDR		0x38
#define ELINK_SFP_EEPROM_REVISION_SIZE		4
#define ELINK_SFP_EEPROM_SERIAL_ADDR			0x44
#define ELINK_SFP_EEPROM_SERIAL_SIZE			16
#define ELINK_SFP_EEPROM_DATE_ADDR			0x54 /* ASCII YYMMDD */
#define ELINK_SFP_EEPROM_DATE_SIZE			6
#define ELINK_PWR_FLT_ERR_MSG_LEN			250

#define ELINK_XGXS_EXT_PHY_TYPE(ext_phy_config) \
		((ext_phy_config) & PORT_HW_CFG_XGXS_EXT_PHY_TYPE_MASK)
#define ELINK_XGXS_EXT_PHY_ADDR(ext_phy_config) \
		(((ext_phy_config) & PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >> \
		 PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT)
#define ELINK_SERDES_EXT_PHY_TYPE(ext_phy_config) \
		((ext_phy_config) & PORT_HW_CFG_SERDES_EXT_PHY_TYPE_MASK)

/* Single Media Direct board is the plain 577xx board with CX4/RJ45 jacks */
#define ELINK_SINGLE_MEDIA_DIRECT(params)	(params->num_phys == 1)
/* Single Media board contains single external phy */
#define ELINK_SINGLE_MEDIA(params)		(params->num_phys == 2)
/* Dual Media board contains two external phy with different media */
#define ELINK_DUAL_MEDIA(params)		(params->num_phys == 3)

#define ELINK_FW_PARAM_PHY_ADDR_MASK		0x000000FF
#define ELINK_FW_PARAM_PHY_TYPE_MASK		0x0000FF00
#define ELINK_FW_PARAM_MDIO_CTRL_MASK		0xFFFF0000
#define ELINK_FW_PARAM_MDIO_CTRL_OFFSET		16
#define ELINK_FW_PARAM_PHY_ADDR(fw_param) (fw_param & \
					   ELINK_FW_PARAM_PHY_ADDR_MASK)
#define ELINK_FW_PARAM_PHY_TYPE(fw_param) (fw_param & \
					   ELINK_FW_PARAM_PHY_TYPE_MASK)
#define ELINK_FW_PARAM_MDIO_CTRL(fw_param) ((fw_param & \
					    ELINK_FW_PARAM_MDIO_CTRL_MASK) >> \
					    ELINK_FW_PARAM_MDIO_CTRL_OFFSET)
#define ELINK_FW_PARAM_SET(phy_addr, phy_type, mdio_access) \
	(phy_addr | phy_type | mdio_access << ELINK_FW_PARAM_MDIO_CTRL_OFFSET)

#define ELINK_PFC_BRB_MAC_PAUSE_XOFF_THRESHOLD_PAUSEABLE		170
#define ELINK_PFC_BRB_MAC_PAUSE_XOFF_THRESHOLD_NON_PAUSEABLE		0

#define ELINK_PFC_BRB_MAC_PAUSE_XON_THRESHOLD_PAUSEABLE			250
#define ELINK_PFC_BRB_MAC_PAUSE_XON_THRESHOLD_NON_PAUSEABLE		0

#define ELINK_PFC_BRB_MAC_FULL_XOFF_THRESHOLD_PAUSEABLE			10
#define ELINK_PFC_BRB_MAC_FULL_XOFF_THRESHOLD_NON_PAUSEABLE		90

#define ELINK_PFC_BRB_MAC_FULL_XON_THRESHOLD_PAUSEABLE			50
#define ELINK_PFC_BRB_MAC_FULL_XON_THRESHOLD_NON_PAUSEABLE		250

#define ELINK_PFC_BRB_FULL_LB_XOFF_THRESHOLD				170
#define ELINK_PFC_BRB_FULL_LB_XON_THRESHOLD				250

/***********************************************************/
/*                         Structs                         */
/***********************************************************/
#define ELINK_INT_PHY		0
#define ELINK_EXT_PHY1	1
#define ELINK_EXT_PHY2	2
#define ELINK_MAX_PHYS	3

/* Same configuration is shared between the XGXS and the first external phy */
#define ELINK_LINK_CONFIG_SIZE (ELINK_MAX_PHYS - 1)
#define ELINK_LINK_CONFIG_IDX(_phy_idx) ((_phy_idx == ELINK_INT_PHY) ? \
					 0 : (_phy_idx - 1))

/***********************************************************/
/*                      elink_phy struct                     */
/*  Defines the required arguments and function per phy    */
/***********************************************************/
struct elink_vars;
struct elink_params;
struct elink_phy;

typedef u8 (*config_init_t)(struct elink_phy *phy, struct elink_params *params,
			    struct elink_vars *vars);
typedef u8 (*read_status_t)(struct elink_phy *phy, struct elink_params *params,
			    struct elink_vars *vars);
typedef void (*link_reset_t)(struct elink_phy *phy,
			     struct elink_params *params);
typedef void (*config_loopback_t)(struct elink_phy *phy,
				  struct elink_params *params);
typedef u8 (*format_fw_ver_t)(u32 raw, u8 *str, u16 *len);
typedef void (*hw_reset_t)(struct elink_phy *phy, struct elink_params *params);
typedef void (*set_link_led_t)(struct elink_phy *phy,
			       struct elink_params *params, u8 mode);
typedef void (*phy_specific_func_t)(struct elink_phy *phy,
				    struct elink_params *params, u32 action);

struct elink_phy {
	u32 type;

	/* Loaded during init */
	u8 addr;

	u8 flags;
	/* Require HW lock */
#define ELINK_FLAGS_HW_LOCK_REQUIRED		(1<<0)
	/* No Over-Current detection */
#define ELINK_FLAGS_NOC				(1<<1)
	/* Fan failure detection required */
#define ELINK_FLAGS_FAN_FAILURE_DET_REQ		(1<<2)
	/* Initialize first the XGXS and only then the phy itself */
#define ELINK_FLAGS_INIT_XGXS_FIRST		(1<<3)
#define ELINK_FLAGS_REARM_LATCH_SIGNAL		(1<<6)
#define ELINK_FLAGS_SFP_NOT_APPROVED		(1<<7)

	u8 def_md_devad;
	u8 reserved;
	/* preemphasis values for the rx side */
	u16 rx_preemphasis[4];

	/* preemphasis values for the tx side */
	u16 tx_preemphasis [4];

	/* EMAC address for access MDIO */
	u32 mdio_ctrl;

	u32 supported;
#define ELINK_SUPPORTED_10baseT_Half	(1<<0)
#define ELINK_SUPPORTED_10baseT_Full	(1<<1)
#define ELINK_SUPPORTED_100baseT_Half	(1<<2)
#define ELINK_SUPPORTED_100baseT_Full 	(1<<3)
#define ELINK_SUPPORTED_1000baseT_Full 	(1<<4)
#define ELINK_SUPPORTED_2500baseX_Full 	(1<<5)
#define ELINK_SUPPORTED_10000baseT_Full (1<<6)
#define ELINK_SUPPORTED_TP 		(1<<7)
#define ELINK_SUPPORTED_FIBRE 		(1<<8)
#define ELINK_SUPPORTED_Autoneg 	(1<<9)
#define ELINK_SUPPORTED_Pause 		(1<<10)
#define ELINK_SUPPORTED_Asym_Pause	(1<<11)

	u32 media_type;
#define	ELINK_ETH_PHY_UNSPECIFIED 0x0
#define	ELINK_ETH_PHY_SFP_FIBER   0x1
#define	ELINK_ETH_PHY_XFP_FIBER   0x2
#define	ELINK_ETH_PHY_DA_TWINAX   0x3
#define	ELINK_ETH_PHY_BASE_T      0x4
#define	ELINK_ETH_PHY_KR	  0xf0
#define	ELINK_ETH_PHY_CX4	  0xf1
#define	ELINK_ETH_PHY_NOT_PRESENT 0xff

	/* The address in which version is located*/
	u32 ver_addr;

	u16 req_flow_ctrl;

	u16 req_line_speed;

	u32 speed_cap_mask;

	u16 req_duplex;
	u16 rsrv;
	/* Called per phy/port init, and it configures LASI, speed, autoneg,
	 duplex, flow control negotiation, etc. */
	config_init_t config_init;

	/* Called due to interrupt. It determines the link, speed */
	read_status_t read_status;

	/* Called when driver is unloading. Should reset the phy */
	link_reset_t link_reset;

	/* Set the loopback configuration for the phy */
	config_loopback_t config_loopback;

	/* Format the given raw number into str up to len */
	format_fw_ver_t format_fw_ver;

	/* Reset the phy (both ports) */
	hw_reset_t hw_reset;

	/* Set link led mode (on/off/oper)*/
	set_link_led_t set_link_led;

	/* PHY Specific tasks */
	phy_specific_func_t phy_specific_func;
#define ELINK_DISABLE_TX	1
#define ELINK_ENABLE_TX	2
};


/* Inputs parameters to the CLC */
struct elink_params {

	u8 port;

	/* Default / User Configuration */
	u8 loopback_mode;
#define ELINK_LOOPBACK_NONE		0
#define ELINK_LOOPBACK_EMAC		1
#define ELINK_LOOPBACK_BMAC		2
#define ELINK_LOOPBACK_XGXS		3
#define ELINK_LOOPBACK_EXT_PHY	4
#define ELINK_LOOPBACK_EXT		5
#define ELINK_LOOPBACK_UMAC		6
#define ELINK_LOOPBACK_XMAC		7

	/* Device parameters */
	u8 mac_addr[6];

	u16 req_duplex[ELINK_LINK_CONFIG_SIZE];
	u16 req_flow_ctrl[ELINK_LINK_CONFIG_SIZE];

	u16 req_line_speed[ELINK_LINK_CONFIG_SIZE]; /* Also determine AutoNeg */

	/* shmem parameters */
	u32 shmem_base;
	u32 shmem2_base;
	u32 speed_cap_mask[ELINK_LINK_CONFIG_SIZE];
	u32 switch_cfg;
#define ELINK_SWITCH_CFG_1G		PORT_FEATURE_CON_SWITCH_1G_SWITCH
#define ELINK_SWITCH_CFG_10G		PORT_FEATURE_CON_SWITCH_10G_SWITCH
#define ELINK_SWITCH_CFG_AUTO_DETECT	PORT_FEATURE_CON_SWITCH_AUTO_DETECT

	u32 lane_config;

	/* Phy register parameter */
	u32 chip_id;

	/* features */
	u32 feature_config_flags;
#define ELINK_FEATURE_CONFIG_OVERRIDE_PREEMPHASIS_ENABLED	(1<<0)
#define ELINK_FEATURE_CONFIG_PFC_ENABLED			(1<<1)
#define ELINK_FEATURE_CONFIG_BC_SUPPORTS_OPT_MDL_VRFY		(1<<2)
#define ELINK_FEATURE_CONFIG_BC_SUPPORTS_DUAL_PHY_OPT_MDL_VRFY	(1<<3)
#define ELINK_FEATURE_CONFIG_EMUL_DISABLE_EMAC			(1<<4)
#define ELINK_FEATURE_CONFIG_EMUL_DISABLE_BMAC			(1<<5)
#define ELINK_FEATURE_CONFIG_EMUL_DISABLE_UMAC			(1<<6)
#define ELINK_FEATURE_CONFIG_EMUL_DISABLE_XMAC			(1<<7)
#define ELINK_FEATURE_CONFIG_BC_SUPPORTS_VNTAG          	(1<<8)
	/* Will be populated during common init */
	struct elink_phy phy[ELINK_MAX_PHYS];

	/* Will be populated during common init */
	u8 num_phys;

	u8 rsrv;
	u16 hw_led_mode; /* part of the hw_config read from the shmem */
	u32 multi_phy_config;

	/* Device pointer passed to all callback functions */
	struct elink_dev *cb;
	u16 req_fc_auto_adv; /* Should be set to TX / BOTH when
				req_flow_ctrl is set to AUTO */
};

/* Output parameters */
struct elink_vars {
	u8 phy_flags;

	u8 mac_type;
#define ELINK_MAC_TYPE_NONE		0
#define ELINK_MAC_TYPE_EMAC		1
#define ELINK_MAC_TYPE_BMAC		2
#define ELINK_MAC_TYPE_UMAC		3
#define ELINK_MAC_TYPE_XMAC		4

	u8 phy_link_up; /* internal phy link indication */
	u8 link_up;
	u16 line_speed;
	u16 duplex;
	u16 flow_ctrl;
	u16 ieee_fc;

	/* The same definitions as the shmem parameter */
	u32 link_status;

	u8 fault_detected;
	u8 rsrv1;
	u16 rsrv2;
	u32 aeu_int_mask;
};

/***********************************************************/
/*                         Functions                       */
/***********************************************************/
u8 elink_phy_init(struct elink_params *input, struct elink_vars *output);

#ifndef EXCLUDE_LINK_RESET
/* Reset the link. Should be called when driver or interface goes down
   Before calling phy firmware upgrade, the reset_ext_phy should be set
   to 0 */
u8 elink_link_reset(struct elink_params *params, struct elink_vars *vars,
		  u8 reset_ext_phy);
#endif

/* elink_link_update should be called upon link interrupt */
u8 elink_link_update(struct elink_params *input, struct elink_vars *output);

/* use the following phy functions to read/write from external_phy
  In order to use it to read/write internal phy registers, use
  ELINK_DEFAULT_PHY_DEV_ADDR as devad, and (_bank + (_addr & 0xf)) as
  the register */
u8 elink_phy_read(struct elink_params *params, u8 phy_addr,
		  u8 devad, u16 reg, u16 *ret_val);

u8 elink_phy_write(struct elink_params *params, u8 phy_addr,
		   u8 devad, u16 reg, u16 val);

#ifdef ELINK_57711E_SUPPORT
/* Reads the link_status from the shmem,
   and update the link vars accordingly */
void elink_link_status_update(struct elink_params *input,
			    struct elink_vars *output);
#endif
#ifdef ELINK_ENHANCEMENTS
/* returns string representing the fw_version of the external phy */
u8 elink_get_ext_phy_fw_version(struct elink_params *params, u8 driver_loaded,
			      u8 *version, u16 len);
#endif

/* Set/Unset the led
   Basically, the CLC takes care of the led for the link, but in case one needs
   to set/unset the led unnaturally, set the "mode" to ELINK_LED_MODE_OPER to
   blink the led, and ELINK_LED_MODE_OFF to set the led off.*/
u8 elink_set_led(struct elink_params *params, struct elink_vars *vars,
		 u8 mode, u32 speed);
#define ELINK_LED_MODE_OFF			0
#define ELINK_LED_MODE_ON			1
#define ELINK_LED_MODE_OPER			2
#define ELINK_LED_MODE_FRONT_PANEL_OFF	3

#ifdef ELINK_ENHANCEMENTS
/* elink_handle_module_detect_int should be called upon module detection
   interrupt */
void elink_handle_module_detect_int(struct elink_params *params);

/* Get the actual link status. In case it returns ELINK_STATUS_OK, link is up,
	otherwise link is down*/
u8 elink_test_link(struct elink_params *input, struct elink_vars *vars,
		   u8 is_serdes);
#endif

/* One-time initialization for external phy after power up */
u8 elink_common_init_phy(struct elink_dev *cb, u32 shmem_base_path[],
			 u32 shmem2_base_path[], u32 chip_id);

/* Reset the external PHY using GPIO */
void elink_ext_phy_hw_reset(struct elink_dev *cb, u8 port);

#ifdef ELINK_ENHANCEMENTS
/* Reset the external of SFX7101 */
void elink_sfx7101_sp_sw_reset(struct elink_dev *cb, struct elink_phy *phy);
#endif

/* Read "byte_cnt" bytes from address "addr" from the SFP+ EEPROM */
u8 elink_read_sfp_module_eeprom(struct elink_phy *phy,
				struct elink_params *params, u16 addr,
				u8 byte_cnt, u8 *o_buf);

void elink_hw_reset_phy(struct elink_params *params);

/* Checks if HW lock is required for this phy/board type */
u8 elink_hw_lock_required(struct elink_dev *cb, u32 shmem_base,
			  u32 shmem2_base);

/* Check swap bit and adjust PHY order */
u32 elink_phy_selection(struct elink_params *params);

/* Probe the phys on board, and populate them in "params" */
u8 elink_phy_probe(struct elink_params *params);

/* Checks if fan failure detection is required on one of the phys on board */
u8 elink_fan_failure_det_req(struct elink_dev *cb, u32 shmem_base,
			     u32 shmem2_base, u8 port);

/* PFC port configuration params */
struct elink_nig_brb_pfc_port_params {
	/* NIG */
	u32 pause_enable;
	u32 llfc_out_en;
	u32 llfc_enable;
	u32 pkt_priority_to_cos;
	u32 rx_cos0_priority_mask;
	u32 rx_cos1_priority_mask;
	u32 llfc_high_priority_classes;
	u32 llfc_low_priority_classes;
	/* BRB */
	u32 cos0_pauseable;
	u32 cos1_pauseable;
};

/**
 * Used to update the PFC attributes in EMAC, BMAC, NIG and BRB
 * when link is already up
 */
void elink_update_pfc(struct elink_params *params,
		      struct elink_vars *vars,
		      struct elink_nig_brb_pfc_port_params *pfc_params);


/* Used to configure the ETS to disable */
void elink_ets_disabled(struct elink_params *params);

/* Used to configure the ETS to BW limited */
void elink_ets_bw_limit(const struct elink_params *params,const u32 cos0_bw,
						const u32 cos1_bw);

/* Used to configure the ETS to strict */
u8 elink_ets_strict(const struct elink_params *params,const u8 strict_cos);

/* Read pfc statistic*/
void elink_pfc_statistic(struct elink_params *params, struct elink_vars *vars,
						 u32 pfc_frames_sent[2],
						 u32 pfc_frames_received[2]);

void elink_init_mod_abs_int(struct elink_dev *cb, struct elink_vars *vars,
			    u32 chip_id, u32 shmem_base, u32 shmem2_base,
			    u8 port);
#endif /* __ELINK_H */

