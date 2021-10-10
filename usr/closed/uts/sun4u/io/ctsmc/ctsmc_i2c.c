/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * ctsmc_i2c - I2c Interface for System Management Controller Driver
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/cred.h>
#include <sys/log.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/kmem.h>

#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/strsun.h>
#include <sys/poll.h>

#include <sys/debug.h>

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>

#include <sys/inttypes.h>
#include <sys/ksynch.h>

#include <sys/ctsmc_debug.h>
#include <sys/ctsmc.h>

extern int ctsmc_command_send(ctsmc_state_t *ctsmc, uint8_t cmd,
		uchar_t *inbuf, uint8_t	inlen, uchar_t	*outbuf,
		uint8_t	*outlen);
/*
 * Send a message contained in in-buf to SMC. This will
 * be used for sending commands to SMC in scnarios where we
 * need to poll for the response synchronously
 */
static int
ctsmc_cmd_send0(ctsmc_state_t *ctsmc,
		uchar_t op,
		uchar_t *in,
		uchar_t dlength,
		uchar_t *out,
		uchar_t olength)
{
	int result;
	uint8_t length;
#ifdef	DEBUG
	int i;
	SMC_DEBUG2(SMC_SEND_DEBUG, "inl %d outl %d", dlength, olength);
	for (i = 0; i < dlength; i++)
		SMC_DEBUG(SMC_SEND_DEBUG, "0x%x", in[i]);
#endif	/* DEBUG */
	length = olength;
	result = ctsmc_command_send(ctsmc, op, in, dlength, out, &length);
	if (result == SMC_SUCCESS) {
		SMC_DEBUG(SMC_SEND_DEBUG, "ctsmc_command_send0 SUCCESS %d",
				length);
#ifdef	DEBUG
		for (i = 0; i < length; i++) {
			if (out) {
				SMC_DEBUG(SMC_SEND_DEBUG, "0x%x", out[i]);
			}
		}
#endif	/* DEBUG */
		if (olength != length)
			return (SMC_FAILURE);
		return (SMC_SUCCESS);
	} else {
		return (SMC_FAILURE);
	}
}

/*
 * ctsmc_i2c_acquire() is called by a thread wishing
 * to "own" the I2C bus.
 * It should not be held across multiple transfers.
 */
static void
ctsmc_i2c_acquire(ctsmc_i2c_state_t *i2c,
			dev_info_t *dip, i2c_transfer_t *tp)
{
	SMC_DEBUG0(SMC_I2C_DEBUG, "ctsmc_i2c_acquire");
	mutex_enter(&i2c->ctsmc_i2c_mutex);
	while (i2c->ctsmc_i2c_busy) {
		cv_wait(&i2c->ctsmc_i2c_cv, &i2c->ctsmc_i2c_mutex);
	}
	i2c->ctsmc_i2c_busy = 1;
	i2c->ctsmc_i2c_cur_tran = tp;
	i2c->ctsmc_i2c_cur_dip = dip;
	mutex_exit(&i2c->ctsmc_i2c_mutex);
}

/*
 * ctsmc_i2c_release() is called to release a hold made
 * by ctsmc_i2c_acquire().
 */
static void
ctsmc_i2c_release(ctsmc_i2c_state_t *i2c)
{
	SMC_DEBUG0(SMC_I2C_DEBUG, "ctsmc_i2c_release");
	mutex_enter(&i2c->ctsmc_i2c_mutex);
	i2c->ctsmc_i2c_busy = 0;
	i2c->ctsmc_i2c_cur_tran = NULL;
	i2c->ctsmc_i2c_cur_dip = NULL;
	cv_signal(&i2c->ctsmc_i2c_cv);
	mutex_exit(&i2c->ctsmc_i2c_mutex);
}

/*
 * i2_nexus_dip_to_bus() takes a dip and returns an I2C bus.
 */
static uchar_t
ctsmc_i2c_dip_to_bus(dev_info_t *dip)
{
	ctsmc_i2c_ppvt_t		*ppvt;
	SMC_DEBUG0(SMC_I2C_DEBUG, "ctsmc_i2c_dip_to_bus");
	ppvt = ddi_get_parent_data(dip);
	return (ppvt->ctsmc_i2c_bus);
}

/*
 * i2_nexus_dip_to_addr() takes a dip and returns an I2C address.
 */
static uchar_t
ctsmc_i2c_dip_to_addr(dev_info_t *dip)
{
	ctsmc_i2c_ppvt_t		*ppvt;
	SMC_DEBUG0(SMC_I2C_DEBUG, "ctsmc_i2c_dip_to_addr");
	ppvt = ddi_get_parent_data(dip);
	return (ppvt->ctsmc_i2c_addr);
}

static uchar_t
ctsmc_i2c_rdwr(dev_info_t *dip, ctsmc_state_t *ctsmc,
			i2c_transfer_t *tp)
{
	uchar_t			dlength;
	uchar_t			inlength;
	ctsmc_i2c_wr_t		i2c_pkt;
	uchar_t			rd_data[SMC_MAX_DATA];
	uchar_t			cc;
	int i, ret = SMC_SUCCESS;

	SMC_DEBUG0(SMC_I2C_DEBUG, "ctsmc_i2c_rdwr");
	SMC_DEBUG(SMC_I2C_DEBUG, "bus Id %d", ctsmc_i2c_dip_to_bus(dip));

	i2c_pkt.head.ctsmc_i2c_i2caddr = ctsmc_i2c_dip_to_addr(dip);

	i2c_pkt.head.ctsmc_i2c_busid = SMC_I2C_PRIV_BUSTYPE;

	cc = ctsmc_i2c_dip_to_bus(dip);
	if (cc == 0)
		i2c_pkt.head.ctsmc_i2c_busid |= SMC_I2C_BP_BUSID;
	else
		i2c_pkt.head.ctsmc_i2c_busid |= SMC_I2C_LOC_BUSID;

	dlength = tp->i2c_wlen;
	if (dlength) {
		for (i = 0; i < dlength; ++i)
			i2c_pkt.ctsmc_i2c_data[i] = tp->i2c_wbuf[i];
	}
	inlength = tp->i2c_wlen + sizeof (i2c_pkt.head);

	/*
	 * Check to see it is write/read or just a read
	 */
	i2c_pkt.head.ctsmc_i2c_rdcount = tp->i2c_rlen;
	dlength = i2c_pkt.head.ctsmc_i2c_rdcount;

	ret = ctsmc_cmd_send0(ctsmc, SMC_I2C_WR_RD, (uchar_t *)&(i2c_pkt.head),
		inlength, (uchar_t *)(rd_data), dlength);
	if (ret != SMC_SUCCESS) {
		return (ret);	/* read error detected */
	}
	if (dlength > 0)
		bcopy(rd_data, tp->i2c_rbuf, dlength);

	return (SMC_SUCCESS);
}

/*
 * ctsmc_i2c_transfer routine
 */
static int
ctsmc_i2c_transfer(dev_info_t *dip, i2c_transfer_t *tp)
{
	ctsmc_state_t	*ctsmc;
	dev_info_t	*prnt_dip;
	uchar_t		cc;
	int		unit;

	SMC_DEBUG0(SMC_I2C_DEBUG, "I2C NEXUS ctsmc_i2c_transfer");
	prnt_dip = ddi_get_parent(dip);
	if (prnt_dip == NULL) {
		cmn_err(CE_WARN, "ctsmc_i2c_transfer%p:"
			"cannot get soft state", (void *)prnt_dip);
		return (I2C_FAILURE);
	}

	unit = ddi_get_instance(prnt_dip);
	ctsmc = ctsmc_get_soft_state(unit);

	if (ctsmc == NULL) {
		cmn_err(CE_WARN, "ctsmc_i2c_transfer%d:"
			"cannot get soft state", unit);
		return (I2C_FAILURE);
	}

	if (tp == NULL) {
		SMC_DEBUG0(SMC_I2C_DEBUG, "ctsmc_i2c_transfer():"
			"i2c_transfer_t *tp = NULL");
		return (I2C_FAILURE);
	}
	if (tp->i2c_wlen > SMC_MAX_DATA || tp->i2c_rlen > SMC_MAX_DATA) {
		SMC_DEBUG(SMC_I2C_DEBUG, "ctsmc_i2c_transfer(): write"
			"rdwr_count > %d", SMC_MAX_DATA);
		return (I2C_FAILURE);
	}
	ctsmc_i2c_acquire(ctsmc->ctsmc_i2c, dip, tp);
	tp->i2c_r_resid = tp->i2c_rlen;
	tp->i2c_w_resid = tp->i2c_wlen;
	tp->i2c_result = I2C_SUCCESS;

	switch (tp->i2c_flags) {
	case I2C_WR:
		SMC_DEBUG(SMC_I2C_DEBUG, "i2c_trans: write op lf_dev:  %s",
			ddi_node_name(dip));

		if (tp->i2c_wlen < 1) {
			cmn_err(CE_WARN, "ctsmc_i2c_transfer():"
				"write op fail: i2c_wlen = 0x%x",
					tp->i2c_wlen);
			tp->i2c_result = I2C_FAILURE;
			break;
		}
		cc = ctsmc_i2c_rdwr(dip, ctsmc, tp);
		if (cc != SMC_SUCCESS) {
			/*
			 * write error detected
			 */
			tp->i2c_result = I2C_FAILURE;
		}
		break;

	case I2C_RD:
		SMC_DEBUG(SMC_I2C_DEBUG, "i2c_trans: read op lf_dev:  %s",
			ddi_node_name(dip));
		if (tp->i2c_rlen < 1) {
			cmn_err(CE_WARN, "ctsmc_i2c_transfer():"
				"read op fail: i2c_rlen = 0x%x",
					tp->i2c_rlen);
			tp->i2c_result = I2C_FAILURE;
			break;
		}
		cc = ctsmc_i2c_rdwr(dip, ctsmc, tp);
		if (cc != SMC_SUCCESS) {
			/*
			 * read error detected
			 */
			tp->i2c_result = I2C_FAILURE;
		}
		break;

	case I2C_WR_RD:
		SMC_DEBUG(SMC_I2C_DEBUG, "i2c_trans: wrt/rd op lf_dev:  %s",
			ddi_node_name(dip));
		if (tp->i2c_rlen < 1) {
			cmn_err(CE_WARN, "ctsmc_i2c_transfer():"
				"wrt/rd op fail: i2c_rlen = 0x%x",
					tp->i2c_rlen);
			tp->i2c_result = I2C_FAILURE;
			break;
		}
		if (tp->i2c_wlen < 1) {
			cmn_err(CE_WARN, "ctsmc_i2c_transfer():"
				"wrt/rd op fail: i2c_wlen = 0x%x",
					tp->i2c_wlen);
			tp->i2c_result = I2C_FAILURE;
			break;
		}

		cc = ctsmc_i2c_rdwr(dip, ctsmc, tp);
		if (cc != SMC_SUCCESS) {
			/*
			 * write error detected
			 */
			tp->i2c_result = I2C_FAILURE;
		}
		break;

	default:
		cmn_err(CE_WARN, "ctsmc_i2c_transfer():i2c_wlen 0x%x "
			"i2c_rlen 0x%x, Command not supported.",
				tp->i2c_wlen, tp->i2c_rlen);
		tp->i2c_result =  I2C_FAILURE;
	}

	ctsmc_i2c_release(ctsmc->ctsmc_i2c);
	return (tp->i2c_result);	/* no error detected */
}

/*
 * ctsmc_i2c_attach is called in ctsmc driver attach routine
 * intializing the state of i2c interface
 */
int
ctsmc_i2c_attach(ctsmc_state_t *ctsmc, dev_info_t *dip)
{
	ctsmc_i2c_state_t	*i2c;
	i2c_nexus_reg_t ctsmc_i2c_regvec = {
		I2C_NEXUS_REV,
		ctsmc_i2c_transfer,
		};

	SMC_DEBUG2(SMC_I2C_DEBUG, "I2C NEXUS ctsmc_i2c_attach: "
			"Node %s%d", ddi_node_name(dip),
			ddi_get_instance(dip));
	i2c = NEW(1, ctsmc_i2c_state_t);
	i2c->ctsmc_i2c_busy = 0;

	ctsmc->ctsmc_i2c = (void *) i2c;

	mutex_init(&i2c->ctsmc_i2c_mutex, NULL, MUTEX_DRIVER, NULL);
	cv_init(&i2c->ctsmc_i2c_cv, NULL, CV_DRIVER, NULL);

	i2c_nexus_register(dip, &ctsmc_i2c_regvec);
	i2c->ctsmc_i2c_attachflags |= NEXUS_REGISTER;

	return (DDI_SUCCESS);
}

/*
 * ctsmc_i2c_detach() is call by ctsmc detach routine
 */
void
ctsmc_i2c_detach(ctsmc_state_t *ctsmc, dev_info_t *dip)
{
	ctsmc_i2c_state_t	*i2c;
	SMC_DEBUG2(SMC_I2C_DEBUG, "I2C NEXUS ctsmc_i2c_detach: "
			"Node %s%d", ddi_node_name(dip),
			ddi_get_instance(dip));
	i2c = ctsmc->ctsmc_i2c;
	if ((i2c->ctsmc_i2c_attachflags & NEXUS_REGISTER) != 0) {
		i2c_nexus_unregister(dip);
	}
	cv_destroy(&i2c->ctsmc_i2c_cv);
	mutex_destroy(&i2c->ctsmc_i2c_mutex);
	FREE(i2c, 1, ctsmc_i2c_state_t);
	ctsmc->ctsmc_i2c = NULL;
}

int
ctsmc_i2c_initchild(dev_info_t *cdip)
{
	ctsmc_i2c_ppvt_t		*ppvt;
	int32_t			cell_size;
	int			len;
	int32_t			regs[2];
	int			err;
	char			name[30];

	SMC_DEBUG2(SMC_I2C_DEBUG, "ctsmc_i2c_initchild: %s%d",
			ddi_node_name(cdip), ddi_get_instance(cdip));

	ppvt = NEW(1, ctsmc_i2c_ppvt_t);

	len = sizeof (cell_size);
	err = ddi_getlongprop_buf(DDI_DEV_T_ANY, cdip,
		DDI_PROP_CANSLEEP, "#address-cells",
		(caddr_t)&cell_size, &len);
	if (err != DDI_PROP_SUCCESS) {
		cmn_err(CE_WARN, "ctsmc_i2c_initchild: No #address-cells");
		cell_size = 2;
	}
	len = sizeof (regs);
	err = ddi_getlongprop_buf(DDI_DEV_T_ANY, cdip,
		DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP,
		"reg", (caddr_t)regs, &len);

	if (err != DDI_PROP_SUCCESS ||
		len != (cell_size * sizeof (int32_t))) {
		return (DDI_FAILURE);
	}

	if (cell_size == 1) {
		ppvt->ctsmc_i2c_addr = regs[0];
		(void) sprintf(name, "%x", regs[0]);
	} else if (cell_size == 2) {
		ppvt->ctsmc_i2c_bus = regs[0];
		ppvt->ctsmc_i2c_addr = regs[1];
		(void) sprintf(name, "%x,%x", regs[0], regs[1]);
	} else {
		return (DDI_FAILURE);
	}

	ddi_set_parent_data(cdip, ppvt);

	ddi_set_name_addr(cdip, name);

	return (DDI_SUCCESS);
}

void
ctsmc_i2c_uninitchild(dev_info_t *cdip)
{
	ctsmc_i2c_ppvt_t		*ppvt;
	SMC_DEBUG0(SMC_I2C_DEBUG, "ctsmc_i2c_uninitchild");
	ppvt = ddi_get_parent_data(cdip);

	FREE(ppvt, 1, ctsmc_i2c_ppvt_t);

	ddi_set_parent_data(cdip, NULL);
	ddi_set_name_addr(cdip, NULL);
}

void
ctsmc_i2c_reportdev(dev_info_t *dip, dev_info_t *rdip)
{
	ctsmc_i2c_ppvt_t		*ppvt;

	SMC_DEBUG0(SMC_I2C_DEBUG, "ctsmc_i2c_reportdev");
	ppvt = ddi_get_parent_data(rdip);
	cmn_err(CE_CONT, "?%s%d at %s%d: addr 0x%x",
		ddi_driver_name(rdip), ddi_get_instance(rdip),
		ddi_driver_name(dip), ddi_get_instance(dip),
		ppvt->ctsmc_i2c_addr);
}
