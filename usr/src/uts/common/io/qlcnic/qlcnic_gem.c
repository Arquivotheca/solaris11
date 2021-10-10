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

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/debug.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strlog.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/kstat.h>
#include <sys/vtrace.h>
#include <sys/dlpi.h>
#include <sys/strsun.h>
#include <sys/ethernet.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/dditypes.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/pci.h>
#include <sys/ddi_intr.h>

#include "qlcnic.h"
#include "qlcnic_hw.h"
#include "qlcnic_brdcfg.h"
#include "qlcnic_cmn.h"
#include "qlcnic_phan_reg.h"
#include "qlcnic_ioctl.h"
#include "qlcnic_hw_pci_regs.h"

char qlcnic_ident[] = "QLogic qlcnic v" QLCNIC_VERSIONID;
char qlcnic_driver_name[] = "qlcnic";
int verbmsg = 0;

static char txbcopythreshold_propname[] = "tx_bcopy_threshold";
static char rxbcopythreshold_propname[] = "rx_bcopy_threshold";
static char rxringsize_propname[] = "rx_ring_size";
static char jumborxringsize_propname[] = "jumbo_rx_ring_size";
static char txringsize_propname[] = "tx_ring_size";
static char defaultmtu_propname[] = "default_mtu";
static char dmesg_propname[] = "verbose_driver";
static char lso_enable_propname[] = "lso_enable";
static char max_sds_rings_propname[] = "max_sds_rings";
static char max_status_ring_size_propname[] = "max_status_ring_size";
static char tx_recycle_threshold_propname[] = "tx_recycle_threshold";
static char intr_coalesce_rx_time_us_propname[] = "intr_coalesce_rx_time_us";
static char intr_coalesce_rx_pkts_propname[] = "intr_coalesce_rx_pkts";
static char intr_coalesce_tx_time_us_propname[] = "intr_coalesce_tx_time_us";
static char intr_coalesce_tx_pkts_propname[] = "intr_coalesce_tx_pkts";
static char fm_cap_propname[] = "fma_capable";


#define	STRUCT_COPY(a, b)	bcopy(&(b), &(a), sizeof (a))

extern int qlcnic_register_mac(qlcnic_adapter *adapter);
extern void qlcnic_fini_kstats(qlcnic_adapter* adapter);
extern void qlcnic_remove(qlcnic_adapter *adapter, int flag);
extern uint_t qlcnic_intr(caddr_t, caddr_t);
extern uint_t qlcnic_msix_rx_intr(caddr_t, caddr_t);
extern int qlcnic_api_lock(struct qlcnic_adapter_s *adapter);
extern void qlcnic_api_unlock(struct qlcnic_adapter_s *adapter);
int qlcnic_p3p_chk_start_firmware(qlcnic_adapter *adapter);
int qlcnic_p3p_clear_dev_state(qlcnic_adapter *adapter, int flag);
int qlcnic_start_firmware(qlcnic_adapter *adapter);
int qlcnic_dev_init(qlcnic_adapter *adapter);
void cleanup_adapter(struct qlcnic_adapter_s *adapter);
static void qlcnic_destroy_rx_ring(qlcnic_rcv_desc_ctx_t *rcv_desc);
/* Data access requirements. */
static struct ddi_device_acc_attr qlcnic_dev_attr = {
	DDI_DEVICE_ATTR_V1,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC,
	DDI_FLAGERR_ACC
};

static struct ddi_device_acc_attr qlcnic_buf_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC
};

static ddi_dma_attr_t qlcnic_dma_attr_desc = {
	DMA_ATTR_V0,		/* dma_attr_version */
	0,			/* dma_attr_addr_lo */
	0xffffffffull,		/* dma_attr_addr_hi */
	0x000fffffull,		/* dma_attr_count_max */
	4096,			/* dma_attr_align */
	0x000fffffull,		/* dma_attr_burstsizes */
	4,			/* dma_attr_minxfer */
	0x003fffffull,		/* dma_attr_maxxfer */
	0xffffffffull,		/* dma_attr_seg */
	1,			/* dma_attr_sgllen */
	1,			/* dma_attr_granular */
	DDI_DMA_FLAGERR		/* dma_attr_flags */
};

static ddi_dma_attr_t qlcnic_dma_attr_txbuf = {
	DMA_ATTR_V0,		/* dma_attr_version */
	0,			/* dma_attr_addr_lo */
	0x7ffffffffULL, 	/* dma_attr_addr_hi */
	0xffffull,		/* dma_attr_count_max */
	4096,			/* dma_attr_align */
	0xfff8ull,		/* dma_attr_burstsizes */
	1,			/* dma_attr_minxfer */
	0xffffffffull,		/* dma_attr_maxxfer */
	0xffffull,		/* dma_attr_seg */
	1,			/* dma_attr_sgllen */
	1,			/* dma_attr_granular */
	DDI_DMA_RELAXED_ORDERING	/* dma_attr_flags */
};

static ddi_dma_attr_t qlcnic_dma_attr_rxbuf = {
	DMA_ATTR_V0,		/* dma_attr_version */
	0,			/* dma_attr_addr_lo */
	0x7ffffffffULL, 	/* dma_attr_addr_hi */
	0xffffull,		/* dma_attr_count_max */
	4096,			/* dma_attr_align */
	0xfff8ull,		/* dma_attr_burstsizes */
	1,			/* dma_attr_minxfer */
	0xffffffffull,		/* dma_attr_maxxfer */
	0xffffull,		/* dma_attr_seg */
	1,			/* dma_attr_sgllen */
	1,			/* dma_attr_granular */
	DDI_DMA_RELAXED_ORDERING	/* dma_attr_flags */
};

static ddi_dma_attr_t qlcnic_dma_attr_cmddesc = {
	DMA_ATTR_V0,		/* dma_attr_version */
	0,			/* dma_attr_addr_lo */
	0xffffffffffffffffull,	/* dma_attr_addr_hi */
	0xffffffffull,		/* dma_attr_count_max */
	1,			/* dma_attr_align */
	0xfff8ull,		/* dma_attr_burstsizes */
	1,			/* dma_attr_minxfer */
	0xffffffff,		/* dma_attr_maxxfer */
	0xffffffff,		/* dma_attr_seg */
	MAX_COOKIES_PER_CMD,	/* dma_attr_sgllen */
	1,			/* dma_attr_granular */
	0			/* dma_attr_flags */
};

static struct qlcnic_legacy_intr_set legacy_intr[] = QLCNIC_LEGACY_INTR_CONFIG;

static int
check_hw_init(struct qlcnic_adapter_s *adapter)
{
	u32 val;
	int ret = 0;

	adapter->qlcnic_hw_read_wx(adapter, QLCNIC_CAM_RAM(0x1fc), &val, 4);
	if (val == 0x55555555) {
		/* This is the first boot after power up */
		adapter->qlcnic_hw_read_wx(adapter, QLCNIC_ROMUSB_GLB_SW_RESET,
		    &val, 4);
		if (val != 0x80000f)
			ret = -1;
	}
	return (ret);
}


static int
qlcnic_p3_get_flash_mac_addr(struct qlcnic_adapter_s *adapter, uint64_t *mac)
{
	uint32_t mac_lo, mac_hi, crbaddr;
	int pci_func = adapter->ahw.pci_func;

	/* FOR P3, read from CAM RAM */
	crbaddr = CRB_MAC_BLOCK_START + (4 * ((pci_func/2) * 3)) +
	    (4 * (pci_func & 1));

	adapter->qlcnic_hw_read_wx(adapter, crbaddr, &mac_lo, 4);
	adapter->qlcnic_hw_read_wx(adapter, crbaddr + 4, &mac_hi, 4);

	if (pci_func & 1) {
		*mac = LE_TO_HOST_64((mac_lo >> 16) |
		    ((uint64_t)mac_hi << 16));
	} else {
		*mac = LE_TO_HOST_64((uint64_t)mac_lo |
		    ((uint64_t)mac_hi <<32));
	}
	return (0);
}

static int
qlcnic_initialize_dummy_dma(qlcnic_adapter *adapter)
{
	uint32_t hi, lo, temp;
	ddi_dma_cookie_t cookie;

	if (qlcnic_pci_alloc_consistent(adapter, QLCNIC_HOST_DUMMY_DMA_SIZE,
	    (caddr_t *)&adapter->dummy_dma.addr, &cookie,
	    &adapter->dummy_dma.dma_handle,
	    &adapter->dummy_dma.acc_handle) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: Unable to alloc dummy dma buf",
		    adapter->name, adapter->instance);
		return (DDI_ENOMEM);
	}

	adapter->dummy_dma.phys_addr = cookie.dmac_laddress;

	hi = (adapter->dummy_dma.phys_addr >> 32) & 0xffffffff;
	lo = adapter->dummy_dma.phys_addr & 0xffffffff;

	QLCNIC_READ_LOCK(&adapter->adapter_lock);
	adapter->qlcnic_hw_write_wx(adapter, CRB_HOST_DUMMY_BUF_ADDR_HI,
	    &hi, 4);
	adapter->qlcnic_hw_write_wx(adapter, CRB_HOST_DUMMY_BUF_ADDR_LO,
	    &lo, 4);
	if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id)) {
		temp = DUMMY_BUF_INIT;
		adapter->qlcnic_hw_write_wx(adapter, CRB_HOST_DUMMY_BUF,
		    &temp, 4);
	}
	QLCNIC_READ_UNLOCK(&adapter->adapter_lock);

	return (DDI_SUCCESS);
}

void
qlcnic_free_dummy_dma(qlcnic_adapter *adapter)
{
	if (adapter->dummy_dma.addr) {
		qlcnic_pci_free_consistent(&adapter->dummy_dma.dma_handle,
		    &adapter->dummy_dma.acc_handle);
		adapter->dummy_dma.addr = NULL;
	}
}

static int
qlcnic_pci_cfg_init(qlcnic_adapter *adapter)
{
	hardware_context *hwcontext;
	ddi_acc_handle_t pci_cfg_hdl;
	int *reg_options;
	dev_info_t *dip;
	uint_t noptions;
	int ret;
	uint16_t vendor_id, device_id, pci_cmd_word;
	uint8_t base_class, sub_class, prog_class;
	struct qlcnic_legacy_intr_set *legacy_intrp;

	hwcontext = &adapter->ahw;
	pci_cfg_hdl = adapter->pci_cfg_handle;
	dip = adapter->dip;

	vendor_id = pci_config_get16(pci_cfg_hdl, PCI_CONF_VENID);
	device_id = pci_config_get16(pci_cfg_hdl, PCI_CONF_DEVID);

	if ((device_id != 0x8020) || (vendor_id != 0x1077)) {
		cmn_err(CE_WARN, "%s%d: vendor id %x deviceid %x"
		    " not supported", adapter->name, adapter->instance,
		    vendor_id, device_id);
		return (DDI_FAILURE);
	}

	ret = ddi_prop_lookup_int_array(DDI_DEV_T_ANY,
	    dip, 0, "reg", &reg_options, &noptions);
	if (ret != DDI_PROP_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: Could not determine reg property",
		    adapter->name, adapter->instance);
		return (DDI_FAILURE);
	}

	hwcontext->pci_func = (reg_options[0] >> 8) & 0x7;
	ddi_prop_free(reg_options);

	base_class = pci_config_get8(pci_cfg_hdl, PCI_CONF_BASCLASS);
	sub_class = pci_config_get8(pci_cfg_hdl, PCI_CONF_SUBCLASS);
	prog_class = pci_config_get8(pci_cfg_hdl, PCI_CONF_PROGCLASS);

	/*
	 * Need this check so that MEZZ card mgmt interface qlcnic0 could fail
	 * attach & return and proceed to next interfaces qlcnic1 and qlcnic2
	 */
	if ((base_class != 0x02) || (sub_class != 0) || (prog_class != 0)) {
		cmn_err(CE_WARN, "%s%d: Base/sub/prog class problem %d/%d/%d",
		    adapter->name, adapter->instance, base_class, sub_class,
		    prog_class);
		return (DDI_FAILURE);
	}

	hwcontext->revision_id = pci_config_get8(pci_cfg_hdl, PCI_CONF_REVID);

	/*
	 * Refuse to work with dubious P3 cards.
	 */
	if ((hwcontext->revision_id >= QLCNIC_P3_A0) &&
	    (hwcontext->revision_id < QLCNIC_P3_B1)) {
		cmn_err(CE_WARN, "%s%d: QLogic chip revs between 0x%x-0x%x "
		    "is unsupported", adapter->name, adapter->instance,
		    QLCNIC_P3_A0, QLCNIC_P3_B0);
		return (DDI_FAILURE);
	}

	pci_cmd_word = pci_config_get16(pci_cfg_hdl, PCI_CONF_COMM);
	pci_cmd_word |= (PCI_COMM_INTX_DISABLE | PCI_COMM_SERR_ENABLE);
	pci_config_put16(pci_cfg_hdl, PCI_CONF_COMM, pci_cmd_word);

	if (hwcontext->revision_id >= QLCNIC_P3_B0)
		legacy_intrp = &legacy_intr[hwcontext->pci_func];
	else
		legacy_intrp = &legacy_intr[0];

	adapter->legacy_intr.int_vec_bit = legacy_intrp->int_vec_bit;
	adapter->legacy_intr.tgt_status_reg = legacy_intrp->tgt_status_reg;
	adapter->legacy_intr.tgt_mask_reg = legacy_intrp->tgt_mask_reg;
	adapter->legacy_intr.pci_int_reg = legacy_intrp->pci_int_reg;

	return (DDI_SUCCESS);
}

static void
qlcnic_free_tx_dmahdl(struct qlcnic_tx_ring_s *tx_ring)
{
	int i;
	qlcnic_dmah_node_t *nodep;
	qlcnic_adapter *adapter = tx_ring->adapter;
	uint32_t max_dma_hdls = adapter->max_tx_dma_hdls;

	mutex_enter(&tx_ring->tx_lock);
	nodep = &tx_ring->tx_dma_hdls[0];

	for (i = 0; i < max_dma_hdls; i++) {
		if (nodep->dmahdl != NULL) {
			ddi_dma_free_handle(&nodep->dmahdl);
			nodep->dmahdl = NULL;
		}
		nodep->next = NULL;
		nodep++;
	}

	tx_ring->dmah_head = 0;
	tx_ring->dmah_tail = 0;
	tx_ring->dmah_free = 0;
	mutex_exit(&tx_ring->tx_lock);
}

static int
qlcnic_alloc_tx_dmahdl(struct qlcnic_tx_ring_s *tx_ring)
{
	int i;
	qlcnic_adapter *adapter = tx_ring->adapter;
	qlcnic_dmah_node_t *nodep = &tx_ring->tx_dma_hdls[0];
	uint32_t max_dma_hdls = adapter->max_tx_dma_hdls;

	DPRINTF(DBG_INIT, (CE_NOTE, "qlcnic_alloc_tx_dmahdl%d entered to "
	    "allocate %d dma handles\n", adapter->instance, max_dma_hdls));
	/*
	 * Allocate dma handle pool and save to free list
	 */
	mutex_enter(&tx_ring->tx_lock);
	for (i = 0; i < max_dma_hdls; i++) {
		if (ddi_dma_alloc_handle(adapter->dip, &qlcnic_dma_attr_cmddesc,
		    DDI_DMA_DONTWAIT, NULL, &nodep->dmahdl) != DDI_SUCCESS) {
			nodep->dmahdl = NULL;
			mutex_exit(&tx_ring->tx_lock);
			goto alloc_hdl_fail;
		}
		tx_ring->dmah_free_list[i] = nodep;
		nodep++;
	}
	tx_ring->dmah_head = 0;
	tx_ring->dmah_tail = 0;
	tx_ring->dmah_free = i;
	mutex_exit(&tx_ring->tx_lock);

	return (DDI_SUCCESS);

alloc_hdl_fail:
	qlcnic_free_tx_dmahdl(tx_ring);
	cmn_err(CE_WARN, "%s%d: Failed transmit ring dma handle allocation",
	    adapter->name, adapter->instance);
	return (DDI_FAILURE);
}

static void
qlcnic_free_dma_mem(dma_area_t *dma_p)
{
	if (dma_p->dma_hdl != NULL) {
		if (dma_p->ncookies) {
			(void) ddi_dma_unbind_handle(dma_p->dma_hdl);
			dma_p->ncookies = 0;
		}
	}
	if (dma_p->acc_hdl != NULL) {
		ddi_dma_mem_free(&dma_p->acc_hdl);
		dma_p->acc_hdl = NULL;
	}
	if (dma_p->dma_hdl != NULL) {
		ddi_dma_free_handle(&dma_p->dma_hdl);
		dma_p->dma_hdl = NULL;
	}
}

static int
qlcnic_alloc_dma_mem(qlcnic_adapter *adapter, int size, uint_t dma_flag,
	ddi_dma_attr_t *dma_attr_p, dma_area_t *dma_p)
{
	int ret;
	caddr_t vaddr;
	size_t actual_size;
	ddi_dma_cookie_t cookie;

	ret = ddi_dma_alloc_handle(adapter->dip,
	    dma_attr_p, DDI_DMA_DONTWAIT,
	    NULL, &dma_p->dma_hdl);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: Failed ddi_dma_alloc_handle, ret %d",
		    adapter->name, adapter->instance, ret);
		dma_p->dma_hdl = NULL;
		goto dma_mem_fail;
	}

	ret = ddi_dma_mem_alloc(dma_p->dma_hdl,
	    size, &adapter->gc_attr_desc,
	    dma_flag & (DDI_DMA_STREAMING | DDI_DMA_CONSISTENT),
	    DDI_DMA_DONTWAIT, NULL, &vaddr, &actual_size,
	    &dma_p->acc_hdl);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: ddi_dma_mem_alloc() failed, size %d, "
		    "ret %d",
		    adapter->name, adapter->instance, size, ret);
		dma_p->acc_hdl = NULL;
		goto dma_mem_fail;
	}

	if (actual_size < size) {
		cmn_err(CE_WARN, "%s%d: ddi_dma_mem_alloc() allocated small",
		    adapter->name, adapter->instance);
		goto dma_mem_fail;
	}

	ret = ddi_dma_addr_bind_handle(dma_p->dma_hdl,
	    NULL, vaddr, size, dma_flag, DDI_DMA_DONTWAIT,
	    NULL, &cookie, &dma_p->ncookies);
	if (ret != DDI_DMA_MAPPED || dma_p->ncookies != 1) {
		cmn_err(CE_WARN, "%s%d: ddi_dma_addr_bind_handle() failed, "
		    "ret %d, ncookies %d", adapter->name, adapter->instance,
		    ret, dma_p->ncookies);
		goto dma_mem_fail;
	}

	dma_p->dma_addr = cookie.dmac_laddress;
	dma_p->vaddr = vaddr;
	(void) memset(vaddr, 0, size);

	return (DDI_SUCCESS);

dma_mem_fail:
	qlcnic_free_dma_mem(dma_p);
	return (DDI_FAILURE);
}

static void
qlcnic_free_tx_buffers(struct qlcnic_tx_ring_s *tx_ring)
{
	int i, j, k;
	dma_area_t *dma_p;
	struct qlcnic_cmd_buffer *cmd_buf;
	qlcnic_buf_node_t *tx_buf;
	qlcnic_adapter *adapter = tx_ring->adapter;
	uint32_t bufs_per_page;

	tx_buf = &tx_ring->tx_bufs[0];
	cmd_buf = &tx_ring->cmd_buf_arr[0];

	if (adapter->tx_buf_size > adapter->page_size) {
		bufs_per_page = 1;
	} else {
		bufs_per_page =
		    (adapter->page_size)/QLCNIC_CT_DEFAULT_RX_BUF_LEN;
	}
	/* clean cmd buffer */
	for (i = 0; i < adapter->MaxTxDescCount; i ++) {
		if (cmd_buf->msg != NULL)
			freemsg(cmd_buf->msg);
		cmd_buf->buf = NULL;
		cmd_buf->head = cmd_buf->tail = NULL;
		cmd_buf++;
	}
	/* clean all tx buffers */
	for (i = k = 0; i < adapter->MaxTxDescCount; i += bufs_per_page) {
		for (j = 0; j < bufs_per_page; j++) {
			if (!j) {
				dma_p = &tx_buf->dma_area;
				qlcnic_free_dma_mem(dma_p);
			}
			tx_ring->buf_free_list[k] = NULL;
			k++;
			tx_buf->next = NULL;
			tx_buf++;
		}
	}
	tx_ring->buf_free = 0;
	tx_ring->buf_head = 0;
	tx_ring->buf_tail = 0;
	tx_ring->freecmds = 0;
}

static int
qlcnic_alloc_tx_buffers(struct qlcnic_tx_ring_s *tx_ring)
{
	int i, j, k, ret, size, allocated = 0;
	dma_area_t *dma_p;
	qlcnic_adapter *adapter = tx_ring->adapter;
	qlcnic_buf_node_t *tx_buf = &tx_ring->tx_bufs[0];
	uint8_t *vaddr;
	u64 dma_addr;
	uint32_t bufs_per_page;
	ddi_dma_handle_t dma_hdl;

	if (adapter->tx_buf_size > adapter->page_size) {
		bufs_per_page = 1;
		size = adapter->tx_buf_size;
	} else {
		bufs_per_page =
		    (adapter->page_size)/QLCNIC_CT_DEFAULT_RX_BUF_LEN;
		size = adapter->page_size;
	}

	DPRINTF(DBG_INIT, (CE_NOTE, "qlcnic_alloc_tx_buffers%d entered to "
	    "allocate %d of size %d buffers", adapter->instance,
	    adapter->MaxTxDescCount, size));
	/* allocate all tx copy buffers */
	for (i = k = 0; i < adapter->MaxTxDescCount; i += bufs_per_page) {
		dma_p = &tx_buf->dma_area;
		ret = qlcnic_alloc_dma_mem(adapter, size,
		    DDI_DMA_WRITE | DDI_DMA_STREAMING,
		    &qlcnic_dma_attr_txbuf, dma_p);
		if (ret != DDI_SUCCESS) {
			allocated = k;
			goto alloc_tx_buffer_fail;
		}
		vaddr = dma_p->vaddr;
		dma_addr = dma_p->dma_addr;
		dma_hdl = dma_p->dma_hdl;
		for (j = 0; j < bufs_per_page; j++) {
			dma_p = &tx_buf->dma_area;
			dma_p->vaddr = vaddr;
			dma_p->dma_addr = dma_addr;
			dma_p->dma_hdl = dma_hdl;
			dma_p->offset = j * BUF_SIZE;
			tx_ring->buf_free_list[k] = tx_buf;
			k++;
			tx_buf++;
			vaddr += BUF_SIZE;
			dma_addr += BUF_SIZE;
		}
	}

	tx_ring->buf_free = adapter->MaxTxDescCount;
	tx_ring->buf_head = 0;
	tx_ring->buf_tail = 0;
	tx_ring->freecmds = adapter->MaxTxDescCount;
	return (DDI_SUCCESS);

alloc_tx_buffer_fail:
	cmn_err(CE_WARN, "%s%d: Failed tx buffer allocation, %d allocated",
	    adapter->name, adapter->instance, allocated);

	tx_buf = &tx_ring->tx_bufs[0];
	for (i = 0; i < allocated; i += bufs_per_page) {
		dma_p = &tx_buf->dma_area;
		qlcnic_free_dma_mem(dma_p);
		tx_buf += bufs_per_page;
	}
	return (DDI_FAILURE);
}

/*
 * Called by freemsg() to "free" the resource.
 */
static void
qlcnic_rx_buffer_recycle(char *arg)
{
	qlcnic_rx_buffer_t *rx_buffer = (qlcnic_rx_buffer_t *)(uintptr_t)arg;
	qlcnic_rcv_desc_ctx_t *rcv_desc = rx_buffer->rcv_desc;
	struct qlcnic_sds_ring_s *sds_ring = rx_buffer->sds_ring;
	uint32_t ref_cnt;

	if (rx_buffer->ref_cnt == 0) {
		/* only happens if rx buffer is being freed by m_stop */
		return;
	}

	atomic_dec_32(&rcv_desc->rx_buf_indicated);
	rx_buffer->mp = desballoc(rx_buffer->dma_info.vaddr,
	    rcv_desc->dma_size, 0, &rx_buffer->rx_recycle);

	ref_cnt = atomic_dec_32_nv(&rx_buffer->ref_cnt);
	/*
	 * The OS may hold some rx buffers when it unloaded the port,
	 * if those rx buffers are returned when the port is plumbed up
	 * later,we should free them.
	 */
	if (ref_cnt == 0) {
#ifdef LOAD_UNLOAD
		qlcnic_adapter *adapter = sds_ring->adapter;
		cmn_err(CE_NOTE,
		    "qlcnic(%d) previously held rx buffer returned to rcv_desc"
		    " %p, %d remaining\n",
		    adapter->instance, rcv_desc, rcv_desc->rx_buf_indicated);
#endif
		if (rx_buffer->mp != NULL) {
			freemsg(rx_buffer->mp);
			rx_buffer->mp = NULL;
		}
		if ((rcv_desc->quit_time) &&
		    (rcv_desc->rx_buf_indicated == 0)) {
#ifdef LOAD_UNLOAD
			cmn_err(CE_NOTE,
			    "qlcnic(%d) all previously held rx buffer returned,"
			    "free rcv_desc %p\n",
			    adapter->instance, rcv_desc);
#endif
			qlcnic_destroy_rx_ring(rcv_desc);
		}
	} else {
		/* if not old buffer, then put to free list */
		qlcnic_free_rx_buffer(sds_ring, rx_buffer);
	}
}


static void
qlcnic_destroy_rx_ring(qlcnic_rcv_desc_ctx_t *rcv_desc)
{
	uint32_t i, total_buf;
	qlcnic_rx_buffer_t *buf_pool, *first_rx_buf_in_page;
	uint32_t j, ref_cnt;
	uint32_t bufs_per_page;
	boolean_t free_rx_buffer;

	bufs_per_page = rcv_desc->bufs_per_page;

	buf_pool = rcv_desc->rx_buf_pool;
	if (buf_pool) {
		total_buf = rcv_desc->rx_buf_total;
		for (i = 0; i < total_buf; i += bufs_per_page) {
			/*
			 * by default, assume all rx buffers have been recycled
			 */
			free_rx_buffer = B_TRUE;
			for (j = 0; j < bufs_per_page; j++) {
				if (!j) {
					first_rx_buf_in_page = buf_pool;
				}
				/*
				 * rx buffer's ref_cnt value:
				 * 0 free-ed or being free-ed;
				 * 1 default (not used) or recycled;
				 * 2 those held by OS.
				 * We only free already recycled rx buffers
				 */
				ref_cnt = atomic_dec_32_nv(&buf_pool->ref_cnt);
				if (ref_cnt == 0) {
					/* this buffer is unused */
					if (buf_pool->mp != NULL) {
						freemsg(buf_pool->mp);
						buf_pool->mp = NULL;
					}
				} else {
					/*
					 * This rx buffer is being held by OS,
					 * can not free this whole page.
					 */
					free_rx_buffer = B_FALSE;
				}
				buf_pool++;
			}
			/*
			 * if no rx buffers in this page is being held by OS
			 */
			if (free_rx_buffer) {
				qlcnic_free_dma_mem(
				    &first_rx_buf_in_page->dma_info);
			}
		}
		/* if no rx buffer being held by upper layer */
		if (rcv_desc->rx_buf_indicated == 0) {
			kmem_free(rcv_desc->rx_buf_pool,
			    sizeof (qlcnic_rx_buffer_t) * total_buf);
			rcv_desc->rx_buf_pool = NULL;
			rcv_desc->pool_list = NULL;
			rcv_desc->rx_buf_free = 0;

			mutex_destroy(rcv_desc->pool_lock);
			kmem_free(rcv_desc, sizeof (qlcnic_rcv_desc_ctx_t));
			rcv_desc = NULL;
		}
	}
}

static void
qlcnic_destroy_all_rx_rings(qlcnic_adapter *adapter)
{
	uint32_t i, ring;
	qlcnic_rcv_desc_ctx_t *rcv_desc;
	struct qlcnic_recv_context_s *recv_ctx;

	for (i = 0; i < MAX_RCV_CTX; ++i) {
		recv_ctx = &adapter->recv_ctx[i];

		for (ring = 0; ring < adapter->max_rds_rings; ring++) {
			rcv_desc = recv_ctx->rcv_desc[ring];
			rcv_desc->quit_time = 1;

			if (rcv_desc->rx_buf_pool != NULL) {
				qlcnic_destroy_rx_ring(rcv_desc);
			}
		}
	}
	adapter->rx_ring_created = B_FALSE;
}

static int
qlcnic_create_rx_ring(qlcnic_adapter *adapter, qlcnic_rcv_desc_ctx_t *rcv_desc)
{
	int i, ret, allocate = 0, sreoff;
	uint32_t total_buf;
	dma_area_t *dma_info;
	qlcnic_rx_buffer_t *rx_buffer;
	uint8_t *vaddr;
	u64 dma_addr;
	uint32_t j;
	uint32_t bufs_per_page, buf_size;
	ddi_dma_handle_t dma_hdl;
	ddi_acc_handle_t acc_hdl;

	sreoff = adapter->ahw.cut_through ? 0 : IP_ALIGNMENT_BYTES;

	total_buf = rcv_desc->rx_buf_total = rcv_desc->MaxRxDescCount;

	DPRINTF(DBG_INIT, (CE_NOTE, "qlcnic_create_rx_ring%d entered to "
	    "allocate %d of size %d rx buffers\n", adapter->instance,
	    total_buf, rcv_desc->buf_size));

	rcv_desc->rx_buf_pool = kmem_zalloc(sizeof (qlcnic_rx_buffer_t) *
	    total_buf, KM_SLEEP);
	if (rcv_desc->rx_buf_pool == NULL) {
		cmn_err(CE_WARN,
		    "qlcnic%d: fail to create rx pool, rx-bufs %d, size %uld\n",
		    adapter->instance, total_buf,
		    total_buf * (uint32_t)sizeof (qlcnic_rx_buffer_t));
		return (1);
	}

	mutex_init(rcv_desc->pool_lock, NULL,
	    MUTEX_DRIVER, (DDI_INTR_PRI(adapter->intr_pri)));

	rx_buffer = rcv_desc->rx_buf_pool;
	if (rcv_desc->buf_size > adapter->page_size) {
		bufs_per_page = 1;
		buf_size = rcv_desc->buf_size;
	} else {
		bufs_per_page =
		    (adapter->page_size)/QLCNIC_CT_DEFAULT_RX_BUF_LEN;
		buf_size = adapter->page_size;
	}
	for (i = 0; i < total_buf; i += bufs_per_page) {
		dma_info = &rx_buffer->dma_info;
		ret = qlcnic_alloc_dma_mem(adapter, buf_size,
		    DDI_DMA_READ | DDI_DMA_STREAMING,
		    &qlcnic_dma_attr_rxbuf, dma_info);
		if (ret != DDI_SUCCESS)
			goto alloc_mem_failed;
		else {
			allocate++;
			vaddr = dma_info->vaddr;
			dma_addr = dma_info->dma_addr;
			dma_hdl = dma_info->dma_hdl;
			acc_hdl = dma_info->acc_hdl;
			for (j = 0; j < bufs_per_page; j++) {
				dma_info = &rx_buffer->dma_info;
				dma_info->vaddr = vaddr;
				dma_info->dma_addr = dma_addr;
				dma_info->dma_hdl = dma_hdl;
				dma_info->acc_hdl = acc_hdl;
				dma_info->offset = j * BUF_SIZE;

				dma_info->vaddr =
				    (void *) ((char *)dma_info->vaddr + sreoff);
				dma_info->dma_addr += sreoff;
				rx_buffer->rx_recycle.free_func =
				    qlcnic_rx_buffer_recycle;
				rx_buffer->rx_recycle.free_arg =
				    (caddr_t)rx_buffer;
				rx_buffer->mp = desballoc(dma_info->vaddr,
				    rcv_desc->dma_size, 0,
				    &rx_buffer->rx_recycle);
				if (rx_buffer->mp == NULL)
					adapter->stats.desballocfailed++;
				rx_buffer->rcv_desc = rcv_desc;
				rx_buffer->adapter = adapter;
				rx_buffer->rds_index = rcv_desc->rds_index;
				rx_buffer->ref_cnt = 1;
				rx_buffer++;
				vaddr += BUF_SIZE;
				dma_addr += BUF_SIZE;
			}
		}
	}

	rcv_desc->pool_list = rcv_desc->rx_buf_pool;

	rcv_desc->rx_buf_free = total_buf;
	rcv_desc->rx_buf_indicated = 0;
#ifdef LOAD_UNLOAD
	cmn_err(CE_NOTE,
	    "qlcnic(%d) create rds ring(%d) done. rcv_desc %p\n",
	    adapter->instance, rcv_desc->rds_index, rcv_desc);
#endif
	return (DDI_SUCCESS);

alloc_mem_failed:
	cmn_err(CE_WARN, "%s%d: Failed receive ring resource allocation, %d "
	    "allocated \n", adapter->name, adapter->instance, allocate);
	return (DDI_FAILURE);
}

static void
qlcnic_check_options(qlcnic_adapter *adapter)
{
	int tx_desc, rx_desc, rx_jdesc;
	dev_info_t *dip = adapter->dip;
	int lso_enable;
	int max_sds_rings, max_status_ring_size, max_tx_rings;
	int tx_recycle_threshold;
	int max_tx_dma_hdls;
	uint32_t ring_per_group;
	struct qlcnic_sds_ring_s *sds_ring;

	verbmsg = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    dmesg_propname, 0);

	adapter->tx_bcopy_threshold = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dip, DDI_PROP_DONTPASS, txbcopythreshold_propname,
	    QLCNIC_TX_BCOPY_THRESHOLD);
	adapter->rx_bcopy_threshold = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dip, DDI_PROP_DONTPASS, rxbcopythreshold_propname,
	    QLCNIC_RX_BCOPY_THRESHOLD);

	tx_desc = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    txringsize_propname, DEFAULT_CMD_DESCRIPTORS);
	if (tx_desc >= 256 && tx_desc <= MAX_CMD_DESCRIPTORS &&
	    !(tx_desc & (tx_desc - 1))) {
		adapter->MaxTxDescCount = tx_desc;
	} else {
		cmn_err(CE_WARN, "%s%d: TxRingSize defaulting to %d, since "
		    ".conf value is not 2 power aligned in range 256 - %d",
		    adapter->name, adapter->instance, DEFAULT_CMD_DESCRIPTORS,
		    MAX_CMD_DESCRIPTORS);
		adapter->MaxTxDescCount = DEFAULT_CMD_DESCRIPTORS;
	}
	DPRINTF(DBG_INIT, (CE_NOTE, "qlcnic(%d) MaxTxDescCount %d\n",
	    adapter->instance, adapter->MaxTxDescCount));

	max_tx_dma_hdls = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "max_tx_dma_hdls",
	    DEFAULT_CMD_DESCRIPTORS_DMA_HDLS);
	if ((max_tx_dma_hdls >= adapter->MaxTxDescCount &&
	    max_tx_dma_hdls <= MAX_CMD_DESCRIPTORS_DMA_HDLS) &&
	    !(tx_desc & (tx_desc - 1))) {
		adapter->max_tx_dma_hdls = max_tx_dma_hdls;
	} else {
		cmn_err(CE_WARN, "%s%d: max_tx_dma_hdls defaulting to %d",
		    adapter->name, adapter->instance,
		    DEFAULT_CMD_DESCRIPTORS_DMA_HDLS);
		adapter->max_tx_dma_hdls = DEFAULT_CMD_DESCRIPTORS_DMA_HDLS;
	}
	DPRINTF(DBG_INIT, (CE_NOTE, "qlcnic(%d) max_tx_dma_hdls %d\n",
	    adapter->instance, adapter->max_tx_dma_hdls));

	tx_recycle_threshold = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, tx_recycle_threshold_propname, 64);
	if ((tx_recycle_threshold >= 16) &&
	    (tx_recycle_threshold < adapter->MaxTxDescCount)) {
		adapter->tx_recycle_threshold = tx_recycle_threshold;
	} else {
		adapter->tx_recycle_threshold = 64;
	}

	rx_desc = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    rxringsize_propname, DEFAULT_RCV_DESCRIPTORS);
	if (rx_desc >= QLCNIC_MIN_DRIVER_RDS_SIZE &&
	    rx_desc <= QLCNIC_MAX_SUPPORTED_RDS_SIZE &&
	    !(rx_desc & (rx_desc - 1))) {
		adapter->MaxRxDescCount = rx_desc;
	} else {
		cmn_err(CE_WARN, "%s%d: RxRingSize defaulting to %d, since "
		    ".conf value is not 2 power aligned in range %d - %d",
		    adapter->name, adapter->instance, MAX_RCV_DESCRIPTORS,
		    QLCNIC_MIN_DRIVER_RDS_SIZE, QLCNIC_MAX_SUPPORTED_RDS_SIZE);
		adapter->MaxRxDescCount = MAX_RCV_DESCRIPTORS;
	}
	DPRINTF(DBG_INIT, (CE_NOTE, "qlcnic(%d) MaxRxDescCount %d\n",
	    adapter->instance, adapter->MaxRxDescCount));

	rx_jdesc = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    jumborxringsize_propname, DEFAULT_JUMBO_RCV_DESCRIPTORS);
	if (rx_jdesc >= QLCNIC_MIN_DRIVER_RDS_SIZE &&
	    rx_jdesc <= QLCNIC_MAX_SUPPORTED_JUMBO_RDS_SIZE &&
	    !(rx_jdesc & (rx_jdesc - 1))) {
		adapter->MaxJumboRxDescCount = rx_jdesc;
	} else {
		cmn_err(CE_WARN, "%s%d: JumboRingSize defaulting to %d, since "
		    ".conf value is not 2 power aligned in range %d - %d",
		    adapter->name, adapter->instance,
		    DEFAULT_JUMBO_RCV_DESCRIPTORS,
		    QLCNIC_MIN_DRIVER_RDS_SIZE,
		    QLCNIC_MAX_SUPPORTED_JUMBO_RDS_SIZE);
		adapter->MaxJumboRxDescCount = DEFAULT_JUMBO_RCV_DESCRIPTORS;
	}
	DPRINTF(DBG_INIT, (CE_NOTE, "qlcnic(%d) MaxJumboRxDescCount %d\n",
	    adapter->instance, adapter->MaxJumboRxDescCount));

	/*
	 * Solaris does not use LRO, but older firmware needs to have a
	 * couple of descriptors for initialization.
	 */
	adapter->MaxLroRxDescCount = (adapter->fw_major < 4) ? 2 : 0;

	adapter->mtu = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, defaultmtu_propname, ETHERMTU);

	if (adapter->mtu < ETHERMTU) {
		cmn_err(CE_WARN, "Raising mtu to %d", ETHERMTU);
		adapter->mtu = ETHERMTU;
	}

	if (adapter->mtu > P3_MAX_MTU) {
		cmn_err(CE_WARN, "Lowering mtu to %d", P3_MAX_MTU);
		adapter->mtu = P3_MAX_MTU;
	}

	lso_enable = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, lso_enable_propname, 1);
	if (lso_enable == 1) {
		adapter->flags |= QLCNIC_LSO_ENABLED;
	}
	max_sds_rings = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, max_sds_rings_propname, DEFAULT_SDS_RINGS);
	if (max_sds_rings > MAX_SDS_RINGS)
		max_sds_rings = MAX_SDS_RINGS;
	adapter->max_sds_rings = max_sds_rings;
	cmn_err(CE_NOTE, "!%s%d max receive rings set to %d",
	    adapter->name, adapter->instance, adapter->max_sds_rings);

	max_tx_rings = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "max_tx_rings", DEFAULT_TX_RINGS);
	if (max_tx_rings > MAX_TX_RINGS)
		max_tx_rings = MAX_TX_RINGS;
	/* tx rings must be no more than sds rings */
	if (max_tx_rings > max_sds_rings)
		max_tx_rings = max_sds_rings;
	adapter->max_tx_rings = max_tx_rings;
	cmn_err(CE_NOTE, "!%s%d max tx rings set to %d",
	    adapter->name, adapter->instance, adapter->max_tx_rings);

	max_status_ring_size = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, max_status_ring_size_propname,
	    DEFAULT_STATUS_RING_SIZE);
	if (max_status_ring_size > MAX_STATUS_RING_SIZE)
		max_status_ring_size = MAX_STATUS_RING_SIZE;
	adapter->MaxStatusDescCount = max_status_ring_size;
	cmn_err(CE_NOTE, "!%s%d max status rings size set to %d",
	    adapter->name, adapter->instance, adapter->MaxStatusDescCount);

	adapter->intr_coalesce_rx_time_us = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, intr_coalesce_rx_time_us_propname,
	    QLCNIC_DEFAULT_INTR_COALESCE_RX_TIME_US);

	adapter->intr_coalesce_rx_pkts = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, intr_coalesce_rx_pkts_propname,
	    QLCNIC_DEFAULT_INTR_COALESCE_RX_PACKETS);

	adapter->intr_coalesce_tx_time_us = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, intr_coalesce_tx_time_us_propname,
	    QLCNIC_DEFAULT_INTR_COALESCE_TX_TIME_US);

	adapter->intr_coalesce_tx_pkts = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, intr_coalesce_tx_pkts_propname,
	    QLCNIC_DEFAULT_INTR_COALESCE_TX_PACKETS);

	qlcnic_recv_context_t *recv_ctx;
	int i;

	adapter->num_rx_groups = 1;
	recv_ctx = &adapter->recv_ctx[0];

	ring_per_group = adapter->max_sds_rings / adapter->num_rx_groups;

	for (i = 0; i < adapter->num_rx_groups; i++) {
		qlcnic_rx_group_t *rx_group = &adapter->rx_groups[i];
		rx_group->index = i;
		rx_group->adapter = adapter;
	}
	for (i = 0; i < adapter->max_sds_rings; i++) {
		sds_ring = &recv_ctx->sds_ring[i];
		sds_ring->group_index = i / ring_per_group;
		sds_ring->adapter = adapter;
	}
	/*
	 * Set up the max number of unicast list
	 */
	adapter->unicst_total = MAX_UNICAST_LIST_SIZE;

}

static void
vector128M(qlcnic_adapter *aptr)
{
	aptr->qlcnic_pci_change_crbwindow = &qlcnic_pci_change_crbwindow_128M;
	aptr->qlcnic_crb_writelit_adapter = &qlcnic_crb_writelit_adapter_128M;
	aptr->qlcnic_hw_write_wx = &qlcnic_hw_write_wx_128M;
	aptr->qlcnic_hw_read_wx = &qlcnic_hw_read_wx_128M;
	aptr->qlcnic_hw_write_ioctl = &qlcnic_hw_write_ioctl_128M;
	aptr->qlcnic_hw_read_ioctl = &qlcnic_hw_read_ioctl_128M;
	aptr->qlcnic_pci_mem_write = &qlcnic_pci_mem_write_128M;
	aptr->qlcnic_pci_mem_read = &qlcnic_pci_mem_read_128M;
	aptr->qlcnic_pci_write_immediate = &qlcnic_pci_write_immediate_128M;
	aptr->qlcnic_pci_read_immediate = &qlcnic_pci_read_immediate_128M;
	aptr->qlcnic_pci_write_normalize = &qlcnic_pci_write_normalize_128M;
	aptr->qlcnic_pci_read_normalize = &qlcnic_pci_read_normalize_128M;
	aptr->qlcnic_pci_set_window = &qlcnic_pci_set_window_128M;
	aptr->qlcnic_clear_statistics = &qlcnic_clear_statistics_128M;
	aptr->qlcnic_fill_statistics = &qlcnic_fill_statistics_128M;
}

static void
vector2M(qlcnic_adapter *aptr)
{
	aptr->qlcnic_pci_change_crbwindow = &qlcnic_pci_change_crbwindow_2M;
	aptr->qlcnic_crb_writelit_adapter = &qlcnic_crb_writelit_adapter_2M;
	aptr->qlcnic_hw_write_wx = &qlcnic_hw_write_wx_2M;
	aptr->qlcnic_hw_read_wx = &qlcnic_hw_read_wx_2M;
	aptr->qlcnic_hw_write_ioctl = &qlcnic_hw_write_wx_2M;
	aptr->qlcnic_hw_read_ioctl = &qlcnic_hw_read_wx_2M;
	aptr->qlcnic_pci_mem_write = &qlcnic_pci_mem_write_2M;
	aptr->qlcnic_pci_mem_read = &qlcnic_pci_mem_read_2M;
	aptr->qlcnic_pci_write_immediate = &qlcnic_pci_write_immediate_2M;
	aptr->qlcnic_pci_read_immediate = &qlcnic_pci_read_immediate_2M;
	aptr->qlcnic_pci_write_normalize = &qlcnic_pci_write_normalize_2M;
	aptr->qlcnic_pci_read_normalize = &qlcnic_pci_read_normalize_2M;
	aptr->qlcnic_pci_set_window = &qlcnic_pci_set_window_2M;
	aptr->qlcnic_clear_statistics = &qlcnic_clear_statistics_2M;
	aptr->qlcnic_fill_statistics = &qlcnic_fill_statistics_2M;
	aptr->qlcnic_get_deviceinfo = &qlcnic_get_deviceinfo_2M;
}

static int
qlcnic_pci_map_setup(qlcnic_adapter *adapter)
{
	int ret;
	caddr_t reg_base;
	caddr_t mem_ptr0, mem_ptr1 = NULL, mem_ptr2 = NULL;
	unsigned long pci_len0;
	unsigned long first_page_group_start, first_page_group_end;
	off_t regsize;
	dev_info_t *dip = adapter->dip;
	int pci_func = adapter->ahw.pci_func;

	/* map register space */

	ret = ddi_dev_regsize(dip, 1, &regsize);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: failed to read reg size for bar0",
		    adapter->name, adapter->instance);
		return (DDI_FAILURE);
	}

	ret = ddi_regs_map_setup(dip, 1, &reg_base, 0,
	    regsize, &qlcnic_dev_attr, &adapter->regs_handle);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: failed to map registers, err %d",
		    adapter->name, adapter->instance, ret);
		return (DDI_FAILURE);
	}

	mem_ptr0 = reg_base;

	if (regsize == QLCNIC_PCI_128MB_SIZE) {
		pci_len0 = FIRST_PAGE_GROUP_SIZE;
		mem_ptr1 = mem_ptr0 + SECOND_PAGE_GROUP_START;
		mem_ptr2 = mem_ptr0 + THIRD_PAGE_GROUP_START;
		first_page_group_start = FIRST_PAGE_GROUP_START;
		first_page_group_end   = FIRST_PAGE_GROUP_END;
		vector128M(adapter);
	} else if (regsize == QLCNIC_PCI_32MB_SIZE) {
		pci_len0 = 0;
		mem_ptr1 = mem_ptr0;
		mem_ptr2 = mem_ptr0 +
		    (THIRD_PAGE_GROUP_START - SECOND_PAGE_GROUP_START);
		first_page_group_start = 0;
		first_page_group_end   = 0;
		vector128M(adapter);
	} else if (regsize == QLCNIC_PCI_2MB_SIZE) {
		pci_len0 = QLCNIC_PCI_2MB_SIZE;
		first_page_group_start = 0;
		first_page_group_end = 0;
		vector2M(adapter);
	} else {
		cmn_err(CE_WARN, "%s%d: invalid pci regs map size %ld",
		    adapter->name, adapter->instance, regsize);
		ddi_regs_map_free(&adapter->regs_handle);
		return (DDI_FAILURE);
	}

	adapter->ahw.pci_base0  = (unsigned long)mem_ptr0;
	adapter->ahw.pci_len0   = pci_len0;
	adapter->ahw.pci_base1  = (unsigned long)mem_ptr1;
	adapter->ahw.pci_len1   = SECOND_PAGE_GROUP_SIZE;
	adapter->ahw.pci_base2  = (unsigned long)mem_ptr2;
	adapter->ahw.pci_len2   = THIRD_PAGE_GROUP_SIZE;
	adapter->ahw.crb_base   =
	    PCI_OFFSET_SECOND_RANGE(adapter, QLCNIC_PCI_CRBSPACE);

	adapter->ahw.first_page_group_start = first_page_group_start;
	adapter->ahw.first_page_group_end   = first_page_group_end;

	if (QLCNIC_IS_REVISION_P3PLUS(adapter->ahw.revision_id)) {
		adapter->ahw.ocm_win_crb = qlcnic_get_ioaddr(adapter,
		    QLCNIC_PCIX_PS_REG(PCIX_OCM_WINDOW_REG(pci_func)));
	} else if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id)) {
		adapter->ahw.ocm_win_crb = qlcnic_get_ioaddr(adapter,
		    QLCNIC_PCIX_PS_REG(PCIE_MN_WINDOW_REG(pci_func)));
	}

	return (DDI_SUCCESS);
}


static int
qlcnic_initialize_intr(qlcnic_adapter *adapter)
{
	int ret;
	int type, count, avail, actual;
	int intr_requested;
	int i;

	ret = ddi_intr_get_supported_types(adapter->dip, &type);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: ddi_intr_get_supported_types() "
		    "failed", adapter->name, adapter->instance);
		return (DDI_FAILURE);
	}

	if (adapter->max_sds_rings > 1) {
		if (type & DDI_INTR_TYPE_MSIX) {
			type = DDI_INTR_TYPE_MSIX;
			ret = ddi_intr_get_nintrs(adapter->dip, type, &count);
			if ((ret == DDI_SUCCESS) && (count > 1)) {
				goto found_msix;
			}
		}
	}

	type = DDI_INTR_TYPE_MSI;
	ret = ddi_intr_get_nintrs(adapter->dip, type, &count);
	if ((ret == DDI_SUCCESS) && (count > 0))
		goto found_msi;

	type = DDI_INTR_TYPE_FIXED;
	ret = ddi_intr_get_nintrs(adapter->dip, type, &count);
	if ((ret != DDI_SUCCESS) || (count == 0)) {
		cmn_err(CE_WARN,
		    "ddi_intr_get_nintrs() failure ret=%d", ret);
		return (DDI_FAILURE);
	}
	intr_requested = 1;

found_msix:
found_msi:
	adapter->intr_type = type;
	adapter->flags &= ~(QLCNIC_MSI_ENABLED | QLCNIC_MSIX_ENABLED);
	if (type == DDI_INTR_TYPE_MSI) {
		adapter->flags |= QLCNIC_MSI_ENABLED;
		intr_requested = 1;
	}

	if (type == DDI_INTR_TYPE_MSIX) {
		adapter->flags |= QLCNIC_MSIX_ENABLED;
		intr_requested = adapter->max_sds_rings;
	}

	/* Get number of available interrupts */
	ret = ddi_intr_get_navail(adapter->dip, type, &avail);
	if ((ret != DDI_SUCCESS) || (avail == 0)) {
		cmn_err(CE_WARN, "ddi_intr_get_navail() failure, ret=%d",
		    ret);
		return (DDI_FAILURE);
	}
	DPRINTF(DBG_INIT, (CE_NOTE,
	    "!%s(%d): Number of Intr requested %d, available %d\n",
	    adapter->name, adapter->instance, intr_requested, avail));

	if (intr_requested > avail)
		intr_requested = avail;

	ret = ddi_intr_alloc(adapter->dip, &adapter->intr_handle[0],
	    type, 0, intr_requested, &actual, DDI_INTR_ALLOC_NORMAL);
	if ((ret != DDI_SUCCESS) || (actual == 0)) {
		cmn_err(CE_WARN, "ddi_intr_alloc() failure: %d", ret);
		return (DDI_FAILURE);
	}
	adapter->intr_count = actual;

	if (adapter->max_sds_rings > actual) {
		adapter->max_sds_rings = actual;
	}
	if (adapter->max_tx_rings > actual) {
		adapter->max_tx_rings = actual;
	}

	cmn_err(CE_NOTE, "!qlcnic(%d) actual interrupts %d, "
	    "max_sds_rings %d, max_tx_rings %d \n",
	    adapter->instance, adapter->intr_count,
	    adapter->max_sds_rings, adapter->max_tx_rings);

	/* Get priority of the first vector, assume rest are the same */
	ret = ddi_intr_get_pri(adapter->intr_handle[0], &adapter->intr_pri);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "ddi_intr_get_pri() failure: %d", ret);
		return (DDI_FAILURE);
	}

	ret = ddi_intr_get_cap(adapter->intr_handle[0], &adapter->intr_cap);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "ddi_intr_get_cap() failure: %d", ret);
		return (DDI_FAILURE);
	}

	/* Call ddi_intr_add_handler() */
	/* For Legacy and MSI use single interrupt handler */
	if (adapter->intr_type != DDI_INTR_TYPE_MSIX) {
		ret = ddi_intr_add_handler(adapter->intr_handle[0], qlcnic_intr,
		    (caddr_t)adapter, NULL);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN, "%s%d: ddi_intr_add_handler() failure",
			    adapter->name, adapter->instance);
			(void) ddi_intr_free(adapter->intr_handle[0]);
			return (DDI_FAILURE);
		}
	} else {
		/*
		 * For now we are assuming additional interrupts are for RSS
		 */
		for (i = 0; i < adapter->intr_count; i++) {
				ret = ddi_intr_add_handler(
				    adapter->intr_handle[i],
				    qlcnic_msix_rx_intr,
				    (caddr_t)&adapter->recv_ctx[0].sds_ring[i],
				    NULL);
			if (ret != DDI_SUCCESS) {
				cmn_err(CE_WARN,
				    "%s%d: ddi_intr_add_handler() fail",
				    adapter->name, adapter->instance);
				(void) ddi_intr_free(
				    adapter->intr_handle[i]);
				return (DDI_FAILURE);
			}
		}
	}
	/* Enable interrupts */
	/* Block enable */
	if (adapter->intr_cap & DDI_INTR_FLAG_BLOCK) {
		(void) ddi_intr_block_enable(&adapter->intr_handle[0],
		    adapter->intr_count);
	} else {
		for (i = 0; i < adapter->intr_count; i++)
			(void) ddi_intr_enable(adapter->intr_handle[i]);
	}

	return (DDI_SUCCESS);

}

void
qlcnic_destroy_intr(qlcnic_adapter *adapter)
{
	int i;

	/* disable interrupt */
	if (adapter->intr_cap & DDI_INTR_FLAG_BLOCK)
		(void) ddi_intr_block_disable(&adapter->intr_handle[0],
		    adapter->max_sds_rings);
	else {
		for (i = 0; i < adapter->max_sds_rings; i++) {
			(void) ddi_intr_disable(adapter->intr_handle[i]);
		}
	}

	for (i = 0; i < adapter->max_sds_rings; i++) {
		(void) ddi_intr_remove_handler(adapter->intr_handle[i]);
		(void) ddi_intr_free(adapter->intr_handle[i]);
	}
	/* Remove the software intr handler */
}

static void
qlcnic_set_port_mode(qlcnic_adapter *adapter)
{
	static int wol_port_mode = QLCNIC_PORT_MODE_AUTO_NEG_1G;
	static int port_mode = QLCNIC_PORT_MODE_AUTO_NEG;
	int btype = adapter->ahw.boardcfg.board_type, data = 0;

	if (btype == QLCNIC_BRDTYPE_P3_HMEZ ||
	    btype == QLCNIC_BRDTYPE_P3_XG_LOM) {
		data = port_mode;	/* set to port_mode normally */
		if ((port_mode != QLCNIC_PORT_MODE_802_3_AP) &&
		    (port_mode != QLCNIC_PORT_MODE_XG) &&
		    (port_mode != QLCNIC_PORT_MODE_AUTO_NEG_1G) &&
		    (port_mode != QLCNIC_PORT_MODE_AUTO_NEG_XG))
			data = QLCNIC_PORT_MODE_AUTO_NEG;

		adapter->qlcnic_hw_write_wx(adapter, QLCNIC_PORT_MODE_ADDR,
		    &data, 4);

		if ((wol_port_mode != QLCNIC_PORT_MODE_802_3_AP) &&
		    (wol_port_mode != QLCNIC_PORT_MODE_XG) &&
		    (wol_port_mode != QLCNIC_PORT_MODE_AUTO_NEG_1G) &&
		    (wol_port_mode != QLCNIC_PORT_MODE_AUTO_NEG_XG))
			wol_port_mode = QLCNIC_PORT_MODE_AUTO_NEG;

		adapter->qlcnic_hw_write_wx(adapter, QLCNIC_WOL_PORT_MODE,
		    &wol_port_mode, 4);
	}
}

static void
qlcnic_pcie_strap_init(qlcnic_adapter *adapter)
{
	ddi_acc_handle_t pcihdl = adapter->pci_cfg_handle;
	u32 chicken, control, c8c9value = 0xF1000;

	adapter->qlcnic_hw_read_wx(adapter, QLCNIC_PCIE_REG(PCIE_CHICKEN3),
	    &chicken, 4);

	chicken &= 0xFCFFFFFF;		/* clear chicken3 25:24 */
	control = pci_config_get32(pcihdl, 0xD0);
	if ((control & 0x000F0000) != 0x00020000)	/* is it gen1? */
		chicken |= 0x01000000;
	adapter->qlcnic_hw_write_wx(adapter, QLCNIC_PCIE_REG(PCIE_CHICKEN3),
	    &chicken, 4);
	control = pci_config_get32(pcihdl, 0xC8);
	control = pci_config_get32(pcihdl, 0xC8);
	pci_config_put32(pcihdl, 0xC8, c8c9value);
}

static int
qlcnic_read_mac_addr(qlcnic_adapter *adapter)
{
	uint64_t mac_addr;
	uint8_t *p;
	int i;

	if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id)) {
		if (qlcnic_p3_get_flash_mac_addr(adapter, &mac_addr) != 0)
			return (-1);
	}

	p = (uint8_t *)&mac_addr;

	for (i = 0; i < 6; i++) {
		adapter->mac_addr[i] = p[5 - i];
	}

	if (qlcnic_macaddr_set(adapter, adapter->mac_addr) != 0)
		return (-1);

	return (0);
}

static void
qlcnic_init_coalesce_defaults(qlcnic_adapter *adapter)
{
	adapter->coal.flags = 0;
	adapter->coal.normal.data.rx_time_us =
	    adapter->intr_coalesce_rx_time_us;
	adapter->coal.normal.data.rx_packets =
	    adapter->intr_coalesce_rx_pkts;
	adapter->coal.normal.data.tx_time_us =
	    adapter->intr_coalesce_tx_time_us;
	adapter->coal.normal.data.tx_packets =
	    adapter->intr_coalesce_tx_pkts;
}
/*
 * The IO fault service error handling callback function
 */
/* ARGSUSED */
static int
qlcnic_fm_err_cb(dev_info_t *dip, ddi_fm_error_t *err,
    const void *impl_data)
{
	pci_ereport_post(dip, err, NULL);
	return (err->fme_status);
}
static void
qlcnic_fm_init(qlcnic_adapter *adapter)
{
	ddi_iblock_cookie_t iblk;

	/*
	 * If adapter has any FMA cap then only register with
	 * IO Fault Mgmt Framework.
	 */
	if (adapter->fm_cap) {
		qlcnic_dev_attr.devacc_attr_access = DDI_FLAGERR_ACC;
		qlcnic_dma_attr_desc.dma_attr_flags = DDI_DMA_FLAGERR;

		/* Register capabilities with IO Fault Services */
		ddi_fm_init(adapter->dip, &adapter->fm_cap, &iblk);

		/*
		 * Initialize pci ereport capabilities if ereport capable
		 */
		if (DDI_FM_EREPORT_CAP(adapter->fm_cap) ||
		    DDI_FM_ERRCB_CAP(adapter->fm_cap)) {
			pci_ereport_setup(adapter->dip);
		}
		/*
		 * Register error callback if error callback capable
		 */
		if (DDI_FM_ERRCB_CAP(adapter->fm_cap)) {
			ddi_fm_handler_register(adapter->dip,
			    qlcnic_fm_err_cb, (void *)adapter);
		}
	} else {
		/*
		 * These fields have to be cleared of FMA if there are no
		 * FMA capabilities at runtime.
		 */
		qlcnic_dev_attr.devacc_attr_access = DDI_DEFAULT_ACC;
		qlcnic_dma_attr_desc.dma_attr_flags = 0;
	}
}

static void
qlcnic_fm_fini(qlcnic_adapter *adapter)
{
	if (adapter->fm_cap) {
		/*
		 * Release any resources allocated by pci_ereport_setup()
		 */
		if (DDI_FM_EREPORT_CAP(adapter->fm_cap) ||
		    DDI_FM_ERRCB_CAP(adapter->fm_cap)) {
			pci_ereport_teardown(adapter->dip);
		}
		/*
		 * Un-register error callback if error callback capable
		 */
		if (DDI_FM_ERRCB_CAP(adapter->fm_cap)) {
			ddi_fm_handler_unregister(adapter->dip);
		}
		/* Unregister from IO Fault Services */
		ddi_fm_fini(adapter->dip);
	}
}

static int
qlcnic_suspend(qlcnic_adapter *adapter)
{
	cmn_err(CE_NOTE, "!qlcnic_suspend(%d) entered \n", adapter->instance);
	mutex_enter(&adapter->lock);
	/* Stop the watchdog timer */
	qlcnic_remove(adapter, 0x1);
	adapter->drv_state = QLCNIC_DRV_SUSPEND;

	mutex_exit(&adapter->lock);

	return (DDI_SUCCESS);
}

static int
qlcnic_resume(qlcnic_adapter *adapter)
{
	int fw_flag;

	cmn_err(CE_NOTE, "!qlcnic_resume(%d) entered\n", adapter->instance);

	mutex_enter(&adapter->lock);

	if ((adapter->is_up == QLCNIC_ADAPTER_UP_MAGIC)) {
		if (QLCNIC_IS_REVISION_P3PLUS(adapter->ahw.revision_id)) {
			adapter->drv_state = QLCNIC_DRV_OPERATIONAL;
		}
		mutex_exit(&adapter->lock);

		DPRINTF(DBG_INIT, (CE_NOTE, "%s: %s%d: adapter instance "
		    "already up. exiting\n",
		    __func__, adapter->name, adapter->instance));
		return (DDI_SUCCESS);
	}

	if (QLCNIC_IS_REVISION_P3PLUS(adapter->ahw.revision_id))
		fw_flag = 1;
	else
		fw_flag = 0;
	if (qlcnic_driver_start(adapter, fw_flag, 0) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: Failed to start driver",
		    adapter->name, adapter->instance);
		mutex_exit(&adapter->lock);
		ddi_fm_service_impact(adapter->dip,
		    DDI_SERVICE_LOST);
		return (DDI_FAILURE);
	}
	mutex_exit(&adapter->lock);

	if (qlcnic_check_acc_handle(adapter,
	    adapter->regs_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(adapter->dip,
		    DDI_SERVICE_LOST);
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);

}
static int
qlcnicattach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	qlcnic_adapter *adapter;
	int ret = DDI_FAILURE;

	switch (cmd) {
	case DDI_ATTACH:
		break;
	case DDI_RESUME:
		adapter = (qlcnic_adapter *)ddi_get_driver_private(dip);
		if (adapter == NULL) {
			return (DDI_FAILURE);
		}
		return (qlcnic_resume(adapter));
	default:
		return (DDI_FAILURE);
	}
	cmn_err(CE_NOTE, "!qlcnic attach entered\n");

	adapter = kmem_zalloc(sizeof (qlcnic_adapter), KM_SLEEP);
	adapter->dip = dip;
	ddi_set_driver_private(dip, adapter);
	adapter->instance = ddi_get_instance(dip);

	adapter->name = ddi_driver_name(dip);

	/* Get page size */
	adapter->page_size = ddi_ptob(adapter->dip, 1L);

	/* Intialize FMA support */
	adapter->fm_cap = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dip, DDI_PROP_DONTPASS, fm_cap_propname,
	    DDI_FM_EREPORT_CAPABLE | DDI_FM_ACCCHK_CAPABLE |
	    DDI_FM_DMACHK_CAPABLE | DDI_FM_ERRCB_CAPABLE);

	qlcnic_fm_init(adapter);

	ret = pci_config_setup(dip, &adapter->pci_cfg_handle);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: pci_config_setup failed",
		    adapter->name, adapter->instance);
		goto attach_setup_err;
	}

	ret = qlcnic_pci_cfg_init(adapter);
	if (ret != DDI_SUCCESS)
		goto attach_err;
	if (qlcnic_check_acc_handle(adapter,
	    adapter->pci_cfg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(adapter->dip,
		    DDI_SERVICE_LOST);
		goto attach_err;
	}

	ret = qlcnic_pci_map_setup(adapter);
	if (ret != DDI_SUCCESS)
		goto attach_err;
	if (qlcnic_check_acc_handle(adapter,
	    adapter->regs_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(adapter->dip,
		    DDI_SERVICE_LOST);
		goto attach_unmap_regs;
	}

	qlcnic_check_options(adapter);

	qlcnic_init_coalesce_defaults(adapter);

	if (qlcnic_initialize_intr(adapter) != DDI_SUCCESS) {
		goto attach_unmap_regs;
	}

	rw_init(&adapter->adapter_lock, NULL,
	    RW_DRIVER, DDI_INTR_PRI(adapter->intr_pri));
	mutex_init(&adapter->lock, NULL,
	    MUTEX_DRIVER, (DDI_INTR_PRI(adapter->intr_pri)));

	adapter->portnum = (int8_t)adapter->ahw.pci_func;

	adapter->remove_entered = 0;

	/*
	 * Set the CRB window to invalid. If any register in window 0 is
	 * accessed it should set window to 0 and then reset it to 1.
	 */
	adapter->ahw.crb_win = 255;
	adapter->ahw.ocm_win = 255;

	adapter->fw_major = adapter->qlcnic_pci_read_normalize(adapter,
	    QLCNIC_FW_VERSION_MAJOR);
	adapter->fw_minor = adapter->qlcnic_pci_read_normalize(adapter,
	    QLCNIC_FW_VERSION_MINOR);
	adapter->fw_sub = adapter->qlcnic_pci_read_normalize(adapter,
	    QLCNIC_FW_VERSION_SUB);

	cmn_err(CE_NOTE, "!qlcnic(%d) fw version : %d.%d.%d \n",
	    adapter->instance, adapter->fw_major,
	    adapter->fw_minor, adapter->fw_sub);

	if (adapter->fw_major < 4)
		adapter->max_rds_rings = 3;
	else
		adapter->max_rds_rings = 2;

	STRUCT_COPY(adapter->gc_dma_attr_desc, qlcnic_dma_attr_desc);
	STRUCT_COPY(adapter->gc_attr_desc, qlcnic_buf_attr);

	ret = qlcnic_get_board_info(adapter);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: error reading board config",
		    adapter->name, adapter->instance);
		goto attach_destroy_intr;
	}

	/* Fix for low performance on Sparc T5240 */
	adapter->ahw.cut_through = 0;

	if (qlcnic_read_mac_addr(adapter))
		cmn_err(CE_WARN, "%s%d: Failed to read MAC addr",
		    adapter->name, adapter->instance);

	ret = qlcnic_register_mac(adapter);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_NOTE, "%s%d: Mac registration error",
		    adapter->name, adapter->instance);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);

attach_destroy_intr:
	if (QLCNIC_IS_REVISION_P3PLUS(adapter->ahw.revision_id))
		(void) qlcnic_p3p_clear_dev_state(adapter, 0x1);
	qlcnic_destroy_intr(adapter);
attach_unmap_regs:
	ddi_regs_map_free(&(adapter->regs_handle));
attach_err:
	pci_config_teardown(&adapter->pci_cfg_handle);
attach_setup_err:
	/* Fix for crash in detach when attach failed with error SJP */
	ddi_set_driver_private(dip, NULL);
	kmem_free(adapter, sizeof (qlcnic_adapter));
	return (ret);
}

static int
qlcnicdetach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	qlcnic_adapter *adapter = (qlcnic_adapter *)ddi_get_driver_private(dip);

	if (adapter == NULL)
		return (DDI_FAILURE);
	cmn_err(CE_NOTE, "!qlcnic detach(%d) entered\n", adapter->instance);

	switch (cmd) {
	case DDI_DETACH:
		mutex_enter(&adapter->lock);
		qlcnic_fini_kstats(adapter);
		adapter->kstats[0] = NULL;

		adapter->drv_state = QLCNIC_DRV_DETACH;

		if (adapter->pci_cfg_handle != NULL)
			pci_config_teardown(&adapter->pci_cfg_handle);

		mutex_exit(&adapter->lock);
		if (adapter->watchdog_running) {
			(void) untimeout(adapter->watchdog_timer);
			adapter->watchdog_running = 0;
			cmn_err(CE_NOTE, "qlcnic_detach(%d)watchdog"
			    " timer stopped \n", adapter->instance);
		}
		if (adapter->mach) {
			(void) mac_unregister(adapter->mach);
		}
		qlcnic_destroy_intr(adapter);
		/* Clean up FM resources */
		qlcnic_fm_fini(adapter);

		ddi_set_driver_private(adapter->dip, NULL);
		cleanup_adapter(adapter);

		return (DDI_SUCCESS);

	case DDI_SUSPEND:
		return (qlcnic_suspend(adapter));

	default:
		break;
	}

	return (DDI_FAILURE);
}

static void
qlcnic_destroy_all_tx_rings(qlcnic_adapter *adapter)
{
	struct qlcnic_tx_ring_s *tx_ring;
	uint32_t rings;

	for (rings = 0; rings < adapter->max_tx_rings; rings++) {
		tx_ring = &(adapter->tx_ring[rings]);

		if (tx_ring->cmd_buf_arr != NULL) {
			qlcnic_free_tx_buffers(tx_ring);
			qlcnic_free_tx_dmahdl(tx_ring);
			kmem_free(tx_ring->cmd_buf_arr,
			    sizeof (struct qlcnic_cmd_buffer) *
			    adapter->MaxTxDescCount);
			tx_ring->cmd_buf_arr = NULL;
			mutex_destroy(&tx_ring->tx_lock);
			mutex_destroy(&tx_ring->buf_head_lock);
			mutex_destroy(&tx_ring->buf_tail_lock);
			mutex_destroy(&tx_ring->dmah_head_lock);
			mutex_destroy(&tx_ring->dmah_tail_lock);
		}
	}
	adapter->tx_ring_created = B_FALSE;
}

int
qlcnic_create_rxtx_rings(qlcnic_adapter *adapter)
{
	qlcnic_recv_context_t *recv_ctx;
	qlcnic_rcv_desc_ctx_t *rcv_desc;
	int i, ring;
	struct qlcnic_tx_ring_s *tx_ring;

	bzero(&adapter->tx_ring[0], sizeof (*tx_ring) * adapter->max_tx_rings);
	bzero(&adapter->recv_ctx[0], sizeof (*recv_ctx) * MAX_RCV_CTX);

	/* set tx flags */
	adapter->tx_buf_size = adapter->mtu + QLCNIC_MAX_ETHHDR;
	/* set rx flags */
	for (i = 0; i < MAX_RCV_CTX; ++i) {
		recv_ctx = &adapter->recv_ctx[i];

		for (ring = 0; ring < adapter->max_rds_rings; ring++) {
			rcv_desc = kmem_zalloc(sizeof (qlcnic_rcv_desc_ctx_t),
			    KM_SLEEP);

			if (!rcv_desc)
				goto free_rcv_desc;

			recv_ctx->rcv_desc[ring] = rcv_desc;

			switch (RCV_DESC_TYPE(ring)) {
			case RCV_DESC_NORMAL:
				rcv_desc->MaxRxDescCount =
				    adapter->MaxRxDescCount;
				if (adapter->ahw.cut_through) {
					rcv_desc->dma_size =
					    QLCNIC_CT_DEFAULT_RX_BUF_LEN;
					rcv_desc->buf_size = rcv_desc->dma_size;
				} else {
					rcv_desc->dma_size =
					    QLCNIC_RX_NORMAL_BUF_MAX_LEN;
					rcv_desc->buf_size =
					    rcv_desc->dma_size +
					    IP_ALIGNMENT_BYTES;
				}
				break;

			case RCV_DESC_JUMBO:
				rcv_desc->MaxRxDescCount =
				    adapter->MaxJumboRxDescCount;
				if (adapter->ahw.cut_through) {
					rcv_desc->dma_size =
					    rcv_desc->buf_size =
					    QLCNIC_P3_RX_JUMBO_BUF_MAX_LEN;
				} else {
					rcv_desc->dma_size =
					    QLCNIC_P3_RX_JUMBO_BUF_MAX_LEN;
					rcv_desc->buf_size =
					    rcv_desc->dma_size +
					    IP_ALIGNMENT_BYTES;
				}
				break;

			case RCV_RING_LRO:
				rcv_desc->MaxRxDescCount =
				    adapter->MaxLroRxDescCount;
				rcv_desc->buf_size = MAX_RX_LRO_BUFFER_LENGTH;
				rcv_desc->dma_size = RX_LRO_DMA_MAP_LEN;
				break;
			default:
				cmn_err(CE_WARN, "%s%d Invalid ring index %d",
				    adapter->name, adapter->instance, ring);
				break;
			}
			if (rcv_desc->buf_size > adapter->page_size) {
				rcv_desc->bufs_per_page = 1;
			} else {
				rcv_desc->bufs_per_page = adapter->page_size /
				    QLCNIC_CT_DEFAULT_RX_BUF_LEN;
			}
		}
	}
	/* Receive rings */
	for (i = 0; i < MAX_RCV_CTX; ++i) {
		recv_ctx = &adapter->recv_ctx[i];
		/* allocate buffers for default and jumbo rx rings */
		for (ring = 0; ring < adapter->max_rds_rings; ring++) {
			rcv_desc = recv_ctx->rcv_desc[ring];
			rcv_desc->quit_time = 0;
			rcv_desc->rds_index = ring;

			if (qlcnic_create_rx_ring(adapter, rcv_desc) !=
			    DDI_SUCCESS)
				goto free_rx_rings;
		}
	}
	adapter->rx_ring_created = B_TRUE;
	/* Transmission rings */
	for (i = 0; i < adapter->max_tx_rings; i++) {
		tx_ring = &(adapter->tx_ring[i]);
		tx_ring->adapter = adapter;

		mutex_init(&tx_ring->tx_lock, NULL,
		    MUTEX_DRIVER, (DDI_INTR_PRI(adapter->intr_pri)));
		mutex_init(&tx_ring->buf_head_lock, NULL,
		    MUTEX_DRIVER, (DDI_INTR_PRI(adapter->intr_pri)));
		mutex_init(&tx_ring->buf_tail_lock, NULL,
		    MUTEX_DRIVER, (DDI_INTR_PRI(adapter->intr_pri)));
		mutex_init(&tx_ring->dmah_head_lock, NULL,
		    MUTEX_DRIVER, (DDI_INTR_PRI(adapter->intr_pri)));
		mutex_init(&tx_ring->dmah_tail_lock, NULL,
		    MUTEX_DRIVER, (DDI_INTR_PRI(adapter->intr_pri)));

		tx_ring->cmd_buf_arr = (struct qlcnic_cmd_buffer *)kmem_zalloc(
		    sizeof (struct qlcnic_cmd_buffer) * adapter->MaxTxDescCount,
		    KM_SLEEP);
		if (tx_ring->cmd_buf_arr == NULL) {
			cmn_err(CE_WARN, "qlcnic(%d) alloc cmd_buf_arr failed",
			    adapter->instance);
			goto free_tx_rings;
		}

		if (qlcnic_alloc_tx_dmahdl(tx_ring) != DDI_SUCCESS) {
			goto free_tx_rings;
		}

		if (qlcnic_alloc_tx_buffers(tx_ring) != DDI_SUCCESS) {
			goto free_tx_rings;
		}
	}
	adapter->tx_ring_created = B_TRUE;

	return (DDI_SUCCESS);

free_tx_rings:
	qlcnic_destroy_all_tx_rings(adapter);
free_rx_rings:
	qlcnic_destroy_all_rx_rings(adapter);
free_rcv_desc:
	for (i = 0; i < MAX_RCV_CTX; ++i) {
		recv_ctx = &adapter->recv_ctx[i];

		for (ring = 0; ring < adapter->max_rds_rings; ring++) {
			if ((rcv_desc = recv_ctx->rcv_desc[ring]) != NULL) {
				kmem_free(rcv_desc,
				    sizeof (qlcnic_rcv_desc_ctx_t));
				recv_ctx->rcv_desc[ring] = NULL;
			}
		}
	}

	return (DDI_FAILURE);
}

void
qlcnic_destroy_rxtx_rings(qlcnic_adapter *adapter)
{
	qlcnic_destroy_all_tx_rings(adapter);
	qlcnic_destroy_all_rx_rings(adapter);
}

/*
 * quiesce(9E) entry point.
 *
 * This function is called when the system is single-threaded at high
 * PIL with preemption disabled. Therefore, this function must not be
 * blocked.
 *
 * This function returns DDI_SUCCESS on success, or DDI_FAILURE on failure.
 */
static int
qlcnic_quiesce(dev_info_t *dip)
{
	qlcnic_adapter *adapter;
	uint16_t pci_cmd_word;
	struct qlcnic_sds_ring_s *sds_ring;
	int i;

	adapter = (qlcnic_adapter *)ddi_get_driver_private(dip);
	if (adapter == NULL)
		return (DDI_FAILURE);

	/* stop watchdog */
	if (adapter->watchdog_running) {
		(void) untimeout(adapter->watchdog_timer);
		adapter->watchdog_running = 0;
	}

	/* stop tx service */
	adapter->drv_state = QLCNIC_DRV_DOWN;

	/* Stop DMAs from the adapter */
	pci_cmd_word = pci_config_get16(adapter->pci_cfg_handle,
	    PCI_CONF_COMM);
	pci_cmd_word &= ~(PCI_COMM_ME);
	pci_config_put16(adapter->pci_cfg_handle, PCI_CONF_COMM,
	    pci_cmd_word);

	/* Destroy fw rx/tx context */
	if (adapter->context_allocated == 1) {
		qlcnic_destroy_rxtx_ctx(adapter);
		adapter->context_allocated = 0;
	}
	/* Disable RX Interrupts */
	for (i = 0; i < adapter->max_sds_rings; i++) {
		sds_ring = &adapter->recv_ctx[0].sds_ring[0];

		if (sds_ring->interrupt_crb_addr) {
			QLCNIC_PCI_WRITE_32(0,
			    (unsigned long)sds_ring->interrupt_crb_addr);
		}
	}

	qlcnic_delay(100);

	/* Remove driver active bit */
	if (QLCNIC_IS_REVISION_P3PLUS(adapter->ahw.revision_id)) {
		(void) qlcnic_p3p_clear_dev_state(adapter, 0x1);
	}

	qlcnic_delay(100);

	return (DDI_SUCCESS);
}

DDI_DEFINE_STREAM_OPS(qlcnic_ops, nulldev, nulldev, qlcnicattach, qlcnicdetach,
	nodev, NULL, D_MP, NULL, qlcnic_quiesce);

static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module.  This one is a driver */
	qlcnic_ident,
	&qlcnic_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(&modldrv),
	NULL
};


int
_init(void)
{
	int ret;

	qlcnic_ops.devo_cb_ops->cb_str = NULL;
	mac_init_ops(&qlcnic_ops, "qlcnic");

	ret = mod_install(&modlinkage);
	if (ret != DDI_SUCCESS) {
		mac_fini_ops(&qlcnic_ops);
		cmn_err(CE_WARN, "qlcnic: mod_install failed %d", ret);
	}

	return (ret);
}


int
_fini(void)
{
	int ret;

	ret = mod_remove(&modlinkage);
	if (ret == DDI_SUCCESS)
		mac_fini_ops(&qlcnic_ops);
	return (ret);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
qlcnic_p3p_chk_start_firmware(qlcnic_adapter *adapter)
{
	uint32_t val, prev_state;
	int cnt = 0;
	int portnum = adapter->portnum;
	uint32_t heartbeat1, heartbeat2, temp;

	DPRINTF(DBG_INIT, (CE_NOTE,
	    "qlcnic_p3p_chk_start_firmware entered \n"));

	if (qlcnic_api_lock(adapter)) {
		cmn_err(CE_WARN, "%s%d: Wait time for sem5 exceeded",
		    adapter->name, adapter->instance);
		return (1);
	}

	adapter->qlcnic_hw_read_wx(adapter,
	    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_ACTIVE), &val, 4);
	/* Read adapter state */
	adapter->qlcnic_hw_read_wx(adapter,
	    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE), &prev_state, 4);
	if (val != QLCNIC_INVALID_REG_VALUE) {
		if (!(val & ((int)0x1 << (portnum * 4)))) {
			DPRINTF(DBG_INIT, (CE_NOTE,
			    "%s %s%d: Activating function, drv_active %x\n",
			    __func__, adapter->name, adapter->instance,
			    val));
			val |= ((uint32_t)0x1 << (portnum * 4));
			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_ACTIVE), &val, 4);
		}
	}
	if (adapter->need_fw_reset) {
		/*
		 * If this instance is called when adapter is needing reset,
		 * switch the state and move to state where it loads the f/w
		 * from the flash
		 */
		DPRINTF(DBG_INIT, (CE_NOTE,
		    "%s %s%d: Need f/w reset, state %x, own reset %d\n",
		    __func__, adapter->name, adapter->instance,
		    prev_state, adapter->own_reset));
		adapter->need_fw_reset = 0;
		if (adapter->own_reset) {
			adapter->own_reset = 0;

			/*
			 * If some other function has not restarted the reset
			 * already, do it now
			 */
			if ((prev_state != QLCNIC_INVALID_REG_VALUE) &&
			    (prev_state == QLCNIC_DEV_NEED_RESET)) {
				val = QLCNIC_DEV_INITALIZING;
				adapter->qlcnic_hw_write_wx(adapter,
				    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE), &val,
				    4);

				qlcnic_msleep(10);

				adapter->qlcnic_hw_read_wx(adapter,
				    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE), &val,
				    4);

				DPRINTF(DBG_INIT, (CE_NOTE,
				    "%s %s%d: New state %x\n",
				    __func__, adapter->name, adapter->instance,
				    val));

				qlcnic_api_unlock(adapter);
				return (1);
			} else {
				/* EMPTY */
				DPRINTF(DBG_INIT, (CE_NOTE,
				    "%s%d: Some other function took over reset,"
				    "dev state %x \n", prev_state));
			}
		}
	}

	DPRINTF(DBG_INIT, (CE_NOTE, "%s%d: dev state %x \n",
	    adapter->name, adapter->instance, prev_state));
	switch (prev_state) {

	case QLCNIC_DEV_COLD:
		if (adapter->force_fw_reset) {
			DPRINTF(DBG_INIT, (CE_NOTE, "%s%d: Force f/w reload \n",
			    adapter->name, adapter->instance));
			val = QLCNIC_DEV_INITALIZING;
			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE), &val, 4);
			qlcnic_api_unlock(adapter);
			return (1);
		}

		/*
		 * Check the peg alive counter, if it is going
		 * we don't need to reload the f/w
		 */
		DPRINTF(DBG_INIT, (CE_NOTE, "%s%d: Checking peg state ...\n",
		    adapter->name, adapter->instance));
		adapter->qlcnic_hw_read_wx(adapter,
		    QLCNIC_CAM_RAM(QLCNIC_PEG_ALIVE_COUNTER), &heartbeat1, 4);

		qlcnic_msleep(1000);

		adapter->qlcnic_hw_read_wx(adapter,
		    QLCNIC_CAM_RAM(QLCNIC_PEG_ALIVE_COUNTER), &heartbeat2, 4);

		/* Heart is beating, update device state and get out */
		if (heartbeat1 != heartbeat2) {
			DPRINTF(DBG_INIT, (CE_NOTE,
			    "%s %s%d: F/W ready, updating device state to "
			    "ready!\n",
			    __func__, adapter->name, adapter->instance));

			temp = QLCNIC_DEV_READY;
			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE), &temp, 4);
			qlcnic_api_unlock(adapter);
			return (0);
		} else {
			DPRINTF(DBG_INIT, (CE_NOTE,
			    "%s %s%d: Firmware will be reloaded!\n",
			    __func__, adapter->name, adapter->instance));

			val = QLCNIC_DEV_INITALIZING;
			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE), &val, 4);
			qlcnic_api_unlock(adapter);
			return (1);
		}
	case QLCNIC_DEV_READY:
		qlcnic_api_unlock(adapter);
		return (0);
	case QLCNIC_DEV_NEED_RESET:
		if (adapter->force_fw_reset) {
			DPRINTF(DBG_INIT, (CE_NOTE, "%s%d: Force f/w reload \n",
			    adapter->name, adapter->instance));
			val = QLCNIC_DEV_INITALIZING;
			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE), &val, 4);
			qlcnic_api_unlock(adapter);
			return (1);
		}
		adapter->qlcnic_hw_read_wx(adapter,
		    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_STATE), &val, 4);
		val |= ((uint32_t)0x1 << (portnum * 4));
		adapter->qlcnic_hw_write_wx(adapter,
		    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_STATE), &val, 4);
		break;
	case QLCNIC_DEV_NEED_QUISCENT:
		adapter->qlcnic_hw_read_wx(adapter,
		    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_STATE), &val, 4);
		val |= ((u32)0x1 << ((portnum * 4) + 1));
		adapter->qlcnic_hw_write_wx(adapter,
		    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_STATE), &val, 4);
		break;
	case QLCNIC_DEV_FAILED:
		qlcnic_api_unlock(adapter);
		return (1);
	case QLCNIC_DEV_INITALIZING:
		cmn_err(CE_WARN, "%s%d: some other function restarting f/w,"
		    " state 0x%x",
		    adapter->name, adapter->instance, prev_state);
		break;
	default:
		cmn_err(CE_WARN, "%s%d: Invalid dev state 0x%x, wait ...",
		    adapter->name, adapter->instance, prev_state);
		break;
	}

	qlcnic_api_unlock(adapter);

	qlcnic_msleep(1000);

	mutex_exit(&adapter->lock);
	/* Wait for adapter state to be ready */
	adapter->qlcnic_hw_read_wx(adapter,
	    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE), &val, 4);
	while ((val != QLCNIC_DEV_READY) && (++cnt < 200)) {
		/* Give up the time slice */
		qlcnic_delay(1000);
		adapter->qlcnic_hw_read_wx(adapter,
		    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE), &val, 4);
	}
	mutex_enter(&adapter->lock);

	if (cnt >= 200) {
		cmn_err(CE_WARN, "%s%d: Wait time for dev state exceeded",
		    adapter->name, adapter->instance);
		return (1);
	}

	if (qlcnic_api_lock(adapter)) {
		cmn_err(CE_WARN, "%s%d: Wait time for sem5 exceeds limit",
		    adapter->name, adapter->instance);
		return (1);
	}

	/* Clear quiscent and reset ready bits */
	adapter->qlcnic_hw_read_wx(adapter,
	    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_STATE), &val, 4);
	val &= ~((uint32_t)0x3 << (portnum * 4));
	adapter->qlcnic_hw_write_wx(adapter,
	    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_STATE), &val, 4);

	qlcnic_api_unlock(adapter);

	return (0);
}


int
qlcnic_p3p_clear_dev_state(qlcnic_adapter *adapter, int ex_flag)
{
	uint32_t val;
	int portnum = adapter->portnum;

	if (qlcnic_api_lock(adapter)) {
		cmn_err(CE_WARN, "%s%d: Wait time for sem5 exceeded",
		    adapter->name, adapter->instance);
		return (1);
	}

	/*
	 * This function is no longer active, and will NOT
	 * participate in IDC
	 */
	adapter->qlcnic_hw_read_wx(adapter,
	    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_ACTIVE), &val, 4);
	val &= ~((uint32_t)0x1 << (adapter->portnum * 4));
	adapter->qlcnic_hw_write_wx(adapter,
	    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_ACTIVE), &val, 4);

	/* Needs to be done only when called from detach */
	if (!(val & 0x11111111) && (ex_flag)) {
		val = QLCNIC_DEV_COLD;
		adapter->qlcnic_hw_write_wx(adapter,
		    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE), &val, 4);
	}

	/* Clear ready for RESET/QUIESCENT mode */
	adapter->qlcnic_hw_read_wx(adapter,
	    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_STATE), &val, 4);
	val &= ~((uint32_t)0x3 << (portnum * 4));
	adapter->qlcnic_hw_write_wx(adapter,
	    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_STATE), &val, 4);

	qlcnic_api_unlock(adapter);

	adapter->need_fw_reset = 0x0;

	return (0);
}

int
qlcnic_start_firmware(qlcnic_adapter *adapter)
{
	int i;
	int ret, temp;
	int first_boot = adapter->qlcnic_pci_read_normalize(adapter,
	    QLCNIC_CAM_RAM(0x1fc));

	if (check_hw_init(adapter) != 0) {
		cmn_err(CE_WARN, "%s%d: Error in HW init sequence",
		    adapter->name, adapter->instance);
		return (DDI_FAILURE);
	}

	if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id))
		qlcnic_set_port_mode(adapter);

	if (first_boot != 0x55555555) {
		temp = 0;
		adapter->qlcnic_hw_write_wx(adapter, CRB_CMDPEG_STATE,
		    &temp, 4);
		if (qlcnic_pinit_from_rom(adapter, 0) != 0) {
			cmn_err(CE_WARN, "%s%d: qlcnic_pinit_from_rom failed",
			    adapter->name, adapter->instance);
			return (DDI_FAILURE);
		}

		drv_usecwait(500);

		ret = qlcnic_load_from_flash(adapter);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN, "%s%d: load from flash failed",
			    adapter->name, adapter->instance);
			return (DDI_FAILURE);
		}
	}

	/*
	 * Tell the hardware our version number.
	 */
	i = (_QLCNIC_MAJOR << 16) |
	    ((_QLCNIC_MINOR << 8)) | (_QLCNIC_SUBVERSION);
	adapter->qlcnic_hw_write_wx(adapter, CRB_DRIVER_VERSION,
	    &i, 4);

	/* Handshake with the card before we register the devices. */
	if (qlcnic_phantom_init(adapter, 0) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: phantom init failed",
		    adapter->name, adapter->instance);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

int
qlcnic_dev_init(qlcnic_adapter *adapter)
{
	int first_driver = 0;
	uint32_t ret, temp;
	int start_flag;
	int status;

	if (QLCNIC_IS_REVISION_P3PLUS(adapter->ahw.revision_id)) {
		start_flag = qlcnic_p3p_chk_start_firmware(adapter);
		if (start_flag)
			first_driver = 1;
	} else if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id)) {
		if (adapter->ahw.pci_func == 0)
			first_driver = 1;
	} else {
		if (adapter->portnum == 0)
			first_driver = 1;
	}

	/* Initialize this field, since it is checked in free_dummy_dma() */
	adapter->dummy_dma.addr = NULL;

	if (first_driver) {
		status = qlcnic_start_firmware(adapter);
		if (status)
			return (status);
		/* If we are reloading firmware, get the latest version */
		adapter->fw_major = adapter->qlcnic_pci_read_normalize(
		    adapter, QLCNIC_FW_VERSION_MAJOR);
		adapter->fw_minor = adapter->qlcnic_pci_read_normalize(
		    adapter, QLCNIC_FW_VERSION_MINOR);
		adapter->fw_sub = adapter->qlcnic_pci_read_normalize(
		    adapter, QLCNIC_FW_VERSION_SUB);
		cmn_err(CE_NOTE, "!qlcnic(%d) fw version : %d.%d.%d \n",
		    adapter->instance, adapter->fw_major,
		    adapter->fw_minor, adapter->fw_sub);


		if (ret = qlcnic_initialize_dummy_dma(adapter)) {
			cmn_err(CE_WARN, "%s%d: load from flash failed",
			    adapter->name, adapter->instance);
			return (status);
		}
		if (qlcnic_api_lock(adapter)) {
			cmn_err(CE_WARN,
			    "%s %s%d: Wait time for sem5 exceeded!",
			    __func__, adapter->name, adapter->instance);
		} else {
			DPRINTF(DBG_INIT, (CE_NOTE,
			    "%s %s%d: Updating device state to ready!\n",
			    __func__, adapter->name, adapter->instance));

			temp = QLCNIC_DEV_READY;
			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_CAM_RAM(QLCNIC_CRB_DEV_STATE), &temp, 4);

			/* Clear quiscent and reset ready bits */
			adapter->qlcnic_hw_read_wx(adapter,
			    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_STATE), &temp, 4);
			temp &= ~((uint32_t)0x3 << (adapter->portnum * 4));
			adapter->qlcnic_hw_write_wx(adapter,
			    QLCNIC_CAM_RAM(QLCNIC_CRB_DRV_STATE), &temp, 4);

			qlcnic_api_unlock(adapter);
		}
	}

	if (QLCNIC_IS_REVISION_P3(adapter->ahw.revision_id)) {
		qlcnic_pcie_strap_init(adapter);
		adapter->physical_port = 0xFFFF;		/* not used */
	}

	/* Check DMA mask and adjust dma_attr_addr_hi if required */
	adapter->qlcnic_hw_read_wx(adapter, CRB_DMA_SIZE, &temp, 4);
	if (temp == 32) {
		/* larger address range supported by device/firmware */
		adapter->gc_dma_attr_desc.dma_attr_addr_hi =
		    0xffffffffffffffffULL;
		qlcnic_dma_attr_txbuf.dma_attr_addr_hi = 0xffffffffffffffffULL;
		qlcnic_dma_attr_rxbuf.dma_attr_addr_hi = 0xffffffffffffffffULL;
		qlcnic_dma_attr_cmddesc.dma_attr_addr_hi =
		    0xffffffffffffffffULL;
	}

	adapter->ahw.linkup = 0;
	/* check if phan initialized */
	if (qlcnic_receive_peg_ready(adapter)) {
		ret = (uint32_t)-EIO;
		goto free_dummy_dma;
	}

	qlcnic_flash_print(adapter);

	if (verbmsg != 0) {
		switch (adapter->ahw.board_type) {
		case QLCNIC_GBE:
			cmn_err(CE_NOTE, "%s: QUAD GbE port %d initialized\n",
			    qlcnic_driver_name, adapter->portnum);
			break;

		case QLCNIC_XGBE:
			cmn_err(CE_NOTE, "%s: XGbE port %d initialized\n",
			    qlcnic_driver_name, adapter->portnum);
			break;
		}
	}

	return (DDI_SUCCESS);

free_dummy_dma:
	qlcnic_free_dummy_dma(adapter);

	return (ret);
}
