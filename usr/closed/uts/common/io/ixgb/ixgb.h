/*
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_IXGB_H
#define	_IXGB_H

#ifdef __cplusplus
extern "C" {
#endif


#include <sys/types.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/strsubr.h>
#include <sys/stat.h>
#include <sys/pci.h>
#include <sys/note.h>
#include <sys/modctl.h>
#ifdef	__sparcv9
#include <v9/sys/membar.h>
#endif	/* __sparcv9 */
#include <sys/kstat.h>
#include <sys/ethernet.h>
#include <sys/vlan.h>
#include <sys/errno.h>
#include <sys/dlpi.h>
#include <sys/devops.h>
#include <sys/debug.h>
#include <sys/conf.h>
#include <sys/callb.h>

#include <netinet/ip6.h>

#include <inet/common.h>
#include <inet/ip.h>
#include <inet/tcp.h>
#include <netinet/udp.h>
#include <inet/mi.h>
#include <inet/nd.h>
#include <sys/pattr.h>

#include <sys/ddi.h>
#include <sys/sunddi.h>

#include <sys/mac_provider.h>
#include <sys/mac_ether.h>

/*
 * Reconfiguring the network devices requires the net_config privilege
 * in Solaris 10+.
 */
extern int secpolicy_net_config(const cred_t *, boolean_t);

#include <sys/netlb.h>			/* originally from cassini	*/
#include <sys/miiregs.h>		/* by fjlite out of intel 	*/

#include "ixgb_chip.h"
#include "ixgb_gld.h"
/*
 * STREAMS parameters
 */
#define	IXGB_IDNUM		0		/* zero seems to work	*/
#define	IXGB_LOWAT		(512)
#define	IXGB_HIWAT		(512*1024)

#define	IXGB_REGNO_NONE		(~(uintptr_t)0u)

#define	PIO_ADDR(ixgbp, offset)	((void *)((caddr_t)(ixgbp)->io_regs+(offset)))

#define	IXGB_RX_INT_TIME	128
#define	IXGB_RX_INT_PKTS	8

/*
 * Copy an ethernet address
 */
#define	ethaddr_copy(src, dst)	bcopy((src), (dst), ETHERADDRL)

#define	BIS(w, b)	(((w) & (b)) ? B_TRUE : B_FALSE)
#define	BIC(w, b)	(((w) & (b)) ? B_FALSE : B_TRUE)
#define	UPORDOWN(x)	((x) ? "up" : "down")
#define	REG_INDEX(base, index, wid)	((base) + ((index) *(wid)))

/*
 * 'Progress' bit flags ...
 */
#define	PROGRESS_CFG		0x0001	/* config space mapped		*/
#define	PROGRESS_REGS		0x0002	/* registers mapped		*/
#define	PROGRESS_BUFS		0x0004	/* allocate buffers		*/
#define	PROGRESS_RINGINT	0x0008	/* initialize rings		*/
#define	PROGRESS_RESCHED	0x0010	/* resched softint registered	*/
#define	PROGRESS_FACTOTUM	0x0020	/* factotum softint registered	*/
#define	PROGRESS_INT		0x0040	/* h/w interrupt registered	*/
#define	PROGRESS_HWRESET	0x0080	/* h/w reset			*/
#define	PROGRESS_PHY		0x0100	/* PHY initialised		*/
#define	PROGRESS_NDD		0x0200	/* NDD parameters set up	*/
#define	PROGRESS_KSTATS		0x0400	/* kstats created		*/
#define	PROGRESS_READY		0x0800	/* ready for work		*/

/*
 * Bit flags in the 'debug' word ...
 */
#define	IXGB_DBG_STOP		0x00000001	/* early debug_enter()	*/
#define	IXGB_DBG_TRACE		0x00000002	/* general flow tracing	*/

#define	IXGB_DBG_REGS		0x00000010	/* low-level accesses	*/
#define	IXGB_DBG_MII		0x00000020	/* low-level MII access	*/
#define	IXGB_DBG_SEEPROM	0x00000040	/* low-level SEEPROM IO	*/
#define	IXGB_DBG_CHIP		0x00000080	/* low(ish)-level code	*/

#define	IXGB_DBG_RECV		0x00000100	/* receive-side code	*/
#define	IXGB_DBG_SEND		0x00000200	/* packet-send code	*/

#define	IXGB_DBG_INT		0x00001000	/* interrupt handler	*/
#define	IXGB_DBG_FACT		0x00002000	/* factotum (softint)	*/

#define	IXGB_DBG_PHY		0x00010000	/* Copper PHY code	*/
#define	IXGB_DBG_SERDES		0x00020000	/* SerDes code		*/
#define	IXGB_DBG_PHYS		0x00040000	/* Physical layer code	*/
#define	IXGB_DBG_LINK		0x00080000	/* Link status check	*/

#define	IXGB_DBG_INIT		0x00100000	/* initialisation	*/
#define	IXGB_DBG_NEMO		0x00200000	/* MAC layer entry points */
#define	IXGB_DBG_ADDR		0x00400000	/* address-setting code	*/
#define	IXGB_DBG_STATS		0x00800000	/* statistics		*/

#define	IXGB_DBG_IOCTL		0x01000000	/* ioctl handling	*/
#define	IXGB_DBG_LOOP		0x02000000	/* loopback ioctl code	*/
#define	IXGB_DBG_PPIO		0x04000000	/* Peek/poke ioctls	*/
#define	IXGB_DBG_BADIOC		0x08000000	/* unknown ioctls	*/

#define	IXGB_DBG_MCTL		0x10000000	/* mctl (csum) code	*/
#define	IXGB_DBG_NDD		0x20000000	/* NDD operations	*/


/*
 * NOTES:
 *
 * #defines:
 *
 *	IXGB_PCI_CONFIG_RNUMBER and IXGB_PCI_OPREGS_RNUMBER are the
 *	register-set numbers to use for the config space registers
 *	and the operating registers respectively.  On an OBP-based
 *	machine, regset 0 refers to CONFIG space, and regset 1 will
 *	be the operating registers in MEMORY space.  If an expansion
 *	ROM is fitted, it may appear as a further register set.
 *
 *	IXGB_DMA_MODE defines the mode (STREAMING/CONSISTENT) used
 *	for the data buffers.  The descriptors are always set up
 *	in CONSISTENT mode.
 *
 *	IXGB_HEADROOM defines how much space we'll leave in allocated
 *	mblks before the first valid data byte.  This should be chosen
 *	to be 2 modulo 4, so that once the ethernet header (14 bytes)
 *	has been stripped off, the packet data will be 4-byte aligned.
 *	The remaining space can be used by upstream modules to prepend
 *	any headers required.
 */

#define	IXGB_PCI_CONFIG_RNUMBER	0
#define	IXGB_PCI_OPREGS_RNUMBER	1
#define	IXGB_DMA_MODE		DDI_DMA_STREAMING
#define	IXGB_HEADROOM		6

#define	IXGB_CYCLIC_PERIOD	(1500000000)	/* ~1.5s */
#define	IXGB_BUF_SIZE		(16*1024)
#define	IXGB_BUF_SIZE_2K	(2*1024)
#define	IXGB_BUF_SIZE_4K	(4*1024)
#define	IXGB_BUF_SIZE_8K	(8*1024)
#define	IXGB_BUF_SIZE_16K	(16*1024)
#define	IXGB_MTU_MAX		9000
#define	IXGB_MTU_DEFAULT	ETHERMTU
#define	IXGB_MTU_2000		2000
#define	IXGB_MTU_4000		4000
#define	IXGB_MTU_8000		8000
#define	IXGB_MTU_16000		16000

#define	IXGB_LSO_MAXLEN		65535

#define	IXGB_SEND_SLOTS_USED	128

#define	IXGB_RECV_SLOTS_USED	512
#define	IXGB_RECV_POLL_HIWATER	(IXGB_RECV_SLOTS_USED>>4)
#define	IXGB_RECV_SLOTS_BUFF	4096

#define	RX_HIGH_WATER_MASK	0x3fff8
#define	RX_HIGH_WATER_MIN	0x0
#define	RX_HIGH_WATER_MAX	0x3fff8
#define	RX_HIGH_WATER_DEFAULT	0x30000

#define	RX_LOW_WATER_MASK	0x3fff8
#define	RX_LOW_WATER_MIN	0x0
#define	RX_LOW_WATER_MAX	0x3fff8
#define	RX_LOW_WATER_DEFAULT	0x28000

#define	PAUSE_TIME_MASK		0x0ffff
#define	PAUSE_TIME_MIN		0x0
#define	PAUSE_TIME_MAX		0x0ffff
#define	PAUSE_TIME_DEFAULT	0x1000

#define	RX_COALESCE_NUM_DEFAULT	122

#define	IXGB_COPY_SIZE		512
#define	IXGB_MAP_FRAGS		12
#define	IXGB_MAP_COOKIES	18
#define	IXGB_TX_HANDLES		(IXGB_SEND_SLOTS_USED * 3)
#define	IXGB_TX_HANDLES_LO	(IXGB_TX_HANDLES / 3)

#define	SMALL_PACKET		0x1
#define	NORMAL_PACKET		0x2
#define	GIGANT_PACKET		0x4
#define	TX_STREAM_MIN		512
#define	DMA_FLAG_CONS		DDI_DMA_CONSISTENT
#define	DMA_FLAG_STR		DDI_DMA_STREAMING

#define	SPAN_BD_ONE		0
#define	SPAN_BD_MORE		1
#define	SPAN_BD_MAX		8

typedef struct {
	ether_addr_t		addr;		/* in canonical form	*/
	uint8_t			spare;
	uint8_t			set;		/* nonzero => valid	*/
} ixgb_mac_addr_t;

/* flag for the rules of Multicast hash */
enum mca_filter {
	FILTER_TYPE0,
	FILTER_TYPE1,
	FILTER_TYPE2,
	FILTER_TYPE3
};

/* flag for flow control */
enum	flow_control {
	FLOW_NONE,
	TX_PAUSE,
	RX_PAUSE,
	FULL_FLOW
};

/*
 * (Internal) return values from ioctl subroutines
 */
enum ioc_reply {
	IOC_INVAL = -1,			/* bad, NAK with EINVAL	*/
	IOC_DONE,			/* OK, reply sent	*/
	IOC_ACK,			/* OK, just send ACK	*/
	IOC_REPLY,			/* OK, just send reply	*/
	IOC_RESTART_ACK,		/* OK, restart & ACK	*/
	IOC_RESTART_REPLY		/* OK, restart & reply	*/
};


/*
 * Actual state of the Intel 82597EX 10GigE chip
 */
enum ixgb_chip_state {
	IXGB_CHIP_FAULT = -2,		/* fault, need reset	*/
	IXGB_CHIP_ERROR,		/* error, want reset	*/
	IXGB_CHIP_INITIAL,		/* Initial state only	*/
	IXGB_CHIP_RESET,		/* reset, need init	*/
	IXGB_CHIP_STOPPED,		/* Tx/Rx stopped	*/
	IXGB_CHIP_RUNNING		/* with interrupts	*/
};

/*
 * Required state according to MAC
 */
enum ixgb_mac_state {
	IXGB_MAC_UNATTACHED = 0,
	IXGB_MAC_STOPPED,
	IXGB_MAC_STARTED
};

/*
 * (Internal) return values from send_msg subroutines
 */
enum send_status {
	SEND_COPY_FAIL = -2,		/* => NORESOURCES	*/
	SEND_MAP_FAIL,			/* => NORESOURCES	*/
	SEND_COPY_SUCCESS,		/* OK, msg queued	*/
	SEND_MAP_SUCCESS		/* OK, free msg		*/
};

/*
 * Flag for the BD type , when debugging the rx's & tx's fifo
 */
enum bd_type {
	RECV_BD,
	SEND_BD
};

/*
 * Flag for the phy type of Intel's adapter
 */
enum phy_type {
	IXGB_PHY_6005,
	IXGB_PHY_6104,
	IXGB_PHY_TXN17201,
	IXGB_PHY_TXN17401,
	IXGB_PHY_NUM
};

/*
 * Flag to indicate what sending way
 */
enum send_way {
	SEND_COPY,		/* Sending packet by copy Way */
	SEND_MAP		/* Sending packet by map Way */
};

/*
 * Flag to indicate whether load a new context
 */
enum send_hwsum {
	LOAD_NONE,		/* No need to load a new context */
	LOAD_CONT		/* Need to load a new context */
};


/*
 * Flag to kstat type
 */
enum {
	IXGB_KSTAT_RAW = 0,
	IXGB_KSTAT_STATS,
	IXGB_KSTAT_PARAMS,
	IXGB_KSTAT_CHIPID,
	IXGB_KSTAT_DEBUG,
	IXGB_KSTAT_PHYS,
	IXGB_KSTAT_COUNT
};

/*
 * Definiton ixgb cookie structure to
 * record the parameters for dma addr
 * bind
 */
typedef struct {
	uint64_t cookie_addr[IXGB_MAP_COOKIES];
	uint32_t cookie_len[IXGB_MAP_COOKIES];
	uint32_t ncookies;
}ixgb_cookie_t;
/*
 * Definition is used by the following data structures (ie.nd_template_t)
 * before detail definition of ixgb
 */
struct ixgb;
struct send_ring;

/*
 * Named Data (ND) Parameter Management Structure
 */
typedef struct {
	int		ndp_info;
	int		ndp_min;
	int		ndp_max;
	int		ndp_val;
	char		*ndp_name;
} nd_param_t;
/*
 * NDD parameters, divided into:
 *
 *	read-only parameters describing the hardware's capabilities
 *	read-write parameters controlling the advertised capabilities
 *	read-only parameters describing the partner's capabilities
 *	read-only parameters describing the link state
 */
typedef struct {
	char		*name;
	ndgetf_t	ndgetfn;
	ndsetf_t	ndsetfn;
	int		(*getfn)(struct ixgb *);
} nd_template_t;

typedef uint32_t 	ixgb_regno_t;	/* register # (offset)	*/

/*
 * Describes one chunk of allocated DMA-able memory
 *
 * In some cases, this is a single chunk as allocated from the system;
 * but we also use this structure to represent slices carved off such
 * a chunk.  Even when we don't really need all the information, we
 * use this structure as a convenient way of correlating the various
 * ways of looking at a piece of memory (kernel VA, IO space DVMA,
 * handle+offset, etc).
 */
typedef struct {

	caddr_t			private;	/* pointer to ixgb */
	frtn_t			rx_recycle;	/* recycle function */
	mblk_t			*mp;
	ddi_acc_handle_t	acc_hdl;	/* handle for memory	*/
	void			*mem_va;	/* CPU VA of memory	*/
	uint32_t		nslots;		/* number of slots	*/
	uint32_t		size;		/* size per slot	*/
	size_t			alength;	/* allocated size	*/
						/* >= product of above	*/
	ddi_dma_handle_t	dma_hdl;	/* DMA handle		*/
	offset_t		offset;		/* relative to handle	*/
	ddi_dma_cookie_t	cookie;		/* associated cookie	*/
	uint32_t		ncookies;
	uint32_t		token;		/* arbitrary identifier	*/
} dma_area_t;

typedef struct ixgb_queue_item {
	struct ixgb_queue_item	*next;
	void			*item;
} ixgb_queue_item_t;

typedef struct ixgb_queue {
	ixgb_queue_item_t	*head;
	uint32_t		count;
	kmutex_t		*lock;
} ixgb_queue_t;

#define	IXGB_QUEUE_POP(queue, item)		\
{						\
	ixgb_queue_item_t *head;		\
	mutex_enter(queue->lock);		\
	head = queue->head;			\
	if (head != NULL) {			\
		queue->head = head->next;	\
		queue->count--;			\
	}					\
	*item = head;				\
	mutex_exit(queue->lock);		\
}						\

#define	IXGB_QUEUE_PUSH(queue, item)		\
{						\
	mutex_enter(queue->lock);		\
	item->next = queue->head;		\
	queue->head = item;			\
	queue->count++;				\
	mutex_exit(queue->lock);		\
}						\

/*
 * Software version of the Recv Descriptor
 * There's one of these for each recv buffer (up to 512 per ring)
 */
typedef struct sw_rbd {

	dma_area_t		desc;		/* (const) related h/w	*/
						/* descriptor area	*/
	dma_area_t		*bufp;		/* (const) related	*/
						/* buffer area		*/
} sw_rbd_t;

/*
 * Software version of the send Buffer Descriptor
 * There's one of these for each send buffer (up to 512 per ring)
 */
typedef struct sw_sbd {

	dma_area_t		desc;		/* (const) related h/w	*/
						/* descriptor area	*/
	dma_area_t		pbuf;		/* (const) related	*/
						/* buffer area		*/
	void		(*tx_recycle)(struct send_ring *, struct sw_sbd *);
	ixgb_queue_item_t	*mblk_hdl[IXGB_MAP_FRAGS];
	mblk_t			*mp;		/* related mblk, if any	*/
	uint32_t		frags;
} sw_sbd_t;

#define	SW_SBD_FLAG_FREE	0x0000000000000001
#define	SW_SBD_FLAG_MAC		0x0000000000000002
#define	SW_SBD_FLAG_HOST	0x0000000000000004
#define	SW_SBD_FLAG_BIND	0x0000000000000006
#define	SW_SBD_FLAG_COPY	0x0000000000000007

#define	SW_SBUF_FLAG_FILL	0x01
#define	SW_SBUF_FLAG_NULL	0x02
/*
 * Software Receive Buffer (Producer) Ring Control Block
 * There's one of these for each receiver producer ring (up to 3),
 * but each holds buffers of a different size.
 */
typedef struct buff_ring {
	uint64_t		nslots;		/* descriptor area	*/
	struct ixgb		*ixgbp;		/* (const) containing	*/
						/* driver soft state	*/
	boolean_t		rx_bcopy;
	volatile uint16_t	cons_index_p;	/* (const) ptr to h/w	*/
						/* "consumer index"	*/
	uint32_t		rx_free;	/* # of slots available	*/
	uint32_t		rc_next;	/* next slot to recycle	*/
	uint32_t		rfree_next;	/* next free buf index */
	kmutex_t		rc_lock[1];	/* receive recycle access */
	sw_rbd_t		*free_srbds;	/* free ring */
} buff_ring_t;

/*
 * Software Receive (Return) Ring Control Block
 * There's one of these for each receiver return ring (up to 16).
 */
typedef struct recv_ring {
	/*
	 * The elements flagged (const) in the comments below are
	 * set up once during initialiation and thereafter unchanged.
	 */
	dma_area_t		desc;		/* (const) related h/w	*/
						/* descriptor area	*/
	ixgb_rbd_t		*rx_ring;
	struct ixgb		*ixgbp;		/* (const) containing	*/
						/* driver soft state	*/
	kmutex_t		rx_lock[1];	/* serialize h/w update	*/
	ddi_softintr_t		rx_softint;	/* (const) per-ring	*/
						/* receive callback	*/
	uint32_t		rx_next;	/* (const) ptr to h/w	*/
						/* "producer index"	*/
	uint32_t		rx_tail;	/* next slot to examine	*/
	sw_rbd_t		*sw_rbds; 	/* software descriptors	*/
	mac_resource_handle_t	handle;
} recv_ring_t;

/*
 * Software Send Ring Control Block
 * There's one of these for each of (up to) 1 send rings
 */
typedef struct send_ring {
	/*
	 * The elements flagged (const) in the comments below are
	 * set up once during initialiation and thereafter unchanged.
	 */
	dma_area_t		desc;		/* (const) related h/w	*/
						/* descriptor area	*/
	ixgb_sbd_t		*tx_ring;
	struct ixgb		*ixgbp;		/* (const) containing	*/
						/* driver soft state	*/

	uint8_t			ether_header_size;
	uint32_t		start_offset;	/* the newest start offset */
	uint32_t		stuff_offset;	/* the newest stuff offset */
	uint32_t		sum_flags;
	uint32_t		lso_flags;
	uint32_t		mss;
	uint32_t		hdr_len;

	/*
	 * Tx buffer dma handle queue used in ixgb_send_mapped()
	 */
	ddi_dma_handle_t	dma_handle_txbuf[IXGB_TX_HANDLES];
	kmutex_t		txhdl_lock[1];
	kmutex_t		freetxhdl_lock[1];
	ixgb_queue_item_t	*txhdl_head;
	ixgb_queue_t		txhdl_queue;
	ixgb_queue_t		freetxhdl_queue;
	ixgb_queue_t		*txhdl_push_queue;
	ixgb_queue_t		*txhdl_pop_queue;

	/*
	 * The tx_lock must be held when updating
	 * the s/w producer index
	 * (tx_next)
	 */
	kmutex_t		tx_lock[1];	/* serialize h/w update	*/
	uint32_t		tx_next;	/* next slot to use	*/
	uint32_t		tx_flow;

	/*
	 * These counters/indexes are manipulated in the transmit
	 * path using atomics rather than mutexes for speed
	 */
	uint32_t		tx_free;	/* # of slots available	*/

	/*
	 * index (tc_next)
	 */
	kmutex_t		tc_lock[1];	/* send recycle access */
	uint32_t		tc_next;	/* next slot to recycle	*/
						/* ("consumer index")	*/

	sw_sbd_t		*sw_sbds; 	/* software descriptors	*/
} send_ring_t;

typedef struct {
	uint32_t		businfo;	/* from private reg	*/
	uint16_t		command;	/* saved during attach	*/

	uint16_t		vendor;		/* vendor-id		*/
	uint16_t		device;		/* device-id		*/
	uint16_t		subven;		/* subsystem-vendor-id	*/
	uint16_t		subdev;		/* subsystem-id		*/
	uint8_t			revision;	/* revision-id		*/
	uint8_t			clsize;		/* cache-line-size	*/
	uint8_t			latency;	/* latency-timer	*/
	uint16_t		phy_type;	/* Fiber module type 	*/
	uint64_t		hw_mac_addr;	/* from chip register	*/
	ixgb_mac_addr_t		vendor_addr;	/* transform of same	*/
} chip_info_t;

typedef struct {
	offset_t		index;
	char			*name;
} ixgb_ksindex_t;

/*
 * statistics parameters to tune the driver
 */
typedef struct {
	uint64_t		intr_count;
	uint64_t		load_context;
	uint64_t		xmt_err;
	uint64_t		rcv_err;
	uint64_t		ip_hwsum_err;
	uint64_t		tcp_hwsum_err;
}ixgb_sw_statistics_t;

typedef struct {
	ixgb_hw_statistics_t	hw_statistics;
	ixgb_sw_statistics_t	sw_statistics;
}ixgb_statistics_t;


typedef struct ixgb {
	/*
	 * These fields are set by attach() and unchanged thereafter ...
	 */
	dev_info_t		*devinfo;	/* device instance	*/
	mac_handle_t		mh;
	chip_info_t		chipinfo;
	ddi_acc_handle_t	cfg_handle;	/* DDI I/O handle	*/
	ddi_acc_handle_t	io_handle;	/* DDI I/O handle	*/
	void			*io_regs;	/* mapped registers	*/
	int32_t			instance;

	ddi_periodic_t		periodic_id;	/* periodical callback	*/
	uint32_t		factotum_flag;
	ddi_softintr_t		factotum_id;	/* factotum callback	*/
	ddi_softintr_t		resched_id;	/* reschedule callback	*/
	ddi_iblock_cookie_t	iblk;
	uint32_t		watchdog;
	kmutex_t		softintr_lock[1];
					/* used for softintr synchronization */

	uint32_t		progress;	/* attach tracking	*/
	uint32_t		debug;		/* flag to debug function */
	char			ifname[8];	/* "ixgb0" ... "ixgb999" */
	uintptr_t		pagesize;
	uintptr_t		pagemask;

	/*
	 * Runtime read-write data starts here ...
	 * 1 Receive Rings
	 * 1 Send Rings
	 *
	 * Note: they're not necessarily all used.
	 */
	struct buff_ring	buff[1];
	struct recv_ring	recv[1];
	struct send_ring	send[1];

	boolean_t		adaptive_int;
	boolean_t		tx_hw_chksum;
	boolean_t		rx_hw_chksum;
	boolean_t		resched_needed;
	boolean_t		resched_running;
	boolean_t		lso_enable;

	kmutex_t		genlock[1];	/* i/o reg access	*/
	krwlock_t		errlock[1];	/* ixgb restart */
	ixgb_mac_addr_t		curr_addr;
	uint32_t		mcast_hash[IXGB_MCA_ENTRY_NUM];
	uint16_t		mcast_refs[IXGB_MCA_BIT_NUM];
	uint32_t		mcast_filter;
	uint32_t		chip_flow;
	uint32_t		rx_highwater;
	uint32_t		rx_lowwater;
	uint32_t		pause_time;
	boolean_t		xon;
	uint32_t		coalesce_num;
	uint32_t		buf_size;	/* buffer size 		*/
	uint32_t		mtu_size;	/* mtu szie		*/
	uint32_t		max_frame;	/* max frame		*/
	uint32_t		rdelay;		/* receive delay timer */

	/*
	 * Link state data (protected by genlock)
	 */
	int32_t			link_state;
	uint32_t		phy_xgmii_addr;
	int			(*phys_restart)(struct ixgb *);
	hrtime_t		phys_event_time; /* when status changed	*/
	hrtime_t		phys_delta_time; /* time to settle	*/
	uint32_t		chip_reset;

	enum ixgb_mac_state	ixgb_mac_state;	/* definitions above	*/
	enum ixgb_chip_state	ixgb_chip_state; /* definitions above	*/
	boolean_t		promisc;
	uint32_t		param_loop_mode;
	caddr_t			nd_data_p;
	kstat_t			*ixgb_kstats[IXGB_KSTAT_COUNT];
	ixgb_statistics_t	statistics;

	boolean_t		suspended;
}ixgb_t;

/*
 * Sync a DMA area described by a dma_area_t
 */
#define	DMA_SYNC(area, flag)	((void) ddi_dma_sync((area).dma_hdl,	\
				    (area).offset, (area).alength, (flag)))

/*
 * Find the (kernel virtual) address of block of memory
 * described by a dma_area_t
 */
#define	DMA_VPTR(area)		((area).mem_va)

/*
 * Zero a block of memory described by a dma_area_t
 */
#define	DMA_ZERO(area)		bzero(DMA_VPTR(area), (area).alength)

/*
 * Next value of a cyclic index
 */
#define	LAST(index, limit)	((index) ? (index)-1 : (limit) - 1)
#define	NEXT(index, limit)	((index)+1 < (limit) ? (index)+1 : 0)
#define	NEXT_N(index, n, limit)	((index)+(n) < (limit) ?	\
				    (index)+(n) :		\
				    (index)+(n)-(limit))

#define	U32TOPTR(x)	((void *)(uintptr_t)(uint32_t)(x))
#define	PTRTOU32(x)	((uint32_t)(uintptr_t)(void *)(x))

#define	IS_VLAN_PACKET(ptr)					\
	((((struct ether_vlan_header *)ptr)->ether_tpid) ==	\
	htons(ETHERTYPE_VLAN))

/*
 * Debugging ...
 */
#ifdef	DEBUG
#define	IXGB_DEBUGGING		1
#else
#define	IXGB_DEBUGGING		0
#endif	/* DEBUG */


/*
 * 'Do-if-debugging' macro.  The parameter <command> should be one or more
 * C statements (but without the *final* semicolon), which will either be
 * compiled inline or completely ignored, depending on the IXGB_DEBUGGING
 * compile-time flag.
 *
 * You should get a compile-time error (at least on a DEBUG build) if
 * your statement isn't actually a statement, rather than unexpected
 * run-time behaviour caused by unintended matching of if-then-elses etc.
 *
 * Note that the IXGB_DDB() macro itself can only be used as a statement,
 * not an expression, and should always be followed by a semicolon.
 */
#if IXGB_DEBUGGING
#define	IXGB_DDB(command)	do {					\
					{ command; }			\
					_NOTE(CONSTANTCONDITION)	\
				} while (0)
#else 	/* IXGB_DEBUGGING */
#define	IXGB_DDB(command)
/*
 * Old way of debugging.  This is a poor way, as it leeaves empty
 * statements that cause lint to croak.
 * #define	IXGB_DDB(command)	do {				\
 * 					{ _NOTE(EMPTY); }		\
 * 					_NOTE(CONSTANTCONDITION)	\
 * 				} while (0)
 */
#endif	/* IXGB_DEBUGGING */

/*
 * 'Internal' macros used to construct the TRACE/DEBUG macros below.
 * These provide the primitive conditional-call capability required.
 * Note: the parameter <args> is a parenthesised list of the actual
 * printf-style arguments to be passed to the debug function ...
 */
#define	IXGB_XDB(b, w, f, args)	IXGB_DDB(if ((b) & (w)) f args)
#define	IXGB_GDB(b, args)	IXGB_XDB(b, ixgb_debug, (*ixgb_gdb()), args)
#define	IXGB_LDB(b, args)	IXGB_XDB(b, ixgbp->debug, \
				    (*ixgb_db(ixgbp)), args)
#define	IXGB_CDB(f, args)	IXGB_XDB(IXGB_DBG, ixgbp->debug, f, args)

/*
 * Conditional-print macros.
 *
 * Define IXGB_DBG to be the relevant member of the set of IXGB_DBG_* values
 * above before using the IXGB_GDEBUG() or IXGB_DEBUG() macros.  The 'G'
 * versions look at the Global debug flag word (ixgb_debug); the non-G
 * versions look in the per-instance data (ixgbp->debug) and so require a
 * variable called 'ixgbp' to be in scope (and initialised!) before use.
 *
 * You could redefine IXGB_TRC too if you really need two different
 * flavours of debugging output in the same area of code, but I don't
 * really recommend it.
 *
 * Note: the parameter <args> is a parenthesised list of the actual
 * arguments to be passed to the debug function, usually a printf-style
 * format string and corresponding values to be formatted.
 */

#define	IXGB_TRC	IXGB_DBG_TRACE

#define	IXGB_GTRACE(args)	IXGB_GDB(IXGB_TRC, args)
#define	IXGB_GDEBUG(args)	IXGB_GDB(IXGB_DBG, args)
#define	IXGB_TRACE(args)	IXGB_LDB(IXGB_TRC, args)
#define	IXGB_DEBUG(args)	IXGB_LDB(IXGB_DBG, args)

/*
 * Debug-only action macros
 */
#define	IXGB_BRKPT(ixgbp, s)	IXGB_DDB(ixgb_dbg_enter(ixgbp, s))

#define	IXGB_REPORT(args)	IXGB_DDB(ixgb_log args)

/* ixgb_chip.c */
uint32_t ixgb_reg_get32(ixgb_t *ixgbp, ixgb_regno_t regno);
void ixgb_reg_put32(ixgb_t *ixgbp, ixgb_regno_t regno, uint32_t data);
void ixgb_reg_set32(ixgb_t *ixgbp, ixgb_regno_t regno, uint32_t bits);
void ixgb_reg_clr32(ixgb_t *ixgbp, ixgb_regno_t regno, uint32_t bits);
uint64_t ixgb_reg_get64(ixgb_t *ixgbp, uint64_t regno);
void ixgb_reg_put64(ixgb_t *ixgbp, uint64_t regno, uint64_t data);
void ixgb_chip_cyclic(void *arg);
uint_t ixgb_chip_factotum(caddr_t args);
void ixgb_chip_stop(ixgb_t *ixgbp, boolean_t fault);
void ixgb_chip_stop_unlocked(ixgb_t *ixgbp, boolean_t fault);
int ixgb_chip_reset(ixgb_t *ixgbp);
boolean_t ixgb_chip_reset_link(ixgb_t *ixgbp);
void ixgb_chip_start(ixgb_t *ixgbp);
void ixgb_chip_sync(ixgb_t *ixgbp);
void ixgb_chip_cfg_init(ixgb_t *ixgbp, chip_info_t *infop, boolean_t reset);
void ixgb_chip_info_get(ixgb_t *ixgbp);
enum ioc_reply ixgb_chip_ioctl(ixgb_t *ixgbp, mblk_t *mp,
		struct iocblk *iocp);
boolean_t ixgb_chip_reset_engine(ixgb_t *ixgbp,	ixgb_regno_t regno,
		uint32_t mask, uint32_t morebits, uint32_t delayusec);
void ixgb_chip_blank(void *arg, time_t ticks, uint_t count, int flag);

/* ixgb_log.c */
void ixgb_problem(ixgb_t *ixgbp, const char *fmt, ...);
void minidump(ixgb_t *ixgbp, const char *caption, void *dp, uint_t len);
void ixgb_pkt_dump(ixgb_t *ixgbp, void *hbdp, uint_t bd_type,
		const char *msg);
void ixgb_log(ixgb_t *ixgbp, const char *fmt, ...);
void ixgb_notice(ixgb_t *ixgbp, const char *fmt, ...);
void ixgb_error(ixgb_t *ixgbp, const char *fmt, ...);
void ixgb_dbg_enter(ixgb_t *ixgbp, const char *s);
void (*ixgb_db(ixgb_t *ixgbp))(const char *fmt, ...);
void (*ixgb_gdb(void))(const char *fmt, ...);
extern	uint32_t ixgb_debug;
extern	kmutex_t ixgb_log_mutex[1];

/* ixgb.c */
int ixgb_alloc_bufs(ixgb_t *ixgbp);
void ixgb_free_bufs(ixgb_t *ixgbp);
int ixgb_init_rings(ixgb_t *ixgbp);
void ixgb_reinit_rings(ixgb_t *ixgbp);
void ixgb_fini_rings(ixgb_t *ixgbp);
int ixgb_find_mac_address(ixgb_t *ixgbp, chip_info_t *infop);
int ixgb_reset(ixgb_t *ixgbp);
void ixgb_stop(ixgb_t *ixgbp);
void ixgb_start(ixgb_t *ixgbp);
void ixgb_restart(ixgb_t *ixgbp);
void ixgb_chip_cyclic(void *arg);
void ixgb_wake_factotum(ixgb_t *ixgbp);
uint16_t ixgb_mca_hash_index(ixgb_t *ixgbp, const uint8_t *mca);
enum ioc_reply ixgb_loop_ioctl(ixgb_t *ixgbp, queue_t *wq, mblk_t *mp,
    struct iocblk *iocp);

/* ixgb_tx.c */
uint_t ixgb_reschedule(caddr_t arg);
mblk_t *ixgb_m_tx(void *arg, mblk_t *mp);
boolean_t ixgb_tx_recycle(ixgb_t *ixgbp);
void ixgb_tx_recycle_all(ixgb_t *ixgbp);

/* ixgb_rx.c */
void ixgb_receive(ixgb_t *ixgbp);
void ixgb_rx_recycle(caddr_t arg);

/* ixgb_xmii.c */
int ixgb_serdes_restart_intel(ixgb_t *ixgbp);
int ixgb_serdes_restart_sun(ixgb_t *ixgbp);
int ixgb_serdes_check(ixgb_t *ixgbp);

/* ixgb_ndd.c */
int ixgb_nd_init(ixgb_t *ixgbp);
void ixgb_nd_cleanup(ixgb_t *ixgbp);
enum ioc_reply ixgb_nd_ioctl(ixgb_t *ixgbp, queue_t *wq, mblk_t *mp,
    struct iocblk *iocp);
extern const nd_template_t nd_template[];

/* ixgb_kstats.c */
void ixgb_init_kstats(ixgb_t *ixgbp, int instance);
void ixgb_fini_kstats(ixgb_t *ixgbp);
extern const ixgb_ksindex_t ixgb_statistics[];

/* ixgb_atomic.c */
uint32_t ixgb_atomic_reserve(uint32_t *count_p, uint32_t n);
void ixgb_atomic_renounce(uint32_t *count_p, uint32_t n);
uint32_t ixgb_atomic_next(uint32_t *sp, uint32_t limit);
uint32_t ixgb_atomic_shl32(uint32_t *sp, uint_t count);

#ifdef __cplusplus
}
#endif

#endif /* _IXGB_H */
