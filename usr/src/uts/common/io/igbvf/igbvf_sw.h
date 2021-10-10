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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_IGBVF_SW_H
#define	_IGBVF_SW_H

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
#include "igbvf_vf.h"
#include "igbvf_debug.h"


#define	MODULE_NAME			"igbvf"	/* module name */

#define	IGBVF_SUCCESS			DDI_SUCCESS
#define	IGBVF_FAILURE			DDI_FAILURE

#define	IGBVF_UNKNOWN			0x00
#define	IGBVF_INITIALIZED		0x01
#define	IGBVF_STARTED			0x02
#define	IGBVF_SUSPENDED			0x04
#define	IGBVF_SUSPENDED_TX_RX		0x08
#define	IGBVF_RESET			0x10
#define	IGBVF_ERROR			0x80

#define	IGBVF_RX_STOPPED		0x1

#define	IGBVF_ADAPTER_REGSET		1	/* mapping adapter registers */
#define	IGBVF_ADAPTER_MSIXTAB		2	/* mapping msi-x table */

#define	IGBVF_NO_POLL			INT_MAX

/*
 * Defined for igbvf live suspend/resume callback
 */
#define	IGBVF_CB_LSR_ACTIVITIES	(DDI_CB_LSR_ACT_DMA | \
				    DDI_CB_LSR_ACT_PIO | DDI_CB_LSR_ACT_INTR)
#define	IGBVF_DDI_SR_ACTIVITIES	(DDI_CB_LSR_ACT_DMA | \
				    DDI_CB_LSR_ACT_PIO | DDI_CB_LSR_ACT_INTR)
#ifdef __sparc
#define	IGBVF_CB_LSR_IMPACTS	(DDI_CB_LSR_IMP_LOSE_POWER | \
			    DDI_CB_LSR_IMP_DEVICE_RESET | \
			    DDI_CB_LSR_IMP_DMA_ADDR_CHANGE | \
			    DDI_CB_LSR_IMP_DMA_PROP_CHANGE)
#define	IGBVF_DDI_SR_IMPACTS	(DDI_CB_LSR_IMP_LOSE_POWER | \
			    DDI_CB_LSR_IMP_DEVICE_RESET)
#else
#define	IGBVF_CB_LSR_IMPACTS	(DDI_CB_LSR_IMP_DEVICE_RESET | \
			    DDI_CB_LSR_IMP_DMA_ADDR_CHANGE | \
			    DDI_CB_LSR_IMP_DMA_PROP_CHANGE)
#define	IGBVF_DDI_SR_IMPACTS	(DDI_CB_LSR_IMP_DEVICE_RESET)
#endif

#define	IGBVF_IS_STARTED(igbvf)	\
		    ((igbvf->igbvf_state & IGBVF_STARTED) != 0)
#define	IGBVF_IS_SUSPENDED(igbvf)	\
		    ((igbvf->igbvf_state & IGBVF_SUSPENDED) != 0)
#define	IGBVF_IS_SUSPENDED_PIO(igbvf)	\
		    ((igbvf->sr_activities & DDI_CB_LSR_ACT_PIO) != 0)
#define	IGBVF_IS_SUSPENDED_DMA(igbvf)	\
		    ((igbvf->sr_activities & DDI_CB_LSR_ACT_DMA) != 0)
#define	IGBVF_IS_SUSPENDED_INTR(igbvf) \
		    ((igbvf->sr_activities & DDI_CB_LSR_ACT_INTR) != 0)
#define	IGBVF_IS_SUSPENDED_ADAPTER(igbvf)	((igbvf->sr_impacts & \
		    (DDI_CB_LSR_IMP_LOSE_POWER | \
		    DDI_CB_LSR_IMP_DEVICE_RESET)) != 0)
#define	IGBVF_IS_SUSPENDED_DMA_UNBIND(igbvf)	((igbvf->sr_impacts & \
		    (DDI_CB_LSR_IMP_DMA_ADDR_CHANGE | \
		    DDI_CB_LSR_IMP_DMA_PROP_CHANGE)) != 0)
#define	IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)	((igbvf->sr_impacts & \
		    DDI_CB_LSR_IMP_DMA_PROP_CHANGE) != 0)

/*
 * Reconfigure flags for live suspend and resume
 */
#define	IGBVF_SR_RC_START				0x0001
#define	IGBVF_SR_RC_STOP				0x0002

/*
 * Maximum unicast addresses: must be the maximum of all supported types
 */
#define	MAX_NUM_UNICAST_ADDRESSES	24

#define	MCAST_ALLOC_COUNT		30
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
#define	MAX_MCAST_NUM			30

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
#define	DEFAULT_MCAST_NUM		30

#define	IGBVF_LSO_MAXLEN		65535

#define	TX_DRAIN_TIME			200
#define	RX_DRAIN_TIME			200

#define	STALL_WATCHDOG_TIMEOUT		8	/* 8 seconds */

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
#define	ATTACH_PROGRESS_TIMER_TASKQ	0x4000	/* Timer taskq created */

#define	PROP_DEFAULT_MTU		"default_mtu"
#define	PROP_TX_RING_SIZE		"tx_ring_size"
#define	PROP_RX_RING_SIZE		"rx_ring_size"
#define	PROP_RX_GROUP_NUM		"rx_group_number"
#define	PROP_RX_QUEUE_NUM		"rx_queue_number"
#define	PROP_TX_QUEUE_NUM		"tx_queue_number"

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

/*
 * have at least "n" ticks elapsed since the "before" timestamp
 */
#define	ticks_later(before, n)	\
	((ddi_get_lbolt() - (before)) >= (n) ? 1 : 0)

#define	IGBVF_VLAN_PACKET(ptr)	\
	(ntohs(((struct ether_vlan_header *)(uintptr_t)(ptr))->ether_tpid) == \
	ETHERTYPE_VLAN)

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

/* forward declarations */
struct igbvf;
struct e1000_hw;
struct igbvf_osdep;

/* function pointer for nic-specific functions */
typedef void (*igbvf_nic_func_t)(struct igbvf *);
typedef uint32_t (*igbvf_rar_set_t)(uint32_t, uint32_t, uint32_t);

/* adapter-specific info for each supported device type */
typedef struct adapter_info {
	/* limits */
	uint32_t	max_rx_que_num;	/* maximum number of rx queues */
	uint32_t	min_rx_que_num;	/* minimum number of rx queues */
	uint32_t	def_rx_que_num;	/* default number of rx queues */

	uint32_t	max_rx_group_num; /* maximum number of rx groups */
	uint32_t	min_rx_group_num; /* minimum number of rx groups */
	uint32_t	def_rx_group_num; /* default number of rx groups */

	uint32_t	max_tx_que_num;	/* maximum number of tx queues */
	uint32_t	min_tx_que_num;	/* minimum number of tx queues */
	uint32_t	def_tx_que_num;	/* default number of tx queues */

	uint32_t	max_intr_throttle; /* maximum interrupt throttle */
	uint32_t	min_intr_throttle; /* minimum interrupt throttle */
	uint32_t	def_intr_throttle; /* default interrupt throttle */

	uint32_t	max_unicst_rar;	/* size unicast receive address table */
	uint32_t	max_intr_vec;	/* size of interrupt vector table */
	uint32_t	max_vf;		/* maximum VFs supported */

	/* function pointers */
	igbvf_nic_func_t	enable_intr;	/* enable adapter interrupts */
	igbvf_nic_func_t	setup_msix;	/* set up msi-x vectors */

	/* capabilities */
	uint32_t	flags;		/* capability flags */
	uint32_t	rxdctl_mask;	/* mask for RXDCTL register */
} adapter_info_t;

typedef struct igbvf_ether_addr {
	uint16_t	set;
	uint8_t		addr[ETHERADDRL];
} igbvf_ether_addr_t;

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
} tx_control_block_t;

/*
 * RX Control Block
 */
typedef struct rx_control_block {
	mblk_t			*mp;
	uint32_t		ref_cnt;
	dma_buffer_t		rx_buf;
	frtn_t			free_rtn;
	struct igbvf_rx_data	*rx_data;
} rx_control_block_t;

/*
 * Software Data Structure for Tx Ring
 */
typedef struct igbvf_tx_ring {
	uint32_t		index;	/* ring index: memory allocation */
	uint32_t		queue;	/* queue index: adapter registers */
	uint32_t		intr_vect; /* interrupt vector */

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
	uint32_t		(*tx_recycle)(struct igbvf_tx_ring *);

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
	uint32_t		recycle_fail;
	uint32_t		stall_watchdog;

	/*
	 * Per-ring statistics
	 */
	uint64_t		tx_pkts;	/* Packets Transmitted Count */
	uint64_t		tx_bytes;	/* Bytes Transmitted Count */

#ifdef IGBVF_DEBUG
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
	 * Pointer to the igbvf struct
	 */
	struct igbvf		*igbvf;
	mac_ring_handle_t	ring_handle;	/* call back ring handle */
} igbvf_tx_ring_t;

/*
 * Software Receive Ring
 */
typedef struct igbvf_rx_data {
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
	 * Rx ring settings and status
	 */
	uint32_t		ring_size;	/* Rx descriptor ring size */
	uint32_t		free_list_size;	/* Rx free list size */

	uint32_t		rcb_pending;
	uint32_t		flag;

	struct igbvf_rx_ring	*rx_ring;	/* Pointer to rx ring */

	struct igbvf_rx_data	*next;
} igbvf_rx_data_t;

/*
 * Software Data Structure for Rx Ring
 */
typedef struct igbvf_rx_ring {
	uint32_t		index;	/* ring index: memory allocation */
	uint32_t		queue;	/* queue index: adapter registers */
	uint32_t		intr_vect; /* interrupt vector */

	igbvf_rx_data_t		*rx_data;	/* Rx software ring */

	kmutex_t		rx_lock;	/* Rx access lock */

	/*
	 * Per-ring statistics
	 */
	uint64_t		rx_pkts;	/* Packets Received Count */
	uint64_t		rx_bytes;	/* Bytes Received Count */

#ifdef IGBVF_DEBUG
	/*
	 * Debug statistics
	 */
	uint32_t		stat_frame_error;
	uint32_t		stat_cksum_error;
	uint32_t		stat_exceed_pkt;
	uint32_t		stat_pkt_cnt;
#endif

	struct igbvf		*igbvf;		/* Pointer to igbvf struct */
	mac_ring_handle_t	ring_handle;	/* call back ring handle */
	uint32_t		group_index;	/* group index */
	uint64_t		ring_gen_num;
} igbvf_rx_ring_t;

/*
 * Software Receive Ring Group
 */
typedef struct igbvf_rx_group {
	uint32_t		index;		/* index of this rx group */
	mac_group_handle_t	group_handle;   /* call back group handle */
	struct igbvf		*igbvf;		/* Pointer to igbvf struct */
} igbvf_rx_group_t;


typedef struct igbvf {
	int 			instance;
	mac_handle_t		mac_hdl;
	ddi_cb_handle_t		cb_hdl;
	dev_info_t		*dip;
	struct e1000_hw		hw;
	struct igbvf_osdep	osdep;

	adapter_info_t		*capab;		/* adapter capabilities */
#ifdef IGBVF_DEBUG
	boolean_t		prt_reg;	/* print register touches */
#endif

	uint32_t		igbvf_state;
	link_state_t		link_state;
	uint32_t		link_speed;
	uint32_t		link_duplex;

	uint32_t		max_tx_rings;
	uint32_t		max_rx_rings;
	uint32_t		max_mtu;
	uint32_t		min_mtu;

	uint32_t		reset_count;
	uint32_t		attach_progress;
	uint32_t		default_mtu;
	uint32_t		max_frame_size;
	uint32_t		rcb_pending;

	/*
	 * Receive Rings and Groups
	 */
	igbvf_rx_ring_t		*rx_rings;	/* Array of rx rings */
	uint32_t		num_rx_rings;	/* Number of rx rings in use */
	uint32_t		rx_ring_size;	/* Rx descriptor ring size */
	uint32_t		rx_buf_size;	/* Rx buffer size */
	igbvf_rx_group_t	*rx_groups;	/* Array of rx groups */
	uint32_t		num_rx_groups;	/* Number of rx groups in use */
	uint32_t		ring_per_group;	/* rx rings per rx group */
	igbvf_rx_data_t		*pending_rx_data; /* rx data wait to be freed */

	/*
	 * Transmit Rings
	 */
	igbvf_tx_ring_t		*tx_rings;	/* Array of tx rings */
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
	boolean_t		transparent_vlan_enable;

	uint32_t		intr_throttling[MAX_NUM_EITR];
	uint32_t		intr_force;

	int			intr_type;
	int			intr_cnt;
	int			intr_cap;
	size_t			intr_size;
	uint_t			intr_pri;
	ddi_intr_handle_t	*htable;
	uint32_t		eims_mask;

	kmutex_t		gen_lock; /* General lock for device access */
	kmutex_t		watchdog_lock;
	kmutex_t		rx_pending_lock;

	ddi_taskq_t		*timer_taskq;
	ddi_taskq_t		*mbx_taskq;
	kcondvar_t		mbx_hold_cv;
	kcondvar_t		mbx_poll_cv;
	boolean_t		mbx_hold;

	boolean_t		watchdog_enable;
	timeout_id_t		watchdog_tid;

	uint32_t		unicst_total;
	igbvf_ether_addr_t	unicst_addr[MAX_NUM_UNICAST_ADDRESSES];
	uint32_t		mcast_count;
	uint32_t		mcast_alloc_count;
	uint32_t		mcast_max_num;
	struct ether_addr	*mcast_table;

	/*
	 * Kstat definitions
	 */
	kstat_t			*igbvf_ks;

	/*
	 * FMA capabilities
	 */
	int			fm_capabilities;

	ulong_t			page_size;
	clock_t			last_reset;

	/*
	 * Live suspend and resume
	 */
	uint_t			sr_activities;
	uint_t			sr_impacts;
	uint32_t		sr_reconfigure;
} igbvf_t;

typedef struct igbvf_stat {

	kstat_named_t link_speed;	/* Link Speed */
	kstat_named_t reset_count;	/* Reset Count */
#ifdef IGBVF_DEBUG
	kstat_named_t rx_frame_error;	/* Rx Error in Packet */
	kstat_named_t rx_cksum_error;	/* Rx Checksum Error */
	kstat_named_t rx_exceed_pkt;	/* Rx Exceed Max Pkt Count */

	kstat_named_t tx_overload;	/* Tx Desc Ring Overload */
	kstat_named_t tx_fail_no_tcb;	/* Tx Fail Freelist Empty */
	kstat_named_t tx_fail_no_tbd;	/* Tx Fail Desc Ring Empty */
	kstat_named_t tx_fail_dma_bind;	/* Tx Fail DMA bind */
	kstat_named_t tx_reschedule;	/* Tx Reschedule */
#endif

	kstat_named_t gprc;	/* Good Packets Received Count */
	kstat_named_t gptc;	/* Good Packets Xmitted Count */
	kstat_named_t gorc;	/* Good Octets Received Count */
	kstat_named_t gotc;	/* Good Octets Xmitd Count */
	kstat_named_t mprc;	/* Multicast Pkts Received Count */
	kstat_named_t gprlbc;	/* Good Rx Packets Loopback Count */
	kstat_named_t gptlbc;	/* Good Tx Packets Loopback Count */
	kstat_named_t gorlbc;	/* Good Rx Octets Loopback Count */
	kstat_named_t gotlbc;	/* Good Tx Octets Loopback Count */

} igbvf_stat_t;

/*
 * Function prototypes in igbvf_buf.c
 */
int igbvf_alloc_dma(igbvf_t *);
void igbvf_free_dma(igbvf_t *);
void igbvf_free_dma_buffer(dma_buffer_t *);
int igbvf_alloc_tx_ring_mem(igbvf_tx_ring_t *);
void igbvf_free_tx_ring_mem(igbvf_tx_ring_t *);
int igbvf_alloc_rx_ring_data(igbvf_rx_ring_t *);
void igbvf_free_rx_ring_data(igbvf_rx_data_t *);
void igbvf_free_pending_rx_data(igbvf_t *);

/*
 * Function prototypes in igbvf_debug.c
 */
#ifdef IGBVF_DEBUG
void igbvf_dump_pci(void *);
#endif

/*
 * Function prototypes in igbvf_main.c
 */
int igbvf_start(igbvf_t *);
void igbvf_stop(igbvf_t *);
int igbvf_setup_link(igbvf_t *, boolean_t);
int igbvf_unicst_add(igbvf_t *, const uint8_t *);
int igbvf_unicst_remove(igbvf_t *, const uint8_t *);
int igbvf_multicst_add(igbvf_t *, const uint8_t *);
int igbvf_multicst_remove(igbvf_t *, const uint8_t *);
int igbvf_setup_multicst(igbvf_t *);
void igbvf_enable_watchdog_timer(igbvf_t *);
void igbvf_disable_watchdog_timer(igbvf_t *);
int igbvf_atomic_reserve(uint32_t *, uint32_t);
int igbvf_check_acc_handle(ddi_acc_handle_t);
int igbvf_check_dma_handle(ddi_dma_handle_t);
void igbvf_fm_ereport(igbvf_t *, char *);
void igbvf_set_fma_flags(int);
int igbvf_set_mcast_promisc(igbvf_t *, boolean_t);

/*
 * Function prototypes in igbvf_vf.c
 */
void e1000_init_function_pointers_vf(struct e1000_hw *);

/*
 * Function prototypes in igbvf_gld.c
 */
int igbvf_m_start(void *);
void igbvf_m_stop(void *);
int igbvf_m_promisc(void *, boolean_t);
int igbvf_m_multicst(void *, boolean_t, const uint8_t *);
int igbvf_m_unicst(void *, const uint8_t *);
int igbvf_m_stat(void *, uint_t, uint64_t *);
void igbvf_m_resources(void *);
void igbvf_m_ioctl(void *, queue_t *, mblk_t *);
boolean_t igbvf_m_getcapab(void *, mac_capab_t, void *);
int igbvf_rx_ring_intr_enable(mac_ring_driver_t);
int igbvf_rx_ring_intr_disable(mac_ring_driver_t);
int igbvf_m_setprop(void *, const char *, mac_prop_id_t, uint_t, const void *);
int igbvf_m_getprop(void *, const char *, mac_prop_id_t, uint_t, void *);
void igbvf_m_propinfo(void *, const char *, mac_prop_id_t,
    mac_prop_info_handle_t);

/*
 * Function prototypes in igbvf_rx.c
 */
mblk_t *igbvf_rx(igbvf_rx_ring_t *, int, int);
mblk_t *igbvf_rx_ring_poll(void *, int, int);
void igbvf_rx_recycle(caddr_t arg);

/*
 * Function prototypes in igbvf_tx.c
 */
void igbvf_free_tcb(tx_control_block_t *);
void igbvf_put_free_list(igbvf_tx_ring_t *, link_list_t *);
uint32_t igbvf_tx_recycle_legacy(igbvf_tx_ring_t *);
uint32_t igbvf_tx_recycle_head_wb(igbvf_tx_ring_t *);
mblk_t *igbvf_tx_ring_send(void *, mblk_t *);

/*
 * Function prototypes in igbvf_log.c
 */
void igbvf_notice(void *, const char *, ...);
void igbvf_log(void *, const char *, ...);
void igbvf_error(void *, const char *, ...);

/*
 * Function prototypes in igbvf_stat.c
 */
int igbvf_init_stats(igbvf_t *);
int igbvf_update_stats(kstat_t *, int);
void igbvf_update_stat_regs(igbvf_t *);
void igbvf_get_vf_stat(struct e1000_hw *, uint32_t, uint64_t *);
int igbvf_rx_ring_stat(mac_ring_driver_t, uint_t, uint64_t *);
int igbvf_tx_ring_stat(mac_ring_driver_t, uint_t, uint64_t *);

#ifdef __cplusplus
}
#endif

#endif /* _IGBVF_SW_H */
