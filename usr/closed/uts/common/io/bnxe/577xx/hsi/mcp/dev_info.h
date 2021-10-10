#ifndef __devinfo_h__
#define __devinfo_h__

#define PORT_0              0
#define PORT_1              1
#define PORT_MAX            2

/****************************************************************************
 * Shared HW configuration                                                  *
 ****************************************************************************/
 #define PIN_CFG_NA                          0x00000000
 #define PIN_CFG_GPIO0_P0                    0x00000001
 #define PIN_CFG_GPIO1_P0                    0x00000002
 #define PIN_CFG_GPIO2_P0                    0x00000003
 #define PIN_CFG_GPIO3_P0                    0x00000004
 #define PIN_CFG_GPIO0_P1                    0x00000005
 #define PIN_CFG_GPIO1_P1                    0x00000006
 #define PIN_CFG_GPIO2_P1                    0x00000007
 #define PIN_CFG_GPIO3_P1                    0x00000008
 #define PIN_CFG_EPIO0                       0x00000009
 #define PIN_CFG_EPIO1                       0x0000000a
 #define PIN_CFG_EPIO2                       0x0000000b
 #define PIN_CFG_EPIO3                       0x0000000c
 #define PIN_CFG_EPIO4                       0x0000000d
 #define PIN_CFG_EPIO5                       0x0000000e
 #define PIN_CFG_EPIO6                       0x0000000f
 #define PIN_CFG_EPIO7                       0x00000010
 #define PIN_CFG_EPIO8                       0x00000011
 #define PIN_CFG_EPIO9                       0x00000012
 #define PIN_CFG_EPIO10                      0x00000013
 #define PIN_CFG_EPIO11                      0x00000014
 #define PIN_CFG_EPIO12                      0x00000015
 #define PIN_CFG_EPIO13                      0x00000016
 #define PIN_CFG_EPIO14                      0x00000017
 #define PIN_CFG_EPIO15                      0x00000018
 #define PIN_CFG_EPIO16                      0x00000019
 #define PIN_CFG_EPIO17                      0x0000001a
 #define PIN_CFG_EPIO18                      0x0000001b
 #define PIN_CFG_EPIO19                      0x0000001c
 #define PIN_CFG_EPIO20                      0x0000001d
 #define PIN_CFG_EPIO21                      0x0000001e
 #define PIN_CFG_EPIO22                      0x0000001f
 #define PIN_CFG_EPIO23                      0x00000020
 #define PIN_CFG_EPIO24                      0x00000021
 #define PIN_CFG_EPIO25                      0x00000022
 #define PIN_CFG_EPIO26                      0x00000023
 #define PIN_CFG_EPIO27                      0x00000024
 #define PIN_CFG_EPIO28                      0x00000025
 #define PIN_CFG_EPIO29                      0x00000026
 #define PIN_CFG_EPIO30                      0x00000027
 #define PIN_CFG_EPIO31                      0x00000028
	
struct shared_hw_cfg {                   /* NVRAM Offset */
    /* Up to 16 bytes of NULL-terminated string */
    u8  part_num[16];                   /* 0x104 */

    u32 config;                     /* 0x114 */
        #define SHARED_HW_CFG_MDIO_VOLTAGE_MASK             0x00000001
            #define SHARED_HW_CFG_MDIO_VOLTAGE_SHIFT            0
            #define SHARED_HW_CFG_MDIO_VOLTAGE_1_2V             0x00000000
            #define SHARED_HW_CFG_MDIO_VOLTAGE_2_5V             0x00000001
        #define SHARED_HW_CFG_MCP_RST_ON_CORE_RST_EN        0x00000002

        #define SHARED_HW_CFG_PORT_SWAP                     0x00000004

        #define SHARED_HW_CFG_BEACON_WOL_EN                 0x00000008

	#define SHARED_HW_CFG_PCIE_GEN3_DISABLED            0x00000000
	#define SHARED_HW_CFG_PCIE_GEN3_ENABLED             0x00000010

        #define SHARED_HW_CFG_MFW_SELECT_MASK               0x00000700
            #define SHARED_HW_CFG_MFW_SELECT_SHIFT              8
        /* Whatever MFW found in NVM
           (if multiple found, priority order is: NC-SI, UMP, IPMI) */
            #define SHARED_HW_CFG_MFW_SELECT_DEFAULT            0x00000000
            #define SHARED_HW_CFG_MFW_SELECT_NC_SI              0x00000100
            #define SHARED_HW_CFG_MFW_SELECT_UMP                0x00000200
            #define SHARED_HW_CFG_MFW_SELECT_IPMI               0x00000300
        /* Use SPIO4 as an arbiter between: 0-NC_SI, 1-IPMI
          (can only be used when an add-in board, not BMC, pulls-down SPIO4) */
            #define SHARED_HW_CFG_MFW_SELECT_SPIO4_NC_SI_IPMI   0x00000400
        /* Use SPIO4 as an arbiter between: 0-UMP, 1-IPMI
          (can only be used when an add-in board, not BMC, pulls-down SPIO4) */
            #define SHARED_HW_CFG_MFW_SELECT_SPIO4_UMP_IPMI     0x00000500
        /* Use SPIO4 as an arbiter between: 0-NC-SI, 1-UMP
          (can only be used when an add-in board, not BMC, pulls-down SPIO4) */
            #define SHARED_HW_CFG_MFW_SELECT_SPIO4_NC_SI_UMP    0x00000600

        #define SHARED_HW_CFG_LED_MODE_MASK                 0x000f0000
            #define SHARED_HW_CFG_LED_MODE_SHIFT                16
            #define SHARED_HW_CFG_LED_MAC1                      0x00000000
            #define SHARED_HW_CFG_LED_PHY1                      0x00010000
            #define SHARED_HW_CFG_LED_PHY2                      0x00020000
            #define SHARED_HW_CFG_LED_PHY3                      0x00030000
            #define SHARED_HW_CFG_LED_MAC2                      0x00040000
            #define SHARED_HW_CFG_LED_PHY4                      0x00050000
            #define SHARED_HW_CFG_LED_PHY5                      0x00060000
            #define SHARED_HW_CFG_LED_PHY6                      0x00070000
            #define SHARED_HW_CFG_LED_MAC3                      0x00080000
            #define SHARED_HW_CFG_LED_PHY7                      0x00090000
            #define SHARED_HW_CFG_LED_PHY9                      0x000a0000
            #define SHARED_HW_CFG_LED_PHY11                     0x000b0000
            #define SHARED_HW_CFG_LED_MAC4                      0x000c0000
            #define SHARED_HW_CFG_LED_PHY8                      0x000d0000
            #define SHARED_HW_CFG_LED_EXTPHY1                   0x000e0000


        #define SHARED_HW_CFG_AN_ENABLE_MASK                0x3f000000
            #define SHARED_HW_CFG_AN_ENABLE_SHIFT               24
            #define SHARED_HW_CFG_AN_ENABLE_CL37                0x01000000
            #define SHARED_HW_CFG_AN_ENABLE_CL73                0x02000000
            #define SHARED_HW_CFG_AN_ENABLE_BAM                 0x04000000
            #define SHARED_HW_CFG_AN_ENABLE_PARALLEL_DETECTION  0x08000000
            #define SHARED_HW_CFG_AN_EN_SGMII_FIBER_AUTO_DETECT 0x10000000
            #define SHARED_HW_CFG_AN_ENABLE_REMOTE_PHY          0x20000000

        #define SHARED_HW_CFG_SRIOV_MASK                 0x40000000
            #define SHARED_HW_CFG_SRIOV_DISABLED                0x00000000
            #define SHARED_HW_CFG_SRIOV_ENABLED                 0x40000000

        #define SHARED_HW_CFG_ATC_MASK                   0x80000000
            #define SHARED_HW_CFG_ATC_DISABLED                  0x00000000
            #define SHARED_HW_CFG_ATC_ENABLED                   0x80000000

    u32 config2;                        /* 0x118 */
    /* one time auto detect grace period (in sec) */
        #define SHARED_HW_CFG_GRACE_PERIOD_MASK             0x000000ff
        #define SHARED_HW_CFG_GRACE_PERIOD_SHIFT            0

        #define SHARED_HW_CFG_PCIE_GEN2_ENABLED             0x00000100
            #define SHARED_HW_CFG_PCIE_GEN2_DISABLED            0x00000000

    /* The default value for the core clock is 250MHz and it is
       achieved by setting the clock change to 4 */
        #define SHARED_HW_CFG_CLOCK_CHANGE_MASK             0x00000e00
        #define SHARED_HW_CFG_CLOCK_CHANGE_SHIFT            9

        #define SHARED_HW_CFG_SMBUS_TIMING_MASK             0x00001000
            #define SHARED_HW_CFG_SMBUS_TIMING_100KHZ           0x00000000
            #define SHARED_HW_CFG_SMBUS_TIMING_400KHZ           0x00001000

        #define SHARED_HW_CFG_HIDE_PORT1                    0x00002000

        #define SHARED_HW_CFG_WOL_CAPABLE_MASK           0x00004000
            #define SHARED_HW_CFG_WOL_CAPABLE_DISABLED          0x00000000
            #define SHARED_HW_CFG_WOL_CAPABLE_ENABLED           0x00004000

    /* Output low when PERST is asserted */
        #define SHARED_HW_CFG_SPIO4_FOLLOW_PERST_MASK    0x00008000
            #define SHARED_HW_CFG_SPIO4_FOLLOW_PERST_DISABLED   0x00000000
            #define SHARED_HW_CFG_SPIO4_FOLLOW_PERST_ENABLED    0x00008000

        #define SHARED_HW_CFG_PCIE_GEN2_PREEMPHASIS_MASK    0x00070000
            #define SHARED_HW_CFG_PCIE_GEN2_PREEMPHASIS_SHIFT   16
            #define SHARED_HW_CFG_PCIE_GEN2_PREEMPHASIS_HW      0x00000000
            #define SHARED_HW_CFG_PCIE_GEN2_PREEMPHASIS_0DB     0x00010000 /*    0dB */
            #define SHARED_HW_CFG_PCIE_GEN2_PREEMPHASIS_3_5DB   0x00020000 /* -3.5dB */
            #define SHARED_HW_CFG_PCIE_GEN2_PREEMPHASIS_6_0DB   0x00030000 /* -6.0dB */

    /*  The fan failure mechanism is usually related to the PHY type
          since the power consumption of the board is determined by the PHY.
          Currently, fan is required for most designs with SFX7101, BCM8727
          and BCM8481. If a fan is not required for a board which uses one
          of those PHYs, this field should be set to "Disabled". If a fan is
          required for a different PHY type, this option should be set to
          "Enabled". The fan failure indication is expected on SPIO5 */
        #define SHARED_HW_CFG_FAN_FAILURE_MASK              0x00180000
            #define SHARED_HW_CFG_FAN_FAILURE_SHIFT             19
            #define SHARED_HW_CFG_FAN_FAILURE_PHY_TYPE          0x00000000
            #define SHARED_HW_CFG_FAN_FAILURE_DISABLED          0x00080000
            #define SHARED_HW_CFG_FAN_FAILURE_ENABLED           0x00100000

    /* ASPM Power Management support */
        #define SHARED_HW_CFG_ASPM_SUPPORT_MASK             0x00600000
            #define SHARED_HW_CFG_ASPM_SUPPORT_SHIFT            21
            #define SHARED_HW_CFG_ASPM_SUPPORT_L0S_L1_ENABLED   0x00000000
            #define SHARED_HW_CFG_ASPM_SUPPORT_L0S_DISABLED     0x00200000
            #define SHARED_HW_CFG_ASPM_SUPPORT_L1_DISABLED      0x00400000
            #define SHARED_HW_CFG_ASPM_SUPPORT_L0S_L1_DISABLED  0x00600000

    /* The value of PM_TL_IGNORE_REQS (bit0) in PCI register
       tl_control_0 (register 0x2800) */
        #define SHARED_HW_CFG_PREVENT_L1_ENTRY_MASK      0x00800000
            #define SHARED_HW_CFG_PREVENT_L1_ENTRY_DISABLED     0x00000000
            #define SHARED_HW_CFG_PREVENT_L1_ENTRY_ENABLED      0x00800000

        #define SHARED_HW_CFG_PORT_MODE_MASK                0x01000000
            #define SHARED_HW_CFG_PORT_MODE_2                   0x00000000
            #define SHARED_HW_CFG_PORT_MODE_4                   0x01000000

        #define SHARED_HW_CFG_PATH_SWAP_MASK             0x02000000
            #define SHARED_HW_CFG_PATH_SWAP_DISABLED            0x00000000
            #define SHARED_HW_CFG_PATH_SWAP_ENABLED             0x02000000

    /*  Set the MDC/MDIO access for the first external phy */
        #define SHARED_HW_CFG_MDC_MDIO_ACCESS1_MASK         0x1C000000
            #define SHARED_HW_CFG_MDC_MDIO_ACCESS1_SHIFT        26
            #define SHARED_HW_CFG_MDC_MDIO_ACCESS1_PHY_TYPE     0x00000000
            #define SHARED_HW_CFG_MDC_MDIO_ACCESS1_EMAC0        0x04000000
            #define SHARED_HW_CFG_MDC_MDIO_ACCESS1_EMAC1        0x08000000
            #define SHARED_HW_CFG_MDC_MDIO_ACCESS1_BOTH         0x0c000000
            #define SHARED_HW_CFG_MDC_MDIO_ACCESS1_SWAPPED      0x10000000

    /*  Set the MDC/MDIO access for the second external phy */
        #define SHARED_HW_CFG_MDC_MDIO_ACCESS2_MASK         0xE0000000
            #define SHARED_HW_CFG_MDC_MDIO_ACCESS2_SHIFT        29
            #define SHARED_HW_CFG_MDC_MDIO_ACCESS2_PHY_TYPE     0x00000000
            #define SHARED_HW_CFG_MDC_MDIO_ACCESS2_EMAC0        0x20000000
            #define SHARED_HW_CFG_MDC_MDIO_ACCESS2_EMAC1        0x40000000
            #define SHARED_HW_CFG_MDC_MDIO_ACCESS2_BOTH         0x60000000
            #define SHARED_HW_CFG_MDC_MDIO_ACCESS2_SWAPPED      0x80000000


    u32 power_dissipated;                   /* 0x11c */
        #define SHARED_HW_CFG_POWER_MGNT_SCALE_MASK         0x00ff0000
            #define SHARED_HW_CFG_POWER_MGNT_SCALE_SHIFT        16
            #define SHARED_HW_CFG_POWER_MGNT_UNKNOWN_SCALE      0x00000000
            #define SHARED_HW_CFG_POWER_MGNT_DOT_1_WATT         0x00010000
            #define SHARED_HW_CFG_POWER_MGNT_DOT_01_WATT        0x00020000
            #define SHARED_HW_CFG_POWER_MGNT_DOT_001_WATT       0x00030000

        #define SHARED_HW_CFG_POWER_DIS_CMN_MASK            0xff000000
        #define SHARED_HW_CFG_POWER_DIS_CMN_SHIFT           24

    u32 ump_nc_si_config;                   /* 0x120 */
        #define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_MASK       0x00000003
            #define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_SHIFT      0
            #define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_MAC        0x00000000
            #define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_PHY        0x00000001
            #define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_MII        0x00000000
            #define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_RMII       0x00000002

        #define SHARED_HW_CFG_UMP_NC_SI_NUM_DEVS_MASK       0x00000f00
            #define SHARED_HW_CFG_UMP_NC_SI_NUM_DEVS_SHIFT      8

        #define SHARED_HW_CFG_UMP_NC_SI_EXT_PHY_TYPE_MASK   0x00ff0000
            #define SHARED_HW_CFG_UMP_NC_SI_EXT_PHY_TYPE_SHIFT  16
            #define SHARED_HW_CFG_UMP_NC_SI_EXT_PHY_TYPE_NONE   0x00000000
            #define SHARED_HW_CFG_UMP_NC_SI_EXT_PHY_TYPE_BCM5221 0x00010000

    u32 board;                      /* 0x124 */
	#define SHARED_HW_CFG_E3_I2C_MUX0_MASK              0x0000003F
	#define SHARED_HW_CFG_E3_I2C_MUX0_SHIFT                       0
	#define SHARED_HW_CFG_E3_I2C_MUX1_MASK              0x00000FC0
	#define SHARED_HW_CFG_E3_I2C_MUX1_SHIFT             6
	/* Use the PIN_CFG_XXX defines on top */
        #define SHARED_HW_CFG_BOARD_REV_MASK                0x00ff0000
        #define SHARED_HW_CFG_BOARD_REV_SHIFT               16

        #define SHARED_HW_CFG_BOARD_MAJOR_VER_MASK          0x0f000000
        #define SHARED_HW_CFG_BOARD_MAJOR_VER_SHIFT         24

        #define SHARED_HW_CFG_BOARD_MINOR_VER_MASK          0xf0000000
        #define SHARED_HW_CFG_BOARD_MINOR_VER_SHIFT         28

    u32 wc_lane_config;                                    /* 0x128 */
	#define SHARED_HW_CFG_LANE_SWAP_CFG_MASK            0x0000FFFF
	#define SHARED_HW_CFG_LANE_SWAP_CFG_SHIFT           0
	#define SHARED_HW_CFG_LANE_SWAP_CFG_32103210        0x00001b1b
	#define SHARED_HW_CFG_LANE_SWAP_CFG_32100123        0x00001be4
	#define SHARED_HW_CFG_LANE_SWAP_CFG_01233210        0x0000e41b
	#define SHARED_HW_CFG_LANE_SWAP_CFG_01230123        0x0000e4e4
	#define SHARED_HW_CFG_LANE_SWAP_CFG_TX_MASK         0x000000FF
        #define SHARED_HW_CFG_LANE_SWAP_CFG_TX_SHIFT        0
        #define SHARED_HW_CFG_LANE_SWAP_CFG_RX_MASK         0x0000FF00
        #define SHARED_HW_CFG_LANE_SWAP_CFG_RX_SHIFT        8
	
	/* TX lane Polarity swap */
	#define SHARED_HW_CFG_TX_LANE0_POL_FLIP_ENABLED     0x00010000
	#define SHARED_HW_CFG_TX_LANE1_POL_FLIP_ENABLED     0x00020000
	#define SHARED_HW_CFG_TX_LANE2_POL_FLIP_ENABLED     0x00040000
	#define SHARED_HW_CFG_TX_LANE3_POL_FLIP_ENABLED     0x00080000
	/* TX lane Polarity swap */
	#define SHARED_HW_CFG_RX_LANE0_POL_FLIP_ENABLED     0x00100000
	#define SHARED_HW_CFG_RX_LANE1_POL_FLIP_ENABLED     0x00200000
	#define SHARED_HW_CFG_RX_LANE2_POL_FLIP_ENABLED     0x00400000
	#define SHARED_HW_CFG_RX_LANE3_POL_FLIP_ENABLED     0x00800000

	/*  Selects the port layout of the board */
	#define SHARED_HW_CFG_E3_PORT_LAYOUT_MASK           0x0F000000
	#define SHARED_HW_CFG_E3_PORT_LAYOUT_SHIFT          24
	#define SHARED_HW_CFG_E3_PORT_LAYOUT_2P_01          0x00000000
	#define SHARED_HW_CFG_E3_PORT_LAYOUT_2P_10          0x01000000
	#define SHARED_HW_CFG_E3_PORT_LAYOUT_4P_0123        0x02000000
	#define SHARED_HW_CFG_E3_PORT_LAYOUT_4P_1032        0x03000000
	#define SHARED_HW_CFG_E3_PORT_LAYOUT_4P_2301        0x04000000
	#define SHARED_HW_CFG_E3_PORT_LAYOUT_4P_3210        0x05000000

};


/****************************************************************************
 * Port HW configuration                                                    *
 ****************************************************************************/
struct port_hw_cfg {                /* port 0: 0x12c  port 1: 0x2bc */

    u32 pci_id;
        #define PORT_HW_CFG_PCI_VENDOR_ID_MASK              0xffff0000
        #define PORT_HW_CFG_PCI_DEVICE_ID_MASK              0x0000ffff

    u32 pci_sub_id;
        #define PORT_HW_CFG_PCI_SUBSYS_DEVICE_ID_MASK       0xffff0000
        #define PORT_HW_CFG_PCI_SUBSYS_VENDOR_ID_MASK       0x0000ffff

    u32 power_dissipated;
        #define PORT_HW_CFG_POWER_DIS_D0_MASK               0x000000ff
        #define PORT_HW_CFG_POWER_DIS_D0_SHIFT              0
        #define PORT_HW_CFG_POWER_DIS_D1_MASK               0x0000ff00
        #define PORT_HW_CFG_POWER_DIS_D1_SHIFT              8
        #define PORT_HW_CFG_POWER_DIS_D2_MASK               0x00ff0000
        #define PORT_HW_CFG_POWER_DIS_D2_SHIFT              16
        #define PORT_HW_CFG_POWER_DIS_D3_MASK               0xff000000
        #define PORT_HW_CFG_POWER_DIS_D3_SHIFT              24

    u32 power_consumed;
        #define PORT_HW_CFG_POWER_CONS_D0_MASK              0x000000ff
        #define PORT_HW_CFG_POWER_CONS_D0_SHIFT             0
        #define PORT_HW_CFG_POWER_CONS_D1_MASK              0x0000ff00
        #define PORT_HW_CFG_POWER_CONS_D1_SHIFT             8
        #define PORT_HW_CFG_POWER_CONS_D2_MASK              0x00ff0000
        #define PORT_HW_CFG_POWER_CONS_D2_SHIFT             16
        #define PORT_HW_CFG_POWER_CONS_D3_MASK              0xff000000
        #define PORT_HW_CFG_POWER_CONS_D3_SHIFT             24

    u32 mac_upper;
        #define PORT_HW_CFG_UPPERMAC_MASK                   0x0000ffff
        #define PORT_HW_CFG_UPPERMAC_SHIFT                  0
    u32 mac_lower;

    u32 iscsi_mac_upper;  /* Upper 16 bits are always zeroes */
    u32 iscsi_mac_lower;

    u32 rdma_mac_upper;   /* Upper 16 bits are always zeroes */
    u32 rdma_mac_lower;

    u32 serdes_config;
        #define PORT_HW_CFG_SERDES_TX_DRV_PRE_EMPHASIS_MASK 0x0000ffff
        #define PORT_HW_CFG_SERDES_TX_DRV_PRE_EMPHASIS_SHIFT  0

        #define PORT_HW_CFG_SERDES_RX_DRV_EQUALIZER_MASK    0xffff0000
        #define PORT_HW_CFG_SERDES_RX_DRV_EQUALIZER_SHIFT   16


    /*  Default values: 2P-64, 4P-32 */
    u32 pf_config;                                      /* 0x158 */
        #define PORT_HW_CFG_PF_NUM_VF_MASK                            0x0000007F
        #define PORT_HW_CFG_PF_NUM_VF_SHIFT                           0

    /*  Default values: 17 */
        #define PORT_HW_CFG_PF_NUM_MSIX_VECTORS_MASK                  0x00007F00
        #define PORT_HW_CFG_PF_NUM_MSIX_VECTORS_SHIFT                 8
        
        #define PORT_HW_CFG_ENABLE_FLR_MASK                           0x00010000
	    #define PORT_HW_CFG_FLR_ENABLED                               0x00010000

    u32 vf_config;                                      /* 0x15C */
        #define PORT_HW_CFG_VF_NUM_MSIX_VECTORS_MASK                  0x0000007F
        #define PORT_HW_CFG_VF_NUM_MSIX_VECTORS_SHIFT                 0

        #define PORT_HW_CFG_VF_PCI_DEVICE_ID_MASK                     0xFFFF0000
        #define PORT_HW_CFG_VF_PCI_DEVICE_ID_SHIFT                    16

    u32 mf_pci_id;                                      /* 0x160 */
    #define PORT_HW_CFG_MF_PCI_DEVICE_ID_MASK                     0x0000FFFF
    #define PORT_HW_CFG_MF_PCI_DEVICE_ID_SHIFT                    0

    /*  Controls the TX laser of the SFP+ module */
    u32 sfp_ctrl;                                       /* 0x164 */
    #define PORT_HW_CFG_TX_LASER_MASK                             0x000000FF
    #define PORT_HW_CFG_TX_LASER_SHIFT                            0
    #define PORT_HW_CFG_TX_LASER_MDIO                             0x00000000
    #define PORT_HW_CFG_TX_LASER_GPIO0                            0x00000001
    #define PORT_HW_CFG_TX_LASER_GPIO1                            0x00000002
    #define PORT_HW_CFG_TX_LASER_GPIO2                            0x00000003
    #define PORT_HW_CFG_TX_LASER_GPIO3                            0x00000004
    
    /*  Controls the fault module LED of the SFP+ */
    #define PORT_HW_CFG_FAULT_MODULE_LED_MASK                     0x0000FF00
    #define PORT_HW_CFG_FAULT_MODULE_LED_SHIFT                    8
    #define PORT_HW_CFG_FAULT_MODULE_LED_GPIO0                    0x00000000
    #define PORT_HW_CFG_FAULT_MODULE_LED_GPIO1                    0x00000100
    #define PORT_HW_CFG_FAULT_MODULE_LED_GPIO2                    0x00000200
    #define PORT_HW_CFG_FAULT_MODULE_LED_GPIO3                    0x00000300
    #define PORT_HW_CFG_FAULT_MODULE_LED_DISABLED                 0x00000400

    /*  The output pin TX_DIS that controls the TX laser of the SFP+
      module. Use the PIN_CFG_XXX defines on top */
    u32 e3_sfp_ctrl;                                    /* 0x168 */
    #define PORT_HW_CFG_E3_TX_LASER_MASK                          0x000000FF
    #define PORT_HW_CFG_E3_TX_LASER_SHIFT                         0
    
    /*  The output pin for SFPP_TYPE which turns on the Fault module LED */
    #define PORT_HW_CFG_E3_FAULT_MDL_LED_MASK                     0x0000FF00
    #define PORT_HW_CFG_E3_FAULT_MDL_LED_SHIFT                    8
    
    /*  The input pin MOD_ABS that indicates whether SFP+ module is
      present or not. Use the PIN_CFG_XXX defines on top */
    #define PORT_HW_CFG_E3_MOD_ABS_MASK                           0x00FF0000
    #define PORT_HW_CFG_E3_MOD_ABS_SHIFT                          16
    
    /*  The output pin PWRDIS_SFP_X which disable the power of the SFP+
      module. Use the PIN_CFG_XXX defines on top */
    #define PORT_HW_CFG_E3_PWR_DIS_MASK                           0xFF000000
    #define PORT_HW_CFG_E3_PWR_DIS_SHIFT                          24
    
    /*  The input pin which signals module transmit fault. Use the PIN_CFG_XXX
     defines on top */
    u32 e3_cmn_pin_cfg;                                 /* 0x16C */
    #define PORT_HW_CFG_E3_TX_FAULT_MASK                          0x000000FF
    #define PORT_HW_CFG_E3_TX_FAULT_SHIFT                         0
    
    /*  The output pin which reset the PHY. Use the PIN_CFG_XXX defines on
     top */
    #define PORT_HW_CFG_E3_PHY_RESET_MASK                         0x0000FF00
    #define PORT_HW_CFG_E3_PHY_RESET_SHIFT                        8
    
    /*  The output pin which powers down the PHY. Use the PIN_CFG_XXX defines
     on top */
    #define PORT_HW_CFG_E3_PWR_DOWN_MASK                          0x00FF0000
    #define PORT_HW_CFG_E3_PWR_DOWN_SHIFT                         16
    
    /*  The output pin values BSC_SEL which selects the I2C for this port
      in the I2C Mux */
    #define PORT_HW_CFG_E3_I2C_MUX0_MASK                          0x01000000
    #define PORT_HW_CFG_E3_I2C_MUX1_MASK                          0x02000000
    u32 reserved0[8];                                  /* 0x170 */

    u32 aeu_int_mask;                                   /* 0x190 */

    u32 media_type;                                     /* 0x194 */
    #define PORT_HW_CFG_MEDIA_TYPE_PHY0_MASK                      0x000000FF
    #define PORT_HW_CFG_MEDIA_TYPE_PHY0_SHIFT                     0

    #define PORT_HW_CFG_MEDIA_TYPE_PHY1_MASK                      0x0000FF00
    #define PORT_HW_CFG_MEDIA_TYPE_PHY1_SHIFT                     8

    #define PORT_HW_CFG_MEDIA_TYPE_PHY2_MASK                      0x00FF0000
    #define PORT_HW_CFG_MEDIA_TYPE_PHY2_SHIFT                     16


    /*  4 times 16 bits for all 4 lanes. In case external PHY is present
          (not direct mode), those values will not take effect on the 4 XGXS
          lanes. For some external PHYs (such as 8706 and 8726) the values
          will be used to configure the external PHY  in those cases, not
          all 4 values are needed. */
    u16 xgxs_config_rx[4];                  /* 0x198 */
    u16 xgxs_config_tx[4];                  /* 0x1A0 */

    /* For storing FCOE mac on shared memory */
    u32 fcoe_fip_mac_upper;
        #define PORT_HW_CFG_FCOE_UPPERMAC_MASK              0x0000ffff
        #define PORT_HW_CFG_FCOE_UPPERMAC_SHIFT             0
    u32 fcoe_fip_mac_lower;

    u32 fcoe_wwn_port_name_upper;
    u32 fcoe_wwn_port_name_lower;

    u32 fcoe_wwn_node_name_upper;
    u32 fcoe_wwn_node_name_lower;

    u32 Reserved1[50];                                  /* 0x1C0 */

    u32 default_cfg;                                    /* 0x288 */
        #define PORT_HW_CFG_GPIO0_CONFIG_MASK                         0x00000003
            #define PORT_HW_CFG_GPIO0_CONFIG_SHIFT                        0
            #define PORT_HW_CFG_GPIO0_CONFIG_NA                           0x00000000
            #define PORT_HW_CFG_GPIO0_CONFIG_LOW                          0x00000001
            #define PORT_HW_CFG_GPIO0_CONFIG_HIGH                         0x00000002
            #define PORT_HW_CFG_GPIO0_CONFIG_INPUT                        0x00000003

        #define PORT_HW_CFG_GPIO1_CONFIG_MASK                         0x0000000C
            #define PORT_HW_CFG_GPIO1_CONFIG_SHIFT                        2
            #define PORT_HW_CFG_GPIO1_CONFIG_NA                           0x00000000
            #define PORT_HW_CFG_GPIO1_CONFIG_LOW                          0x00000004
            #define PORT_HW_CFG_GPIO1_CONFIG_HIGH                         0x00000008
            #define PORT_HW_CFG_GPIO1_CONFIG_INPUT                        0x0000000c

        #define PORT_HW_CFG_GPIO2_CONFIG_MASK                         0x00000030
            #define PORT_HW_CFG_GPIO2_CONFIG_SHIFT                        4
            #define PORT_HW_CFG_GPIO2_CONFIG_NA                           0x00000000
            #define PORT_HW_CFG_GPIO2_CONFIG_LOW                          0x00000010
            #define PORT_HW_CFG_GPIO2_CONFIG_HIGH                         0x00000020
            #define PORT_HW_CFG_GPIO2_CONFIG_INPUT                        0x00000030

        #define PORT_HW_CFG_GPIO3_CONFIG_MASK                         0x000000C0
            #define PORT_HW_CFG_GPIO3_CONFIG_SHIFT                        6
            #define PORT_HW_CFG_GPIO3_CONFIG_NA                           0x00000000
            #define PORT_HW_CFG_GPIO3_CONFIG_LOW                          0x00000040
            #define PORT_HW_CFG_GPIO3_CONFIG_HIGH                         0x00000080
            #define PORT_HW_CFG_GPIO3_CONFIG_INPUT                        0x000000c0

    /*  When KR link is required to be set to force which is not
          KR-compliant, this parameter determine what is the trigger for it.
          When GPIO is selected, low input will force the speed. Currently
          default speed is 1G. In the future, it may be widen to select the
          forced speed in with another parameter. Note when force-1G is
          enabled, it override option 56: Link Speed option. */
        #define PORT_HW_CFG_FORCE_KR_ENABLER_MASK                     0x00000F00
            #define PORT_HW_CFG_FORCE_KR_ENABLER_SHIFT                    8
            #define PORT_HW_CFG_FORCE_KR_ENABLER_NOT_FORCED               0x00000000
            #define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO0_P0                 0x00000100
            #define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO1_P0                 0x00000200
            #define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO2_P0                 0x00000300
            #define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO3_P0                 0x00000400
            #define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO0_P1                 0x00000500
            #define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO1_P1                 0x00000600
            #define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO2_P1                 0x00000700
            #define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO3_P1                 0x00000800
            #define PORT_HW_CFG_FORCE_KR_ENABLER_FORCED                   0x00000900
    /*  Enable to determine with which GPIO to reset the external phy */
    #define PORT_HW_CFG_EXT_PHY_GPIO_RST_MASK                     0x000F0000
        #define PORT_HW_CFG_EXT_PHY_GPIO_RST_SHIFT                    16
        #define PORT_HW_CFG_EXT_PHY_GPIO_RST_PHY_TYPE                 0x00000000
        #define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO0_P0                 0x00010000
        #define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO1_P0                 0x00020000
        #define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO2_P0                 0x00030000
        #define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO3_P0                 0x00040000
        #define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO0_P1                 0x00050000
        #define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO1_P1                 0x00060000
        #define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO2_P1                 0x00070000
        #define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO3_P1                 0x00080000

	/*  Enable BAM on KR */
	#define PORT_HW_CFG_ENABLE_BAM_ON_KR_MASK                     0x00100000
	#define PORT_HW_CFG_ENABLE_BAM_ON_KR_SHIFT                    20
	#define PORT_HW_CFG_ENABLE_BAM_ON_KR_DISABLED                 0x00000000
	#define PORT_HW_CFG_ENABLE_BAM_ON_KR_ENABLED                  0x00100000

	/*  Enable Common Mode Sense */
	#define PORT_HW_CFG_ENABLE_CMS_MASK                           0x00200000
	#define PORT_HW_CFG_ENABLE_CMS_SHIFT                          21
	#define PORT_HW_CFG_ENABLE_CMS_DISABLED                       0x00000000
	#define PORT_HW_CFG_ENABLE_CMS_ENABLED                        0x00200000

	/*  Determine the Serdes electrical interface   */
	#define PORT_HW_CFG_NET_SERDES_IF_MASK                        0x0F000000
	#define PORT_HW_CFG_NET_SERDES_IF_SHIFT                       24
	#define PORT_HW_CFG_NET_SERDES_IF_SGMII                       0x00000000
	#define PORT_HW_CFG_NET_SERDES_IF_XFI                         0x01000000
	#define PORT_HW_CFG_NET_SERDES_IF_SFI                         0x02000000
	#define PORT_HW_CFG_NET_SERDES_IF_KR                          0x03000000
	#define PORT_HW_CFG_NET_SERDES_IF_DXGXS                       0x04000000
	#define PORT_HW_CFG_NET_SERDES_IF_KR2                         0x05000000


    u32 speed_capability_mask2;                         /* 0x28C */
        #define PORT_HW_CFG_SPEED_CAPABILITY2_D3_MASK                 0x0000FFFF
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D3_SHIFT                0
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D3_10M_FULL             0x00000001
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D3__                    0x00000002
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D3___                   0x00000004
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D3_100M_FULL            0x00000008
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D3_1G                   0x00000010
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D3_2_DOT_5G             0x00000020
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D3_10G                  0x00000040
	    #define PORT_HW_CFG_SPEED_CAPABILITY2_D3_20G                  0x00000080

        #define PORT_HW_CFG_SPEED_CAPABILITY2_D0_MASK                 0xFFFF0000
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D0_SHIFT                16
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D0_10M_FULL             0x00010000
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D0__                    0x00020000
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D0___                   0x00040000
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D0_100M_FULL            0x00080000
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D0_1G                   0x00100000
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D0_2_DOT_5G             0x00200000
            #define PORT_HW_CFG_SPEED_CAPABILITY2_D0_10G                  0x00400000
	    #define PORT_HW_CFG_SPEED_CAPABILITY2_D0_20G                  0x00800000


    /*  In the case where two media types (e.g. copper and fiber) are
          present and electrically active at the same time, PHY Selection
          will determine which of the two PHYs will be designated as the
          Active PHY and used for a connection to the network.  */
    u32 multi_phy_config;                               /* 0x290 */
        #define PORT_HW_CFG_PHY_SELECTION_MASK               0x00000007
            #define PORT_HW_CFG_PHY_SELECTION_SHIFT              0
            #define PORT_HW_CFG_PHY_SELECTION_HARDWARE_DEFAULT   0x00000000
            #define PORT_HW_CFG_PHY_SELECTION_FIRST_PHY          0x00000001
            #define PORT_HW_CFG_PHY_SELECTION_SECOND_PHY         0x00000002
            #define PORT_HW_CFG_PHY_SELECTION_FIRST_PHY_PRIORITY 0x00000003
            #define PORT_HW_CFG_PHY_SELECTION_SECOND_PHY_PRIORITY 0x00000004

    /*  When enabled, all second phy nvram parameters will be swapped
          with the first phy parameters */
        #define PORT_HW_CFG_PHY_SWAPPED_MASK                 0x00000008
            #define PORT_HW_CFG_PHY_SWAPPED_SHIFT                3
            #define PORT_HW_CFG_PHY_SWAPPED_DISABLED             0x00000000
            #define PORT_HW_CFG_PHY_SWAPPED_ENABLED              0x00000008


    /*  Address of the second external phy */
    u32 external_phy_config2;                           /* 0x294 */
        #define PORT_HW_CFG_XGXS_EXT_PHY2_ADDR_MASK         0x000000FF
        #define PORT_HW_CFG_XGXS_EXT_PHY2_ADDR_SHIFT        0

    /*  The second XGXS external PHY type */
        #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_MASK         0x0000FF00
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_SHIFT        8
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_DIRECT       0x00000000
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8071      0x00000100
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8072      0x00000200
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8073      0x00000300
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8705      0x00000400
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8706      0x00000500
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8726      0x00000600
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8481      0x00000700
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_SFX7101      0x00000800
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8727      0x00000900
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8727_NOC  0x00000a00
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM84823     0x00000b00
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM54640     0x00000c00
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM84833     0x00000d00
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM54616     0x00000e00
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8722      0x00000f00
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_FAILURE      0x0000fd00
            #define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_NOT_CONN     0x0000ff00


    /*  4 times 16 bits for all 4 lanes. For some external PHYs (such as
          8706, 8726 and 8727) not all 4 values are needed. */
    u16 xgxs_config2_rx[4];                             /* 0x296 */
    u16 xgxs_config2_tx[4];                             /* 0x2A0 */

    u32 lane_config;
        #define PORT_HW_CFG_LANE_SWAP_CFG_MASK              0x0000ffff
            #define PORT_HW_CFG_LANE_SWAP_CFG_SHIFT             0
        /* AN and forced */
            #define PORT_HW_CFG_LANE_SWAP_CFG_01230123          0x00001b1b
        /* forced only */
            #define PORT_HW_CFG_LANE_SWAP_CFG_01233210          0x00001be4
        /* forced only */
            #define PORT_HW_CFG_LANE_SWAP_CFG_31203120          0x0000d8d8
        /* forced only */
            #define PORT_HW_CFG_LANE_SWAP_CFG_32103210          0x0000e4e4
        #define PORT_HW_CFG_LANE_SWAP_CFG_TX_MASK           0x000000ff
        #define PORT_HW_CFG_LANE_SWAP_CFG_TX_SHIFT          0
        #define PORT_HW_CFG_LANE_SWAP_CFG_RX_MASK           0x0000ff00
        #define PORT_HW_CFG_LANE_SWAP_CFG_RX_SHIFT          8
        #define PORT_HW_CFG_LANE_SWAP_CFG_MASTER_MASK       0x0000c000
        #define PORT_HW_CFG_LANE_SWAP_CFG_MASTER_SHIFT      14

    /*  Indicate whether to swap the external phy polarity */
        #define PORT_HW_CFG_SWAP_PHY_POLARITY_MASK             0x00010000
            #define PORT_HW_CFG_SWAP_PHY_POLARITY_DISABLED      0x00000000
            #define PORT_HW_CFG_SWAP_PHY_POLARITY_ENABLED       0x00010000


    u32 external_phy_config;
        #define PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK          0x000000ff
        #define PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT         0

        #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_MASK          0x0000ff00
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SHIFT         8
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT        0x00000000
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8071       0x00000100
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072       0x00000200
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073       0x00000300
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705       0x00000400
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706       0x00000500
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8726       0x00000600
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8481       0x00000700
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101       0x00000800
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8727       0x00000900
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8727_NOC   0x00000a00
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM84823      0x00000b00
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM54640      0x00000c00
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM84833      0x00000d00
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM54616      0x00000e00
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8722       0x00000f00
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT_WC     0x0000fc00
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE       0x0000fd00
            #define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_NOT_CONN      0x0000ff00

        #define PORT_HW_CFG_SERDES_EXT_PHY_ADDR_MASK        0x00ff0000
        #define PORT_HW_CFG_SERDES_EXT_PHY_ADDR_SHIFT       16

        #define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_MASK        0xff000000
            #define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_SHIFT       24
            #define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT      0x00000000
            #define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_BCM5482     0x01000000
	    #define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT_SD   0x02000000
            #define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_NOT_CONN    0xff000000

    u32 speed_capability_mask;
        #define PORT_HW_CFG_SPEED_CAPABILITY_D3_MASK        0x0000ffff
            #define PORT_HW_CFG_SPEED_CAPABILITY_D3_SHIFT       0
            #define PORT_HW_CFG_SPEED_CAPABILITY_D3_10M_FULL    0x00000001
            #define PORT_HW_CFG_SPEED_CAPABILITY_D3_10M_HALF    0x00000002
            #define PORT_HW_CFG_SPEED_CAPABILITY_D3_100M_HALF   0x00000004
            #define PORT_HW_CFG_SPEED_CAPABILITY_D3_100M_FULL   0x00000008
            #define PORT_HW_CFG_SPEED_CAPABILITY_D3_1G          0x00000010
            #define PORT_HW_CFG_SPEED_CAPABILITY_D3_2_5G        0x00000020
            #define PORT_HW_CFG_SPEED_CAPABILITY_D3_10G         0x00000040
	    #define PORT_HW_CFG_SPEED_CAPABILITY_D3_20G         0x00000080
            #define PORT_HW_CFG_SPEED_CAPABILITY_D3_RESERVED    0x0000f000

        #define PORT_HW_CFG_SPEED_CAPABILITY_D0_MASK        0xffff0000
            #define PORT_HW_CFG_SPEED_CAPABILITY_D0_SHIFT       16
            #define PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_FULL    0x00010000
            #define PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_HALF    0x00020000
            #define PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_HALF   0x00040000
            #define PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_FULL   0x00080000
            #define PORT_HW_CFG_SPEED_CAPABILITY_D0_1G          0x00100000
            #define PORT_HW_CFG_SPEED_CAPABILITY_D0_2_5G        0x00200000
            #define PORT_HW_CFG_SPEED_CAPABILITY_D0_10G         0x00400000
	    #define PORT_HW_CFG_SPEED_CAPABILITY_D0_20G         0x00800000
            #define PORT_HW_CFG_SPEED_CAPABILITY_D0_RESERVED    0xf0000000

    /*  A place to hold the original MAC address as a backup */
    u32 backup_mac_upper;                   /* 0x2B4 */
    u32 backup_mac_lower;                   /* 0x2B8 */

};


/****************************************************************************
 * Shared Feature configuration                                             *
 ****************************************************************************/
struct shared_feat_cfg {                 /* NVRAM Offset */

    u32 config;                     /* 0x450 */
        #define SHARED_FEATURE_BMC_ECHO_MODE_EN             0x00000001

    /* Use NVRAM values instead of HW default values */
        #define SHARED_FEAT_CFG_OVERRIDE_PREEMPHASIS_CFG_MASK  0x00000002
            #define SHARED_FEAT_CFG_OVERRIDE_PREEMPHASIS_CFG_DISABLED 0x00000000
            #define SHARED_FEAT_CFG_OVERRIDE_PREEMPHASIS_CFG_ENABLED  0x00000002

        #define SHARED_FEAT_CFG_NCSI_ID_METHOD_MASK         0x00000008
            #define SHARED_FEAT_CFG_NCSI_ID_METHOD_SPIO         0x00000000
            #define SHARED_FEAT_CFG_NCSI_ID_METHOD_NVRAM        0x00000008

        #define SHARED_FEAT_CFG_NCSI_ID_MASK                0x00000030
        #define SHARED_FEAT_CFG_NCSI_ID_SHIFT               4

    /*  Override the OTP back to single function mode. When using GPIO,
          high means only SF, 0 is according to CLP configuration */
        #define SHARED_FEAT_CFG_FORCE_SF_MODE_MASK                    0x00000700
            #define SHARED_FEAT_CFG_FORCE_SF_MODE_SHIFT                   8
            #define SHARED_FEAT_CFG_FORCE_SF_MODE_MF_ALLOWED              0x00000000
            #define SHARED_FEAT_CFG_FORCE_SF_MODE_FORCED_SF               0x00000100
            #define SHARED_FEAT_CFG_FORCE_SF_MODE_SPIO4                   0x00000200
            #define SHARED_FEAT_CFG_FORCE_SF_MODE_SWITCH_INDEPT           0x00000300
            #define SHARED_FEAT_CFG_FORCE_SF_MODE_NIV_MODE                0x00000400

    /* The interval in seconds between sending LLDP packets. Set to zero
       to disable the feature */
        #define SHARED_FEAT_CFG_LLDP_XMIT_INTERVAL_MASK     0x00ff0000
        #define SHARED_FEAT_CFG_LLDP_XMIT_INTERVAL_SHIFT    16

    /* The assigned device type ID for LLDP usage */
        #define SHARED_FEAT_CFG_LLDP_DEVICE_TYPE_ID_MASK    0xff000000
        #define SHARED_FEAT_CFG_LLDP_DEVICE_TYPE_ID_SHIFT   24

};


/****************************************************************************
 * Port Feature configuration                                               *
 ****************************************************************************/
struct port_feat_cfg {              /* port 0: 0x454  port 1: 0x4c8 */

    u32 config;
        #define PORT_FEATURE_BAR1_SIZE_MASK                 0x0000000f
            #define PORT_FEATURE_BAR1_SIZE_SHIFT                0
            #define PORT_FEATURE_BAR1_SIZE_DISABLED             0x00000000
            #define PORT_FEATURE_BAR1_SIZE_64K                  0x00000001
            #define PORT_FEATURE_BAR1_SIZE_128K                 0x00000002
            #define PORT_FEATURE_BAR1_SIZE_256K                 0x00000003
            #define PORT_FEATURE_BAR1_SIZE_512K                 0x00000004
            #define PORT_FEATURE_BAR1_SIZE_1M                   0x00000005
            #define PORT_FEATURE_BAR1_SIZE_2M                   0x00000006
            #define PORT_FEATURE_BAR1_SIZE_4M                   0x00000007
            #define PORT_FEATURE_BAR1_SIZE_8M                   0x00000008
            #define PORT_FEATURE_BAR1_SIZE_16M                  0x00000009
            #define PORT_FEATURE_BAR1_SIZE_32M                  0x0000000a
            #define PORT_FEATURE_BAR1_SIZE_64M                  0x0000000b
            #define PORT_FEATURE_BAR1_SIZE_128M                 0x0000000c
            #define PORT_FEATURE_BAR1_SIZE_256M                 0x0000000d
            #define PORT_FEATURE_BAR1_SIZE_512M                 0x0000000e
            #define PORT_FEATURE_BAR1_SIZE_1G                   0x0000000f
        #define PORT_FEATURE_BAR2_SIZE_MASK                 0x000000f0
            #define PORT_FEATURE_BAR2_SIZE_SHIFT                4
            #define PORT_FEATURE_BAR2_SIZE_DISABLED             0x00000000
            #define PORT_FEATURE_BAR2_SIZE_64K                  0x00000010
            #define PORT_FEATURE_BAR2_SIZE_128K                 0x00000020
            #define PORT_FEATURE_BAR2_SIZE_256K                 0x00000030
            #define PORT_FEATURE_BAR2_SIZE_512K                 0x00000040
            #define PORT_FEATURE_BAR2_SIZE_1M                   0x00000050
            #define PORT_FEATURE_BAR2_SIZE_2M                   0x00000060
            #define PORT_FEATURE_BAR2_SIZE_4M                   0x00000070
            #define PORT_FEATURE_BAR2_SIZE_8M                   0x00000080
            #define PORT_FEATURE_BAR2_SIZE_16M                  0x00000090
            #define PORT_FEATURE_BAR2_SIZE_32M                  0x000000a0
            #define PORT_FEATURE_BAR2_SIZE_64M                  0x000000b0
            #define PORT_FEATURE_BAR2_SIZE_128M                 0x000000c0
            #define PORT_FEATURE_BAR2_SIZE_256M                 0x000000d0
            #define PORT_FEATURE_BAR2_SIZE_512M                 0x000000e0
            #define PORT_FEATURE_BAR2_SIZE_1G                   0x000000f0

        #define PORT_FEAT_CFG_DCBX_MASK                     0x00000100
            #define PORT_FEAT_CFG_DCBX_DISABLED                 0x00000000
            #define PORT_FEAT_CFG_DCBX_ENABLED                  0x00000100

        #define PORT_FEATURE_EN_SIZE_MASK                   0x0f000000
        #define PORT_FEATURE_EN_SIZE_SHIFT                  24
        #define PORT_FEATURE_WOL_ENABLED                    0x01000000
        #define PORT_FEATURE_MBA_ENABLED                    0x02000000
        #define PORT_FEATURE_MFW_ENABLED                    0x04000000

    /* Advertise expansion ROM even if MBA is disabled */
        #define PORT_FEAT_CFG_FORCE_EXP_ROM_ADV_MASK     0x08000000
            #define PORT_FEAT_CFG_FORCE_EXP_ROM_ADV_DISABLED    0x00000000
            #define PORT_FEAT_CFG_FORCE_EXP_ROM_ADV_ENABLED     0x08000000

    /* Check the optic vendor via i2c against a list of approved modules
       in a separate nvram image */
        #define PORT_FEAT_CFG_OPT_MDL_ENFRCMNT_MASK         0xe0000000
            #define PORT_FEAT_CFG_OPT_MDL_ENFRCMNT_SHIFT        29
            #define PORT_FEAT_CFG_OPT_MDL_ENFRCMNT_NO_ENFORCEMENT   0x00000000
            #define PORT_FEAT_CFG_OPT_MDL_ENFRCMNT_DISABLE_TX_LASER 0x20000000
            #define PORT_FEAT_CFG_OPT_MDL_ENFRCMNT_WARNING_MSG  0x40000000
            #define PORT_FEAT_CFG_OPT_MDL_ENFRCMNT_POWER_DOWN   0x60000000

    u32 wol_config;
    /* Default is used when driver sets to "auto" mode */
        #define PORT_FEATURE_WOL_DEFAULT_MASK               0x00000003
            #define PORT_FEATURE_WOL_DEFAULT_SHIFT              0
            #define PORT_FEATURE_WOL_DEFAULT_DISABLE            0x00000000
            #define PORT_FEATURE_WOL_DEFAULT_MAGIC              0x00000001
            #define PORT_FEATURE_WOL_DEFAULT_ACPI               0x00000002
            #define PORT_FEATURE_WOL_DEFAULT_MAGIC_AND_ACPI     0x00000003
        #define PORT_FEATURE_WOL_RES_PAUSE_CAP              0x00000004
        #define PORT_FEATURE_WOL_RES_ASYM_PAUSE_CAP         0x00000008
        #define PORT_FEATURE_WOL_ACPI_UPON_MGMT             0x00000010

    u32 mba_config;
        #define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_MASK       0x00000007
            #define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_SHIFT      0
            #define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_PXE        0x00000000
            #define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_RPL        0x00000001
            #define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_BOOTP      0x00000002
            #define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_ISCSIB     0x00000003
            #define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_FCOE_BOOT  0x00000004
            #define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_NONE       0x00000007

        #define PORT_FEATURE_MBA_BOOT_RETRY_MASK            0x00000038
        #define PORT_FEATURE_MBA_BOOT_RETRY_SHIFT           3

        #define PORT_FEATURE_MBA_RES_PAUSE_CAP              0x00000100
        #define PORT_FEATURE_MBA_RES_ASYM_PAUSE_CAP         0x00000200
        #define PORT_FEATURE_MBA_SETUP_PROMPT_ENABLE        0x00000400
        #define PORT_FEATURE_MBA_HOTKEY_MASK                0x00000800
            #define PORT_FEATURE_MBA_HOTKEY_CTRL_S              0x00000000
            #define PORT_FEATURE_MBA_HOTKEY_CTRL_B              0x00000800
        #define PORT_FEATURE_MBA_EXP_ROM_SIZE_MASK          0x000ff000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_SHIFT         12
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_DISABLED      0x00000000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_2K            0x00001000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_4K            0x00002000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_8K            0x00003000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_16K           0x00004000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_32K           0x00005000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_64K           0x00006000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_128K          0x00007000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_256K          0x00008000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_512K          0x00009000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_1M            0x0000a000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_2M            0x0000b000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_4M            0x0000c000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_8M            0x0000d000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_16M           0x0000e000
            #define PORT_FEATURE_MBA_EXP_ROM_SIZE_32M           0x0000f000
        #define PORT_FEATURE_MBA_MSG_TIMEOUT_MASK           0x00f00000
        #define PORT_FEATURE_MBA_MSG_TIMEOUT_SHIFT          20
        #define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_MASK        0x03000000
            #define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_SHIFT       24
            #define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_AUTO        0x00000000
            #define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_BBS         0x01000000
            #define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_INT18H      0x02000000
            #define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_INT19H      0x03000000
        #define PORT_FEATURE_MBA_LINK_SPEED_MASK            0x3c000000
            #define PORT_FEATURE_MBA_LINK_SPEED_SHIFT           26
            #define PORT_FEATURE_MBA_LINK_SPEED_AUTO            0x00000000
            #define PORT_FEATURE_MBA_LINK_SPEED_10HD            0x04000000
            #define PORT_FEATURE_MBA_LINK_SPEED_10FD            0x08000000
            #define PORT_FEATURE_MBA_LINK_SPEED_100HD           0x0c000000
            #define PORT_FEATURE_MBA_LINK_SPEED_100FD           0x10000000
            #define PORT_FEATURE_MBA_LINK_SPEED_1GBPS           0x14000000
            #define PORT_FEATURE_MBA_LINK_SPEED_2_5GBPS         0x18000000
            #define PORT_FEATURE_MBA_LINK_SPEED_10GBPS_CX4      0x1c000000
            #define PORT_FEATURE_MBA_LINK_SPEED_20GBPS          0x20000000
    u32 bmc_config;
        #define PORT_FEATURE_BMC_LINK_OVERRIDE_MASK         0x00000001
            #define PORT_FEATURE_BMC_LINK_OVERRIDE_DEFAULT      0x00000000
            #define PORT_FEATURE_BMC_LINK_OVERRIDE_EN           0x00000001

    u32 mba_vlan_cfg;
        #define PORT_FEATURE_MBA_VLAN_TAG_MASK              0x0000ffff
        #define PORT_FEATURE_MBA_VLAN_TAG_SHIFT             0
        #define PORT_FEATURE_MBA_VLAN_EN                    0x00010000

    u32 resource_cfg;
        #define PORT_FEATURE_RESOURCE_CFG_VALID             0x00000001
        #define PORT_FEATURE_RESOURCE_CFG_DIAG              0x00000002
        #define PORT_FEATURE_RESOURCE_CFG_L2                0x00000004
        #define PORT_FEATURE_RESOURCE_CFG_ISCSI             0x00000008
        #define PORT_FEATURE_RESOURCE_CFG_RDMA              0x00000010

    u32 smbus_config;
        #define PORT_FEATURE_SMBUS_EN                       0x00000001/* Obsolete */
        #define PORT_FEATURE_SMBUS_ADDR_MASK                0x000000fe
        #define PORT_FEATURE_SMBUS_ADDR_SHIFT               1

    u32 vf_config;
        #define PORT_FEAT_CFG_VF_BAR2_SIZE_MASK             0x0000000f
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_SHIFT            0
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_DISABLED         0x00000000
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_4K               0x00000001
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_8K               0x00000002
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_16K              0x00000003
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_32K              0x00000004
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_64K              0x00000005
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_128K             0x00000006
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_256K             0x00000007
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_512K             0x00000008
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_1M               0x00000009
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_2M               0x0000000a
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_4M               0x0000000b
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_8M               0x0000000c
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_16M              0x0000000d
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_32M              0x0000000e
            #define PORT_FEAT_CFG_VF_BAR2_SIZE_64M              0x0000000f

    u32 link_config;    /* Used as HW defaults for the driver */
        #define PORT_FEATURE_CONNECTED_SWITCH_MASK          0x03000000
            #define PORT_FEATURE_CONNECTED_SWITCH_SHIFT         24
        /* (forced) low speed switch (< 10G) */
            #define PORT_FEATURE_CON_SWITCH_1G_SWITCH           0x00000000
        /* (forced) high speed switch (>= 10G) */
            #define PORT_FEATURE_CON_SWITCH_10G_SWITCH          0x01000000
            #define PORT_FEATURE_CON_SWITCH_AUTO_DETECT         0x02000000
            #define PORT_FEATURE_CON_SWITCH_ONE_TIME_DETECT     0x03000000

        #define PORT_FEATURE_LINK_SPEED_MASK                0x000f0000
            #define PORT_FEATURE_LINK_SPEED_SHIFT               16
            #define PORT_FEATURE_LINK_SPEED_AUTO                0x00000000
            #define PORT_FEATURE_LINK_SPEED_10M_FULL            0x00010000
            #define PORT_FEATURE_LINK_SPEED_10M_HALF            0x00020000
            #define PORT_FEATURE_LINK_SPEED_100M_HALF           0x00030000
            #define PORT_FEATURE_LINK_SPEED_100M_FULL           0x00040000
            #define PORT_FEATURE_LINK_SPEED_1G                  0x00050000
            #define PORT_FEATURE_LINK_SPEED_2_5G                0x00060000
            #define PORT_FEATURE_LINK_SPEED_10G_CX4             0x00070000
	    #define PORT_FEATURE_LINK_SPEED_20G                 0x00080000

        #define PORT_FEATURE_FLOW_CONTROL_MASK              0x00000700
            #define PORT_FEATURE_FLOW_CONTROL_SHIFT             8
            #define PORT_FEATURE_FLOW_CONTROL_AUTO              0x00000000
            #define PORT_FEATURE_FLOW_CONTROL_TX                0x00000100
            #define PORT_FEATURE_FLOW_CONTROL_RX                0x00000200
            #define PORT_FEATURE_FLOW_CONTROL_BOTH              0x00000300
            #define PORT_FEATURE_FLOW_CONTROL_NONE              0x00000400

    /* The default for MCP link configuration,
       uses the same defines as link_config */
    u32 mfw_wol_link_cfg;

    /* The default for the driver of the second external phy,
       uses the same defines as link_config */
    u32 link_config2;                                   /* 0x47C */

    /* The default for MCP of the second external phy,
       uses the same defines as link_config */
    u32 mfw_wol_link_cfg2;                              /* 0x480 */
    
    u32 Reserved2[17];                                  /* 0x484 */

};


/****************************************************************************
 * Device Information                                                       *
 ****************************************************************************/
struct shm_dev_info {                           /* size */

    u32    bc_rev; /* 8 bits each: major, minor, build */          /* 4 */

    struct shared_hw_cfg     shared_hw_config;            /* 40 */

    struct port_hw_cfg       port_hw_config[PORT_MAX];     /* 400*2=800 */

    struct shared_feat_cfg   shared_feature_config;            /* 4 */

    struct port_feat_cfg     port_feature_config[PORT_MAX];/* 116*2=232 */

};

#endif
