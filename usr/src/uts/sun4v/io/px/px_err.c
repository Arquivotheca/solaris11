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
 * sun4v Fire Error Handling
 */

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/membar.h>
#include "px_obj.h"
#include "px_err.h"

static void px_err_fill_pfd(dev_info_t *dip, pf_data_t *pfd_p,
    px_rc_err_t *epkt);
static uint_t px_err_intr(px_fault_t *fault_p, px_rc_err_t *epkt);
static int  px_err_epkt_severity(px_t *px_p, ddi_fm_error_t *derr,
    px_rc_err_t *epkt, pf_data_t *pfd_p);

static void px_err_log_handle(dev_info_t *dip, px_rc_err_t *epkt,
    boolean_t is_block_pci, char *msg);
static void px_err_send_epkt_erpt(dev_info_t *dip, px_rc_err_t *epkt,
    boolean_t is_block_pci, int err, ddi_fm_error_t *derr,
    boolean_t is_valid_epkt);
static int px_cb_epkt_severity(dev_info_t *dip, ddi_fm_error_t *derr,
    px_rc_err_t *epkt, pf_data_t *pfd_p);
static int px_mmu_epkt_severity(dev_info_t *dip, ddi_fm_error_t *derr,
    px_rc_err_t *epkt, pf_data_t *pfd_p);
static int px_intr_epkt_severity(dev_info_t *dip, ddi_fm_error_t *derr,
    px_rc_err_t *epkt, pf_data_t *pfd_p);
static int px_port_epkt_severity(dev_info_t *dip, ddi_fm_error_t *derr,
    px_rc_err_t *epkt, pf_data_t *pfd_p);
static int px_pcie_epkt_severity(dev_info_t *dip, ddi_fm_error_t *derr,
    px_rc_err_t *epkt, pf_data_t *pfd_p);
static int px_intr_handle_errors(dev_info_t *dip, ddi_fm_error_t *derr,
    px_rc_err_t *epkt, pf_data_t *pfd_p);
static int px_port_handle_errors(dev_info_t *dip, ddi_fm_error_t *derr,
    px_rc_err_t *epkt, pf_data_t *pfd_p);
static void px_fix_legacy_epkt(dev_info_t *dip, ddi_fm_error_t *derr,
    px_rc_err_t *epkt);
static int px_mmu_handle_lookup(dev_info_t *dip, ddi_fm_error_t *derr,
    px_rc_err_t *epkt);

/* Include the code generated sun4v epkt checking code */
#include "px_err_gen.c"

/*
 * This variable indicates if we have a hypervisor that could potentially send
 * incorrect epkts. We always set this to TRUE for now until we find a way to
 * tell if this HV bug has been fixed.
 */
boolean_t px_legacy_epkt = B_TRUE;

#define	EPKT_IS_PORT_PIO_TO(epkt)			\
	((epkt->rc_descr.block == BLOCK_PORT) &&	\
	(epkt->rc_descr.op == OP_PIO) && 		\
	(epkt->rc_descr.phase == PH_IRR) && (epkt->rc_descr.cond == CND_TO))

/*
 * px_err_cb_intr:
 * Interrupt handler for the Host Bus Block.
 */
uint_t
px_err_cb_intr(caddr_t arg)
{
	px_fault_t	*fault_p = (px_fault_t *)arg;
	px_rc_err_t	*epkt = (px_rc_err_t *)fault_p->px_intr_payload;

	if (epkt != NULL) {
		return (px_err_intr(fault_p, epkt));
	}

	return (DDI_INTR_UNCLAIMED);
}

/*
 * px_err_dmc_pec_intr:
 * Interrupt handler for the DMC/PEC block.
 */
uint_t
px_err_dmc_pec_intr(caddr_t arg)
{
	px_fault_t	*fault_p = (px_fault_t *)arg;
	px_rc_err_t	*epkt = (px_rc_err_t *)fault_p->px_intr_payload;

	if (epkt != NULL) {
		return (px_err_intr(fault_p, epkt));
	}

	return (DDI_INTR_UNCLAIMED);
}

/*
 * px_err_cmn_intr:
 * Common function called by trap, mondo and fabric intr.
 * This function is more meaningful in sun4u implementation.  Kept
 * to mirror sun4u call stack.
 * o check for safe access
 * o create and queue RC info for later use in fabric scan.
 *   o RUC/WUC, PTLP, MMU Errors(CA), UR
 *
 * @param px_p		leaf in which to check access
 * @param derr		fm err data structure to be updated
 * @param caller	PX_TRAP_CALL | PX_INTR_CALL
 * @param chkjbc	whether to handle hostbus registers (ignored)
 * @return err		PX_NO_PANIC | PX_PROTECTED |
 *                      PX_PANIC | PX_HW_RESET | PX_EXPECTED
 */
/* ARGSUSED */
int
px_err_cmn_intr(px_t *px_p, ddi_fm_error_t *derr, int caller, int block)
{
	px_err_safeacc_check(px_p, derr);
	return (PX_NO_ERROR);
}

/*
 * fills RC specific fault data
 */
static void
px_err_fill_pfd(dev_info_t *dip, pf_data_t *pfd_p, px_rc_err_t *epkt) {
	pf_pcie_adv_err_regs_t adv_reg;
	pcie_req_id_t	fault_bdf = PCIE_INVALID_BDF;
	uint64_t	fault_addr = 0;
	uint16_t	s_status = 0;
	px_pec_err_t	*pec_p;
	uint32_t	dir;

	/* Add an PCIE PF_DATA Entry */
	switch (epkt->rc_descr.block) {
	case BLOCK_MMU:
		/* Only PIO Fault Addresses are valid, this is DMA */
		s_status = PCI_STAT_S_TARG_AB;
		fault_addr = NULL;

		if (epkt->rc_descr.H) {
			fault_bdf = (pcie_req_id_t)(epkt->hdr[0] >> 16);
			PFD_AFFECTED_DEV(pfd_p)->pe_affected_flags =
			    PF_AFFECTED_BDF;
			PFD_AFFECTED_DEV(pfd_p)->pe_affected_bdf =
			    fault_bdf;
		}
		break;
	case BLOCK_PCIE:
		pec_p = (px_pec_err_t *)epkt;
		dir = pec_p->pec_descr.dir;

		/* translate RC UR/CA to legacy secondary errors */
		if ((dir == DIR_READ || dir == DIR_WRITE) &&
		    pec_p->pec_descr.U) {
			if (pec_p->ue_reg_status & PCIE_AER_UCE_UR)
				s_status |= PCI_STAT_R_MAST_AB;
			if (pec_p->ue_reg_status & PCIE_AER_UCE_CA)
				s_status |= PCI_STAT_R_TARG_AB;
		}

		if (pec_p->ue_reg_status & PCIE_AER_UCE_PTLP)
			s_status |= PCI_STAT_PERROR;

		if (pec_p->ue_reg_status & PCIE_AER_UCE_CA)
			s_status |= PCI_STAT_S_TARG_AB;

		if (pec_p->pec_descr.H) {
			adv_reg.pcie_ue_hdr[0] = (uint32_t)(pec_p->hdr[0] >>32);
			adv_reg.pcie_ue_hdr[1] = (uint32_t)(pec_p->hdr[0]);
			adv_reg.pcie_ue_hdr[2] = (uint32_t)(pec_p->hdr[1] >>32);
			adv_reg.pcie_ue_hdr[3] = (uint32_t)(pec_p->hdr[1]);

			if (pf_tlp_decode(PCIE_DIP2BUS(dip), &adv_reg) ==
			    DDI_SUCCESS) {
				fault_bdf = adv_reg.pcie_ue_tgt_bdf;
				fault_addr = adv_reg.pcie_ue_tgt_addr;
				/*
				 * affected BDF is to be filled in by
				 * px_scan_fabric
				 */
			}
		}
		break;
	case BLOCK_HOSTBUS:
	case BLOCK_INTR:
	case BLOCK_PORT:
		/*
		 *  If the affected device information is available then we
		 *  add the affected_bdf to the pfd, so the affected device
		 *  will be scanned and added to the error q. This will then
		 *  go through the pciev_eh code path and forgive the error
		 *  as needed.
		 */
		if (PFD_AFFECTED_DEV(pfd_p)->pe_affected_flags ==
		    PF_AFFECTED_BDF)
			fault_bdf = PFD_AFFECTED_DEV(pfd_p)->pe_affected_bdf;

		break;
	default:
		break;
	}

	/*
	 * Don't overwrite the following if px_xxx_epkt_severity() has
	 * set these values
	 */
	if ((PFD_ROOT_ERR_INFO(pfd_p)->scan_bdf == PCIE_INVALID_BDF) &&
	    (PFD_ROOT_ERR_INFO(pfd_p)->scan_addr == 0)) {
		PFD_ROOT_ERR_INFO(pfd_p)->scan_bdf = fault_bdf;
		PFD_ROOT_ERR_INFO(pfd_p)->scan_addr = (uint64_t)fault_addr;
		PCI_BDG_ERR_REG(pfd_p)->pci_bdg_sec_stat = s_status;
	}
}

/*
 * Convert error severity from PX internal values to PCIe Fabric values.  Most
 * are self explanitory, except PX_PROTECTED.  PX_PROTECTED will never be
 * returned as is if forgivable.
 */
static int
px_err_to_fab_sev(int *rc_err) {
	int fab_err = 0;

	if (*rc_err & px_die) {
		/*
		 * Let fabric scan decide the final severity of the error.
		 * This is needed incase IOV code needs to forgive the error.
		 */
		*rc_err = PX_FABRIC_SCAN;
		fab_err |= PF_ERR_PANIC;
	}

	if (*rc_err & (PX_EXPECTED | PX_NO_PANIC))
		fab_err |= PF_ERR_NO_PANIC;

	if (*rc_err & PX_NO_ERROR)
		fab_err |= PF_ERR_NO_ERROR;

	return (fab_err);
}

/*
 * px_err_intr:
 * Interrupt handler for the JBC/DMC/PEC block.
 * o lock
 * o create derr
 * o check safe access
 * o px_err_check_severity(epkt)
 * o pcie_scan_fabric
 * o Idle intr state
 * o unlock
 * o handle error: fatal? fm_panic() : return INTR_CLAIMED)
 */
static uint_t
px_err_intr(px_fault_t *fault_p, px_rc_err_t *epkt)
{
	px_t		*px_p = DIP_TO_STATE(fault_p->px_fh_dip);
	dev_info_t	*rpdip = px_p->px_dip;
	int		rc_err, tmp_rc_err, fab_err, msg;
	ddi_fm_error_t	derr;
	pf_data_t	*pfd_p;

	if (px_fm_enter(px_p) != DDI_SUCCESS)
		goto done;

	pfd_p = px_get_pfd(px_p);
	PCIE_ROOT_EH_SRC(pfd_p)->intr_type = PF_INTR_TYPE_INTERNAL;
	PCIE_ROOT_EH_SRC(pfd_p)->intr_data = epkt;

	/* Create the derr */
	bzero(&derr, sizeof (ddi_fm_error_t));
	derr.fme_version = DDI_FME_VERSION;
	derr.fme_ena = fm_ena_generate(epkt->stick, FM_ENA_FMT1);
	derr.fme_flag = DDI_FM_ERR_UNEXPECTED;

	/* Basically check for safe access */
	(void) px_err_cmn_intr(px_p, &derr, PX_INTR_CALL, PX_FM_BLOCK_ALL);

	/* Check the severity of this error */
	rc_err = px_err_epkt_severity(px_p, &derr, epkt, pfd_p);

	/* Pass the 'rc_err' severity to the fabric scan code. */
	tmp_rc_err = rc_err;
	pfd_p->pe_severity_flags = px_err_to_fab_sev(&rc_err);

	/* Scan the fabric */
	if (!(fab_err = px_scan_fabric(px_p, rpdip, &derr))) {
		/*
		 * Fabric scan didn't occur because of some error condition
		 * such as Root Port being in drain state, so reset rc_err.
		 */
		rc_err = tmp_rc_err;
	}

	/* Set the intr state to idle for the leaf that received the mondo */
	if (px_lib_intr_setstate(rpdip, fault_p->px_fh_sysino,
	    INTR_IDLE_STATE) != DDI_SUCCESS) {
		px_fm_exit(px_p);
		return (DDI_INTR_UNCLAIMED);
	}

	switch (epkt->rc_descr.block) {
	case BLOCK_MMU: /* FALLTHROUGH */
	case BLOCK_INTR:
		msg = PX_RC;
		break;
	case BLOCK_PCIE:
		msg = PX_RP;
		break;
	case BLOCK_HOSTBUS: /* FALLTHROUGH */
	default:
		msg = PX_HB;
		break;
	}

	px_err_panic(rc_err, msg, fab_err, B_TRUE);
	px_fm_exit(px_p);
	px_err_panic(rc_err, msg, fab_err, B_FALSE);

done:
	return (DDI_INTR_CLAIMED);
}

/*
 * px_err_epkt_severity:
 * Check the severity of the fire error based the epkt received
 *
 * @param px_p		leaf in which to take the snap shot.
 * @param derr		fm err in which the ereport is to be based on
 * @param epkt		epkt recevied from HV
 */
static int
px_err_epkt_severity(px_t *px_p, ddi_fm_error_t *derr, px_rc_err_t *epkt,
    pf_data_t *pfd_p)
{
	px_pec_t 	*pec_p = px_p->px_pec_p;
	dev_info_t	*dip = px_p->px_dip;
	boolean_t	is_safeacc = B_FALSE;
	boolean_t	is_block_pci = B_FALSE;
	boolean_t	is_valid_epkt = B_FALSE;
	int		err = 0;

	/* Cautious access error handling  */
	switch (derr->fme_flag) {
	case DDI_FM_ERR_EXPECTED:
		/*
		 * For ddi_caut_put treat all events as nonfatal. Here
		 * we have the handle and can call ndi_fm_acc_err_set().
		 */
		derr->fme_status = DDI_FM_NONFATAL;
		ndi_fm_acc_err_set(pec_p->pec_acc_hdl, derr);
		is_safeacc = B_TRUE;
		break;
	case DDI_FM_ERR_PEEK:
	case DDI_FM_ERR_POKE:
		/*
		 * For ddi_peek/poke treat all events as nonfatal.
		 */
		is_safeacc = B_TRUE;
		break;
	default:
		is_safeacc = B_FALSE;
	}

	/*
	 * Older hypervisors in some cases send epkts with incorrect fields.
	 * We have to handle these "special" epkts correctly.
	 */
	if (px_legacy_epkt)
		px_fix_legacy_epkt(dip, derr, epkt);

	/*
	 * The affected device by default is set to 'SELF'. The 'block'
	 * specific error handling below will update this as needed.
	 */
	PFD_AFFECTED_DEV(pfd_p)->pe_affected_flags = PF_AFFECTED_SELF;

	switch (epkt->rc_descr.block) {
	case BLOCK_HOSTBUS:
		err = px_cb_epkt_severity(dip, derr, epkt, pfd_p);
		break;
	case BLOCK_MMU:
		err = px_mmu_epkt_severity(dip, derr, epkt, pfd_p);
		break;
	case BLOCK_INTR:
		err = px_intr_epkt_severity(dip, derr, epkt, pfd_p);
		break;
	case BLOCK_PORT:
		err = px_port_epkt_severity(dip, derr, epkt, pfd_p);
		break;
	case BLOCK_PCIE:
		is_block_pci = B_TRUE;
		err = px_pcie_epkt_severity(dip, derr, epkt, pfd_p);
		break;
	default:
		err = 0;
	}

	px_err_fill_pfd(dip, pfd_p, epkt);

	if ((err & PX_HW_RESET) || (err & PX_PANIC)) {
		if (px_log & PX_PANIC)
			px_err_log_handle(dip, epkt, is_block_pci, "PANIC");
		is_valid_epkt = B_TRUE;
	} else if (err & PX_PROTECTED) {
		if (px_log & PX_PROTECTED)
			px_err_log_handle(dip, epkt, is_block_pci, "PROTECTED");
		is_valid_epkt = B_TRUE;
	} else if (err & PX_NO_PANIC) {
		if (px_log & PX_NO_PANIC)
			px_err_log_handle(dip, epkt, is_block_pci, "NO PANIC");
		is_valid_epkt = B_TRUE;
	} else if (err & PX_NO_ERROR) {
		if (px_log & PX_NO_ERROR)
			px_err_log_handle(dip, epkt, is_block_pci, "NO ERROR");
		is_valid_epkt = B_TRUE;
	} else if (err == 0) {
		px_err_log_handle(dip, epkt, is_block_pci, "UNRECOGNIZED");
		is_valid_epkt = B_FALSE;

		/* Panic on a unrecognized epkt */
		err = PX_PANIC;
	}

	px_err_send_epkt_erpt(dip, epkt, is_block_pci, err, derr,
	    is_valid_epkt);

	/* Readjust the severity as a result of safe access */
	if (is_safeacc && !(err & PX_PANIC) && !(px_die & PX_PROTECTED))
		err = PX_NO_PANIC;

	return (err);
}

static void
px_err_send_epkt_erpt(dev_info_t *dip, px_rc_err_t *epkt,
    boolean_t is_block_pci, int err, ddi_fm_error_t *derr,
    boolean_t is_valid_epkt)
{
	char buf[FM_MAX_CLASS], descr_buf[1024];

	/* send ereport for debug purposes */
	(void) snprintf(buf, FM_MAX_CLASS, "%s", PX_FM_RC_UNRECOG);

	if (is_block_pci) {
		px_pec_err_t *pec = (px_pec_err_t *)epkt;
		(void) snprintf(descr_buf, sizeof (descr_buf),
		    "%s Epkt contents:\n"
		    "Block: 0x%x, Dir: 0x%x, Flags: Z=%d, S=%d, R=%d\n"
		    "I=%d, H=%d, C=%d, U=%d, E=%d, P=%d\n"
		    "PCI Err Status: 0x%x, PCIe Err Status: 0x%x\n"
		    "CE Status Reg: 0x%x, UE Status Reg: 0x%x\n"
		    "HDR1: 0x%lx, HDR2: 0x%lx\n"
		    "Err Src Reg: 0x%x, Root Err Status: 0x%x\n"
		    "Err Severity: 0x%x\n",
		    is_valid_epkt ? "Valid" : "Invalid",
		    pec->pec_descr.block, pec->pec_descr.dir,
		    pec->pec_descr.Z, pec->pec_descr.S,
		    pec->pec_descr.R, pec->pec_descr.I,
		    pec->pec_descr.H, pec->pec_descr.C,
		    pec->pec_descr.U, pec->pec_descr.E,
		    pec->pec_descr.P, pec->pci_err_status,
		    pec->pcie_err_status, pec->ce_reg_status,
		    pec->ue_reg_status, pec->hdr[0],
		    pec->hdr[1], pec->err_src_reg,
		    pec->root_err_status, err);

		ddi_fm_ereport_post(dip, buf, derr->fme_ena,
		    DDI_NOSLEEP, FM_VERSION, DATA_TYPE_UINT8, 0,
		    EPKT_SYSINO, DATA_TYPE_UINT64,
		    is_valid_epkt ? pec->sysino : 0,
		    EPKT_EHDL, DATA_TYPE_UINT64,
		    is_valid_epkt ? pec->ehdl : 0,
		    EPKT_STICK, DATA_TYPE_UINT64,
		    is_valid_epkt ? pec->stick : 0,
		    EPKT_DW0, DATA_TYPE_UINT64, ((uint64_t *)pec)[3],
		    EPKT_DW1, DATA_TYPE_UINT64, ((uint64_t *)pec)[4],
		    EPKT_DW2, DATA_TYPE_UINT64, ((uint64_t *)pec)[5],
		    EPKT_DW3, DATA_TYPE_UINT64, ((uint64_t *)pec)[6],
		    EPKT_DW4, DATA_TYPE_UINT64, ((uint64_t *)pec)[7],
		    EPKT_PEC_DESCR, DATA_TYPE_STRING, descr_buf);
	} else {
		(void) snprintf(descr_buf, sizeof (descr_buf),
		    "%s Epkt contents:\n"
		    "Block: 0x%x, Op: 0x%x, Phase: 0x%x, Cond: 0x%x\n"
		    "Dir: 0x%x, Flags: STOP=%d, H=%d, R=%d, D=%d\n"
		    "M=%d, S=%d, Size: 0x%x, Addr: 0x%lx\n"
		    "Hdr1: 0x%lx, Hdr2: 0x%lx, Res: 0x%lx\n"
		    "Err Severity: 0x%x\n",
		    is_valid_epkt ? "Valid" : "Invalid",
		    epkt->rc_descr.block, epkt->rc_descr.op,
		    epkt->rc_descr.phase, epkt->rc_descr.cond,
		    epkt->rc_descr.dir, epkt->rc_descr.STOP,
		    epkt->rc_descr.H, epkt->rc_descr.R,
		    epkt->rc_descr.D, epkt->rc_descr.M,
		    epkt->rc_descr.S, epkt->size, epkt->addr,
		    epkt->hdr[0], epkt->hdr[1], epkt->reserved,
		    err);

		ddi_fm_ereport_post(dip, buf, derr->fme_ena,
		    DDI_NOSLEEP, FM_VERSION, DATA_TYPE_UINT8, 0,
		    EPKT_SYSINO, DATA_TYPE_UINT64,
		    is_valid_epkt ? epkt->sysino : 0,
		    EPKT_EHDL, DATA_TYPE_UINT64,
		    is_valid_epkt ? epkt->ehdl : 0,
		    EPKT_STICK, DATA_TYPE_UINT64,
		    is_valid_epkt ? epkt->stick : 0,
		    EPKT_DW0, DATA_TYPE_UINT64, ((uint64_t *)epkt)[3],
		    EPKT_DW1, DATA_TYPE_UINT64, ((uint64_t *)epkt)[4],
		    EPKT_DW2, DATA_TYPE_UINT64, ((uint64_t *)epkt)[5],
		    EPKT_DW3, DATA_TYPE_UINT64, ((uint64_t *)epkt)[6],
		    EPKT_DW4, DATA_TYPE_UINT64, ((uint64_t *)epkt)[7],
		    EPKT_RC_DESCR, DATA_TYPE_STRING, descr_buf);
	}
}

static void
px_err_log_handle(dev_info_t *dip, px_rc_err_t *epkt, boolean_t is_block_pci,
    char *msg)
{
	if (is_block_pci) {
		px_pec_err_t *pec = (px_pec_err_t *)epkt;
		DBG(DBG_ERR_INTR, dip,
		    "A PCIe root port error has occured with a severity"
		    " \"%s\"\n"
		    "\tBlock: 0x%x, Dir: 0x%x, Flags: Z=%d, S=%d, R=%d, I=%d\n"
		    "\tH=%d, C=%d, U=%d, E=%d, P=%d\n"
		    "\tpci_err: 0x%x, pcie_err=0x%x, ce_reg: 0x%x\n"
		    "\tue_reg: 0x%x, Hdr1: 0x%p, Hdr2: 0x%p\n"
		    "\terr_src: 0x%x, root_err: 0x%x\n",
		    msg, pec->pec_descr.block, pec->pec_descr.dir,
		    pec->pec_descr.Z, pec->pec_descr.S, pec->pec_descr.R,
		    pec->pec_descr.I, pec->pec_descr.H, pec->pec_descr.C,
		    pec->pec_descr.U, pec->pec_descr.E, pec->pec_descr.P,
		    pec->pci_err_status, pec->pcie_err_status,
		    pec->ce_reg_status, pec->ue_reg_status, pec->hdr[0],
		    pec->hdr[1], pec->err_src_reg, pec->root_err_status);
	} else {
		DBG(DBG_ERR_INTR, dip,
		    "A PCIe root complex error has occured with a severity"
		    " \"%s\"\n"
		    "\tBlock: 0x%x, Op: 0x%x, Phase: 0x%x, Cond: 0x%x\n"
		    "\tDir: 0x%x, Flags: STOP=%d, H=%d, R=%d, D=%d, M=%d\n"
		    "\tS=%d, Size: 0x%x, Addr: 0x%p\n"
		    "\tHdr1: 0x%p, Hdr2: 0x%p, Res: 0x%p\n",
		    msg, epkt->rc_descr.block, epkt->rc_descr.op,
		    epkt->rc_descr.phase, epkt->rc_descr.cond,
		    epkt->rc_descr.dir, epkt->rc_descr.STOP, epkt->rc_descr.H,
		    epkt->rc_descr.R, epkt->rc_descr.D, epkt->rc_descr.M,
		    epkt->rc_descr.S, epkt->size, epkt->addr, epkt->hdr[0],
		    epkt->hdr[1], epkt->reserved);
	}
}

/* ARGSUSED */
static void
px_fix_legacy_epkt(dev_info_t *dip, ddi_fm_error_t *derr, px_rc_err_t *epkt)
{
	/*
	 * We don't have a default case for any of the below switch statements
	 * since we are ok with the code falling through.
	 */
	switch (epkt->rc_descr.block) {
	case BLOCK_HOSTBUS:
		switch (epkt->rc_descr.op) {
		case OP_DMA:
			switch (epkt->rc_descr.phase) {
			case PH_UNKNOWN:
				switch (epkt->rc_descr.cond) {
				case CND_UNKNOWN:
					switch (epkt->rc_descr.dir) {
					case DIR_RESERVED:
						epkt->rc_descr.dir = DIR_READ;
						break;
					} /* DIR */
				} /* CND */
			} /* PH */
		} /* OP */
		break;
	case BLOCK_MMU:
		switch (epkt->rc_descr.op) {
		case OP_XLAT:
			switch (epkt->rc_descr.phase) {
			case PH_DATA:
				switch (epkt->rc_descr.cond) {
				case CND_PROT:
					switch (epkt->rc_descr.dir) {
					case DIR_UNKNOWN:
						epkt->rc_descr.dir = DIR_WRITE;
						break;
					} /* DIR */
				} /* CND */
				break;
			case PH_IRR:
				switch (epkt->rc_descr.cond) {
				case CND_RESERVED:
					switch (epkt->rc_descr.dir) {
					case DIR_IRR:
						epkt->rc_descr.phase = PH_ADDR;
						epkt->rc_descr.cond = CND_IRR;
					} /* DIR */
				} /* CND */
			} /* PH */
		} /* OP */
		break;
	case BLOCK_INTR:
		switch (epkt->rc_descr.op) {
		case OP_MSIQ:
			switch (epkt->rc_descr.phase) {
			case PH_UNKNOWN:
				switch (epkt->rc_descr.cond) {
				case CND_ILL:
					switch (epkt->rc_descr.dir) {
					case DIR_RESERVED:
						epkt->rc_descr.dir = DIR_IRR;
						break;
					} /* DIR */
					break;
				case CND_IRR:
					switch (epkt->rc_descr.dir) {
					case DIR_IRR:
						epkt->rc_descr.cond = CND_OV;
						break;
					} /* DIR */
				} /* CND */
			} /* PH */
			break;
		case OP_RESERVED:
			switch (epkt->rc_descr.phase) {
			case PH_UNKNOWN:
				switch (epkt->rc_descr.cond) {
				case CND_ILL:
					switch (epkt->rc_descr.dir) {
					case DIR_IRR:
						epkt->rc_descr.op = OP_MSI32;
						epkt->rc_descr.phase = PH_DATA;
						break;
					} /* DIR */
				} /* CND */
				break;
			case PH_DATA:
				switch (epkt->rc_descr.cond) {
				case CND_INT:
					switch (epkt->rc_descr.dir) {
					case DIR_UNKNOWN:
						epkt->rc_descr.op = OP_MSI32;
						break;
					} /* DIR */
				} /* CND */
			} /* PH */
		} /* OP */
	} /* BLOCK */
}

/* ARGSUSED */
static int
px_intr_handle_errors(dev_info_t *dip, ddi_fm_error_t *derr, px_rc_err_t *epkt,
    pf_data_t *pfd_p)
{
	return (px_err_check_eq(dip));
}

/* ARGSUSED */
static int
px_port_handle_errors(dev_info_t *dip, ddi_fm_error_t *derr, px_rc_err_t *epkt,
    pf_data_t *pfd_p)
{
	pf_pcie_adv_err_regs_t	adv_reg;
	uint16_t		s_status;
	int			sts = PX_PANIC;

	/*
	 * Check for failed non-posted writes, which are errors that are not
	 * defined in the PCIe spec.  If not return panic.
	 */
	if (!((epkt->rc_descr.op == OP_PIO) &&
	    (epkt->rc_descr.phase == PH_IRR))) {
		sts = (PX_PANIC);
		goto done;
	}

	/*
	 * Gather the error logs, if they do not exist just return with no panic
	 * and let the fabric message take care of the error.
	 */
	if (!epkt->rc_descr.H) {
		sts = (PX_NO_PANIC);
		goto done;
	}

	adv_reg.pcie_ue_hdr[0] = (uint32_t)(epkt->hdr[0] >> 32);
	adv_reg.pcie_ue_hdr[1] = (uint32_t)(epkt->hdr[0]);
	adv_reg.pcie_ue_hdr[2] = (uint32_t)(epkt->hdr[1] >> 32);
	adv_reg.pcie_ue_hdr[3] = (uint32_t)(epkt->hdr[1]);

	/* XXX May need some check for header log validity */

	sts = pf_tlp_decode(PCIE_DIP2BUS(dip), &adv_reg);

	if (epkt->rc_descr.M)
		adv_reg.pcie_ue_tgt_addr = epkt->addr;

	if (!((sts == DDI_SUCCESS) || (epkt->rc_descr.M))) {
		/* Let the fabric message take care of error */
		sts = PX_NO_PANIC;
		goto done;
	}

	/* See if the failed transaction belonged to a hardened driver */
	if (pf_hdl_lookup(dip, derr->fme_ena,
	    adv_reg.pcie_ue_tgt_trans, adv_reg.pcie_ue_tgt_addr,
	    adv_reg.pcie_ue_tgt_bdf) == PF_HDL_FOUND)
		sts = (PX_NO_PANIC);
	else
		sts = (PX_PANIC);

	/* Add pfd to cause a fabric scan */
	switch (epkt->rc_descr.cond) {
	case CND_RCA:
		s_status = PCI_STAT_R_TARG_AB;
		break;
	case CND_RUR:
		s_status = PCI_STAT_R_MAST_AB;
		break;
	}
	PFD_ROOT_ERR_INFO(pfd_p)->scan_bdf = adv_reg.pcie_ue_tgt_bdf;
	PFD_ROOT_ERR_INFO(pfd_p)->scan_addr = adv_reg.pcie_ue_tgt_addr;
	PCI_BDG_ERR_REG(pfd_p)->pci_bdg_sec_stat = s_status;

	if (PCIE_CHECK_VALID_BDF(adv_reg.pcie_ue_tgt_bdf)) {
		PFD_SET_AFFECTED_FLAG(pfd_p, PF_AFFECTED_BDF);
		PFD_SET_AFFECTED_BDF(pfd_p, adv_reg.pcie_ue_tgt_bdf);
	} else if (adv_reg.pcie_ue_tgt_addr) {
		PFD_SET_AFFECTED_FLAG(pfd_p, PF_AFFECTED_ADDR);
	}

done:
	/* PIO timeout error needs to panic */
	if (EPKT_IS_PORT_PIO_TO(epkt))
		sts = PX_PANIC;

	return (sts);
}

/* ARGSUSED */
static int
px_pcie_epkt_severity(dev_info_t *dip, ddi_fm_error_t *derr, px_rc_err_t *epkt,
    pf_data_t *pfd_p)
{
	px_pec_err_t	*pec_p = (px_pec_err_t *)epkt;
	px_err_pcie_t	pcie;
	pf_pcie_adv_err_regs_t adv_reg;
	int		sts;
	uint32_t	temp;

	/*
	 * Check for failed PIO Read/Writes, which are errors that are not
	 * defined in the PCIe spec.
	 */

	temp = PCIE_AER_UCE_UR | PCIE_AER_UCE_CA;
	if (((pec_p->pec_descr.dir == DIR_READ) ||
	    (pec_p->pec_descr.dir == DIR_WRITE)) &&
	    pec_p->pec_descr.U && (pec_p->ue_reg_status & temp)) {

		adv_reg.pcie_ue_hdr[0] = (uint32_t)(pec_p->hdr[0] >> 32);
		adv_reg.pcie_ue_hdr[1] = (uint32_t)(pec_p->hdr[0]);
		adv_reg.pcie_ue_hdr[2] = (uint32_t)(pec_p->hdr[1] >> 32);
		adv_reg.pcie_ue_hdr[3] = (uint32_t)(pec_p->hdr[1]);

		sts = pf_tlp_decode(PCIE_DIP2BUS(dip), &adv_reg);

		if (sts == DDI_SUCCESS &&
		    pf_hdl_lookup(dip, derr->fme_ena,
		    adv_reg.pcie_ue_tgt_trans,
		    adv_reg.pcie_ue_tgt_addr,
		    adv_reg.pcie_ue_tgt_bdf) == PF_HDL_FOUND)
			return (PX_NO_PANIC);
		else
			return (PX_PANIC);
	}

	if (!pec_p->pec_descr.C)
		pec_p->ce_reg_status = 0;
	if (!pec_p->pec_descr.U)
		pec_p->ue_reg_status = 0;
	if (!pec_p->pec_descr.H)
		pec_p->hdr[0] = 0;
	if (!pec_p->pec_descr.I)
		pec_p->hdr[1] = 0;

	/*
	 * According to the PCIe spec, there is a first error pointer.  If there
	 * are header logs recorded and there are more than one error, the log
	 * will belong to the error that the first error pointer points to.
	 *
	 * The regs.primary_ue expects a bit number, go through the ue register
	 * and find the first error that occured.  Because the sun4v epkt spec
	 * does not define this value, the algorithm below gives the lower bit
	 * priority.
	 */
	bcopy(epkt, &pcie, sizeof (px_err_pcie_t));
	temp = pcie.ue_reg;
	if (temp) {
		int x;
		for (x = 0; !(temp & 0x1); x++) {
			temp = temp >> 1;
		}
		pcie.primary_ue = 1 << x;
	} else {
		pcie.primary_ue = 0;
	}

	/* Sun4v doesn't log the TX hdr except for CTOs */
	if (pcie.primary_ue == PCIE_AER_UCE_TO) {
		pcie.tx_hdr1 = pcie.rx_hdr1;
		pcie.tx_hdr2 = pcie.rx_hdr2;
		pcie.tx_hdr3 = pcie.rx_hdr3;
		pcie.tx_hdr4 = pcie.rx_hdr4;
		pcie.rx_hdr1 = 0;
		pcie.rx_hdr2 = 0;
		pcie.rx_hdr3 = 0;
		pcie.rx_hdr4 = 0;
	} else {
		pcie.tx_hdr1 = 0;
		pcie.tx_hdr2 = 0;
		pcie.tx_hdr3 = 0;
		pcie.tx_hdr4 = 0;
	}

	return (px_err_check_pcie(dip, derr, &pcie, PF_INTR_TYPE_INTERNAL));
}

static int
px_mmu_handle_lookup(dev_info_t *dip, ddi_fm_error_t *derr, px_rc_err_t *epkt)
{
	uint64_t addr = (uint64_t)epkt->addr;
	pcie_req_id_t bdf = PCIE_INVALID_BDF;

	if (epkt->rc_descr.H) {
		bdf = (uint32_t)((epkt->hdr[0] >> 16) && 0xFFFF);
	}

	return (pf_hdl_lookup(dip, derr->fme_ena, PF_ADDR_DMA, addr,
	    bdf));
}
