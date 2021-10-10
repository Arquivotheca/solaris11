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

#ifndef __QLCNIC_IOCTL_H__
#define	__QLCNIC_IOCTL_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ioctl's dealing with PCI read/writes */
#define	QLCNIC_CMD_START 0
#define	QLCNIC_CMD  (QLCNIC_CMD_START + 1)
#define	QLCNIC_NAME (QLCNIC_CMD_START + 2)

typedef enum {
		qlcnic_cmd_none = 0,
		qlcnic_cmd_pci_read,
		qlcnic_cmd_pci_write,
		qlcnic_cmd_pci_mem_read,
		qlcnic_cmd_pci_mem_write,
		qlcnic_cmd_pci_config_read,
		qlcnic_cmd_pci_config_write,
		qlcnic_cmd_get_stats,
		qlcnic_cmd_clear_stats,
		qlcnic_cmd_get_version,
		qlcnic_cmd_get_phy_type,
		qlcnic_cmd_efuse_chip_id,
		qlcnic_cmd_get_port_num,
		qlcnic_cmd_get_pci_func_num,
		qlcnic_cmd_port_led_blink,
		qlcnic_cmd_loopback_test,
		qlcnic_cmd_irq_test,
		qlcnic_cmd_interface_info,

		qlcnic_cmd_flash_read = 50,
		qlcnic_cmd_flash_write,
		qlcnic_cmd_flash_se,
		qlcnic_cmd_set_dbg_level
} qlcnic_ioctl_cmd_t;

#pragma pack(1)

typedef struct {
		uint32_t cmd;
		uint32_t unused1;
		uint64_t off;
		uint32_t size;
		uint32_t rv;
		char uabc[2048];
		void *ptr;
} qlcnic_ioctl_data_t;

struct qlcnic_statistics {
	uint64_t rx_packets;
	uint64_t tx_packets;
	uint64_t rx_bytes;
	uint64_t rx_errors;
	uint64_t tx_bytes;
	uint64_t tx_errors;
	uint64_t rx_CRC_errors;
	uint64_t rx_short_length_error;
	uint64_t rx_long_length_error;
	uint64_t rx_MAC_errors;
};

struct qlcnic_devinfo {

	const char	*name;
	int		instance;
	char		bdf[8];
	char		link_speed[10];
	char		link_status[20];
	char		link_duplex[25];
	int		mtu;
	uint32_t	max_sds_rings;
	/* Num of bufs posted in phantom */
	uint32_t	MaxTxDescCount;
	uint32_t	MaxRxDescCount;
	uint32_t	MaxJumboRxDescCount;
	uint32_t	tx_recycle_threshold;
	uint32_t	max_tx_dma_hdls;

	/* Number of Status descriptors */
	uint32_t	MaxStatusDescCount;
	int		rx_bcopy_threshold;
	int		tx_bcopy_threshold;
	int		lso_enable;
};


#pragma pack()

#ifdef __cplusplus
}
#endif

#endif /* !__QLCNIC_IOCTL_H__ */
