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
 * Copyright 2010 QLogic Corporation. All rights reserved.
 */

#ifndef _QLCNIC_BRDINFO_H_
#define	_QLCNIC_BRDINFO_H_

#ifdef __cplusplus
extern "C" {
#endif

/* The version of the main data structure */
#define	QLCNIC_BDINFO_VERSION 1

/* Magic number to let user know flash is programmed */
#define	QLCNIC_BDINFO_MAGIC 0x12345678

#define	P3_CHIP 3
#define	QLCNIC_P3_A0		0x30
#define	QLCNIC_P3_A2		0x32
#define	QLCNIC_P3_B0		0x40
#define	QLCNIC_P3_B1		0x41
#define	QLCNIC_P3_B2		0x42
#define	QLCNIC_P3P_A0		0x50

#define	QLCNIC_IS_REVISION_P3(REVISION)	(REVISION >= QLCNIC_P3_A0)
#define	QLCNIC_IS_REVISION_P3PLUS(REVISION)	(REVISION >= QLCNIC_P3P_A0)

typedef enum {

	/* Reference quad gig */
	QLCNIC_BRDTYPE_P3_REF_QG	=	0x0021,
	QLCNIC_BRDTYPE_P3_HMEZ		=	0x0022,
	/* Dual CX4 - Low Profile - Red card */
	QLCNIC_BRDTYPE_P3_10G_CX4_LP	=  0x0023,
	QLCNIC_BRDTYPE_P3_4_GB		=	0x0024,
	QLCNIC_BRDTYPE_P3_IMEZ		=	0x0025,
	QLCNIC_BRDTYPE_P3_10G_SFP_PLUS	=	0x0026,
	QLCNIC_BRDTYPE_P3_10000_BASE_T	=	0x0027,
	QLCNIC_BRDTYPE_P3_XG_LOM	=	0x0028,

	QLCNIC_BRDTYPE_P3_4_GB_MM	=	0x0029,
	QLCNIC_BRDTYPE_P3_10G_CX4	=	0x0031, /* Reference CX4 */
	QLCNIC_BRDTYPE_P3_10G_XFP	=	0x0032, /* Reference XFP */

    QLCNIC_BRDTYPE_P3_10G_TRP	 =  0x0080

} qlcnic_brdtype_t;

typedef enum {
	QLCNIC_UNKNOWN_TYPE_ROMIMAGE = 0,
	QLCNIC_P2_MN_TYPE_ROMIMAGE = 1,
	QLCNIC_P3_CT_TYPE_ROMIMAGE,
	QLCNIC_P3_MN_TYPE_ROMIMAGE,
	QLCNIC_P3_MS_TYPE_ROMIMAGE,
	QLCNIC_UNKNOWN_TYPE_ROMIMAGE_LAST
} qlcnic_fw_type_t;

/* board type specific information */
typedef struct {
	qlcnic_brdtype_t	brdtype; /* type of board */
	long		ports; /* max no of physical ports */
	qlcnic_fw_type_t	fwtype; /* The FW Associated with board type */
	char		*short_name;
} qlcnic_brdinfo_t;

#define	NUM_SUPPORTED_BOARDS (sizeof (qlcnic_boards)/sizeof (qlcnic_brdinfo_t))

#define	GET_BRD_NAME_BY_TYPE(type, name)            \
{                                                   \
	int i, found = 0;                               \
	for (i = 0; i < NUM_SUPPORTED_BOARDS; ++i) {    \
		if (qlcnic_boards[i].brdtype == type) {        \
			name = qlcnic_boards[i].short_name;        \
			found = 1;                              \
			break;                                  \
		}                                           \
	}                                               \
	if (!found)                                   \
	name = "Unknown";                           \
}

typedef struct {
	uint32_t header_version;
	uint32_t board_mfg;
	uint32_t board_type;
	uint32_t board_num;
	uint32_t chip_id;
	uint32_t chip_minor;
	uint32_t chip_major;
	uint32_t chip_pkg;
	uint32_t chip_lot;
	uint32_t port_mask; /* available niu ports */
	uint32_t peg_mask; /* available pegs */
	uint32_t icache_ok; /* can we run with icache? */
	uint32_t dcache_ok; /* can we run with dcache? */
	uint32_t casper_ok;

	/* qlcnic_eth_addr_t  mac_address[MAX_PORTS]; */
	uint32_t mac_addr_lo_0;
	uint32_t mac_addr_lo_1;
	uint32_t mac_addr_lo_2;
	uint32_t mac_addr_lo_3;

	/* MN-related config */
	uint32_t mn_sync_mode;    /* enable/ sync shift cclk/ sync shift mclk */
	uint32_t mn_sync_shift_cclk;
	uint32_t mn_sync_shift_mclk;
	uint32_t mn_wb_en;
	uint32_t mn_crystal_freq; /* in MHz */
	uint32_t mn_speed; /* in MHz */
	uint32_t mn_org;
	uint32_t mn_depth;
	uint32_t mn_ranks_0; /* ranks per slot */
	uint32_t mn_ranks_1; /* ranks per slot */
	uint32_t mn_rd_latency_0;
	uint32_t mn_rd_latency_1;
	uint32_t mn_rd_latency_2;
	uint32_t mn_rd_latency_3;
	uint32_t mn_rd_latency_4;
	uint32_t mn_rd_latency_5;
	uint32_t mn_rd_latency_6;
	uint32_t mn_rd_latency_7;
	uint32_t mn_rd_latency_8;
	uint32_t mn_dll_val[18];
	uint32_t mn_mode_reg; /* See MIU DDR Mode Register */
	uint32_t mn_ext_mode_reg; /* See MIU DDR Extended Mode Register */
	uint32_t mn_timing_0; /* See MIU Memory Control Timing Rgister */
	uint32_t mn_timing_1; /* See MIU Extended Memory Ctrl Timing Register */
	uint32_t mn_timing_2; /* See MIU Extended Memory Ctrl Timing2 Reg */

	/* SN-related config */
	uint32_t sn_sync_mode; /* enable/ sync shift cclk / sync shift mclk */
	uint32_t sn_pt_mode; /* pass through mode */
	uint32_t sn_ecc_en;
	uint32_t sn_wb_en;
	uint32_t sn_crystal_freq;
	uint32_t sn_speed;
	uint32_t sn_org;
	uint32_t sn_depth;
	uint32_t sn_dll_tap;
	uint32_t sn_rd_latency;

	uint32_t mac_addr_hi_0;
	uint32_t mac_addr_hi_1;
	uint32_t mac_addr_hi_2;
	uint32_t mac_addr_hi_3;

	uint32_t magic; /* indicates flash has been initialized */

	uint32_t mn_rdimm;
	uint32_t mn_dll_override;
	uint32_t coreclock_speed;
} qlcnic_board_info_t;

#define	FLASH_NUM_PORTS		4

typedef struct {
	uint32_t flash_addr[32];
} qlcnic_flash_mac_addr_t;

/* flash user area */
typedef struct {
	uint8_t  flash_md5[16];
	uint8_t  crbinit_md5[16];
	uint8_t  brdcfg_md5[16];
	/* bootloader */
	uint32_t bootld_version;
	uint32_t bootld_size;
	uint8_t  bootld_md5[16];
	/* image */
	uint32_t image_version;
	uint32_t image_size;
	uint8_t  image_md5[16];
	/* primary image status */
	uint32_t primary_status;
	uint32_t secondary_present;

	/* MAC address , 4 ports */
    qlcnic_flash_mac_addr_t mac_addr[FLASH_NUM_PORTS];

	/* Any user defined data */
} qlcnic_old_user_info_t;

#define	FLASH_NUM_MAC_PER_PORT		32
typedef struct {
	uint8_t  flash_md5[16 * 64];
	/* uint8_t  crbinit_md5[16]; */
	/* uint8_t  brdcfg_md5[16]; */
	/* bootloader */
	uint32_t bootld_version;
	uint32_t bootld_size;
	/* uint8_t  bootld_md5[16]; */
	/* image */
	uint32_t image_version;
	uint32_t image_size;
	/* U8  image_md5[16]; */
	/* primary image status */
	uint32_t primary_status;
	uint32_t secondary_present;

	/* MAC address , 4 ports, 32 address per port */
	uint64_t mac_addr[FLASH_NUM_PORTS * FLASH_NUM_MAC_PER_PORT];
	uint32_t sub_sys_id;
	uint8_t  serial_num[32];
	uint32_t bios_version;
	uint32_t pxe_enable;  /* bitmask, per port */
	uint32_t vlan_tag[FLASH_NUM_PORTS];

	/* Any user defined data */
} qlcnic_user_info_t;

/* Flash memory map */
typedef enum {
    CRBINIT_START   = 0,		/* Crbinit section */
    BRDCFG_START    = 0x4000,	/* board config */
    INITCODE_START  = 0x6000,	/* pegtune code */
    BOOTLD_START    = 0x10000,	/* bootld */
    BOOTLD1_START   = 0x14000,	/* Start of booloader 1 */
	IMAGE_START		= 0x43000,	/* compressed image */
    SECONDARY_START = 0x200000,	/* backup images */
    PXE_FIRST_STAGE_INTEL = 0x3C0000, /* Intel First Stage info */
    PXE_FIRST_STAGE_PPC = 0x3C4000, /* PPC First Stage info */
    PXE_SECOND_STAGE_INTEL = 0x3B0000, /* Intel Second Stage info */
    PXE_SECOND_STAGE_PPC = 0x3A0000, /* Intel Second Stage info */
/*    LICENSE_TIME_START = 0x3C0000,  license expiry time info */
	PXE_START		= 0x3D0000,   /* PXE image area */
    DEFAULT_DATA_START = 0x3e0000, /* where we place default factory data */
	/* User defined region for new boards */
	USER_START		= 0x3E8000,
    VPD_START		= 0x3E8C00,   /* Vendor private data */
    LICENSE_START	= 0x3E9000,   /* Firmware License */
    FIXED_START		= 0x3F0000    /* backup of crbinit */
} qlcnic_flash_map_t;

#define	USER_START_OLD		PXE_START /* for backward compatibility */

#define	OLD_MAC_ADDR_OFFSET	(USER_START)
#define	FW_VERSION_OFFSET	(USER_START + 0x408)
#define	FW_SIZE_OFFSET		(USER_START + 0x40c)
#define	FW_MAC_ADDR_OFFSET	(USER_START + 0x418)
#define	FW_SERIAL_NUM_OFFSET	(USER_START + 0x81c)
#define	BIOS_VERSION_OFFSET	(USER_START + 0x83c)

#ifdef __cplusplus
}
#endif

#endif	/* !_QLCNIC_BRDINFO_H_ */
