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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * PX Fault Management Architecture
 */
#include <sys/types.h>
#include <sys/sunndi.h>
#include <sys/sunddi.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/fm/io/pci.h>
#include <sys/membar.h>
#include "px_obj.h"

extern uint_t px_ranges_phi_mask;

#define	PX_PCIE_PANIC_BITS \
	(PCIE_AER_UCE_DLP | PCIE_AER_UCE_FCP | PCIE_AER_UCE_TO | \
	PCIE_AER_UCE_RO | PCIE_AER_UCE_MTLP | PCIE_AER_UCE_ECRC)
#define	PX_PCIE_NO_PANIC_BITS \
	(PCIE_AER_UCE_TRAINING | PCIE_AER_UCE_SD | PCIE_AER_UCE_CA | \
	PCIE_AER_UCE_UC | PCIE_AER_UCE_UR)

/*
 * Global panicking state variabled used to control if further error handling
 * should occur.  If the system is already panic'ing or if PX itself has
 * recommended panic'ing the system, no further error handling should occur to
 * prevent the system from hanging.
 */
boolean_t px_panicing = B_FALSE;

/* Flag to disable px error channel registration and communication */
boolean_t px_err_channel_disable = B_FALSE;

/* XXX test */
int px_pil_test = 0;

static int px_pcie_ptlp(dev_info_t *dip, ddi_fm_error_t *derr,
    px_err_pcie_t *regs);

#if defined(DEBUG)
static void px_pcie_log(dev_info_t *dip, px_err_pcie_t *regs);
#else	/* DEBUG */
#define	px_pcie_log 0 &&
#endif	/* DEBUG */

static void px_fm_fini(px_t *px_p);
static int px_fm_channel_cb(dev_info_t *dip, ddi_cb_action_t action,
    void *cbarg, void *arg1, void *arg2);
static void px_fm_channel_handler(void *arg);

/*
 * Initialize px FMA support
 */
int
px_fm_attach(px_t *px_p)
{
	int		i;
	dev_info_t	*dip = px_p->px_dip;
	pcie_bus_t	*bus_p;
	pf_impl_t	*impl;

	px_p->px_fm_cap = DDI_FM_EREPORT_CAPABLE | DDI_FM_ERRCB_CAPABLE |
	    DDI_FM_ACCCHK_CAPABLE | DDI_FM_DMACHK_CAPABLE;

	/*
	 * check parents' capability
	 */
	ddi_fm_init(dip, &px_p->px_fm_cap, &px_p->px_fm_ibc);

	/*
	 * parents need to be ereport and error handling capable
	 */
	ASSERT(px_p->px_fm_cap &&
	    (DDI_FM_ERRCB_CAPABLE | DDI_FM_EREPORT_CAPABLE));

	/*
	 * Initialize lock to synchronize fabric error handling
	 */
	mutex_init(&px_p->px_fm_mutex, NULL, MUTEX_DRIVER,
	    (void *)px_p->px_fm_ibc);

	px_p->px_pfd_idx = 0;
	for (i = 0; i < 5; i++)
		pcie_rc_init_pfd(dip, &px_p->px_pfd_arr[i]);
	PCIE_DIP2PFD(dip) = px_p->px_pfd_arr[0];

	bus_p = PCIE_DIP2BUS(dip);
	bus_p->bus_rp_bdf = px_p->px_bdf;
	bus_p->bus_rp_dip = dip;
	impl = bus_p->bus_root_pf_impl;

	/* IOV initialization */
	/* Create error handling taskq for guest domain */
	if (!pcie_is_physical_fabric(dip)) {
		mutex_init(&px_p->px_fm_tq_mutex, NULL,
		    MUTEX_DRIVER, NULL);
		impl->pf_derr = kmem_zalloc(sizeof (ddi_fm_error_t),
		    KM_SLEEP);
		px_p->px_fm_tq = ddi_taskq_create(dip,
		    "px_taskq", 1, TASKQ_DEFAULTPRI, 0);
		if (px_p->px_fm_tq == NULL) {
			DBG(DBG_ATTACH, dip,
			    "ddi_taskq_create failed\n");
			goto err2;
		}
	}

	/* Setup FMA communication channel */
	if (!px_err_channel_disable) {
		if (ddi_cb_register(dip, DDI_CB_FLAG_COMM,
		    px_fm_channel_cb, (void *)px_p, NULL,
		    &px_p->px_fm_cb_hdl) != DDI_SUCCESS) {
			DBG(DBG_ATTACH, dip,
			    "ddi_cb_register failed\n");
			if (px_p->px_fm_tq != NULL) {
				goto err3;
			} else {
				goto err1;
			}
		}
	}

	return (DDI_SUCCESS);

err3:
	ddi_taskq_destroy(px_p->px_fm_tq);
err2:
	kmem_free(impl->pf_derr, sizeof (ddi_fm_error_t));
	mutex_destroy(&px_p->px_fm_tq_mutex);
err1:
	px_fm_fini(px_p);
	return (DDI_FAILURE);
}

static void
px_fm_fini(px_t *px_p)
{
	int i;

	mutex_destroy(&px_p->px_fm_mutex);
	ddi_fm_fini(px_p->px_dip);
	for (i = 0; i < 5; i++)
		pcie_rc_fini_pfd(px_p->px_pfd_arr[i]);
}

/*
 * register error callback in parent
 */
void
px_fm_cb_enable(px_t *px_p)
{
	ddi_fm_handler_register(px_p->px_dip, px_fm_callback, px_p);
}

void
px_fm_cb_disable(px_t *px_p)
{
	ddi_fm_handler_unregister(px_p->px_dip);
}

/*
 * Deregister FMA
 */
void
px_fm_detach(px_t *px_p)
{
	dev_info_t	*dip = px_p->px_dip;

	if (px_p->px_fm_cb_hdl != NULL)
		(void) ddi_cb_unregister(px_p->px_fm_cb_hdl);

	if (px_p->px_fm_tq != NULL) {
		ddi_taskq_destroy(px_p->px_fm_tq);
		kmem_free(PCIE_DIP2BUS(dip)->bus_root_pf_impl->pf_derr,
		    sizeof (ddi_fm_error_t));
		mutex_destroy(&px_p->px_fm_tq_mutex);
	}
	px_fm_fini(px_p);
}

/*
 * Function used to setup access functions depending on level of desired
 * protection.
 */
void
px_fm_acc_setup(ddi_map_req_t *mp, dev_info_t *rdip, pci_regspec_t *rp)
{
	uchar_t fflag;
	ndi_err_t *errp;
	ddi_acc_hdl_t *hp;
	ddi_acc_impl_t *ap;

	hp = mp->map_handlep;
	ap = (ddi_acc_impl_t *)hp->ah_platform_private;
	fflag = ap->ahi_common.ah_acc.devacc_attr_access;

	if (mp->map_op == DDI_MO_MAP_LOCKED) {
		ndi_fmc_insert(rdip, ACC_HANDLE, (void *)hp, NULL);
		switch (fflag) {
		case DDI_FLAGERR_ACC:
			ap->ahi_get8 = i_ddi_prot_get8;
			ap->ahi_get16 = i_ddi_prot_get16;
			ap->ahi_get32 = i_ddi_prot_get32;
			ap->ahi_get64 = i_ddi_prot_get64;
			ap->ahi_put8 = i_ddi_prot_put8;
			ap->ahi_put16 = i_ddi_prot_put16;
			ap->ahi_put32 = i_ddi_prot_put32;
			ap->ahi_put64 = i_ddi_prot_put64;
			ap->ahi_rep_get8 = i_ddi_prot_rep_get8;
			ap->ahi_rep_get16 = i_ddi_prot_rep_get16;
			ap->ahi_rep_get32 = i_ddi_prot_rep_get32;
			ap->ahi_rep_get64 = i_ddi_prot_rep_get64;
			ap->ahi_rep_put8 = i_ddi_prot_rep_put8;
			ap->ahi_rep_put16 = i_ddi_prot_rep_put16;
			ap->ahi_rep_put32 = i_ddi_prot_rep_put32;
			ap->ahi_rep_put64 = i_ddi_prot_rep_put64;
			impl_acc_err_init(hp);
			errp = ((ddi_acc_impl_t *)hp)->ahi_err;
			if ((rp->pci_phys_hi & PCI_REG_ADDR_M) ==
			    PCI_ADDR_CONFIG)
				errp->err_cf = px_err_cfg_hdl_check;
			else
				errp->err_cf = px_err_pio_hdl_check;
			break;
		case DDI_CAUTIOUS_ACC :
			ap->ahi_get8 = i_ddi_caut_get8;
			ap->ahi_get16 = i_ddi_caut_get16;
			ap->ahi_get32 = i_ddi_caut_get32;
			ap->ahi_get64 = i_ddi_caut_get64;
			ap->ahi_put8 = i_ddi_caut_put8;
			ap->ahi_put16 = i_ddi_caut_put16;
			ap->ahi_put32 = i_ddi_caut_put32;
			ap->ahi_put64 = i_ddi_caut_put64;
			ap->ahi_rep_get8 = i_ddi_caut_rep_get8;
			ap->ahi_rep_get16 = i_ddi_caut_rep_get16;
			ap->ahi_rep_get32 = i_ddi_caut_rep_get32;
			ap->ahi_rep_get64 = i_ddi_caut_rep_get64;
			ap->ahi_rep_put8 = i_ddi_caut_rep_put8;
			ap->ahi_rep_put16 = i_ddi_caut_rep_put16;
			ap->ahi_rep_put32 = i_ddi_caut_rep_put32;
			ap->ahi_rep_put64 = i_ddi_caut_rep_put64;
			impl_acc_err_init(hp);
			errp = ((ddi_acc_impl_t *)hp)->ahi_err;
			if ((rp->pci_phys_hi & PCI_REG_ADDR_M) ==
			    PCI_ADDR_CONFIG)
				errp->err_cf = px_err_cfg_hdl_check;
			else
				errp->err_cf = px_err_pio_hdl_check;
			break;
		default:
			/* Illegal state, remove the handle from cache */
			ndi_fmc_remove(rdip, ACC_HANDLE, (void *)hp);
			break;
		}
	} else if (mp->map_op == DDI_MO_UNMAP) {
		ndi_fmc_remove(rdip, ACC_HANDLE, (void *)hp);
	}
}

/*
 * Function used to initialize FMA for our children nodes. Called
 * through pci busops when child node calls ddi_fm_init.
 */
/*ARGSUSED*/
int
px_fm_init_child(dev_info_t *dip, dev_info_t *cdip, int cap,
    ddi_iblock_cookie_t *ibc_p)
{
	px_t *px_p = DIP_TO_STATE(dip);

	ASSERT(ibc_p != NULL);
	*ibc_p = px_p->px_fm_ibc;

	return (px_p->px_fm_cap);
}

/*
 * lock access for exclusive PCIe access
 */
void
px_bus_enter(dev_info_t *dip, ddi_acc_handle_t handle)
{
	px_pec_t	*pec_p = ((px_t *)DIP_TO_STATE(dip))->px_pec_p;

	/*
	 * Exclusive access has been used for cautious put/get,
	 * Both utilize i_ddi_ontrap which, on sparcv9, implements
	 * similar protection as what on_trap() does, and which calls
	 * membar  #Sync to flush out all cpu deferred errors
	 * prior to get/put operation, so here we're not calling
	 * membar  #Sync - a difference from what's in pci_bus_enter().
	 */
	mutex_enter(&pec_p->pec_pokefault_mutex);
	pec_p->pec_acc_hdl = handle;
}

/*
 * unlock access for exclusive PCIe access
 */
/* ARGSUSED */
void
px_bus_exit(dev_info_t *dip, ddi_acc_handle_t handle)
{
	px_t		*px_p = DIP_TO_STATE(dip);
	px_pec_t	*pec_p = px_p->px_pec_p;

	pec_p->pec_acc_hdl = NULL;
	mutex_exit(&pec_p->pec_pokefault_mutex);
}

static uint64_t
px_in_addr_range(dev_info_t *dip, pci_ranges_t *ranges_p, uint64_t addr)
{
	uint64_t	addr_low, addr_high;

	addr_low = (uint64_t)(ranges_p->parent_high & px_ranges_phi_mask) << 32;
	addr_low |= (uint64_t)ranges_p->parent_low;
	addr_high = addr_low + ((uint64_t)ranges_p->size_high << 32) +
	    (uint64_t)ranges_p->size_low;

	DBG(DBG_ERR_INTR, dip, "Addr: 0x%llx high: 0x%llx low: 0x%llx\n",
	    addr, addr_high, addr_low);

	if ((addr < addr_high) && (addr >= addr_low))
		return (addr_low);

	return (0);
}

/*
 * PCI error callback which is registered with our parent to call
 * for PCIe logging when the CPU traps due to PCIe Uncorrectable Errors
 * and PCI BERR/TO/UE on IO Loads.
 */
/*ARGSUSED*/
int
px_fm_callback(dev_info_t *dip, ddi_fm_error_t *derr, const void *impl_data)
{
	dev_info_t	*pdip = ddi_get_parent(dip);
	px_t		*px_p = (px_t *)impl_data;
	int		i, acc_type = 0;
	int		lookup, rc_err, fab_err;
	uint64_t	addr, base_addr;
	uint64_t	fault_addr = (uint64_t)derr->fme_bus_specific;
	pcie_req_id_t	bdf = PCIE_INVALID_BDF;
	pci_ranges_t	*ranges_p;
	int		range_len;
	pf_data_t	*pfd_p;

	/*
	 * If the current thread already owns the px_fm_mutex, then we
	 * have encountered an error while processing a previous
	 * error.  Attempting to take the mutex again will cause the
	 * system to deadlock.
	 */
	if (px_p->px_fm_mutex_owner == curthread)
		return (DDI_FM_FATAL);

	i_ddi_fm_handler_exit(pdip);

	if (px_fm_enter(px_p) != DDI_SUCCESS) {
		i_ddi_fm_handler_enter(pdip);
		return (DDI_FM_FATAL);
	}

	/*
	 * Make sure this failed load came from this PCIe port.	 Check by
	 * matching the upper 32 bits of the address with the ranges property.
	 */
	range_len = px_p->px_ranges_length / sizeof (pci_ranges_t);
	i = 0;
	for (ranges_p = px_p->px_ranges_p; i < range_len; i++, ranges_p++) {
		base_addr = px_in_addr_range(dip, ranges_p, fault_addr);
		if (base_addr) {
			switch (ranges_p->child_high & PCI_ADDR_MASK) {
			case PCI_ADDR_CONFIG:
				acc_type = PF_ADDR_CFG;
				addr = NULL;
				bdf = (pcie_req_id_t)((fault_addr >> 12) &
				    0xFFFF);
				break;
			case PCI_ADDR_IO:
			case PCI_ADDR_MEM64:
			case PCI_ADDR_MEM32:
				acc_type = PF_ADDR_PIO;
				addr = fault_addr - base_addr;
				bdf = PCIE_INVALID_BDF;
				break;
			}
			break;
		}
	}

	/* This address doesn't belong to this leaf, just return with OK */
	if (!acc_type) {
		px_fm_exit(px_p);
		i_ddi_fm_handler_enter(pdip);
		return (DDI_FM_OK);
	}

	/* Do not do fabric scan in IO domain for CPU traps */
	if (!pcie_is_physical_fabric(px_p->px_dip)) {
		px_fm_exit(px_p);
		i_ddi_fm_handler_enter(pdip);
		return (DDI_FM_FATAL);
	}

	rc_err = px_err_cmn_intr(px_p, derr, PX_TRAP_CALL, PX_FM_BLOCK_ALL);
	lookup = pf_hdl_lookup(dip, derr->fme_ena, acc_type, (uint64_t)addr,
	    bdf);

	pfd_p = px_rp_en_q(px_p, bdf, addr,
	    (PCI_STAT_R_MAST_AB | PCI_STAT_R_TARG_AB));
	PCIE_ROOT_EH_SRC(pfd_p)->intr_type = PF_INTR_TYPE_DATA;

	/* Update affected info, either addr or bdf is not NULL */
	if (addr) {
		PFD_AFFECTED_DEV(pfd_p)->pe_affected_flags = PF_AFFECTED_ADDR;
	} else if (PCIE_CHECK_VALID_BDF(bdf)) {
		PFD_AFFECTED_DEV(pfd_p)->pe_affected_flags = PF_AFFECTED_BDF;
		PFD_AFFECTED_DEV(pfd_p)->pe_affected_bdf = bdf;
	}

	fab_err = px_scan_fabric(px_p, dip, derr);

	px_fm_exit(px_p);
	i_ddi_fm_handler_enter(pdip);

	if (!px_die)
		return (DDI_FM_OK);

	if ((rc_err & (PX_PANIC | PX_PROTECTED)) ||
	    (fab_err & PF_ERR_FATAL_FLAGS) ||
	    (lookup == PF_HDL_NOTFOUND))
		return (DDI_FM_FATAL);
	else if ((rc_err == PX_NO_ERROR) && (fab_err == PF_ERR_NO_ERROR))
		return (DDI_FM_OK);

	return (DDI_FM_NONFATAL);
}

/*
 * px_err_fabric_intr:
 * Interrupt handler for PCIE fabric block.
 * o lock
 * o create derr
 * o px_err_cmn_intr(leaf, with jbc)
 * o send ereport(fire fmri, derr, payload = BDF)
 * o dispatch (leaf)
 * o unlock
 * o handle error: fatal? fm_panic() : return INTR_CLAIMED)
 */
/* ARGSUSED */
uint_t
px_err_fabric_intr(px_t *px_p, msgcode_t msg_code, pcie_req_id_t rid)
{
	dev_info_t	*rpdip = px_p->px_dip;
	int		rc_err, fab_err;
	ddi_fm_error_t	derr;
	uint32_t	rp_status;
	uint16_t	ce_source, ue_source;
	pf_data_t	*pfd_p;

	if (px_fm_enter(px_p) != DDI_SUCCESS)
		goto done;

	/* Create the derr */
	bzero(&derr, sizeof (ddi_fm_error_t));
	derr.fme_version = DDI_FME_VERSION;
	derr.fme_ena = fm_ena_generate(0, FM_ENA_FMT1);
	derr.fme_flag = DDI_FM_ERR_UNEXPECTED;

	px_err_safeacc_check(px_p, &derr);

	if (msg_code == PCIE_MSG_CODE_ERR_COR) {
		rp_status = PCIE_AER_RE_STS_CE_RCVD;
		ce_source = rid;
		ue_source = 0;
	} else {
		rp_status = PCIE_AER_RE_STS_FE_NFE_RCVD;
		ce_source = 0;
		ue_source = rid;
		if (msg_code == PCIE_MSG_CODE_ERR_NONFATAL)
			rp_status |= PCIE_AER_RE_STS_NFE_MSGS_RCVD;
		else {
			rp_status |= PCIE_AER_RE_STS_FE_MSGS_RCVD;
			rp_status |= PCIE_AER_RE_STS_FIRST_UC_FATAL;
		}
	}

	if (derr.fme_flag == DDI_FM_ERR_UNEXPECTED) {
		ddi_fm_ereport_post(rpdip, PCI_ERROR_SUBCLASS "." PCIEX_FABRIC,
		    derr.fme_ena,
		    DDI_NOSLEEP, FM_VERSION, DATA_TYPE_UINT8, 0,
		    FIRE_PRIMARY, DATA_TYPE_BOOLEAN_VALUE, B_TRUE,
		    "pcie_adv_rp_status", DATA_TYPE_UINT32, rp_status,
		    "pcie_adv_rp_command", DATA_TYPE_UINT32, 0,
		    "pcie_adv_rp_ce_src_id", DATA_TYPE_UINT16, ce_source,
		    "pcie_adv_rp_ue_src_id", DATA_TYPE_UINT16, ue_source,
		    NULL);
	}

	/* Ensure that the rid of the fabric message will get scanned. */
	pfd_p = px_rp_en_q(px_p, rid, NULL, NULL);
	PCIE_ROOT_EH_SRC(pfd_p)->intr_type = PF_INTR_TYPE_FABRIC;

	rc_err = px_err_cmn_intr(px_p, &derr, PX_INTR_CALL, PX_FM_BLOCK_PCIE);

	/* call rootport dispatch */
	fab_err = px_scan_fabric(px_p, rpdip, &derr);

	px_err_panic(rc_err, PX_RC, fab_err, B_TRUE);
	px_fm_exit(px_p);
	px_err_panic(rc_err, PX_RC, fab_err, B_FALSE);

done:
	return (DDI_INTR_CLAIMED);
}

/*
 * px_scan_fabric:
 *
 * Check for drain state and if there is anything to scan.
 *
 * Note on pfd: Different interrupts will populate the pfd's differently.  The
 * px driver can have a total of 5 different error sources, so it has a queue of
 * 5 pfds.  Each valid PDF is linked together and passed to pf_scan_fabric.
 *
 * Each error handling will populate the following info in the pfd
 *
 *			Root Fault	 Intr Src	 Affected BDF
 *			----------------+---------------+------------
 * Callback/CPU Trap	Address/BDF	|DATA		|Lookup Addr
 * Mondo 62/63 (sun4u)	decode error	|N/A		|N/A
 * EPKT (sun4v)		decode epkt	|INTERNAL	|decode epkt
 * Fabric Message	fabric payload	|FABRIC		|NULL
 * Peek/Poke		Address/BDF	|NULL		|NULL
 *			----------------+---------------+------------
 */
int
px_scan_fabric(px_t *px_p, dev_info_t *rpdip, ddi_fm_error_t *derr) {
	int fab_err = 0;

	ASSERT(MUTEX_HELD(&px_p->px_fm_mutex));

	if (!px_lib_is_in_drain_state(px_p) && px_p->px_pfd_idx) {
		fab_err = pf_scan_fabric(rpdip, derr, px_p->px_pfd_arr[0]);
	}

	return (fab_err);
}

/*
 * px_err_safeacc_check:
 * Check to see if a peek/poke and cautious access is currently being
 * done on a particular leaf.
 *
 * Safe access reads induced fire errors will be handled by cpu trap handler
 * which will call px_fm_callback() which calls this function. In that
 * case, the derr fields will be set by trap handler with the correct values.
 *
 * Safe access writes induced errors will be handled by px interrupt
 * handlers, this function will fill in the derr fields.
 *
 * If a cpu trap does occur, it will quiesce all other interrupts allowing
 * the cpu trap error handling to finish before Fire receives an interrupt.
 *
 * If fire does indeed have an error when a cpu trap occurs as a result of
 * a safe access, a trap followed by a Mondo/Fabric interrupt will occur.
 * In which case derr will be initialized as "UNEXPECTED" by the interrupt
 * handler and this function will need to find if this error occured in the
 * middle of a safe access operation.
 *
 * @param px_p		leaf in which to check access
 * @param derr		fm err data structure to be updated
 */
void
px_err_safeacc_check(px_t *px_p, ddi_fm_error_t *derr)
{
	px_pec_t 	*pec_p = px_p->px_pec_p;
	int		acctype = pec_p->pec_safeacc_type;

	ASSERT(MUTEX_HELD(&px_p->px_fm_mutex));

	if (derr->fme_flag != DDI_FM_ERR_UNEXPECTED) {
		return;
	}

	/* safe access checking */
	switch (acctype) {
	case DDI_FM_ERR_EXPECTED:
		/*
		 * cautious access protection, protected from all err.
		 */
		ddi_fm_acc_err_get(pec_p->pec_acc_hdl, derr,
		    DDI_FME_VERSION);
		derr->fme_flag = acctype;
		derr->fme_acc_handle = pec_p->pec_acc_hdl;
		break;
	case DDI_FM_ERR_POKE:
		/*
		 * ddi_poke protection, check nexus and children for
		 * expected errors.
		 */
		membar_sync();
		derr->fme_flag = acctype;
		break;
	case DDI_FM_ERR_PEEK:
		derr->fme_flag = acctype;
		break;
	}
}

static void
px_err_post_eq(px_t *px_p, msgcode_t msg_code, pcie_req_id_t rid, uint64_t ena,
    msiqid_t msiq_id)
{
	dev_info_t	*rpdip = px_p->px_dip;

	ASSERT(MUTEX_HELD(&px_p->px_fm_mutex));

	ddi_fm_ereport_post(rpdip, PCIEX_ERROR_SUBCLASS "." PCIEX_RC_OVERFLOW,
	    ena, DDI_NOSLEEP, FM_VERSION, DATA_TYPE_UINT8, 0,
	    FIRE_PRIMARY, DATA_TYPE_BOOLEAN_VALUE, B_TRUE,
	    "eq_num", DATA_TYPE_UINT32, (uint32_t)msiq_id,
	    "eq_type", DATA_TYPE_UINT8, (uint8_t)msg_code,
	    "source_id", DATA_TYPE_UINT16, (uint16_t)rid,
	    NULL);
}

/*
 * Scan the msiq and post ereport for each of the records.
 */
static void
px_err_scan_eq(dev_info_t *dip, px_msiq_state_t *msiq_state_p, msiqid_t msiq_id,
    uint16_t *bdf, uint32_t size)
{
	px_t 		*px_p = DIP_TO_STATE(dip);
	msiq_rec_t	msiq_rec;
	msiqhead_t	*curr_head_p;
	msgcode_t	msg_code;
	int 		i, j, count = 0;
	uint64_t 	ena = fm_ena_generate(0, FM_ENA_FMT1);

	for (i = 1, curr_head_p = msiq_state_p->msiq_p[msiq_id].msiq_base_p;
	    i <= msiq_state_p->msiq_rec_cnt;
	    i++, curr_head_p = (msiqhead_t *)
	    ((caddr_t)curr_head_p + sizeof (msiq_rec_t))) {

		msiq_rec_type_t rec_type;
		/*
		 * Pay attention that the type field will be cleard,
		 * and later work will not run for it.
		 */
		px_lib_get_msiq_rec(dip, curr_head_p, &msiq_rec);

		rec_type = msiq_rec.msiq_rec_type;

		DBG(DBG_MSIQ_INTR, dip, "px_err_eq_post: MSIQ RECORD, "
		    "msiq_rec_type 0x%llx msiq_rec_rid 0x%llx\n",
		    rec_type, msiq_rec.msiq_rec_rid);

		/* Check MSIQ record type */
		if (rec_type != MSG_REC)
			continue;

		for (j = 0; j < count; j++)
			if (bdf[j] == msiq_rec.msiq_rec_rid)
				goto next_rec;
		if (count < size)
			bdf[count++] = msiq_rec.msiq_rec_rid;

		msg_code = msiq_rec.msiq_rec_data.msg.msg_code;

		DBG(DBG_MSIQ_INTR, dip, "px_err_eq_post: PCIE MSG "
		    "record, msg type 0x%x\n", msg_code);

		px_err_post_eq(px_p, msg_code,
		    msiq_rec.msiq_rec_rid, ena, msiq_id);
next_rec:
		;
	}
}


/*
 * Suggest panic if any EQ (except CE q) has overflown.
 */
int
px_err_check_eq(dev_info_t *dip)
{
	px_t			*px_p = DIP_TO_STATE(dip);
	px_msiq_state_t 	*msiq_state_p = &px_p->px_ib_p->ib_msiq_state;
	px_pec_t		*pec_p = px_p->px_pec_p;
	msiqid_t		eq_no = msiq_state_p->msiq_1st_msiq_id;
	pci_msiq_state_t	msiq_state;
	int			i;
	uint16_t		bdf[128];

	for (i = 0; i < msiq_state_p->msiq_cnt; i++) {
		if ((px_lib_msiq_getstate(dip, i + eq_no, &msiq_state) !=
		    DDI_SUCCESS) || msiq_state == PCI_MSIQ_STATE_ERROR) {
			bzero(bdf, sizeof (bdf));
			px_err_scan_eq(dip, msiq_state_p, i + eq_no, bdf, 128);

			if (i + eq_no == pec_p->pec_corr_msg_msiq_id)
				continue;

			return (PX_PANIC);
		}
	}
	return (PX_NO_PANIC);
}

/* ARGSUSED */
int
px_err_check_pcie(dev_info_t *dip, ddi_fm_error_t *derr, px_err_pcie_t *regs,
    pf_intr_type_t intr_type)
{
	px_t		*px_p = DIP_TO_STATE(dip);
	pf_data_t	*pfd_p = px_get_pfd(px_p);
	int		i;
	pf_pcie_adv_err_regs_t *adv_reg = PCIE_ADV_REG(pfd_p);

	PCIE_ROOT_EH_SRC(pfd_p)->intr_type = intr_type;

	/*
	 * set RC s_status in PCI term to coordinate with downstream fabric
	 * errors ananlysis.
	 */
	if (regs->primary_ue & PCIE_AER_UCE_UR)
		PCI_BDG_ERR_REG(pfd_p)->pci_bdg_sec_stat = PCI_STAT_R_MAST_AB;
	if (regs->primary_ue & PCIE_AER_UCE_CA)
		PCI_BDG_ERR_REG(pfd_p)->pci_bdg_sec_stat = PCI_STAT_R_TARG_AB;
	if (regs->primary_ue & (PCIE_AER_UCE_PTLP | PCIE_AER_UCE_ECRC))
		PCI_BDG_ERR_REG(pfd_p)->pci_bdg_sec_stat = PCI_STAT_PERROR;

	if (!regs->primary_ue)
		goto done;

	adv_reg->pcie_ce_status = regs->ce_reg;
	adv_reg->pcie_ue_status = regs->ue_reg | regs->primary_ue;
	PCIE_ADV_HDR(pfd_p, 0) = regs->rx_hdr1;
	PCIE_ADV_HDR(pfd_p, 1) = regs->rx_hdr2;
	PCIE_ADV_HDR(pfd_p, 2) = regs->rx_hdr3;
	PCIE_ADV_HDR(pfd_p, 3) = regs->rx_hdr4;
	for (i = regs->primary_ue; i != 1; i = i >> 1)
		adv_reg->pcie_adv_ctl++;

	if (regs->primary_ue & (PCIE_AER_UCE_UR | PCIE_AER_UCE_CA)) {
		if (pf_tlp_decode(PCIE_DIP2BUS(dip), adv_reg) == DDI_SUCCESS)
			PFD_ROOT_ERR_INFO(pfd_p)->scan_bdf =
			    adv_reg->pcie_ue_tgt_bdf;
	} else if (regs->primary_ue & PCIE_AER_UCE_PTLP) {
		if (pf_tlp_decode(PCIE_DIP2BUS(dip), adv_reg) == DDI_SUCCESS) {
			PFD_ROOT_ERR_INFO(pfd_p)->scan_bdf =
			    adv_reg->pcie_ue_tgt_bdf;
			if (adv_reg->pcie_ue_tgt_trans ==
			    PF_ADDR_PIO)
				PFD_ROOT_ERR_INFO(pfd_p)->scan_addr =
				    adv_reg->pcie_ue_tgt_addr;
		}

		/*
		 * Normally for Poisoned Completion TLPs we can look at the
		 * transmit log header for the original request and the original
		 * address, however this doesn't seem to be working.  HW BUG.
		 */
	} else if (regs->primary_ue & PCIE_AER_UCE_TO) {
		PCIE_ADV_HDR(pfd_p, 0) = regs->tx_hdr1;
		PCIE_ADV_HDR(pfd_p, 1) = regs->tx_hdr2;
		PCIE_ADV_HDR(pfd_p, 2) = regs->tx_hdr3;
		PCIE_ADV_HDR(pfd_p, 3) = regs->tx_hdr4;
		if (pf_tlp_decode(PCIE_DIP2BUS(dip), adv_reg) == DDI_SUCCESS) {
			PFD_ROOT_ERR_INFO(pfd_p)->scan_bdf =
			    adv_reg->pcie_ue_tgt_bdf;
			if (adv_reg->pcie_ue_tgt_trans == PF_ADDR_PIO)
				PFD_ROOT_ERR_INFO(pfd_p)->scan_addr =
				    adv_reg->pcie_ue_tgt_addr;
			/*
			 * Tx header is only logged on sparc for CTO error,
			 * the common fabric scan code should not be aware of
			 * the header. So need to decode the header only here
			 * and set the affected flags here instead of doing
			 * it in the fabric scan code.
			 */
			if (PCIE_CHECK_VALID_BDF(adv_reg->pcie_ue_tgt_bdf)) {
				PFD_SET_AFFECTED_FLAG(pfd_p, PF_AFFECTED_BDF);
				PFD_SET_AFFECTED_BDF(pfd_p,
				    adv_reg->pcie_ue_tgt_bdf);
			} else if (PFD_ROOT_ERR_INFO(pfd_p)->scan_addr) {
				PFD_SET_AFFECTED_FLAG(pfd_p, PF_AFFECTED_ADDR);
			}
		}
	}

done:
	px_pcie_log(dip, regs);

	/* Return No Error here and let the pcie misc module analyse it */
	return (PX_NO_ERROR);
}

#if defined(DEBUG)
static void
px_pcie_log(dev_info_t *dip, px_err_pcie_t *regs)
{
	DBG(DBG_ERR_INTR, dip,
	    "A PCIe RC error has occured\n"
	    "\tCE: 0x%x UE: 0x%x Primary UE: 0x%x\n"
	    "\tTX Hdr: 0x%x 0x%x 0x%x 0x%x\n\tRX Hdr: 0x%x 0x%x 0x%x 0x%x\n",
	    regs->ce_reg, regs->ue_reg, regs->primary_ue,
	    regs->tx_hdr1, regs->tx_hdr2, regs->tx_hdr3, regs->tx_hdr4,
	    regs->rx_hdr1, regs->rx_hdr2, regs->rx_hdr3, regs->rx_hdr4);
}
#endif

/*
 * look through poisoned TLP cases and suggest panic/no panic depend on
 * handle lookup.
 */
static int
px_pcie_ptlp(dev_info_t *dip, ddi_fm_error_t *derr, px_err_pcie_t *regs)
{
	pf_pcie_adv_err_regs_t adv_reg;
	pcie_req_id_t	bdf;
	uint64_t	addr;
	uint32_t	trans_type;
	int		tlp_sts, tlp_cmd;
	int		lookup = PF_HDL_NOTFOUND;

	if (regs->primary_ue != PCIE_AER_UCE_PTLP)
		return (PX_PANIC);

	if (!regs->rx_hdr1)
		goto done;

	adv_reg.pcie_ue_hdr[0] = regs->rx_hdr1;
	adv_reg.pcie_ue_hdr[1] = regs->rx_hdr2;
	adv_reg.pcie_ue_hdr[2] = regs->rx_hdr3;
	adv_reg.pcie_ue_hdr[3] = regs->rx_hdr4;

	tlp_sts = pf_tlp_decode(PCIE_DIP2BUS(dip), &adv_reg);
	tlp_cmd = ((pcie_tlp_hdr_t *)(adv_reg.pcie_ue_hdr))->type;

	if (tlp_sts == DDI_FAILURE)
		goto done;

	bdf = adv_reg.pcie_ue_tgt_bdf;
	addr = adv_reg.pcie_ue_tgt_addr;
	trans_type = adv_reg.pcie_ue_tgt_trans;

	switch (tlp_cmd) {
	case PCIE_TLP_TYPE_CPL:
	case PCIE_TLP_TYPE_CPLLK:
		/*
		 * Usually a PTLP is a CPL with data.  Grab the completer BDF
		 * from the RX TLP, and the original address from the TX TLP.
		 */
		if (regs->tx_hdr1) {
			adv_reg.pcie_ue_hdr[0] = regs->tx_hdr1;
			adv_reg.pcie_ue_hdr[1] = regs->tx_hdr2;
			adv_reg.pcie_ue_hdr[2] = regs->tx_hdr3;
			adv_reg.pcie_ue_hdr[3] = regs->tx_hdr4;

			lookup = pf_tlp_decode(PCIE_DIP2BUS(dip), &adv_reg);
			if (lookup != DDI_SUCCESS)
				break;
			addr = adv_reg.pcie_ue_tgt_addr;
			trans_type = adv_reg.pcie_ue_tgt_trans;
		} /* FALLTHRU */
	case PCIE_TLP_TYPE_IO:
	case PCIE_TLP_TYPE_MEM:
	case PCIE_TLP_TYPE_MEMLK:
		lookup = pf_hdl_lookup(dip, derr->fme_ena, trans_type, addr,
		    bdf);
		break;
	default:
		lookup = PF_HDL_NOTFOUND;
	}
done:
	return (lookup == PF_HDL_FOUND ? PX_NO_PANIC : PX_PANIC);
}

/*
 * px_get_pdf automatically allocates a RC pf_data_t and returns a pointer to
 * it.  This function should be used when an error requires a fabric scan.
 */
pf_data_t *
px_get_pfd(px_t *px_p) {
	int		idx = px_p->px_pfd_idx++;
	pf_data_t	*pfd_p = px_p->px_pfd_arr[idx];

	/* Clear Old Data */
	PFD_ROOT_ERR_INFO(pfd_p)->scan_bdf = PCIE_INVALID_BDF;
	PFD_ROOT_ERR_INFO(pfd_p)->scan_addr = 0;
	PCIE_ROOT_EH_SRC(pfd_p)->intr_type = PF_INTR_TYPE_NONE;
	PCIE_ROOT_EH_SRC(pfd_p)->intr_data = NULL;
	PFD_AFFECTED_DEV(pfd_p)->pe_affected_flags = NULL;
	PFD_AFFECTED_DEV(pfd_p)->pe_affected_bdf = PCIE_INVALID_BDF;
	PCI_BDG_ERR_REG(pfd_p)->pci_bdg_sec_stat = 0;
	PCIE_ADV_REG(pfd_p)->pcie_ce_status = 0;
	PCIE_ADV_REG(pfd_p)->pcie_ue_status = 0;
	PCIE_ADV_REG(pfd_p)->pcie_adv_ctl = 0;
	PCIE_ADV_REG(pfd_p)->pcie_aer_flag = 0;

	pfd_p->pe_next = NULL;

	if (idx > 0) {
		(px_p->px_pfd_arr[idx - 1])->pe_next = pfd_p;
		pfd_p->pe_prev = px_p->px_pfd_arr[idx - 1];
	} else {
		pfd_p->pe_prev = NULL;
	}

	pfd_p->pe_severity_flags = 0;
	pfd_p->pe_orig_severity_flags = 0;
	pfd_p->pe_valid = B_TRUE;

	return (pfd_p);
}

/*
 * This function appends a pf_data structure to the error q which is used later
 * during PCIe fabric scan.  It signifies:
 * o errs rcvd in RC, that may have been propagated to/from the fabric
 * o the fabric scan code should scan the device path of fault bdf/addr
 *
 * scan_bdf: The bdf that caused the fault, which may have error bits set.
 * scan_addr: The PIO addr that caused the fault, such as failed PIO, but not
 *	       failed DMAs.
 * s_status: Secondary Status equivalent to why the fault occured.
 *	     (ie S-TA/MA, R-TA)
 * Either the scan bdf or addr may be NULL, but not both.
 */
pf_data_t *
px_rp_en_q(px_t *px_p, pcie_req_id_t scan_bdf, uint32_t scan_addr,
    uint16_t s_status)
{
	pf_data_t	*pfd_p;

	if (!PCIE_CHECK_VALID_BDF(scan_bdf) && !scan_addr)
		return (NULL);

	pfd_p = px_get_pfd(px_p);

	PFD_ROOT_ERR_INFO(pfd_p)->scan_bdf = scan_bdf;
	PFD_ROOT_ERR_INFO(pfd_p)->scan_addr = (uint64_t)scan_addr;
	PCI_BDG_ERR_REG(pfd_p)->pci_bdg_sec_stat = s_status;

	return (pfd_p);
}


/*
 * Find and Mark CFG Handles as failed associated with the given BDF. We should
 * always know the BDF for CFG accesses, since it is encoded in the address of
 * the TLP.  Since there can be multiple cfg handles, mark them all as failed.
 */
/* ARGSUSED */
int
px_err_cfg_hdl_check(dev_info_t *dip, const void *handle, const void *arg1,
    const void *arg2)
{
	int			status = DDI_FM_FATAL;
	uint32_t		addr = *(uint32_t *)arg1;
	uint16_t		bdf = *(uint16_t *)arg2;
	pcie_bus_t		*bus_p;

	DBG(DBG_ERR_INTR, dip, "Check CFG Hdl: dip 0x%p addr 0x%x bdf=0x%x\n",
	    dip, addr, bdf);

	bus_p = PCIE_DIP2BUS(dip);

	/*
	 * Because CFG and IO Acc Handlers are on the same cache list and both
	 * types of hdls gets called for both types of errors.  For this checker
	 * only mark the device as "Non-Fatal" if the addr == NULL and bdf !=
	 * NULL.
	 */
	status = (!addr && (PCIE_CHECK_VALID_BDF(bdf) &&
	    (bus_p->bus_bdf == bdf))) ? DDI_FM_NONFATAL : DDI_FM_FATAL;

	return (status);
}

/*
 * Find and Mark all ACC Handles associated with a give address and BDF as
 * failed.  If the BDF != NULL, then check to see if the device has a ACC Handle
 * associated with ADDR.  If the handle is not found, mark all the handles as
 * failed.  If the BDF == NULL, mark the handle as failed if it is associated
 * with ADDR.
 */
int
px_err_pio_hdl_check(dev_info_t *dip, const void *handle, const void *arg1,
    const void *arg2)
{
	dev_info_t		*px_dip;
	px_t			*px_p;
	pci_ranges_t		*ranges_p;
	int			range_len;
	ddi_acc_handle_t	ap = (ddi_acc_handle_t)handle;
	ddi_acc_hdl_t		*hp = impl_acc_hdl_get(ap);
	int			i, status = DDI_FM_FATAL;
	uint64_t		fault_addr = *(uint64_t *)arg1;
	uint16_t		bdf = *(uint16_t *)arg2;
	uint64_t		base_addr, range_addr;
	uint_t			size;

	/*
	 * Find the correct px dip.  On system with a real Root Port, it's the
	 * node above the root port.  On systems without a real Root Port the px
	 * dip is the bus_rp_dip.
	 */
	px_dip = PCIE_DIP2BUS(dip)->bus_rp_dip;

	if (!PCIE_IS_RC(PCIE_DIP2BUS(px_dip)))
		px_dip = ddi_get_parent(px_dip);

	ASSERT(PCIE_IS_RC(PCIE_DIP2BUS(px_dip)));
	px_p = INST_TO_STATE(ddi_get_instance(px_dip));

	DBG(DBG_ERR_INTR, dip, "Check PIO Hdl: dip 0x%x addr 0x%x bdf=0x%x\n",
	    dip, fault_addr, bdf);

	/* Normalize the base addr to the addr and strip off the HB info. */
	base_addr = (hp->ah_pfn << MMU_PAGESHIFT) + hp->ah_offset;
	range_len = px_p->px_ranges_length / sizeof (pci_ranges_t);
	i = 0;
	for (ranges_p = px_p->px_ranges_p; i < range_len; i++, ranges_p++) {
		range_addr = px_in_addr_range(dip, ranges_p, base_addr);
		if (range_addr) {
			switch (ranges_p->child_high & PCI_ADDR_MASK) {
			case PCI_ADDR_IO:
			case PCI_ADDR_MEM64:
			case PCI_ADDR_MEM32:
				base_addr = base_addr - range_addr;
				break;
			}
			break;
		}
	}

	/*
	 * Mark the handle as failed if the ADDR is mapped, or if we
	 * know the BDF and ADDR == 0.
	 */
	size = hp->ah_len;
	if (((fault_addr >= base_addr) && (fault_addr < (base_addr + size))) ||
	    ((fault_addr == NULL) && (PCIE_CHECK_VALID_BDF(bdf) &&
	    (bdf == PCIE_DIP2BUS(dip)->bus_bdf))))
		status = DDI_FM_NONFATAL;

	return (status);
}

/*
 * Find and Mark all DNA Handles associated with a give address and BDF as
 * failed.  If the BDF != NULL, then check to see if the device has a DMA Handle
 * associated with ADDR.  If the handle is not found, mark all the handles as
 * failed.  If the BDF == NULL, mark the handle as failed if it is associated
 * with ADDR.
 */
int
px_err_dma_hdl_check(dev_info_t *dip, const void *handle, const void *arg1,
    const void *arg2)
{
	ddi_dma_impl_t		*pcie_dp;
	int			status = DDI_FM_FATAL;
	uint32_t		addr = *(uint32_t *)arg1;
	uint16_t		bdf = *(uint16_t *)arg2;
	uint32_t		base_addr;
	uint_t			size;

	DBG(DBG_ERR_INTR, dip, "Check PIO Hdl: dip 0x%x addr 0x%x bdf=0x%x\n",
	    dip, addr, bdf);

	pcie_dp = (ddi_dma_impl_t *)handle;
	base_addr = (uint32_t)pcie_dp->dmai_mapping;
	size = pcie_dp->dmai_size;

	/*
	 * Mark the handle as failed if the ADDR is mapped, or if we
	 * know the BDF and ADDR == 0.
	 */
	if (((addr >= base_addr) && (addr < (base_addr + size))) ||
	    ((addr == NULL) && PCIE_CHECK_VALID_BDF(bdf)))
		status = DDI_FM_NONFATAL;

	return (status);
}

int
px_fm_enter(px_t *px_p) {
	if (px_panicing || (px_p->px_fm_mutex_owner == curthread))
		return (DDI_FAILURE);

	mutex_enter(&px_p->px_fm_mutex);
	/*
	 * In rare cases when trap occurs and in the middle of scanning the
	 * fabric, a PIO will fail in the scan fabric.  The CPU error handling
	 * code will correctly panic the system, while a mondo for the failed
	 * PIO may also show up.  Normally the mondo will try to grab the mutex
	 * and wait until the callback finishes.  But in this rare case,
	 * mutex_enter actually suceeds also continues to scan the fabric.
	 *
	 * This code below is designed specifically to check for this case.  If
	 * we successfully grab the px_fm_mutex, the px_fm_mutex_owner better be
	 * NULL.  If it isn't that means we are in the rare corner case.  Return
	 * DDI_FAILURE, this should prevent PX from doing anymore error
	 * handling.
	 */
	if (px_p->px_fm_mutex_owner) {
		return (DDI_FAILURE);
	}

	px_p->px_fm_mutex_owner = curthread;

	if (px_panicing) {
		px_fm_exit(px_p);
		return (DDI_FAILURE);
	}

	/* Signal the PCIe error handling module error handling is starting */
	pf_eh_enter(PCIE_DIP2BUS(px_p->px_dip));

	return (DDI_SUCCESS);
}

static void
px_guest_panic(px_t *px_p)
{
	pf_data_t *root_pfd_p = PCIE_DIP2PFD(px_p->px_dip);
	pf_data_t *pfd_p;
	pcie_bus_t *bus_p, *root_bus_p;
	pcie_req_id_list_t *rl;
	pcie_domain_t *dom_p = PCIE_DIP2DOM(px_p->px_dip);
	uint_t count = dom_p->nfma_domid_cnt;
	dom_id_t dom_id;
	uint_t prop;
	pcie_domains_t *pd_list_p;
	pcie_req_id_t tgt_bdf;

	/*
	 * check if all devices under the root device are unassigned.
	 * this function should quickly return in non-IOV environment.
	 */
	root_bus_p = PCIE_PFD2BUS(root_pfd_p);
	if (PCIE_BDG_IS_UNASSIGNED(root_bus_p))
		return;

	/* assume all affected devs were in the error Q */
	for (pfd_p = root_pfd_p; pfd_p; pfd_p = pfd_p->pe_next) {
		bus_p = PCIE_PFD2BUS(pfd_p);

		/* deal with non-fma-capable domains */
		if (PCIE_BUS2DOM(bus_p)->nfma_panic) {
			if (PCIE_IS_BDG(bus_p)) {
				rl = PCIE_BDF_LIST_GET(bus_p);
				while (rl) {
					px_panic_domain(px_p, rl->bdf);
					rl = rl->next;
				}
			} else {
				px_panic_domain(px_p, bus_p->bus_bdf);
			}
			/* clear panic flag */
			PCIE_BUS2DOM(bus_p)->nfma_panic = B_FALSE;
		}

		/* deal with fma channel error fallback */
		if (PCIE_BUS2DOM(bus_p)->nfma_panic_backup) {
			PCIE_BUS2DOM(bus_p)->nfma_panic_backup = B_FALSE;

			if (count == 0)
				continue;

			if (PCIE_IS_BDG(bus_p)) {
				pd_list_p = PCIE_DOMAIN_LIST_GET(bus_p);
				while (pd_list_p) {
					dom_id = pd_list_p->domain_id;
					if ((prop = pciev_get_domain_prop(
					    dom_p->dom_hashp, dom_id)) &
					    PCIE_DOM_PROP_FB) {
						/*
						 * Get one device under this
						 * bdg which belong to this
						 * domainid
						 */
						tgt_bdf = pciev_get_leaf(
						    PCIE_BUS2DIP(bus_p),
						    dom_id);
						if (!PCIE_CHECK_VALID_BDF(
						    tgt_bdf))
							continue;

						px_panic_domain(px_p,
						    tgt_bdf);
						(void) pciev_update_domain_prop(
						    px_p->px_dip,
						    dom_id,
						    prop & (~PCIE_DOM_PROP_FB));
						count--;
					}
					pd_list_p = pd_list_p->cached_next;
				}
			} else {
				dom_id = PCIE_DOMAIN_ID_GET(bus_p);
				if ((prop = pciev_get_domain_prop(
				    dom_p->dom_hashp, dom_id)) &
				    PCIE_DOM_PROP_FB) {
					px_panic_domain(px_p, bus_p->bus_bdf);
					(void) pciev_update_domain_prop(
					    px_p->px_dip,
					    dom_id,
					    prop & (~PCIE_DOM_PROP_FB));
					count--;
				}
			}
		}
	}

	dom_p->nfma_domid_cnt = 0;
	prop = PCIE_DOM_PROP_FB;
	pciev_clear_domain_prop(dom_p->dom_hashp, prop);
}

void
px_fm_exit(px_t *px_p)
{
	pf_data_t *root_pfd_p = PCIE_DIP2PFD(px_p->px_dip);
	uint_t intr_type = PCIE_ROOT_EH_SRC(root_pfd_p)->intr_type;

	if (px_p->px_pfd_idx == 0) {
		px_p->px_fm_mutex_owner = NULL;
		mutex_exit(&px_p->px_fm_mutex);
		return;
	}

	/* forward error data to affected domains that are fma-capable */
	(void) pciev_eh_exit(root_pfd_p, intr_type);

	/* panic the affected domains that are non-fma-capable */
	px_guest_panic(px_p);

	/* Signal the PCIe error handling module error handling is ending */
	pf_eh_exit(PCIE_DIP2BUS(px_p->px_dip));

	px_p->px_pfd_idx = 0;
	px_p->px_fm_mutex_owner = NULL;
	mutex_exit(&px_p->px_fm_mutex);
}

/*
 * Panic if the err tunable is set and that we are not already in the middle
 * of panic'ing.
 *
 * rc_err = Error severity of PX specific errors
 * msg = Where the error was detected
 * fabric_err = Error severity of PCIe Fabric errors
 * isTest = Test if error severity causes panic
 */
#define	MSZ (sizeof (fm_msg) -strlen(fm_msg) - 1)
void
px_err_panic(int rc_err, int msg, int fabric_err, boolean_t isTest)
{
	char fm_msg[96] = "";
	int ferr = PX_NO_ERROR;

	if (panicstr) {
		px_panicing = B_TRUE;
		return;
	}

	if ((!(rc_err & px_die)) || px_no_panic)
		goto fabric;
	if (msg & PX_RC)
		(void) strncat(fm_msg, px_panic_rc_msg, MSZ);
	if (msg & PX_RP)
		(void) strncat(fm_msg, px_panic_rp_msg, MSZ);
	if (msg & PX_HB)
		(void) strncat(fm_msg, px_panic_hb_msg, MSZ);

fabric:
	if (fabric_err & PF_ERR_FATAL_FLAGS)
		ferr = PX_PANIC;
	else if (fabric_err & ~(PF_ERR_FATAL_FLAGS | PF_ERR_NO_ERROR))
		ferr = PX_NO_PANIC;

	if ((ferr & px_die) && (!px_no_panic)) {
		if (strlen(fm_msg)) {
			(void) strncat(fm_msg, " and", MSZ);
		}
		(void) strncat(fm_msg, px_panic_fab_msg, MSZ);
	}

	if (strlen(fm_msg)) {
		px_panicing = B_TRUE;
		if (!isTest)
			fm_panic("Fatal error has occured in:%s.(0x%x)(0x%x)",
			    fm_msg, rc_err, fabric_err);
	}
}


/* ARGSUSED */
static int
px_fm_channel_cb(dev_info_t *dip, ddi_cb_action_t action, void *cbarg,
    void *arg1, void *arg2)
{
	px_t			*px_p = (px_t *)arg1;
	pciv_recv_event_t	*event = (pciv_recv_event_t *)cbarg;
	char			*buf;
	pcie_fm_buf_hdr_t	*fm_hdr;
	size_t			len;
	pcie_fm_tq_arg_t	*tq_arg = &px_p->px_fm_tq_arg;
	int			ret = DDI_SUCCESS;
	uint16_t		peer_major, peer_minor;
	uint_t			prop = 0;
	pf_impl_t		*impl = PCIE_DIP2BUS(dip)->bus_root_pf_impl;
	int			ver;

	ASSERT(action == DDI_CB_COMM_RECV);

	switch (event->type) {
	case PCIV_EVT_READY:
		/*
		 * This event happens in IO domain too, but is only served
		 * in root domain
		 */
		if (!pcie_is_physical_fabric(dip))
			break;

		DBG(DBG_FM_CB, dip, "px_fm_channel_cb: "
		    "PCIV_EVT_READY from domain:%"PRIu64", dev:%x\n",
		    event->src_domain, event->src_func);

		/*
		 * Do error telemetry version negotiation, only forwarding
		 * data to IO domain with the same major version
		 */
		if (pciev_get_peer_err_ver(dip, event->src_domain,
		    &peer_major, &peer_minor) != 0) {
			DBG(DBG_FM_CB, dip, "px_fm_channel_cb: "
			    "Get peer version failed\n");
		} else {
			if (peer_major != PCIE_PFD_MAJOR_VER) {
				DBG(DBG_FM_CB, dip, "px_fm_channel_cb: "
				    "Major version dismatch, root ver=%d, "
				    "guest ver=%d\n", PCIE_PFD_MAJOR_VER,
				    peer_major);
			} else {
				prop = PCIE_DOM_PROP_FMA;
				ver = (int)peer_minor;
				pciev_set_negotiated_err_ver(impl,
				    min(ver, PCIE_PFD_MINOR_VER));
			}
		}

		/* Add the domain id to global domain list and mark it */
		if (pciev_update_domain_prop(dip, event->src_domain,
		    prop) != 0) {
			DBG(DBG_FM_CB, dip, "px_fm_channel_cb: "
			    "update global domain list failed\n");
		}

		break;
	case PCIV_EVT_NOT_READY:
		/*
		 * This event happens in IO domain too, but is only served
		 * in root domain
		 */
		if (!pcie_is_physical_fabric(dip))
			break;

		DBG(DBG_FM_CB, dip, "px_fm_channel_cb: "
		    "PCIV_EVT_NOT_READY from domain:%"PRIu64", dev:%x\n",
		    event->src_domain, event->src_func);

		/* Unmark the domain id to be non-fma capable */
		if (pciev_update_domain_prop(dip, event->src_domain, 0) != 0) {
			DBG(DBG_FM_CB, dip, "px_fm_channel_cb: "
			    "update global domain list failed\n");
		}

		break;
	case PCIV_EVT_DRV_DATA:		/* Served in IO domain only */
		if (pcie_is_physical_fabric(dip))
			break;

		ASSERT(event->buf != NULL && event->nbyte != 0);
		DBG(DBG_FM_CB, dip, "px_fm_channel_cb: "
		    "PCIV_EVT_DRV_DATA from domain:%"PRIu64", dev:%x\n",
		    event->src_domain, event->src_func);

		if (event->nbyte < sizeof (pcie_fm_buf_hdr_t)) {
			DBG(DBG_FM_CB, dip,
			    "px_fm_channel_cb: receiving bytes %d, less than "
			    "expected\n", event->nbyte);
			ret = DDI_FAILURE;
			break;
		}

		fm_hdr = (pcie_fm_buf_hdr_t *)event->buf;

		/* check received data length */
		len = sizeof (pcie_fm_buf_t) + fm_hdr->fb_data_size - 1;
		if (event->nbyte != len) {
			DBG(DBG_FM_CB, dip, "px_fm_channel_cb: "
			    "receiving bytes %d, expected bytes %d\n",
			    event->nbyte, len);
			goto err1;
		}

		/* version negotiation */
		if (fm_hdr->fb_type == PCIE_FM_BUF_TYPE_VER) {
			pcie_fm_ver_t fm_ver;

			ASSERT(fm_hdr->fb_data_size == sizeof (pcie_fm_ver_t));
			bcopy(((pcie_fm_buf_t *)event->buf)->fb_data,
			    (char *)&fm_ver, sizeof (pcie_fm_ver_t));

			DBG(DBG_FM_CB, dip, "px_fm_channel_cb: "
			    "Negotiating version, native %d.%d recvd %d.%d\n",
			    PCIE_PFD_MAJOR_VER, PCIE_PFD_MINOR_VER,
			    fm_ver.fv_root_maj_ver, fm_ver.fv_root_min_ver);

			fm_ver.fv_io_maj_ver = PCIE_PFD_MAJOR_VER;
			fm_ver.fv_io_min_ver = PCIE_PFD_MINOR_VER;
			bcopy((char *)&fm_ver,
			    ((pcie_fm_buf_t *)event->buf)->fb_data,
			    sizeof (pcie_fm_ver_t));

			ver = (int)fm_ver.fv_root_min_ver;
			pciev_set_negotiated_err_ver(impl,
			    min(ver, PCIE_PFD_MINOR_VER));
			break;
		}

		/* check received data type */
		if ((fm_hdr->fb_type != PCIE_FM_BUF_TYPE_PFD) &&
		    (fm_hdr->fb_type != PCIE_FM_BUF_TYPE_CTRL)) {
			DBG(DBG_FM_CB, dip, "px_fm_channel_cb: "
			    "receiving unknown data type %d\n",
			    fm_hdr->fb_type);
			goto err1;
		}

		buf = kmem_zalloc(event->nbyte, KM_SLEEP);
		bcopy(event->buf, buf, event->nbyte);

		/* Dispatch the data to a taskq */
		tq_arg->tq_buf = buf;
		tq_arg->tq_dip = dip;

		if (ddi_taskq_dispatch(px_p->px_fm_tq, px_fm_channel_handler,
		    (void *)tq_arg, DDI_NOSLEEP) != DDI_SUCCESS) {
			DBG(DBG_FM_CB, dip,
			    "px_fm_channel_cb: ddi_taskq_dispatch failed\n");
			if (px_p->px_fm_tq_state == PCIE_FM_BUSY)
				px_p->px_fm_tq_state = PCIE_FM_ERR;
			goto err2;
		}

		break;
	default:
		ret = DDI_FAILURE;
		break;
	}

	return (ret);

err2:
	kmem_free(buf, event->nbyte);
err1:
	fm_hdr->fb_io_sts = PCIE_FM_PEER_ERR;
	return (DDI_FAILURE);
}

static void
px_fm_channel_handler(void *arg)
{
	pcie_fm_tq_arg_t *tq_arg = (pcie_fm_tq_arg_t *)arg;
	pcie_fm_buf_t *fm_buf = (pcie_fm_buf_t *)tq_arg->tq_buf;
	pcie_fm_buf_hdr_t *fm_hdr = &fm_buf->fb_header;
	size_t len;
	dev_info_t *dip = tq_arg->tq_dip;
	px_t *px_p;
	dev_info_t *new_dip;
	pf_data_t *pfd_p;
	pf_impl_t *impl;

	px_p = INST_TO_STATE(ddi_get_instance(dip));
	impl = PCIE_DIP2BUS(dip)->bus_root_pf_impl;

	len = sizeof (pcie_fm_buf_t) + fm_hdr->fb_data_size - 1;

	mutex_enter(&px_p->px_fm_tq_mutex);

	if (px_p->px_fm_tq_state == PCIE_FM_ERR) {
		px_fm_exit(px_p);
		px_p->px_fm_tq_state = PCIE_FM_IDLE;
	}

	if ((fm_hdr->fb_type == PCIE_FM_BUF_TYPE_PFD) &&
	    (fm_hdr->fb_seq_num == 0)) {
		/* receiving the first FMA data segment whatever situation */
		if (px_p->px_fm_tq_state != PCIE_FM_IDLE) {
			px_fm_exit(px_p);
		}
		if (px_fm_enter(px_p) != DDI_SUCCESS)
			goto exit1;
		px_p->px_fm_data_id = fm_hdr->fb_data_id;
		px_p->px_fm_seg_count = 0;
		impl->pf_total = 0;
		impl->pf_dq_head_p = NULL;
		impl->pf_dq_tail_p = NULL;
		bzero(impl->pf_derr, sizeof (ddi_fm_error_t));
		impl->pf_derr->fme_version = DDI_FME_VERSION;
		impl->pf_derr->fme_ena = fm_ena_generate(0,
		    FM_ENA_FMT1);
		impl->pf_derr->fme_flag = DDI_FM_ERR_UNEXPECTED;
		px_p->px_fm_tq_state = PCIE_FM_BUSY;
	}

	if (px_p->px_fm_tq_state != PCIE_FM_BUSY) {
		/*
		 * the preceding data segments were in wrong condition,
		 * discard the new error data and simply return
		 */
		goto exit1;
	}

	if (fm_hdr->fb_data_id != px_p->px_fm_data_id) {
		DBG(DBG_FM_CB, dip,
		    "px_fm_channel_cb: id not match, received=%llx, "
		    "expected=%llx\n", fm_hdr->fb_data_id,
		    px_p->px_fm_data_id);
		goto exit2;
	}

	if (fm_hdr->fb_type == PCIE_FM_BUF_TYPE_CTRL) {
		uint32_t seg_count = fm_hdr->fb_seq_num;
		int sts = 0;
		int ferr = PX_NO_ERROR;

		if (seg_count != px_p->px_fm_seg_count) {
			DBG(DBG_FM_CB, dip, "px_fm_channel_cb: "
			    "seg count not match, received=%x, "
			    "expected=%x\n", px_p->px_fm_seg_count,
			    seg_count);
			goto exit2;
		}

		/* start error handling since all data segments are received */
		sts = pf_analyse_error(impl->pf_derr, impl);
		if (pcie_enable_io_dom_erpt)
			pf_send_ereport(impl->pf_derr, impl);

		if (sts & PF_ERR_FATAL_FLAGS)
			ferr = PX_PANIC;
		else if (sts & ~(PF_ERR_FATAL_FLAGS | PF_ERR_NO_ERROR))
			ferr = PX_NO_PANIC;
		if ((px_die & ferr) && (!px_no_panic)) {
			px_fm_exit(px_p);
			px_p->px_fm_tq_state = PCIE_FM_IDLE;
			px_p->px_fm_seg_count = 0;
			mutex_exit(&px_p->px_fm_tq_mutex);
			kmem_free(fm_buf, len);

			px_panicing = B_TRUE;
			fm_panic("%s-%d: PCI(-X) Express Fatal Error. (0x%x)",
			    ddi_driver_name(dip), ddi_get_instance(dip), sts);
		}

		goto exit2;
	}

	/* search the corresponding device node */
	if ((new_dip = pcie_find_dip_by_bdf(dip, fm_hdr->fb_bdf)) == NULL) {
		DBG(DBG_FM_CB, dip,
		    "px_fm_channel_cb: cannot find a device with bdf=0x%x\n",
		    fm_hdr->fb_bdf);
		goto exit2;
	}

	/* the received data must at least hold the block headers */
	if (fm_hdr->fb_data_size < sizeof (pf_blk_hdr_t) * PF_MAX_REG_BLOCK) {
		DBG(DBG_FM_CB, dip,
		    "px_fm_channel_cb: received data length too small %d\n",
		    fm_hdr->fb_data_size);
		goto exit2;

	}

	pfd_p = PCIE_DIP2PFD(new_dip);
	/* copy data to the corresponding pfd and queue the pfd */
	pciev_process_err_data(fm_buf->fb_data, pfd_p);
	if (PFD_IS_ROOT(pfd_p)) {
		pfd_p->pe_severity_flags =
		    PFD_ROOT_ERR_INFO(pfd_p)->severity_flags;
	}

	/* XXX need to initialize pfd fields not in the received data */

	pfd_p->pe_prev = NULL;
	pfd_p->pe_next = NULL;
	pfd_p->pe_lock = B_FALSE;
	pfd_p->pe_valid = B_TRUE;
	pf_en_dq(pfd_p, impl);

	px_p->px_fm_seg_count++;

	goto exit1;

exit2:
	px_fm_exit(px_p);
	px_p->px_fm_tq_state = PCIE_FM_IDLE;

exit1:
	mutex_exit(&px_p->px_fm_tq_mutex);
	kmem_free(fm_buf, len);
}
