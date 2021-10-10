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
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/kobj.h>
#include <sys/kobj_lex.h>
#include <sys/pcie_impl.h>
#include <sys/pcie_pwr.h>
#include <sys/pciev_impl.h>
#ifdef sun4v
#include <sys/ldoms.h>
#endif

/*
 * try to determine if the dip matches the pathname in
 * pciv_dev_cache which has a link list of VF devices in them.
 * This is done by comparing the BDF of the dip to that of the device path
 * in the pciv_dev_cache.
 * pathname cannot be obtained from the dip because computaion of the
 * unit address of the last component of the pathname requires the
 * knowledge if the device is a VF or not.
 * The followimg logic is used to get the BDF from the pathname in
 * pciv_dev_cache.
 *
 * First match the pathname of the parent of our dip to the pciv_dev_cache
 * pathname. If they match then obtain the secbus# of the dip.
 * Next get the func# of the last component of the pathname in pciv_dev_cache
 * BDF of the pathname is (secbus# <<8) + func#
 */
boolean_t
pcie_is_vf(dev_info_t *dip)
{
	pcie_req_id_t	bdf;
	dev_info_t	*parent;
	char		*parent_path, *leaf_path, *funcp;
	pciv_assigned_dev_t *devc = pciv_dev_cache;
	int		func, secbus;
	char		scratch_buf[16];
	u_longlong_t	value = 0;

	if (pcie_get_bdf_from_dip(dip, &bdf) != DDI_SUCCESS)
		return (B_FALSE);
	parent = ddi_get_parent(dip);
	parent_path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	(void) pcie_pathname(parent, parent_path);
	while ((devc != NULL) && (devc->type == FUNC_TYPE_VF)) {
		if (strncmp(devc->devpath, parent_path, strlen(parent_path))
		    == 0) {
			/*
			 * parent matched
			 * Now compute the bdf of the devc->devpath
			 */
			secbus = pcie_get_secbus(dip);
			leaf_path = devc->devpath + strlen(parent_path);
			funcp = strstr(leaf_path, "@0,");
			if (funcp == NULL)
				continue;
			funcp += 3;
			(void) strcpy(scratch_buf, "0x");
			(void) strncpy(scratch_buf + 2, funcp,
			    sizeof (scratch_buf) - 2);
			(void) kobj_getvalue(scratch_buf, &value);
			func = (int)value;
			if (bdf == ((secbus << 8) + func)) {
				kmem_free(parent_path, MAXPATHLEN);
				return (B_TRUE);
			}
		}
		devc = devc->next;
	}
	kmem_free(parent_path, MAXPATHLEN);
	return (B_FALSE);
}

void
pcie_init_plat(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);

	if (PCIE_IS_PCIE_BDG(bus_p)) {
		bus_p->bus_pcie2pci_secbus = bus_p->bus_bdg_secbus;
	} else {
		dev_info_t *pdip;

		for (pdip = ddi_get_parent(dip); pdip;
		    pdip = ddi_get_parent(pdip)) {
			pcie_bus_t *parent_bus_p = PCIE_DIP2BUS(pdip);

			if (parent_bus_p->bus_pcie2pci_secbus) {
				bus_p->bus_pcie2pci_secbus =
				    parent_bus_p->bus_pcie2pci_secbus;
				break;
			}
			if (PCIE_IS_ROOT(parent_bus_p))
				break;
		}
	}
}

void
pcie_fini_plat(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);

	if (PCIE_IS_PCIE_BDG(bus_p))
		bus_p->bus_pcie2pci_secbus = 0;
}

int
pcie_plat_pwr_setup(dev_info_t *dip)
{
	if (ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
	    "pm-want-child-notification?", NULL, NULL) != DDI_PROP_SUCCESS) {
		PCIE_DBG("%s(%d): can't create pm-want-child-notification \n",
		    ddi_driver_name(dip), ddi_get_instance(dip));
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

/*
 * Undo whatever is done in pcie_plat_pwr_common_setup
 */
void
pcie_plat_pwr_teardown(dev_info_t *dip)
{
	(void) ddi_prop_remove(DDI_DEV_T_NONE, dip,
	    "pm-want-child-notification?");
}

/* Create per Root Complex taskq */
/* ARGSUSED */
int
pcie_rc_taskq_create(dev_info_t *dip)
{
#ifdef sun4v
	uint32_t	rc_addr;

	if ((rc_addr = pciv_get_rc_addr(dip)) == PCIV_INVAL_RC_ADDR) {
		PCIE_DBG("%s(%d): Can not get RC address\n",
		    ddi_driver_name(dip), ddi_get_instance(dip));
		return (DDI_EINVAL);
	}

	if (pciv_tx_taskq_create(dip, rc_addr, B_TRUE) != DDI_SUCCESS) {
		PCIE_DBG("%s(%d): pciv_tx_taskq_create failed, rc_addr %d\n",
		    ddi_driver_name(dip), ddi_get_instance(dip), rc_addr);
		return (DDI_FAILURE);
	}

	if (domaining_supported()) {
		if (pciv_tx_taskq_create(dip, rc_addr, B_FALSE)
		    != DDI_SUCCESS) {
			PCIE_DBG("%s(%d): pciv_tx_taskq_create failed, "
			    "rc_addr %d\n", ddi_driver_name(dip),
			    ddi_get_instance(dip), rc_addr);
			return (DDI_FAILURE);
		}
	}
#endif
	return (DDI_SUCCESS);
}

/* Destroy per Root Complex taskq */
/* ARGSUSED */
void
pcie_rc_taskq_destroy(dev_info_t *dip)
{
#ifdef sun4v
	pciv_tx_taskq_destroy(dip);
#endif
}

boolean_t
pcie_plat_rbl_enabled()
{
#ifdef sun4v
	/*
	 * sun4v hypervisor currently keeps track of device
	 * bus numbers and BAR addresses, thus resource re-
	 * balance is not enabled.
	 */
	return (B_FALSE);
#else
	return (B_TRUE);
#endif
}

void
pcie_plat_fastreboot_disable()
{
}
