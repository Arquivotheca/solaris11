#ifndef __shmem_h__
#define __shmem_h__

#if !defined(__LITTLE_ENDIAN) && !defined(__BIG_ENDIAN)
#error "Missing either LITTLE_ENDIAN or BIG_ENDIAN definition."
#endif

#define FUNC_0              0
#define FUNC_1              1
#define FUNC_2              2
#define FUNC_3              3
#define FUNC_4              4
#define FUNC_5              5
#define FUNC_6              6
#define FUNC_7              7
#define E1_FUNC_MAX         2
#define E1H_FUNC_MAX            8
#define E2_FUNC_MAX         4   /* per path */

#define VN_0                0
#define VN_1                1
#define VN_2                2
#define VN_3                3
#define E1VN_MAX            1
#define E1HVN_MAX           4

#define E2_VF_MAX           64  // HC_REG_VF_CONFIGURATION_SIZE
/* This value (in milliseconds) determines the frequency of the driver
 * issuing the PULSE message code.  The firmware monitors this periodic
 * pulse to determine when to switch to an OS-absent mode. */
#define DRV_PULSE_PERIOD_MS     250

/* This value (in milliseconds) determines how long the driver should
 * wait for an acknowledgement from the firmware before timing out.  Once
 * the firmware has timed out, the driver will assume there is no firmware
 * running and there won't be any firmware-driver synchronization during a
 * driver reset. */
#define FW_ACK_TIME_OUT_MS      5000

#define FW_ACK_POLL_TIME_MS     1

#define FW_ACK_NUM_OF_POLL  (FW_ACK_TIME_OUT_MS/FW_ACK_POLL_TIME_MS)

/* LED Blink rate that will achieve ~15.9Hz */
#define LED_BLINK_RATE_VAL      480

/****************************************************************************
 * Driver <-> FW Mailbox                                                    *
 ****************************************************************************/
struct drv_port_mb {

    u32 link_status;
    /* Driver should update this field on any link change event */

	#define LINK_STATUS_LINK_FLAG_MASK			0x00000001
	#define LINK_STATUS_LINK_UP				0x00000001
	#define LINK_STATUS_SPEED_AND_DUPLEX_MASK		0x0000001E
	#define LINK_STATUS_SPEED_AND_DUPLEX_AN_NOT_COMPLETE	(0<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_10THD		(1<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_10TFD		(2<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_100TXHD		(3<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_100T4		(4<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_100TXFD		(5<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_1000THD		(6<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_1000TFD		(7<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_1000XFD		(7<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_2500THD		(8<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_2500TFD		(9<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_2500XFD		(9<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_10GTFD		(10<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_10GXFD		(10<<1)
            #define LINK_STATUS_SPEED_AND_DUPLEX_12GTFD     (11<<1)
            #define LINK_STATUS_SPEED_AND_DUPLEX_12GXFD     (11<<1)
            #define LINK_STATUS_SPEED_AND_DUPLEX_12_5GTFD       (12<<1)
            #define LINK_STATUS_SPEED_AND_DUPLEX_12_5GXFD       (12<<1)
            #define LINK_STATUS_SPEED_AND_DUPLEX_13GTFD     (13<<1)
            #define LINK_STATUS_SPEED_AND_DUPLEX_13GXFD     (13<<1)
            #define LINK_STATUS_SPEED_AND_DUPLEX_15GTFD     (14<<1)
            #define LINK_STATUS_SPEED_AND_DUPLEX_15GXFD     (14<<1)
            #define LINK_STATUS_SPEED_AND_DUPLEX_16GTFD     (15<<1)
            #define LINK_STATUS_SPEED_AND_DUPLEX_16GXFD     (15<<1)

	#define LINK_STATUS_AUTO_NEGOTIATE_FLAG_MASK        0x00000020
	#define LINK_STATUS_AUTO_NEGOTIATE_ENABLED          0x00000020

	#define LINK_STATUS_AUTO_NEGOTIATE_COMPLETE         0x00000040
	#define LINK_STATUS_PARALLEL_DETECTION_FLAG_MASK    0x00000080
	#define LINK_STATUS_PARALLEL_DETECTION_USED         0x00000080

	#define LINK_STATUS_LINK_PARTNER_1000TFD_CAPABLE    0x00000200
	#define LINK_STATUS_LINK_PARTNER_1000THD_CAPABLE    0x00000400
	#define LINK_STATUS_LINK_PARTNER_100T4_CAPABLE      0x00000800
	#define LINK_STATUS_LINK_PARTNER_100TXFD_CAPABLE    0x00001000
	#define LINK_STATUS_LINK_PARTNER_100TXHD_CAPABLE    0x00002000
	#define LINK_STATUS_LINK_PARTNER_10TFD_CAPABLE      0x00004000
	#define LINK_STATUS_LINK_PARTNER_10THD_CAPABLE      0x00008000

	#define LINK_STATUS_TX_FLOW_CONTROL_FLAG_MASK       0x00010000
	    #define LINK_STATUS_TX_FLOW_CONTROL_ENABLED     0x00010000

	#define LINK_STATUS_RX_FLOW_CONTROL_FLAG_MASK       0x00020000
	    #define LINK_STATUS_RX_FLOW_CONTROL_ENABLED     0x00020000

	#define LINK_STATUS_LINK_PARTNER_FLOW_CONTROL_MASK  0x000C0000
	    #define LINK_STATUS_LINK_PARTNER_NOT_PAUSE_CAPABLE  (0<<18)
	    #define LINK_STATUS_LINK_PARTNER_SYMMETRIC_PAUSE    (1<<18)
	    #define LINK_STATUS_LINK_PARTNER_ASYMMETRIC_PAUSE   (2<<18)
	    #define LINK_STATUS_LINK_PARTNER_BOTH_PAUSE     (3<<18)

	#define LINK_STATUS_SERDES_LINK             0x00100000

	#define LINK_STATUS_LINK_PARTNER_2500XFD_CAPABLE	0x00200000
	#define LINK_STATUS_LINK_PARTNER_2500XHD_CAPABLE	0x00400000
	#define LINK_STATUS_LINK_PARTNER_10GXFD_CAPABLE		0x00800000
        #define LINK_STATUS_LINK_PARTNER_12GXFD_CAPABLE     0x01000000
        #define LINK_STATUS_LINK_PARTNER_12_5GXFD_CAPABLE   0x02000000
        #define LINK_STATUS_LINK_PARTNER_13GXFD_CAPABLE     0x04000000
        #define LINK_STATUS_LINK_PARTNER_15GXFD_CAPABLE     0x08000000
        #define LINK_STATUS_LINK_PARTNER_16GXFD_CAPABLE     0x10000000

    u32 port_stx;

    u32 stat_nig_timer;

    /* MCP firmware does not use this field */
    u32 ext_phy_fw_version;

};


struct drv_func_mb {

    u32 drv_mb_header;
	#define DRV_MSG_CODE_MASK                           0xffff0000
	    #define DRV_MSG_CODE_LOAD_REQ                   0x10000000
	    #define DRV_MSG_CODE_LOAD_DONE                  0x11000000
	    #define DRV_MSG_CODE_UNLOAD_REQ_WOL_EN          0x20000000
	    #define DRV_MSG_CODE_UNLOAD_REQ_WOL_DIS         0x20010000
	    #define DRV_MSG_CODE_UNLOAD_REQ_WOL_MCP         0x20020000
	    #define DRV_MSG_CODE_UNLOAD_DONE                0x21000000
	    #define DRV_MSG_CODE_DCC_OK                     0x30000000
	    #define DRV_MSG_CODE_DCC_FAILURE                0x31000000
	    #define DRV_MSG_CODE_DIAG_ENTER_REQ             0x50000000
	    #define DRV_MSG_CODE_DIAG_EXIT_REQ              0x60000000
	    #define DRV_MSG_CODE_VALIDATE_KEY               0x70000000
	    #define DRV_MSG_CODE_GET_CURR_KEY               0x80000000
	    #define DRV_MSG_CODE_GET_UPGRADE_KEY            0x81000000
	    #define DRV_MSG_CODE_GET_MANUF_KEY              0x82000000
	    #define DRV_MSG_CODE_LOAD_L2B_PRAM              0x90000000
    /*
     * The optic module verification command requires bootcode
     * v5.0.6 or later, te specific optic module verification command
	 * requires bootcode v5.2.12 or later
     */
	    #define DRV_MSG_CODE_VRFY_FIRST_PHY_OPT_MDL     0xa0000000
	    #define REQ_BC_VER_4_VRFY_FIRST_PHY_OPT_MDL     0x00050006
	    #define DRV_MSG_CODE_VRFY_SPECIFIC_PHY_OPT_MDL  0xa1000000
	    #define REQ_BC_VER_4_VRFY_SPECIFIC_PHY_OPT_MDL  0x00050234
	    #define DRV_MSG_CODE_VRFY_VNTAG_SUPPORTED       0xa2000000
	    #define REQ_BC_VER_4_VRFY_VNTAG_SUPPORTED       0x00060402

	    #define DRV_MSG_CODE_DCBX_ADMIN_PMF_MSG         0xb0000000
	    #define DRV_MSG_CODE_DCBX_PMF_DRV_OK            0xb2000000

	    #define DRV_MSG_CODE_VF_DISABLED_DONE           0xc0000000

	    #define DRV_MSG_CODE_VNTAG_DRIVER_SETMAC        0xd0000000
	    #define DRV_MSG_CODE_VNTAG_LISTGET_ACK          0xd1000000
	    #define DRV_MSG_CODE_VNTAG_LISTSET_ACK          0xd2000000
	    #define DRV_MSG_CODE_VNTAG_STATSGET_ACK         0xd3000000
	    #define DRV_MSG_CODE_VNTAG_VIFSET_ACK           0xd4000000

			#define DRV_MSG_CODE_SET_MF_BW                  0xe0000000
	    #define REQ_BC_VER_4_SET_MF_BW                  0x00060202
	    #define DRV_MSG_CODE_SET_MF_BW_ACK              0xe1000000

	    #define DRV_MSG_CODE_LINK_STATUS_CHANGED        0x01000000

	    #define BIOS_MSG_CODE_LIC_CHALLENGE         0xff010000
	    #define BIOS_MSG_CODE_LIC_RESPONSE          0xff020000
	    #define BIOS_MSG_CODE_VIRT_MAC_PRIM             0xff030000
	    #define BIOS_MSG_CODE_VIRT_MAC_ISCSI            0xff040000

	#define DRV_MSG_SEQ_NUMBER_MASK             0x0000ffff

    u32 drv_mb_param;
	    #define DRV_MSG_CODE_SET_MF_BW_MIN_MASK         0x00ff0000
	    #define DRV_MSG_CODE_SET_MF_BW_MAX_MASK         0xff000000

    u32 fw_mb_header;
	#define FW_MSG_CODE_MASK                0xffff0000
	    #define FW_MSG_CODE_DRV_LOAD_COMMON         0x10100000
	    #define FW_MSG_CODE_DRV_LOAD_PORT           0x10110000
	    #define FW_MSG_CODE_DRV_LOAD_FUNCTION           0x10120000
	/* Load common chip is supported from bc 6.0.0  */
	    #define REQ_BC_VER_4_DRV_LOAD_COMMON_CHIP       0x00060000
	    #define FW_MSG_CODE_DRV_LOAD_COMMON_CHIP        0x10130000

	    #define FW_MSG_CODE_DRV_LOAD_REFUSED            0x10200000
	    #define FW_MSG_CODE_DRV_LOAD_DONE           0x11100000
	    #define FW_MSG_CODE_DRV_UNLOAD_COMMON           0x20100000
	    #define FW_MSG_CODE_DRV_UNLOAD_PORT         0x20110000
	    #define FW_MSG_CODE_DRV_UNLOAD_FUNCTION         0x20120000
	    #define FW_MSG_CODE_DRV_UNLOAD_DONE         0x21100000
	    #define FW_MSG_CODE_DCC_DONE                0x30100000
	    #define FW_MSG_CODE_LLDP_DONE               0x40100000
	    #define FW_MSG_CODE_DIAG_ENTER_DONE         0x50100000
	    #define FW_MSG_CODE_DIAG_REFUSE             0x50200000
	    #define FW_MSG_CODE_DIAG_EXIT_DONE          0x60100000
	    #define FW_MSG_CODE_VALIDATE_KEY_SUCCESS        0x70100000
	    #define FW_MSG_CODE_VALIDATE_KEY_FAILURE        0x70200000
	    #define FW_MSG_CODE_GET_KEY_DONE            0x80100000
	    #define FW_MSG_CODE_NO_KEY              0x80f00000
	    #define FW_MSG_CODE_LIC_INFO_NOT_READY          0x80f80000
	    #define FW_MSG_CODE_L2B_PRAM_LOADED         0x90100000
	    #define FW_MSG_CODE_L2B_PRAM_T_LOAD_FAILURE     0x90210000
	    #define FW_MSG_CODE_L2B_PRAM_C_LOAD_FAILURE     0x90220000
	    #define FW_MSG_CODE_L2B_PRAM_X_LOAD_FAILURE     0x90230000
	    #define FW_MSG_CODE_L2B_PRAM_U_LOAD_FAILURE     0x90240000
	    #define FW_MSG_CODE_VRFY_OPT_MDL_SUCCESS        0xa0100000
	    #define FW_MSG_CODE_VRFY_OPT_MDL_INVLD_IMG      0xa0200000
	    #define FW_MSG_CODE_VRFY_OPT_MDL_UNAPPROVED     0xa0300000
	    #define FW_MSG_CODE_VF_DISABLED_DONE            0xb0000000

	    #define FW_MSG_CODE_VNTAG_DRIVER_SETMAC_DONE    0xd0100000
	    #define FW_MSG_CODE_VNTAG_LISTGET_ACK           0xd1100000
	    #define FW_MSG_CODE_VNTAG_LISTSET_ACK           0xd2100000
	    #define FW_MSG_CODE_VNTAG_STATSGET_ACK          0xd3100000
	    #define FW_MSG_CODE_VNTAG_VIFSET_ACK            0xd4100000

			#define FW_MSG_CODE_SET_MF_BW_SENT              0xe0000000
	    #define FW_MSG_CODE_SET_MF_BW_DONE              0xe1000000

	    #define FW_MSG_CODE_LINK_CHANGED_ACK            0x01100000

	    #define FW_MSG_CODE_LIC_CHALLENGE           0xff010000
	    #define FW_MSG_CODE_LIC_RESPONSE            0xff020000
	    #define FW_MSG_CODE_VIRT_MAC_PRIM           0xff030000
	    #define FW_MSG_CODE_VIRT_MAC_ISCSI          0xff040000

	#define FW_MSG_SEQ_NUMBER_MASK              0x0000ffff

    u32 fw_mb_param;

    u32 drv_pulse_mb;
	#define DRV_PULSE_SEQ_MASK              0x00007fff
	#define DRV_PULSE_SYSTEM_TIME_MASK          0xffff0000
    /* The system time is in the format of
     * (year-2001)*12*32 + month*32 + day. */
	#define DRV_PULSE_ALWAYS_ALIVE              0x00008000
    /* Indicate to the firmware not to go into the
     * OS-absent when it is not getting driver pulse.
     * This is used for debugging as well for PXE(MBA). */

    u32 mcp_pulse_mb;
	#define MCP_PULSE_SEQ_MASK              0x00007fff
	#define MCP_PULSE_ALWAYS_ALIVE              0x00008000
    /* Indicates to the driver not to assert due to lack
     * of MCP response */
	#define MCP_EVENT_MASK                  0xffff0000
	    #define MCP_EVENT_OTHER_DRIVER_RESET_REQ        0x00010000

    u32 iscsi_boot_signature;
    u32 iscsi_boot_block_offset;

    u32 drv_status;
	#define DRV_STATUS_PMF                      0x00000001
	#define DRV_STATUS_VF_DISABLED              0x00000002
	#define DRV_STATUS_SET_MF_BW                0x00000004

	#define DRV_STATUS_DCC_EVENT_MASK           0x0000ff00
	    #define DRV_STATUS_DCC_DISABLE_ENABLE_PF        0x00000100
	    #define DRV_STATUS_DCC_BANDWIDTH_ALLOCATION     0x00000200
	    #define DRV_STATUS_DCC_CHANGE_MAC_ADDRESS       0x00000400
	    #define DRV_STATUS_DCC_RESERVED1                0x00000800
	    #define DRV_STATUS_DCC_SET_PROTOCOL             0x00001000
	    #define DRV_STATUS_DCC_SET_PRIORITY         0x00002000

	#define DRV_STATUS_DCBX_EVENT_MASK          0x000f0000
	    #define DRV_STATUS_DCBX_NEGOTIATION_RESULTS     0x00010000
	#define DRV_STATUS_VNTAG_EVENT_MASK         0x03f00000
	    #define DRV_STATUS_VNTAG_LISTGET_REQ            0x00100000
	    #define DRV_STATUS_VNTAG_LISTSET_REQ            0x00200000
	    #define DRV_STATUS_VNTAG_STATSGET_REQ           0x00400000
	    #define DRV_STATUS_VNTAG_VIFSET_REQ             0x00800000

    u32 virt_mac_upper;
	#define VIRT_MAC_SIGN_MASK              0xffff0000
	    #define VIRT_MAC_SIGNATURE              0x564d0000
    u32 virt_mac_lower;

};


/****************************************************************************
 * Management firmware state                                                *
 ****************************************************************************/
/* Allocate 440 bytes for management firmware */
#define MGMTFW_STATE_WORD_SIZE                              110

struct mgmtfw_state {
    u32 opaque[MGMTFW_STATE_WORD_SIZE];
};


/****************************************************************************
 * Multi-Function configuration                                             *
 ****************************************************************************/
struct shared_mf_cfg {

    u32 clp_mb;
	    #define SHARED_MF_CLP_SET_DEFAULT                   0x00000000
    /* set by CLP */
	    #define SHARED_MF_CLP_EXIT                          0x00000001
    /* set by MCP */
	    #define SHARED_MF_CLP_EXIT_DONE                     0x00010000

};

struct port_mf_cfg {

    u32 dynamic_cfg;    /* device control channel */
	#define PORT_MF_CFG_E1HOV_TAG_MASK                  0x0000ffff
	#define PORT_MF_CFG_E1HOV_TAG_SHIFT                 0
	#define PORT_MF_CFG_E1HOV_TAG_DEFAULT               PORT_MF_CFG_E1HOV_TAG_MASK

    u32 reserved[3];

};

struct func_mf_cfg {

    u32 config;
    /* E/R/I/D */
    /* function 0 of each port cannot be hidden */
	#define FUNC_MF_CFG_FUNC_HIDE                       0x00000001

	#define FUNC_MF_CFG_PROTOCOL_MASK                   0x00000006
	    #define FUNC_MF_CFG_PROTOCOL_FCOE                   0x00000000
	    #define FUNC_MF_CFG_PROTOCOL_ETHERNET               0x00000002
	    #define FUNC_MF_CFG_PROTOCOL_ETHERNET_WITH_RDMA     0x00000004
	    #define FUNC_MF_CFG_PROTOCOL_ISCSI                  0x00000006
	    #define FUNC_MF_CFG_PROTOCOL_DEFAULT                FUNC_MF_CFG_PROTOCOL_ETHERNET_WITH_RDMA

	#define FUNC_MF_CFG_FUNC_DISABLED                   0x00000008
	#define FUNC_MF_CFG_FUNC_DELETED                    0x00000010

    /* PRI */
    /* 0 - low priority, 3 - high priority */
	#define FUNC_MF_CFG_TRANSMIT_PRIORITY_MASK          0x00000300
	#define FUNC_MF_CFG_TRANSMIT_PRIORITY_SHIFT         8
	    #define FUNC_MF_CFG_TRANSMIT_PRIORITY_DEFAULT       0x00000000

    /* MINBW, MAXBW */
    /* value range - 0..100, increments in 100Mbps */
	#define FUNC_MF_CFG_MIN_BW_MASK                     0x00ff0000
	    #define FUNC_MF_CFG_MIN_BW_SHIFT                    16
	    #define FUNC_MF_CFG_MIN_BW_DEFAULT                  0x00000000
	#define FUNC_MF_CFG_MAX_BW_MASK                     0xff000000
	    #define FUNC_MF_CFG_MAX_BW_SHIFT                    24
	    #define FUNC_MF_CFG_MAX_BW_DEFAULT                  0x64000000

    u32 mac_upper;      /* MAC */
	#define FUNC_MF_CFG_UPPERMAC_MASK                   0x0000ffff
	    #define FUNC_MF_CFG_UPPERMAC_SHIFT                  0
	    #define FUNC_MF_CFG_UPPERMAC_DEFAULT                FUNC_MF_CFG_UPPERMAC_MASK
    u32 mac_lower;
	    #define FUNC_MF_CFG_LOWERMAC_DEFAULT                0xffffffff

    u32 e1hov_tag;  /* VNI */
	#define FUNC_MF_CFG_E1HOV_TAG_MASK                  0x0000ffff
	    #define FUNC_MF_CFG_E1HOV_TAG_SHIFT                 0
	    #define FUNC_MF_CFG_E1HOV_TAG_DEFAULT               FUNC_MF_CFG_E1HOV_TAG_MASK

	/* It a VNTAG default VLAN ID - 12 bits */
	#define FUNC_MF_CFG_NIV_VLAN_MASK                   0x0fff0000
		#define FUNC_MF_CFG_NIV_VLAN_SHIFT              16

    u32 niv_config;
	#define FUNC_MF_CFG_NIV_COS_FILTER_MASK             0x000000ff
		#define FUNC_MF_CFG_NIV_COS_FILTER_SHIFT        0
	#define FUNC_MF_CFG_NIV_MBA_ENABLED_MASK            0x0000ff00
		#define FUNC_MF_CFG_NIV_MBA_ENABLED_SHIFT       8
		#define FUNC_MF_CFG_NIV_MBA_ENABLED_VAL     0x00000100

    u32 reserved;

};

/* This structure is not applicable and should not be accessed on 57711 */
struct func_ext_cfg {
    u32 func_cfg;
	#define MACP_FUNC_CFG_FLAGS_MASK                              0x000000FF
	    #define MACP_FUNC_CFG_FLAGS_SHIFT                             0
	    #define MACP_FUNC_CFG_FLAGS_ENABLED                           0x00000001
	    #define MACP_FUNC_CFG_FLAGS_ETHERNET                          0x00000002
	    #define MACP_FUNC_CFG_FLAGS_ISCSI_OFFLOAD                     0x00000004
	    #define MACP_FUNC_CFG_FLAGS_FCOE_OFFLOAD                      0x00000008

    u32 iscsi_mac_addr_upper;
    u32 iscsi_mac_addr_lower;

    u32 fcoe_mac_addr_upper;
    u32 fcoe_mac_addr_lower;

    u32 fcoe_wwn_port_name_upper;
    u32 fcoe_wwn_port_name_lower;

    u32 fcoe_wwn_node_name_upper;
    u32 fcoe_wwn_node_name_lower;

    u32 preserve_data;
        #define MF_FUNC_CFG_PRESERVE_L2_MAC                          (1<<0)
        #define MF_FUNC_CFG_PRESERVE_ISCSI_MAC                       (1<<1)
        #define MF_FUNC_CFG_PRESERVE_FCOE_MAC                        (1<<2)
        #define MF_FUNC_CFG_PRESERVE_FCOE_WWN_P                      (1<<3)
        #define MF_FUNC_CFG_PRESERVE_FCOE_WWN_N                      (1<<4)
};

struct mf_cfg {

    struct shared_mf_cfg    shared_mf_config;       /* 0x4 */
    struct port_mf_cfg  port_mf_config[PORT_MAX];   /* 0x10 * 2 = 0x20 */
    /* for all chips, there are 8 mf functions */
    struct func_mf_cfg  func_mf_config[E1H_FUNC_MAX];   /* 0x18 * 8 = 0xc0 */
    /* Extended configuration per function  - this array does not exist and
     * should not be accessed on 57711 */
    struct func_ext_cfg func_ext_config[E1H_FUNC_MAX];  /* 0x28 * 8 = 0x140*/
}; /* 0x224 */

/****************************************************************************
 * Shared Memory Region                                                     *
 ****************************************************************************/
struct shmem_region {                  /*   SharedMem Offset (size) */

    u32         validity_map[PORT_MAX];  /* 0x0 (4*2 = 0x8) */
	#define SHR_MEM_FORMAT_REV_MASK                     0xff000000
	    #define SHR_MEM_FORMAT_REV_ID                       ('A'<<24)
    /* validity bits */
	#define SHR_MEM_VALIDITY_PCI_CFG                    0x00100000
	#define SHR_MEM_VALIDITY_MB                         0x00200000
	#define SHR_MEM_VALIDITY_DEV_INFO                   0x00400000
	#define SHR_MEM_VALIDITY_RESERVED                   0x00000007
    /* One licensing bit should be set */
	#define SHR_MEM_VALIDITY_LIC_KEY_IN_EFFECT_MASK     0x00000038
	    #define SHR_MEM_VALIDITY_LIC_MANUF_KEY_IN_EFFECT    0x00000008
	    #define SHR_MEM_VALIDITY_LIC_UPGRADE_KEY_IN_EFFECT  0x00000010
	    #define SHR_MEM_VALIDITY_LIC_NO_KEY_IN_EFFECT       0x00000020
	/* Active MFW */
	    #define SHR_MEM_VALIDITY_ACTIVE_MFW_UNKNOWN         0x00000000
	#define SHR_MEM_VALIDITY_ACTIVE_MFW_MASK            0x000001c0
	    #define SHR_MEM_VALIDITY_ACTIVE_MFW_IPMI            0x00000040
	    #define SHR_MEM_VALIDITY_ACTIVE_MFW_UMP             0x00000080
	    #define SHR_MEM_VALIDITY_ACTIVE_MFW_NCSI            0x000000c0
	    #define SHR_MEM_VALIDITY_ACTIVE_MFW_NONE            0x000001c0

    struct shm_dev_info dev_info;        /* 0x8     (0x438) */

    license_key_t       drv_lic_key[PORT_MAX]; /* 0x440 (52*2=0x68) */

    /* FW information (for internal FW use) */
    u32         fw_info_fio_offset;    /* 0x4a8       (0x4) */
    struct mgmtfw_state mgmtfw_state;          /* 0x4ac     (0x1b8) */

    struct drv_port_mb  port_mb[PORT_MAX];     /* 0x664 (16*2=0x20) */

#ifdef BMAPI
    /* This is a variable length array */
    /* the number of function depends on the chip type */
    struct drv_func_mb func_mb[1];             /* 0x684
						(44*2/4/8=0x58/0xb0/0x160) */
#else
    /* the number of function depends on the chip type */
    struct drv_func_mb  func_mb[];             /* 0x684
					     (44*2/4/8=0x58/0xb0/0x160) */
#endif /* BMAPI */

}; /* 57710 = 0x6dc | 57711 = 0x7E4 | 57712 = 0x734 */

/****************************************************************************
 * Shared Memory 2 Region                                                   *
 ****************************************************************************/
/* The fw_flr_ack is actually built in the following way:                   */
/* 8 bit:  PF ack                                                           */
/* 64 bit: VF ack                                                           */
/* 8 bit:  ios_dis_ack                                                      */
/* In order to maintain endianity in the mailbox hsi, we want to keep using */
/* u32. The fw must have the VF right after the PF since this is how it     */
/* access arrays(it expects always the VF to reside after the PF, and that  */
/* makes the calculation much easier for it. )                              */
/* In order to answer both limitations, and keep the struct small, the code */
/* will abuse the structure defined here to achieve the actual partition    */
/* above                                                                    */
/****************************************************************************/
struct fw_flr_ack {
    u32         pf_ack;
    u32         vf_ack[1];
    u32         iov_dis_ack;
};

struct fw_flr_mb {
    u32         aggint;
    u32         opgen_addr;
    struct fw_flr_ack ack;
};

/**** SUPPORT FOR SHMEM ARRRAYS ***
 * The SHMEM HSI is aligned on 32 bit boundaries which makes it difficult to
 * define arrays with storage types smaller then unsigned dwords.
 * The macros below add generic support for SHMEM arrays with numeric elements
 * that can span 2,4,8 or 16 bits. The array underlying type is a 32 bit dword
 * array with individual bit-filed elements accessed using shifts and masks.
 *
 */

/* eb is the bitwidth of a single element */
#define SHMEM_ARRAY_MASK(eb)		((1<<(eb))-1)
#define SHMEM_ARRAY_ENTRY(i, eb)	((i)/(32/(eb)))

/* the bit-position macro allows the used to flip the order of the arrays
 * elements on a per byte or word boundary.
 *
 * example: an array with 8 entries each 4 bit wide. This array will fit into
 * a single dword. The diagrmas below show the array order of the nibbles.
 *
 * SHMEM_ARRAY_BITPOS(i, 4, 4) defines the stadard ordering:
 *
 * 		|		|		|		|
 *   0	|   1	|   2	|   3   |   4  	|   5	|   6   |   7	|
 * 		|		|		|		|
 *
 * SHMEM_ARRAY_BITPOS(i, 4, 8) defines a flip ordering per byte:
 *
 * 		|		|		|		|
 *   1	|   0	|   3	|   2   |   5  	|   4	|   7   |   6	|
 * 		|		|		|		|
 *
 * SHMEM_ARRAY_BITPOS(i, 4, 16) defines a flip ordering per word:
 *
 * 		|		|		|		|
 *   3	|   2	|   1	|   0   |   7  	|   6   |   5   |   4	|
 * 		|		|		|		|
 */
#define SHMEM_ARRAY_BITPOS(i, eb, fb)	\
	((((32/(fb)) - 1 - ((i)/((fb)/(eb))) % (32/(fb))) * (fb)) + \
	(((i)%((fb)/(eb))) * (eb)))

#define SHMEM_ARRAY_GET(a, i, eb, fb) 					   \
	((a[SHMEM_ARRAY_ENTRY(i, eb)] >> SHMEM_ARRAY_BITPOS(i, eb, fb)) &  \
	SHMEM_ARRAY_MASK(eb))

#define SHMEM_ARRAY_SET(a, i, eb, fb, val) 				   \
do {									   \
	a[SHMEM_ARRAY_ENTRY(i, eb)] &= ~(SHMEM_ARRAY_MASK(eb) << 	   \
	SHMEM_ARRAY_BITPOS(i, eb, fb));					   \
	a[SHMEM_ARRAY_ENTRY(i, eb)] |= (((val) & SHMEM_ARRAY_MASK(eb)) <<  \
	SHMEM_ARRAY_BITPOS(i, eb, fb));					   \
} while (0)



/****START OF DCBX STRUCTURES DECLARATIONS****/
#define DCBX_MAX_NUM_PRI_PG_ENTRIES  	8
#define DCBX_PRI_PG_BITWIDTH		4
#define DCBX_PRI_PG_FBITS		8
#define DCBX_PRI_PG_GET(a, i)		\
	SHMEM_ARRAY_GET(a, i, DCBX_PRI_PG_BITWIDTH, DCBX_PRI_PG_FBITS)
#define DCBX_PRI_PG_SET(a, i, val)	\
	SHMEM_ARRAY_SET(a, i, DCBX_PRI_PG_BITWIDTH, DCBX_PRI_PG_FBITS, val)
#define DCBX_MAX_NUM_PG_BW_ENTRIES	8
#define DCBX_BW_PG_BITWIDTH  		8
#define DCBX_PG_BW_GET(a, i)		\
	SHMEM_ARRAY_GET(a, i, DCBX_BW_PG_BITWIDTH, DCBX_BW_PG_BITWIDTH)
#define DCBX_PG_BW_SET(a, i, val)	\
	SHMEM_ARRAY_SET(a, i, DCBX_BW_PG_BITWIDTH, DCBX_BW_PG_BITWIDTH, val)
#define DCBX_STRICT_PRI_PG           	15
#define DCBX_MAX_APP_PROTOCOL        	16
#define FCOE_APP_IDX            	0
#define ISCSI_APP_IDX           	1
#define PREDEFINED_APP_IDX_MAX  	2


// Big/Little endian have the same representation.
struct dcbx_ets_feature{
    u32 enabled;            // For Admin MIB - is this feature supported by the driver | For Local MIB - should this feature be enabled.
    u32  pg_bw_tbl[2];
    u32  pri_pg_tbl[1];
};

// Driver structure in LE
struct dcbx_pfc_feature {
#ifdef __BIG_ENDIAN
    u8 pri_en_bitmap;
	#define DCBX_PFC_PRI_0 0x01
	#define DCBX_PFC_PRI_1 0x02
	#define DCBX_PFC_PRI_2 0x04
	#define DCBX_PFC_PRI_3 0x08
	#define DCBX_PFC_PRI_4 0x10
	#define DCBX_PFC_PRI_5 0x20
	#define DCBX_PFC_PRI_6 0x40
	#define DCBX_PFC_PRI_7 0x80
    u8 pfc_caps;
    u8 reserved;
    u8 enabled;
#elif defined(__LITTLE_ENDIAN)
    u8 enabled;
    u8 reserved;
    u8 pfc_caps;
    u8 pri_en_bitmap;
	#define DCBX_PFC_PRI_0 0x01
	#define DCBX_PFC_PRI_1 0x02
	#define DCBX_PFC_PRI_2 0x04
	#define DCBX_PFC_PRI_3 0x08
	#define DCBX_PFC_PRI_4 0x10
	#define DCBX_PFC_PRI_5 0x20
	#define DCBX_PFC_PRI_6 0x40
	#define DCBX_PFC_PRI_7 0x80
#endif
};

struct dcbx_app_priority_entry {
#ifdef __BIG_ENDIAN
    u16  app_id;
    u8  pri_bitmap;
    u8  appBitfield;
	#define DCBX_APP_ENTRY_VALID         0x01
	#define DCBX_APP_ENTRY_SF_MASK       0x30
	#define DCBX_APP_ENTRY_SF_SHIFT      4
	#define DCBX_APP_SF_ETH_TYPE         0x10
	#define DCBX_APP_SF_PORT             0x20
#elif defined(__LITTLE_ENDIAN)
    u8 appBitfield;
	#define DCBX_APP_ENTRY_VALID         0x01
	#define DCBX_APP_ENTRY_SF_MASK       0x30
	#define DCBX_APP_ENTRY_SF_SHIFT      4
	#define DCBX_APP_SF_ETH_TYPE         0x10
	#define DCBX_APP_SF_PORT             0x20
    u8  pri_bitmap;
    u16  app_id;
#endif
};


// FW structure in BE
struct dcbx_app_priority_feature {
#ifdef __BIG_ENDIAN
    u8 reserved;
    u8 default_pri;
    u8 tc_supported;
    u8 enabled;
#elif defined(__LITTLE_ENDIAN)
    u8 enabled;
    u8 tc_supported;
    u8 default_pri;
    u8 reserved;
#endif
    struct dcbx_app_priority_entry  app_pri_tbl[DCBX_MAX_APP_PROTOCOL];
};

// FW structure in BE
struct dcbx_features {
    // PG feature.
    struct dcbx_ets_feature ets;
    // PFC feature.
    struct dcbx_pfc_feature pfc;
    // APP feature.
    struct dcbx_app_priority_feature app;
};

// LLDP protocol parameters.
// FW structure in BE
struct lldp_params {
#ifdef __BIG_ENDIAN
    u8  msg_fast_tx_interval;
    u8  msg_tx_hold;
    u8  msg_tx_interval;
    u8  admin_status;
	    #define LLDP_TX_ONLY  0x01
	    #define LLDP_RX_ONLY  0x02
	    #define LLDP_TX_RX    0x03
	    #define LLDP_DISABLED 0x04
    u8  reserved1;
    u8  tx_fast;
    u8  tx_crd_max;
    u8  tx_crd;
#elif defined(__LITTLE_ENDIAN)
    u8  admin_status;
	    #define LLDP_TX_ONLY  0x01
	    #define LLDP_RX_ONLY  0x02
	    #define LLDP_TX_RX    0x03
	    #define LLDP_DISABLED 0x04
    u8  msg_tx_interval;
    u8  msg_tx_hold;
    u8  msg_fast_tx_interval;
    u8  tx_crd;
    u8  tx_crd_max;
    u8  tx_fast;
    u8  reserved1;
#endif
		#define REM_CHASSIS_ID_STAT_LEN 4
		#define REM_PORT_ID_STAT_LEN 4
    u32 peer_chassis_id[REM_CHASSIS_ID_STAT_LEN];		// Holds remote Chassis ID TLV header, subtype and 9B of payload.
    u32 peer_port_id[REM_PORT_ID_STAT_LEN];			// Holds remote Port ID TLV header, subtype and 9B of payload.
};

struct lldp_dcbx_stat {
		#define LOCAL_CHASSIS_ID_STAT_LEN 2
		#define LOCAL_PORT_ID_STAT_LEN 2
	u32 local_chassis_id[LOCAL_CHASSIS_ID_STAT_LEN];	// Holds local Chassis ID 8B payload of constant subtype 4.
	u32 local_port_id[LOCAL_PORT_ID_STAT_LEN];			// Holds local Port ID 8B payload of constant subtype 3.
	u32 num_tx_dcbx_pkts;		// Number of DCBX frames transmitted.
	u32 num_rx_dcbx_pkts;		// Number of DCBX frames received.
};

// ADMIN MIB - DCBX local machine default configuration.
struct lldp_admin_mib {
    u32     ver_cfg_flags;
	#define DCBX_ETS_CONFIG_TX_ENABLED      0x00000001
	#define DCBX_PFC_CONFIG_TX_ENABLED      0x00000002
	#define DCBX_APP_CONFIG_TX_ENABLED      0x00000004
	#define DCBX_ETS_RECO_TX_ENABLED        0x00000008
	#define DCBX_ETS_RECO_VALID             0x00000010
	#define DCBX_ETS_WILLING                0x00000020
	#define DCBX_PFC_WILLING                0x00000040
	#define DCBX_APP_WILLING                0x00000080
	#define DCBX_VERSION_CEE                0x00000100
	#define DCBX_VERSION_IEEE               0x00000200
	#define DCBX_DCBX_ENABLED               0x00000400
	#define DCBX_CEE_VERSION_MASK           0x0000f000
	#define DCBX_CEE_VERSION_SHIFT          12
	#define DCBX_CEE_MAX_VERSION_MASK       0x000f0000
	#define DCBX_CEE_MAX_VERSION_SHIFT      16
    struct dcbx_features     features;
};

// REMOTE MIB - remote machine DCBX configuration.
struct lldp_remote_mib {
    u32 prefix_seq_num;
    u32 flags;
	#define DCBX_ETS_TLV_RX     0x00000001
	#define DCBX_PFC_TLV_RX     0x00000002
	#define DCBX_APP_TLV_RX     0x00000004
	#define DCBX_ETS_RX_ERROR   0x00000010
	#define DCBX_PFC_RX_ERROR   0x00000020
	#define DCBX_APP_RX_ERROR   0x00000040
	#define DCBX_ETS_REM_WILLING    0x00000100
	#define DCBX_PFC_REM_WILLING    0x00000200
	#define DCBX_APP_REM_WILLING    0x00000400
	#define DCBX_REMOTE_ETS_RECO_VALID  0x00001000
    struct dcbx_features features;
    u32 suffix_seq_num;
};

// LOCAL MIB - operational DCBX configuration - transmitted on Tx LLDPDU.
struct lldp_local_mib {
    u32 prefix_seq_num;
    u32 error;              // Indicates if there is mismatch with negotiation results.
	#define DCBX_LOCAL_ETS_ERROR     0x00000001
	#define DCBX_LOCAL_PFC_ERROR     0x00000002
	#define DCBX_LOCAL_APP_ERROR     0x00000004
	#define DCBX_LOCAL_PFC_MISMATCH  0x00000010
	#define DCBX_LOCAL_APP_MISMATCH  0x00000020
    struct dcbx_features   features;
    u32 suffix_seq_num;
};
/***END OF DCBX STRUCTURES DECLARATIONS***/

struct shmem2_region {

    u32         size; 				/* 0x0000 */

    u32         dcc_support; 			/* 0x0004 */
	#define SHMEM_DCC_SUPPORT_NONE                      0x00000000
	#define SHMEM_DCC_SUPPORT_DISABLE_ENABLE_PF_TLV     0x00000001
	#define SHMEM_DCC_SUPPORT_BANDWIDTH_ALLOCATION_TLV  0x00000004
	#define SHMEM_DCC_SUPPORT_CHANGE_MAC_ADDRESS_TLV    0x00000008
	#define SHMEM_DCC_SUPPORT_SET_PROTOCOL_TLV          0x00000040
	#define SHMEM_DCC_SUPPORT_SET_PRIORITY_TLV          0x00000080

    u32 ext_phy_fw_version2[PORT_MAX];		/* 0x0008 */
    /*
     * For backwards compatibility, if the mf_cfg_addr does not exist
     * (the size filed is smaller than 0xc) the mf_cfg resides at the
     * end of struct shmem_region
     */
    u32         mf_cfg_addr;			/* 0x0010 */
	    #define SHMEM_MF_CFG_ADDR_NONE                      0x00000000

    struct fw_flr_mb flr_mb;			/* 0x0014 */
    u32         dcbx_lldp_params_offset;	/* 0x0028 */
	    #define SHMEM_LLDP_DCBX_PARAMS_NONE                 0x00000000
    u32         dcbx_neg_res_offset;		/* 0x002c */
	    #define SHMEM_DCBX_NEG_RES_NONE                     0x00000000
    u32         dcbx_remote_mib_offset;		/* 0x0030 */
	    #define SHMEM_DCBX_REMOTE_MIB_NONE                  0x00000000
    /*
     * The other shmemX_base_addr holds the other path's shmem address
     * required for example in case of common phy init, or for path1 to know
     * the address of mcp debug trace which is located in offset from shmem
     * of path0
     */
    u32         other_shmem_base_addr;		/* 0x0034 */
    u32         other_shmem2_base_addr;		/* 0x0038 */
    /*
     * mcp_vf_disabled is set by the MCP to indicate the driver about VFs
     * which were disabled/flred
     */
    u32         mcp_vf_disabled[E2_VF_MAX / 32];/* 0x003c */

    /*
     * drv_ack_vf_disabled is set by the PF driver to ack handled disabled
     * VFs
     */
    u32         drv_ack_vf_disabled[E2_FUNC_MAX][E2_VF_MAX / 32];	/* 0x0044 */

    u32         dcbx_lldp_dcbx_stat_offset;	/* 0x0064 */
	    #define SHMEM_LLDP_DCBX_STAT_NONE                  0x00000000

    /* edebug_driver_if field is used to transfer messages between edebug app
     * to the driver through shmem2.
     *
     * message format:
     * bits 0-2 -  function number / instance of driver to perform request
     * bits 3-5 -  op code / is_ack?
     * bits 6-63 - data
     */
    u32         edebug_driver_if[2];		/* 0x0068 */
	#define EDEBUG_DRIVER_IF_OP_CODE_GET_PHYS_ADDR          1
	#define EDEBUG_DRIVER_IF_OP_CODE_GET_BUS_ADDR           2
	#define EDEBUG_DRIVER_IF_OP_CODE_DISABLE_STAT           3

    u32         nvm_retain_bitmap_addr;		/* 0x0070 */

    /* vntag support of that driver */
    u32         vntag_driver_niv_support;	/* 0x0074 */
	#define SHMEM_NIV_SUPPORTED_VERSION_ONE 0x1001

    /* driver receives addr in scratchpad to which it should respond */
    u32         vntag_scratchpad_addr_to_write[E2_FUNC_MAX];

    /* generic params from MCP to driver (value depends on the msg sent to driver */
    u32         vntag_param1_to_driver[E2_FUNC_MAX];/* 0x0088 */
    u32         vntag_param2_to_driver[E2_FUNC_MAX];/* 0x0098 */

    u32 	    swim_base_addr;			/* 0x0108 */
    u32 	    swim_funcs;
    u32         swim_main_cb;

    /* bitmap notifying which VIF profiles stored in nvram are enabled by switch */
    u32         vntag_profiles_enabled[2];

    /* generic flags controlled by the driver */
    u32         drv_flags;
    #define DRV_FLAGS_DCB_CONFIGURED 0x1
};

#endif
