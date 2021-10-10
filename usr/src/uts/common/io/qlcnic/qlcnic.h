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

/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _QLCNIC_
#define	_QLCNIC_

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/inttypes.h>
#include <sys/rwlock.h>
#include <sys/mutex.h>
#include <sys/ddi.h>

#include <sys/sunddi.h>
#include <sys/types.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/strsubr.h>
#include <sys/dlpi.h>
#include <sys/devops.h>
#include <sys/stat.h>
#include <sys/pci.h>
#include <sys/note.h>
#include <sys/modctl.h>
#include <sys/kstat.h>
#include <sys/ethernet.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <inet/common.h>
#include <sys/pattr.h>
#include <inet/mi.h>
#include <inet/nd.h>
#include <sys/ddifm.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/fm/io/ddi.h>
#include <sys/mac.h>
#include <sys/mac_ether.h>
#include <sys/mac_provider.h>
#include <sys/note.h>
#include <sys/miiregs.h> /* by fjlite out of intel */
#include "qlcnic_hw.h"
#include "qlcnic_cmn.h"
#include "qlcnic_inc.h" /* For MAX_RCV_CTX */
#include "qlcnic_brdcfg.h"
#include "qlcnic_version.h"
#include "qlcnic_phan_reg.h"
#include "qlcnic_ioctl.h"

#define	QLC_ILB_PKT_SIZE		64
#define	QLCNIC_INTERRUPT_TEST		1
#define	QLCNIC_LOOPBACK_TEST		2

#define	MAX_UNICAST_LIST_SIZE	1	/* 1 mac address per 1 rx group */

#define	ADDR_IN_WINDOW1(off)	\
	((off > QLCNIC_CRB_PCIX_HOST2) && (off < QLCNIC_CRB_MAX)) ? 1 : 0

#ifdef __LP64__
typedef unsigned long long uptr_t;
#else
typedef unsigned long uptr_t;
#endif

#define	FIRST_PAGE_GROUP_START	0
#define	FIRST_PAGE_GROUP_END	0x100000

#define	SECOND_PAGE_GROUP_START	0x6000000
#define	SECOND_PAGE_GROUP_END	0x68BC000

#define	THIRD_PAGE_GROUP_START	0x70E4000
#define	THIRD_PAGE_GROUP_END	0x8000000

#define	FIRST_PAGE_GROUP_SIZE	FIRST_PAGE_GROUP_END - FIRST_PAGE_GROUP_START
#define	SECOND_PAGE_GROUP_SIZE	SECOND_PAGE_GROUP_END - SECOND_PAGE_GROUP_START
#define	THIRD_PAGE_GROUP_SIZE	THIRD_PAGE_GROUP_END - THIRD_PAGE_GROUP_START

/* CRB Window: bit 25 of CRB addr */
#define	QLCNIC_WINDOW_ONE		0x2000000
/*
 * normalize a 64MB crb address to 32MB PCI window
 * To use CRB_NORMALIZE, window _must_ be set to 1
 */
#define	CRB_NORMAL(reg)	\
	(reg) - QLCNIC_CRB_PCIX_HOST2 + QLCNIC_CRB_PCIX_HOST
#define	CRB_NORMALIZE(adapter, reg) \
	(void *)(unsigned long)(pci_base_offset(adapter, CRB_NORMAL(reg)))

#define	find_diff_among(a, b, range) \
	((a) < (b)?((b)-(a)):((b)+(range)-(a)))

#define	__FUNCTION__		__func__
#define	qlcnic_msleep(_msecs_)	drv_usecwait(_msecs_ * 1000)
#define	qlcnic_delay(_msecs_)	delay(drv_usectohz(_msecs_ * 1000))

#define	LE_TO_HOST_64			LE_64
#define	HOST_TO_LE_64			LE_64
#define	HOST_TO_LE_32			LE_32
#define	LE_TO_HOST_32			LE_32
#define	HOST_TO_LE_16			LE_16
#define	LE_TO_HOST_16			LE_16

/*
 * Following macros require the mapped addresses to access
 * the Phantom memory.
 */
#define	QLCNIC_PCI_READ_8(ADDRESS) \
	ddi_get8(adapter->regs_handle, (uint8_t *)(ADDRESS))
#define	QLCNIC_PCI_READ_16(ADDRESS) \
	ddi_get16(adapter->regs_handle, (uint16_t *)(ADDRESS))
#define	QLCNIC_PCI_READ_32(ADDRESS) \
	ddi_get32(adapter->regs_handle, (uint32_t *)(ADDRESS))
#define	QLCNIC_PCI_READ_64(ADDRESS) \
	ddi_get64(adapter->regs_handle, (uint64_t *)(ADDRESS))

#define	QLCNIC_PCI_WRITE_8(DATA, ADDRESS) \
	ddi_put8(adapter->regs_handle, (uint8_t *)(ADDRESS), (DATA))
#define	QLCNIC_PCI_WRITE_16(DATA, ADDRESS) \
	ddi_put16(adapter->regs_handle, (uint16_t *)(ADDRESS), (DATA))
#define	QLCNIC_PCI_WRITE_32(DATA, ADDRESS) \
	ddi_put32(adapter->regs_handle, (uint32_t *)(ADDRESS), (DATA))
#define	QLCNIC_PCI_WRITE_64(DATA, ADDRESS) \
	ddi_put64(adapter->regs_handle, (uint64_t *)(ADDRESS), (DATA))

#if DEBUG_LEVEL
#define	DPRINTF(n, args)	if (DEBUG_LEVEL & (n)) cmn_err args;
#define	QLCNIC_DUMP_BUFFER(a, b, c, d) \
	qlcnic_dump_buf((char *)a, (uint8_t *)b, (uint8_t)c, (uint32_t)d)
#define	QLCNIC_DUMP(dbg_level, a, b, c, d)	\
		if (DEBUG_LEVEL & dbg_level) QLCNIC_DUMP_BUFFER(a, b, c, d)

#else
#define	DPRINTF(n, args)
#define	QLCNIC_DUMP_BUFFER(a, b, c, d)
#define	QLCNIC_DUMP(dbg_level, a, b, c, d)
#endif



#define	DBG_HW			0x01
#define	DBG_INIT		0x02
#define	DBG_GLD 		0x04
#define	DBG_INTR		0x08
#define	DBG_RX			0x10
#define	DBG_TX			0x20
#define	DBG_DATA		0x40
#define	DBG_IOCTL		0x80

#define	RESUME_TX(tx_ring)	mac_tx_ring_update(tx_ring->adapter->mach, \
				    tx_ring->ring_handle)
#define	RX_UPSTREAM(rx_ring, mp)	mac_rx_ring(rx_ring->adapter->mach, \
					    rx_ring->ring_handle, mp, \
					    rx_ring->ring_gen_num);

#define	QLCNIC_POLL_ALL			INT_MAX

#define	QLCNIC_SPIN_LOCK(_lp_)			mutex_enter((_lp_))
#define	QLCNIC_SPIN_UNLOCK(_lp_)		mutex_exit((_lp_))
#define	QLCNIC_SPIN_LOCK_ISR(_lp_)		mutex_enter((_lp_))
#define	QLCNIC_SPIN_UNLOCK_ISR(_lp_)		mutex_exit((_lp_))

#define	QLCNIC_WRITE_LOCK(_lp_)			rw_enter((_lp_), RW_WRITER)
#define	QLCNIC_WRITE_UNLOCK(_lp_)		rw_exit((_lp_))
#define	QLCNIC_READ_LOCK(_lp_)			rw_enter((_lp_), RW_READER)
#define	QLCNIC_READ_UNLOCK(_lp_)		rw_exit((_lp_))
#define	QLCNIC_WRITE_LOCK_IRQS(_lp_, _fl_)	rw_enter((_lp_), RW_WRITER)
#define	QLCNIC_WRITE_UNLOCK_IRQR(_lp_, _fl_)	rw_exit((_lp_))

extern char qlcnic_driver_name[];
extern int verbmsg;

typedef struct dma_area {
	ddi_acc_handle_t	acc_hdl;	/* handle for memory	*/
	ddi_dma_handle_t	dma_hdl;	/* DMA handle		*/
	uint32_t		ncookies;
	uint32_t		offset;
	u64			dma_addr;
	void			*vaddr;
	uint32_t		pad;
} dma_area_t;

typedef struct qlcnic_dmah_node {
	struct qlcnic_dmah_node *next;
	ddi_dma_handle_t dmahdl;
}qlcnic_dmah_node_t;

typedef struct qlcnic_buf_node {
	struct qlcnic_buf_node *next;
	dma_area_t dma_area;
}qlcnic_buf_node_t;

struct qlcnic_cmd_buffer {
	mblk_t			*msg;
	qlcnic_dmah_node_t	*head, *tail;	/* for map mode */
	qlcnic_buf_node_t	*buf;		/* for copy mode */
};

typedef struct pkt_info {
	uint32_t	total_len;
	uint16_t	mblk_no;
	uint16_t	etype;
	uint16_t	mac_hlen;
	uint16_t	ip_hlen;
	uint16_t	l4_hlen;
	uint16_t	l4_proto;
	uint32_t	ip_src_addr;
	uint32_t	ip_desc_addr;
	uint16_t	src_port;
	uint16_t	dest_port;
	boolean_t	use_cksum;
	boolean_t	use_lso;
	uint16_t	mss;
} pktinfo_t;

typedef struct qlcnic_rcv_desc_context_s qlcnic_rcv_desc_ctx_t;
typedef struct qlcnic_adapter_s qlcnic_adapter;
typedef struct qlcnic_sds_ring_s qlcnic_sds_ring_t;
typedef struct qlcnic_tx_ring_s qlcnic_tx_ring_t;
typedef struct qlcnic_rds_buf_recycle_list_s qlcnic_rds_buf_recycle_list_t;

typedef struct qlcnic_rx_buffer {
	dma_area_t		dma_info;
	frtn_t			rx_recycle;	/* recycle function */
	mblk_t			*mp;
	qlcnic_rcv_desc_ctx_t	*rcv_desc;
	qlcnic_adapter		*adapter;
	uint32_t		index;
	uint32_t		rds_index; /* attached to which rds ring */
	struct qlcnic_sds_ring_s *sds_ring;
	uint32_t		ref_cnt;
}qlcnic_rx_buffer_t;

/* Board types */
#define	QLCNIC_GBE		0x01
#define	QLCNIC_XGBE		0x02
#define	QLCNIC_XGBE_LINK_SPEED	10000

/*
 * Interrupt coalescing defaults. The defaults are for 1500 MTU. It is
 * adjusted based on configured MTU.
 */
#define	QLCNIC_DEFAULT_INTR_COALESCE_RX_TIME_US	1
#define	QLCNIC_DEFAULT_INTR_COALESCE_RX_PACKETS	128 /* 64 */
#define	QLCNIC_DEFAULT_INTR_COALESCE_TX_PACKETS	64
#define	QLCNIC_DEFAULT_INTR_COALESCE_TX_TIME_US	4

#define	QLCNIC_INTR_DEFAULT			0x04

union qlcnic_nic_intr_coalesce_data_t {
	struct {
		uint16_t	rx_packets;
		uint16_t	rx_time_us;
		uint16_t	tx_packets;
		uint16_t	tx_time_us;
	} data;
	uint64_t		word;
};

struct qlcnic_nic_intr_coalesce_t {
	uint16_t			stats_time_us;
	uint16_t			rate_sample_time;
	uint16_t			flags;
	uint16_t			rsvd_1;
	uint32_t			low_threshold;
	uint32_t			high_threshold;
	union qlcnic_nic_intr_coalesce_data_t	normal;
	union qlcnic_nic_intr_coalesce_data_t	low;
	union qlcnic_nic_intr_coalesce_data_t	high;
	union qlcnic_nic_intr_coalesce_data_t	irq;
};

/*
 * One hardware_context{} per adapter
 * contains interrupt info as well shared hardware info.
 */
typedef	struct _hardware_context {
	unsigned long	pci_base0;
	unsigned long	pci_len0;
	unsigned long	pci_base1;
	unsigned long	pci_len1;
	unsigned long	pci_base2;
	unsigned long	pci_len2;
	unsigned long	first_page_group_end;
	unsigned long	first_page_group_start;
	unsigned long	ocm_win_crb;
	uint8_t			revision_id;
	uint8_t			cut_through;
	uint16_t		board_type;
	int			pci_func;
	uint16_t		max_ports;
	qlcnic_board_info_t	boardcfg;
	uint32_t		linkup;

	uint32_t		crb_win;
	uint32_t		ocm_win;

	struct qlcnic_adapter_s	*adapter;

	uint32_t		rcvFlag;
	uint32_t		crb_base;

} hardware_context, *phardware_context;

#define	QLCNIC_CT_DEFAULT_RX_BUF_LEN	2048
#define	MAX_COOKIES_PER_CMD		15
#define	QLCNIC_DB_MAPSIZE_BYTES		0x1000
#define	EXTRA_HANDLES			512
#define	QLCNIC_TX_BCOPY_THRESHOLD		128
#define	QLCNIC_RX_BCOPY_THRESHOLD		128
#define	QLCNIC_MIN_DRIVER_RDS_SIZE		64
#define	MAX_HDR_SIZE_PER_TX_PKT		256

#define	ETHER_VALAN_HEADER_LEN		sizeof (struct ether_vlan_header)
#define	ETHER_HEADER_LEN		sizeof (struct ether_header)
#define	IP_HEADER_LEN			sizeof (ipha_t)
#define	TX_DESC_LEN			sizeof (cmdDescType0_t)
#define	STATUS_DESC_LEN			sizeof (statusDesc_t)
#define	MAX_CMD_DESC_PER_TX		4 /* max descriptors per tranmit */

#define	MAX_NORMAL_BUF_LEN	1536
#define	BUF_SIZE		QLCNIC_CT_DEFAULT_RX_BUF_LEN

typedef struct qlcnic_pauseparam {
	uint16_t rx_pause;
	uint16_t tx_pause;
} qlcnic_pauseparam_t;

/*
 * The driver supports the NDD ioctls ND_GET/ND_SET, and the loopback
 * ioctls LB_GET_INFO_SIZE/LB_GET_INFO/LB_GET_MODE/LB_SET_MODE
 */
/*
 * Loop Back Modes
 */
enum {	QLCNIC_LOOP_NONE,
	QLCNIC_LOOP_INTERNAL,
};

/*
 * Named Data (ND) Parameter Management Structure
 */
typedef	struct {
	int			ndp_info;
	int			ndp_min;
	int			ndp_max;
	int			ndp_val;
	char		*ndp_name;
} nd_param_t; /* 0x18 (24) bytes  */

/*
 * NDD parameter indexes, divided into:
 *
 *      read-only parameters describing the hardware's capabilities
 *      read-write parameters controlling the advertised capabilities
 *      read-only parameters describing the partner's capabilities
 *      read-only parameters describing the link state
 */
enum {
	PARAM_AUTONEG_CAP = 0,
	PARAM_PAUSE_CAP,
	PARAM_ASYM_PAUSE_CAP,
	PARAM_10000FDX_CAP,
	PARAM_1000FDX_CAP,
	PARAM_1000HDX_CAP,
	PARAM_100T4_CAP,
	PARAM_100FDX_CAP,
	PARAM_100HDX_CAP,
	PARAM_10FDX_CAP,
	PARAM_10HDX_CAP,

	PARAM_ADV_AUTONEG_CAP,
	PARAM_ADV_PAUSE_CAP,
	PARAM_ADV_ASYM_PAUSE_CAP,
	PARAM_ADV_10000FDX_CAP,
	PARAM_ADV_1000FDX_CAP,
	PARAM_ADV_1000HDX_CAP,
	PARAM_ADV_100T4_CAP,
	PARAM_ADV_100FDX_CAP,
	PARAM_ADV_100HDX_CAP,
	PARAM_ADV_10FDX_CAP,
	PARAM_ADV_10HDX_CAP,

	PARAM_LINK_STATUS,
	PARAM_LINK_SPEED,
	PARAM_LINK_DUPLEX,

	PARAM_LOOP_MODE,

	PARAM_COUNT
};

struct qlcnic_adapter_stats {
	uint64_t  rcvdbadmsg;
	uint64_t  nocmddescriptor;
	uint64_t  polled;
	uint64_t  uphappy;
	uint64_t  updropped;
	uint64_t  uplcong;
	uint64_t  uphcong;
	uint64_t  upmcong;
	uint64_t  updunno;
	uint64_t  msgfreed;
	uint64_t  csummed;
	uint64_t  no_rcv;
	uint64_t  rxbytes;
	uint64_t  ints;
	uint64_t  desballocfailed;
	uint64_t  rxcopyed;
	uint64_t  rxmapped;
	uint64_t  outofrxbuf;
	uint64_t  promiscmode;
	uint64_t  rxbufshort;
	uint64_t  allocbfailed;
};

/* descriptor types */
#define	RCV_RING_STD		RCV_DESC_NORMAL
#define	RCV_RING_JUMBO		RCV_DESC_JUMBO
#define	RCV_RING_LRO		RCV_DESC_LRO

struct qlcnic_rds_buf_recycle_list_s {
	qlcnic_rx_buffer_t **free_list;
	uint32_t	free_list_size;
	kmutex_t	lock; /* buffer recycle lock */
	uint32_t	count;
	uint32_t	tail_index;
	uint32_t	head_index;
	uint32_t	max_free_entries;
};

struct qlcnic_sds_ring_s {
	kmutex_t		sds_lock;
	uint32_t		statusRxConsumer;
	uint64_t		rcvStatusDesc_physAddr;
	statusDesc_t		*rcvStatusDescHead;
	uint64_t		interrupt_crb_addr;
	uint64_t		host_sds_consumer_addr;
	uint32_t		max_status_desc_count;
	ddi_dma_handle_t	status_desc_dma_handle;

	ddi_acc_handle_t	status_desc_acc_handle;
	ddi_dma_cookie_t	status_desc_dma_cookie;
	struct qlcnic_adapter_s	*adapter;

	uint64_t		no_rcv;
	uint64_t		rxbytes;

	mac_ring_handle_t	ring_handle;
	uint64_t		ring_gen_num;
	uint32_t		group_index;	/* Group index */
	uint32_t		pad0;
	uint32_t		index;		/* ring id */

	qlcnic_rds_buf_recycle_list_t rds_buf_recycle_list[NUM_RCV_DESC_RINGS];
};

typedef struct qlcnic_rx_group {
	uint32_t		index;		/* Group index */
	mac_group_handle_t	group_handle;	/* call back group handle */
	qlcnic_adapter		*adapter;	/* Pointer to adapter struct */
} qlcnic_rx_group_t;

/*
 * Rcv Descriptor Context. One such per Rcv Descriptor. There may
 * be one Rcv Descriptor for normal packets, one for jumbo,
 * one for LRO and may be expanded.
 */
struct qlcnic_rcv_desc_context_s {
	kmutex_t	pool_lock[1];	/* buffer pool lock */

	uint64_t	phys_addr;
	dev_info_t	*phys_pdev;
	/* address of rx ring in Phantom */
	rcvDesc_t	*desc_head;

	volatile uint32_t	producer;
	uint32_t	MaxRxDescCount;
	volatile uint32_t	rx_desc_handled;
	volatile uint32_t	rx_buf_card;
	uint32_t		rx_buf_total;
	volatile uint32_t	rx_buf_free;
	qlcnic_rx_buffer_t *rx_buf_pool;
	volatile qlcnic_rx_buffer_t *pool_list;
	/* size of the receive buf */
	uint32_t	buf_size;
	/* rx buffers for receive   */

	ddi_dma_handle_t	rx_desc_dma_handle;
	ddi_acc_handle_t 	rx_desc_acc_handle;
	ddi_dma_cookie_t	rx_desc_dma_cookie;
	uint64_t		host_rx_producer_addr;
	uint32_t		dma_size;
	volatile uint32_t	quit_time;
	volatile uint32_t	rx_buf_indicated; /* packets still with stack */
	uint32_t		rds_index; /* rds ring index */
	uint32_t		bufs_per_page;	/* multi bufs share same page */
};

#define	DEFAULT_SDS_RINGS	4
#define	MAX_SDS_RINGS		4
#define	DEFAULT_TX_RINGS	1
#define	MAX_TX_RINGS		4

/*
 * Receive context. There is one such structure per instance of the
 * receive processing. Any state information that is relevant to
 * the receive, and is must be in this structure. The global data may be
 * present elsewhere.
 */
typedef struct qlcnic_recv_context_s {
	qlcnic_rcv_desc_ctx_t		*rcv_desc[NUM_RCV_DESC_RINGS];
	struct qlcnic_sds_ring_s	sds_ring[MAX_SDS_RINGS];
	uint32_t			state;
	uint16_t			context_id, virt_port;
} qlcnic_recv_context_t;

#define	QLCNIC_MSI_ENABLED	0x02
#define	QLCNIC_MSIX_ENABLED	0x04
#define	QLCNIC_LSO_ENABLED	0x08
#define	QLCNIC_LRO_ENABLED	0x10


#define	QLCNIC_LSO_MAXLEN		65535

#define	QLCNIC_IS_MSI_FAMILY(ADAPTER)	\
	((ADAPTER)->flags & (QLCNIC_MSI_ENABLED | QLCNIC_MSIX_ENABLED))

#define	QLCNIC_USE_MSIX

/* msix defines */
#define	MSIX_ENTRIES_PER_ADAPTER	8
#define	QLCNIC_MSIX_TBL_SPACE		8192
#define	QLCNIC_PCI_REG_MSIX_TBL		0x44

/* Max number of MSIX interrupts per function */
#define	MAX_INTR			8

/*
 * Bug: word or char write on MSI-X capcabilities register (0x40) in PCI config
 * space has no effect on register values. Need to write dword.
 */
#define	QLCNIC_HWBUG_8_WORKAROUND

/*
 * Bug: Can not reset bit 32 (msix enable bit) on MSI-X capcabilities
 * register (0x40) independently.
 * Need to write 0x0 (zero) to MSI-X capcabilities register in order to reset
 * msix enable bit. On writing zero rest of the bits are not touched.
 */
#define	QLCNIC_HWBUG_9_WORKAROUND

#define	QLCNIC_MC_COUNT    38	/* == ((QLCNIC_ADDR_L2LU_COUNT-1)/4) -2 */

#define	MAX_RESET_ACK_TIMEOUT	10 /* 20 secs */
typedef struct qlcnic_tx_stats_s {
	uint64_t		xmitcalled;
	uint64_t		xmitedframes;
	uint64_t		xmitfinished;
	uint64_t		txdropped;
	uint64_t		txbytes;
	uint64_t		txcopyed;
	uint64_t		txmapped;
	uint64_t		outofcmddesc;
	uint64_t		outoftxdmahdl;
	uint64_t		outoftxbuffer;
	uint64_t		dmabindfailures;
	uint64_t		lastfailedcookiecount;
	uint64_t		exceedcookiesfailures;
	uint64_t		lastfailedhdrsize;
	uint64_t		hdrsizefailures;
	uint64_t		hdrtoosmallfailures;
	uint64_t		lastdmabinderror;
	uint64_t		lastdmabindfailsize;
	uint64_t		sendcopybigpkt;
	uint64_t		msgpulluped;
} qlcnic_tx_stats_t;

struct qlcnic_tx_ring_s {
	kmutex_t		tx_lock;
	qlcnic_adapter		*adapter;

	uint32_t		*cmdConsumer;
	/*
	 * Several tx rings' consumer indices are created toger, this flag
	 * indicates where this tx ring's consumer index is.
	 */
	uint32_t		cmd_consumer_offset;

	struct qlcnic_cmd_buffer *cmd_buf_arr;  /* Command buffers for xmit */

	/* dma handles */
	qlcnic_dmah_node_t	tx_dma_hdls[MAX_CMD_DESCRIPTORS_DMA_HDLS];
	qlcnic_dmah_node_t	*dmah_free_list[MAX_CMD_DESCRIPTORS_DMA_HDLS];
	uint32_t		dmah_free; /* atomic */
	uint32_t		dmah_head; /* Head index of free list */
	uint32_t		dmah_tail; /* Tail index of free list */
	kmutex_t		dmah_head_lock;	/* dma list lock */
	kmutex_t		dmah_tail_lock;

	qlcnic_buf_node_t	tx_bufs[MAX_CMD_DESCRIPTORS];
	qlcnic_buf_node_t	*buf_free_list[MAX_CMD_DESCRIPTORS];
	uint32_t		buf_free; /* atomic */
	uint32_t		buf_head; /* Head index of free list */
	uint32_t		buf_tail; /* Tail index of free list */
	kmutex_t		buf_head_lock;
	kmutex_t		buf_tail_lock;

	uint64_t		freecmds;
	uint32_t		lastCmdConsumer;
	uint32_t		cmdProducer;
	cmdDescType0_t		*cmdDescHead;
	/* Num of instances active on cmd buffer ring */
	int			resched_needed;
	uint64_t		cmdDesc_physAddr;
	ddi_dma_handle_t	cmd_desc_dma_handle;
	ddi_acc_handle_t	cmd_desc_acc_handle;
	ddi_dma_cookie_t	cmd_desc_dma_cookie;

	uint64_t		crb_addr_cmd_producer;
	uint64_t		cmd_consumer_phys;

	uint32_t		tx_comp;
	uint32_t		index;	/* ring id */

	uint32_t		tx_context_id;
	/* statistics */
	qlcnic_tx_stats_t	stats;
	mac_ring_handle_t	ring_handle;
};

typedef struct {
	struct ether_addr	addr;		/* in canonical form	*/
	boolean_t		set;		/* B_TRUE => valid	*/
} qlcnic_mac_addr_t;

typedef struct {
	mac_ring_handle_t	ring_handle;
	uint64_t		ring_gen_num;
} qlcnic_rx_ring_reserved_attr_t;

typedef struct {
	mac_ring_handle_t	ring_handle;
} qlcnic_tx_ring_reserved_attr_t;

/* Following structure is for specific port information */
struct qlcnic_adapter_s {
	qlcnic_recv_context_t	recv_ctx[MAX_RCV_CTX];
	struct qlcnic_tx_ring_s tx_ring[MAX_TX_RINGS];
	hardware_context	ahw;
	uint8_t			id[32];
	uint16_t		portnum;
	uint16_t		physical_port;
	uint16_t		link_speed;
	uint16_t		link_duplex;
	uint16_t		rx_pause;
	uint16_t		tx_pause;

	struct qlcnic_adapter_stats stats;
	int			rx_csum;
	int			status;
	kmutex_t		stats_lock;
	unsigned char		mac_addr[ETHERADDRL];
	qlcnic_mac_addr_t	unicst_addr[MAX_UNICAST_LIST_SIZE];
	uint32_t		unicst_total; /* total unicst addresses */
	uint32_t		unicst_avail;

	int			mtu;		/* active mtu */
	int			tx_buf_size;
	uint32_t		promisc;

	mac_resource_handle_t	mac_rx_ring_ha;
	mac_handle_t		mach;
	int			flags;
	uint32_t		loop_back_mode;

	int			instance;
	dev_info_t		*dip;
	ddi_acc_handle_t	pci_cfg_handle;
	ddi_acc_handle_t	regs_handle;
	ddi_dma_attr_t		gc_dma_attr_desc;

	struct ddi_device_acc_attr	gc_attr_desc;
	ddi_iblock_cookie_t	iblock_cookie;
	const char *name;

	ddi_intr_handle_t	intr_handle[MAX_INTR];
	krwlock_t		adapter_lock;
	int			intr_count;
	int			intr_type;
	int			intr_cap;	/* Interrupt capabilities */
	uint_t			intr_pri;
	uint32_t		lso_max;
	int			tx_bcopy_threshold;
	struct qlcnic_legacy_intr_set	legacy_intr;
	timeout_id_t		watchdog_timer;
	kstat_t			*kstats[1];
	short			context_allocated;

	uint32_t		fw_major;
	uint32_t		fw_minor;
	uint32_t		fw_sub;
	uint32_t		max_rds_rings;
	uint32_t		max_sds_rings;
	uint32_t		max_tx_rings;
#define	MAX_RX_GROUPS		1
	qlcnic_rx_group_t	rx_groups[MAX_RX_GROUPS];
	uint32_t		num_rx_groups;
	qlcnic_rx_ring_reserved_attr_t rx_reserved_attr[MAX_SDS_RINGS];
	qlcnic_tx_ring_reserved_attr_t tx_reserved_attr[MAX_TX_RINGS];
	/* Num of bufs posted in phantom */
	uint32_t	MaxTxDescCount;
	uint32_t	MaxRxDescCount;
	uint32_t	MaxJumboRxDescCount;
	uint32_t	MaxLroRxDescCount;
	uint32_t	tx_recycle_threshold;
	uint32_t	max_tx_dma_hdls;

	uint32_t	intr_coalesce_rx_time_us;
	uint32_t	intr_coalesce_rx_pkts;
	uint32_t	intr_coalesce_tx_time_us;
	uint32_t	intr_coalesce_tx_pkts;

	/* Number of Status descriptors */
	uint32_t	MaxStatusDescCount;

	int		driver_mismatch;
	uint32_t	temp;

	int		rx_bcopy_threshold;

	/*
	 * Receive instances. These can be either one per port,
	 * or one per peg, etc.
	 */
	kmutex_t		lock;
	int		is_up;
	int		init_att_done;

	/* context interface shared between card and host */
	RingContext		*ctxDesc;
	uint64_t		ctxDesc_physAddr;
	ddi_dma_handle_t 	ctxDesc_dma_handle;
	ddi_acc_handle_t 	ctxDesc_acc_handle;

	struct {
		void			*addr;
		uint64_t		phys_addr;
		ddi_dma_handle_t	dma_handle;
		ddi_acc_handle_t	acc_handle;
	} dummy_dma;

	void	(*qlcnic_pci_change_crbwindow)(struct qlcnic_adapter_s *,
		    uint32_t);
	int	(*qlcnic_crb_writelit_adapter)(struct qlcnic_adapter_s *,
		    u64, int);
	unsigned long long
		(*qlcnic_pci_set_window)(struct qlcnic_adapter_s *,
		    unsigned long long);
	int	(*qlcnic_fill_statistics)(struct qlcnic_adapter_s *,
		    struct qlcnic_statistics *);
	int	(*qlcnic_clear_statistics)(struct qlcnic_adapter_s *);
	int	(*qlcnic_hw_write_wx)(struct qlcnic_adapter_s *, u64,
	    void *, int);
	int	(*qlcnic_hw_read_wx)(struct qlcnic_adapter_s *, u64, void *,
	    int);
	int	(*qlcnic_hw_write_ioctl)(struct qlcnic_adapter_s *, u64, void *,
		    int);
	int	(*qlcnic_hw_read_ioctl)(struct qlcnic_adapter_s *, u64, void *,
		    int);
	int	(*qlcnic_pci_mem_write)(struct qlcnic_adapter_s *, u64, u64);
	int	(*qlcnic_pci_mem_read)(struct qlcnic_adapter_s *, u64, u64 *);
	int	(*qlcnic_pci_write_immediate)(struct qlcnic_adapter_s *, u64,
		    u32 *);
	int	(*qlcnic_pci_read_immediate)(struct qlcnic_adapter_s *, u64,
		    u32 *);
	void	(*qlcnic_pci_write_normalize)(struct qlcnic_adapter_s *, u64,
		    u32);
	u32	(*qlcnic_pci_read_normalize)(struct qlcnic_adapter_s *, u64);
	int	(*qlcnic_get_deviceinfo)(struct qlcnic_adapter_s *,
	    struct qlcnic_devinfo *);

	caddr_t			nd_data_p;
	nd_param_t		nd_params[PARAM_COUNT];
	int			need_fw_reset;
	int			heartbeat;
	int			fw_fail_cnt;
	volatile int drv_state;
	int			dev_state;
	int			diag_test;
	int			diag_cnt;
	int			watchdog_running;
	uint32_t		current_ip_addr;
	uint32_t		dbg_level;
	struct qlcnic_nic_intr_coalesce_t coal;
	uint32_t		dev_init_in_progress;
	uint32_t		reset_ack_timeout;
	uint32_t		own_reset;
	int			fm_cap;
	uint32_t		page_size;
	volatile uint32_t	remove_entered;
	boolean_t		rx_ring_created;
	boolean_t		tx_ring_created;
	uint32_t		force_fw_reset;
};  /* qlcnic_adapter structure */

/* Arp structure */
#define	ARP_REQUEST		0x01
#define	ARP_REPLY		0x02

#pragma pack(1)
typedef struct _arp_hdr {
	uint16_t hw_type;
	uint16_t protocol_type;
	uint8_t hw_addr_len;
	uint8_t protocol_addr_len;
	uint16_t opcode;
	uint8_t sender_ha[6];
	uint32_t sender_ip;
	uint8_t target_ha[6];
	uint32_t target_ip;
} arp_hdr_t;
#pragma pack()

#define	QLCNIC_HOST_DUMMY_DMA_SIZE	 1024

/* Following structure is for specific port information    */


#define	PCI_OFFSET_FIRST_RANGE(adapter, off)	\
	((adapter)->ahw.pci_base0 + off)
#define	PCI_OFFSET_SECOND_RANGE(adapter, off)	\
	((adapter)->ahw.pci_base1 + off - SECOND_PAGE_GROUP_START)
#define	PCI_OFFSET_THIRD_RANGE(adapter, off)	\
	((adapter)->ahw.pci_base2 + off - THIRD_PAGE_GROUP_START)
extern uint64_t
pci_base_offset(struct qlcnic_adapter_s *adapter, unsigned long off);

#define	qlcnic_reg_write(_adp_, _off_, _val_)			\
	{							\
		u32	_v1_ = (_val_);			\
		((_adp_)->qlcnic_hw_write_wx((_adp_), (_off_),	\
		    &_v1_, 4));					\
	}

#define	qlcnic_reg_read(_adp_, _off_, _ptr_)			\
	((_adp_)->qlcnic_hw_read_wx((_adp_), (_off_), (_ptr_), 4))


#define	qlcnic_write_w0(_adp_, _idx_, _val_)			\
	((_adp_)->qlcnic_hw_write_wx((_adp_), (_idx_), &(_val_), 4))

#define	qlcnic_read_w0(_adp_, _idx_, _val_)			\
	((_adp_)->qlcnic_hw_read_wx((_adp_), (_idx_), (_val_), 4))

/* Functions available from qlcnic_hw.c */
int qlcnic_get_board_info(struct qlcnic_adapter_s *adapter);
void _qlcnic_write_crb(struct qlcnic_adapter_s *adapter, uint32_t index,
				uint32_t value);
void  qlcnic_write_crb(struct qlcnic_adapter_s *adapter, uint32_t index,
				uint32_t value);
void _qlcnic_read_crb(struct qlcnic_adapter_s *adapter, uint32_t index,
				uint32_t *value);
void  qlcnic_read_crb(struct qlcnic_adapter_s *adapter, uint32_t index,
				uint32_t *value);
int _qlcnic_hw_write(struct qlcnic_adapter_s *adapter,
				u64 off, void *data, int len);
int  qlcnic_hw_write(struct qlcnic_adapter_s *adapter,
				u64 off, void *data, int len);
int _qlcnic_hw_read(struct qlcnic_adapter_s *adapter,
				u64 off, void *data, int len);
int  qlcnic_hw_read(struct qlcnic_adapter_s *adapter,
				u64 off, void *data, int len);
void _qlcnic_hw_block_read(struct qlcnic_adapter_s *adapter,
				u64 off, void *data, int num_words);
void  qlcnic_hw_block_read(struct qlcnic_adapter_s *adapter,
				u64 off, void *data, int num_words);
void _qlcnic_hw_block_write(struct qlcnic_adapter_s *adapter,
				u64 off, void *data, int num_words);
void qlcnic_hw_block_write(struct qlcnic_adapter_s *adapter,
				u64 off, void *data, int num_words);
void qlcnic_mem_block_read(struct qlcnic_adapter_s *adapter, u64 off,
				void *data, int num_words);
void qlcnic_mem_block_write(struct qlcnic_adapter_s *adapter, u64 off,
				void *data, int num_words);
int qlcnic_hw_read_ioctl(qlcnic_adapter *adapter, u64 off, void *data, int len);
int qlcnic_hw_write_ioctl(qlcnic_adapter *adapter, u64 off, void *data, int);
int  qlcnic_macaddr_set(struct qlcnic_adapter_s *, u8 *addr);
void qlcnic_tcl_resetall(struct qlcnic_adapter_s *adapter);
void qlcnic_tcl_phaninit(struct qlcnic_adapter_s *adapter);
void qlcnic_tcl_postimage(struct qlcnic_adapter_s *adapter);
int qlcnic_set_mtu(struct qlcnic_adapter_s *adapter, int new_mtu);
int qlcnic_phy_read(qlcnic_adapter *adapter, u32 reg, u32 *);
int qlcnic_init_port(struct qlcnic_adapter_s *adapter);
void qlcnic_crb_write_adapter(unsigned long off, void *data,
		struct qlcnic_adapter_s *adapter);
int qlcnic_crb_read_adapter(unsigned long off, void *data,
		struct qlcnic_adapter_s *adapter);
int qlcnic_crb_read_val_adapter(unsigned long off,
		struct qlcnic_adapter_s *adapter);
void qlcnic_stop_port(struct qlcnic_adapter_s *adapter);
int qlcnic_set_promisc_mode(struct qlcnic_adapter_s *adapter);
int qlcnic_unset_promisc_mode(struct qlcnic_adapter_s *adapter);

/* qlcnic_hw.c */
u64 qlcnic_get_ioaddr(qlcnic_adapter *adapter, u32 offset);
void qlcnic_pci_change_crbwindow_128M(qlcnic_adapter *adapter, uint32_t wndw);
int qlcnic_crb_writelit_adapter_128M(struct qlcnic_adapter_s *, u64, int);
int qlcnic_hw_write_wx_128M(qlcnic_adapter *adapter, u64 off, void *data,
    int len);
int qlcnic_hw_read_wx_128M(qlcnic_adapter *adapter, u64 off, void *data, int);
int qlcnic_hw_write_ioctl_128M(qlcnic_adapter *adapter, u64 off, void *data,
    int len);
int qlcnic_hw_read_ioctl_128M(qlcnic_adapter *adapter, u64 off, void *data,
    int len);
int qlcnic_pci_mem_write_128M(struct qlcnic_adapter_s *adapter, u64 off,
    u64 data);
int qlcnic_pci_mem_read_128M(struct qlcnic_adapter_s *adapter, u64 off,
    u64 *data);
void qlcnic_pci_write_normalize_128M(qlcnic_adapter *adapter, u64 off, u32);
u32 qlcnic_pci_read_normalize_128M(qlcnic_adapter *adapter, u64 off);
int qlcnic_pci_write_immediate_128M(qlcnic_adapter *adapter, u64 off, u32 *);
int qlcnic_pci_read_immediate_128M(qlcnic_adapter *adapter, u64 off, u32 *data);
unsigned long long qlcnic_pci_set_window_128M(qlcnic_adapter *adapter,
    unsigned long long addr);
int qlcnic_clear_statistics_128M(struct qlcnic_adapter_s *adapter);
int qlcnic_fill_statistics_128M(struct qlcnic_adapter_s *adapter,
    struct qlcnic_statistics *qlcnic_stats);
int qlcnic_get_deviceinfo_2M(struct qlcnic_adapter_s *adapter,
    struct qlcnic_devinfo *qlcnic_devinfo);

void qlcnic_pci_change_crbwindow_2M(qlcnic_adapter *adapter, uint32_t wndw);
int qlcnic_crb_writelit_adapter_2M(struct qlcnic_adapter_s *, u64, int);
int qlcnic_hw_write_wx_2M(qlcnic_adapter *adapter, u64 off, void *data, int);
int qlcnic_pci_mem_write_2M(struct qlcnic_adapter_s *adapter, u64 off, u64);
int qlcnic_pci_mem_read_2M(struct qlcnic_adapter_s *adapter, u64 off, u64 *);
int qlcnic_hw_read_wx_2M(qlcnic_adapter *adapter, u64 off, void *data, int len);
void qlcnic_pci_write_normalize_2M(qlcnic_adapter *adapter, u64 off, u32 data);
u32 qlcnic_pci_read_normalize_2M(qlcnic_adapter *adapter, u64 off);
int qlcnic_pci_write_immediate_2M(qlcnic_adapter *adapter, u64 off, u32 *data);
int qlcnic_pci_read_immediate_2M(qlcnic_adapter *adapter, u64 off, u32 *data);
unsigned long long qlcnic_pci_set_window_2M(qlcnic_adapter *adapter,
    unsigned long long addr);
int qlcnic_clear_statistics_2M(struct qlcnic_adapter_s *adapter);
int qlcnic_fill_statistics_2M(struct qlcnic_adapter_s *adapter,
    struct qlcnic_statistics *qlcnic_stats);
void qlcnic_p3_nic_set_multi(qlcnic_adapter *adapter);
int qlcnic_p3_sre_macaddr_change(qlcnic_adapter *adapter, u8 *addr, u8 op);
/* qlcnic_init.c */
int qlcnic_phantom_init(struct qlcnic_adapter_s *adapter, int first_time);
int qlcnic_load_from_flash(struct qlcnic_adapter_s *adapter);
int qlcnic_pinit_from_rom(qlcnic_adapter *adapter, int verbose);
int qlcnic_rom_fast_read(struct qlcnic_adapter_s *adapter, int addr, int *valp);

/* qlcnic_isr.c */
void qlcnic_handle_phy_intr(qlcnic_adapter *adapter);

/* niu.c */
native_t qlcnic_niu_set_promiscuous_mode(struct qlcnic_adapter_s *adapter,
    qlcnic_niu_prom_mode_t mode);
native_t qlcnic_niu_xg_set_promiscuous_mode(struct qlcnic_adapter_s *adapter,
    qlcnic_niu_prom_mode_t mode);

int qlcnic_niu_xg_macaddr_set(struct qlcnic_adapter_s *adapter,
    qlcnic_ethernet_macaddr_t addr);
native_t qlcnic_niu_disable_xg_port(struct qlcnic_adapter_s *adapter);

long qlcnic_niu_gbe_init_port(long port);
native_t qlcnic_niu_enable_gbe_port(struct qlcnic_adapter_s *adapter,
    qlcnic_niu_gbe_ifmode_t mode);
native_t qlcnic_niu_disable_gbe_port(struct qlcnic_adapter_s *adapter);

int qlcnic_niu_macaddr_get(struct qlcnic_adapter_s *adapter, unsigned char *);
int qlcnic_niu_macaddr_set(struct qlcnic_adapter_s *adapter,
    qlcnic_ethernet_macaddr_t addr);

int qlcnic_niu_xg_set_tx_flow_ctl(struct qlcnic_adapter_s *adapter, int enable);
int qlcnic_niu_gbe_set_rx_flow_ctl(struct qlcnic_adapter_s *adapter, int);
int qlcnic_niu_gbe_set_tx_flow_ctl(struct qlcnic_adapter_s *adapter, int);
int qlcnic_niu_gbe_disable_phy_interrupts(struct qlcnic_adapter_s *);
int qlcnic_niu_gbe_phy_read(struct qlcnic_adapter_s *,
    u32 reg, qlcnic_crbword_t *readval);

/* qlcnic_ctx.c */
int qlcnic_create_rxtx_ctx(struct qlcnic_adapter_s *adapter);
void qlcnic_destroy_rxtx_ctx(struct qlcnic_adapter_s *adapter);
int qlcnic_fw_cmd_set_mtu(struct qlcnic_adapter_s *adapter, int mtu);

/* qlcnic_main.c */
int qlcnic_receive_peg_ready(struct qlcnic_adapter_s *adapter);
void qlcnic_update_cmd_producer(struct qlcnic_tx_ring_s *tx_ring,
    uint32_t crb_producer);
void qlcnic_clear_ilb_mode(struct qlcnic_adapter_s *adapter);
int qlcnic_set_ilb_mode(struct qlcnic_adapter_s *adapter);
void qlcnic_desc_dma_sync(ddi_dma_handle_t handle, uint_t start, uint_t count,
    uint_t range, uint_t unit_size, uint_t direction);
int qlcnic_pci_alloc_consistent(qlcnic_adapter *, int, caddr_t *,
    ddi_dma_cookie_t *, ddi_dma_handle_t *, ddi_acc_handle_t *);
void qlcnic_pci_free_consistent(ddi_dma_handle_t *, ddi_acc_handle_t *);
void qlcnic_fm_ereport(qlcnic_adapter *, char *);
int qlcnic_check_dma_handle(qlcnic_adapter *, ddi_dma_handle_t);
int qlcnic_check_acc_handle(qlcnic_adapter *, ddi_acc_handle_t);
int qlcnic_driver_start(qlcnic_adapter* adapter, int fw_flag, int fw_recovery);
void qlcnic_dump_buf(char *string, uint8_t *buffer, uint8_t wd_size,
    uint32_t count);
void qlcnic_free_rx_buffer(struct qlcnic_sds_ring_s *sds_ring,
    qlcnic_rx_buffer_t *rx_buffer);
/* qlcnic_gem.c */
void qlcnic_destroy_intr(qlcnic_adapter *adapter);
void qlcnic_free_dummy_dma(qlcnic_adapter *adapter);
int qlcnic_atomic_reserve(uint32_t *count_p, uint32_t n);

/*
 * (Internal) return values from ioctl subroutines
 */
enum ioc_reply {
	IOC_INVAL = -1,	/* bad, NAK with EINVAL */
	IOC_DONE, /* OK, reply sent  */
	IOC_ACK, /* OK, just send ACK  */
	IOC_REPLY, /* OK, just send reply */
	IOC_RESTART_ACK, /* OK, restart & ACK */
	IOC_RESTART_REPLY /* OK, restart & reply */
};

/*
 * Shorthand for the NDD parameters
 */
#define	param_adv_autoneg	nd_params[PARAM_ADV_AUTONEG_CAP].ndp_val
#define	param_adv_pause		nd_params[PARAM_ADV_PAUSE_CAP].ndp_val
#define	param_adv_asym_pause	nd_params[PARAM_ADV_ASYM_PAUSE_CAP].ndp_val
#define	param_adv_10000fdx	nd_params[PARAM_ADV_10000FDX_CAP].ndp_val
#define	param_adv_1000fdx	nd_params[PARAM_ADV_1000FDX_CAP].ndp_val
#define	param_adv_1000hdx	nd_params[PARAM_ADV_1000HDX_CAP].ndp_val
#define	param_adv_100fdx	nd_params[PARAM_ADV_100FDX_CAP].ndp_val
#define	param_adv_100hdx	nd_params[PARAM_ADV_100HDX_CAP].ndp_val
#define	param_adv_10fdx		nd_params[PARAM_ADV_10FDX_CAP].ndp_val
#define	param_adv_10hdx		nd_params[PARAM_ADV_10HDX_CAP].ndp_val
#define	param_link_up		nd_params[PARAM_LINK_STATUS].ndp_val
#define	param_link_speed	nd_params[PARAM_LINK_SPEED].ndp_val
#define	param_link_duplex	nd_params[PARAM_LINK_DUPLEX].ndp_val
#define	param_loop_mode		nd_params[PARAM_LOOP_MODE].ndp_val

/*
 * Property lookups
 */
#define	QLCNIC_PROP_EXISTS(d, n) \
	ddi_prop_exists(DDI_DEV_T_ANY, (d), DDI_PROP_DONTPASS, (n))
#define	QLCNIC_PROP_GET_INT(d, n) \
	ddi_prop_get_int(DDI_DEV_T_ANY, (d), DDI_PROP_DONTPASS, (n), -1)

/*
 * Driver state values
 */
#define	QLCNIC_DRV_OPERATIONAL	0x1
#define	QLCNIC_DRV_QUIESCENT	0x2
#define	QLCNIC_DRV_DOWN		0x3
#define	QLCNIC_DRV_DETACH		0x4
#define	QLCNIC_DRV_SUSPEND		0x5


#define	FW_FAIL_THRESH		20

#define	QLCNIC_ADAPTER_UP_MAGIC	777
/*
 * Bit flags in the 'debug' word ...
 */
#define	QLCNIC_DBG_TRACE	0x00000002 /* general flow tracing */
#define	QLCNIC_DBG_NDD		0x20000000 /* NDD operations */

#define	MBPS_10		10
#define	MBPS_100	100
#define	MBPS_1000	1000

#ifdef __cplusplus
}
#endif

#endif	/* !_QLCNIC_ */
