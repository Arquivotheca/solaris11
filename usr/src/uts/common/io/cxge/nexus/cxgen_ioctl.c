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

#include <sys/ddi.h>
#include <sys/sunddi.h>

#include "cxge_common.h"
#include "cxge_regs.h"

#include "cxgen.h"

extern void *cxgen_list;

/* helpers */
static int cxgen_ioctl_pci_rw(p_adapter_t, void *, int, int);
static int cxgen_ioctl_reg_rw(p_adapter_t, void *, int, int);
static int cxgen_ioctl_mii_rw(p_adapter_t, void *, int, int);
static int cxgen_ioctl_get_mem(p_adapter_t, void *, int);
static int cxgen_ioctl_get_context(p_adapter_t, void *, int);
static int cxgen_ioctl_get_sgedesc(p_adapter_t, void *, int);
static int cxgen_ioctl_get_qsparams(p_adapter_t, void *, int);
static int cxgen_ioctl_regdump(p_adapter_t, void *, int);

/* ARGSUSED */
int
cxgen_ioctl(dev_t dev, int cmd, intptr_t d, int mode, cred_t *credp, int *rvalp)
{
	int rc = ENOTSUP;
	void *data = (void *) d;
	int instance = getminor(dev);
	p_adapter_t cxgenp = ddi_get_soft_state(cxgen_list, instance);

	switch (cmd) {
	case CXGEN_IOCTL_PCIGET32:
	case CXGEN_IOCTL_PCIPUT32:
		rc = cxgen_ioctl_pci_rw(cxgenp, data, mode,
		    cmd == CXGEN_IOCTL_PCIPUT32);
		break;
	case CXGEN_IOCTL_GET32:
	case CXGEN_IOCTL_PUT32:
		rc = cxgen_ioctl_reg_rw(cxgenp, data, mode,
		    cmd == CXGEN_IOCTL_PUT32);
		break;
	case CXGEN_IOCTL_SFGET: /* TODO: revisit. */
	case CXGEN_IOCTL_SFPUT: /* TODO: revisit. */
		return (ENOTSUP);
	case CXGEN_IOCTL_MIIGET:
	case CXGEN_IOCTL_MIIPUT:
		rc = cxgen_ioctl_mii_rw(cxgenp, data, mode,
		    cmd == CXGEN_IOCTL_MIIPUT);
		break;
	case CXGEN_IOCTL_GET_MEM:
		rc = cxgen_ioctl_get_mem(cxgenp, data, mode);
		break;
	case CXGEN_IOCTL_FPGA_READY:
	case CXGEN_IOCTL_MAKE_UNLOAD:
		return (ENOTSUP);
	case CXGEN_IOCTL_GET_CNTXT:
		rc = cxgen_ioctl_get_context(cxgenp, data, mode);
		break;
	case CXGEN_IOCTL_GET_QE:
	case CXGEN_IOCTL_GET_QS:
	case CXGEN_IOCTL_SEND:
	case CXGEN_IOCTL_CTRLSEND:
	case CXGEN_IOCTL_IRQ:
		return (ENOTSUP);
	case CXGEN_IOCTL_GET_SGEDESC:
		rc = cxgen_ioctl_get_sgedesc(cxgenp, data, mode);
		break;
	case CXGEN_IOCTL_GET_QSPARAM:
		rc = cxgen_ioctl_get_qsparams(cxgenp, data, mode);
		break;
	case CXGEN_IOCTL_REGDUMP:
		rc = cxgen_ioctl_regdump(cxgenp, data, mode);
		break;
	default:
		return (EINVAL);
	}

	return (rc);
}

static int
cxgen_ioctl_pci_rw(p_adapter_t cxgenp, void *data, int flags, int write)
{
	cxgen_reg32_cmd_t r;

	if (ddi_copyin(data, &r, sizeof (r), flags) < 0)
		return (EFAULT);

	/* address must be 32 bit aligned */
	r.reg &= ~0x3;

	if (write)
		t3_os_pci_write_config_4(cxgenp, r.reg, r.value);
	else {
		t3_os_pci_read_config_4(cxgenp, r.reg, &r.value);
		if (ddi_copyout(&r, data, sizeof (r), flags) < 0)
			return (EFAULT);
	}

	if (cxgen_fm_check_acc_handle(cxgenp->pci_regh) != DDI_FM_OK) {
		ddi_fm_service_impact(cxgenp->dip, DDI_SERVICE_DEGRADED);
		return (EFAULT);
	}

	return (0);
}

static int
cxgen_ioctl_reg_rw(p_adapter_t cxgenp, void *data, int flags, int write)
{
	cxgen_reg32_cmd_t r;

	if (ddi_copyin(data, &r, sizeof (r), flags) < 0)
		return (EFAULT);

	/* Register address must be 32 bit aligned */
	r.reg &= ~0x3;

	if (write)
		t3_write_reg(cxgenp, r.reg, r.value);
	else {
		r.value = t3_read_reg(cxgenp, r.reg);
		if (ddi_copyout(&r, data, sizeof (r), flags) < 0)
			return (EFAULT);
	}

	if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK) {
		ddi_fm_service_impact(cxgenp->dip, DDI_SERVICE_DEGRADED);
		return (EFAULT);
	}

	return (0);
}

static int
cxgen_ioctl_mii_rw(p_adapter_t cxgenp, void *data, int flags, int write)
{
	cxgen_mii_reg_data_t m;
	struct cphy *phy = &cxgenp->port[0].phy;
	int mmd, rc, reg;

#ifdef DEBUG
	/*
	 * We'll do all operations using port 0's phy->mdio_read, so we'd like
	 * to make sure that all read routines are the same.  Will fail for
	 * weird cards with different PHYs for different ports.
	 */
	for (int i = 1; i < cxgenp->params.nports; i++) {
		ASSERT(phy->mdio_read == cxgenp->port[i].phy.mdio_read);
		ASSERT(phy->mdio_write == cxgenp->port[i].phy.mdio_write);
	}
#endif

	if (write && phy->mdio_write == NULL)
		return (ENOTSUP);

	if (!write && phy->mdio_read == NULL)
		return (ENOTSUP);

	if (ddi_copyin(data, &m, sizeof (m), flags) < 0)
		return (EFAULT);

	if (is_10G(cxgenp)) {
		mmd = m.phy_id >> 8;
		if (!mmd)
			mmd = MDIO_DEV_PCS;
		else if (mmd > MDIO_DEV_VEND2)
			return (EINVAL);

		reg = m.reg_num;
	} else {
		mmd = 0;
		reg = m.reg_num & 0x1f;
	}

	if (write)
		rc = -phy->mdio_write(cxgenp, m.phy_id & 0x1f, mmd, reg,
		    m.val_in);
	else
		rc = -phy->mdio_read(cxgenp, m.phy_id & 0x1f, mmd, reg,
		    &m.val_out);

	if (rc == 0 && !write &&
	    ddi_copyout(&m, data, sizeof (m), flags) < 0)
		rc = EFAULT;

	if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK) {
		ddi_fm_service_impact(cxgenp->dip, DDI_SERVICE_DEGRADED);
		rc = EFAULT;
	}

	return (rc);
}

static int
cxgen_ioctl_get_mem(p_adapter_t cxgenp, void *data, int flags)
{
	cxgen_mem_range_t mr;
	struct mc7 *mem;
	uint8_t *useraddr;
	uint64_t buf[32];
	uint32_t len, addr, chunk;
	int error, status = 0;

	if (!is_offload(cxgenp)) {
		status = ENOTSUP;
		goto get_mem_exit;
	}

	/* mem controllers needed */
	if (!(cxgenp->flags & FULL_INIT_DONE)) {
		status = EIO;
		goto get_mem_exit;
	}

	if (ddi_copyin(data, &mr, sizeof (mr), flags) < 0) {
		status = EFAULT;
		goto get_mem_exit;
	}

	len = mr.len;
	addr = mr.addr;

	if (mr.mem_id == MEM_CM) {
		mem = &cxgenp->cm;
	} else if (mr.mem_id == MEM_PMRX) {
		mem = &cxgenp->pmrx;
	} else if (mr.mem_id == MEM_PMTX) {
		mem = &cxgenp->pmtx;
	} else {
		status = EINVAL;
		goto get_mem_exit;
	}

	mr.version = 3 | (cxgenp->params.rev << 10);

	useraddr = mr.buf;
	while (len) {
		chunk = min(len, sizeof (buf));

		error = t3_mc7_bd_read(mem, addr / 8, chunk / 8, buf);
		if (error) {
			status = -error;
			goto get_mem_exit;
		}

		if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK) {
			ddi_fm_service_impact(cxgenp->dip,
			    DDI_SERVICE_DEGRADED);
			status = EFAULT;
			goto get_mem_exit;
		}

		/* copy out straight to user buffer */
		if (copyout(buf, useraddr, chunk)) {
			status = EFAULT;
			goto get_mem_exit;
		}

		useraddr += chunk;
		addr += chunk;
		len -= chunk;
	}

	/* needed only for mr.version? */
	if (ddi_copyout(&mr, data, sizeof (mr), flags) < 0)
		status = EFAULT;

get_mem_exit:
	return (status);
}

static int
cxgen_ioctl_get_context(p_adapter_t cxgenp, void *data, int flags)
{
	cxgen_get_cntxt_cmd_t ctx;
	int rc = 0;

	if (ddi_copyin(data, &ctx, sizeof (ctx), flags) < 0) {
		rc = EFAULT;
		goto get_context_exit;
	}

	/* context operations require sge lock */
	mutex_enter(&cxgenp->sge.reg_lock);

	switch (ctx.type) {
	case tunnel:
	case control:
	case command:
		rc = -t3_sge_read_ecntxt(cxgenp, ctx.id, ctx.data);
		break;
	case free_list:
		rc = -t3_sge_read_fl(cxgenp, ctx.id, ctx.data);
		break;
	case resp_queue:
		rc = -t3_sge_read_rspq(cxgenp, ctx.id, ctx.data);
		break;
	case cq:
		rc = -t3_sge_read_cq(cxgenp, ctx.id, ctx.data);
		break;
	default:
		rc = EINVAL;
		break;
	}

	mutex_exit(&cxgenp->sge.reg_lock);

	if (rc == 0 && ddi_copyout(&ctx, data, sizeof (ctx), flags) < 0)
		rc = EFAULT;

	if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK) {
		ddi_fm_service_impact(cxgenp->dip, DDI_SERVICE_DEGRADED);
		rc = EFAULT;
	}

get_context_exit:
	return (rc);
}

static int
cxgen_ioctl_get_sgedesc(p_adapter_t cxgenp, void *data, int flags)
{
	cxgen_sgedesc_t d;
	int qset, queue, rc, status = 0;

	if (ddi_copyin(data, &d, sizeof (d), flags) < 0) {
		status = EFAULT;
		goto get_sgedesc_exit;
	}

	if (d.queue_num >= SGE_QSETS * 6) {
		status = EINVAL;
		goto get_sgedesc_exit;
	}

	qset = d.queue_num / 6;
	queue = d.queue_num % 6;

	rc = t3_get_desc(&cxgenp->sge.qs[qset], queue, d.idx, d.data);
	if (rc < 0) {
		status = EINVAL;
		goto get_sgedesc_exit;
	}

	d.size = rc;

	if (ddi_copyout(&d, data, sizeof (d), flags) < 0)
		status = EFAULT;

	if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK) {
		ddi_fm_service_impact(cxgenp->dip, DDI_SERVICE_DEGRADED);
		status = EFAULT;
	}

get_sgedesc_exit:
	return (status);
}

static int
cxgen_ioctl_get_qsparams(p_adapter_t cxgenp, void *data, int flags)
{
	int i, lq;
	struct qset_params *q;
	cxgen_qsparam_t qp;
	struct port_info *pi;

	if (ddi_copyin(data, &qp, sizeof (qp), flags) < 0)
		return (EFAULT);

	/* number of qsets assigned to ports on the card */
	for (i = 0, lq = 0; i < cxgenp->params.nports; i++)
		lq += cxgenp->port[i].nqsets;

	if (qp.qset_idx >= lq)
		return (EINVAL);

	q = &cxgenp->params.sge.qset[qp.qset_idx];

	qp.rspq_size = q->rspq_size;

	qp.txq_size[0] = q->txq_size[0];
	qp.txq_size[1] = q->txq_size[1];
	qp.txq_size[2] = q->txq_size[2];

	qp.fl_size[0] = q->fl_size;
	qp.fl_size[1] = q->jumbo_size;

	qp.cong_thres = q->cong_thres;
	qp.intr_lat = cxgenp->sge.qs[qp.qset_idx].rspq.holdoff_tmr / 10;

	pi = cxgenp->sge.qs[qp.qset_idx].port;
	qp.instance = pi ? pi->port_id : -1;

	if (ddi_copyout(&qp, data, sizeof (qp), flags) < 0)
		return (EFAULT);

	return (0);
}

static inline void
reg_block_dump(p_adapter_t ap, uint8_t *buf, uint_t start, uint_t end)
{
	uint32_t *p = (void *)(buf + start);

	for (; start <= end; start += sizeof (uint32_t))
		*p++ = t3_read_reg(ap, start);
}

static int
cxgen_ioctl_regdump(p_adapter_t cxgenp, void *data, int flags)
{
	cxgen_regdump_t r;
	uint8_t *buf;
	int rc = 0;

	if (ddi_copyin(data, &r, sizeof (r), flags) < 0)
		return (EFAULT);

	if (r.len > REGDUMP_SIZE)
		r.len = REGDUMP_SIZE;
	else if (r.len < REGDUMP_SIZE)
		return (E2BIG);

	buf = kmem_zalloc(REGDUMP_SIZE, KM_SLEEP);
	if (buf == NULL)
		return (ENOMEM);

	r.version = 3 | (cxgenp->params.rev << 10) | (is_pcie(cxgenp) << 31);

	reg_block_dump(cxgenp, buf, 0, A_SG_RSPQ_CREDIT_RETURN);
	reg_block_dump(cxgenp, buf, A_SG_HI_DRB_HI_THRSH, A_ULPRX_PBL_ULIMIT);
	reg_block_dump(cxgenp, buf, A_ULPTX_CONFIG, A_MPS_INT_CAUSE);
	reg_block_dump(cxgenp, buf, A_CPL_SWITCH_CNTRL, A_CPL_MAP_TBL_DATA);
	reg_block_dump(cxgenp, buf, A_SMB_GLOBAL_TIME_CFG, A_XGM_SERDES_STAT3);
	reg_block_dump(cxgenp, buf, A_XGM_SERDES_STATUS0,
	    XGM_REG(A_XGM_SERDES_STAT3, 1));
	reg_block_dump(cxgenp, buf, XGM_REG(A_XGM_SERDES_STATUS0, 1),
	    XGM_REG(A_XGM_RX_SPI4_SOP_EOP_CNT, 1));

	if (copyout(buf, r.data, r.len) < 0)
		rc = EFAULT;

	if (rc == 0 && ddi_copyout(&r, data, sizeof (r), flags) < 0)
		rc = EFAULT;

	kmem_free(buf, REGDUMP_SIZE);

	return (rc);
}
