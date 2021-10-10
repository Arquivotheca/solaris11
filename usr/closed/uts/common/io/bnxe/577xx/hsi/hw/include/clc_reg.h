
#define MDIO_REG_BANK_CL73_IEEEB0			0x0
	#define MDIO_CL73_IEEEB0_CL73_AN_CONTROL		0x0
		#define MDIO_CL73_IEEEB0_CL73_AN_CONTROL_RESTART_AN	0x0200
		#define MDIO_CL73_IEEEB0_CL73_AN_CONTROL_AN_EN		0x1000
		#define MDIO_CL73_IEEEB0_CL73_AN_CONTROL_MAIN_RST	0x8000

#define MDIO_REG_BANK_CL73_IEEEB1			0x10
	#define MDIO_CL73_IEEEB1_AN_ADV1			0x00
		#define	MDIO_CL73_IEEEB1_AN_ADV1_PAUSE			0x0400
		#define	MDIO_CL73_IEEEB1_AN_ADV1_ASYMMETRIC 		0x0800
		#define	MDIO_CL73_IEEEB1_AN_ADV1_PAUSE_BOTH		0x0C00
		#define	MDIO_CL73_IEEEB1_AN_ADV1_PAUSE_MASK		0x0C00
	#define MDIO_CL73_IEEEB1_AN_ADV2				0x01
		#define MDIO_CL73_IEEEB1_AN_ADV2_ADVR_1000M		0x0000
		#define MDIO_CL73_IEEEB1_AN_ADV2_ADVR_1000M_KX		0x0020
		#define MDIO_CL73_IEEEB1_AN_ADV2_ADVR_10G_KX4		0x0040
		#define MDIO_CL73_IEEEB1_AN_ADV2_ADVR_10G_KR		0x0080
	#define	MDIO_CL73_IEEEB1_AN_LP_ADV1			0x03
		#define	MDIO_CL73_IEEEB1_AN_LP_ADV1_PAUSE		0x0400
		#define	MDIO_CL73_IEEEB1_AN_LP_ADV1_ASYMMETRIC 		0x0800
		#define	MDIO_CL73_IEEEB1_AN_LP_ADV1_PAUSE_BOTH		0x0C00
		#define	MDIO_CL73_IEEEB1_AN_LP_ADV1_PAUSE_MASK		0x0C00


#define	MDIO_REG_BANK_RX0				0x80b0
	#define	MDIO_RX0_RX_STATUS				0x10
		#define	MDIO_RX0_RX_STATUS_SIGDET			0x8000
		#define	MDIO_RX0_RX_STATUS_RX_SEQ_DONE			0x1000
	#define	MDIO_RX0_RX_EQ_BOOST				0x1c
		#define	MDIO_RX0_RX_EQ_BOOST_EQUALIZER_CTRL_MASK	0x7
		#define	MDIO_RX0_RX_EQ_BOOST_OFFSET_CTRL		0x10

#define	MDIO_REG_BANK_RX1				0x80c0
	#define	MDIO_RX1_RX_EQ_BOOST				0x1c
		#define	MDIO_RX1_RX_EQ_BOOST_EQUALIZER_CTRL_MASK	0x7
		#define	MDIO_RX1_RX_EQ_BOOST_OFFSET_CTRL		0x10

#define	MDIO_REG_BANK_RX2				0x80d0
	#define	MDIO_RX2_RX_EQ_BOOST				0x1c
		#define	MDIO_RX2_RX_EQ_BOOST_EQUALIZER_CTRL_MASK	0x7
		#define	MDIO_RX2_RX_EQ_BOOST_OFFSET_CTRL		0x10

#define	MDIO_REG_BANK_RX3				0x80e0
	#define	MDIO_RX3_RX_EQ_BOOST				0x1c
		#define	MDIO_RX3_RX_EQ_BOOST_EQUALIZER_CTRL_MASK	0x7
		#define	MDIO_RX3_RX_EQ_BOOST_OFFSET_CTRL		0x10

#define	MDIO_REG_BANK_RX_ALL				0x80f0
	#define	MDIO_RX_ALL_RX_EQ_BOOST				0x1c
		#define	MDIO_RX_ALL_RX_EQ_BOOST_EQUALIZER_CTRL_MASK	0x7
		#define	MDIO_RX_ALL_RX_EQ_BOOST_OFFSET_CTRL	0x10

#define	MDIO_REG_BANK_TX0				0x8060
	#define	MDIO_TX0_TX_DRIVER				0x17
		#define	MDIO_TX0_TX_DRIVER_PREEMPHASIS_MASK		0xf000
		#define	MDIO_TX0_TX_DRIVER_PREEMPHASIS_SHIFT		12
		#define	MDIO_TX0_TX_DRIVER_IDRIVER_MASK			0x0f00
		#define	MDIO_TX0_TX_DRIVER_IDRIVER_SHIFT		8
		#define	MDIO_TX0_TX_DRIVER_IPREDRIVER_MASK		0x00f0
		#define	MDIO_TX0_TX_DRIVER_IPREDRIVER_SHIFT		4
		#define	MDIO_TX0_TX_DRIVER_IFULLSPD_MASK		0x000e
		#define	MDIO_TX0_TX_DRIVER_IFULLSPD_SHIFT		1
		#define	MDIO_TX0_TX_DRIVER_ICBUF1T			1

#define	MDIO_REG_BANK_TX1				0x8070
	#define	MDIO_TX1_TX_DRIVER				0x17
		#define	MDIO_TX0_TX_DRIVER_PREEMPHASIS_MASK		0xf000
		#define	MDIO_TX0_TX_DRIVER_PREEMPHASIS_SHIFT		12
		#define	MDIO_TX0_TX_DRIVER_IDRIVER_MASK			0x0f00
		#define	MDIO_TX0_TX_DRIVER_IDRIVER_SHIFT		8
		#define	MDIO_TX0_TX_DRIVER_IPREDRIVER_MASK		0x00f0
		#define	MDIO_TX0_TX_DRIVER_IPREDRIVER_SHIFT		4
		#define	MDIO_TX0_TX_DRIVER_IFULLSPD_MASK		0x000e
		#define	MDIO_TX0_TX_DRIVER_IFULLSPD_SHIFT		1
		#define	MDIO_TX0_TX_DRIVER_ICBUF1T			1

#define	MDIO_REG_BANK_TX2				0x8080
	#define	MDIO_TX2_TX_DRIVER				0x17
		#define	MDIO_TX0_TX_DRIVER_PREEMPHASIS_MASK		0xf000
		#define	MDIO_TX0_TX_DRIVER_PREEMPHASIS_SHIFT		12
		#define	MDIO_TX0_TX_DRIVER_IDRIVER_MASK			0x0f00
		#define	MDIO_TX0_TX_DRIVER_IDRIVER_SHIFT		8
		#define	MDIO_TX0_TX_DRIVER_IPREDRIVER_MASK		0x00f0
		#define	MDIO_TX0_TX_DRIVER_IPREDRIVER_SHIFT		4
		#define	MDIO_TX0_TX_DRIVER_IFULLSPD_MASK		0x000e
		#define	MDIO_TX0_TX_DRIVER_IFULLSPD_SHIFT		1
		#define	MDIO_TX0_TX_DRIVER_ICBUF1T			1

#define	MDIO_REG_BANK_TX3				0x8090
	#define	MDIO_TX3_TX_DRIVER				0x17
		#define	MDIO_TX0_TX_DRIVER_PREEMPHASIS_MASK		0xf000
		#define	MDIO_TX0_TX_DRIVER_PREEMPHASIS_SHIFT		12
		#define	MDIO_TX0_TX_DRIVER_IDRIVER_MASK			0x0f00
		#define	MDIO_TX0_TX_DRIVER_IDRIVER_SHIFT		8
		#define	MDIO_TX0_TX_DRIVER_IPREDRIVER_MASK		0x00f0
		#define	MDIO_TX0_TX_DRIVER_IPREDRIVER_SHIFT		4
		#define	MDIO_TX0_TX_DRIVER_IFULLSPD_MASK		0x000e
		#define	MDIO_TX0_TX_DRIVER_IFULLSPD_SHIFT		1
		#define	MDIO_TX0_TX_DRIVER_ICBUF1T			1

#define	MDIO_REG_BANK_XGXS_BLOCK0			0x8000
	#define	MDIO_BLOCK0_XGXS_CONTROL			0x10

#define	MDIO_REG_BANK_XGXS_BLOCK1			0x8010
	#define	MDIO_BLOCK1_LANE_CTRL0				0x15
	#define	MDIO_BLOCK1_LANE_CTRL1				0x16
	#define	MDIO_BLOCK1_LANE_CTRL2				0x17
	#define	MDIO_BLOCK1_LANE_PRBS				0x19

#define	MDIO_REG_BANK_XGXS_BLOCK2			0x8100
	#define	MDIO_XGXS_BLOCK2_RX_LN_SWAP			0x10
		#define	MDIO_XGXS_BLOCK2_RX_LN_SWAP_ENABLE		0x8000
		#define	MDIO_XGXS_BLOCK2_RX_LN_SWAP_FORCE_ENABLE	0x4000
		#define	MDIO_XGXS_BLOCK2_TX_LN_SWAP		0x11
		#define	MDIO_XGXS_BLOCK2_TX_LN_SWAP_ENABLE		0x8000
		#define	MDIO_XGXS_BLOCK2_UNICORE_MODE_10G	0x14
		#define	MDIO_XGXS_BLOCK2_UNICORE_MODE_10G_CX4_XGXS	0x0001
		#define	MDIO_XGXS_BLOCK2_UNICORE_MODE_10G_HIGIG_XGXS	0x0010
		#define	MDIO_XGXS_BLOCK2_TEST_MODE_LANE		0x15

#define	MDIO_REG_BANK_GP_STATUS				0x8120
#define	MDIO_GP_STATUS_TOP_AN_STATUS1				0x1B
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_CL73_AUTONEG_COMPLETE	0x0001
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_CL37_AUTONEG_COMPLETE	0x0002
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_LINK_STATUS		0x0004
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_DUPLEX_STATUS		0x0008
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_CL73_MR_LP_NP_AN_ABLE	0x0010
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_CL73_LP_NP_BAM_ABLE	0x0020
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_PAUSE_RSOLUTION_TXSIDE	0x0040
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_PAUSE_RSOLUTION_RXSIDE	0x0080
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_MASK		0x3f00
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_10M		0x0000
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_100M		0x0100
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_1G		0x0200
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_2_5G		0x0300
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_5G		0x0400
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_6G		0x0500
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_10G_HIG	0x0600
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_10G_CX4	0x0700
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_12G_HIG	0x0800
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_12_5G	0x0900
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_13G		0x0A00
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_15G		0x0B00
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_16G		0x0C00
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_1G_KX	0x0D00
	#define	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_10G_KX4	0x0E00


#define	MDIO_REG_BANK_10G_PARALLEL_DETECT		0x8130
#define	MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_STATUS		0x10
#define	MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_STATUS_PD_LINK		0x8000
#define	MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_CONTROL		0x11
#define	MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_CONTROL_PARDET10G_EN	0x1
#define	MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_LINK		0x13
#define	MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_LINK_CNT		(0xb71<<1)

#define	MDIO_REG_BANK_SERDES_DIGITAL			0x8300
#define	MDIO_SERDES_DIGITAL_A_1000X_CONTROL1			0x10
#define	MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_FIBER_MODE			0x0001
#define	MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_TBI_IF			0x0002
#define	MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_SIGNAL_DETECT_EN		0x0004
#define	MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_INVERT_SIGNAL_DETECT	0x0008
#define	MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_AUTODET			0x0010
#define	MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_MSTR_MODE			0x0020
#define	MDIO_SERDES_DIGITAL_A_1000X_CONTROL2			0x11
#define	MDIO_SERDES_DIGITAL_A_1000X_CONTROL2_PRL_DT_EN			0x0001
#define	MDIO_SERDES_DIGITAL_A_1000X_CONTROL2_AN_FST_TMR			0x0040
#define	MDIO_SERDES_DIGITAL_A_1000X_STATUS1			0x14
#define	MDIO_SERDES_DIGITAL_A_1000X_STATUS1_SGMII			0x0001
#define	MDIO_SERDES_DIGITAL_A_1000X_STATUS1_LINK			0x0002
#define	MDIO_SERDES_DIGITAL_A_1000X_STATUS1_DUPLEX			0x0004
#define	MDIO_SERDES_DIGITAL_A_1000X_STATUS1_SPEED_MASK			0x0018
#define	MDIO_SERDES_DIGITAL_A_1000X_STATUS1_SPEED_SHIFT			3
#define	MDIO_SERDES_DIGITAL_A_1000X_STATUS1_SPEED_2_5G			0x0018
#define	MDIO_SERDES_DIGITAL_A_1000X_STATUS1_SPEED_1G			0x0010
#define	MDIO_SERDES_DIGITAL_A_1000X_STATUS1_SPEED_100M			0x0008
#define	MDIO_SERDES_DIGITAL_A_1000X_STATUS1_SPEED_10M			0x0000
#define	MDIO_SERDES_DIGITAL_A_1000X_STATUS2			0x15
#define	MDIO_SERDES_DIGITAL_A_1000X_STATUS2_AN_DISABLED			0x0002
#define	MDIO_SERDES_DIGITAL_MISC1				0x18
#define	MDIO_SERDES_DIGITAL_MISC1_REFCLK_SEL_MASK			0xE000
#define	MDIO_SERDES_DIGITAL_MISC1_REFCLK_SEL_25M			0x0000
#define	MDIO_SERDES_DIGITAL_MISC1_REFCLK_SEL_100M			0x2000
#define	MDIO_SERDES_DIGITAL_MISC1_REFCLK_SEL_125M			0x4000
#define	MDIO_SERDES_DIGITAL_MISC1_REFCLK_SEL_156_25M			0x6000
#define	MDIO_SERDES_DIGITAL_MISC1_REFCLK_SEL_187_5M			0x8000
#define	MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_SEL			0x0010
#define	MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_MASK			0x000f
#define	MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_2_5G			0x0000
#define	MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_5G			0x0001
#define	MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_6G			0x0002
#define	MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_10G_HIG			0x0003
#define	MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_10G_CX4			0x0004
#define	MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_12G			0x0005
#define	MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_12_5G			0x0006
#define	MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_13G			0x0007
#define	MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_15G			0x0008
#define	MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_16G			0x0009

#define	MDIO_REG_BANK_OVER_1G				0x8320
#define	MDIO_OVER_1G_DIGCTL_3_4					0x14
#define	MDIO_OVER_1G_DIGCTL_3_4_MP_ID_MASK				0xffe0
#define	MDIO_OVER_1G_DIGCTL_3_4_MP_ID_SHIFT				5
#define	MDIO_OVER_1G_UP1					0x19
#define	MDIO_OVER_1G_UP1_2_5G						0x0001
#define	MDIO_OVER_1G_UP1_5G						0x0002
#define	MDIO_OVER_1G_UP1_6G						0x0004
#define	MDIO_OVER_1G_UP1_10G						0x0010
#define	MDIO_OVER_1G_UP1_10GH						0x0008
#define	MDIO_OVER_1G_UP1_12G						0x0020
#define	MDIO_OVER_1G_UP1_12_5G						0x0040
#define	MDIO_OVER_1G_UP1_13G						0x0080
#define	MDIO_OVER_1G_UP1_15G						0x0100
#define	MDIO_OVER_1G_UP1_16G						0x0200
#define	MDIO_OVER_1G_UP2					0x1A
#define	MDIO_OVER_1G_UP2_IPREDRIVER_MASK				0x0007
#define	MDIO_OVER_1G_UP2_IDRIVER_MASK					0x0038
#define	MDIO_OVER_1G_UP2_PREEMPHASIS_MASK				0x03C0
#define	MDIO_OVER_1G_UP3					0x1B
#define	MDIO_OVER_1G_UP3_HIGIG2						0x0001
#define	MDIO_OVER_1G_LP_UP1					0x1C
#define	MDIO_OVER_1G_LP_UP2					0x1D
#define	MDIO_OVER_1G_LP_UP2_MR_ADV_OVER_1G_MASK				0x03ff
#define	MDIO_OVER_1G_LP_UP2_PREEMPHASIS_MASK				0x0780
#define	MDIO_OVER_1G_LP_UP2_PREEMPHASIS_SHIFT				7
#define	MDIO_OVER_1G_LP_UP3						0x1E

#define	MDIO_REG_BANK_REMOTE_PHY			0x8330
#define	MDIO_REMOTE_PHY_MISC_RX_STATUS				0x10
#define	MDIO_REMOTE_PHY_MISC_RX_STATUS_CL37_FSM_RECEIVED_OVER1G_MSG	0x0010
#define	MDIO_REMOTE_PHY_MISC_RX_STATUS_CL37_FSM_RECEIVED_BRCM_OUI_MSG	0x0600

#define	MDIO_REG_BANK_BAM_NEXT_PAGE			0x8350
#define	MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL			0x10
#define	MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL_BAM_MODE			0x0001
#define	MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL_TETON_AN			0x0002

#define	MDIO_REG_BANK_CL73_USERB0		0x8370
#define	MDIO_CL73_USERB0_CL73_UCTRL				0x10
#define	MDIO_CL73_USERB0_CL73_UCTRL_USTAT1_MUXSEL			0x0002
#define	MDIO_CL73_USERB0_CL73_USTAT1				0x11
#define	MDIO_CL73_USERB0_CL73_USTAT1_LINK_STATUS_CHECK			0x0100
#define	MDIO_CL73_USERB0_CL73_USTAT1_AN_GOOD_CHECK_BAM37		0x0400
#define	MDIO_CL73_USERB0_CL73_BAM_CTRL1				0x12
#define	MDIO_CL73_USERB0_CL73_BAM_CTRL1_BAM_EN				0x8000
#define	MDIO_CL73_USERB0_CL73_BAM_CTRL1_BAM_STATION_MNGR_EN		0x4000
#define	MDIO_CL73_USERB0_CL73_BAM_CTRL1_BAM_NP_AFTER_BP_EN		0x2000
#define	MDIO_CL73_USERB0_CL73_BAM_CTRL3				0x14
#define	MDIO_CL73_USERB0_CL73_BAM_CTRL3_USE_CL73_HCD_MR			0x0001

#define	MDIO_REG_BANK_AER_BLOCK			0xFFD0
#define	MDIO_AER_BLOCK_AER_REG					0x1E

#define	MDIO_REG_BANK_COMBO_IEEE0		0xFFE0
#define	MDIO_COMBO_IEEE0_MII_CONTROL				0x10
#define	MDIO_COMBO_IEEO_MII_CONTROL_MAN_SGMII_SP_MASK			0x2040
#define	MDIO_COMBO_IEEO_MII_CONTROL_MAN_SGMII_SP_10			0x0000
#define	MDIO_COMBO_IEEO_MII_CONTROL_MAN_SGMII_SP_100			0x2000
#define	MDIO_COMBO_IEEO_MII_CONTROL_MAN_SGMII_SP_1000			0x0040
#define	MDIO_COMBO_IEEO_MII_CONTROL_FULL_DUPLEX				0x0100
#define	MDIO_COMBO_IEEO_MII_CONTROL_RESTART_AN				0x0200
#define	MDIO_COMBO_IEEO_MII_CONTROL_AN_EN				0x1000
#define	MDIO_COMBO_IEEO_MII_CONTROL_LOOPBACK				0x4000
#define	MDIO_COMBO_IEEO_MII_CONTROL_RESET				0x8000
#define	MDIO_COMBO_IEEE0_MII_STATUS				0x11
#define	MDIO_COMBO_IEEE0_MII_STATUS_LINK_PASS				0x0004
#define	MDIO_COMBO_IEEE0_MII_STATUS_AUTONEG_COMPLETE			0x0020
#define	MDIO_COMBO_IEEE0_AUTO_NEG_ADV				0x14
#define	MDIO_COMBO_IEEE0_AUTO_NEG_ADV_FULL_DUPLEX			0x0020
#define	MDIO_COMBO_IEEE0_AUTO_NEG_ADV_HALF_DUPLEX			0x0040
#define	MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_MASK			0x0180
#define	MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_NONE			0x0000
#define	MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_SYMMETRIC			0x0080
#define	MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC			0x0100
#define	MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH			0x0180
#define	MDIO_COMBO_IEEE0_AUTO_NEG_ADV_NEXT_PAGE				0x8000
#define	MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1		0x15
#define	MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_NEXT_PAGE	0x8000
#define	MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_ACK		0x4000
#define	MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_PAUSE_MASK	0x0180
#define	MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_PAUSE_NONE	0x0000
#define	MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_PAUSE_BOTH	0x0180
#define	MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_HALF_DUP_CAP	0x0040
#define	MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_FULL_DUP_CAP	0x0020
/*WhenthelinkpartnerisinSGMIImode(bit0=1),then
bit15=link,bit12=duplex,bits11:10=speed,bit14=acknowledge.
Theotherbitsarereservedandshouldbezero*/
#define	MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1_SGMII_MODE	0x0001


#define	MDIO_PMA_DEVAD			0x1
/*ieee*/
#define	MDIO_PMA_REG_CTRL		0x0
#define	MDIO_PMA_REG_STATUS		0x1
#define	MDIO_PMA_REG_10G_CTRL2		0x7
#define	MDIO_PMA_REG_RX_SD		0xa
/*bcm*/
#define	MDIO_PMA_REG_BCM_CTRL		0x0096
#define MDIO_PMA_REG_FEC_CTRL		0x00ab
#define	MDIO_PMA_REG_RX_ALARM_CTRL	0x9000
#define	MDIO_PMA_REG_TX_ALARM_CTRL	0x9001
#define	MDIO_PMA_REG_LASI_CTRL		0x9002
#define	MDIO_PMA_REG_RX_ALARM		0x9003
#define	MDIO_PMA_REG_TX_ALARM		0x9004
#define	MDIO_PMA_REG_LASI_STATUS	0x9005
#define	MDIO_PMA_REG_PHY_IDENTIFIER	0xc800
#define	MDIO_PMA_REG_DIGITAL_CTRL	0xc808
#define	MDIO_PMA_REG_DIGITAL_STATUS	0xc809
#define	MDIO_PMA_REG_TX_POWER_DOWN	0xca02
#define	MDIO_PMA_REG_CMU_PLL_BYPASS	0xca09
#define	MDIO_PMA_REG_MISC_CTRL		0xca0a
#define	MDIO_PMA_REG_GEN_CTRL		0xca10
	#define	MDIO_PMA_REG_GEN_CTRL_ROM_RESET_INTERNAL_MP	0x0188
	#define	MDIO_PMA_REG_GEN_CTRL_ROM_MICRO_RESET		0x018a
#define	MDIO_PMA_REG_M8051_MSGIN_REG	0xca12
#define	MDIO_PMA_REG_M8051_MSGOUT_REG	0xca13
#define	MDIO_PMA_REG_ROM_VER1		0xca19
#define	MDIO_PMA_REG_ROM_VER2		0xca1a
#define	MDIO_PMA_REG_EDC_FFE_MAIN	0xca1b
#define	MDIO_PMA_REG_PLL_BANDWIDTH	0xca1d
#define MDIO_PMA_REG_PLL_CTRL 		0xca1e
#define MDIO_PMA_REG_MISC_CTRL0 	0xca23
#define MDIO_PMA_REG_LRM_MODE	 	0xca3f
#define	MDIO_PMA_REG_CDR_BANDWIDTH 	0xca46
#define	MDIO_PMA_REG_MISC_CTRL1		0xca85

#define MDIO_PMA_REG_SFP_TWO_WIRE_CTRL		0x8000
	#define MDIO_PMA_REG_SFP_TWO_WIRE_CTRL_STATUS_MASK 	0x000c
		#define MDIO_PMA_REG_SFP_TWO_WIRE_STATUS_IDLE 		0x0000
		#define MDIO_PMA_REG_SFP_TWO_WIRE_STATUS_COMPLETE 	0x0004
		#define MDIO_PMA_REG_SFP_TWO_WIRE_STATUS_IN_PROGRESS 	0x0008
		#define MDIO_PMA_REG_SFP_TWO_WIRE_STATUS_FAILED 	0x000c
#define MDIO_PMA_REG_SFP_TWO_WIRE_BYTE_CNT 	0x8002
#define MDIO_PMA_REG_SFP_TWO_WIRE_MEM_ADDR 	0x8003
#define MDIO_PMA_REG_8726_TWO_WIRE_DATA_BUF	0xc820
	#define MDIO_PMA_REG_8726_TWO_WIRE_DATA_MASK 0xff
#define MDIO_PMA_REG_8726_TX_CTRL1		0xca01
#define MDIO_PMA_REG_8726_TX_CTRL2		0xca05

#define MDIO_PMA_REG_8727_TWO_WIRE_SLAVE_ADDR	0x8005
#define MDIO_PMA_REG_8727_TWO_WIRE_DATA_BUF	0x8007
	#define MDIO_PMA_REG_8727_TWO_WIRE_DATA_MASK 0xff
#define MDIO_PMA_REG_8727_TX_CTRL1		0xca02
#define MDIO_PMA_REG_8727_TX_CTRL2		0xca05
#define MDIO_PMA_REG_8727_PCS_OPT_CTRL		0xc808
#define MDIO_PMA_REG_8727_GPIO_CTRL		0xc80e
#define MDIO_PMA_REG_8727_PCS_GP		0xc842
#define MDIO_PMA_REG_8727_OPT_CFG_REG		0xc8e4

#define MDIO_AN_REG_8727_MISC_CTRL		0x8309
#define	MDIO_PMA_REG_8073_CHIP_REV			0xc801
#define MDIO_PMA_REG_8073_SPEED_LINK_STATUS		0xc820
#define MDIO_PMA_REG_8073_XAUI_WA 			0xc841
#define MDIO_PMA_REG_8073_OPT_DIGITAL_CTRL 		0xcd08

#define MDIO_PMA_REG_7101_RESET		0xc000
#define	MDIO_PMA_REG_7107_LED_CNTL	0xc007
#define	MDIO_PMA_REG_7107_LINK_LED_CNTL	0xc009
#define	MDIO_PMA_REG_7101_VER1		0xc026
#define	MDIO_PMA_REG_7101_VER2		0xc027

#define MDIO_PMA_REG_8481_PMD_SIGNAL	0xa811
#define MDIO_PMA_REG_8481_LED1_MASK	0xa82c
#define MDIO_PMA_REG_8481_LED2_MASK	0xa82f
#define MDIO_PMA_REG_8481_LED3_MASK	0xa832
#define MDIO_PMA_REG_8481_LED3_BLINK	0xa834
#define MDIO_PMA_REG_8481_LED5_MASK	                0xa838
#define MDIO_PMA_REG_8481_SIGNAL_MASK	0xa835
#define MDIO_PMA_REG_8481_LINK_SIGNAL	0xa83b
#define MDIO_PMA_REG_8481_LINK_SIGNAL_LED4_ENABLE_MASK	0x800
#define MDIO_PMA_REG_8481_LINK_SIGNAL_LED4_ENABLE_SHIFT	11



#define	MDIO_WIS_DEVAD			0x2
/*bcm*/
#define	MDIO_WIS_REG_LASI_CNTL		0x9002
#define	MDIO_WIS_REG_LASI_STATUS	0x9005

#define	MDIO_PCS_DEVAD			0x3
#define	MDIO_PCS_REG_STATUS		0x0020
#define MDIO_PCS_REG_LASI_STATUS	0x9005
#define MDIO_PCS_REG_7101_DSP_ACCESS	0xD000
#define MDIO_PCS_REG_7101_SPI_MUX 	0xD008
#define MDIO_PCS_REG_7101_SPI_CTRL_ADDR 0xE12A
	#define MDIO_PCS_REG_7101_SPI_RESET_BIT (5)
#define MDIO_PCS_REG_7101_SPI_FIFO_ADDR 0xE02A
	#define MDIO_PCS_REG_7101_SPI_FIFO_ADDR_WRITE_ENABLE_CMD (6)
	#define MDIO_PCS_REG_7101_SPI_FIFO_ADDR_BULK_ERASE_CMD   (0xC7)
	#define MDIO_PCS_REG_7101_SPI_FIFO_ADDR_PAGE_PROGRAM_CMD (2)
#define MDIO_PCS_REG_7101_SPI_BYTES_TO_TRANSFER_ADDR 0xE028



#define	MDIO_XS_DEVAD			0x4
#define	MDIO_XS_REG_STATUS		0x0001
#define MDIO_XS_PLL_SEQUENCER 		0x8000
#define	MDIO_XS_SFX7101_XGXS_TEST1	0xc00a

#define MDIO_XS_8706_REG_BANK_RX0	0x80bc
#define MDIO_XS_8706_REG_BANK_RX1	0x80cc
#define MDIO_XS_8706_REG_BANK_RX2	0x80dc
#define MDIO_XS_8706_REG_BANK_RX3	0x80ec
#define MDIO_XS_8706_REG_BANK_RXA	0x80fc

#define MDIO_XS_REG_8073_RX_CTRL_PCIE	0x80FA

#define	MDIO_AN_DEVAD			0x7
/*ieee*/
#define	MDIO_AN_REG_CTRL		0x0000
#define	MDIO_AN_REG_STATUS		0x0001
	#define	MDIO_AN_REG_STATUS_AN_COMPLETE		0x0020
#define	MDIO_AN_REG_ADV_PAUSE		0x0010
	#define	MDIO_AN_REG_ADV_PAUSE_PAUSE		0x0400
	#define	MDIO_AN_REG_ADV_PAUSE_ASYMMETRIC	0x0800
	#define	MDIO_AN_REG_ADV_PAUSE_BOTH		0x0C00
	#define	MDIO_AN_REG_ADV_PAUSE_MASK		0x0C00
#define	MDIO_AN_REG_ADV			0x0011
#define MDIO_AN_REG_ADV2		0x0012
#define	MDIO_AN_REG_LP_AUTO_NEG		0x0013
#define	MDIO_AN_REG_MASTER_STATUS	0x0021
/*bcm*/
#define	MDIO_AN_REG_LINK_STATUS		0x8304
#define	MDIO_AN_REG_CL37_CL73		0x8370
#define	MDIO_AN_REG_CL37_AN		0xffe0
#define	MDIO_AN_REG_CL37_FC_LD		0xffe4
#define MDIO_AN_REG_CL37_FC_LP		0xffe5

#define MDIO_AN_REG_8073_2_5G		0x8329
#define MDIO_AN_REG_8073_BAM		0x8350

#define MDIO_AN_REG_8481_10GBASE_T_AN_CTRL	0x0020
#define MDIO_AN_REG_8481_LEGACY_MII_CTRL	0xffe0
#define MDIO_AN_REG_8481_LEGACY_MII_STATUS	0xffe1
#define MDIO_AN_REG_8481_LEGACY_AN_ADV		0xffe4
#define MDIO_AN_REG_8481_LEGACY_AN_EXPANSION	0xffe6
#define MDIO_AN_REG_8481_1000T_CTRL		0xffe9
#define MDIO_AN_REG_8481_EXPANSION_REG_RD_RW	0xfff5
#define MDIO_AN_REG_8481_EXPANSION_REG_ACCESS	0xfff7
#define MDIO_AN_REG_8481_AUX_CTRL		0xfff8
#define MDIO_AN_REG_8481_LEGACY_SHADOW		0xfffc

/* BCM84823 only */
#define	MDIO_CTL_DEVAD			0x1e
#define MDIO_CTL_REG_84823_MEDIA		0x401a
	#define MDIO_CTL_REG_84823_MEDIA_MAC_MASK		0x0018
	/* These pins configure the BCM84823 interface to MAC after reset. */
		#define MDIO_CTL_REG_84823_CTRL_MAC_XFI			0x0008
		#define MDIO_CTL_REG_84823_MEDIA_MAC_XAUI_M		0x0010
	/* These pins configure the BCM84823 interface to Line after reset. */
	#define MDIO_CTL_REG_84823_MEDIA_LINE_MASK		0x0060
		#define MDIO_CTL_REG_84823_MEDIA_LINE_XAUI_L		0x0020
		#define MDIO_CTL_REG_84823_MEDIA_LINE_XFI		0x0040
	/* When this pin is active high during reset, 10GBASE-T core is power
	 * down, When it is active low the 10GBASE-T is power up
	 */
	#define MDIO_CTL_REG_84823_MEDIA_COPPER_CORE_DOWN	0x0080
	#define MDIO_CTL_REG_84823_MEDIA_PRIORITY_MASK		0x0100
		#define MDIO_CTL_REG_84823_MEDIA_PRIORITY_COPPER	0x0000
		#define MDIO_CTL_REG_84823_MEDIA_PRIORITY_FIBER		0x0100
	#define MDIO_CTL_REG_84823_MEDIA_FIBER_1G			0x1000
#define MDIO_CTL_REG_84823_USER_CTRL_REG		        0x4005
	#define MDIO_CTL_REG_84823_USER_CTRL_CMS			0x0080

#define MDIO_PMA_REG_84823_CTL_LED_CTL_1                0xa8e3
	#define MDIO_PMA_REG_84823_LED3_STRETCH_EN          0x0080
