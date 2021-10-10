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
 * Copyright Exar 2010. Copyright (c) 2002-2010 Neterion, Inc.
 * All right Reserved.
 *
 * FileName :   vxgehal-virtualpath.h
 *
 * Description:  HAL Virtual Path structures
 *
 * Created:       14 Sep 2006
 */

#ifndef	VXGE_HAL_VIRTUALPATH_H
#define	VXGE_HAL_VIRTUALPATH_H

__EXTERN_BEGIN_DECLS

struct __vxge_hal_device_t;
struct __vxge_hal_umq_dmq_ir_t;
struct __vxge_hal_reg_entry_t;

/*
 * struct __vxge_hal_rnic_oid_db_t - Object Id Database
 * @id_start: The start of object Id range
 * @id_end: The end of object id range
 * @id_map: The bit fields that identify the allocation status of an object id
 * @id_next_byte: The next byte in the map to be searched for free object id
 * @id_inst_number: Number of times the object ids are recycled
 * @ids_used: Number of Ids in use
 * @db_lock: Database lock
 *
 * Database used to allocate object Ids.
 */
typedef struct __vxge_hal_rnic_oid_db_t {
	u32			id_start;
	u32			id_end;
	u8			id_map[4096];
	u32			id_next_byte;
	u8			id_inst_number;
#define	VXGE_HAL_RNIC_OID_DB_OID_GET(sid, sin)		((sin<<24)|sid)
#define	VXGE_HAL_RNIC_OID_DB_SID_GET(id)			(id&0xFFFFFF)
#define	VXGE_HAL_RNIC_OID_DB_SIN_GET(id)			((id>>24)&0xFF)
	u32			ids_used;
	spinlock_t		db_lock;
}__vxge_hal_rnic_oid_db_t;

/*
 * struct __vxge_hal_virtualpath_t - Virtual Path
 *
 * @vp_id: Virtual path id
 * @vp_open: This flag specifies if vxge_hal_vp_open is called from LL Driver
 * @hldev: Hal device
 * @vp_config: Virtual Path Config
 * @vp_reg: VPATH Register map address in BAR0
 * @vpmgmt_reg: VPATH_MGMT register map address
 * @is_first_vpath: 1 if this first vpath in this vfunc, 0 otherwise
 * @ifmsg_up_seqno: Up wmsg seq number
 * @promisc_en: Promisc mode state flag.
 * @min_bandwidth: Guaranteed Band Width in Mbps
 * @max_bandwidth: Maximum Band Width in Mbps
 * @max_mtu: Max mtu that can be supported
 * @sess_grps_available: The mask of available session groups for this vpath
 * @bmap_root_assigned: The bitmap root for this vpath
 * @vsport_choices: The mask of vsports that are available for this vpath
 * @vsport_number: vsport attached to this vpath
 * @sess_grp_start: Session oid start
 * @sess_grp_end: session oid end
 * @max_kdfc_db: Maximum kernel mode doorbells
 * @max_nofl_db: Maximum non offload doorbells
 * @max_ofl_db: Maximum offload doorbells
 * @max_msg_db: Maximum message doorbells
 * @rxd_mem_size: Maximum RxD memory size
 * @tx_intr_num: Interrupt Number associated with the TX
 * @rx_intr_num: Interrupt Number associated with the RX
 * @einta_intr_num: Interrupt Number associated with Emulated MSIX DeAssert IntA
 * @bmap_intr_num: Interrupt Number associated with the bitmap
 * @nce_oid_db: NCE ID database
 * @session_oid_db: Session Object Id database
 * @active_lros: Active LRO session list
 * @active_lro_count: Active LRO count
 * @free_lros: Free LRO session list
 * @free_lro_count: Free LRO count
 * @lro_lock: LRO session lists' lock
 * @sqs: List of send queues
 * @sq_lock: Lock for operations on sqs
 * @srqs: List of SRQs
 * @srq_lock: Lock for operations on srqs
 * @srq_oid_db: DRQ object id database
 * @cqrqs: CQRQs
 * @cqrq_lock: Lock for operations on cqrqs
 * @cqrq_oid_db: CQRQ object id database
 * @umqh: UP Message Queue
 * @dmqh: Down Message Queue
 * @umq_dmq_ir: The adapter will overwrite and update this location as Messages
 *		are read from DMQ and written into UMQ.
 * @umq_dmq_ir_reg_entry: Reg entry of umq_dmq_ir_t
 * @ringh: Ring Queue
 * @fifoh: FIFO Queue
 * @vpath_handles: Virtual Path handles list
 * @vpath_handles_lock: Lock for operations on Virtual Path handles list
 * @stats_block: Memory for DMAing stats
 * @stats: Vpath statistics
 *
 * Virtual path structure to encapsulate the data related to a virtual path.
 * Virtual paths are allocated by the HAL upon getting configuration from the
 * driver and inserted into the list of virtual paths.
 */
typedef struct __vxge_hal_virtualpath_t {
	u32				vp_id;

	u32				vp_open;
#define	VXGE_HAL_VP_NOT_OPEN		0
#define	VXGE_HAL_VP_OPEN		1

	struct __vxge_hal_device_t		*hldev;
	vxge_hal_vp_config_t		*vp_config;
	vxge_hal_vpath_reg_t		*vp_reg;
	vxge_hal_vpmgmt_reg_t		*vpmgmt_reg;
	__vxge_hal_non_offload_db_wrapper_t	*nofl_db;
	__vxge_hal_messaging_db_wrapper_t	*msg_db;
	u32				is_first_vpath;
	u32				ifmsg_up_seqno;

	u32				promisc_en;
#define	VXGE_HAL_VP_PROMISC_ENABLE	1
#define	VXGE_HAL_VP_PROMISC_DISABLE	0

	u32				min_bandwidth;
	u32				max_bandwidth;

	u32				max_mtu;
	u64				sess_grps_available;
	u32				bmap_root_assigned;
	u32				vsport_choices;
	u32				vsport_number;
	u32				sess_grp_start;
	u32				sess_grp_end;
	u32				max_kdfc_db;
	u32				max_nofl_db;
	u32				max_ofl_db;
	u32				max_msg_db;
	u32				rxd_mem_size;
	u32				tx_intr_num;
	u32				rx_intr_num;
	u32				einta_intr_num;
	u32				bmap_intr_num;
	__vxge_hal_rnic_oid_db_t		nce_oid_db;
	__vxge_hal_rnic_oid_db_t		session_oid_db;
	vxge_list_t			active_lros;
	u32				active_lro_count;
	vxge_list_t			free_lros;
	u32				free_lro_count;
	spinlock_t			lro_lock;
	vxge_list_t			sqs;
	spinlock_t			sq_lock;
	vxge_list_t			srqs;
	spinlock_t			srq_lock;
	__vxge_hal_rnic_oid_db_t		srq_oid_db;
	vxge_list_t			cqrqs;
	spinlock_t			cqrq_lock;
	__vxge_hal_rnic_oid_db_t		cqrq_oid_db;
	vxge_hal_umq_h			umqh;
	vxge_hal_dmq_h			dmqh;
	struct __vxge_hal_umq_dmq_ir_t	*umq_dmq_ir;
	struct __vxge_hal_reg_entry_t	*umq_dmq_ir_reg_entry;
	vxge_hal_ring_h			ringh;
	vxge_hal_fifo_h			fifoh;
	vxge_list_t			vpath_handles;
	spinlock_t			vpath_handles_lock;
	__vxge_hal_blockpool_entry_t		*stats_block;
	vxge_hal_vpath_stats_hw_info_t	*hw_stats;
	vxge_hal_vpath_stats_hw_info_t	*hw_stats_sav;
	vxge_hal_vpath_stats_sw_info_t	*sw_stats;
} __vxge_hal_virtualpath_t;

/*
 * struct __vxge_hal_vpath_handle_t - List item to store callback information
 * @item: List head to keep the item in linked list
 * @vpath: Virtual path to which this item belongs
 * @cb_fn: Callback function to be called
 * @client_handle: Client handle to be returned with the callback
 *
 * This structure is used to store the callback information.
 */
typedef	struct __vxge_hal_vpath_handle_t {
	vxge_list_t			item;
	__vxge_hal_virtualpath_t		*vpath;
	vxge_hal_vpath_callback_f	cb_fn;
	vxge_hal_client_h		client_handle;
}__vxge_hal_vpath_handle_t;

#define	VXGE_HAL_VIRTUAL_PATH_DB_INIT(p, start, end)			\
	    {								\
		p->id_start = start;					\
		p->id_end = end;					\
		p->id_inst_number = 0;					\
		vxge_os_memzero(p->id_map, (end - start + 8)/8);	\
		p->id_next_byte = 0;					\
		p->ids_used = 0;					\
	}

#define	VXGE_HAL_VIRTUAL_PATH_HANDLE(vpath)				\
		((vxge_hal_vpath_h)(vpath)->vpath_handles.next)

#define	VXGE_HAL_VIRTUAL_PATH_DMQ_HANDLE(vph)				\
		(((__vxge_hal_vpath_handle_t *)(vph))->vpath->dmqh)

#define	VXGE_HAL_VIRTUAL_PATH_UMQ_HANDLE(vph)				\
		(((__vxge_hal_vpath_handle_t *)(vph))->vpath->umqh)

#define	VXGE_HAL_VPATH_STATS_PIO_READ(offset) {				\
	status = __vxge_hal_vpath_stats_access(vpath,			\
			VXGE_HAL_STATS_OP_READ,				\
			offset,						\
			&val64);					\
	if (status != VXGE_HAL_OK) {					\
		vxge_hal_trace_log_stats(hldev, vpath->vp_id,		\
		    "<==  %s:%s:%d  Result: %d",			\
		    __FILE__, __func__, __LINE__, status);		\
		return (status);					\
	}								\
}

vxge_hal_status_e
__vxge_hal_vpath_pci_read(
	struct __vxge_hal_device_t  *hldev,
	u32			vp_id,
	u32			offset,
	u32			length,
	void			*val);

vxge_hal_status_e
__vxge_hal_vpath_reset_check(
	__vxge_hal_virtualpath_t	*vpath);

vxge_hal_status_e
__vxge_hal_vpath_fw_memo_get(
	pci_dev_h		 pdev,
	pci_reg_h		 regh0,
	u32			 vp_id,
	vxge_hal_vpath_reg_t	 *vpath_reg,
	u32			 action,
	u64			 param_index,
	u64			 *data0,
	u64			 *data1);

vxge_hal_status_e
__vxge_hal_vpath_fw_flash_ver_get(
	pci_dev_h		 pdev,
	pci_reg_h		 regh0,
	u32			 vp_id,
	vxge_hal_vpath_reg_t	 *vpath_reg,
	vxge_hal_device_version_t *fw_version,
	vxge_hal_device_date_t	 *fw_date,
	vxge_hal_device_version_t *flash_version,
	vxge_hal_device_date_t	 *flash_date);

vxge_hal_status_e
__vxge_hal_vpath_card_info_get(
	pci_dev_h		 pdev,
	pci_reg_h		 regh0,
	u32			 vp_id,
	vxge_hal_vpath_reg_t	 *vpath_reg,
	u8			 *serial_number,
	u8			 *part_number,
	u8			 *product_description);

vxge_hal_status_e
__vxge_hal_vpath_pmd_info_get(
	pci_dev_h		   pdev,
	pci_reg_h		   regh0,
	u32			   vp_id,
	vxge_hal_vpath_reg_t	   *vpath_reg,
	u32			   *ports,
	vxge_hal_device_pmd_info_t *pmd_port0,
	vxge_hal_device_pmd_info_t *pmd_port1);

u64
__vxge_hal_vpath_pci_func_mode_get(
	pci_dev_h		 pdev,
	pci_reg_h		 regh0,
	u32			 vp_id,
	vxge_hal_vpath_reg_t	 *vpath_reg);

vxge_hal_device_lag_mode_e __vxge_hal_vpath_lag_mode_get(
			__vxge_hal_virtualpath_t *vpath);

u64
__vxge_hal_vpath_vpath_map_get(
	pci_dev_h		 pdev,
	pci_reg_h		 regh0,
	u32			 vp_id,
	u32			 vh,
	u32			 func,
	vxge_hal_vpath_reg_t	 *vpath_reg);

vxge_hal_status_e
__vxge_hal_vpath_fw_upgrade(
	pci_dev_h		 pdev,
	pci_reg_h		 regh0,
	u32			 vp_id,
	vxge_hal_vpath_reg_t	 *vpath_reg,
	u8			 *buffer,
	u32			 length);

vxge_hal_status_e
__vxge_hal_vpath_pcie_func_mode_set(
		    struct __vxge_hal_device_t *hldev,
		    u32 vp_id,
		    u32 func_mode);

vxge_hal_status_e
__vxge_hal_vpath_flick_link_led(
		    struct __vxge_hal_device_t *hldev,
		    u32 vp_id,
		    u32 port,
		    u32 on_off);

vxge_hal_status_e
__vxge_hal_vpath_udp_rth_set(
		    struct __vxge_hal_device_t *hldev,
		    u32 vp_id,
		    u32 on_off);

vxge_hal_status_e
__vxge_hal_vpath_rts_table_get(
	vxge_hal_vpath_h		vpath_handle,
	u32			action,
	u32			rts_table,
	u32			offset,
	u64			*data1,
	u64			*data2);

vxge_hal_status_e
__vxge_hal_vpath_rts_table_set(
	vxge_hal_vpath_h		vpath_handle,
	u32			action,
	u32			rts_table,
	u32			offset,
	u64			data1,
	u64			data2);

vxge_hal_status_e
__vxge_hal_vpath_adaptive_lro_filter_set(
	vxge_hal_vpath_h		vpath_handle,
	u32			is_ipv6,
	u64			ip_addr_low,
	u64			ip_addr_high,
	u32			use_vlan,
	u32			vlan_id);

vxge_hal_status_e
__vxge_hal_vpath_hw_reset(
	vxge_hal_device_h	devh,
	u32			vp_id);

vxge_hal_status_e
__vxge_hal_vpath_sw_reset(
	vxge_hal_device_h	devh,
	u32			vp_id);

vxge_hal_status_e
__vxge_hal_vpath_prc_configure(
	vxge_hal_device_h	devh,
	u32			vp_id);

vxge_hal_status_e
__vxge_hal_vpath_kdfc_configure(
	vxge_hal_device_h	devh,
	u32			vp_id);

vxge_hal_status_e
__vxge_hal_vpath_mac_configure(
	vxge_hal_device_h	devh,
	u32			vp_id);

vxge_hal_status_e
__vxge_hal_vpath_umqdmq_configure(
	vxge_hal_device_h	devh,
	u32			vp_id);

vxge_hal_status_e
__vxge_hal_vpath_tim_configure(
	vxge_hal_device_h	devh,
	u32			vp_id);

vxge_hal_status_e
__vxge_hal_vpath_umqdmq_start(
	vxge_hal_device_h	devh,
	u32			vp_id);

vxge_hal_status_e
__vxge_hal_vpath_hw_initialize(
	vxge_hal_device_h	devh,
	u32			vp_id);

vxge_hal_status_e
__vxge_hal_vp_initialize(
	vxge_hal_device_h	devh,
	u32			vp_id,
	vxge_hal_vp_config_t	*config);

void
__vxge_hal_vp_terminate(
	vxge_hal_device_h	devh,
	u32			vp_id);

vxge_hal_status_e
__vxge_hal_vpath_hw_addr_get(
	pci_dev_h		pdev,
	pci_reg_h		regh0,
	u32			vp_id,
	vxge_hal_vpath_reg_t	*vpath_reg,
	macaddr_t		macaddr,
	macaddr_t		macaddr_mask);

vxge_hal_status_e
__vxge_hal_vp_oid_allocate(
	__vxge_hal_virtualpath_t *vpath,
	__vxge_hal_rnic_oid_db_t *objdb,
	u32 *objid);

vxge_hal_status_e
__vxge_hal_vp_oid_free(
	__vxge_hal_virtualpath_t *vpath,
	__vxge_hal_rnic_oid_db_t *objdb,
	u32 objid);

vxge_hal_status_e
vxge_hal_vpath_attach(
	vxge_hal_device_h	devh,
	u32			vp_id,
	vxge_hal_vpath_callback_f cb_fn,
	void			*userdata,
	vxge_hal_vpath_h		*vpath_handle);

vxge_hal_status_e
vxge_hal_vpath_detach(
	vxge_hal_vpath_h		vpath_handle);

vxge_hal_status_e
vxge_hal_vpath_obj_count_get(
	vxge_hal_vpath_h		vpath_handle,
	vxge_hal_vpath_sw_obj_count_t *obj_counts);

vxge_hal_status_e
__vxge_hal_vpath_intr_enable(
	__vxge_hal_virtualpath_t	*vpath);

vxge_hal_status_e
__vxge_hal_vpath_intr_disable(
	__vxge_hal_virtualpath_t	*vpath);

vxge_hal_device_link_state_e
__vxge_hal_vpath_link_state_test(
	__vxge_hal_virtualpath_t	*vpath);

vxge_hal_device_link_state_e
__vxge_hal_vpath_link_state_poll(
	__vxge_hal_virtualpath_t	*vpath);

vxge_hal_device_data_rate_e
__vxge_hal_vpath_data_rate_poll(
	__vxge_hal_virtualpath_t	*vpath);

vxge_hal_status_e
__vxge_hal_vpath_alarm_process(
	__vxge_hal_virtualpath_t	*vpath,
	u32			skip_alarms);

vxge_hal_status_e
__vxge_hal_vpath_stats_access(
	__vxge_hal_virtualpath_t	*vpath,
	u32			operation,
	u32			offset,
	u64			*stat);

vxge_hal_status_e
__vxge_hal_vpath_xmac_tx_stats_get(
	__vxge_hal_virtualpath_t	*vpath,
	vxge_hal_xmac_vpath_tx_stats_t *vpath_tx_stats);

vxge_hal_status_e
__vxge_hal_vpath_xmac_rx_stats_get(
	__vxge_hal_virtualpath_t	*vpath,
	vxge_hal_xmac_vpath_rx_stats_t *vpath_rx_stats);


vxge_hal_status_e
__vxge_hal_vpath_hw_stats_get(
	__vxge_hal_virtualpath_t *vpath,
	vxge_hal_vpath_stats_hw_info_t *hw_stats);

__EXTERN_END_DECLS

#endif /* VXGE_HAL_VIRTUALPATH_H */
