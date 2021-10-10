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
 * Copyright (c) 2010 by Chelsio Communications, Inc.
 */

#ifndef _CXGE_ADAPTER_H
#define	_CXGE_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/ddi.h>
#include <sys/ethernet.h>

#define	t3_unimplemented(x) cmn_err(CE_PANIC, \
	"%s unimplemented (%s in %s at line %d)", x,  __func__, __FILE__, \
	__LINE__)

#define	CXGEN_DEVNAME	"cxgen"
#define	CXGE_DEVNAME	"cxge"

enum {
	FULL_INIT_DONE	= (1 << 0),
	USING_MSI	= (1 << 1),
	USING_MSIX	= (1 << 2),
	QUEUES_BOUND	= (1 << 3),
	CXGEN_STOP_TIMER = (1 << 4),
};

enum {
	TXQ_ETH = 0,
	TXQ_OFLD = 1,
	TXQ_CTRL = 2
};

struct cxgen_sge_stats {
	ulong_t parity_errors;
	ulong_t framing_errors;
	ulong_t rspq_starved;
	ulong_t fl_empty;
	ulong_t rspq_credit_overflows;
	ulong_t rspq_disabled;
	ulong_t lo_db_empty;
	ulong_t lo_db_full;
	ulong_t hi_db_empty;
	ulong_t hi_db_full;
	ulong_t lo_credit_underflows;
	ulong_t hi_credit_underflows;
};

typedef enum {
	THRD_IDLE,
	THRD_BUSY,
	THRD_EXITING,
	THRD_EXITED
} thread_status_t;

typedef enum {
	unused,
	use_small,
	use_dma,
	immediate_data
} meta_state_t;

typedef struct egr_meta_s {
	ddi_dma_handle_t dma_hdl;
	ddi_dma_handle_t dvma_hdl;
	ddi_dma_cookie_t cookie;
	uint_t ccount;
	mblk_t *message;
	int meta_cnt;
	int hw_cnt;
	meta_state_t state;
	void *small_buf_va;
	uint64_t small_buf_pa;
} egr_meta_t, *p_egr_meta_t;

typedef struct fl_meta_s {
	ddi_dma_handle_t dma_hdl;
	ddi_dma_handle_t dvma_hdl;
	ddi_acc_handle_t acc_hdl;
	ddi_dma_cookie_t cookie;
	frtn_t fr_rtn;
	caddr_t buf;
	size_t buf_size;
	mblk_t *message;
	uint_t ref_cnt;
} fl_meta_t, *p_fl_meta_t;

typedef struct egr_hwq_s {
	struct tx_desc *queue;
	uint32_t wr_index;
	uint32_t rd_index;
	volatile uint32_t crd_index;
	uint32_t credits;
	uint32_t depth;
	uint32_t gen_bit;
	ddi_dma_handle_t dma_hdl;
	ddi_acc_handle_t acc_hdl;
	ddi_dma_cookie_t cookie;
} egr_hwq_t, *p_egr_hwq_t;

typedef struct fl_hwq_s {
	struct rx_desc *queue;
	uint32_t index;
	uint32_t pending;
	uint32_t gen_bit;
	uint32_t depth;
	ddi_dma_handle_t dma_hdl;
	ddi_acc_handle_t acc_hdl;
	ddi_dma_cookie_t cookie;
} fl_hwq_t, *p_fl_hwq_t;

typedef struct egr_metaq_s {
	p_egr_meta_t queue;
	uint32_t wr_index;
	uint32_t rd_index;
	uint32_t depth;
	caddr_t bufs;
	uint32_t buf_size;
	ddi_dma_handle_t dma_hdl;
	ddi_acc_handle_t acc_hdl;
	ddi_dma_cookie_t cookie;
} egr_metaq_t, *p_egr_metaq_t;

typedef struct fl_metaq_s {
	p_fl_meta_t *queue;
	uint32_t depth;
} fl_metaq_t, *p_fl_metaq_t;

struct mblk_pair {
	mblk_t *head, *tail;
};

struct rspq_budget {
	int descs;
	int frames;
	int bytes;
};

struct rspq_stats {
	ulong_t rx_imm_data;	/* # of rx descs with imm. data */
	ulong_t rx_buf_data;	/* # of rx descs with data in a fl buf */
	ulong_t nomem;		/* # of failures to make an mblk out of data */
	ulong_t starved;	/* # of times rspq was starved */
	ulong_t disabled;	/* # of times rspq disabled */
	ulong_t pure;		/* # of pure responses seen so far */
	ulong_t txq_unblocked;	/* # of times txq was unblocked in rx */
	ulong_t max_descs_processed;	/* max # of descs processed in one go */
	ulong_t desc_budget_met;	/* # of times desc budget was met */
	ulong_t max_frames_chained;	/* max # of frames chained together */
	ulong_t chain_budget_met;	/* # of times chaining budget was met */
	ulong_t	rx_frames;		/* # of frames received */
	ulong_t	rx_octets;		/* # of octets received */
};

struct sge_rspq {
	kmutex_t	lock;
	uint32_t	cleanup;
	uint32_t	cntxt_id;
	struct rsp_desc	*queue;
	uint32_t	index;
	uint32_t	gen_bit;
	uint32_t	credits;
	uint32_t	holdoff_tmr;
	uint32_t	next_holdoff;
	uint32_t	depth;
	ddi_dma_handle_t dma_hdl;
	ddi_acc_handle_t acc_hdl;
	ddi_dma_cookie_t cookie;
	kcondvar_t	cv;
	kthread_t	*thread;
	volatile thread_status_t state;
	uint32_t 	more;
	uint32_t 	polling;
	uint32_t	drop_frame;
	struct mblk_pair chain;
	struct mblk_pair frame;
	struct rspq_stats stats;
	struct rspq_budget budget;
	struct kstat 	*ksp;
};

struct fl_stats {
	ulong_t spareq_hit;	/* spareq replacement was reused */
	ulong_t spareq_miss;	/* spareq replacement was busy, had to alloc */
	ulong_t nomem_kalloc;	/* kmem_alloc failed */
	ulong_t nomem_mblk;	/* desballoc failed */
	ulong_t nomem_meta_hdl;	/* DMA handle allocation failed */
	ulong_t nomem_meta_mem;	/* DMA memory allocation failed */
	ulong_t nomem_meta_bind; /* DMA binding failed or unsuitable */
	ulong_t nomem_meta_mblk; /* desballoc failed */
	ulong_t empty;		/* # of times fl was empty */
};

struct sge_fl {
	kmutex_t	lock;
	uint32_t	cleanup;
	uint32_t	cntxt_id;
	fl_hwq_t	hwq;
	fl_metaq_t	metaq[2];
	struct fl_stats stats;
	struct kstat 	*ksp;
};

struct txq_stats {
	ulong_t tx_imm_data;	/* data would've fit, but we don't do it yet */
	ulong_t tx_lso;		/* cpl_tx_pkt_lso requests so far */
	ulong_t tx_pkt;		/* cpl_tx_pkt requests so far */
	ulong_t used_small_buf;	/* copy buffer was used */
	ulong_t used_big_buf;	/* buffer too big for copy buffer */
	ulong_t txq_blocked;	/* tx blocked due to lack of descriptors */
	ulong_t pullup;		/* msg block had to be pulled up */
	ulong_t pullup_failed;	/* msg block could not be pulled up */
	ulong_t dma_map_failed;	/* DMA mapping failed */
	ulong_t max_frame_len;	/* max length of a frame given to us */
	ulong_t max_mblks_in_frame;
	ulong_t max_dma_segs_in_mblk;
	ulong_t max_dma_segs_in_frame;
	ulong_t	tx_frames;	/* # of frames transmitted */
	ulong_t	tx_octets;	/* # of octets transmitted */
};

struct sge_txq {
	kmutex_t	lock;
	uint32_t	cleanup;
	uint32_t	token;
	uint32_t	cntxt_id;
	egr_hwq_t	hwq;
	egr_metaq_t	metaq;
	ulong_t		sys_page_sz;
	boolean_t	queueing;
	uint32_t	unacked;
	struct txq_stats stats;
	struct kstat 	*ksp;
};

struct sge_qset {
	int			idx;
	void			*rx_rh;
	void			*tx_rh;
	struct port_info	*port;
	struct sge_rspq		rspq;
	struct sge_fl		fl[SGE_RXQ_PER_SET];
	struct sge_txq		txq[SGE_TXQ_PER_SET];
	uint64_t		rx_gen_num;
};

struct sge {
	struct sge_qset	qs[SGE_QSETS];
	kmutex_t	reg_lock; /* for context operations */
};

/*
 * Simple container for upto 8 L2 multicast addresses.  "valid" indicates which
 * entries are in use (0x1 = addr[0], ...,  0xff = all 8 in use).  More can be
 * appended via the next pointer if/when this bucket fills up.  Insertions and
 * deletions are O(n) which is not that great.  Optimize if that matters.
 */
#define	MC_BUCKET_SIZE 8
struct mcaddr_list {
	long valid; /* fls friendly and big enough for MC_BUCKET_SIZE */
	struct ether_addr addr[MC_BUCKET_SIZE];
	struct mcaddr_list *next;
};

enum {
	LF_NO = 0,
	LF_MAYBE,
	LF_YES
};

struct port_info {
	p_adapter_t adapter;

	/* child dip */
	dev_info_t *dip;

	kmutex_t lock;

	struct cphy phy;
	struct cmac mac;
	struct link_config link_config;
	void (*link_change)(struct port_info *, int, int, int, int, int);
	int link_fault;

	uint8_t port_id;
	uint8_t tx_chan;
	uint8_t txpkt_intf;
	uint8_t first_qset;
	uint32_t nqsets;

	/* ucast + mcast addresses, promisc counter. */
	uint8_t hw_addr[ETHERADDRL]; /* from the eeprom */
	struct ether_addr ucaddr_list[EXACT_ADDR_FILTERS];
	uint_t ucaddr_count;
	struct mcaddr_list *mcaddr_list;
	uint_t mcaddr_count;
	uint_t promisc;
	uint_t allmulti;
	krwlock_t rxmode_lock;
#define	UCADDR(p, i) (&p->ucaddr_list[i].ether_addr_octet[0])

	/*
	 * The data rx routine - called with the qset# it was received on and
	 * the mblk it should process.
	 */
	int (*rx)(struct sge_qset *, mblk_t *);

	/*
	 * tx update routine - called to indicate any blocked tx should be tried
	 * again as resources are now available.
	 */
	int (*tx_update)(struct sge_qset *);

	/* Power Management */
	int (*port_suspend)(struct port_info *);
	int (*port_resume)(struct port_info *);
};

struct adapter {
	dev_info_t *dip;
	int instance;

	/* PCI config space access handle */
	ddi_acc_handle_t pci_regh;

	/* MMIO register access handle */
	ddi_acc_handle_t regh;
	caddr_t regp;

	/* kstats */
	kstat_t	*config_ksp;
	kstat_t	*sge_ksp;

	uint_t qsets_per_port;
	struct port_info port[MAX_NPORTS];

	/* Interrupt handling related */
	int intr_type;
	int intr_count;
	int intr_cap;
	uint_t intr_lo_priority;
	uint_t intr_hi_priority;
	ddi_intr_handle_t *intr_handle;

	/* Task queue */
	ddi_taskq_t *tq;

	uint_t open;
	uint_t flags;
	uint_t open_device_map;

	uint8_t rxpkt_map[SGE_QSETS];
	uint8_t rrss_map[SGE_QSETS];
	uint16_t rspq_map[RSS_TABLE_SIZE];

	/* adapter lock */
	kmutex_t lock;

	/* these two are needed by common routines */
	kmutex_t mdio_lock;
	kmutex_t elmr_lock;

	/* version strings */
	char fw_vers[16];
	char mc_vers[16];

	/* Miscellaneous stats (to feed kstats) */
	struct cxgen_sge_stats sge_stats;

	/* tick timer */
	timeout_id_t timer;

	/* Bookkeeping for the hardware layer */
	struct adapter_params  params;
	unsigned int slow_intr_mask;
	unsigned long irq_stats[IRQ_NUM_STATS];

	/* FMA */
	int fma_cap;

	struct sge	sge;
	struct mc7	pmrx;
	struct mc7	pmtx;
	struct mc7	cm;
	struct mc5	mc5;
	uint16_t	pci_vendor;
	uint16_t	pci_device;
};

struct t3_rx_mode {
	int idx;
	struct port_info *pi;
	struct mcaddr_list *mc_bucket;
	long mc_bucket_idx;
};

#define	MDIO_LOCK(adapter)	mutex_enter(&(adapter)->mdio_lock)
#define	MDIO_UNLOCK(adapter)	mutex_exit(&(adapter)->mdio_lock)
#define	ELMR_LOCK(adapter)	mutex_enter(&(adapter)->elmr_lock)
#define	ELMR_UNLOCK(adapter)	mutex_exit(&(adapter)->elmr_lock)

uint32_t t3_read_reg(p_adapter_t adapter, uint32_t reg_addr);
void t3_write_reg(p_adapter_t adapter, uint32_t reg_addr, uint32_t val);

void t3_os_pci_read_config_4(p_adapter_t adapter, int reg, uint32_t *val);
void t3_os_pci_write_config_4(p_adapter_t adapter, int reg, uint32_t val);
void t3_os_pci_read_config_2(p_adapter_t adapter, int reg, uint16_t *val);
void t3_os_pci_write_config_2(p_adapter_t adapter, int reg, uint16_t val);
void t3_os_pci_read_config_1(p_adapter_t adapter, int reg, uint8_t *val);

void t3_init_rx_mode(struct t3_rx_mode *rm, struct port_info *pi);
uint8_t *t3_get_next_mcaddr(struct t3_rx_mode *rm);
struct port_info *adap2pinfo(p_adapter_t adapter, int idx);
int t3_os_pci_save_state(p_adapter_t adapter);
int t3_os_pci_restore_state(struct adapter *adapter);
void t3_os_set_hw_addr(p_adapter_t adapter, int port_idx, u8 hw_addr[]);

int t3_os_find_pci_capability(p_adapter_t adapter, int cap);
void t3_os_link_changed(p_adapter_t adapter, int port_id, int link_status,
			int speed, int duplex, int fc, int mac_was_reset);
void t3_os_phymod_changed(p_adapter_t adap, int port_id);
void t3_sge_err_intr_handler(p_adapter_t);
void t3_os_ext_intr_handler(p_adapter_t);
void t3_os_link_fault_handler(adapter_t *adapter, int port_id);

unsigned long simple_strtoul(unsigned char *, char **, int);
int t3_get_desc(struct sge_qset *, uint_t, uint_t, uint64_t *);

#ifdef __cplusplus
}
#endif

#endif /* _CXGE_ADAPTER_H */
