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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SXGE_H
#define	_SXGE_H

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
#include <sys/fm/io/ddi.h>

#include <sxge_impl.h>
#include <sxge_hw_defs.h>
#include <sxge_xdc.h>
#include <sxge_fma.h>

#define	MODULE_NAME		"sxge"	/* module name */
/*
 * SXGE debug/diagnostic IOCTLS.
 */
#define	SXGE_IOC		((((('S' << 8) + 'X') << 8) + 'G') << 8)

#define	SXGE_GET_REGS		(SXGE_IOC|1)
#define	SXGE_GLOBAL_RESET	(SXGE_IOC|2)
#define	SXGE_TX_SIDE_RESET	(SXGE_IOC|3)
#define	SXGE_RX_SIDE_RESET	(SXGE_IOC|4)
#define	SXGE_RESET_MAC		(SXGE_IOC|5)
#define	SXGE_GET_TCAM		(SXGE_IOC|6)
#define	SXGE_PUT_TCAM		(SXGE_IOC|7)

/*
 * Required for EPS bypass mode.
 */
#define	SXGE_EPS_BYPASS_RD	(SXGE_IOC|13)
#define	SXGE_EPS_BYPASS_WR	(SXGE_IOC|14)

typedef struct _sxge_eb_cmd_t {
	uint64_t		offset;
	uint64_t		data;
} sxge_eb_cmd_t;

/*
 * sxge driver state
 */
#define	SXGE_UNKNOWN			0x00
#define	SXGE_INITIALIZED		0x01
#define	SXGE_STARTED			0x02
#define	SXGE_SUSPENDED			0x04

/*
 * Bit flags for driver attach state
 */
#define	A_FM_INIT_DONE			0x0001
#define	A_PCI_SETUP_DONE		0x0002
#define	A_REGS_MAP_DONE			0x0004
#define	A_GET_RES_DONE			0x0008
#define	A_PARAM_INIT_DONE		0x0010
#define	A_CFG_PROPS_DONE		0x0020
#define	A_STATS_INIT_DONE		0x0040
#define	A_MUTEX_INIT_DONE		0x0080
#define	A_LINK_INIT_DONE		0x0100
#define	A_RINGS_INIT_DONE		0x0200
#define	A_INTR_ADD_DONE			0x0400
#define	A_MAC_REG_DONE			0x0800
#define	A_EN_INTR_DONE			0x1000
#define	A_SRIOV_CB_DONE			0x2000

/*
 * Some driver tunables/variables
 */
#define	SXGE_CHECK_TIMER		5000
#define	SXGE_INTR_TIMER			16

/*
 * Property lookups
 */
#define	SXGE_PROP_EXISTS(d, n)	ddi_prop_exists(DDI_DEV_T_ANY, (d), \
				    DDI_PROP_DONTPASS, (n))
#define	SXGE_PROP_GET_INT(d, n)	ddi_prop_get_int(DDI_DEV_T_ANY, (d), \
				    DDI_PROP_DONTPASS, (n), -1)

typedef	union _sxge_ether_addr_t {
#if 1
	struct {
		uint32_t	high;
		uint32_t	low;
	} reg;
#endif
	struct {
		uint8_t		set;
		uint8_t		redundant;
		uint8_t		addr[ETHERADDRL];
	} mac;
} sxge_ether_addr_t;

typedef enum {
	sxge_param_instance,
	sxge_param_rx_intr_pkts,
	sxge_param_rx_intr_time,
	sxge_param_accept_jumbo,
	sxge_param_max
} sxge_param_index_t;

/*
 * Named Dispatch Parameter Management Structure
 */
typedef struct _sxge_param_t {
	uint32_t minimum;
	uint32_t maximum;
	uint32_t val;
	uint32_t old_value;
	char   *fcode_name;
	char   *name;
} sxge_param_t;


typedef enum {
	free = 0,
	busy
} sxge_udelay_t;

extern	sxge_udelay_t sxge_delay_busy;

#define	SXGE_PARAM_READ			0x00000001ULL
#define	SXGE_PARAM_WRITE		0x00000002ULL
#define	SXGE_PARAM_SHARED		0x00000004ULL
#define	SXGE_PARAM_PRIV			0x00000008ULL
#define	SXGE_PARAM_RW			SXGE_PARAM_READ | SXGE_PARAM_WRITE
#define	SXGE_PARAM_RWS			SXGE_PARAM_RW | SXGE_PARAM_SHARED
#define	SXGE_PARAM_RWP			SXGE_PARAM_RW | SXGE_PARAM_PRIV

#define	SXGE_PARAM_RXDMA		0x00000010ULL
#define	SXGE_PARAM_TXDMA		0x00000020ULL
#define	SXGE_PARAM_MAC			0x00000040ULL

#define	SXGE_PARAM_CMPLX		0x00010000ULL
#define	SXGE_PARAM_NDD_WR_OK		0x00020000ULL
#define	SXGE_PARAM_INIT_ONLY		0x00040000ULL
#define	SXGE_PARAM_INIT_CONFIG		0x00080000ULL

#define	SXGE_PARAM_READ_PROP		0x00100000ULL
#define	SXGE_PARAM_PROP_ARR32		0x00200000ULL
#define	SXGE_PARAM_PROP_ARR64		0x00400000ULL
#define	SXGE_PARAM_PROP_STR		0x00800000ULL

#define	SXGE_PARAM_DONT_SHOW		0x80000000ULL

#define	SXGE_PARAM_ARRAY_CNT_MASK	0x0000ffff00000000ULL
#define	SXGE_PARAM_ARRAY_CNT_SHIFT	32ULL
#define	SXGE_PARAM_ARRAY_ALLOC_MASK	0xffff000000000000ULL
#define	SXGE_PARAM_ARRAY_ALLOC_SHIFT	48ULL

typedef enum {
	sxge_lb_normal,
	sxge_lb_internal,
	sxge_lb_external
} sxge_lb_t;

enum sxge_mac_state {
	SXGE_MAC_STOPPED = 0,
	SXGE_MAC_STARTED
};

/*
 * kstat
 */
typedef struct {
	uint8_t index;
	uint8_t type;
	char *name;
} sxge_kstat_index_t;

typedef struct _filter_t {
	uint32_t all_phys_cnt;
	uint32_t all_multicast_cnt;
	uint32_t all_sap_cnt;
} filter_t;

typedef	struct _sxge_vmac_kstat_t {
	/* TxVMAC stats */
	kstat_named_t	tx_frame_cnt;
	kstat_named_t	tx_byte_cnt;
	/* RxVMAC stats */
	kstat_named_t	rx_frame_cnt_d;
	kstat_named_t	rx_byte_cnt_d;
	kstat_named_t	rx_drop_frame_cnt_d;
	kstat_named_t	rx_drop_byte_cnt_d;
	kstat_named_t	rx_mcast_frame_cnt_d;
	kstat_named_t	rx_bcast_frame_cnt_d;
	kstat_named_t	rx_frame_cnt;
	kstat_named_t	rx_byte_cnt;
	kstat_named_t	rx_drop_frame_cnt;
	kstat_named_t	rx_drop_byte_cnt;
	kstat_named_t	rx_mcast_frame_cnt;
	kstat_named_t	rx_bcast_frame_cnt;
} sxge_vmac_kstat_t;

typedef struct _sxge_vmac_stats_t {
	/* TxVMAC stats */
	uint64_t	tx_frame_cnt;
	uint64_t	tx_byte_cnt;
	/* RxVMAC stats */
	uint64_t	rx_frame_cnt;
	uint64_t	rx_byte_cnt;
	uint64_t	rx_drop_frame_cnt;
	uint64_t	rx_drop_byte_cnt;
	uint64_t	rx_mcast_frame_cnt;
	uint64_t	rx_bcast_frame_cnt;

	uint32_t	errlog;
} sxge_vmac_stats_t;

typedef struct _sxge_rdc_kstat {
	/*
	 * Receive DMA channel statistics.
	 * This structure needs to be consistent with sxge_rdc_stat_index_t
	 * in sxge_kstat.c
	 */
	kstat_named_t	ipackets;
	kstat_named_t	rbytes;
	kstat_named_t	errors;
	kstat_named_t	jumbo_pkts;

	kstat_named_t	rcr_unknown_err;
	kstat_named_t	rcr_sha_par_err;
	kstat_named_t	rbr_pre_par_err;
	kstat_named_t	rbr_pre_emty;

	kstat_named_t	rcr_shadow_full;
	kstat_named_t	rbr_tmout;
	kstat_named_t	peu_resp_err;

	kstat_named_t	ctrl_fifo_ecc_err;
	kstat_named_t	data_fifo_ecc_err;

	kstat_named_t	rcrfull;
	kstat_named_t	rbr_empty;
	kstat_named_t	rbr_empty_fail;
	kstat_named_t	rbr_empty_restore;
	kstat_named_t	rbrfull;
	kstat_named_t	rcr_invalids;	/* Account for invalid RCR entries. */

	kstat_named_t	rcr_to;
	kstat_named_t	rcr_thresh;
	kstat_named_t	pkt_drop;
} sxge_rdc_kstat_t;

typedef struct _sxge_rx_ring_stats_t {
	uint64_t	ipackets;
	uint64_t	ibytes;
	uint32_t	ierrors;
	uint32_t	jumbo_pkts;

	/*
	 * Error event stats.
	 */
	uint32_t	rcr_unknown_err;
	uint32_t	ctrl_fifo_ecc_err;
	uint32_t	data_fifo_ecc_err;
	uint32_t	rbr_tmout;		/* rbr_cpl_to */
	uint32_t 	peu_resp_err;		/* peu_resp_err */
	uint32_t 	rcr_sha_par;		/* rcr_shadow_par_err */
	uint32_t 	rbr_pre_par;		/* rbr_prefetch_par_err */
	uint32_t 	rbr_pre_empty;		/* rbr_pre_empty */
	uint32_t 	rcr_shadow_full;	/* rcr_shadow_full */
	uint32_t 	rcrfull;		/* rcr_full */
	uint32_t 	rbr_empty;		/* rbr_empty */
	uint32_t 	rbr_empty_fail;		/* rbr_empty_fail */
	uint32_t 	rbr_empty_restore;	/* rbr_empty_restore */
	uint32_t 	rbrfull;		/* rbr_full */
	/*
	 * RCR invalids: when processing RCR entries, can
	 * run into invalid RCR entries.  This counter provides
	 * a means to account for invalid RCR entries.
	 */
	uint32_t 	rcr_invalids;		/* rcr invalids */
	uint32_t 	rcr_to;			/* rcr_to */
	uint32_t 	rcr_thres;		/* rcr_thres */
	/* Packets dropped in order to prevent rbr_empty condition */
	uint32_t 	pkt_drop;
	uint32_t	errlog;
} sxge_rx_ring_stats_t;

typedef struct _sxge_tx_ring_stats_t {
	uint64_t		opackets;
	uint64_t		obytes;
	uint64_t		obytes_with_pad;
	uint64_t		oerrors;

	uint32_t		tx_inits;
	uint32_t		tx_no_buf;

	uint32_t		peu_resp_err;
	uint32_t		pkt_size_hdr_err;
	uint32_t		runt_pkt_drop_err;
	uint32_t		pkt_size_err;
	uint32_t		tx_rng_oflow;
	uint32_t		pref_par_err;
	uint32_t		tdr_pref_cpl_to;
	uint32_t		pkt_cpl_to;
	uint32_t		invalid_sop;
	uint32_t		unexpected_sop;
	uint32_t		desc_len_err;
	uint32_t		desc_nptr_err;

	uint64_t		count_hdr_size_err;
	uint64_t		count_runt;
	uint64_t		count_abort;

	uint32_t		tx_starts;
	uint32_t		tx_no_desc;
	uint32_t		tx_dma_bind_fail;
	uint32_t		tx_hdr_pkts;
	uint32_t		tx_ddi_pkts;
	uint32_t		tx_jumbo_pkts;
	uint32_t		tx_max_pend;
	uint32_t		tx_marks;
	uint32_t		errlog;
} sxge_tx_ring_stats_t;

typedef struct _sxge_pfc_stats {
	uint32_t		pkt_drop;	/* pfc_int_status */
	uint32_t		tcam_parity_err;
	uint32_t		vlan_parity_err;

	uint32_t		bad_cs_count;	/* pfc_bad_cs_counter */
	uint32_t		drop_count;	/* pfc_drop_counter */
	uint32_t		errlog;
} sxge_pfc_stats_t;

typedef struct _sxge_stats_t {
	/*
	 *  Overall structure size
	 */
	size_t			stats_size;

	kstat_t			*ksp;
	kstat_t			*rdc_ksp[SXGE_MAX_RDCS];
	kstat_t			*tdc_ksp[SXGE_MAX_TDCS];
	kstat_t			*rdc_sys_ksp;
	kstat_t			*tdc_sys_ksp;
	kstat_t			*pfc_ksp;
	kstat_t			*vmac_ksp;
	kstat_t			*port_ksp;
	kstat_t			*mmac_ksp;
	kstat_t			*peu_sys_ksp;
	sxge_vmac_stats_t	vmac_stats;	/* VMAC Statistics */
	sxge_rx_ring_stats_t	rdc_stats[SXGE_MAX_RDCS]; /* per rdc stats */
	sxge_tx_ring_stats_t	tdc_stats[SXGE_MAX_TDCS]; /* per tdc stats */
	sxge_pfc_stats_t	pfc_stats;	/* pfc stats */
} sxge_stats_t;

typedef struct _sxge_t sxge_t;
typedef struct _sxge_ldg_t sxge_ldg_t;
typedef struct _sxge_ldv_t sxge_ldv_t;
typedef uint_t	(*sxge_sys_intr_t)(void *arg1, void *arg2);
typedef uint_t	(*sxge_ldv_intr_t)(void *arg1, void *arg2);

typedef struct _sxge_intr_t {
	boolean_t		registered; /* interrupts are registered */
	boolean_t		enabled; 	/* interrupts are enabled */
	boolean_t		msi_enable;	/* debug or configurable? */
	uint8_t			nldevs;		/* # of logical devices */
	int			sup_types; /* interrupt types supported */
	int			type;		/* interrupt type to add */
	int			msi_intx_cnt;	/* # msi/intx ints returned */
	int			intr_added;	/* # ints actually needed */
	int			intr_cap;	/* interrupt capabilities */
	size_t			intr_size;	/* size of array to allocate */
	ddi_intr_handle_t 	*htable;	/* For array of interrupts */
	/* Add interrupt number for each interrupt vector */
	int			pri;
} sxge_intr_t;

/* Logical device group */
struct _sxge_ldg_t {
	uint8_t			vni_num;
	uint8_t			ldg;		/* logical group number */
	boolean_t		arm;
	boolean_t		interrupted;
	uint16_t		timer;	/* counter */
	uint8_t			nldvs;
	uint32_t		ldsv;
	uint8_t			nf;
	uint8_t			vni_cnt;
	sxge_sys_intr_t		sys_intr_handler;
	sxge_t			*sxge;
	sxge_ldv_t		*ldvp;
	sxge_ldg_t		*next;
};

/* Logical device vector */
struct _sxge_ldv_t {
	uint8_t			ldg_assigned;
	uint8_t			vni_num;
	uint8_t			ldv;
	boolean_t		use_timer;
	sxge_ldg_t		*ldgp;
	uint8_t			ldf_masks;
	uint8_t			nf;
	sxge_ldv_intr_t		intr_handler;
	sxge_t			*sxge;
	sxge_ldv_t		*next;
	uint_t			idx;
};

typedef struct _sxge_ldgv_t {
	uint8_t			nldvs;
	uint8_t			start_ldg;
	uint8_t			maxldgs;
	uint8_t			maxldvs;
	uint8_t			ldg_intrs;
	uint8_t			ldv_intrs;
	uint32_t		tmres;
	sxge_ldg_t		*ldgp;
	sxge_ldv_t		*ldvp;
} sxge_ldgv_t;

typedef struct _sxge_timeout {
	timeout_id_t	id;
	clock_t		ticks;
	kmutex_t	lock;
	uint32_t	link_status;
	boolean_t	report_link_status;
} sxge_timeout;

typedef struct _sxge_vmac_t {
	uint_t			enable;
	uint_t			vnin;
	uint_t			vmacn;
	uint64_t		pbase;
	sxge_pio_handle_t	phdl;

	boolean_t		is_jumbo;
	uint64_t		tx_config;
	uint64_t		rx_config;
	uint16_t		minframesize;
	uint16_t		maxframesize;
	uint16_t		maxburstsize;
	uint8_t			addr[ETHERADDRL];
	boolean_t		link_up;
	uint32_t		default_mtu;
	boolean_t		promisc;
} sxge_vmac_t;

struct sxge_hw {
	uint16_t	device_id;
	uint16_t	vendor_id;
	uint16_t	subsystem_device_id;
	uint16_t	subsystem_vendor_id;
};

/*
 * Ring Group Strucuture.
 */
typedef struct _sxge_rx_ring_group_t {
	mac_ring_type_t		type;
	mac_group_handle_t	ghandle;
	sxge_t			*sxge;
	uint_t			index;
	uint_t			started;
} sxge_ring_group_t;

/*
 * Ring Handle
 */
typedef struct _sxge_ring_handle_t {
	sxge_t			*sxge;
	uint_t			index;		/* port-wise */
	uint_t			started;
	uint_t			mr_valid;
	uint64_t		mr_gen_num;
	mac_ring_handle_t	ring_handle;
	uint_t			polled;
} sxge_ring_handle_t;

typedef struct _sxge_vni_resource_t {
	uint8_t		vmac[SXGE_MAX_VNI_VMAC_NUM];
	uint8_t		dma[SXGE_MAX_VNI_DMA_NUM];
	uint8_t		didx[SXGE_MAX_VNI_DMA_NUM];
	uint8_t		vidx[SXGE_MAX_VNI_DMA_NUM];
	uint8_t		vmac_cnt;
	uint8_t		dma_cnt;
	uint8_t		present;
	uint16_t	vni_num;
} sxge_vni_resource_t;

/*
 * Host EPS Mailbox
 */
typedef struct _sxge_hosteps_mbx_t {
	boolean_t ready;
	boolean_t posted;
	boolean_t acked;
	boolean_t imb_full;
} sxge_hosteps_mbx_t;

#define	XMAC_MAX_ADDR_ENTRY	(3)

typedef struct _sxge_mac_addr_t {
	ether_addr_t    addr;
	uint_t	  flags;
} sxge_mac_addr_t;

/*
 * Software adapter state
 */
struct _sxge_t {
	int 			instance;
	int			nf; /* network function number ??? */
	dev_info_t		*dip;	/* device instance */
	dev_info_t		*p_dip;	/* Parent's device instance */
	struct sxge_hw		hw;
	uint16_t		pci_did;
	uint16_t		pci_vid;
	uint8_t			*pbase;

	sxge_pcicfg_handle_t	pcicfg_hdl;
	sxge_pio_handle_t	pio_hdl;
	sxge_msix_handle_t	msix_hdl;
	sxge_rom_handle_t	rom_hdl;
	uint64_t		sys_pgsz;
	uint64_t		sys_pgmsk;
	ether_addr_t		factaddr;
	ether_addr_t		ouraddr;
	uint_t			accept_jumbo;

	mac_handle_t		mach;
	enum sxge_mac_state	sxge_mac_state;
	sxge_vmac_t		vmac;
	sxge_vni_resource_t	vni_arr[SXGE_MAX_VNI_NUM];
	int			vni_cnt;
	int			dma_cnt;
	int			vmac_cnt;

	uint32_t		link_speed;
	uint32_t		link_duplex;
	uint32_t		link_state;
	uint32_t		link_asmpause;
	uint32_t		link_autoneg;
	uint32_t		link_pause;

	uint32_t		sxge_state;	/* driver state */
	uint32_t		load_state;	/* driver load state */

	sxge_hosteps_mbx_t	mbox;

	sxge_stats_t		*statsp;
	uint32_t		param_count;
	sxge_param_t		param_arr[sxge_param_max];
	caddr_t			param_list;

	sxge_lb_t		lb_mode;

	/*
	 * Receive Rings
	 */
	uint32_t		num_rx_rings;	/* Number of rx rings in use */
	rdc_state_t		rdc[SXGE_MAX_RDCS]; /* Array of rx rings */
	sxge_os_mutex_t		rdc_lock[SXGE_MAX_RDCS];

	uint32_t		rx_ring_size;	/* Rx descriptor ring size */
	uint32_t		rx_buf_size;	/* Rx buffer size */

	/*
	 * Receive Groups
	 */
	uint32_t		num_rx_groups;	/* Number rx groups in use */
	uint8_t			rx_grps[SXGE_MAX_RXGRPS]; /* Array rx grps */

	/*
	 * Transmit Rings
	 */
	uint32_t		num_tx_rings;	/* Number of tx rings in use */
	uint_t			tdc_prsr_en;
	tdc_state_t		tdc[SXGE_MAX_TDCS]; /* Array of tx rings */
	sxge_os_mutex_t		tdc_lock[SXGE_MAX_TDCS];
	uint32_t		num_tx_groups;	/* Number rx groups in use */

	uint32_t		tx_ring_size;	/* Tx descriptor ring size */
	uint32_t		tx_buf_size;	/* Tx buffer size */

	sxge_ring_handle_t	tx_ring_handles[SXGE_MAX_TDCS];
	sxge_ring_handle_t	rx_ring_handles[SXGE_MAX_RDCS];
	sxge_ring_group_t	rx_groups[SXGE_MAX_RXGRPS];
	sxge_ring_group_t	tx_groups[SXGE_MAX_TXGRPS];


	sxge_mac_addr_t		mac_pool[XMAC_MAX_ADDR_ENTRY];

	int			fm_capabilities; /* FMA capabilities */

	sxge_intr_t		intr;

	/* Logical device and group data structures. */
	sxge_ldgv_t		*ldgvp;

	sxge_os_mutex_t		gen_lock;	/* lock for device access */
	sxge_os_mutex_t	 mbox_lock;


	ulong_t			sys_page_size;
	ulong_t 		sys_burst_sz;
	timeout_id_t 		sxge_timerid;

	int 			suspended;

	filter_t 		filter;		/* Current instance filter */
/* 	p_hash_filter_t 	hash_filter; */	/* Multicast hash filter. */
	sxge_os_rwlock_t	filter_lock;	/* Lock to protect filters. */


	sxge_os_mutex_t		pio_lock;
	sxge_timeout		tmout;

	int			msix_count;
	int			msix_index;
	uint32_t		msix_table[32][3];
	uint32_t		msix_table_check[1][3];

	ddi_softintr_t		idp;
	ddi_iblock_cookie_t	ibc;
	uint_t			soft_poll_en;
	timeout_id_t 		soft_tmr_id;

	boolean_t		tx_hcksum_enable;

/* lock and conditional variable for non-blocking wait */
	kmutex_t		nbw_lock;
	kcondvar_t		nbw_cv;

/* debug level and block settings */
	uint32_t		dbg_level;
	uint32_t		dbg_blks;

	uint_t			sriov_pf;
	uint_t			num_vfs;
	uint_t			pf_inst;
	uint32_t		pf_grp;	/* group number of PF */
	ddi_cb_handle_t	 cb_hdl;	/* sriov callback handle */
	/* vf_data_t		vf[4]; */

	void			*eb_req;
};

/*
 * Function prototypes
 */

typedef	void	(*fptrv_t)();
timeout_id_t sxge_start_timer(sxge_t *, fptrv_t, int);
void sxge_stop_timer(sxge_t *, timeout_id_t);

void sxge_check_hw_state(sxge_t *);
void sxge_fill_ring(void *, mac_ring_type_t, const int, const int,
	    mac_ring_info_t *, mac_ring_handle_t);
void sxge_group_get(void *, mac_ring_type_t, int, mac_group_info_t *,
		    mac_group_handle_t);
void sxge_error(sxge_t *, char *);
int sxge_init_param(sxge_t *);
int sxge_uninit_param(sxge_t *);
int sxge_init_stats(sxge_t *);
int sxge_uninit_stats(sxge_t *);
int sxge_link_init(sxge_t *);
int sxge_link_uninit(sxge_t *);
int sxge_check_acc_handle(ddi_acc_handle_t);
int sxge_mbx_link_speed_req(sxge_t *);
int sxge_add_mcast_addr(sxge_t *, struct ether_addr *);
int sxge_add_ucast_addr(sxge_t *, struct ether_addr *);
int sxge_del_mcast_addr(sxge_t *, struct ether_addr *);
int sxge_del_ucast_addr(sxge_t *, struct ether_addr *);
int sxge_rx_vmac_enable(sxge_t *);
int sxge_rx_vmac_disable(sxge_t *);
int sxge_tx_vmac_enable(sxge_t *);
int sxge_tx_vmac_disable(sxge_t *);
int sxge_disable_poll(void *);
int sxge_enable_poll(void *);

mblk_t *sxge_rx_ring_poll(void *, int);
mblk_t *sxge_rx_ring_poll_outer(void *, int, int);
mblk_t *sxge_tx_ring_send(void *, mblk_t *);

int sxge_rx_ring_stat(mac_ring_driver_t, uint_t, uint64_t *);
int sxge_tx_ring_stat(mac_ring_driver_t, uint_t, uint64_t *);

int sxge_eb_rd_ioctl(sxge_t *, mblk_t *);
void sxge_eb_wr_ioctl(sxge_t *, mblk_t *);

void sxge_dbg_msg(sxge_t *, uint32_t, char *, ...);
void sxge_dbg_mp(mblk_t *mp);

int sxge_vmac_stat_update(kstat_t *ksp, int rw);
int sxge_mbx_l2_addr(sxge_t *sxge, uint64_t addr, uint16_t type);

int sxge_tdc_init(tdc_state_t *tdcp);
int sxge_tdc_mask(tdc_state_t *tdcp, boolean_t mask);
int sxge_tdc_reinit(tdc_state_t *tdcp);
int sxge_tdc_errs(tdc_state_t *tdcp);
int  sxge_tdc_send(tdc_state_t *tdcp,  mblk_t *mp, uint_t *flags);
int sxge_tdc_fini(tdc_state_t *tdcp);

int sxge_rdc_init(rdc_state_t *rdcp);
int sxge_rdc_mask(rdc_state_t *rdcp, boolean_t mask);
int sxge_rdc_reinit(rdc_state_t *rdcp);
int sxge_rdc_errs(rdc_state_t *rdcp);

mblk_t *sxge_rdc_recv(rdc_state_t *rdcp, uint_t bytes, uint_t *flags);
int sxge_rdc_fini(rdc_state_t *rdcp);

void sxge_tdc_dump(tdc_state_t *tdcp);
void sxge_rdc_dump(rdc_state_t *rdcp);

void sxge_fm_report_error(sxge_t *sxgep, uint8_t err_portn, uint8_t err_chan,
					sxge_fm_ereport_id_t fm_ereport_id);
int sxge_fm_check_acc_handle(ddi_acc_handle_t handle);
int sxge_fm_check_dma_handle(ddi_dma_handle_t handle);
void sxge_fm_init(sxge_t *sxgep, ddi_device_acc_attr_t *reg_attr,
					ddi_dma_attr_t *dma_attr);
void sxge_fm_fini(sxge_t *sxgep);

void sxge_tdc_inject_err(sxge_t *, uint32_t, uint8_t);
void sxge_rdc_inject_err(sxge_t *, uint32_t, uint8_t);

#ifdef __cplusplus
}
#endif

#endif /* _SXGE_H */
