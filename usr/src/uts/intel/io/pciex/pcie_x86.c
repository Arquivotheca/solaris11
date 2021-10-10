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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/kmem.h>
#include <sys/sysmacros.h>
#include <sys/sunddi.h>
#include <sys/sunpm.h>
#include <sys/epm.h>
#include <sys/sunndi.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddi_implfuncs.h>
#include <sys/pcie.h>
#include <sys/pcie_impl.h>
#include <sys/pcie_pwr.h>
#include <sys/pcie_acpi.h>	/* pcie_x86_priv_t */
#include <sys/fastboot_impl.h>

/* ARGSUSED */
boolean_t
pcie_is_vf(dev_info_t *dip)
{
	return (B_FALSE);
}

void
pcie_init_plat(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	bus_p->bus_plat_private =
	    (pcie_x86_priv_t *)kmem_zalloc(sizeof (pcie_x86_priv_t), KM_SLEEP);
}

void
pcie_fini_plat(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);

	kmem_free(bus_p->bus_plat_private, sizeof (pcie_x86_priv_t));
}

/* ARGSUSED */
int
pcie_plat_pwr_setup(dev_info_t *dip)
{
	return (DDI_SUCCESS);
}

/*
 * Undo whatever is done in pcie_plat_pwr_common_setup
 */
/* ARGSUSED */
void
pcie_plat_pwr_teardown(dev_info_t *dip)
{
}


/* Create per Root Complex taskq */
int
pcie_rc_taskq_create(dev_info_t *dip)
{
	uint32_t	rc_addr;

	if ((rc_addr = pciv_get_rc_addr(dip)) == PCIV_INVAL_RC_ADDR) {
		PCIE_DBG("%s(%d): Can not get RC address\n",
		    ddi_driver_name(dip), ddi_get_instance(dip));
		return (DDI_EINVAL);
	}

	/* Currently x86 only supports loopback mode */
	if (pciv_tx_taskq_create(dip, rc_addr, B_TRUE) != DDI_SUCCESS) {
		PCIE_DBG("%s(%d): pciv_tx_taskq_create failed, rc_addr %d\n",
		    ddi_driver_name(dip), ddi_get_instance(dip), rc_addr);
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

/* Destroy per Root Complex taskq */
void
pcie_rc_taskq_destroy(dev_info_t *dip)
{
	pciv_tx_taskq_destroy(dip);
}

boolean_t
pcie_plat_rbl_enabled()
{
	return (B_TRUE);
}

void
pcie_plat_fastreboot_disable()
{
	fastreboot_disable(FBNS_FMAHWERR);
}
