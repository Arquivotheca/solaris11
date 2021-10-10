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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * File:   pcirm.c
 * Functions for all the interfaces exported by pcirm module, including
 * allocate/free and also rebalance interfaces.
 */
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/pctypes.h>
#include <sys/modctl.h>
#include <sys/debug.h>
#include <sys/spl.h>
#include <sys/pci.h>
#include <sys/autoconf.h>
#include <sys/memlist.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/note.h>
#include <sys/sdt.h>
#include <sys/pcirm.h>
#include <sys/pcirm_impl.h>

int pcirm_debug = 0;
#define	DEBUGPRT \
	if (pcirm_debug) cmn_err

/*
 * Interface for allocating resource.
 *
 * Allocate resource for device "dip" from its parent "pdip".
 *
 * This function allocates specified resource from node pdip to node dip,
 * during the allocation it will update pdip's properties to remove the
 * resource, and also dip's properties to add this resource in.
 *
 * If dip is NULL, this function only allocates the resource from pdip,
 * and update pdip's properties; otherwise pdip must be dip's immediate
 * parent.
 *
 * Note: the argument dip is allowed to be NULL for two reasons:
 *   1. Keep compliant with busra interface ndi_ra_alloc(). So that the existing
 *	consumers (pcicfg, cardbus modules) of busra can be easily ported to
 *	new PCIRM interfaces with low risk.
 *   2. Enable the feature that pcirm_alloc_resource() can be used to remove
 *	resource from a given pdip without impacting any other (child) dips.
 *
 * If the req can be satifised, this function returns success and the allocated
 * resource is returned via "allocated" parameter; otherwise, it will return
 * failure, and nothing will be changed in the device tree.
 */
int
pcirm_alloc_resource(dev_info_t *dip, dev_info_t *pdip, pcirm_req_t *req,
    pcirm_res_t *allocated)
{
	pcirm_type_t type;
	uint64_t base, len;
	int r;

	ASSERT(req && pdip && (!dip || ddi_get_parent(dip) == pdip));

	if (pcirm_get_resource(pdip, req, allocated)) {
		/*
		 * No resource are found for this piece of req,
		 * free resource list and return.
		 */
		DEBUGPRT(CE_WARN, "pcirm_alloc_resource: "
		    "failed to get resource from dip %p, "
		    "type=%x, len=%" PRIx64 ", flags=%x \n",
		    (void *)pdip, req->type, req->len, req->flags);
		return (PCIRM_FAILURE);
	}

	type = allocated->type;
	base = allocated->base;
	len = allocated->len;

	if (type == PCIRM_TYPE_BUS) {
		r = dip ? pcirm_prop_add_bus_range(dip, base, len) :
		    PCIRM_SUCCESS;
		goto done;
	}

	/* remove resource from parent dip */
	if (r = pcirm_prop_rm_addr(pdip, "available", base, len, type))
		goto done;

	/* add the allocated resource to dip */
	if (dip && pcirm_prop_add_assigned(dip, base, len, type)) {
		if (pcie_is_pci_bridge(dip) &&
		    !pcirm_prop_add_ranges(dip, base, len, type)) {
			if (!pcirm_prop_add_addr(dip, "available",
			    base, len, type)) {
				r = PCIRM_SUCCESS;
				goto done;
			}
			(void) pcirm_prop_rm_ranges(dip, base, len, type);
		}
		DEBUGPRT(CE_WARN, "Resource allocated from %p "
		    "but cannot be put to device %p !\n",
		    (void *)pdip, (void *)dip);
		(void) pcirm_prop_add_addr(pdip, "available", base, len, type);
		r = PCIRM_FAILURE;
	}

done:
	return (r ? PCIRM_FAILURE : PCIRM_SUCCESS);
}

/*
 * Free a piece of resource from dip to its parent pdip. Both dip and pdip's
 * properties will be updated to reflect that the resource has been removed
 * from dip to pdip.
 *
 * If dip is NULL, this function frees the resource to pdip, and update
 * pdip's properties to add the resource; otherwise pdip must be dip's
 * immediate parent.
 *
 * Note: the pdip argument is kept for two reasons:
 *   1. Be compatible to the legacy busra ndi_ra_free() interface so busra
 *      consumers (pcicfg, cardbus) can be adapted.
 *   2. Allow resources to be given to a pdip without involing any of its
 *      children.
 */
int
pcirm_free_resource(dev_info_t *dip, dev_info_t *pdip,
    pcirm_res_t *resource)
{
	pcirm_type_t type = resource->type;
	uint64_t base = resource->base;
	uint64_t len = resource->len;
	boolean_t is_assigned;

	ASSERT(resource && pdip && (!dip || ddi_get_parent(dip) == pdip));

	if (type == PCIRM_TYPE_BUS) {
		if (dip && pcirm_prop_rm_bus_range(dip, base, len))
			return (PCIRM_FAILURE);

		/*
		 * Update the parent's bus-range with the freed resource.
		 */
		if (pcirm_prop_add_bus_range(pdip, base, len)) {
			/* add back the resource */
			(void) pcirm_prop_add_bus_range(dip, base, len);
			return (PCIRM_FAILURE);
		}

		return (PCIRM_SUCCESS);
	}

	if (!dip)
		goto add_resource;

	/*
	 * For IO/MEM/PMEM, remove the resource from the property.
	 * It might be in "assigned-addresses" or "ranges" property.
	 * Check "assigned-addresses" first.
	 */
	if (!pcirm_prop_rm_addr(dip, "assigned-addresses",
	    base, len, type)) {
		is_assigned = B_TRUE;
		goto add_resource;
	}

	if (!pcie_is_pci_bridge(dip))
		return (PCIRM_FAILURE);
	/*
	 * Now we know it might be a bridge and the resource does not
	 * belong to "assigned-addresses" property. It might belong to
	 * "ranges" property.
	 */
	/*
	 * Check alignment first according to the ailgnment requirement
	 * in pci-pci bridge spec
	 */
	if (type == PCIRM_TYPE_IO) {
		if (base % PPB_ALIGNMENT_IO || len % PPB_ALIGNMENT_IO) {
			/* base or len is not aligned to 4K */
			return (PCIRM_FAILURE);
		}
	} else {
		if (base % PPB_ALIGNMENT_MEM || len % PPB_ALIGNMENT_MEM) {
			/* base or len is not aligned to 1MB */
			return (PCIRM_FAILURE);
		}
	}

	if (!pcirm_within_range(dip, base, len, type)) {
		/* no such address to free */
		return (PCIRM_FAILURE);
	}
	if (pcirm_prop_rm_ranges(dip, base, len, type)) {
		return (PCIRM_FAILURE);
	}

add_resource:
	/* Just free the resource to pdip */
	if (pcirm_prop_add_addr(pdip, "available", base, len, type)) {
		if (is_assigned) {
			(void) pcirm_prop_add_addr(dip, "assigned-addresses",
			    base, len, type);
		} else {
			(void) pcirm_prop_add_ranges(dip, base, len, type);
		}
		return (PCIRM_FAILURE);
	}

	/*
	 * Update the "available" property for removing the allocated
	 * resource from parents.
	 */
	(void) pcirm_prop_merge_available(pdip, type);
	return (PCIRM_SUCCESS);
}

/*
 * Free all the resource (IO/MEM/PMEM/Busnum) of a device node to its parent.
 */
int
pcirm_free_resource_all(dev_info_t *dip)
{
	pci_regspec_t *regs = NULL;
	ppb_ranges_t *ranges = NULL;
	int rlen, rcount, i, rval;
	pcirm_res_t resource;

	rval = ndi_prop_remove(DDI_DEV_T_NONE, dip, "bus-range");
	if (rval != DDI_SUCCESS && rval != DDI_PROP_NOT_FOUND) {
		/* There might be a "bus-range" there but can not read it */
		return (PCIRM_FAILURE);
	}

	/*
	 * read the "assigned-addresses" property and then free its resource
	 * to parent's "available" property.
	 */
	rval = ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "assigned-addresses", (caddr_t)&regs, &rlen);
	if (rval != DDI_SUCCESS && rval != DDI_PROP_NOT_FOUND) {
		return (PCIRM_FAILURE);
	}
	if (rval == DDI_SUCCESS) {
		/*
		 * Find in the assigned-addresses regs for
		 * (base, len, type), then
		 * update the parent's "available" property.
		 */
		rcount = rlen / sizeof (pci_regspec_t);
		for (i = 0; i < rcount; i++) {
			resource.type = PCIRM_REQTYPE(regs[i].pci_phys_hi);
			resource.base = REG_TO_BASE(regs[i]);
			resource.len = REG_TO_SIZE(regs[i]);
			rval = pcirm_free_resource(dip, NULL, &resource);
			if (rval != PCIRM_SUCCESS) {
				kmem_free(regs, rlen);
				return (rval);
			}
		}
		kmem_free(regs, rlen);
	}

	/* read the "ranges" property and free the resources */
	rval = ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS |
	    DDI_PROP_NOTPROM, "ranges", (caddr_t)&ranges, &rlen);
	if (rval != DDI_SUCCESS && rval != DDI_PROP_NOT_FOUND) {
		return (PCIRM_FAILURE);
	}
	if (rval == DDI_SUCCESS) {
		/*
		 * Find in the assigned-addresses regs for
		 * (base, len, type), then
		 * update the parent's "available" property.
		 */
		rcount = rlen / sizeof (ppb_ranges_t);
		for (i = 0; i < rcount; i++) {
			resource.type = PCIRM_RESOURCETYPE(
			    ranges[i].child_high);
			resource.base = RANGES_TO_BASE(ranges[i]);
			resource.len = RANGES_TO_SIZE(ranges[i]);
			rval = pcirm_free_resource(dip, NULL, &resource);
			if (rval != PCIRM_SUCCESS) {
				kmem_free(regs, rlen);
				return (rval);
			}
		}
		kmem_free(ranges, rlen);
	}

	return (PCIRM_SUCCESS);
}

/*
 * Rebalance interfaces
 */

/*
 * Firstly, it calculates the total resource requirements according to
 * properties of all devices under "dip".
 *
 * According to the total resource requirements, this function tries to find
 * the resource for the devices under "dip".
 *
 * If there are enough resource available from "dip", then return
 * PCIRM_SUCCESS. No rebalance is needed in this case.
 *
 * Otherwise, it then tries to calculates a re-balance map, and setup the map
 * information in the impacted device nodes.
 * If enough resources are found by doing rebalance, it returns
 * PCIRM_SUCCESS with a non-NULL handle. The handle can then
 * be used to get the rebalance map and allocate resources for devices
 * under dip. Otherwise, it returns PCIRM_FAILURE with a NULL handle.
 */
int
pcirm_find_resource(dev_info_t *dip, uint_t dev_num, int types,
    pcirm_handle_t *handle)
{
	return (pcirm_find_resource_impl(dip, dev_num, types,
	    (pcirm_handle_impl_t **)handle));
}

/*
 * Walk all the nodes in the whole re-balance map
 */
int
pcirm_walk_resource_map(pcirm_handle_t handle,
    int (*func)(dev_info_t *, pcirm_rbl_map_t *, void *), void *arg)
{
	pcirm_handle_impl_t *hdim = (pcirm_handle_impl_t *)handle;
	dev_info_t *head;
	pcirm_rbl_map_t *rbl;
	int rval;

	head = hdim->rbl_head;
	for (; head != NULL; head = DIP2RBL(head)->next) {
		rbl = DIP2RBL(head)->pci_rbl_map;
		rval = func(head, rbl, arg);
		if (rval != DDI_WALK_CONTINUE) {
			DEBUGPRT(CE_WARN, "pcirm_walk_resource_map: "
			    "walk failed: dip=%p, rbl=%p \n",
			    (void *)head, (void *)rbl);

			return (PCIRM_FAILURE);
		}
	}

	return (PCIRM_SUCCESS);
}

/*
 * Commit resource updates for all devices included in this session.
 */
int
pcirm_commit_resource(pcirm_handle_t handle)
{
	pcirm_handle_impl_t *hdim = (pcirm_handle_impl_t *)handle;
	dev_info_t *head, *head_tmp;
	pcirm_rbl_map_t *rbl, *rbl_tmp;
	pcirm_rbl_map_t map;

	for (head = hdim->rbl_head; head != NULL; head = DIP2RBL(head)->next) {
		rbl = DIP2RBL(head)->pci_rbl_map;
		for (; rbl != NULL; rbl = rbl->next) {
			if (pcirm_rbl_update_property(head, rbl)
			    != PCIRM_SUCCESS) {
				/* This should not fail */
				goto undo;
			}
		}
		pcirm_update_bus_props(head);
	}

	/* Update "available" property for the related devices */
	head = hdim->rbl_head;
	if (head) {
		if (hdim->types & PCIRM_TYPE_IO_M)
			pcirm_update_avail_props(head, PCIRM_TYPE_IO);
		if (hdim->types & PCIRM_TYPE_MEM_M)
			pcirm_update_avail_props(head, PCIRM_TYPE_MEM);
		if (hdim->types & PCIRM_TYPE_PMEM)
			pcirm_update_avail_props(head, PCIRM_TYPE_PMEM);
	}

	return (PCIRM_SUCCESS);

undo:
	cmn_err(CE_CONT, "Resource rebalance failed: unable to "
	    "update property for node dip=%p, rbl=%p \n",
	    (void *)head, (void *)rbl);

	/* Undo the property changes if property update fails */
	head_tmp = head;
	rbl_tmp = rbl;
	for (head = hdim->rbl_head; head != NULL; head = DIP2RBL(head)->next) {
		rbl = DIP2RBL(head)->pci_rbl_map;
		for (; rbl != NULL; rbl = rbl->next) {
			if (head == head_tmp && rbl == rbl_tmp)
				goto undo_done;

			map = *rbl;
			map.base = rbl->new_base;
			map.len = rbl->new_len;
			map.new_base = rbl->base;
			map.new_len = rbl->len;
			(void) pcirm_rbl_update_property(head, &map);

		}
		pcirm_update_bus_props(head);
	}
undo_done:
	return (PCIRM_FAILURE);
}

/*
 * End the re-balance session, cleanup everything.
 */
int
pcirm_free_handle(pcirm_handle_t handle)
{
	return (pcirm_free_handle_impl((pcirm_handle_impl_t *)handle));
}
