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

#ifndef	_IXGBE_SW_H
#define	_IXGBE_SW_H

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
#include <sys/bitmap.h>
#include <sys/ddifm.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/disp.h>
#include <sys/file.h>
#include <sys/fm/io/ddi.h>
#include <sys/pci_param.h>
#include <sys/iov_param.h>
#include "ixgbe_api.h"
#include "ixgbe_mbx.h"
#include "ixgbe_dcb.h"
#include "ixgbe_dcb_82599.h"

#define	MODULE_NAME			"ixgbe"	/* module name */

#define	IXGBE_FAILURE			DDI_FAILURE

#define	IXGBE_UNKNOWN			0x00
#define	IXGBE_INITIALIZED		0x01
#define	IXGBE_STARTED			0x02
#define	IXGBE_SUSPENDED			0x04
#define	IXGBE_STALL			0x08
#define	IXGBE_OVERTEMP			0x20
#define	IXGBE_INTR_ADJUST		0x40
#define	IXGBE_ERROR			0x80

#define	MAX_NUM_UNICAST_ADDRESSES 	0x80
#define	MAX_NUM_MULTICAST_ADDRESSES 	0x1000
#define	MAX_NUM_MULTICAST_ADDRESSES_VF 	0x1E
#define	IXGBE_INTR_NONE			0
#define	IXGBE_INTR_MSIX			1
#define	IXGBE_INTR_MSI			2
#define	IXGBE_INTR_LEGACY		3

#define	IXGBE_POLL_NULL			INT_MAX

#define	MAX_COOKIE			18
#define	MIN_NUM_TX_DESC			2

#define	IXGBE_TX_DESC_LIMIT		32	/* tx desc limitation	*/

#define	IXGBE_ADAPTER_REGSET		1	/* map adapter registers */

#define	IXGBE_RX_STOPPED		0x1

#define	IXGBE_PKG_BUF_16k		16384

/*
 * MAX_xx_QUEUE_NUM and MAX_INTR_VECTOR values need to be the maximum of all
 * supported silicon types.
 */
#define	MAX_TX_QUEUE_NUM		128
#define	MAX_RX_QUEUE_NUM		128
#define	MAX_INTR_VECTOR			64

/*
 * Maximum values for user configurable parameters
 */
#define	MAX_TX_RING_SIZE		4096
#define	MAX_RX_RING_SIZE		4096

#define	MAX_RX_LIMIT_PER_INTR		4096

#define	MAX_RX_COPY_THRESHOLD		9216
#define	MAX_TX_COPY_THRESHOLD		9216
#define	MAX_TX_RECYCLE_THRESHOLD	DEFAULT_TX_RING_SIZE
#define	MAX_TX_OVERLOAD_THRESHOLD	DEFAULT_TX_RING_SIZE
#define	MAX_TX_RESCHED_THRESHOLD	DEFAULT_TX_RING_SIZE

/*
 * Minimum values for user configurable parameters
 */
#define	MIN_TX_RING_SIZE		64
#define	MIN_RX_RING_SIZE		64

#define	MIN_MTU				ETHERMIN
#define	MIN_RX_LIMIT_PER_INTR		16
#define	MIN_TX_COPY_THRESHOLD		0
#define	MIN_RX_COPY_THRESHOLD		0
#define	MIN_TX_RECYCLE_THRESHOLD	MIN_NUM_TX_DESC
#define	MIN_TX_OVERLOAD_THRESHOLD	MIN_NUM_TX_DESC
#define	MIN_TX_RESCHED_THRESHOLD	MIN_NUM_TX_DESC

/*
 * Default values for user configurable parameters
 */
#define	DEFAULT_TX_RING_SIZE		1024
#define	DEFAULT_RX_RING_SIZE		1024

#define	DEFAULT_MTU			ETHERMTU
#define	DEFAULT_RX_LIMIT_PER_INTR	1024
#define	DEFAULT_RX_COPY_THRESHOLD	128
#define	DEFAULT_TX_COPY_THRESHOLD	512
#define	DEFAULT_TX_RECYCLE_THRESHOLD	(MAX_COOKIE + 1)
#define	DEFAULT_TX_OVERLOAD_THRESHOLD	MIN_NUM_TX_DESC
#define	DEFAULT_TX_RESCHED_THRESHOLD	128
#define	DEFAULT_FCRTH			0x20000
#define	DEFAULT_FCRTL			0x10000
#define	DEFAULT_FCPAUSE			0xFFFF

#define	DEFAULT_TX_HCKSUM_ENABLE	B_TRUE
#define	DEFAULT_RX_HCKSUM_ENABLE	B_TRUE
#define	DEFAULT_LSO_ENABLE		B_TRUE
#define	DEFAULT_LRO_ENABLE		B_FALSE
#define	DEFAULT_MR_ENABLE		B_TRUE
#define	DEFAULT_RSS_UDP_ENABLE		B_FALSE
#define	DEFAULT_TX_HEAD_WB_ENABLE	B_TRUE
#define	DEFAULT_RELAX_ORDER_ENABLE	B_TRUE
#define	DEFAULT_RSS_FIXED_SEED_ENABLE	B_FALSE
#define	DEFAULT_MAX_RINGS_ENABLE	B_FALSE
#define	DEFAULT_FCOE_TXCRC_ENABLE	B_TRUE
#define	DEFAULT_FCOE_RXCRC_ENABLE	B_TRUE
#define	DEFAULT_FCOE_LSO_ENABLE		B_TRUE
#define	DEFAULT_FCOE_LRO_ENABLE		B_FALSE
#define	DEFAULT_FCOE_RXMR_ENABLE	B_FALSE

#define	IXGBE_FCOE_VER			0
#define	IXGBE_FCOE_HDR_LEN		14

#define	IXGBE_FC_OXID_OFFSET		16
#define	IXGBE_FC_RXID_OFFSET		18
#define	IXGBE_FC_TRAILER_EOF_OFFSET	4
#define	IXGBE_FC_TRAILER_LEN		8

#define	IXGBE_FC_SOF_I2			0x2d
#define	IXGBE_FC_SOF_I3			0x2e
#define	IXGBE_FC_SOF_N2			0x35
#define	IXGBE_FC_SOF_N3			0x36

#define	IXGBE_FC_EOF_N			0x41
#define	IXGBE_FC_EOF_T			0x42
#define	IXGBE_FC_EOF_NI			0x49
#define	IXGBE_FC_EOF_A			0x50

#define	IXGBE_LSO_MAXLEN		65535
#define	IXGBE_FCOE_LSO_MAXLEN		(65536 - 96)

/*
 * Here 4K buffer will be used, 4K * 32 = 128K LRO/DDP
 * One extra buffer is used to workaround hardware limitation.
 */
#define	IXGBE_DDP_UBD_COUNT		(32 + 1)
#define	IXGBE_DDP_BUF_SHIFT		12
#define	IXGBE_DDP_BUF_SIZE		(1 << IXGBE_DDP_BUF_SHIFT)
#define	IXGBE_DDP_BUF_MASK		(IXGBE_DDP_BUF_SIZE - 1)
#define	IXGBE_FCOE_LRO_MAXLEN	(IXGBE_DDP_BUF_SIZE * (IXGBE_DDP_UBD_COUNT - 1))

#define	IXGBE_FCOE_BUF_4KB		0x0
#define	IXGBE_FCOE_BUF_8KB		0x1
#define	IXGBE_FCOE_BUF_16KB		0x2
#define	IXGBE_FCOE_BUF_64KB		0x3

#define	IXGBE_DDP_RING_SIZE		512
#define	IXGBE_DDP_MIN_XCHGID		0
#define	IXGBE_DDP_MAX_XCHGID		511

#define	IXGBE_FCERR_BADCRC		0x00100000

#define	TX_DRAIN_TIME			200
#define	RX_DRAIN_TIME			200

#define	STALL_WATCHDOG_TIMEOUT		8	/* 8 seconds */
#define	MAX_LINK_DOWN_TIMEOUT		8	/* 8 seconds */

#define	IXGBE_CYCLIC_PERIOD		(1000000000)	/* 1s */

#define	IXGBE_DEFAULT_BWG		0

/*
 * Extra register bit masks for 82598
 */
#define	IXGBE_PCS1GANA_FDC	0x20
#define	IXGBE_PCS1GANLP_LPFD	0x20
#define	IXGBE_PCS1GANLP_LPHD	0x40

/*
 * External PHY ID on NEM
 */
#define	BCM8706_PHYID	0x206030

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
#define	ATTACH_PROGRESS_INIT		0x0080	/* Device initialized */
#define	ATTACH_PROGRESS_STATS		0x0200	/* Kstats created */
#define	ATTACH_PROGRESS_MAC		0x0800	/* MAC registered */
#define	ATTACH_PROGRESS_ENABLE_INTR	0x1000	/* DDI interrupts enabled */
#define	ATTACH_PROGRESS_FM_INIT		0x2000	/* FMA initialized */
#define	ATTACH_PROGRESS_SFP_TASKQ	0x4000	/* SFP taskq created */
#define	ATTACH_PROGRESS_LINK_TIMER	0x8000	/* link check timer */
#define	ATTACH_PROGRESS_OVERTEMP_TASKQ	0x10000 /* Over-temp taskq created */
#define	ATTACH_PROGRESS_CB		0x20000	/* Callback registered */

#define	PROP_DCB_MODE			"dcb_mode"
#define	PROP_DEFAULT_MTU		"default_mtu"
#define	PROP_FLOW_CONTROL		"flow_control"
#define	PROP_IOV_MODE			"iov_mode"
#define	PROP_TX_QUEUE_NUM		"tx_queue_number"
#define	PROP_TX_RING_SIZE		"tx_ring_size"
#define	PROP_RX_QUEUE_NUM		"rx_queue_number"
#define	PROP_RX_RING_SIZE		"rx_ring_size"
#define	PROP_RX_GROUP_NUM		"rx_group_number"
#define	PROP_MAX_RINGS_ENABLE		"max_rings_enable"

#define	PROP_INTR_FORCE			"intr_force"
#define	PROP_TX_HCKSUM_ENABLE		"tx_hcksum_enable"
#define	PROP_RX_HCKSUM_ENABLE		"rx_hcksum_enable"
#define	PROP_LSO_ENABLE			"lso_enable"
#define	PROP_LRO_ENABLE			"lro_enable"
#define	PROP_MR_ENABLE			"mr_enable"
#define	PROP_RSS_UDP_ENABLE		"rss_udp_enable"
#define	PROP_FCOE_TXCRC_ENABLE		"fcoe_txcrc_enable"
#define	PROP_FCOE_RXCRC_ENABLE		"fcoe_rxcrc_enable"
#define	PROP_FCOE_LSO_ENABLE		"fcoe_lso_enable"
#define	PROP_FCOE_LRO_ENABLE		"fcoe_lro_enable"
#define	PROP_FCOE_RXMR_ENABLE		"fcoe_rxmr_enable"
#define	PROP_RELAX_ORDER_ENABLE		"relax_order_enable"
#define	PROP_RSS_FIXED_SEED_ENABLE	"rss_fixed_seed_enable"
#define	PROP_TX_HEAD_WB_ENABLE		"tx_head_wb_enable"
#define	PROP_TX_COPY_THRESHOLD		"tx_copy_threshold"
#define	PROP_TX_RECYCLE_THRESHOLD	"tx_recycle_threshold"
#define	PROP_TX_OVERLOAD_THRESHOLD	"tx_overload_threshold"
#define	PROP_TX_RESCHED_THRESHOLD	"tx_resched_threshold"
#define	PROP_RX_COPY_THRESHOLD		"rx_copy_threshold"
#define	PROP_RX_LIMIT_PER_INTR		"rx_limit_per_intr"
#define	PROP_INTR_THROTTLING		"intr_throttling"
#define	PROP_FM_CAPABLE			"fm_capable"

#define	IXGBE_NUM_IOV_PARAMS		5

#define	ixgbe_iov_param_unicast_slots	\
	    ixgbe_iov_param_list[3]

#define	IXGBE_LB_NONE			0
#define	IXGBE_LB_EXTERNAL		1
#define	IXGBE_LB_INTERNAL_MAC		2
#define	IXGBE_LB_INTERNAL_PHY		3
#define	IXGBE_LB_INTERNAL_SERDES	4

/*
 * capability/feature flags
 * Flags named _CAPABLE are set when the NIC hardware is capable of the feature.
 * Separately, the flag named _ENABLED is set when the feature is enabled.
 */
#define	IXGBE_FLAG_DCA_ENABLED		(u32)(1)
#define	IXGBE_FLAG_DCA_CAPABLE		(u32)(1 << 1)
#define	IXGBE_FLAG_DCB_ENABLED		(u32)(1 << 2)
#define	IXGBE_FLAG_DCB_CAPABLE		(u32)(1 << 4)
#define	IXGBE_FLAG_RSS_ENABLED		(u32)(1 << 4)
#define	IXGBE_FLAG_RSS_CAPABLE		(u32)(1 << 5)
#define	IXGBE_FLAG_VMDQ_CAPABLE		(u32)(1 << 6)
#define	IXGBE_FLAG_VMDQ_ENABLED		(u32)(1 << 7)
#define	IXGBE_FLAG_FAN_FAIL_CAPABLE	(u32)(1 << 8)
#define	IXGBE_FLAG_RSC_CAPABLE		(u32)(1 << 9)
#define	IXGBE_FLAG_SFP_PLUG_CAPABLE	(u32)(1 << 10)
#define	IXGBE_FLAG_TEMP_SENSOR_CAPABLE	(u32)(1 << 11)

/*
 * Classification mode
 */
#define	IXGBE_CLASSIFY_NONE		0
#define	IXGBE_CLASSIFY_RSS		1
#define	IXGBE_CLASSIFY_VT		2
#define	IXGBE_CLASSIFY_VT_RSS		3
#define	IXGBE_CLASSIFY_DCB		4
#define	IXGBE_CLASSIFY_RSS_DCB		5

typedef void (*ixgbe_nic_func_t)(struct ixgbe *);

/* adapter-specific info for each supported device type */
typedef struct adapter_info {
	uint32_t	max_rx_que_num; /* maximum number of rx queues */
	uint32_t	min_rx_que_num; /* minimum number of rx queues */
	uint32_t	def_rx_que_num; /* default number of rx queues */

	uint32_t	max_tx_que_num;	/* maximum number of tx queues */
	uint32_t	min_tx_que_num;	/* minimum number of tx queues */
	uint32_t	def_tx_que_num;	/* default number of tx queues */

	uint32_t	max_grp_num; /* maximum number of groups */
	uint32_t	min_grp_num; /* minimum number of groups */
	uint32_t	def_grp_num; /* default number of groups */

	uint32_t	max_mtu;	/* maximum MTU size */
	/*
	 * Interrupt throttling is in unit of 256 nsec
	 */
	uint32_t	max_intr_throttle; /* maximum interrupt throttle */
	uint32_t	min_intr_throttle; /* minimum interrupt throttle */
	uint32_t	def_intr_throttle; /* default interrupt throttle */

	uint32_t	max_vf;		/* maximum VFs supported */

	uint32_t	max_msix_vect;	/* maximum total msix vectors */
	uint32_t	max_ring_vect;	/* maximum number of ring vectors */
	uint32_t	max_other_vect;	/* maximum number of other vectors */
	uint32_t	other_intr;	/* "other" interrupt types handled */
	uint32_t	other_gpie;	/* "other" interrupt types enabling */

	/* function pointers */
	/* enable adapter interrupts */
	ixgbe_nic_func_t enable_hw_intr;
	/* set up rx classification features */
	ixgbe_nic_func_t setup_rx_classify;
	/* set up tx classification features */
	ixgbe_nic_func_t setup_tx_classify;

	uint32_t	flags;		/* capability flags */
} adapter_info_t;

/*
 * Enable the driver code of the register dump feature, required by ethregs
 * #define	DEBUG_INTEL	TRUE
 */

#ifdef DEBUG_INTEL
/*
 * Used by ethregs support ioctl.  IXGBE_IOC_DEV_ID must be binary
 * equal to the equivalent definition in the ethregs application.
 */
#define	IXGBE_IOC	((((((('E' << 4) + '1') << 4) \
			    + 'K') << 4) + 'G') << 4)
#define	IXGBE_IOC_DEV_ID 	(IXGBE_IOC | 4)
#endif  /* DEBUG_INTEL */

/* bits representing all interrupt types other than tx & rx */
#define	IXGBE_OTHER_INTR	0x3ff00000
#define	IXGBE_82599_OTHER_INTR	0x86100000

enum ioc_reply {
	IOC_INVAL = -1,	/* bad, NAK with EINVAL */
	IOC_DONE, 	/* OK, reply sent */
	IOC_ACK,	/* OK, just send ACK */
	IOC_REPLY	/* OK, just send reply */
};

#define	DMA_SYNC(area, flag)	((void) ddi_dma_sync((area)->dma_handle, \
				    0, 0, (flag)))

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

/*
 * Property lookups
 */
#define	IXGBE_PROP_EXISTS(d, n)	ddi_prop_exists(DDI_DEV_T_ANY, (d), \
				    DDI_PROP_DONTPASS, (n))
#define	IXGBE_PROP_GET_INT(d, n)	ddi_prop_get_int(DDI_DEV_T_ANY, (d), \
				    DDI_PROP_DONTPASS, (n), -1)

/* flags for ether addresses */
#define	IXGBE_ADDRESS_SET	0x01
#define	IXGBE_ADDRESS_SHARED	0x02
#define	IXGBE_ADDRESS_ENABLED	0x80

typedef struct ixgbe_ether_addr {
	uint8_t		flags;
	uint8_t		group_index;
	uint8_t		addr[ETHERADDRL];
} ixgbe_ether_addr_t;

typedef enum {
	USE_NONE,
	USE_COPY,
	USE_DMA
} tx_type_t;

/*
 * Each of the locks contained in structures below are aligned to be in
 * different cache line and to keep the size of the structure a multiple of
 * 64 bytes. This will reduce contention for the same piece of memory.
 */
typedef struct ixgbe_tx_context {
	uint32_t		hcksum_flags;
	uint32_t		ip_hdr_len;

	uint32_t		mac_hdr_len;
	uint32_t		l4_proto;

	uint32_t		mss;
	uint32_t		l4_hdr_len;

	boolean_t		lso_flag;
	uint32_t		pad1;

	uint32_t		fcoe_flags;
	boolean_t		fcoe_lso_flag;

	boolean_t		fcoe_fc_f_ctl;
	uint32_t		fcoe_mss;

	uint32_t		fcoe_hdr_len;
	uint32_t		fcoe_mac_len;

	uint8_t			fcoe_sof;
	uint8_t			fcoe_eof;
	uint16_t		fcoe_fc_hdr_len;
	uint32_t		pad2;
} ixgbe_tx_context_t;

/*
 * Hold address/length of each DMA segment
 */
typedef struct sw_desc {
	uint64_t		address;
	size_t			length;
} sw_desc_t;

/*
 * Handles and addresses of DMA buffer
 */
typedef struct dma_buffer {
	caddr_t			address;	/* Virtual address */
	uint64_t		dma_address;	/* DMA (Hardware) address */
	ddi_acc_handle_t	acc_handle;	/* Data access handle */
	ddi_dma_handle_t	dma_handle;	/* DMA handle */
	size_t			size;		/* Buffer size */
	size_t			len;		/* Data length in the buffer */
	uint8_t			pad[16];
} dma_buffer_t;

/*
 * Tx Control Block
 */
typedef struct tx_control_block {
	single_link_t		link;
	uint32_t		last_index; /* last descriptor of the pkt */
	uint32_t		frag_num;
	uint32_t		desc_num;
	mblk_t			*mp;
	tx_type_t		tx_type;
	ddi_dma_handle_t	tx_dma_handle;
	mblk_t			*tx_mp;
	dma_buffer_t		tx_buf;
	sw_desc_t		desc[MAX_COOKIE];
	uint8_t			pad[40];
} tx_control_block_t;

/*
 * RX Control Block
 */
typedef struct rx_control_block {
	mblk_t			*mp;
	dma_buffer_t		rx_buf;
	frtn_t			free_rtn;
	struct ixgbe_rx_data	*rx_data;
	int			lro_next;	/* Index of next rcb */
	int			lro_prev;	/* Index of previous rcb */
	boolean_t		lro_pkt;	/* Flag for LRO rcb */
	uint8_t			pad[16];
} rx_control_block_t;

/*
 * Software Data Structure for Tx Ring.
 *
 * We have padding fields to keep some of the lock fields
 * on different cache lines and to keep the size of the structure
 * a multiple of 64 bytes. The former eliminates any false sharing with
 * locks in this structure while the latter eliminates any false sharing in the
 * array of Tx rings in ixgbe structure.
 *
 * Note: Keep in mind when adding a new field to this structure
 * that the size of the structure needs to be kept a multiple of 64 bytes.
 */
#define	ITR_PADL1_BYTES		4
#define	ITR_PADL5_BYTES		12
#define	ITR_PADL6_BYTES		24

typedef struct ixgbe_tx_ring {
	/* cache line 1 */
	mac_ring_handle_t	ring_handle;
	struct ixgbe		*ixgbe;	/* Pointer to the ixgbe struct */
	uint32_t		(*tx_recycle)(struct ixgbe_tx_ring *);
	uint32_t		index;		/* Ring index */
	uint32_t		group_index;	/* TX Group Index */
	uint32_t		hw_index;
	uint32_t		tc_index;
	uint32_t		intr_vector;	/* Interrupt vector index */
	uint32_t		vect_bit;	/* vector's bit in register */
	uint32_t		free_list_size;	/* Tx free list size */
	uint32_t		ring_size;	/* Tx descriptor ring size */
	boolean_t		started;
	boolean_t		wthresh_nonzero;

	/* cache line 2 */
	kmutex_t		tx_lock;
	uint64_t		stat_obytes;
	uint64_t		stat_opackets;
	uint64_t		stat_opackets_copy;
	uint64_t		stat_opackets_bind;
	uint64_t		stat_opackets_premap;
	uint32_t		tbd_tail; /* Index of next tbd to transmit */
	uint32_t		tbd_free; /* Number of free tbd */
	union ixgbe_adv_tx_desc	*tbd_ring;

	/* cache line 3 */
	/*
	 * s/w context structure for TCP/UDP checksum offload
	 * and LSO.
	 */
	ixgbe_tx_context_t	tx_context;

	/* cache line 4 */
	/*
	 * Tx descriptor ring definitions
	 */
	dma_buffer_t		tbd_area;

	/* cache line 5 */
	kmutex_t		tcb_head_lock;
	kmutex_t		tcb_tail_lock;
	/*
	 * Tx control block list definitions
	 */
	tx_control_block_t	*tcb_area;
	tx_control_block_t	**work_list;
	tx_control_block_t	**free_list;
	uint32_t		tcb_head; /* Head index of free list */
	uint32_t		tcb_tail; /* Tail index of free list */
	uint32_t		tcb_free; /* Number of free tcb in free list */
	uint8_t			padl5[ITR_PADL5_BYTES];

	/* cache line 6 */
	/*
	 * Tx ring settings and status
	 */
	kmutex_t		recycle_lock;
	uint32_t		*tbd_head_wb; /* Head write-back */
	uint32_t		tbd_head; /* Index of next tbd to recycle */
	boolean_t		reschedule;
	uint32_t		recycle_fail;
	uint32_t		stall_watchdog;
	mac_descriptor_handle_t	tbd_mdh;
	uint8_t			padl6[ITR_PADL6_BYTES];

#if defined(IXGBE_DEBUG) || defined(lint)
	/*
	 * Debug statistics
	 */
	uint32_t		stat_overload;
	uint32_t		stat_fail_no_tbd;
	uint32_t		stat_fail_no_tcb;
	uint32_t		stat_fail_dma_bind;
	uint32_t		stat_reschedule;
	uint32_t		stat_break_tbd_limit;
	uint32_t		stat_lso_header_fail;
#endif
} ixgbe_tx_ring_t;

/*
 * Software Receive Ring
 */
typedef struct ixgbe_rx_data {
	/*
	 * Rx descriptor ring definitions
	 */
	uint32_t		rbd_next;	/* Index of next rx desc */
	uint32_t		rx_data_pad1;
	mac_descriptor_handle_t	rbd_mdh;
	dma_buffer_t		rbd_area;	/* DMA buffer of rx desc ring */
	union ixgbe_adv_rx_desc	*rbd_ring;	/* Rx desc ring */

	/*
	 * Rx control block list definitions
	 */
	rx_control_block_t	*rcb_area;
	rx_control_block_t	**work_list;	/* Work list of rcbs */
	mblk_t			*mblk_head;
	mblk_t			*mblk_tail;
	uint_t			mblk_cnt;

	/*
	 * Rx sw ring settings and status
	 */
	uint32_t		ring_size;	/* Rx descriptor ring size */
	uint32_t		flag;

	uint32_t		lro_num;	/* Number of rcbs of one LRO */
	uint32_t		lro_first;	/* Index of first LRO rcb */

	struct ixgbe_rx_ring	*rx_ring;	/* Pointer to rx ring */
	uint8_t			pad[40];
} ixgbe_rx_data_t;

/*
 * Software Data Structure for Rx Ring.
 *
 * We have the padding field to keep the size of the structure
 * a multiple of 64 bytes. This eliminates any false sharing in the
 * array of Rx rings in ixgbe structure.
 *
 * Note: Keep in mind when adding a new field to this structure
 * that the size of the structure needs to be kept a multiple of 64 bytes.
 */

#define	IRR_PADL2_BYTES		44

typedef struct ixgbe_rx_ring {
	/* cache line 1 */
	kmutex_t		rx_lock;	/* Rx access lock */
	struct ixgbe		*ixgbe;		/* Pointer to ixgbe struct */
	ixgbe_rx_data_t		*rx_data;	/* Rx software ring */
	uint64_t		stat_rbytes;
	uint64_t		stat_ipackets;
	uint32_t		index;		/* Ring index */
	uint32_t		hw_index;	/* h/w ring index */
	mac_ring_handle_t	ring_handle;
	uint64_t		ring_gen_num;

	/* cache line 2 */
	uint32_t		group_index;	/* Group index */
	uint32_t		tc_index;	/* TC index */
	uint32_t		intr_vector;	/* Interrupt vector index */
	uint32_t		vect_bit;	/* vector's bit in register */
	boolean_t		started;	/* Rx ring started ? */
	uint8_t			padl2[IRR_PADL2_BYTES];

#ifdef IXGBE_DEBUG
	/*
	 * Debug statistics
	 */
	uint32_t		stat_frame_error;
	uint32_t		stat_cksum_error;
	uint32_t		stat_exceed_pkt;
#endif
} ixgbe_rx_ring_t;

/*
 * Software Ring Group
 */
typedef struct ixgbe_group {
	uint32_t		index;		/* Group index */
	mac_group_handle_t	group_handle;   /* call back group handle */
	struct ixgbe		*ixgbe;		/* Pointer to ixgbe struct */
} ixgbe_group_t;

/*
 * Software Data Structure for FCoE ddp.
 */
typedef struct ixgbe_fcoe_ddp {
	uint16_t xchg_id;
	struct ixgbe_fcoe_ddp_buf *ddp_buf;
} ixgbe_fcoe_ddp_t;

typedef struct ixgbe_ddp_ubd {
	uint64_t		dma_address;	/* DMA (Hardware) address */
} ixgbe_ddp_ubd_t;

typedef struct ixgbe_fcoe_ddp_buf {
	/* cache line 1 */
	dma_buffer_t		ubd_area;

	/* cache line 2 */
	struct ixgbe_rx_fcoe	*rx_fcoe;
	ixgbe_ddp_ubd_t		*ubd_list;	/* ddp buffer desc list */
	uint32_t		used_ubd_count; /* used ubd during ddp */
	uint32_t		recycled_ubd_count; /* recycled ubd */
	uint32_t		last_ub_len;	/* used last ddp buffer len */
	uint32_t		used_buf_size;	/* used total ddp buffer size */
	uint32_t		ref_cnt;
	frtn_t			free_rtn;
	mblk_t			*mp[IXGBE_DDP_UBD_COUNT];
	uint8_t			pad[60];

	/* cache line 3 */
	dma_buffer_t		rx_buf[IXGBE_DDP_UBD_COUNT];
} ixgbe_fcoe_ddp_buf_t;

typedef	struct ixgbe_rx_fcoe {
	/* cache line 1 */
	kmutex_t		recycle_lock;	/* Recycle lock */
	ixgbe_fcoe_ddp_buf_t	*ddp_buf_area;
	ixgbe_fcoe_ddp_buf_t	**work_list;	/* Work list of ddp buf */
	ixgbe_fcoe_ddp_buf_t	**free_list;	/* Free list of ddp buf */
	uint32_t		ddp_buf_head;	/* Index of next free buf */
	uint32_t		ddp_buf_tail;	/* Index to put recycled buf */
	uint32_t		ddp_buf_free;	/* Number of free ddp bufs */
	uint32_t		ddp_ring_size;	/* FCoE ddp ring size */
	uint32_t		free_list_size;	/* FCoE ddp free list size */
	uint32_t		ddp_buf_pending;
	uint32_t		flag;
	boolean_t		on_target;

	/* cache line 2 */
	ixgbe_fcoe_ddp_t	ddp_ring[IXGBE_DDP_RING_SIZE];
						/* FCoE ddp ring */

	/* cache line 3 */
	struct ixgbe		*ixgbe;	/* Pointer to ixgbe struct */
	uint8_t			pad[56];
} ixgbe_rx_fcoe_t;

/*
 * structure to map interrupt cleanup to msi-x vector.
 *
 * Note: The size of the structure is currently 64-bytes and
 * fits a single cache line. Padding is needed to make the size
 * a multiple of 64 bytes, if the structure is changed.
 * This eliminates any false sharing in the vect_map field
 * (array of interrupt vectors) in ixgbe structure.
 */
typedef struct ixgbe_intr_vector {
	struct ixgbe *ixgbe;	/* point to my adapter */
	int	rxr_cnt;	/* count rx rings */
	int	txr_cnt;	/* count tx rings */
	ulong_t rx_map[BT_BITOUL(MAX_RX_QUEUE_NUM)];	/* bitmap of rx rings */
	ulong_t tx_map[BT_BITOUL(MAX_TX_QUEUE_NUM)];	/* bitmap of tx rings */
	ulong_t other_map[BT_BITOUL(2)];		/* bitmap of other */
	int	other_cnt;	/* count other interrupt */
} ixgbe_intr_vector_t;

enum ixgbe_dcb_mode {
	ixgbe_dcb_disabled,
	ixgbe_dcb_4tc,
	ixgbe_dcb_8tc,
	ixgbe_dcb_4tc_rss,
	ixgbe_dcb_8tc_rss,
};

typedef struct ixgbe_dcb_info_s {
	int			num_tc;	/* Number of TC */
	uint_t			up2tc_rx[MAC_MAX_TRAFFIC_CLASS];
	uint_t			up2tc_tx[MAC_MAX_TRAFFIC_CLASS];
	boolean_t		vmdq_enabled;	/* VMDQ enabled */
} ixgbe_dcb_info_t;

/*
 * Constants neeed for PF/VF
 */
#define	IXGBE_MAX_VF_MC_ENTRIES		30
#define	IXGBE_MAX_VF_UNICAST		2
#define	IXGBE_MAX_VF_MTU_82599		ETHERMTU

#define	IXGBE_VF_FLAG_CTS		(1 << 0) /* VF is clear to send data */
#define	IXGBE_VF_FLAG_UNI_PROMISC_OK	(1 << 1) /* VF allow unicast promisc */
#define	IXGBE_VF_FLAG_UNI_PROMISC	(1 << 2) /* VF unicast promisc set */
#define	IXGBE_VF_FLAG_MULTI_PROMISC	(1 << 3) /* VF multicast promisc set */

/*
 * This must be the maximum of all supported types
 */
#define	IXGBE_MAX_VF		64
/* maximum configurable VFs */
#define	IXGBE_MAX_CONFIG_VF	(IXGBE_MAX_VF - 1)

typedef struct iov_params {
	uint32_t		max_num_mac;
	uint32_t		rx_que_num;
	uint32_t		tx_que_num;
} iov_params_t;

typedef enum {
	IOV_NONE = 0,
	IOV_16VF = 16,
	IOV_32VF = 32,
	IOV_64VF = 64,
} iov_mode_t;

extern iov_param_desc_t ixgbe_iov_param_list[IXGBE_NUM_IOV_PARAMS];

/*
 * Function type which implements message dispatch for an API version
 */
typedef void (*msg_api_implement_t)(struct ixgbe *, int);

/*
 * Store elements that PF shares with VF
 */
typedef struct vf_data {
	msg_api_implement_t	vf_api;
	uint32_t		num_mac_addrs;
	uint32_t		mac_addr_chg;
	uint32_t		unicast_slots;
	uint32_t		vf_mc_hashes[IXGBE_MAX_VF_MC_ENTRIES];
	uint32_t		num_vf_mc_hashes;
	uint32_t		default_vlan_id;
	uint16_t		vlan_ids[IXGBE_VLVF_ENTRIES];
	uint32_t		num_vlans;
	uint32_t		port_vlan_id;
	boolean_t		vlan_stripping;
	iov_params_t		iov_params;
	uint32_t		max_mtu;
	uint32_t		flags;
	time_t			last_nack;
} vf_data_t;

/*
 * Software adapter state
 */
typedef struct ixgbe {
	int 			instance;
	mac_handle_t		mac_hdl;
	dev_info_t		*dip;
	struct ixgbe_hw		hw;
	struct ixgbe_osdep	osdep;

	adapter_info_t		*capab;	/* adapter hardware capabilities */
	ddi_taskq_t		*sfp_taskq;	/* sfp-change taskq */
	ddi_taskq_t		*overtemp_taskq; /* overtemp taskq */
	uint32_t		eims;		/* interrupt mask setting */
	uint32_t		eimc;		/* interrupt mask clear */
	uint32_t		eicr;		/* interrupt cause reg */

	uint32_t		ixgbe_state;
	link_state_t		link_state;
	uint32_t		link_speed;
	uint32_t		link_duplex;

	uint32_t		reset_count;
	uint32_t		attach_progress;
	uint32_t		loopback_mode;
	uint32_t		default_mtu;
	uint32_t		max_frame_size;

	uint32_t		ddp_buf_pending;

	/*
	 * Each msi-x vector: map vector to interrupt cleanup
	 */
	ixgbe_intr_vector_t	vect_map[MAX_INTR_VECTOR];

	/*
	 * Receive Rings
	 */
	ixgbe_rx_ring_t		*rx_rings;	/* Array of rx rings */
	uint32_t		num_rx_rings;	/* Number of rx rings in use */
	uint32_t		rx_ring_size;	/* Rx descriptor ring size */
	uint32_t		rx_buf_size;	/* Rx buffer size */
	boolean_t		lro_enable;	/* Large Receive Offload */
	uint64_t		lro_pkt_count;	/* LRO packet count */
	uint64_t		alloc_mp_fail;	/* Alloc mp fail count */

	ixgbe_rx_fcoe_t		*rx_fcoe;

	/*
	 * RX Ring Groups
	 */
	ixgbe_group_t		*rx_groups;	/* Array of rx groups */
	uint32_t		num_pf_rx_groups; /* Number of groups in use */
	uint32_t		num_all_rx_groups; /* Number of all groups */

	/*
	 * Transmit Rings
	 */
	ixgbe_tx_ring_t		*tx_rings;	/* Array of tx rings */
	uint32_t		num_tx_rings;	/* Number of tx rings in use */
	uint32_t		tx_ring_size;	/* Tx descriptor ring size */
	uint32_t		tx_buf_size;	/* Tx buffer size */

	/*
	 * TX Ring Groups
	 */
	ixgbe_group_t		*tx_groups;	/* Array of tx groups */
	uint32_t		num_pf_tx_groups; /* Number of groups in use */
	uint32_t		num_all_tx_groups; /* Number of all groups */

	boolean_t		tx_ring_init;
	boolean_t		tx_head_wb_enable; /* Tx head wrtie-back */
	boolean_t		tx_hcksum_enable; /* Tx h/w cksum offload */
	boolean_t 		lso_enable; 	/* Large Segment Offload */
	boolean_t 		mr_enable; 	/* Multiple Tx and Rx Ring */
	boolean_t		rss_udp_enable; /* UDP hash in RSS field */
	boolean_t 		fcoe_txcrc_enable; /* FCoE Tx CRC offload */
	boolean_t 		fcoe_lso_enable; /* FCoE LSO */
	boolean_t 		fcoe_rxcrc_enable; /* FCoE Rx CRC offload */
	boolean_t 		fcoe_lro_enable; /* FCoE LRO */
	boolean_t 		fcoe_rxmr_enable; /* FCoE Multiple Rx Ring */
	boolean_t		relax_order_enable; /* Relax Order */
	boolean_t		rss_fixed_seed_enable; /* use fixed seed */
	boolean_t		max_rings_enable; /* allow up to hardware max */
	uint32_t		classify_mode;	/* Classification mode */
	uint32_t		tx_copy_thresh;	/* Tx copy threshold */
	uint32_t		tx_recycle_thresh; /* Tx recycle threshold */
	uint32_t		tx_overload_thresh; /* Tx overload threshold */
	uint32_t		tx_resched_thresh; /* Tx reschedule threshold */
	boolean_t		rx_hcksum_enable; /* Rx h/w cksum offload */
	uint32_t		rx_copy_thresh; /* Rx copy threshold */
	uint32_t		rx_limit_per_intr; /* Rx pkts per interrupt */
	uint32_t		rx_pfc_low;	/* Rx PFC low threshhold */
	uint32_t		rx_pfc_high;	/* Rx PFC high threshhold */
	uint32_t		intr_throttling[MAX_INTR_VECTOR];
	uint32_t		intr_force;
	int			fm_capabilities; /* FMA capabilities */

	int			intr_type;
	int			intr_cnt;
	uint32_t		intr_cnt_max;
	uint32_t		intr_cnt_min;
	int			intr_cap;
	size_t			intr_size;
	uint_t			intr_pri;
	ddi_intr_handle_t	*htable;
	uint32_t		eims_mask;

	uint32_t		cb_flags;
	ddi_cb_handle_t		cb_hdl;

	kmutex_t		gen_lock; /* General lock for device access */
	kmutex_t		watchdog_lock;
	kmutex_t		rx_pending_lock;
	kmutex_t		rx_fcoe_lock;	/* RX FCoE access lock */

	boolean_t		watchdog_enable;
	boolean_t		watchdog_start;
	timeout_id_t		watchdog_tid;

	boolean_t		unicst_init;
	uint32_t		unicst_avail;
	uint32_t		unicst_total;
	ixgbe_ether_addr_t	unicst_addr[MAX_NUM_UNICAST_ADDRESSES];
	uint32_t		mcast_count;
	struct ether_addr	mcast_table[MAX_NUM_MULTICAST_ADDRESSES];

	ulong_t			sys_page_size;

	boolean_t		link_check_complete;
	hrtime_t		link_check_hrtime;
	ddi_periodic_t		periodic_id; /* for link check timer func */

	uint64_t		start_times;

	/*
	 * DCB
	 */
	struct ixgbe_dcb_config	dcb_config;
	uint8_t			pfc_map;

	/*
	 * Kstat definitions
	 */
	kstat_t			*ixgbe_ks;

	uint32_t		param_en_10000fdx_cap:1,
				param_en_1000fdx_cap:1,
				param_en_100fdx_cap:1,
				param_adv_10000fdx_cap:1,
				param_adv_1000fdx_cap:1,
				param_adv_100fdx_cap:1,
				param_pause_cap:1,
				param_asym_pause_cap:1,
				param_rem_fault:1,
				param_adv_autoneg_cap:1,
				param_adv_pause_cap:1,
				param_adv_asym_pause_cap:1,
				param_adv_rem_fault:1,
				param_lp_10000fdx_cap:1,
				param_lp_1000fdx_cap:1,
				param_lp_100fdx_cap:1,
				param_lp_autoneg_cap:1,
				param_lp_pause_cap:1,
				param_lp_asym_pause_cap:1,
				param_lp_rem_fault:1,
				param_pad_to_32:12;

	/*
	 * Elements for PF/VF
	 */
	boolean_t	sriov_pf;	/* act as sriov PF driver */
	uint32_t	num_vfs;	/* number of VFs in use */
	uint32_t	pf_grp;		/* group number of PF */
	iov_mode_t	iov_mode;	/* IOV mode */
	vf_data_t	vf[IXGBE_MAX_VF]; /* one of vf[] is used for PF */
} ixgbe_t;

typedef struct ixgbe_stat {
	kstat_named_t link_speed;	/* Link Speed */

	kstat_named_t reset_count;	/* Reset Count */

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
	kstat_named_t qprc[16];	/* Queue Packets Received Count */
	kstat_named_t qptc[16];	/* Queue Packets Transmitted Count */
	kstat_named_t qbrc[16];	/* Queue Bytes Received Count */
	kstat_named_t qbtc[16];	/* Queue Bytes Transmitted Count */
	kstat_named_t pxofftxc[8]; /* Link Priority XOFF Transmitted Count */
	kstat_named_t pxoffrxc[8]; /* Link Priority XOFF Received Count */

	kstat_named_t crcerrs;	/* CRC Error Count */
	kstat_named_t illerrc;	/* Illegal Byte Error Count */
	kstat_named_t errbc;	/* Error Byte Count */
	kstat_named_t mspdc;	/* MAC Short Packet Discard Count */
	kstat_named_t mpc;	/* Missed Packets Count */
	kstat_named_t mlfc;	/* MAC Local Fault Count */
	kstat_named_t mrfc;	/* MAC Remote Fault Count */
	kstat_named_t rlec;	/* Receive Length Error Count */
	kstat_named_t lxontxc;	/* Link XON Transmitted Count */
	kstat_named_t lxonrxc;	/* Link XON Received Count */
	kstat_named_t lxofftxc;	/* Link XOFF Transmitted Count */
	kstat_named_t lxoffrxc;	/* Link XOFF Received Count */
	kstat_named_t bprc;	/* Broadcasts Pkts Received Count */
	kstat_named_t mprc;	/* Multicast Pkts Received Count */
	kstat_named_t rnbc;	/* Receive No Buffers Count */
	kstat_named_t ruc;	/* Receive Undersize Count */
	kstat_named_t rfc;	/* Receive Frag Count */
	kstat_named_t roc;	/* Receive Oversize Count */
	kstat_named_t rjc;	/* Receive Jabber Count */
	kstat_named_t tor;	/* Total Octets Recvd Count */
	kstat_named_t tot;	/* Total Octets Xmitted Count */
	kstat_named_t tpr;	/* Total Packets Received */
	kstat_named_t tpt;	/* Total Packets Xmitted */
	kstat_named_t mptc;	/* Multicast Packets Xmited Count */
	kstat_named_t bptc;	/* Broadcast Packets Xmited Count */
	kstat_named_t lroc;	/* LRO Packets Received Count */
	kstat_named_t ampfc;	/* Alloc MP for Rx Copy Fail Count */

	kstat_named_t fccrc;	/* FC CRC Error Count */
	kstat_named_t fcoerpdc;	/* FCoE Rx Packets Drop Count */
	kstat_named_t fclast;	/* FC Last Error Count */
	kstat_named_t fcoeprc;	/* FCoE Packets Recvd Count */
	kstat_named_t fcoedwrc;	/* FCoE Dword Recvd Count */
	kstat_named_t fcoeptc;	/* FCoE Packets Xmitd Count */
	kstat_named_t fcoedwtc;	/* FCoE Dword Xmitd Count */
} ixgbe_stat_t;

/*
 * Function prototypes in ixgbe_buf.c
 */
int ixgbe_alloc_dma(ixgbe_t *);
void ixgbe_free_dma(ixgbe_t *);
void ixgbe_set_fma_flags(int);
void ixgbe_free_dma_buffer(dma_buffer_t *);
int ixgbe_alloc_rx_ring_data(ixgbe_rx_ring_t *rx_ring);
void ixgbe_free_rx_ring_data(ixgbe_rx_data_t *rx_data);
int ixgbe_alloc_rx_fcoe_data(ixgbe_t *ixgbe);
void ixgbe_free_rx_fcoe_data(ixgbe_rx_fcoe_t *rx_fcoe);

/*
 * Function prototypes in ixgbe_main.c
 */
int ixgbe_start(ixgbe_t *, boolean_t);
void ixgbe_stop(ixgbe_t *, boolean_t);
int ixgbe_driver_setup_link(ixgbe_t *, boolean_t);
void ixgbe_driver_setup_up2tc(ixgbe_t *, uint_t, uint_t *);
int ixgbe_multicst_add(ixgbe_t *, const uint8_t *);
int ixgbe_multicst_remove(ixgbe_t *, const uint8_t *);
enum ioc_reply ixgbe_loopback_ioctl(ixgbe_t *, struct iocblk *, mblk_t *);

void ixgbe_enable_watchdog_timer(ixgbe_t *);
void ixgbe_disable_watchdog_timer(ixgbe_t *);
int ixgbe_atomic_reserve(uint32_t *, uint32_t);

int ixgbe_check_acc_handle(ddi_acc_handle_t handle);
int ixgbe_check_dma_handle(ddi_dma_handle_t handle);
void ixgbe_fm_ereport(ixgbe_t *, char *);

void ixgbe_fill_ring(void *, mac_ring_type_t, const int, const int,
    mac_ring_info_t *, mac_ring_handle_t);
void ixgbe_fill_group(void *arg, mac_ring_type_t, const int,
    mac_group_info_t *, mac_group_handle_t);
int ixgbe_get_ring_tc(mac_ring_driver_t, mac_ring_type_t);
int ixgbe_rx_ring_intr_enable(mac_ring_driver_t);
int ixgbe_rx_ring_intr_disable(mac_ring_driver_t);

int ixgbe_fcoe_setup_lro(void *, mac_fcoe_lro_params_t *);
void ixgbe_fcoe_cancel_lro(void *, mac_fcoe_lro_params_t *);

void ixgbe_enable_sriov_interrupt(ixgbe_t *);
int ixgbe_unicst_find(ixgbe_t *, const uint8_t *, uint32_t);
int ixgbe_unicst_add(ixgbe_t *, const uint8_t *, uint32_t);
int ixgbe_unicst_remove(ixgbe_t *, const uint8_t *, uint32_t);
int ixgbe_unicst_replace(ixgbe_t *, const uint8_t *, uint32_t);
boolean_t ixgbe_is_valid_mac_addr(uint8_t *);
int ixgbe_get_param_unicast_slots(pci_plist_t *, uint32_t *, char *);

void ixgbe_setup_multicst(ixgbe_t *);

/*
 * Function prototypes in ixgbe_pf.c
 */
void ixgbe_init_function_pointer_pf(ixgbe_t *);
void ixgbe_enable_vf(ixgbe_t *);
void ixgbe_msg_task(ixgbe_t *);
void ixgbe_hold_vfs(ixgbe_t *);
void ixgbe_set_vmolr(ixgbe_t *, int);
int ixgbe_vlvf_set(ixgbe_t *, uint16_t, boolean_t, uint16_t);
int ixgbe_vlan_stripping(ixgbe_t *, uint16_t, boolean_t);

/*
 * Function prototypes in ixgbe_gld.c
 */
int ixgbe_m_start(void *);
void ixgbe_m_stop(void *);
int ixgbe_m_promisc(void *, boolean_t);
int ixgbe_m_multicst(void *, boolean_t, const uint8_t *);
void ixgbe_m_resources(void *);
void ixgbe_m_ioctl(void *, queue_t *, mblk_t *);
boolean_t ixgbe_m_getcapab(void *, mac_capab_t, void *);
int ixgbe_m_setprop(void *, const char *, mac_prop_id_t, uint_t, const void *);
int ixgbe_m_getprop(void *, const char *, mac_prop_id_t, uint_t, void *);
void ixgbe_m_propinfo(void *, const char *, mac_prop_id_t,
    mac_prop_info_handle_t);

int ixgbe_set_priv_prop(ixgbe_t *, const char *, uint_t, const void *);
int ixgbe_get_priv_prop(ixgbe_t *, const char *, uint_t, void *);
boolean_t ixgbe_param_locked(mac_prop_id_t);
enum ioc_reply ixgbe_iov_ioctl(ixgbe_t *, struct iocblk *, mblk_t *);
int ixgbe_validate_iov_params(pci_param_t, char *);

/*
 * Function prototypes in ixgbe_rx.c
 */
mblk_t *ixgbe_ring_rx(ixgbe_rx_ring_t *, int, int);
void ixgbe_ddp_buf_recycle(caddr_t arg);
mblk_t *ixgbe_ring_rx_poll(void *, int, int);

/*
 * Function prototypes in ixgbe_tx.c
 */
mblk_t *ixgbe_ring_tx(void *, mblk_t *);
void ixgbe_free_tcb(tx_control_block_t *);
void ixgbe_put_free_list(ixgbe_tx_ring_t *, link_list_t *);
uint32_t ixgbe_tx_recycle_legacy(ixgbe_tx_ring_t *);
uint32_t ixgbe_tx_recycle_head_wb(ixgbe_tx_ring_t *);

/*
 * Function prototypes in ixgbe_log.c
 */
void ixgbe_notice(void *, const char *, ...);
void ixgbe_log(void *, const char *, ...);
void ixgbe_error(void *, const char *, ...);

/*
 * Function prototypes in ixgbe_stat.c
 */
int ixgbe_init_stats(ixgbe_t *);
int ixgbe_m_stat(void *, uint_t, uint64_t *);
int ixgbe_rx_ring_stat(mac_ring_driver_t, uint_t, uint64_t *);
int ixgbe_tx_ring_stat(mac_ring_driver_t, uint_t, uint64_t *);

#ifdef __cplusplus
}
#endif

#endif /* _IXGBE_SW_H */
