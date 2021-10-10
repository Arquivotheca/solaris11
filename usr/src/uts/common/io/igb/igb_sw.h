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
 * Copyright(c) 2007-2011 Intel Corporation. All rights reserved.
 */

/*
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_IGB_SW_H
#define	_IGB_SW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/debug.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/strlog.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/kstat.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/dlpi.h>
#include <sys/mac_provider.h>
#include <sys/mac_ether.h>
#include <sys/vlan.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/pci.h>
#include <sys/pcie.h>
#include <sys/sdt.h>
#include <sys/ethernet.h>
#include <sys/pattr.h>
#include <sys/strsubr.h>
#include <sys/netlb.h>
#include <sys/random.h>
#include <inet/common.h>
#include <inet/tcp.h>
#include <inet/ip.h>
#include <inet/mi.h>
#include <inet/nd.h>
#include <sys/ddifm.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/fm/io/ddi.h>
#include <sys/pci_param.h>
#include <sys/iov_param.h>
#include <sys/file.h>
#include "igb_debug.h"
#include "igb_api.h"
#include "igb_82575.h"


#define	MODULE_NAME			"igb"	/* module name */

#define	IGB_SUCCESS			DDI_SUCCESS
#define	IGB_FAILURE			DDI_FAILURE

#define	IGB_UNKNOWN			0x00
#define	IGB_INITIALIZED			0x01
#define	IGB_STARTED			0x02
#define	IGB_STARTED_TX_RX		0x04
#define	IGB_SUSPENDED			0x08
#define	IGB_STALL			0x10
#define	IGB_ERROR			0x80

#define	IGB_RX_STOPPED			0x1

#define	IGB_INTR_NONE			0
#define	IGB_INTR_MSIX			1
#define	IGB_INTR_MSI			2
#define	IGB_INTR_LEGACY			3

#define	IGB_ADAPTER_REGSET		1	/* mapping adapter registers */
#define	IGB_ADAPTER_MSIXTAB		4	/* mapping msi-x table */

#define	IGB_NO_POLL			INT_MAX
#define	IGB_NO_FREE_SLOT		-1

/*
 * Defined for igb live suspend/resume callback
 */
#define	IGB_CB_LSR_ACTIVITIES	(DDI_CB_LSR_ACT_DMA | \
				    DDI_CB_LSR_ACT_PIO | DDI_CB_LSR_ACT_INTR)
#define	IGB_DDI_SR_ACTIVITIES	(DDI_CB_LSR_ACT_DMA | \
				    DDI_CB_LSR_ACT_PIO | DDI_CB_LSR_ACT_INTR)
#ifdef __sparc
#define	IGB_CB_LSR_IMPACTS	(DDI_CB_LSR_IMP_LOSE_POWER | \
			    DDI_CB_LSR_IMP_DEVICE_RESET | \
			    DDI_CB_LSR_IMP_DMA_ADDR_CHANGE | \
			    DDI_CB_LSR_IMP_DMA_PROP_CHANGE)
#define	IGB_DDI_SR_IMPACTS	(DDI_CB_LSR_IMP_LOSE_POWER | \
			    DDI_CB_LSR_IMP_DEVICE_RESET)
#else
#define	IGB_CB_LSR_IMPACTS	(DDI_CB_LSR_IMP_DEVICE_RESET | \
			    DDI_CB_LSR_IMP_DMA_ADDR_CHANGE | \
			    DDI_CB_LSR_IMP_DMA_PROP_CHANGE)
#define	IGB_DDI_SR_IMPACTS	(DDI_CB_LSR_IMP_DEVICE_RESET)
#endif

#define	IGB_IS_STARTED(igb)	((igb->igb_state & IGB_STARTED) != 0)
#define	IGB_IS_SUSPENDED(igb)	((igb->igb_state & IGB_SUSPENDED) != 0)
#define	IGB_IS_SUSPENDED_PIO(igb) \
		    ((igb->sr_activities & DDI_CB_LSR_ACT_PIO) != 0)
#define	IGB_IS_SUSPENDED_DMA(igb) \
		    ((igb->sr_activities & DDI_CB_LSR_ACT_DMA) != 0)
#define	IGB_IS_SUSPENDED_INTR(igb) \
		    ((igb->sr_activities & DDI_CB_LSR_ACT_INTR) != 0)
#define	IGB_IS_SUSPENDED_ADAPTER(igb)	((igb->sr_impacts & \
		    (DDI_CB_LSR_IMP_LOSE_POWER | \
		    DDI_CB_LSR_IMP_DEVICE_RESET)) != 0)
#define	IGB_IS_SUSPENDED_DMA_UNBIND(igb)	((igb->sr_impacts & \
		    (DDI_CB_LSR_IMP_DMA_ADDR_CHANGE | \
		    DDI_CB_LSR_IMP_DMA_PROP_CHANGE)) != 0)
#define	IGB_IS_SUSPENDED_DMA_FREE(igb)	((igb->sr_impacts & \
		    DDI_CB_LSR_IMP_DMA_PROP_CHANGE) != 0)

/*
 * Reconfigure flags for live suspend and resume
 */
#define	IGB_SR_RC_START				0x0001
#define	IGB_SR_RC_STOP				0x0002

/*
 * Maximum unicast addresses: must be the maximum of all supported types
 */
#define	MAX_NUM_UNICAST_ADDRESSES	32

#define	MCAST_ALLOC_COUNT		256
#define	MAX_COOKIE			18
#define	MIN_NUM_TX_DESC			2

/*
 * Number of settings for interrupt throttle rate (ITR).  There is one of
 * these per msi-x vector and it needs to be the maximum of all silicon
 * types supported by this driver.
 */
#define	MAX_NUM_EITR			25

/*
 * Maximum values for user configurable parameters
 */
#define	MAX_TX_RING_SIZE		4096
#define	MAX_RX_RING_SIZE		4096

#define	MAX_MTU				9216
#define	MAX_RX_LIMIT_PER_INTR		4096

#define	MAX_RX_COPY_THRESHOLD		9216
#define	MAX_TX_COPY_THRESHOLD		9216
#define	MAX_TX_RECYCLE_THRESHOLD	DEFAULT_TX_RING_SIZE
#define	MAX_TX_OVERLOAD_THRESHOLD	DEFAULT_TX_RING_SIZE
#define	MAX_TX_RESCHED_THRESHOLD	DEFAULT_TX_RING_SIZE
#define	MAX_MCAST_NUM			8192

/*
 * Minimum values for user configurable parameters
 */
#define	MIN_TX_RING_SIZE		64
#define	MIN_RX_RING_SIZE		64

#define	MIN_MTU				ETHERMIN
#define	MIN_RX_LIMIT_PER_INTR		16

#define	MIN_RX_COPY_THRESHOLD		0
#define	MIN_TX_COPY_THRESHOLD		0
#define	MIN_TX_RECYCLE_THRESHOLD	MIN_NUM_TX_DESC
#define	MIN_TX_OVERLOAD_THRESHOLD	MIN_NUM_TX_DESC
#define	MIN_TX_RESCHED_THRESHOLD	MIN_NUM_TX_DESC
#define	MIN_MCAST_NUM			8

/*
 * Default values for user configurable parameters
 */
#define	DEFAULT_TX_RING_SIZE		512
#define	DEFAULT_RX_RING_SIZE		512

#define	DEFAULT_MTU			ETHERMTU
#define	DEFAULT_RX_LIMIT_PER_INTR	256

#define	DEFAULT_RX_COPY_THRESHOLD	128
#define	DEFAULT_TX_COPY_THRESHOLD	512
#define	DEFAULT_TX_RECYCLE_THRESHOLD	(MAX_COOKIE + 1)
#define	DEFAULT_TX_OVERLOAD_THRESHOLD	MIN_NUM_TX_DESC
#define	DEFAULT_TX_RESCHED_THRESHOLD	128
#define	DEFAULT_TX_RESCHED_THRESHOLD_LOW	32
#define	DEFAULT_MCAST_NUM		4096

#define	IGB_LSO_MAXLEN			65535

#define	TX_DRAIN_TIME			200
#define	RX_DRAIN_TIME			200

#define	TX_STALL_TIME_2S		200	/* in unit of tick */
#define	TX_STALL_TIME_8S		800	/* in unit of tick */

/*
 * Defined for IP header alignment.
 */
#define	IPHDR_ALIGN_ROOM		2

/*
 * Bit flags for attach_progress
 */
#define	ATTACH_PROGRESS_PCI_CONFIG	0x0001	/* PCI config setup */
#define	ATTACH_PROGRESS_REGS_MAP	0x0002	/* Registers mapped */
#define	ATTACH_PROGRESS_PROPS		0x0004	/* Properties initialized */
#define	ATTACH_PROGRESS_ALLOC_INTR	0x0008	/* Interrupts allocated */
#define	ATTACH_PROGRESS_ALLOC_RINGS	0x0010	/* Rings allocated */
#define	ATTACH_PROGRESS_ADD_INTR	0x0020	/* Intr handlers added */
#define	ATTACH_PROGRESS_LOCKS		0x0040	/* Locks initialized */
#define	ATTACH_PROGRESS_INIT_ADAPTER	0x0080	/* Adapter initialized */
#define	ATTACH_PROGRESS_STATS		0x0100	/* Kstats created */
#define	ATTACH_PROGRESS_MAC		0x0200	/* MAC registered */
#define	ATTACH_PROGRESS_ENABLE_INTR	0x0400	/* DDI interrupts enabled */
#define	ATTACH_PROGRESS_FMINIT		0x0800	/* FMA initialized */
#define	ATTACH_PROGRESS_CB		0x1000	/* Callback registered */
#define	ATTACH_PROGRESS_MBX_TASKQ	0x2000	/* Mailbox taskq created */

#define	PROP_ADV_AUTONEG_CAP		"adv_autoneg_cap"
#define	PROP_ADV_1000FDX_CAP		"adv_1000fdx_cap"
#define	PROP_ADV_1000HDX_CAP		"adv_1000hdx_cap"
#define	PROP_ADV_100FDX_CAP		"adv_100fdx_cap"
#define	PROP_ADV_100HDX_CAP		"adv_100hdx_cap"
#define	PROP_ADV_10FDX_CAP		"adv_10fdx_cap"
#define	PROP_ADV_10HDX_CAP		"adv_10hdx_cap"
#define	PROP_DEFAULT_MTU		"default_mtu"
#define	PROP_FLOW_CONTROL		"flow_control"
#define	PROP_TX_RING_SIZE		"tx_ring_size"
#define	PROP_RX_RING_SIZE		"rx_ring_size"
#define	PROP_MR_ENABLE			"mr_enable"
#define	PROP_GROUP_NUM			"group_number"
#define	PROP_RX_QUEUE_NUM		"rx_queue_number"
#define	PROP_TX_QUEUE_NUM		"tx_queue_number"

#define	PROP_INTR_FORCE			"intr_force"
#define	PROP_TX_HCKSUM_ENABLE		"tx_hcksum_enable"
#define	PROP_RX_HCKSUM_ENABLE		"rx_hcksum_enable"
#define	PROP_LSO_ENABLE			"lso_enable"
#define	PROP_TX_HEAD_WB_ENABLE		"tx_head_wb_enable"
#define	PROP_TX_COPY_THRESHOLD		"tx_copy_threshold"
#define	PROP_TX_RECYCLE_THRESHOLD	"tx_recycle_threshold"
#define	PROP_TX_OVERLOAD_THRESHOLD	"tx_overload_threshold"
#define	PROP_TX_RESCHED_THRESHOLD	"tx_resched_threshold"
#define	PROP_RX_COPY_THRESHOLD		"rx_copy_threshold"
#define	PROP_RX_LIMIT_PER_INTR		"rx_limit_per_intr"
#define	PROP_INTR_THROTTLING		"intr_throttling"
#define	PROP_MCAST_MAX_NUM		"mcast_max_num"
#define	PROP_SRIOV_PF			"sriov_pf"
#define	PROP_NUM_VF			"num_vf"
#define	PROP_DMA_COALESCING_ENABLE	"dmac_enable"
#define	PROP_DMA_COALESCING_TIMER	"dmac_timer"
#define	PROP_EEE_ENABLE			"eee_enable"
#define	PROP_POLLING_ENABLE		"polling_enable"

#define	IGB_NUM_IOV_PARAMS		5

#define	igb_iov_param_unicast_slots	\
	igb_iov_param_list[IGB_NUM_IOV_PARAMS - 1]

#define	IGB_LB_NONE			0
#define	IGB_LB_EXTERNAL			1
#define	IGB_LB_INTERNAL_PHY		3
#define	IGB_LB_INTERNAL_SERDES		4


enum ioc_reply {
	IOC_INVAL = -1,	/* bad, NAK with EINVAL */
	IOC_DONE, 	/* OK, reply sent */
	IOC_ACK,	/* OK, just send ACK */
	IOC_REPLY	/* OK, just send reply */
};

/*
 * For s/w context extraction from a tx frame
 */
#define	TX_CXT_SUCCESS		0
#define	TX_CXT_E_LSO_CSUM	(-1)
#define	TX_CXT_E_ETHER_TYPE	(-2)

#define	DMA_SYNC(area, flag)	((void) ddi_dma_sync((area)->dma_handle, \
				    0, 0, (flag)))

#define	DESBALLOC(rcb)		\
	(rcb)->mp = desballoc((unsigned char *)	\
	    (rcb)->rx_buf.address,		\
	    (rcb)->rx_buf.size,			\
	    0, &(rcb)->free_rtn)

/*
 * Defined for ring index operations
 * ASSERT(index < limit)
 * ASSERT(step < limit)
 * ASSERT(index1 < limit)
 * ASSERT(index2 < limit)
 */
#define	NEXT_INDEX(index, step, limit)	(((index) + (step)) < (limit) ? \
	(index) + (step) : (index) + (step) - (limit))
#define	PREV_INDEX(index, step, limit)	((index) >= (step) ? \
	(index) - (step) : (index) + (limit) - (step))
#define	OFFSET(index1, index2, limit)	((index1) <= (index2) ? \
	(index2) - (index1) : (index2) + (limit) - (index1))

#define	LINK_LIST_INIT(_LH)	\
	(_LH)->head = (_LH)->tail = NULL

#define	LIST_GET_HEAD(_LH)	((single_link_t *)((_LH)->head))

#define	LIST_POP_HEAD(_LH)	\
	(single_link_t *)(_LH)->head; \
	{ \
		if ((_LH)->head != NULL) { \
			(_LH)->head = (_LH)->head->link; \
			if ((_LH)->head == NULL) \
				(_LH)->tail = NULL; \
		} \
	}

#define	LIST_GET_TAIL(_LH)	((single_link_t *)((_LH)->tail))

#define	LIST_PUSH_TAIL(_LH, _E)	\
	if ((_LH)->tail != NULL) { \
		(_LH)->tail->link = (single_link_t *)(_E); \
		(_LH)->tail = (single_link_t *)(_E); \
	} else { \
		(_LH)->head = (_LH)->tail = (single_link_t *)(_E); \
	} \
	(_E)->link = NULL;

#define	LIST_GET_NEXT(_LH, _E)		\
	(((_LH)->tail == (single_link_t *)(_E)) ? \
	NULL : ((single_link_t *)(_E))->link)


typedef struct single_link {
	struct single_link	*link;
} single_link_t;

typedef struct link_list {
	single_link_t		*head;
	single_link_t		*tail;
} link_list_t;


/* capability/feature flags */
#define	IGB_FLAG_HAS_DCA	(1 << 0) /* has Direct Cache Access */
#define	IGB_FLAG_VMDQ_RSS	(1 << 1) /* has vmdq + rss capability */
#define	IGB_FLAG_NEED_CTX_IDX	(1 << 2) /* context descriptor needs index */
#define	IGB_FLAG_SPARSE_RX	(1 << 3) /* allocate rx rings sparsely */
#define	IGB_FLAG_FULL_DEV_RESET	(1 << 4) /* global reset required for Tx hang */
#define	IGB_FLAG_DMA_COALESCING	(1 << 5) /* has DMA Coalescing feature */
#define	IGB_FLAG_EEE	(1 << 6) /* has Energy Efficient Ethernet */
#define	IGB_FLAG_THERMAL_SENSOR	(1 << 7) /* has thermal sensor */

#define	IGB_VMDQ_STGY_MAX_NUM	14

/* function pointer for nic-specific functions */
typedef void (*igb_nic_func_t)(struct igb *);
typedef uint32_t (*igb_rar_set_t)(uint32_t, uint32_t, uint32_t);

typedef struct vmdq_strategy {
	uint32_t	group_num;
	uint32_t	queue_num;
} vmdq_strategy_t;

/* adapter-specific info for each supported device type */
typedef struct adapter_info {
	/* limits */
	uint32_t	max_rx_que_num;	/* maximum number of rx queues */
	uint32_t	min_rx_que_num;	/* minimum number of rx queues */
	uint32_t	def_rx_que_num;	/* default number of rx queues */

	uint32_t	max_group_num; /* maximum number of rx groups */
	uint32_t	min_group_num; /* minimum number of rx groups */
	uint32_t	def_group_num; /* default number of rx groups */

	uint32_t	max_tx_que_num;	/* maximum number of tx queues */
	uint32_t	min_tx_que_num;	/* minimum number of tx queues */
	uint32_t	def_tx_que_num;	/* default number of tx queues */

	uint32_t	max_intr_throttle; /* maximum interrupt throttle */
	uint32_t	min_intr_throttle; /* minimum interrupt throttle */
	uint32_t	def_intr_throttle; /* default interrupt throttle */

	uint32_t	max_unicst_rar;	/* size unicast receive address table */
	uint32_t	max_intr_vec;	/* size of interrupt vector table */
	uint32_t	max_vf;		/* maximum VFs supported */

	uint32_t	vmdq_stgy_num;	/* number of valid stgy in the array */
	vmdq_strategy_t	vmdq_stgy[IGB_VMDQ_STGY_MAX_NUM]; /* vmdq stgy array */

	/* function pointers */
	igb_nic_func_t	enable_intr;	/* enable adapter interrupts */
	igb_nic_func_t	setup_msix;	/* set up msi-x vectors */
	igb_nic_func_t	set_rx_classify; /* set up rx classification features */
	igb_rar_set_t	set_vmdq_rar;	/* set vmdq rx address selector */
	igb_nic_func_t	setup_group;	/* set up rx rings & groups */

	/* capabilities */
	uint32_t	flags;		/* capability flags */
	uint32_t	rxdctl_mask;	/* mask for RXDCTL register */
} adapter_info_t;

/*
 * Definitions for VMDQ and RSS settings
 */

/* VMDq modes supported by hardware */
#define	IGB_CLASSIFY_NONE	0
#define	IGB_CLASSIFY_RSS	1
#define	IGB_CLASSIFY_VMDQ	2
#define	IGB_CLASSIFY_VMDQ_RSS	3

/* The default queue in each VMDQ pool */
#define	E1000_VMDQ_MAC_GROUP_DEFAULT_QUEUE	0x100

/* flags for ether addresses */
#define	IGB_ADDRESS_SET		0x01
#define	IGB_ADDRESS_ENABLED	0x02
#define	IGB_ADDRESS_RESERVED	0x80

typedef struct igb_ether_addr {
	uint8_t			flags;
	uint8_t			vmdq_group;
	uint8_t			addr[ETHERADDRL];
} igb_ether_addr_t;

typedef enum {
	USE_NONE,
	USE_COPY,
	USE_DMA
} tx_type_t;

typedef struct tx_context {
	uint32_t		hcksum_flags;
	uint32_t		ip_hdr_len;
	uint32_t		mac_hdr_len;
	uint32_t		l4_proto;
	uint32_t		mss;
	uint32_t		l4_hdr_len;
	boolean_t		lso_flag;
} tx_context_t;

/* Hold address/length of each DMA segment */
typedef struct sw_desc {
	uint64_t		address;
	size_t			length;
} sw_desc_t;

/* Handles and addresses of DMA buffer */
typedef struct dma_buffer {
	caddr_t			address;	/* Virtual address */
	uint64_t		dma_address;	/* DMA (Hardware) address */
	ddi_acc_handle_t	acc_handle;	/* Data access handle */
	ddi_dma_handle_t	dma_handle;	/* DMA handle */
	size_t			size;		/* Buffer size */
	size_t			len;		/* Data length in the buffer */
} dma_buffer_t;

/*
 * Tx Control Block
 */
typedef struct tx_control_block {
	single_link_t		link;
	uint32_t		last_index;
	uint32_t		frag_num;
	uint32_t		desc_num;
	mblk_t			*mp;
	tx_type_t		tx_type;
	ddi_dma_handle_t	tx_dma_handle;
	dma_buffer_t		tx_buf;
	sw_desc_t		desc[MAX_COOKIE];
	int64_t			time_stamp;
} tx_control_block_t;

/*
 * RX Control Block
 */
typedef struct rx_control_block {
	mblk_t			*mp;
	uint32_t		ref_cnt;
	dma_buffer_t		rx_buf;
	frtn_t			free_rtn;
	struct igb_rx_data	*rx_data;
} rx_control_block_t;

/*
 * Software Data Structure for Tx Ring
 */
typedef struct igb_tx_ring {
	uint32_t		index;	/* Ring index: memory allocation */
	uint32_t		queue;	/* Queue index: adapter registers */
	uint32_t		int_vec; /* Interrupt vector */

	/*
	 * Mutexes
	 */
	kmutex_t		tx_lock;
	kmutex_t		recycle_lock;
	kmutex_t		tcb_head_lock;
	kmutex_t		tcb_tail_lock;

	/*
	 * Tx descriptor ring definitions
	 */
	dma_buffer_t		tbd_area;
	union e1000_adv_tx_desc	*tbd_ring;
	uint32_t		tbd_head; /* Index of next tbd to recycle */
	uint32_t		tbd_tail; /* Index of next tbd to transmit */
	uint32_t		tbd_free; /* Number of free tbd */

	/*
	 * Tx control block list definitions
	 */
	tx_control_block_t	*tcb_area;
	tx_control_block_t	**work_list;
	tx_control_block_t	**free_list;
	uint32_t		tcb_head; /* Head index of free list */
	uint32_t		tcb_tail; /* Tail index of free list */
	uint32_t		tcb_free; /* Number of free tcb in free list */

	uint32_t		*tbd_head_wb; /* Head write-back */
	uint32_t		(*tx_recycle)(struct igb_tx_ring *);

	/*
	 * s/w context structure for TCP/UDP checksum offload and LSO.
	 */
	tx_context_t		tx_context;

	/*
	 * Tx ring settings and status
	 */
	uint32_t		ring_size; /* Tx descriptor ring size */
	uint32_t		free_list_size;	/* Tx free list size */

	boolean_t		reschedule;

	/*
	 * Per-ring statistics
	 */
	uint64_t		tx_pkts;	/* Packets Transmitted Count */
	uint64_t		tx_bytes;	/* Bytes Transmitted Count */

#ifdef IGB_DEBUG
	/*
	 * Debug statistics
	 */
	uint32_t		stat_overload;
	uint32_t		stat_fail_no_tbd;
	uint32_t		stat_fail_no_tcb;
	uint32_t		stat_fail_dma_bind;
	uint32_t		stat_reschedule;
	uint32_t		stat_pkt_cnt;
#endif

	/*
	 * Pointer to the igb struct
	 */
	struct igb		*igb;
	mac_ring_handle_t	ring_handle;	/* call back ring handle */
} igb_tx_ring_t;

/*
 * Software Receive Ring
 */
typedef struct igb_rx_data {
	kmutex_t		recycle_lock;	/* Recycle lock, for rcb_tail */

	/*
	 * Rx descriptor ring definitions
	 */
	dma_buffer_t		rbd_area;	/* DMA buffer of rx desc ring */
	union e1000_adv_rx_desc	*rbd_ring;	/* Rx desc ring */
	uint32_t		rbd_next;	/* Index of next rx desc */

	/*
	 * Rx control block list definitions
	 */
	rx_control_block_t	*rcb_area;
	rx_control_block_t	**work_list;	/* Work list of rcbs */
	rx_control_block_t	**free_list;	/* Free list of rcbs */
	uint32_t		rcb_head;	/* Index of next free rcb */
	uint32_t		rcb_tail;	/* Index to put recycled rcb */
	uint32_t		rcb_free;	/* Number of free rcbs */

	/*
	 * Rx sw ring settings and status
	 */
	uint32_t		ring_size;	/* Rx descriptor ring size */
	uint32_t		free_list_size;	/* Rx free list size */

	uint32_t		rcb_pending;
	uint32_t		flag;

	struct igb_rx_ring	*rx_ring;	/* Pointer to rx ring */

	struct igb_rx_data *next;
} igb_rx_data_t;

/*
 * Software Data Structure for Rx Ring
 */
typedef struct igb_rx_ring {
	uint32_t		index;	/* Ring index: memory allocation */
	uint32_t		queue;	/* Queue index: adapter registers */
	uint32_t		int_vec; /* Interrupt vector, OS view */

	igb_rx_data_t		*rx_data;	/* Rx software ring */

	kmutex_t		rx_lock;	/* Rx access lock */

	/*
	 * Per-ring statistics
	 */
	uint64_t		rx_pkts;	/* Packets Received Count */
	uint64_t		rx_bytes;	/* Bytes Received Count */

#ifdef IGB_DEBUG
	/*
	 * Debug statistics
	 */
	uint32_t		stat_frame_error;
	uint32_t		stat_cksum_error;
	uint32_t		stat_exceed_pkt;
	uint32_t		stat_pkt_cnt;
#endif

	struct igb		*igb;		/* Pointer to igb struct */
	mac_ring_handle_t	ring_handle;	/* call back ring handle */
	uint32_t		group_index;	/* group index */
	uint64_t		ring_gen_num;
} igb_rx_ring_t;

/*
 * Software Ring Group
 */
typedef struct igb_group {
	uint32_t		index;		/* index of this rx group */
	mac_group_handle_t	rg_handle;	/* call back group handle */
	mac_group_handle_t	tg_handle;	/* call back group handle */
	struct vf_data		*vf;		/* pointer to the vf data */
	struct igb		*igb;		/* Pointer to igb struct */
} igb_group_t;

/*
 * Constants neeed for PF/VF
 */
#define	IGB_MAX_VF_MC_ENTRIES	30
#define	IGB_MAX_UTA_ENTRIES	128

#define	IGB_VF_FLAG_CTS			(1 << 0) /* VF is clear to send data */
#define	IGB_VF_FLAG_UNI_PROMISC		(1 << 1) /* VF unicast promisc set */
#define	IGB_VF_FLAG_MULTI_PROMISC	(1 << 2) /* VF multicast promisc set */

/*
 * This must be the maximum of all supported types
 */
#define	IGB_MAX_VF		8
#define	IGB_MAX_CONFIG_VF	(IGB_MAX_VF - 1) /* maximum configurable VFs */

/*
 * Function type which implements message dispatch for an API version
 */
typedef void (*msg_api_implement_t)(struct igb *, uint16_t);

/*
 * Store information that PF keeps about each VF
 */
typedef struct vf_data {
	msg_api_implement_t	vf_api;
	uint32_t		num_mac_addrs;
	uint32_t		mac_addr_chg;
	uint32_t		unicast_slots;
	uint32_t		mc_hashes[IGB_MAX_VF_MC_ENTRIES];
	uint32_t		num_mc_hashes;
	uint32_t		port_vlan_id;
	uint16_t		vlan_ids[E1000_VLVF_ARRAY_SIZE];
	uint32_t		num_vlans;
	uint32_t		max_mtu;
	uint32_t		flags;
	time_t			last_nack;
} vf_data_t;

typedef struct igb {
	int 			instance;
	mac_handle_t		mac_hdl;
	ddi_cb_handle_t		cb_hdl;
	dev_info_t		*dip;
	struct e1000_hw		hw;
	struct igb_osdep	osdep;

	adapter_info_t		*capab;		/* adapter capabilities */

	uint32_t		igb_state;
	link_state_t		link_state;
	uint32_t		link_speed;
	uint32_t		link_duplex;
	boolean_t		link_complete;
	timeout_id_t		link_tid;

	uint32_t		reset_count;
	uint32_t		stall_threshold;
	boolean_t		stall_flag;
	uint32_t		attach_progress;
	uint32_t		loopback_mode;
	uint32_t		default_mtu;
	uint32_t		max_frame_size;
	uint32_t		dout_sync;
	boolean_t		uta_wa_done;	/* 82576 UTA workaround */

	uint32_t		rcb_pending;

	uint32_t		mr_enable;	/* Enable multiple rings */
	uint32_t		vmdq_mode;	/* Mode of VMDq */
	int			vmdq_stgy_idx;	/* Index of current vmdq stgy */

	/*
	 * Ring Groups
	 */
	igb_group_t		*groups;	/* Array of groups */
	uint32_t		num_groups;	/* Number of groups in use */
	uint32_t		rxq_per_group;	/* Rx rings per group */
	uint32_t		txq_per_group;	/* Tx rings per group */

	/*
	 * Receive Rings
	 */
	igb_rx_ring_t		*rx_rings;	/* Array of rx rings */
	uint32_t		num_rx_rings;	/* Number of rx rings in use */
	uint32_t		rx_ring_size;	/* Rx descriptor ring size */
	uint32_t		rx_buf_size;	/* Rx buffer size */
	boolean_t		polling_enable;
	igb_rx_data_t		*pending_rx_data; /* rx data wait to be freed */

	/*
	 * Transmit Rings
	 */
	igb_tx_ring_t		*tx_rings;	/* Array of tx rings */
	uint32_t		num_tx_rings;	/* Number of tx rings in use */
	uint32_t		tx_ring_size;	/* Tx descriptor ring size */
	uint32_t		tx_buf_size;	/* Tx buffer size */

	boolean_t		tx_ring_init;
	boolean_t		tx_head_wb_enable; /* Tx head wrtie-back */
	boolean_t		tx_hcksum_enable; /* Tx h/w cksum offload */
	boolean_t 		lso_enable; 	/* Large Segment Offload */
	uint32_t		tx_copy_thresh;	/* Tx copy threshold */
	uint32_t		tx_recycle_thresh; /* Tx recycle threshold */
	uint32_t		tx_overload_thresh; /* Tx overload threshold */
	uint32_t		tx_resched_thresh; /* Tx reschedule threshold */
	boolean_t		rx_hcksum_enable; /* Rx h/w cksum offload */
	uint32_t		rx_copy_thresh; /* Rx copy threshold */
	uint32_t		rx_limit_per_intr; /* Rx pkts per interrupt */
	boolean_t		promisc_mode;

	uint32_t		intr_throttling[MAX_NUM_EITR];
	uint32_t		intr_force;
	boolean_t		dmac_enable; /* DMA Coalescing */
	uint32_t		dmac_timer;
	boolean_t		eee_enable; /* Energy Efficient Ethernet */

	int			intr_type;
	int			intr_cnt;
	int			intr_cap;
	size_t			intr_size;
	uint_t			intr_pri;
	ddi_intr_handle_t	*htable;
	uint32_t		eims_mask;
	uint32_t		ims_mask;

	kmutex_t		gen_lock; /* General lock for device access */
	kmutex_t		watchdog_lock;
	kmutex_t		link_lock;
	kmutex_t		rx_pending_lock;

	ddi_taskq_t		*mbx_taskq;
	kcondvar_t		mbx_hold_cv;
	kcondvar_t		mbx_poll_cv;
	boolean_t		mbx_hold;

	boolean_t		watchdog_enable;
	boolean_t		watchdog_start;
	timeout_id_t		watchdog_tid;

	boolean_t		unicst_init;
	uint32_t		unicst_avail;
	uint32_t		unicst_total;
	igb_ether_addr_t	unicst_addr[MAX_NUM_UNICAST_ADDRESSES];
	uint32_t		mcast_count;
	uint32_t		mcast_alloc_count;
	uint32_t		mcast_max_num;
	struct ether_addr	*mcast_table;

	/*
	 * Kstat definitions
	 */
	kstat_t			*igb_ks;

	uint32_t		param_en_1000fdx_cap:1,
				param_en_1000hdx_cap:1,
				param_en_100t4_cap:1,
				param_en_100fdx_cap:1,
				param_en_100hdx_cap:1,
				param_en_10fdx_cap:1,
				param_en_10hdx_cap:1,
				param_1000fdx_cap:1,
				param_1000hdx_cap:1,
				param_100t4_cap:1,
				param_100fdx_cap:1,
				param_100hdx_cap:1,
				param_10fdx_cap:1,
				param_10hdx_cap:1,
				param_autoneg_cap:1,
				param_pause_cap:1,
				param_asym_pause_cap:1,
				param_rem_fault:1,
				param_adv_1000fdx_cap:1,
				param_adv_1000hdx_cap:1,
				param_adv_100t4_cap:1,
				param_adv_100fdx_cap:1,
				param_adv_100hdx_cap:1,
				param_adv_10fdx_cap:1,
				param_adv_10hdx_cap:1,
				param_adv_autoneg_cap:1,
				param_adv_pause_cap:1,
				param_adv_asym_pause_cap:1,
				param_adv_rem_fault:1,
				param_lp_1000fdx_cap:1,
				param_lp_1000hdx_cap:1,
				param_lp_100t4_cap:1;

	uint32_t		param_lp_100fdx_cap:1,
				param_lp_100hdx_cap:1,
				param_lp_10fdx_cap:1,
				param_lp_10hdx_cap:1,
				param_lp_autoneg_cap:1,
				param_lp_pause_cap:1,
				param_lp_asym_pause_cap:1,
				param_lp_rem_fault:1,
				param_pad_to_32:24;

	/*
	 * FMA capabilities
	 */
	int			fm_capabilities;

	ulong_t			page_size;

	/*
	 * Elements for PF/VF
	 */
	boolean_t		sriov_pf;	/* act as sriov PF driver */
	uint32_t		num_vfs;	/* number of VFs in use */
	uint32_t		pf_grp;		/* group number of PF */
	vf_data_t		vf[IGB_MAX_VF];	/* one of vf[] is used for PF */

	/*
	 * Live suspend and resume
	 */
	uint_t			sr_activities;
	uint_t			sr_impacts;
	uint32_t		sr_reconfigure;
	uint64_t		resume_stamp;
} igb_t;

typedef struct igb_stat {

	kstat_named_t link_speed;	/* Link Speed */
	kstat_named_t reset_count;	/* Reset Count */
	kstat_named_t dout_sync;	/* DMA out of sync */
#ifdef IGB_DEBUG
	kstat_named_t rx_frame_error;	/* Rx Error in Packet */
	kstat_named_t rx_cksum_error;	/* Rx Checksum Error */
	kstat_named_t rx_exceed_pkt;	/* Rx Exceed Max Pkt Count */

	kstat_named_t tx_overload;	/* Tx Desc Ring Overload */
	kstat_named_t tx_fail_no_tcb;	/* Tx Fail Freelist Empty */
	kstat_named_t tx_fail_no_tbd;	/* Tx Fail Desc Ring Empty */
	kstat_named_t tx_fail_dma_bind;	/* Tx Fail DMA bind */
	kstat_named_t tx_reschedule;	/* Tx Reschedule */

	kstat_named_t gprc;	/* Good Packets Received Count */
	kstat_named_t gptc;	/* Good Packets Xmitted Count */
	kstat_named_t gor;	/* Good Octets Received Count */
	kstat_named_t got;	/* Good Octets Xmitd Count */
	kstat_named_t prc64;	/* Packets Received - 64b */
	kstat_named_t prc127;	/* Packets Received - 65-127b */
	kstat_named_t prc255;	/* Packets Received - 127-255b */
	kstat_named_t prc511;	/* Packets Received - 256-511b */
	kstat_named_t prc1023;	/* Packets Received - 511-1023b */
	kstat_named_t prc1522;	/* Packets Received - 1024-1522b */
	kstat_named_t ptc64;	/* Packets Xmitted (64b) */
	kstat_named_t ptc127;	/* Packets Xmitted (64-127b) */
	kstat_named_t ptc255;	/* Packets Xmitted (128-255b) */
	kstat_named_t ptc511;	/* Packets Xmitted (255-511b) */
	kstat_named_t ptc1023;	/* Packets Xmitted (512-1023b) */
	kstat_named_t ptc1522;	/* Packets Xmitted (1024-1522b */
#endif
	kstat_named_t crcerrs;	/* CRC Error Count */
	kstat_named_t symerrs;	/* Symbol Error Count */
	kstat_named_t mpc;	/* Missed Packet Count */
	kstat_named_t scc;	/* Single Collision Count */
	kstat_named_t ecol;	/* Excessive Collision Count */
	kstat_named_t mcc;	/* Multiple Collision Count */
	kstat_named_t latecol;	/* Late Collision Count */
	kstat_named_t colc;	/* Collision Count */
	kstat_named_t dc;	/* Defer Count */
	kstat_named_t sec;	/* Sequence Error Count */
	kstat_named_t rlec;	/* Receive Length Error Count */
	kstat_named_t xonrxc;	/* XON Received Count */
	kstat_named_t xontxc;	/* XON Xmitted Count */
	kstat_named_t xoffrxc;	/* XOFF Received Count */
	kstat_named_t xofftxc;	/* Xoff Xmitted Count */
	kstat_named_t fcruc;	/* Unknown Flow Conrol Packet Rcvd Count */
	kstat_named_t bprc;	/* Broadcasts Pkts Received Count */
	kstat_named_t mprc;	/* Multicast Pkts Received Count */
	kstat_named_t rnbc;	/* Receive No Buffers Count */
	kstat_named_t ruc;	/* Receive Undersize Count */
	kstat_named_t rfc;	/* Receive Frag Count */
	kstat_named_t roc;	/* Receive Oversize Count */
	kstat_named_t rjc;	/* Receive Jabber Count */
	kstat_named_t tor;	/* Total Octets Recvd Count */
	kstat_named_t tot;	/* Total Octets Xmted Count */
	kstat_named_t tpr;	/* Total Packets Received */
	kstat_named_t tpt;	/* Total Packets Xmitted */
	kstat_named_t mptc;	/* Multicast Packets Xmited Count */
	kstat_named_t bptc;	/* Broadcast Packets Xmited Count */
	kstat_named_t algnerrc;	/* Alignment Error count */
	kstat_named_t rxerrc;	/* Rx Error Count */
	kstat_named_t tncrs;	/* Transmit with no CRS */
	kstat_named_t cexterr;	/* Carrier Extension Error count */
	kstat_named_t tsctc;	/* TCP seg contexts xmit count */
	kstat_named_t tsctfc;	/* TCP seg contexts xmit fail count */
} igb_stat_t;

/*
 * Function prototypes in e1000_osdep.c
 */
void e1000_write_pci_cfg(struct e1000_hw *, uint32_t, uint16_t *);
void e1000_read_pci_cfg(struct e1000_hw *, uint32_t, uint16_t *);
int32_t e1000_read_pcie_cap_reg(struct e1000_hw *, uint32_t, uint16_t *);
int32_t e1000_write_pcie_cap_reg(struct e1000_hw *, uint32_t, uint16_t *);

/*
 * Function prototypes in igb_debug.c
 */
#ifdef IGB_DEBUG
void igb_dump_pci(void *);
#endif  /* IGB_DEBUG */

/*
 * Function prototypes in igb_buf.c
 */
int igb_alloc_dma(igb_t *);
void igb_free_dma(igb_t *);
void igb_free_dma_buffer(dma_buffer_t *);
int igb_alloc_tx_ring_mem(igb_tx_ring_t *);
void igb_free_tx_ring_mem(igb_tx_ring_t *);
int igb_alloc_rx_ring_data(igb_rx_ring_t *);
void igb_free_rx_ring_data(igb_rx_data_t *);
void igb_free_pending_rx_data(igb_t *);

/*
 * Function prototypes in igb_main.c
 */
void igb_enable_mailbox_interrupt(igb_t *);
int igb_start(igb_t *);
void igb_stop(igb_t *);
int igb_pf_start(igb_t *, boolean_t);
void igb_pf_stop(igb_t *, boolean_t);
int igb_setup_link(igb_t *, boolean_t);
int igb_unicst_find(igb_t *, const uint8_t *, uint32_t);
int igb_unicst_add(igb_t *, const uint8_t *, uint32_t);
int igb_unicst_remove(igb_t *, const uint8_t *, uint32_t);
int igb_unicst_replace(igb_t *, const uint8_t *, uint32_t);
int igb_multicst_add(igb_t *, const uint8_t *);
int igb_multicst_remove(igb_t *, const uint8_t *);
void igb_setup_multicst(igb_t *, uint16_t);
void igb_rar_clear(igb_t *, uint32_t);
void igb_rar_set_vmdq(igb_t *, const uint8_t *, uint32_t, uint32_t);
enum ioc_reply igb_loopback_ioctl(igb_t *, struct iocblk *, mblk_t *);
void igb_enable_watchdog_timer(igb_t *);
void igb_disable_watchdog_timer(igb_t *);
int igb_atomic_reserve(uint32_t *, uint32_t);
int igb_check_acc_handle(ddi_acc_handle_t);
int igb_check_dma_handle(ddi_dma_handle_t);
void igb_fm_ereport(igb_t *, char *);
void igb_set_fma_flags(int);
boolean_t is_valid_mac_addr(uint8_t *);
void igb_vfta_set(struct e1000_hw *, uint32_t, boolean_t);
int igb_get_param_unicast_slots(pci_plist_t, uint32_t *, char *);
void igb_setup_promisc(igb_t *);

/*
 * Function prototypes in igb_pf.c
 */
int igb_vf_config_handler(dev_info_t *, ddi_cb_action_t, void *, void *,
    void *);
void igb_enable_vf(igb_t *);
void igb_msg_task(void *);
void igb_hold_vfs(igb_t *);
void igb_set_vmolr(igb_t *, uint16_t);
void igb_clear_vf_vfta(igb_t *, uint16_t);
int igb_vlvf_set(igb_t *, uint16_t, boolean_t, uint16_t);
void igb_notify_vfs(igb_t *);
void igb_init_vf_settings(igb_t *);
int igb_transparent_vlan_vf(igb_t *, uint16_t, boolean_t);

/*
 * Function prototypes in igb_gld.c
 */
int igb_m_start(void *);
void igb_m_stop(void *);
int igb_m_promisc(void *, boolean_t);
int igb_m_multicst(void *, boolean_t, const uint8_t *);
int igb_m_unicst(void *, const uint8_t *);
int igb_m_stat(void *, uint_t, uint64_t *);
void igb_m_resources(void *);
void igb_m_ioctl(void *, queue_t *, mblk_t *);
boolean_t igb_m_getcapab(void *, mac_capab_t, void *);
void igb_fill_ring(void *, mac_ring_type_t, const int, const int,
    mac_ring_info_t *, mac_ring_handle_t);
int igb_m_setprop(void *, const char *, mac_prop_id_t, uint_t, const void *);
int igb_m_getprop(void *, const char *, mac_prop_id_t, uint_t, void *);
void igb_m_propinfo(void *, const char *, mac_prop_id_t,
    mac_prop_info_handle_t);
void igb_fill_group(void *arg, mac_ring_type_t, const int,
    mac_group_info_t *, mac_group_handle_t);
int igb_rx_ring_intr_enable(mac_ring_driver_t);
int igb_rx_ring_intr_disable(mac_ring_driver_t);
int igb_enable_vf_port_vlan(igb_t *, uint16_t, uint32_t);

/*
 * Function prototypes in igb_rx.c
 */
mblk_t *igb_rx(igb_rx_ring_t *, int, int);
mblk_t *igb_rx_ring_poll(void *, int, int);
void igb_rx_recycle(caddr_t arg);

/*
 * Function prototypes in igb_tx.c
 */
void igb_free_tcb(tx_control_block_t *);
void igb_put_free_list(igb_tx_ring_t *, link_list_t *);
uint32_t igb_tx_recycle_legacy(igb_tx_ring_t *);
uint32_t igb_tx_recycle_head_wb(igb_tx_ring_t *);
mblk_t *igb_tx_ring_send(void *, mblk_t *);

/*
 * Function prototypes in igb_log.c
 */
void igb_notice(void *, const char *, ...);
void igb_log(void *, const char *, ...);
void igb_error(void *, const char *, ...);

/*
 * Function prototypes in igb_stat.c
 */
int igb_init_stats(igb_t *);
int igb_rx_ring_stat(mac_ring_driver_t, uint_t, uint64_t *);
int igb_tx_ring_stat(mac_ring_driver_t, uint_t, uint64_t *);

#ifdef __cplusplus
}
#endif

#endif /* _IGB_SW_H */
