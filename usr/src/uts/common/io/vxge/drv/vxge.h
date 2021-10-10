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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *  Copyright Exar 2010. Copyright (c) 2002-2010 Neterion, Inc.
 *  All right Reserved.
 *
 *  FileName :    vxge.h
 *
 *  Description:  Link Layer driver declaration
 *
 */

#ifndef _SYS_VXGE_H
#define	_SYS_VXGE_H


#include <sys/types.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/ksynch.h>
#include <sys/stat.h>
#include <sys/modctl.h>
#include <sys/debug.h>
#include <sys/pci.h>
#include <sys/ethernet.h>
#include <sys/vlan.h>
#include <sys/dlpi.h>
#include <sys/taskq.h>
#include <sys/cyclic.h>
#include <sys/ddifm.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/fm/io/ddi.h>

#include <sys/pattr.h>
#include <sys/strsun.h>

#include <sys/mac_provider.h>
#include <sys/mac_ether.h>

#include <sys/types.h>
#include <sys/kstat.h>

#ifdef __cplusplus
extern "C" {
#endif

/* For DIX-only packets */
#define	HEADROOM		0x12

#define	VXGE_IOC	((((('V' << 8) + 'X') << 8) + 'G') << 8)
#define	VXGE_REGISTER_GET	(VXGE_IOC | 1)
#define	VXGE_REGISTER_SET	(VXGE_IOC | 2)
#define	VXGE_REGISTER_BLOCK_GET	(VXGE_IOC | 3)
#define	VXGE_MAC_ADD	(VXGE_IOC | 4)
#define	VXGE_MAC_DEL	(VXGE_IOC | 5)
#define	VXGE_MAC_LIST	(VXGE_IOC | 6)
#define	VXGE_RESET	(VXGE_IOC | 7)
#define	VXGE_PROMISCUOUS_ENABLE	(VXGE_IOC | 8)
#define	VXGE_PROMISCUOUS_DISABLE	(VXGE_IOC | 9)
#define	VXGE_FLICK_LED	(VXGE_IOC | 11)

#define	VXGE_mBIT(loc)	(0x8000000000000000ULL >> (loc))

/* return value if MAC add is present in DA table */
#define	VXGE_MAC_PRESENT	2

/* Debug tracing for XGE driver */
#ifndef VXGE_DEBUG_MASK
#define	VXGE_DEBUG_MASK  0x0
#endif

/*
 * VXGE_DEBUG_INIT	: debug for initialization functions
 * VXGE_DEBUG_TX	: debug transmit related functions
 *  VXGE_DEBUG_RX	: debug recevice related functions
 * VXGE_DEBUG_MEM	: debug memory module
 * VXGE_DEBUG_LOCK	: debug locks
 * VXGE_DEBUG_SEM	: debug semaphore
 * VXGE_DEBUG_ENTRYEXIT	: debug functions by adding entry exit statements
 */
#define	VXGE_DEBUG_INIT		0x00000001
#define	VXGE_DEBUG_TX		0x00000002
#define	VXGE_DEBUG_RX		0x00000004
#define	VXGE_DEBUG_MEM		0x00000008
#define	VXGE_DEBUG_LOCK		0x00000010
#define	VXGE_DEBUG_SEM		0x00000020
#define	VXGE_DEBUG_ENTRYEXIT	0x00000040
#define	VXGE_DEBUG_INTR		0x00000080

#ifdef VXGE_ULD_DEBUG_TRACE
#define	vxge_debug_init(level, fmt, ...) \
	if (VXGE_DEBUG_INIT & trace_mask) \
		vxge_debug_driver(VXGE_ERR, fmt, __VA_ARGS__)

#define	vxge_debug_tx(level, fmt, ...) \
	if (VXGE_DEBUG_TX & trace_mask) \
		vxge_debug_driver(VXGE_ERR, fmt, __VA_ARGS__)

#define	vxge_debug_rx(level, fmt, ...) \
	if (VXGE_DEBUG_RX & trace_mask) \
		vxge_debug_driver(VXGE_ERR, fmt, __VA_ARGS__)

#define	vxge_debug_mem(level, fmt, ...) \
	if (VXGE_DEBUG_MEM & trace_mask) \
		vxge_debug_driver(VXGE_ERR, fmt, __VA_ARGS__)

#define	vxge_debug_lock(level, fmt, ...) \
	if (VXGE_DEBUG_LOCK & trace_mask) \
		vxge_debug_driver(VXGE_ERR, fmt, __VA_ARGS__)

#define	vxge_debug_sem(level, fmt, ...) \
	if (VXGE_DEBUG_SEM & trace_mask) \
		vxge_debug_driver(VXGE_ERR, fmt, __VA_ARGS__)

#define	vxge_debug_entryexit(level, fmt, ...) \
	if (VXGE_DEBUG_ENTRYEXIT & trace_mask) \
		vxge_debug_driver(VXGE_ERR, fmt, __VA_ARGS__)

#define	vxge_debug_intr(level, fmt, ...) \
	if (VXGE_DEBUG_INTR & trace_mask) \
		vxge_debug_driver(VXGE_ERR, fmt, __VA_ARGS__)
#else
#define	vxge_debug_init(level, fmt, ...)
#define	vxge_debug_tx(level, fmt, ...)
#define	vxge_debug_rx(level, fmt, ...)
#define	vxge_debug_mem(level, fmt, ...)
#define	vxge_debug_lock(level, fmt, ...)
#define	vxge_debug_sem(level, fmt, ...)
#define	vxge_debug_entryexit(level, fmt, ...)
#define	vxge_debug_intr(level, fmt, ...)
#endif

#define	VXGE_IFNAME		"vxge"
#define	VXGE_TX_LEVEL_LOW	8
#define	VXGE_TX_LEVEL_HIGH	32
#define	VXGE_TX_LEVEL_CHECK	3

#define	VXGE_DEFAULT_MTU	1500
#define	VXGE_MAXIMUM_MTU	9000

#define	VXGE_MAX_DEVICES	1

#include "vxgehal-ll.h"

#define	VXGE_DEFAULT_FIFO_BLOCKS	85

/* Currently two MSIX vectors are active per vpath */
#define	VXGE_VPATH_MSIX_ACTIVE	(VXGE_HAL_VPATH_MSIX_MAX - 2)
/*
 * These default values can be overridden by vaules in vxge.conf.
 * In xge.conf user has to specify actual (not percentages) values.
 */
#define	VXGE_RX_BUFFER_TOTAL		(127 * 6)
#define	VXGE_RX_BUFFER_POST_HIWAT	(127 * 4)
#define	VXGE_RX_BUFFER_RECYCLE_CACHE	127


/* Control driver to copy or DMA received packets */
#define	VXGE_RX_DMA_LOWAT		128

#define	VXGE_RING_MAIN_QID		0

/*
 * Minimum supported firmware revisions
 */
#define	VXGE_FIRMWARE_VERSION_MAJOR_T1	1
#define	VXGE_FIRMWARE_VERSION_MINOR_T1	6
#define	VXGE_FIRMWARE_VERSION_FIX_T1	0
#define	VXGE_FIRMWARE_VERSION_MAJOR_T1A	1
#define	VXGE_FIRMWARE_VERSION_MINOR_T1A	6
#define	VXGE_FIRMWARE_VERSION_FIX_T1A	0

#define	VXGE_FCS_STRIP_SIZE		4

#if defined(__x86)
#define	VXGE_TX_DMA_LOWAT		128
#else
#define	VXGE_TX_DMA_LOWAT		512
#endif

#define	VXGE_ALIGN_16B		16

/* Current fw modes ranges from 0 to 8 */
#define	VXGE_MAX_FW_MODE		8
#define	VXGE_MIN_FW_MODE		0

#define	VXGE_MAX_DEVICE_SF1_VP17	1
#define	VXGE_MAX_DEVICE_MF2_VP8		2
#define	VXGE_MAX_DEVICE_MF4_VP4		4
#define	VXGE_MAX_DEVICE_MF8_VP2		8

/* vxge states */
#define	VXGE_ERROR			0x10
#define	VXGE_STALL			0x20
#define	VXGE_STARTED			0x40

#define	VXGE_RESHED_RETRY		0x01
#define	VXGE_POOL_LIVE			0x20
#define	VXGE_POOL_DESTROY		0x40

#define	VXGE_TX_DRAIN_TIME		100
/*
 * Try to collapse up to VXGE_RX_PKT_BURST packets into single mblk
 * sequence before mac_rx() is called.
 */
#define	VXGE_DEFAULT_RX_PKT_BURST		32

/* About 1s */
#define	VXGE_DEV_POLL_TICKS			drv_usectohz(1000000)

#define	VXGE_CYCLIC_PERIOD			(1000000000)

#define	VXGE_LSO_MAXLEN				65535
#define	VXGE_CONF_ENABLE_BY_DEFAULT		1
#define	VXGE_CONF_DISABLE_BY_DEFAULT		0

#if defined(__x86)
#define	VXGE_DEFAULT_CONF_VPATH			8
#else
#define	VXGE_DEFAULT_CONF_VPATH			6
#endif

#define	VXGE_DEFAULT_CONF_RING			1
#define	VXGE_DEFAULT_CONF_FIFO			1
#define	VXGE_DEFAULT_CONF_DEV			0xFF
#define	VXGE_DEFAULT_CONF_LRO			0
#define	VXGE_DEFAULT_CONF_LSO			1
#define	VXGE_DEFAULT_CONF_RTH			1
#define	VXGE_DEFAULT_TX_STEERING		0

#define	VXGELL_DEFAULT_UPGRADE			1
#define	VXGELL_DEFAULT_FUNC_MODE		0xFF

#define	VXGELL_DEFAULT_PORT_MODE		0xFF
#define	VXGELL_DEFAULT_PORT_FAILURE		0xFF

#define	VXGE_MAX_PHYS_DEV_DEF			10
#define	VXGE_MIN_PHYS_DEV_DEF			1

#define	VXGE_DUAL_PORT				2

#define	VXGE_MAX_VPATHS				17
#define	VXGE_RTH_BUCKET_SIZE			8
#if defined(__sparc)
#define	VXGE_BLOCK_SIZE				8192
#else
#define	VXGE_BLOCK_SIZE				4096
#endif

/* Bimodal adaptive schema defaults - ENABLED */
#define	VXGE_DEFAULT_BIMODAL_INTERRUPTS		-1
#define	VXGE_DEFAULT_BIMODAL_TIMER_LO_US	24
#define	VXGE_DEFAULT_BIMODAL_TIMER_HI_US	256

/* Interrupt moderation/utilization defaults */
#define	VXGE_DEFAULT_TX_URANGE_A		5
#define	VXGE_DEFAULT_TX_URANGE_B		15
#define	VXGE_DEFAULT_TX_URANGE_C		40
#define	VXGE_DEFAULT_TX_UFC_A			5
#define	VXGE_DEFAULT_TX_UFC_B			40
#define	VXGE_DEFAULT_TX_UFC_C			60
#define	VXGE_DEFAULT_TX_UFC_D			100
#define	VXGE_DEFAULT_TX_TIMER_CI_EN		1
#define	VXGE_DEFAULT_TX_TIMER_AC_EN		1
#define	VXGE_DEFAULT_TX_TIMER_VAL		10000
#define	VXGE_DEFAULT_INDICATE_MAX_PKTS_B	512 /* bimodal */
#define	VXGE_DEFAULT_INDICATE_MAX_PKTS_N	256 /* normal UFC */
#define	VXGE_DEFAULT_RX_URANGE_A		10
#define	VXGE_DEFAULT_RX_URANGE_B		30
#define	VXGE_DEFAULT_RX_URANGE_C		50
#define	VXGE_DEFAULT_RX_UFC_A			1
#define	VXGE_DEFAULT_RX_UFC_B_J			5
#define	VXGE_DEFAULT_RX_UFC_B_N			5
#define	VXGE_DEFAULT_RX_UFC_C_J			10
#define	VXGE_DEFAULT_RX_UFC_C_N			10
#define	VXGE_DEFAULT_RX_UFC_D			15
#define	VXGE_DEFAULT_RX_TIMER_AC_EN		0
#define	VXGE_DEFAULT_RX_TIMER_CI_EN		0
#define	VXGE_DEFAULT_RX_TIMER_VAL		384

#define	VXGE_DEFAULT_FIFO_QUEUE_LENGTH_J	2048
#define	VXGE_DEFAULT_FIFO_QUEUE_LENGTH_N	4096
#define	VXGE_DEFAULT_FIFO_QUEUE_INTR		0
#define	VXGE_DEFAULT_FIFO_RESERVE_THRESHOLD	0
#define	VXGE_DEFAULT_FIFO_MEMBLOCK_SIZE		PAGESIZE
/* 2 pkts can be aggregated (Max) */
#define	VXGE_SW_LRO_MAX_SG_SIZE			2

/*
 * This will force HAL to allocate extra copied buffer per TXDL which
 * size calculated by formula:
 *
 * (ALIGNMENT_SIZE * ALIGNED_FRAGS)
 */
#define	VXGE_DEFAULT_FIFO_ALIGNMENT_SIZE	4096
#define	VXGE_DEFAULT_FIFO_MAX_ALIGNED_FRAGS	1
#if defined(__x86)
#define	VXGE_DEFAULT_FIFO_FRAGS			128
#else
#define	VXGE_DEFAULT_FIFO_FRAGS			64
#endif
#define	VXGE_DEFAULT_FIFO_FRAGS_THRESHOLD	18

#define	VXGE_DEFAULT_RING_QUEUE_BLOCKS_J	2
#define	VXGE_DEFAULT_RING_QUEUE_BLOCKS_N	2
#define	VXGE_RING_QUEUE_BUFFER_MODE_DEFAULT	1
#define	VXGE_DEFAULT_BACKOFF_INTERVAL_US	64
#define	VXGE_DEFAULT_RING_PRIORITY		0
#define	VXGE_DEFAULT_RING_MEMBLOCK_SIZE		PAGESIZE
#define	VXGE_DEFAULT_RING_TIMER_VAL		4

#define	VXGE_DEFAULT_RING_NUM			8
#define	VXGE_DEFAULT_TMAC_UTIL_PERIOD		5
#define	VXGE_DEFAULT_RMAC_UTIL_PERIOD		5
#define	VXGE_DEFAULT_RMAC_HIGH_PTIME		65535
#define	VXGE_DEFAULT_MC_PAUSE_THRESHOLD_Q0Q3	187
#define	VXGE_DEFAULT_MC_PAUSE_THRESHOLD_Q4Q7	187
#define	VXGE_DEFAULT_RMAC_PAUSE_GEN_EN		1
#define	VXGE_DEFAULT_RMAC_PAUSE_GEN_DIS		0
#define	VXGE_DEFAULT_RMAC_PAUSE_RCV_EN		1
#define	VXGE_DEFAULT_RMAC_PAUSE_RCV_DIS		0
#define	VXGE_DEFAULT_INITIAL_MTU		VXGE_HAL_DEFAULT_MTU /* 1500 */
#define	VXGE_DEFAULT_ISR_POLLING_CNT		0
#define	VXGE_DEFAULT_LATENCY_TIMER		255
#define	VXGE_DEFAULT_SHARED_SPLITS		0
#define	VXGE_DEFAULT_STATS_REFRESH_TIME		1

#define	VXGE_DEFAULT_LINK_RETRY_COUNT		10
#define	VXGE_DEFAULT_LINK_VALID_COUNT		10

#define	VXGE_DEFAULT_DEVICE_POLL_TIME_MS	10000
#define	VXGE_DEFAULT_SCHED_TIMER_US		10000

#define	VXGE_INDICATE_PACKET_ARRAY_SIZE	VXGE_HAL_MAX_RING_INDICATE_MAX_PKTS

#define	VXGE_DEFAULT_RTI_BTIMER_VAL		250
#define	VXGE_DEFAULT_RTI_RTIMER_VAL		0
#define	VXGE_DEFAULT_RTI_LTIMER_VAL		1000

#define	VXGE_DEFAULT_TTI_BTIMER_VAL		250
#if defined(__x86)
#define	VXGE_DEFAULT_TTI_RTIMER_VAL		1700
#else
#define	VXGE_DEFAULT_TTI_RTIMER_VAL		0
#endif
#define	VXGE_DEFAULT_TTI_LTIMER_VAL		3677

#define	VXGE_MAX_MAC_PORT			3
#define	VXGE_DEFAULT_FRAGMENT_ALIGNMENT		sizeof (u64)

#if defined(__sparc)
#define	VXGE_DEFAULT_MMRB_COUNT			3 /* 4k */
#define	VXGE_DEFAULT_SPLIT_TRANSACTION	\
				VXGE_EIGHT_SPLIT_TRANSACTION
#else
#define	VXGE_DEFAULT_MMRB_COUNT			1 /* 1k */
#define	VXGE_DEFAULT_SPLIT_TRANSACTION	\
				VXGE_TWO_SPLIT_TRANSACTION
#endif

/* Macros for vlan tag handling */
#define	VXGE_DO_NOT_STRIP_VLAN_TAG		0
#define	VXGE_STRIP_VLAN_TAG			1
#define	VXGE_DEFAULT_STRIP_MODE_VLAN_TAG	VXGE_STRIP_VLAN_TAG

/* debug_module_mask and level */
#define	VXGE_DEFAULT_DEBUG_MODULE_MASK		VXGE_COMPONENT_LL
#define	VXGE_DEFAULT_DEBUG_MODULE_LEVEL		VXGE_NONE
/*
 * default the size of buffers allocated for ndd interface functions
 */
#define	VXGE_STATS_BUFSIZE			6144
#define	VXGE_MRPCIM_STATS_BUFSIZE		65000
#define	VXGE_PCICONF_BUFSIZE			2048
#define	VXGE_ABOUT_BUFSIZE			1024
#define	VXGE_IOCTL_BUFSIZE			64
#define	VXGE_DEVCONF_BUFSIZE			(40 * 2048)

#define	VXGE_RX_1B_LINK(mp, mp_head, mp_end) { \
	if (mp_head == NULL) {                 \
		mp_head = mp;                  \
		mp_end  = mp;                  \
	} else {                               \
		mp_end->b_next = mp;           \
		mp_end = mp;                   \
	}                                      \
}

/*
 * Multiple rings configuration
 */

#define	VXGE_RX_RING_NUM_MIN			1
#define	VXGE_TX_RING_NUM_MIN			1
#define	VXGE_RX_RING_NUM_MAX			16
#define	VXGE_TX_RING_NUM_MAX			16
#define	VXGE_RX_RING_NUM_DEFAULT		VXGE_RX_RING_NUM_MAX
#define	VXGE_TX_RING_NUM_DEFAULT		VXGE_TX_RING_NUM_MAX
#define	VXGE_XBOW_CURRENT_MAX_VPATH		2

#define	VXGE_CONF_GROUP_POLICY_BASIC		0
#define	VXGE_CONF_GROUP_POLICY_VIRT		1
#define	VXGE_CONF_GROUP_POLICY_PERF		2

/*
 * The _PERF configuration enable a fat group of all rx rings, as approachs
 * better fanout performance of the primary interface.
 */
#define	VXGE_CONF_GROUP_POLICY_DEFAULT	VXGE_CONF_GROUP_POLICY_PERF

/*
 * FLAG for addtion / deletion of mac address.
 */

#define	VXGE_ADD_MAC				1
#define	VXGE_REM_MAC				0

/*
 * vxge_event_e
 *
 * This enumeration derived from xgehal_event_e. It extends it
 * for the reason to get serialized context.
 */
/* Renamb the macro from HAL */
#define	VXGE_EVENT_BASE	VXGE_LL_EVENT_BASE
typedef enum vxge_event_e {
	/* LL events */
	VXGE_EVENT_RESCHED_NEEDED	= VXGE_LL_EVENT_BASE + 1,
} vxge_event_e;

typedef struct {
	int rx_pkt_burst;
	int rx_buffer_total;
	int rx_buffer_total_per_rxd;
	int rx_buffer_post_hiwat;
	int rx_buffer_post_hiwat_per_rxd;
	int rx_dma_lowat;
	int tx_dma_lowat;
	int msix_enable;

	int lso_enable;
	int mtu;
	int fm_capabilities;
	int vpath_count;
	int max_config_dev;
	int max_config_vpath;
	int max_fifo_cnt;
	int max_ring_cnt;
	int rth_enable;
	int vlan_promisc_enable;
	int strip_vlan_tag;
	int tx_steering_type;
	int fw_upgrade;
	int func_mode;
	int port_mode;
	int port_failure;
	u32 dev_func_mode;
	u32 flow_control_gen;
	u32 flow_control_rcv;
	u32 debug_module_mask;
	u32 debug_module_level;
	vxge_hal_device_hw_info_t device_hw_info;
} vxge_config_t;

typedef struct vxge_ring vxge_ring_t;
typedef struct vxge_fifo vxge_fifo_t;
typedef struct vxge_vpath vxge_vpath_t;

typedef struct vxge_rx_buffer_t {
	struct vxge_rx_buffer_t	*next;
	struct vxge_rx_buffer_t	*blocknext;
	void				*vaddr;
	dma_addr_t			dma_addr;
	ddi_dma_handle_t		dma_handle;
	ddi_acc_handle_t		dma_acch;
	vxge_ring_t			*ring;
	frtn_t				frtn;
} vxge_rx_buffer_t;

/* Buffer pool for all rings */
typedef struct vxge_rx_buffer_pool_t {
	uint_t			total;		/* total buffers */
	uint_t			size;		/* buffer size */
	vxge_rx_buffer_t	*head;		/* header pointer */
	uint_t			free;		/* free buffers */
	uint_t			post;		/* posted buffers */
	uint_t			post_hiwat;	/* hiwat to stop post */
	spinlock_t		pool_lock;	/* buffer pool lock */
	volatile uint32_t	live;		/* pool status */
	vxge_rx_buffer_t	*recycle_head;	/* Recycle list's head */
	vxge_rx_buffer_t	*recycle_tail;	/* Recycle list's tail */
	uint_t			recycle;	/* No. of buffers recycled */
	spinlock_t		recycle_lock;	/* Buffer recycle lock */
} vxge_rx_buffer_pool_t;

typedef struct vxgedev vxgedev_t;

struct vxge_ring {
	int			id;
	int			valid;
	vxge_hal_vpath_h	channelh;
	vxgedev_t		*vdev;
	mac_resource_handle_t	handle;		/* per ring cookie */
	vxge_rx_buffer_pool_t	bf_pool;
	vxge_rx_buffer_t	*block_pool;
	int			opened;
	int			index;
	uint64_t		ring_intr_cnt;
	uint64_t		ring_rx_cmpl_cnt;
};
struct vxge_fifo {
	int			id;
	int			valid;
	int			level_low;
	int			opened;
	vxge_hal_vpath_h	channelh;
	vxgedev_t		*vdev;
	mac_resource_handle_t	handle;		/* per fifo cookie */
	int			index;
	uint64_t		fifo_tx_cnt;
	uint64_t		fifo_tx_cmpl_cnt;
};

#define	VXGE_STATS_DRV_INC(item)		(vdev->stats.item++)
#define	VXGE_STATS_DRV_ADD(item, value)	(vdev->stats.item += value)

typedef struct vxge_sw_stats_t {
	/* Tx */
	u64 tx_frms;
	u64 txd_alloc_fail;

	/* Virtual Path */
	u64 vpaths_open;
	u64 vpath_open_fail;

	/* Rx */
	u64 rx_frms;
	u64 rxd_alloc_fail;

	/* Misc. */
	u64 link_up;
	u64 link_down;
	u64 kmem_zalloc_fail;
	u64 allocb_fail;
	u64 desballoc_fail;
	u64 ddi_dma_alloc_handle_fail;
	u64 ddi_dma_mem_alloc_fail;
	u64 ddi_dma_addr_bind_handle_fail;
	u64 ddi_dma_addr_unbind_handle_fail;
	u64 kmem_alloc;
	u64 kmem_free;
	u64 spurious_intr_cnt;
	u64 dma_sync_fail_cnt;
	u64 copyit_mblk_buff_cnt;
	u64 rx_tcode_cnt;
	u64 tx_tcode_cnt;
	u64 xmit_compl_cnt;
	u64 xmit_tot_resched_cnt;
	u64 xmit_resched_cnt;
	u64 xmit_tot_update_cnt;
	u64 xmit_update_cnt;
	u64 low_dtr_cnt;
	u64 fifo_txdl_reserve_fail_cnt;
	u64 ring_buff_pool_free_cnt;
	u64 rxd_full_cnt;
	u64 reset_cnt;
	u8 xmit_stall_cnt;
#define	VXGE_STALL_THRESHOLD	2
#define	VXGE_STALL_EVENT	0xFF
} vxge_sw_stats_t;

typedef struct vxge_mac_info {
	u8 macaddr[6];
	u8 macmask[6];
	u16 vpath_no;
} vxge_mac_info_t;

struct vxge_vpath {
	vxge_hal_vpath_h	handle;
	volatile int	id;
	volatile int	kstat_id;
	volatile int	is_configured;
	volatile int	is_open;
	struct vxgedev	*vdev;
	vxge_ring_t	ring;
	vxge_fifo_t	fifo;
	macaddr_t	macaddr;
	macaddr_t	macmask;
	u16		mac_addr_cnt;
#define	VXGE_MAX_MAC_ENTRIES	512
	vxge_mac_info_t mac_list[VXGE_MAX_MAC_ENTRIES];
#define	VXGE_PROMISC_MODE_UNKNOWN	0
#define	VXGE_PROMISC_MODE_DISABLE	1
#define	VXGE_PROMISC_MODE_ENABLE	2
	u16		promiscuous_mode;

	int 		rx_tx_msix_vec;
	int 		alarm_msix_vec;
};
typedef struct macList {
	u16 mac_addr_cnt;
	vxge_mac_info_t macL[VXGE_MAX_MAC_ENTRIES];
} macList_t;


typedef struct vxge_kstat_vpath {
	kstat_named_t	ini_num_mwr_sent;
	kstat_named_t	ini_num_mrd_sent;
	kstat_named_t	ini_num_cpl_rcvd;
	kstat_named_t	ini_num_mwr_byte_sent;
	kstat_named_t	ini_num_cpl_byte_rcvd;
	kstat_named_t	wrcrdtarb_xoff;
	kstat_named_t	rdcrdtarb_xoff;
	kstat_named_t	vpath_genstats_count0;
	kstat_named_t	vpath_genstats_count1;
	kstat_named_t	vpath_genstats_count2;
	kstat_named_t	vpath_genstats_count3;
	kstat_named_t	vpath_genstats_count4;
	kstat_named_t	vpath_gennstats_count5;

	/* XMAC vpath Tx statistics */

	kstat_named_t	tx_ttl_eth_frms;
	kstat_named_t	tx_ttl_eth_octets;
	kstat_named_t	tx_data_octets;
	kstat_named_t	tx_mcast_frms;
	kstat_named_t	tx_bcast_frms;
	kstat_named_t	tx_ucast_frms;
	kstat_named_t	tx_tagged_frms;
	kstat_named_t	tx_vld_ip;
	kstat_named_t	tx_vld_ip_octets;
	kstat_named_t	tx_icmp;
	kstat_named_t	tx_tcp;
	kstat_named_t	tx_rst_tcp;
	kstat_named_t	tx_udp;
	kstat_named_t	tx_lost_ip;
	kstat_named_t	tx_unknown_protocol;
	kstat_named_t	tx_parse_error;
	kstat_named_t	tx_tcp_offload;
	kstat_named_t	tx_retx_tcp_offload;
	kstat_named_t	tx_lost_ip_offload;

	/* XMAC vpath Rx Statistics */

	kstat_named_t	rx_ttl_eth_frms;
	kstat_named_t	rx_vld_frms;
	kstat_named_t	rx_offload_frms;
	kstat_named_t	rx_ttl_eth_octets;
	kstat_named_t	rx_data_octets;
	kstat_named_t	rx_offload_octets;
	kstat_named_t	rx_vld_mcast_frms;
	kstat_named_t	rx_vld_bcast_frms;
	kstat_named_t	rx_accepted_ucast_frms;
	kstat_named_t	rx_accepted_nucast_frms;
	kstat_named_t	rx_tagged_frms;
	kstat_named_t	rx_long_frms;
	kstat_named_t	rx_usized_frms;
	kstat_named_t	rx_osized_frms;
	kstat_named_t	rx_frag_frms;
	kstat_named_t	rx_jabber_frms;
	kstat_named_t 	rx_ttl_64_frms;
	kstat_named_t	rx_ttl_65_127_frms;
	kstat_named_t	rx_ttl_128_255_frms;
	kstat_named_t	rx_ttl_256_511_frms;
	kstat_named_t	rx_ttl_512_1023_frms;
	kstat_named_t	rx_ttl_1024_1518_frms;
	kstat_named_t	rx_ttl_1519_4095_frms;
	kstat_named_t	rx_ttl_4096_8191_frms;
	kstat_named_t	rx_ttl_8192_max_frms;
	kstat_named_t	rx_ttl_gt_max_frms;
	kstat_named_t	rx_ip;
	kstat_named_t	rx_accepted_ip;
	kstat_named_t	rx_ip_octets;
	kstat_named_t	rx_err_ip;
	kstat_named_t	rx_icmp;
	kstat_named_t	rx_tcp;
	kstat_named_t	rx_udp;
	kstat_named_t	rx_err_tcp;
	kstat_named_t	rx_lost_frms;
	kstat_named_t	rx_lost_ip;
	kstat_named_t	rx_lost_ip_offload;
	kstat_named_t	rx_queue_full_discard;
	kstat_named_t	rx_red_discard;
	kstat_named_t	rx_sleep_discard;
	kstat_named_t	rx_mpa_ok_frms;


	kstat_named_t	prog_event_vnum1;
	kstat_named_t	prog_event_vnum0;
	kstat_named_t	prog_event_vnum3;
	kstat_named_t	prog_event_vnum2;
	kstat_named_t	rx_multi_cast_frame_discard;
	kstat_named_t	rx_frm_transferred;
	kstat_named_t	rxd_returned;
	kstat_named_t	rx_mpa_len_fail_frms;
	kstat_named_t	rx_mpa_mrk_fail_frms;
	kstat_named_t	rx_mpa_crc_fail_frms;
	kstat_named_t	rx_permitted_frms;
	kstat_named_t	rx_vp_reset_discarded_frms;
	kstat_named_t	rx_wol_frms;
	kstat_named_t	tx_vp_reset_discarded_frms;

	/* Vpath Software Statistics */

	kstat_named_t	no_nces;
	kstat_named_t	no_sqs;
	kstat_named_t	no_srqs;
	kstat_named_t	no_cqrqs;
	kstat_named_t	no_sessions;

	kstat_named_t	unknown_alarms;
	kstat_named_t	network_sustained_fault;
	kstat_named_t	network_sustained_ok;
	kstat_named_t	kdfcctl_fifo0_overwrite;
	kstat_named_t	kdfcctl_fifo0_poison;
	kstat_named_t	kdfcctl_fifo0_dma_error;
	kstat_named_t	kdfcctl_fifo1_overwrite;
	kstat_named_t	kdfcctl_fifo1_poison;
	kstat_named_t	kdfcctl_fifo1_dma_error;
	kstat_named_t	kdfcctl_fifo2_overwrite;
	kstat_named_t	kdfcctl_fifo2_poison;
	kstat_named_t	kdfcctl_fifo2_dma_error;
	kstat_named_t	dblgen_fifo0_overflow;
	kstat_named_t	dblgen_fifo1_overflow;
	kstat_named_t	dblgen_fifo2_overflow;
	kstat_named_t	statsb_pif_chain_error;
	kstat_named_t	statsb_drop_timeout;
	kstat_named_t	target_illegal_access;
	kstat_named_t	ini_serr_det;
	kstat_named_t	pci_config_status_err;
	kstat_named_t	pci_config_uncor_err;
	kstat_named_t	pci_config_cor_err;
	kstat_named_t	mrpcim_to_vpath_alarms;
	kstat_named_t	srpcim_to_vpath_alarms;
	kstat_named_t	srpcim_msg_to_vpath;

	kstat_named_t	ring_full_cnt;
	kstat_named_t	ring_usage_cnt;
	kstat_named_t	ring_usage_max;
	kstat_named_t	ring_avg_compl_per_intr_cnt;
	kstat_named_t	ring_total_compl_cnt;
	kstat_named_t	rxd_t_code_err_cnt_0;
	kstat_named_t	rxd_t_code_err_cnt_1;
	kstat_named_t	rxd_t_code_err_cnt_2;
	kstat_named_t	rxd_t_code_err_cnt_3;
	kstat_named_t	rxd_t_code_err_cnt_4;
	kstat_named_t	rxd_t_code_err_cnt_5;
	kstat_named_t	rxd_t_code_err_cnt_6;
	kstat_named_t	rxd_t_code_err_cnt_7;
	kstat_named_t	rxd_t_code_err_cnt_8;
	kstat_named_t	rxd_t_code_err_cnt_9;
	kstat_named_t	rxd_t_code_err_cnt_10;
	kstat_named_t	rxd_t_code_err_cnt_11;
	kstat_named_t	rxd_t_code_err_cnt_12;
	kstat_named_t	rxd_t_code_err_cnt_13;
	kstat_named_t	rxd_t_code_err_cnt_14;
	kstat_named_t	rxd_t_code_err_cnt_15;
	kstat_named_t	prc_ring_bump_cnt;
	kstat_named_t	prc_rxdcm_sc_err_cnt;
	kstat_named_t	prc_rxdcm_sc_abort_cnt;
	kstat_named_t	prc_quanta_size_err_cnt;

	kstat_named_t	fifo_full_cnt;
	kstat_named_t	fifo_usage_cnt;
	kstat_named_t	fifo_usage_max;
	kstat_named_t	fifo_avg_compl_per_intr_cnt;
	kstat_named_t	fifo_total_compl_cnt;
	kstat_named_t	fifo_total_posts;
	kstat_named_t	fifo_total_buffers;
	kstat_named_t	fifo_avg_buffers_per_post;
	kstat_named_t	fifo_copied_frags;
	kstat_named_t	fifo_copied_buffers;
	kstat_named_t	fifo_avg_buffer_size;
	kstat_named_t	fifo_avg_post_size;
	kstat_named_t	fifo_total_posts_dang_dtrs;
	kstat_named_t	fifo_total_posts_dang_frags;
	kstat_named_t	txd_t_code_err_cnt_0;
	kstat_named_t	txd_t_code_err_cnt_1;
	kstat_named_t	txd_t_code_err_cnt_2;
	kstat_named_t	txd_t_code_err_cnt_3;
	kstat_named_t	txd_t_code_err_cnt_4;
	kstat_named_t	txd_t_code_err_cnt_5;
	kstat_named_t	txd_t_code_err_cnt_6;
	kstat_named_t	txd_t_code_err_cnt_7;
	kstat_named_t	txd_t_code_err_cnt_8;
	kstat_named_t	txd_t_code_err_cnt_9;
	kstat_named_t	txd_t_code_err_cnt_10;
	kstat_named_t	txd_t_code_err_cnt_11;
	kstat_named_t	txd_t_code_err_cnt_12;
	kstat_named_t	txd_t_code_err_cnt_13;
	kstat_named_t	txd_t_code_err_cnt_14;
	kstat_named_t	txd_t_code_err_cnt_15;
	kstat_named_t	total_frags;
	kstat_named_t	copied_frags;

} vxge_kstat_vpath_t;

typedef struct vxge_sw_kstats_t {

	/* Tx */
	kstat_named_t tx_frms;
	kstat_named_t txd_alloc_fail;

	/* Virtual Path */
	kstat_named_t vpaths_open;
	kstat_named_t vpath_open_fail;

	/* Rx */
	kstat_named_t rx_frms;
	kstat_named_t rxd_alloc_fail;

	/* Misc. */
	kstat_named_t link_up;
	kstat_named_t link_down;
	kstat_named_t kmem_zalloc_fail;
	kstat_named_t allocb_fail;
	kstat_named_t desballoc_fail;
	kstat_named_t ddi_dma_alloc_handle_fail;
	kstat_named_t ddi_dma_mem_alloc_fail;
	kstat_named_t ddi_dma_addr_bind_handle_fail;
	kstat_named_t ddi_dma_addr_unbind_handle_fail;
	kstat_named_t kmem_alloc;
	kstat_named_t kmem_free;
} vxge_sw_kstats_t;

typedef struct vxge_kstat_port {
	kstat_named_t	rx_ttl_frms;
	kstat_named_t	rx_vld_frms;
	kstat_named_t	rx_offload_frms;
	kstat_named_t	rx_ttl_octets;
	kstat_named_t	rx_data_octets;
	kstat_named_t	rx_offload_octets;
	kstat_named_t	rx_vld_mcast_frms;
	kstat_named_t	rx_vld_bcast_frms;
	kstat_named_t	rx_accepted_ucast_frms;
	kstat_named_t	rx_accepted_nucast_frms;
	kstat_named_t	rx_tagged_frms;
	kstat_named_t	rx_long_frms;
	kstat_named_t	rx_usized_frms;
	kstat_named_t	rx_osized_frms;
	kstat_named_t	rx_frag_frms;
	kstat_named_t	rx_jabber_frms;
	kstat_named_t	rx_ttl_64_frms;
	kstat_named_t	rx_ttl_65_127_frms;
	kstat_named_t	rx_ttl_128_255_frms;
	kstat_named_t	rx_ttl_256_511_frms;
	kstat_named_t	rx_ttl_512_1023_frms;
	kstat_named_t	rx_ttl_1024_1518_frms;
	kstat_named_t	rx_ttl_1519_4095_frms;
	kstat_named_t	rx_ttl_4096_8191_frms;
	kstat_named_t	rx_ttl_8192_max_frms;
	kstat_named_t	rx_ttl_gt_max_frms;
	kstat_named_t	rx_ip;
	kstat_named_t	rx_accepted_ip;
	kstat_named_t	rx_ip_octets;
	kstat_named_t	rx_err_ip;
	kstat_named_t	rx_icmp;
	kstat_named_t	rx_tcp;
	kstat_named_t	rx_udp;
	kstat_named_t	rx_err_tcp;
	kstat_named_t	rx_pause_count;
	kstat_named_t	rx_pause_ctrl_frms;
	kstat_named_t	rx_unsup_ctrl_frms;
	kstat_named_t	rx_fcs_err_frms;
	kstat_named_t	rx_in_rng_len_err_frms;
	kstat_named_t	rx_out_rng_len_err_frms;
	kstat_named_t	rx_drop_frms;
	kstat_named_t	rx_discarded_frms;
	kstat_named_t	rx_drop_ip;
	kstat_named_t	rx_drop_udp;
	kstat_named_t	rx_lacpdu_frms;
	kstat_named_t	rx_marker_pdu_frms;
	kstat_named_t	rx_marker_resp_pdu_frms;
	kstat_named_t	rx_unknown_pdu_frms;
	kstat_named_t	rx_illegal_pdu_frms;
	kstat_named_t	rx_fcs_discard;
	kstat_named_t	rx_len_discard;
	kstat_named_t	rx_switch_discard;
	kstat_named_t	rx_l2_mgmt_discard;
	kstat_named_t	rx_rpa_discard;
	kstat_named_t	rx_trash_discard;
	kstat_named_t	rx_rts_discard;
	kstat_named_t	rx_red_discard;
	kstat_named_t	rx_buff_full_discard;
	kstat_named_t	rx_xgmii_data_err_cnt;
	kstat_named_t	rx_xgmii_ctrl_err_cnt;
	kstat_named_t	rx_xgmii_err_sym;
	kstat_named_t	rx_xgmii_char1_match;
	kstat_named_t	rx_xgmii_char2_match;
	kstat_named_t	rx_xgmii_column1_match;
	kstat_named_t	rx_xgmii_column2_match;
	kstat_named_t	rx_local_fault;
	kstat_named_t	rx_remote_fault;
	kstat_named_t	rx_jettison;

	kstat_named_t	tx_ttl_frms;
	kstat_named_t	tx_ttl_octets;
	kstat_named_t	tx_data_octets;
	kstat_named_t	tx_mcast_frms;
	kstat_named_t	tx_bcast_frms;
	kstat_named_t	tx_ucast_frms;
	kstat_named_t	tx_tagged_frms;
	kstat_named_t	tx_vld_ip;
	kstat_named_t	tx_vld_ip_octets;
	kstat_named_t	tx_icmp;
	kstat_named_t	tx_tcp;
	kstat_named_t	tx_rst_tcp;
	kstat_named_t	tx_udp;
	kstat_named_t	tx_unknown_protocol;
	kstat_named_t	tx_parse_error;
	kstat_named_t	tx_pause_ctrl_frms;
	kstat_named_t	tx_lacpdu_frms;
	kstat_named_t	tx_marker_pdu_frms;
	kstat_named_t	tx_marker_resp_pdu_frms;
	kstat_named_t	tx_drop_ip;
	kstat_named_t	tx_xgmii_char1_match;
	kstat_named_t	tx_xgmii_char2_match;
	kstat_named_t	tx_xgmii_column1_match;
	kstat_named_t	tx_xgmii_column2_match;
	kstat_named_t	tx_drop_frms;
	kstat_named_t	tx_any_err_frms;
} vxge_kstat_port_t;

typedef struct vxge_kstat_aggr {
	kstat_named_t	rx_frms;
	kstat_named_t	rx_data_octets;
	kstat_named_t	rx_mcast_frms;
	kstat_named_t	rx_bcast_frms;
	kstat_named_t	rx_discarded_frms;
	kstat_named_t	rx_errored_frms;
	kstat_named_t	rx_unknown_slow_proto_frms;

	kstat_named_t	tx_frms;
	kstat_named_t	tx_data_octets;
	kstat_named_t	tx_mcast_frms;
	kstat_named_t	tx_bcast_frms;
	kstat_named_t	tx_discarded_frms;
	kstat_named_t	tx_errored_frms;
}  vxge_kstat_aggr_t;

typedef struct vxge_kstat_mrpcim {
	kstat_named_t	pic_ini_rd_drop;
	kstat_named_t	pic_ini_wr_drop;

	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn0;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn1;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn2;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn3;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn4;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn5;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn6;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn7;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn8;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn9;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn10;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn11;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn12;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn13;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn14;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn15;
	kstat_named_t	pic_wrcrb_ph_crdt_depd_vpn16;

	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn0;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn1;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn2;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn3;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn4;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn5;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn6;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn7;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn8;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn9;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn10;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn11;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn12;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn13;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn14;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn15;
	kstat_named_t	pic_wrcrb_pd_crdt_depd_vpn16;

	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn0;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn1;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn2;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn3;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn4;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn5;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn6;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn7;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn8;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn9;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn10;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn11;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn12;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn13;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn14;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn15;
	kstat_named_t	pic_rdcrb_nph_crdt_depd_vpn16;

	kstat_named_t	pic_ini_rd_vpin_drop;
	kstat_named_t	pic_ini_wr_vpin_drop;

	kstat_named_t	pic_genstats_count0;
	kstat_named_t	pic_genstats_count1;
	kstat_named_t	pic_genstats_count2;
	kstat_named_t	pic_genstats_count3;
	kstat_named_t	pic_genstats_count4;
	kstat_named_t	pic_genstats_count5;
	kstat_named_t	pci_rstdrop_cpl;
	kstat_named_t	pci_rstdrop_msg;
	kstat_named_t	pci_rstdrop_client1;
	kstat_named_t	pci_rstdrop_client0;
	kstat_named_t	pci_rstdrop_client2;

	kstat_named_t	pci_depl_cplh_vplane0;
	kstat_named_t	pci_depl_nph_vplane0;
	kstat_named_t	pci_depl_ph_vplane0;
	kstat_named_t	pci_depl_cplh_vplane1;
	kstat_named_t	pci_depl_nph_vplane1;
	kstat_named_t	pci_depl_ph_vplane1;
	kstat_named_t	pci_depl_cplh_vplane2;
	kstat_named_t	pci_depl_nph_vplane2;
	kstat_named_t	pci_depl_ph_vplane2;
	kstat_named_t	pci_depl_cplh_vplane3;
	kstat_named_t	pci_depl_nph_vplane3;
	kstat_named_t	pci_depl_ph_vplane3;
	kstat_named_t	pci_depl_cplh_vplane4;
	kstat_named_t	pci_depl_nph_vplane4;
	kstat_named_t	pci_depl_ph_vplane4;
	kstat_named_t	pci_depl_cplh_vplane5;
	kstat_named_t	pci_depl_nph_vplane5;
	kstat_named_t	pci_depl_ph_vplane5;
	kstat_named_t	pci_depl_cplh_vplane6;
	kstat_named_t	pci_depl_nph_vplane6;
	kstat_named_t	pci_depl_ph_vplane6;
	kstat_named_t	pci_depl_cplh_vplane7;
	kstat_named_t	pci_depl_nph_vplane7;
	kstat_named_t	pci_depl_ph_vplane7;
	kstat_named_t	pci_depl_cplh_vplane8;
	kstat_named_t	pci_depl_nph_vplane8;
	kstat_named_t	pci_depl_ph_vplane8;
	kstat_named_t	pci_depl_cplh_vplane9;
	kstat_named_t	pci_depl_nph_vplane9;
	kstat_named_t	pci_depl_ph_vplane9;
	kstat_named_t	pci_depl_cplh_vplane10;
	kstat_named_t	pci_depl_nph_vplane10;
	kstat_named_t	pci_depl_ph_vplane10;
	kstat_named_t	pci_depl_cplh_vplane11;
	kstat_named_t	pci_depl_nph_vplane11;
	kstat_named_t	pci_depl_ph_vplane11;
	kstat_named_t	pci_depl_cplh_vplane12;
	kstat_named_t	pci_depl_nph_vplane12;
	kstat_named_t	pci_depl_ph_vplane12;
	kstat_named_t	pci_depl_cplh_vplane13;
	kstat_named_t	pci_depl_nph_vplane13;
	kstat_named_t	pci_depl_ph_vplane13;
	kstat_named_t	pci_depl_cplh_vplane14;
	kstat_named_t	pci_depl_nph_vplane14;
	kstat_named_t	pci_depl_ph_vplane14;
	kstat_named_t	pci_depl_cplh_vplane15;
	kstat_named_t	pci_depl_nph_vplane15;
	kstat_named_t	pci_depl_ph_vplane15;
	kstat_named_t	pci_depl_cplh_vplane16;
	kstat_named_t	pci_depl_nph_vplane16;
	kstat_named_t	pci_depl_ph_vplane16;

	kstat_named_t	pci_depl_cpld_vplane0;
	kstat_named_t	pci_depl_npd_vplane0;
	kstat_named_t	pci_depl_pd_vplane0;
	kstat_named_t	pci_depl_cpld_vplane1;
	kstat_named_t	pci_depl_npd_vplane1;
	kstat_named_t	pci_depl_pd_vplane1;
	kstat_named_t	pci_depl_cpld_vplane2;
	kstat_named_t	pci_depl_npd_vplane2;
	kstat_named_t	pci_depl_pd_vplane2;
	kstat_named_t	pci_depl_cpld_vplane3;
	kstat_named_t	pci_depl_npd_vplane3;
	kstat_named_t	pci_depl_pd_vplane3;
	kstat_named_t	pci_depl_cpld_vplane4;
	kstat_named_t	pci_depl_npd_vplane4;
	kstat_named_t	pci_depl_pd_vplane4;
	kstat_named_t	pci_depl_cpld_vplane5;
	kstat_named_t	pci_depl_npd_vplane5;
	kstat_named_t	pci_depl_pd_vplane5;
	kstat_named_t	pci_depl_cpld_vplane6;
	kstat_named_t	pci_depl_npd_vplane6;
	kstat_named_t	pci_depl_pd_vplane6;
	kstat_named_t	pci_depl_cpld_vplane7;
	kstat_named_t	pci_depl_npd_vplane7;
	kstat_named_t	pci_depl_pd_vplane7;
	kstat_named_t	pci_depl_cpld_vplane8;
	kstat_named_t	pci_depl_npd_vplane8;
	kstat_named_t	pci_depl_pd_vplane8;
	kstat_named_t	pci_depl_cpld_vplane9;
	kstat_named_t	pci_depl_npd_vplane9;
	kstat_named_t	pci_depl_pd_vplane9;
	kstat_named_t	pci_depl_cpld_vplane10;
	kstat_named_t	pci_depl_npd_vplane10;
	kstat_named_t	pci_depl_pd_vplane10;
	kstat_named_t	pci_depl_cpld_vplane11;
	kstat_named_t	pci_depl_npd_vplane11;
	kstat_named_t	pci_depl_pd_vplane11;
	kstat_named_t	pci_depl_cpld_vplane12;
	kstat_named_t	pci_depl_npd_vplane12;
	kstat_named_t	pci_depl_pd_vplane12;
	kstat_named_t	pci_depl_cpld_vplane13;
	kstat_named_t	pci_depl_npd_vplane13;
	kstat_named_t	pci_depl_pd_vplane13;
	kstat_named_t	pci_depl_cpld_vplane14;
	kstat_named_t	pci_depl_npd_vplane14;
	kstat_named_t	pci_depl_pd_vplane14;
	kstat_named_t	pci_depl_cpld_vplane15;
	kstat_named_t	pci_depl_npd_vplane15;
	kstat_named_t	pci_depl_pd_vplane15;
	kstat_named_t	pci_depl_cpld_vplane16;
	kstat_named_t	pci_depl_npd_vplane16;
	kstat_named_t	pci_depl_pd_vplane16;

	kstat_named_t	tx_ttl_frms_PORT0;
	kstat_named_t	tx_ttl_octets_PORT0;
	kstat_named_t	tx_data_octets_PORT0;
	kstat_named_t	tx_mcast_frms_PORT0;
	kstat_named_t	tx_bcast_frms_PORT0;
	kstat_named_t	tx_ucast_frms_PORT0;
	kstat_named_t	tx_tagged_frms_PORT0;
	kstat_named_t	tx_vld_ip_PORT0;
	kstat_named_t	tx_vld_ip_octets_PORT0;
	kstat_named_t	tx_icmp_PORT0;
	kstat_named_t	tx_tcp_PORT0;
	kstat_named_t	tx_rst_tcp_PORT0;
	kstat_named_t	tx_udp_PORT0;
	kstat_named_t	tx_parse_error_PORT0;
	kstat_named_t	tx_unknown_protocol_PORT0;
	kstat_named_t	tx_pause_ctrl_frms_PORT0;
	kstat_named_t	tx_marker_pdu_frms_PORT0;
	kstat_named_t	tx_lacpdu_frms_PORT0;
	kstat_named_t	tx_drop_ip_PORT0;
	kstat_named_t	tx_marker_resp_pdu_frms_PORT0;
	kstat_named_t	tx_xgmii_char2_match_PORT0;
	kstat_named_t	tx_xgmii_char1_match_PORT0;
	kstat_named_t	tx_xgmii_column2_match_PORT0;
	kstat_named_t	tx_xgmii_column1_match_PORT0;
	kstat_named_t	tx_any_err_frms_PORT0;
	kstat_named_t	tx_drop_frms_PORT0;
	kstat_named_t	rx_ttl_frms_PORT0;
	kstat_named_t	rx_vld_frms_PORT0;
	kstat_named_t	rx_offload_frms_PORT0;
	kstat_named_t	rx_ttl_octets_PORT0;
	kstat_named_t	rx_data_octets_PORT0;
	kstat_named_t	rx_offload_octets_PORT0;
	kstat_named_t	rx_vld_mcast_frms_PORT0;
	kstat_named_t	rx_vld_bcast_frms_PORT0;
	kstat_named_t	rx_accepted_ucast_frms_PORT0;
	kstat_named_t	rx_accepted_nucast_frms_PORT0;
	kstat_named_t	rx_tagged_frms_PORT0;
	kstat_named_t	rx_long_frms_PORT0;
	kstat_named_t	rx_usized_frms_PORT0;
	kstat_named_t	rx_osized_frms_PORT0;
	kstat_named_t	rx_frag_frms_PORT0;
	kstat_named_t	rx_jabber_frms_PORT0;
	kstat_named_t	rx_ttl_64_frms_PORT0;
	kstat_named_t	rx_ttl_65_127_frms_PORT0;
	kstat_named_t	rx_ttl_128_255_frms_PORT0;
	kstat_named_t	rx_ttl_256_511_frms_PORT0;
	kstat_named_t	rx_ttl_512_1023_frms_PORT0;
	kstat_named_t	rx_ttl_1024_1518_frms_PORT0;
	kstat_named_t	rx_ttl_1519_4095_frms_PORT0;
	kstat_named_t	rx_ttl_4096_8191_frms_PORT0;
	kstat_named_t	rx_ttl_8192_max_frms_PORT0;
	kstat_named_t	rx_ttl_gt_max_frms_PORT0;
	kstat_named_t	rx_ip_PORT0;
	kstat_named_t	rx_accepted_ip_PORT0;
	kstat_named_t	rx_ip_octets_PORT0;
	kstat_named_t	rx_err_ip_PORT0;
	kstat_named_t	rx_icmp_PORT0;
	kstat_named_t	rx_tcp_PORT0;
	kstat_named_t	rx_udp_PORT0;
	kstat_named_t	rx_err_tcp_PORT0;
	kstat_named_t	rx_pause_cnt_PORT0;
	kstat_named_t	rx_pause_ctrl_frms_PORT0;
	kstat_named_t	rx_unsup_ctrl_frms_PORT0;
	kstat_named_t	rx_fcs_err_frms_PORT0;
	kstat_named_t	rx_in_rng_len_err_frms_PORT0;
	kstat_named_t	rx_out_rng_len_err_frms_PORT0;
	kstat_named_t	rx_drop_frms_PORT0;
	kstat_named_t	rx_discarded_frms_PORT0;
	kstat_named_t	rx_drop_ip_PORT0;
	kstat_named_t	rx_drp_udp_PORT0;
	kstat_named_t	rx_marker_pdu_frms_PORT0;
	kstat_named_t	rx_lacpdu_frms_PORT0;
	kstat_named_t	rx_unknown_pdu_frms_PORT0;
	kstat_named_t	rx_marker_resp_pdu_frms_PORT0;
	kstat_named_t	rx_fcs_discard_PORT0;
	kstat_named_t	rx_illegal_pdu_frms_PORT0;
	kstat_named_t	rx_switch_discard_PORT0;
	kstat_named_t	rx_len_discard_PORT0;
	kstat_named_t	rx_rpa_discard_PORT0;
	kstat_named_t	rx_l2_mgmt_discard_PORT0;
	kstat_named_t	rx_rts_discard_PORT0;
	kstat_named_t	rx_trash_discard_PORT0;
	kstat_named_t	rx_buff_full_discard_PORT0;
	kstat_named_t	rx_red_discard_PORT0;
	kstat_named_t	rx_xgmii_ctrl_err_cnt_PORT0;
	kstat_named_t	rx_xgmii_data_err_cnt_PORT0;
	kstat_named_t	rx_xgmii_char1_match_PORT0;
	kstat_named_t	rx_xgmii_err_sym_PORT0;
	kstat_named_t	rx_xgmii_column1_match_PORT0;
	kstat_named_t	rx_xgmii_char2_match_PORT0;
	kstat_named_t	rx_local_fault_PORT0;
	kstat_named_t	rx_xgmii_column2_match_PORT0;
	kstat_named_t	rx_jettison_PORT0;
	kstat_named_t	rx_remote_fault_PORT0;

	kstat_named_t	tx_ttl_frms_PORT1;
	kstat_named_t	tx_ttl_octets_PORT1;
	kstat_named_t	tx_data_octets_PORT1;
	kstat_named_t	tx_mcast_frms_PORT1;
	kstat_named_t	tx_bcast_frms_PORT1;
	kstat_named_t	tx_ucast_frms_PORT1;
	kstat_named_t	tx_tagged_frms_PORT1;
	kstat_named_t	tx_vld_ip_PORT1;
	kstat_named_t	tx_vld_ip_octets_PORT1;
	kstat_named_t	tx_icmp_PORT1;
	kstat_named_t	tx_tcp_PORT1;
	kstat_named_t	tx_rst_tcp_PORT1;
	kstat_named_t	tx_udp_PORT1;
	kstat_named_t	tx_parse_error_PORT1;
	kstat_named_t	tx_unknown_protocol_PORT1;
	kstat_named_t	tx_pause_ctrl_frms_PORT1;
	kstat_named_t	tx_marker_pdu_frms_PORT1;
	kstat_named_t	tx_lacpdu_frms_PORT1;
	kstat_named_t	tx_drop_ip_PORT1;
	kstat_named_t	tx_marker_resp_pdu_frms_PORT1;
	kstat_named_t	tx_xgmii_char2_match_PORT1;
	kstat_named_t	tx_xgmii_char1_match_PORT1;
	kstat_named_t	tx_xgmii_column2_match_PORT1;
	kstat_named_t	tx_xgmii_column1_match_PORT1;
	kstat_named_t	tx_any_err_frms_PORT1;
	kstat_named_t	tx_drop_frms_PORT1;
	kstat_named_t	rx_ttl_frms_PORT1;
	kstat_named_t	rx_vld_frms_PORT1;
	kstat_named_t	rx_offload_frms_PORT1;
	kstat_named_t	rx_ttl_octets_PORT1;
	kstat_named_t	rx_data_octets_PORT1;
	kstat_named_t	rx_offload_octets_PORT1;
	kstat_named_t	rx_vld_mcast_frms_PORT1;
	kstat_named_t	rx_vld_bcast_frms_PORT1;
	kstat_named_t	rx_accepted_ucast_frms_PORT1;
	kstat_named_t	rx_accepted_nucast_frms_PORT1;
	kstat_named_t	rx_tagged_frms_PORT1;
	kstat_named_t	rx_long_frms_PORT1;
	kstat_named_t	rx_usized_frms_PORT1;
	kstat_named_t	rx_osized_frms_PORT1;
	kstat_named_t	rx_frag_frms_PORT1;
	kstat_named_t	rx_jabber_frms_PORT1;
	kstat_named_t	rx_ttl_64_frms_PORT1;
	kstat_named_t	rx_ttl_65_127_frms_PORT1;
	kstat_named_t	rx_ttl_128_255_frms_PORT1;
	kstat_named_t	rx_ttl_256_511_frms_PORT1;
	kstat_named_t	rx_ttl_512_1023_frms_PORT1;
	kstat_named_t	rx_ttl_1024_1518_frms_PORT1;
	kstat_named_t	rx_ttl_1519_4095_frms_PORT1;
	kstat_named_t	rx_ttl_4096_8191_frms_PORT1;
	kstat_named_t	rx_ttl_8192_max_frms_PORT1;
	kstat_named_t	rx_ttl_gt_max_frms_PORT1;
	kstat_named_t	rx_ip_PORT1;
	kstat_named_t	rx_accepted_ip_PORT1;
	kstat_named_t	rx_ip_octets_PORT1;
	kstat_named_t	rx_err_ip_PORT1;
	kstat_named_t	rx_icmp_PORT1;
	kstat_named_t	rx_tcp_PORT1;
	kstat_named_t	rx_udp_PORT1;
	kstat_named_t	rx_err_tcp_PORT1;
	kstat_named_t	rx_pause_count_PORT1;
	kstat_named_t	rx_pause_ctrl_frms_PORT1;
	kstat_named_t	rx_unsup_ctrl_frms_PORT1;
	kstat_named_t	rx_fcs_err_frms_PORT1;
	kstat_named_t	rx_in_rng_len_err_frms_PORT1;
	kstat_named_t	rx_out_rng_len_err_frms_PORT1;
	kstat_named_t	rx_drop_frms_PORT1;
	kstat_named_t	rx_discarded_frms_PORT1;
	kstat_named_t	rx_drop_ip_PORT1;
	kstat_named_t	rx_drop_udp_PORT1;
	kstat_named_t	rx_marker_pdu_frms_PORT1;
	kstat_named_t	rx_lacpdu_frms_PORT1;
	kstat_named_t	rx_unknown_pdu_frms_PORT1;
	kstat_named_t	rx_marker_resp_pdu_frms_PORT1;
	kstat_named_t	rx_fcs_discard_PORT1;
	kstat_named_t	rx_illegal_pdu_frms_PORT1;
	kstat_named_t	rx_switch_discard_PORT1;
	kstat_named_t	rx_len_discard_PORT1;
	kstat_named_t	rx_rpa_discard_PORT1;
	kstat_named_t	rx_l2_mgmt_discard_PORT1;
	kstat_named_t	rx_rts_discard_PORT1;
	kstat_named_t	rx_trash_discard_PORT1;
	kstat_named_t	rx_buff_full_discard_PORT1;
	kstat_named_t	rx_red_discard_PORT1;
	kstat_named_t	rx_xgmii_ctrl_err_cnt_PORT1;
	kstat_named_t	rx_xgmii_data_err_cnt_PORT1;
	kstat_named_t	rx_xgmii_char1_match_PORT1;
	kstat_named_t	rx_xgmii_err_sym_PORT1;
	kstat_named_t	rx_xgmii_column1_match_PORT1;
	kstat_named_t	rx_xgmii_char2_match_PORT1;
	kstat_named_t	rx_local_fault_PORT1;
	kstat_named_t	rx_xgmii_column2_match_PORT1;
	kstat_named_t	rx_jettison_PORT1;
	kstat_named_t	rx_remote_fault_PORT1;
	kstat_named_t	tx_ttl_frms_PORT2;
	kstat_named_t	tx_ttl_octets_PORT2;
	kstat_named_t	tx_data_octets_PORT2;
	kstat_named_t	tx_mcast_frms_PORT2;
	kstat_named_t	tx_bcast_frms_PORT2;
	kstat_named_t	tx_ucast_frms_PORT2;
	kstat_named_t	tx_tagged_frms_PORT2;
	kstat_named_t	tx_vld_ip_PORT2;
	kstat_named_t	tx_vld_ip_octets_PORT2;
	kstat_named_t	tx_icmp_PORT2;
	kstat_named_t	tx_tcp_PORT2;
	kstat_named_t	tx_rst_tcp_PORT2;
	kstat_named_t	tx_udp_PORT2;
	kstat_named_t	tx_parse_error_PORT2;
	kstat_named_t	tx_unknown_protocol_PORT2;
	kstat_named_t	tx_pause_ctrl_frms_PORT2;
	kstat_named_t	tx_marker_pdu_frms_PORT2;
	kstat_named_t	tx_lacpdu_frms_PORT2;
	kstat_named_t	tx_drop_ip_PORT2;
	kstat_named_t	tx_marker_resp_pdu_frms_PORT2;
	kstat_named_t	tx_xgmii_char2_match_PORT2;
	kstat_named_t	tx_xgmii_char1_match_PORT2;
	kstat_named_t	tx_xgmii_column2_match_PORT2;
	kstat_named_t	tx_xgmii_column1_match_PORT2;
	kstat_named_t	tx_any_err_frms_PORT2;
	kstat_named_t	tx_drop_frms_PORT2;
	kstat_named_t	rx_ttl_frms_PORT2;
	kstat_named_t	rx_vld_frms_PORT2;
	kstat_named_t	rx_offload_frms_PORT2;
	kstat_named_t	rx_ttl_octets_PORT2;
	kstat_named_t	rx_data_octets_PORT2;
	kstat_named_t	rx_offload_octets_PORT2;
	kstat_named_t	rx_vld_mcast_frms_PORT2;
	kstat_named_t	rx_vld_bcast_frms_PORT2;
	kstat_named_t	rx_accepted_ucast_frms_PORT2;
	kstat_named_t	rx_accepted_nucast_frms_PORT2;
	kstat_named_t	rx_tagged_frms_PORT2;
	kstat_named_t	rx_long_frms_PORT2;
	kstat_named_t	rx_usized_frms_PORT2;
	kstat_named_t	rx_osized_frms_PORT2;
	kstat_named_t	rx_frag_frms_PORT2;
	kstat_named_t	rx_jabber_frms_PORT2;
	kstat_named_t	rx_ttl_64_frms_PORT2;
	kstat_named_t	rx_ttl_65_127_frms_PORT2;
	kstat_named_t	rx_ttl_128_255_frms_PORT2;
	kstat_named_t	rx_ttl_256_511_frms_PORT2;
	kstat_named_t	rx_ttl_512_1023_frms_PORT2;
	kstat_named_t	rx_ttl_1024_1518_frms_PORT2;
	kstat_named_t	rx_ttl_1519_4095_frms_PORT2;
	kstat_named_t	rx_ttl_4096_8191_frms_PORT2;
	kstat_named_t	rx_ttl_8192_max_frms_PORT2;
	kstat_named_t	rx_ttl_gt_max_frms_PORT2;
	kstat_named_t	rx_ip_PORT2;
	kstat_named_t	rx_accepted_ip_PORT2;
	kstat_named_t	rx_ip_octets_PORT2;
	kstat_named_t	rx_err_ip_PORT2;
	kstat_named_t	rx_icmp_PORT2;
	kstat_named_t	rx_tcp_PORT2;
	kstat_named_t	rx_udp_PORT2;
	kstat_named_t	rx_err_tcp_PORT2;
	kstat_named_t	rx_pause_count_PORT2;
	kstat_named_t	rx_pause_ctrl_frms_PORT2;
	kstat_named_t	rx_unsup_ctrl_frms_PORT2;
	kstat_named_t	rx_fcs_err_frms_PORT2;
	kstat_named_t	rx_in_rng_len_err_frms_PORT2;
	kstat_named_t	rx_out_rng_len_err_frms_PORT2;
	kstat_named_t	rx_drop_frms_PORT2;
	kstat_named_t	rx_discarded_frms_PORT2;
	kstat_named_t	rx_drop_ip_PORT2;
	kstat_named_t	rx_drop_udp_PORT2;
	kstat_named_t	rx_marker_pdu_frms_PORT2;
	kstat_named_t	rx_lacpdu_frms_PORT2;
	kstat_named_t	rx_unknown_pdu_frms_PORT2;
	kstat_named_t	rx_marker_resp_pdu_frms_PORT2;
	kstat_named_t	rx_fcs_discard_PORT2;
	kstat_named_t	rx_illegal_pdu_frms_PORT2;
	kstat_named_t	rx_switch_discard_PORT2;
	kstat_named_t	rx_len_discard_PORT2;
	kstat_named_t	rx_rpa_discard_PORT2;
	kstat_named_t	rx_l2_mgmt_discard_PORT2;
	kstat_named_t	rx_rts_discard_PORT2;
	kstat_named_t	rx_trash_discard_PORT2;
	kstat_named_t	rx_buff_full_discard_PORT2;
	kstat_named_t	rx_red_discard_PORT2;
	kstat_named_t	rx_xgmii_ctrl_err_cnt_PORT2;
	kstat_named_t	rx_xgmii_data_err_cnt_PORT2;
	kstat_named_t	rx_xgmii_char1_match_PORT2;
	kstat_named_t	rx_xgmii_err_sym_PORT2;
	kstat_named_t	rx_xgmii_column1_match_PORT2;
	kstat_named_t	rx_xgmii_char2_match_PORT2;
	kstat_named_t	rx_local_fault_PORT2;
	kstat_named_t	rx_xgmii_column2_match_PORT2;
	kstat_named_t	rx_jettison_PORT2;
	kstat_named_t	rx_remote_fault_PORT2;

	kstat_named_t	tx_frms_AGGR0;
	kstat_named_t	tx_data_octets_AGGR0;
	kstat_named_t	tx_mcast_frms_AGGR0;
	kstat_named_t	tx_bcast_frms_AGGR0;
	kstat_named_t	tx_discarded_frms_AGGR0;
	kstat_named_t	tx_errored_frms_AGGR0;
	kstat_named_t	rx_frms_AGGR0;
	kstat_named_t	rx_data_octets_AGGR0;
	kstat_named_t	rx_mcast_frms_AGGR0;
	kstat_named_t	rx_bcast_frms_AGGR0;
	kstat_named_t	rx_discarded_frms_AGGR0;
	kstat_named_t	rx_errored_frms_AGGR0;
	kstat_named_t	rx_ukwn_slow_proto_frms_AGGR0;

	kstat_named_t	tx_frms_AGGR1;
	kstat_named_t	tx_data_octets_AGGR1;
	kstat_named_t	tx_mcast_frms_AGGR1;
	kstat_named_t	tx_bcast_frms_AGGR1;
	kstat_named_t	tx_discarded_frms_AGGR1;
	kstat_named_t	tx_errored_frms_AGGR1;
	kstat_named_t	rx_frms_AGGR1;
	kstat_named_t	rx_data_octets_AGGR1;
	kstat_named_t	rx_mcast_frms_AGGR1;
	kstat_named_t	rx_bcast_frms_AGGR1;
	kstat_named_t	rx_discarded_frms_AGGR1;
	kstat_named_t	rx_errored_frms_AGGR1;
	kstat_named_t	rx_ukwn_slow_proto_frms_AGGR1;

	kstat_named_t	xgmac_global_prog_event_gnum0;
	kstat_named_t	xgmac_global_prog_event_gnum1;

	kstat_named_t	xgmac_orp_lro_events;
	kstat_named_t	xgmac_orp_bs_events;
	kstat_named_t	xgmac_orp_iwarp_events;
	kstat_named_t	xgmac_tx_permitted_frms;

	kstat_named_t	xgmac_port2_tx_any_frms;
	kstat_named_t	xgmac_port1_tx_any_frms;
	kstat_named_t	xgmac_port0_tx_any_frms;

	kstat_named_t	xgmac_port2_rx_any_frms;
	kstat_named_t	xgmac_port1_rx_any_frms;
	kstat_named_t	xgmac_port0_rx_any_frms;
} vxge_kstat_mrpcim_t;

typedef struct v_ioctl_t {
	u64 regtype;
	u64 offset;
	u64 index;
	u64 value;
	u64 data[1];
} v_ioctl_t;

typedef struct port_stats {
	int port_id;
	struct vxgedev *vdev;
} port_stats_t;

struct vxge_error {
	vxge_hal_event_e type;
	u64 vp_id;
};

struct vxgedev {
	vxge_hal_device_h	devh;
	vxge_hal_card_e		dev_revision;
	kmutex_t		genlock;
	mac_handle_t		mh;
	dev_info_t		*dev_info;
	int			resched_avail;
	int			resched_send;
	volatile uint32_t	resched_retry;
	int			tx_copied_max;
	ddi_intr_handle_t	*htable;
	ddi_softint_handle_t	soft_hdl;
	ddi_softint_handle_t	soft_hdl_alarm;
	ddi_softint_handle_t	soft_hdl_status;
	vxge_config_t		config;
	volatile int		is_initialized;
	volatile int		in_reset;
	volatile int		need_start;
	int			mtu;
	int			link_state;
	uint_t			intr_size;
	int			intr_type;
	int			intr_cnt;
	uint_t			intr_pri;
	int			intr_cap;
	int			no_of_vpath;
	int			no_of_ring;
	int			no_of_fifo;
	int			rth_enable;
	int			vlan_promisc_enable;
	int 			strip_vlan_tag;
	int 			tx_steering_type;
	u32			debug_module_mask;
	u32			debug_module_level;
	int			instance;
	unsigned char		*bar0;
	unsigned char		*bar1;
	struct vxge_error	cric_err_event;
	vxge_vpath_t		vpaths[VXGE_HAL_MAX_VIRTUAL_PATHS];
	vxge_hal_vpath_h	vp_handles[VXGE_HAL_MAX_VIRTUAL_PATHS];
	vxge_sw_stats_t	stats;

	/* Below variables are used for vpath selection to transmit a packet */
	unsigned char	vpath_mapping[VXGE_MAX_VPATHS];
	unsigned int	vpath_selector[VXGE_MAX_VPATHS];

	kstat_t		*vxge_kstat_vpath_ksp[VXGE_HAL_MAX_VIRTUAL_PATHS];
	kstat_t		*vxge_kstat_driver_ksp;
	kstat_t		*vxge_kstat_mrpcim_ksp;
	kstat_t		*vxge_kstat_port_ksp[VXGE_HAL_WIRE_PORT_MAX_PORTS];
	port_stats_t	port_no[VXGE_HAL_WIRE_PORT_MAX_PORTS];
	kstat_t		*vxge_kstat_aggr_ksp[VXGE_HAL_WIRE_PORT_MAX_PORTS];
	port_stats_t	aggr_port_no[VXGE_HAL_WIRE_PORT_MAX_PORTS];
	kmutex_t		soft_lock;
	int			soft_running;
	int			link_state_update;
	kmutex_t		soft_lock_alarm;
	kmutex_t		soft_lock_status;
	int			soft_running_alarm;
	int			soft_check_status_running;
	kcondvar_t		cv_initiate_stop;
	u64			vpaths_deployed;
	u32			dev_func_mode;
	u32			flick_led;
	ddi_periodic_t		periodic_id;	/* periodical callback  */
	uint32_t		vdev_state;
};

typedef struct {
	mblk_t			*mblk;
	ddi_dma_handle_t	dma_handles[VXGE_DEFAULT_FIFO_FRAGS];
	int			handle_cnt;
} vxge_txd_priv_t;

typedef struct {
	vxge_rx_buffer_t	*rx_buffer;
} vxge_rxd_priv_t;

int vxge_device_alloc(dev_info_t *dev_info, vxge_config_t *config,
	vxgedev_t **vdev_out);

void vxge_device_free(vxgedev_t *vdev);

void vxge_free_total_dev();

int vxge_device_register(vxgedev_t *vdev);

int vxge_device_unregister(vxgedev_t *vdev);

void vxge_callback_link_up(vxge_hal_device_h devh, void *userdata);

void vxge_callback_link_down(vxge_hal_device_h devh, void *userdata);

static void vxge_onerr_reset(vxgedev_t *vdev);

void vxge_set_fma_flags(vxgedev_t *vdev, int dma_flag);
int vxge_check_acc_handle(pci_reg_h handle);

static int vxge_initiate_start(vxgedev_t *vdev);
static int vxge_initiate_stop(vxgedev_t *vdev);
static void vxge_restore_promiscuous(vxgedev_t *vdev);
static int vxge_close_vpaths(vxgedev_t *vdev);

uint_t vxge_soft_intr_link_handler(char *arg1, char *arg2);
uint_t vxge_soft_intr_reset_handler(char *arg1, char *arg2);
uint_t vxge_soft_state_check_handler(char *arg1, char *arg2);
static vxge_hal_status_e vxge_reset_vpaths(vxgedev_t *vdev);

static vxge_hal_status_e vxge_add_mac_addr(vxgedev_t *vdev,
	vxge_mac_info_t *mac);
static vxge_hal_status_e vxge_store_mac_addr(vxgedev_t *vdev);
static vxge_hal_status_e vxge_restore_mac_addr(vxgedev_t *vdev);
static vxge_hal_status_e vxge_delete_mac_addr(vxgedev_t *vdev,
	vxge_mac_info_t *mac);
static vxge_hal_status_e vxge_search_mac_addr_in_da_table(vxgedev_t *vdev,
	vxge_mac_info_t *mac);
static void vxge_kstat_vpath_init(vxgedev_t *vdev);
static void vxge_kstat_driver_init(vxgedev_t *vdev);
static void vxge_kstat_port_init(vxgedev_t *vdev);
static void vxge_kstat_aggr_init(vxgedev_t *vdev);
static void vxge_kstat_mrpcim_init(vxgedev_t *vdev);
static void vxge_kstat_vpath_destroy(vxgedev_t *vdev);
static void vxge_kstat_port_destroy(vxgedev_t *vdev);
static void vxge_kstat_aggr_destroy(vxgedev_t *vdev);
static void vxge_kstat_mrpcim_destroy(vxgedev_t *vdev);
static void vxge_kstat_driver_destroy(vxgedev_t *vdev);
static void vxge_kstat_destroy(vxgedev_t *vdev);
static void vxge_kstat_init(vxgedev_t *vdev);
static void vxge_reset_stats(vxgedev_t *vdev);

void vxge_callback_crit_err(vxge_hal_device_h devh, void *userdata,
	vxge_hal_event_e type, u64 serr_data);

static int vxge_about_get(vxgedev_t *, uint_t, void *);
static int vxge_pciconf_get(vxgedev_t *, uint_t, void *);
static int vxge_stats_vpath_get(vxgedev_t *, uint_t, void *);
static int vxge_stats_driver_get(vxgedev_t *, uint_t, void *);
static int vxge_mrpcim_stats_get(vxgedev_t *, uint_t, void *);
static int vxge_flick_led_get(vxgedev_t *, uint_t, void *);
static int vxge_bar0_get(vxgedev_t *, uint_t, void *);
static int vxge_debug_ldevel_get(vxgedev_t *, uint_t, void *);
static int vxge_flow_control_gen_get(vxgedev_t *, uint_t, void *);
static int vxge_flow_control_rcv_get(vxgedev_t *, uint_t, void *);
static int vxge_debug_module_mask_get(vxgedev_t *, uint_t, void *);
static int vxge_devconfig_get(vxgedev_t *, uint_t, void *);

static int vxge_identify_adapter(vxgedev_t *, uint_t, char *);
static int vxge_bar0_set(vxgedev_t *, uint_t, char *);
static int vxge_debug_ldevel_set(vxgedev_t *, uint_t, char *);
static int vxge_flow_control_gen_set(vxgedev_t *, uint_t, char *);
static int vxge_flow_control_rcv_set(vxgedev_t *, uint_t, char *);
static int vxge_debug_module_mask_set(vxgedev_t *, uint_t, char *);
static int vxge_rx_destroy_buffer_pool(vxge_ring_t *);
static void vxge_check_status(void *);
static void vxge_fm_ereport(vxgedev_t *, char *);
static void vxge_tx_drain(vxgedev_t *);
static void vxge_vpath_rx_buff_mutex_destroy(vxge_ring_t *);

/*
 * vxge_get_vpath - simplified VPATH accessor
 */
static inline vxge_hal_vpath_h
vxge_get_vpath(vxgedev_t *vdev, int i)
{
	return (vdev->vpaths[i].is_open ? vdev->vpaths[i].handle : NULL);
}

#ifdef __cplusplus
}
#endif

#endif /* _SYS_VXGE_H */
