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


#ifndef BNX_H
#define BNX_H


#include <sys/types.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/errno.h>
#include <sys/cred.h>
#include <sys/poll.h>
#include <sys/modctl.h>
#ifdef __10u7
#include <sys/mac.h>
#else
#include <sys/mac_provider.h>
#endif
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/pattr.h>
#include <sys/sysmacros.h>
#include <sys/ethernet.h>
#include <sys/strsun.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <inet/common.h>
#include <inet/ip.h>
#include <inet/ip_if.h>
#include <sys/strsubr.h>
#include <sys/pci.h>
#include <sys/kstat.h>



/*
 */
#pragma weak   hcksum_retrieve
#pragma weak   hcksum_assoc


#include "listq.h"
#include "lm5706.h"
#include "54xx_reg.h"

#define BNX_STR_SIZE       32

#define BNX_DMA_ALIGNMENT           64
#define BNX_MAX_SGL_ENTRIES         16
#define BNX_MIN_BYTES_PER_FRAGMENT  32


#define FW_VER_WITH_UNLOAD_POWER_DOWN	0x01090003


extern ddi_device_acc_attr_t bnx_access_attrib;
extern ddi_dma_attr_t        bnx_std_dma_attrib;


typedef struct _bnx_memreq_t {
	void   * addr;
	size_t   size;
} bnx_memreq_t;



/******************************************************************************
 * Transmit queue info structure holds information regarding transmit resources.
 * This consists of two list, one is a free list of transmit packets and another
 * a list of pending packets (packets posted to ASIC for transmit). Upon receiving
 * transmit complete notification from the asic, the message blocks are freed and
 * and packet structure is moved to the free list.
 ******************************************************************************/

typedef	struct _um_xmit_qinfo {
	ddi_dma_handle_t   dcpyhndl;
	ddi_acc_handle_t   dcpyahdl;
	caddr_t            dcpyvirt;
	lm_u64_t           dcpyphys;
	size_t             dcpyhard;

	/* Packet descriptor memory. */
	u32_t              desc_cnt;
	bnx_memreq_t       desc_mem;

	/* Low resource water marks. */
	u32_t              thresh_pdwm;

	/* Free queue mutex */
	kmutex_t           free_mutex;

	/* Packet descriptors that are free for use. */
	s_list_t free_tx_desc;

	/* Packet descriptors that have been setup and are awaiting xmit. */
	s_list_t tx_resc_que;
} um_xmit_qinfo;



/******************************************************************************
 * Receive queue is mostly managed by the LM (lm_dev->rx_info.chain[]).
 * During initialization, UM allocates receive buffers and prepares the
 * rx descriptions to posts the receive buffers.
 ******************************************************************************/
typedef	struct _um_recv_qinfo {
	volatile boolean_t processing;

	/* For packet descriptors that do not have rx buffers assigned. */
	s_list_t buffq;

	/* For packet descriptors waiting to be sent up. */
	s_list_t waitq;
} um_recv_qinfo;


typedef	struct _os_param {
	u32_t active_resc_flag;
#define DRV_RESOURCE_PCICFG_MAPPED   0x0001
#define DRV_RESOURCE_MAP_REGS        0x0002
#define DRV_RESOURCE_INTR_1          0x0004
#define DRV_RESOURCE_MUTEX           0x0008
#define DRV_RESOURCE_NDD             0x0010
#define DRV_RESOURCE_HDWR_REGISTER   0x0020
#define DRV_RESOURCE_GLD_REGISTER    0x0040
#define DRV_RESOURCE_KSTAT           0x0080
#define DRV_RESOURCE_TIMER           0x0100
#define DRV_RESOURCE_MINOR_NODE      0x0200
#define DRV_LINK_TIMEOUT_CB          0x0400

	dev_info_t * dip;

	ddi_acc_handle_t pci_cfg_handle;
	ddi_acc_handle_t reg_acc_handle;

	mac_handle_t macp;
	mac_resource_handle_t rx_resc_handle[NUM_RX_CHAIN];
	caddr_t regs_addr;

	kmutex_t  gld_mutex;
	krwlock_t gld_snd_mutex;
	kmutex_t  xmit_mutex;
	kmutex_t  rcv_mutex;
	kmutex_t  phy_mutex;
	kmutex_t  ind_mutex;

	/* Following are generic DMA handles used for the following -
	 * 1. Status _ Statistic DMA memory
	 * 2. TXBD queue
	 * 3. RXBD queue
	 */
	#define BNX_MAX_PHYS_MEMREQS   32
	u32_t            dma_handles_used;
	void *           dma_virt[BNX_MAX_PHYS_MEMREQS];
	ddi_dma_handle_t dma_handle[BNX_MAX_PHYS_MEMREQS];
	ddi_acc_handle_t dma_acc_handle[BNX_MAX_PHYS_MEMREQS];

	ddi_dma_handle_t *status_block_dma_hdl;

} os_param_t;




/******************************************************************************
 * Following structure hosts attributes related to the device, like media type,
 * transmit/receive descriptor queue information, last status index
 * processed/acknowledged, etc'
 ******************************************************************************/

typedef struct _dev_param {

	u32_t mtu;

	lm_rx_mask_t rx_filter_mask;
	lm_offload_t enabled_oflds;

	/* This is the last value of 'status_idx' processed and acknowledged
	 * by the driver. This value is compared with current value in the
	 * status block to determine if new status block was generated by
	 * host coalesce block.
	 */
	u32_t processed_status_idx;

	u32_t fw_ver;

	boolean_t isfiber;

    boolean_t disableMsix;

    lm_status_t indLink;
    lm_medium_t indMedium;
} device_param_t;


typedef struct _bnx_ndd_lnk_tbl_t {
	const char      ** label;
	const boolean_t *  value;
} bnx_ndd_lnk_tbl_t;


/* NDD parameters related structure members. */
typedef struct _bnx_ndd_t {
	caddr_t           ndd_data;

	bnx_ndd_lnk_tbl_t lnktbl[3];

	int               link_speed;
	boolean_t         link_duplex;
	boolean_t         link_tx_pause;
	boolean_t         link_rx_pause;
} bnx_ndd_t;



typedef struct _bnx_lnk_cfg_t {
	boolean_t link_autoneg;
	boolean_t param_2500fdx;
	boolean_t param_1000fdx;
	boolean_t param_1000hdx;
	boolean_t param_100fdx;
	boolean_t param_100hdx;
	boolean_t param_10fdx;
	boolean_t param_10hdx;
	boolean_t param_tx_pause;
	boolean_t param_rx_pause;
} bnx_lnk_cfg_t;



typedef struct _bnx_phy_cfg_t {
	bnx_lnk_cfg_t lnkcfg;

	boolean_t     flow_autoneg;
	boolean_t     wirespeed;
} bnx_phy_cfg_t;



typedef	struct _um_device {
	/* Lower Module device structure should be the first element */
	struct _lm_device_t  lm_dev;

    ddi_intr_handle_t * pIntrBlock;
    u32_t               intrPriority;
    int                 intrType;

	volatile boolean_t  intr_enabled;
	kmutex_t            intr_mutex;
	uint32_t            intr_count;
	uint32_t            intr_no_change;
	uint32_t            intr_in_disabled;

	volatile boolean_t  timer_enabled;
	kmutex_t            tmr_mutex;
	timeout_id_t        tmrtid;
	unsigned int        timer_link_check_interval;
	unsigned int        timer_link_check_counter;
	unsigned int        timer_link_check_interval2;
	unsigned int        timer_link_check_counter2;

	volatile boolean_t  dev_start;
	volatile boolean_t  link_updates_ok;

	os_param_t          os_param;
	device_param_t      dev_var;

	u32_t               tx_copy_threshold;

	u32_t no_tx_credits;
#define BNX_TX_RESOURCES_NO_CREDIT          0x01
#define BNX_TX_RESOURCES_NO_DESC            0x02
/* Unable to allocate DMA resources. (e.g. bind error) */
#define BNX_TX_RESOURCES_NO_OS_DMA_RES      0x08
#define BNX_TX_RESOURCES_TOO_MANY_FRAGS     0x10

	um_xmit_qinfo       txq[NUM_TX_CHAIN];
#define _TX_QINFO(pdev, chain)      (pdev->txq[chain])
#define _TXQ_FREE_DESC(pdev, chain) (pdev->txq[chain].free_tx_desc)
#define _TXQ_RESC_DESC(pdev, chain) (pdev->txq[chain].tx_resc_que)

	u32_t               rx_copy_threshold;
	uint32_t            recv_discards;

	um_recv_qinfo       rxq[NUM_RX_CHAIN];
#define _RX_QINFO(pdev, chain)      (pdev->rxq[chain])

	bnx_ndd_t           nddcfg;

	bnx_phy_cfg_t       hwinit;
	bnx_phy_cfg_t       curcfg;
	bnx_lnk_cfg_t       remote;

	char                dev_name[BNX_STR_SIZE];
    int                 instance;
    char                version[BNX_STR_SIZE];
    char                versionFW[BNX_STR_SIZE];
    char                chipName[BNX_STR_SIZE];
    char                intrAlloc[BNX_STR_SIZE];

    kstat_t *           kstats;
    kmutex_t            kstatMutex;

	#define BNX_MAX_MEMREQS             2
	unsigned int                   memcnt;
	bnx_memreq_t  memreq[BNX_MAX_MEMREQS];
} um_device_t;



/******************************************************************************
 * Following structure defines the packet descriptor as seen by the UM module.
 * This is used to map  buffers to lm_packet on xmit path and receive path.
 ******************************************************************************/

typedef	struct _um_txpacket_t {
	/* Must be the first entry in this structure. */
	struct _lm_packet_t lm_pkt;

	mblk_t * mp;

	ddi_dma_handle_t * cpyhdl;
	caddr_t            cpymem;
	lm_u64_t           cpyphy;
	off_t              cpyoff;

	u32_t num_handles;
	ddi_dma_handle_t dma_handle[BNX_MAX_SGL_ENTRIES];

	lm_frag_list_t frag_list;
	lm_frag_t      frag_list_buffer[BNX_MAX_SGL_ENTRIES];
} um_txpacket_t;



#define BNX_RECV_MAX_FRAGS 1
typedef struct _um_rxpacket_t {
	/* Must be the first entry in this structure. */
	struct _lm_packet_t lmpacket;

	ddi_dma_handle_t    dma_handle;
	ddi_acc_handle_t    dma_acc_handle;
} um_rxpacket_t;


#define VLAN_TPID		0x8100u
#define VLAN_TAGSZ		4
#define VLAN_TAG_SIZE		4
#define VLAN_VID_MAX    4094    /* 4095 is reserved */


typedef struct ether_vlan_header vlan_hdr_t;
#define DRV_EXTRACT_VLAN_TPID(vhdrp)    htons(vhdrp->ether_tpid)
#define DRV_EXTRACT_VLAN_TCI(vhdrp)     htons(vhdrp->ether_tci)
#define DRV_SET_VLAN_TPID(vhdrp, tpid)  vhdrp->ether_tpid = htons(tpid)
#define DRV_SET_VLAN_TCI(vhdrp, vtag)   vhdrp->ether_tci = htons(vtag)





/**********************************************************************
 *
 *                      'ndd' Get/Set IOCTL Definition
 *
 **********************************************************************/


/*
 * (Internal) return values from ioctl subroutines
 */
enum ioc_reply {
	IOC_INVAL = -1,                         /* bad, NAK with EINVAL */
	IOC_DONE,                               /* OK, reply sent       */
	IOC_ACK,                                /* OK, just send ACK    */
	IOC_REPLY,                              /* OK, just send reply  */
	IOC_RESTART_ACK,                        /* OK, restart & ACK    */
	IOC_RESTART_REPLY                       /* OK, restart & reply  */
};







/**********************************************************************
 *
 *			Function Prototypes
 *
 **********************************************************************/

ddi_dma_handle_t *
bnx_find_dma_hdl(
	um_device_t * const umdevice,
	const void * const virtaddr
	);

void
um_send_driver_pulse(
    um_device_t *udevp
	);

int
bnx_find_mchash_collision(
	lm_mc_table_t * mc_table, const u8_t * const mc_addr );

void
bnx_update_phy(
	um_device_t *pdev
	);


boolean_t BnxKstatInit(um_device_t * pUM);
void BnxKstatFini(um_device_t * pUM);


#endif /* BNX_H */
