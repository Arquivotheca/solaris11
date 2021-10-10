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

/*
 * File:   pcirm_impl.c
 * Functions for all the internal routines of pcirm module, including
 * allocate/free and also rebalance routines.
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
#include <sys/pcie.h>
#include <sys/pcie_impl.h>
#include <sys/pci_cap.h>
#include <sys/pci_cfgacc.h>
#include <sys/pcirm.h>
#include <sys/pcirm_impl.h>
#include <sys/sysmacros.h>

extern int pcirm_debug;
#define	DEBUGPRT \
	if (pcirm_debug) cmn_err

extern void pcie_init_bus_props(dev_info_t *);
extern void pcie_fini_bus_props(dev_info_t *);
extern uint16_t pcie_get_vf_busnum(dev_info_t *);
extern dev_info_t *pcie_get_boot_usb_hc();
extern dev_info_t *pcie_get_boot_disk();

/*
 * Resource allocation functions
 */
static boolean_t pcirm_within_rangeset(uint64_t, uint64_t, pcirm_range_t *);
static pcirm_range_t *pcirm_create_rangeset(dev_info_t *, pcirm_type_t);
static int pcirm_create_rangeset_reg(dev_info_t *, pcirm_type_t,
    char *, pcirm_range_t **, boolean_t);
static int pcirm_create_rangeset_from_avail(dev_info_t *, pcirm_type_t,
    pcirm_range_t **, boolean_t);
static int pcirm_create_rangeset_from_assigned(dev_info_t *, pcirm_type_t,
    pcirm_range_t **, boolean_t);
static int pcirm_add_range(pcirm_range_t **, uint64_t, uint64_t);
static int pcirm_create_rangeset_busnum(dev_info_t *, pcirm_range_t **);
static void pcirm_destroy_rangeset(pcirm_range_t *);
static void pcirm_claim_busnum(dev_info_t *, pcirm_range_t **);
static void pcirm_remove_bus_range(pcirm_range_t **, uint64_t, uint64_t);

static boolean_t
pcirm_match_reg_type(dev_info_t *, pci_regspec_t *, pcirm_type_t);
static boolean_t
pcirm_match_reg_assigned_entry(pci_regspec_t *, pci_regspec_t *);
static boolean_t
pcirm_match_reg_entry(uint64_t, pcirm_type_t, pci_regspec_t *);
static int pcirm_append_assigned_prop(dev_info_t *, pci_regspec_t *);
static void pcirm_debug_dump_regs(pci_regspec_t *, int);

static int
pcirm_prop_remove_avail(dev_info_t *, pcirm_type_t);

/*
 * Utility routines
 */
static void
pcirm_debug_dump_ranges(pcirm_range_t *head, const char *name);

/*
 * Rebalance routines
 */
static void
pcirm_addr_to_range(pci_regspec_t *, uint64_t *, uint64_t *);
static int
pcirm_prop_modify_bus_range(dev_info_t *, uint64_t, uint64_t);
static int
pcirm_prop_modify_addr(dev_info_t *, pcirm_rbl_map_t *);
static int
pcirm_update_bus_number_impacted(dev_info_t *, pcirm_rbl_map_t *);
static int
pcirm_calculate_tree_requests(dev_info_t *, int);
static int
pcirm_calculate_bridge_requests(dev_info_t *, int);
static void
pcirm_sort_immediate_children_reqs(dev_info_t *, pcirm_type_t,
    pcirm_bridge_req_t **, pcirm_bridge_req_t **);
static int
pcirm_calculate_bridge_req(dev_info_t *, pcirm_type_t);
static int
pcirm_get_curr_bridge_range(dev_info_t *, pcirm_type_t,
    uint64_t *, uint64_t *);
static void
pcirm_insert_rbl_node(pcirm_handle_impl_t *, dev_info_t *dip);
static int
pcirm_rbl_match_addr(pcirm_addr_list_t *addr, uint64_t *, uint64_t *);
static int
pcirm_calculate_tree_req(dev_info_t *, pcirm_type_t);
static boolean_t
pcirm_size_calculated(dev_info_t *, pcirm_type_t);
static pcirm_bridge_req_t *
pcirm_get_size_calculated(dev_info_t *, pcirm_type_t);
static void
pcirm_set_bridge_req(pcirm_bridge_req_t *);
static uint64_t
pcirm_sum_aligned_size(pcirm_size_list_t *);
static uint64_t
pcirm_cal_aligned_end(pcirm_size_list_t *, uint64_t);
static int
pcirm_get_aligned_base(pcirm_size_list_t *, uint64_t, pcirm_type_t, uint64_t *);
static void
pcirm_list_insert_fixed(pcirm_bridge_req_t **, pcirm_bridge_req_t *);
static void
pcirm_list_insert_size(pcirm_bridge_req_t **, pcirm_bridge_req_t *);
static pcirm_bridge_req_t *
pcirm_get_size_from_dip(dev_info_t *, pcirm_type_t);
static void
pcirm_free_bridge_req(pcirm_bridge_req_t *br);
static pcirm_bridge_req_t *
pcirm_get_size_from_reg(dev_info_t *, pcirm_type_t);
static void
pcirm_debug_dump_bdgreq_list(pcirm_bridge_req_t *, const char *);
static int
pcirm_extend_rc_bus_range(dev_info_t *, uint64_t);

static pcirm_req_t *
pcirm_get_vf_res(dev_info_t *, pcirm_type_t);
static pcirm_bridge_req_t *
pcirm_get_vf_resource_req(dev_info_t *, pcirm_type_t);

static int
pcirm_find_for_one_type(pcirm_bridge_req_t *);
static int
pcirm_try_available_resources(dev_info_t *, uint_t, int);
static int
pcirm_cal_fixed_bdgreq(dev_info_t *, pcirm_bridge_req_t *,
    pcirm_bridge_req_t **);
static int
pcirm_merge_relo_in_fixed_brs(dev_info_t *, pcirm_bridge_req_t **,
    pcirm_bridge_req_t **);
static void
pcirm_fill_relobrs_to_fixed_holes(pcirm_bridge_req_t **,
    pcirm_bridge_req_t *);
static void
pcirm_merge_fixed_bdgreq_list(dev_info_t *, pcirm_bridge_req_t **);
static void
pcirm_turn_relo_to_fixed_br(pcirm_bridge_req_t *, uint64_t);
uint64_t pcirm_cal_bridge_align(pcirm_type_t, uint64_t);
uint64_t pcirm_cal_bridge_size(pcirm_type_t, uint64_t);
static pcirm_addr_list_t *
pcirm_turn_relo_to_fixed(pcirm_size_list_t *, uint64_t);
static int pcirm_fit_into_range(pcirm_bridge_req_t *, uint64_t, uint64_t);
static void
pcirm_mark_rebalance_map_for_resource(pcirm_addr_list_t *, pcirm_type_t);
static void
pcirm_mark_rebalance_map(dev_info_t *, pcirm_type_t, pcirm_addr_list_t *);
static void
pcirm_mark_insert_children_bus_rbl_map(dev_info_t *dip,
    uint64_t base, uint64_t old_base);
static void
pcirm_mark_rebalance_map_for_subtree(dev_info_t *dip, pcirm_type_t type);
static void
pcirm_update_bridge_req(pcirm_addr_list_t *addr, pcirm_type_t type);
boolean_t
pcirm_is_fixed_device(dev_info_t *dip, pcirm_type_t type);
static int
pcirm_mark_rebalance_map_walk(dev_info_t *dip, void *arg);
boolean_t
pcirm_is_pci_root(dev_info_t *dip);
static void
pcirm_merge_relo_bdgreq_list(dev_info_t *dip, pcirm_bridge_req_t **br);
static int
pcirm_try_curr_bridge(pcirm_bridge_req_t *sa);

#if defined(__i386) || defined(__amd64)
extern void pci_cfgacc_update_bad_bridge(uchar_t, uchar_t);
extern void pci_cfgacc_update_bad_bridge_range(uint16_t, uchar_t, uchar_t);
#endif /* defined(__i386) || defined(__amd64) */

boolean_t pcirm_fix_rc_avail = B_TRUE;

#define	PCIRM_FIND_BDGREQ_TYPE(dip, brtype, br)	\
	for (br = DIP2RBL(dip)->pci_bdg_req; br; br = br->next) { \
		if (br->type == brtype)	\
			break;		\
	}

#define	DIP2RCHDL(dip) \
	(DIP2RCRBL(dip)->pcirm_hdl)

#define	CLASS_CODE_AMD_IOMMU		0x080600
#define	CLASS_CODE_PCI_SUB_BRIDGE	0x060401

/*
 * Allocate/Free routines
 */

/*
 * Find a type of resource from the given device dip.
 * It does not update the device's properties. Just return the resource found.
 */
int
pcirm_get_resource(dev_info_t *dip, pcirm_req_t *req, pcirm_res_t *resource)
{
	pcirm_range_t *head, *rangeset, **backp, **backlargestp, *tmp;
	uint64_t len, remlen, largestbase, largestlen;
	uint64_t base, oldbase, lower, upper;
	uint64_t retbase, retlen;
	boolean_t  flag = B_FALSE;
	uint_t mask;

	len = req->len;

	if (req->type == PCIRM_TYPE_BUS) {
		mask = 0;
	} else {
		if (req->flags & PCIRM_REQ_ALIGN_SIZE) {
			if (!ISP2(req->len))
				return (PCIRM_FAILURE);
		}
		mask = (req->flags & PCIRM_REQ_ALIGN_SIZE) ? (len - 1) :
		    req->align_mask;
	}

	/*
	 * Create the resource range set according to properties:
	 * "available" and "bus-range" and the resource type.
	 */
	rangeset = pcirm_create_rangeset(dip, req->type);
	if (!rangeset)
		return (PCIRM_FAILURE);

	head = rangeset;
	backp = &rangeset;
	backlargestp = NULL;
	largestbase = 0;
	largestlen = 0;

	lower = 0;
	upper = ~(uint64_t)0;

	/* Allocate from the given bounded base and len area. */
	if (req->flags & PCIRM_REQ_ALLOC_BOUNDED) {
		/* bounded so skip to first possible */
		lower = req->boundbase;
		upper = req->boundlen + lower;

		/* if the sum is overflowed */
		if ((upper == 0) || (upper < req->boundlen))
			upper = ~(uint64_t)0;
		DEBUGPRT(CE_CONT, "pcirm_get_resource: "
		    "rangeset->len = %"	PRIx64 ", req len = %" PRIx64 ","
		    "rangeset->base=%" PRIx64 ", mask=%u, type=%x \n",
		    rangeset->len, len, rangeset->base, mask, req->type);
		for (; rangeset != NULL &&
		    (rangeset->base + rangeset->len) < lower;
		    backp = &(rangeset->next), rangeset = rangeset->next) {
			if (((rangeset->len + rangeset->base) == 0) ||
			    ((rangeset->len + rangeset->base) < rangeset->len))
				/*
				 * This elements end goes beyond max uint64_t.
				 * potential candidate, check end against lower
				 * would not be precise.
				 */
				break;

			DEBUGPRT(CE_CONT, " len = %" PRIx64 ", ra_base=%"
			    PRIx64 "\n", rangeset->len, rangeset->base);
		}
	}
	/* if no specific address specified, allocate according to len */
	if (!(req->flags & PCIRM_REQ_ALLOC_SPECIFIED)) {
		DEBUGPRT(CE_CONT, "pcirm_get_resource(unspecified request)"
		    "lower=%" PRIx64 ", upper=%" PRIx64 "\n", lower, upper);
		for (; rangeset != NULL && rangeset->base <= upper;
		    backp = &(rangeset->next), rangeset = rangeset->next) {

			DEBUGPRT(CE_CONT, "pcirm_get_resource: "
			    "rangeset->len = %" PRIx64 ", len = %" PRIx64 "",
			    rangeset->len, len);
			base = rangeset->base;
			if (base < lower) {
				base = lower;
				DEBUGPRT(CE_CONT, "\tbase=%" PRIx64
				    ", ra_base=%" PRIx64 ", mask=%u",
				    base, rangeset->base, mask);
			}

			if ((base & mask) != 0) {
				oldbase = base;
				/*
				 * failed a critical constraint
				 * adjust and see if it still fits
				 */
				base = base & ~mask;
				base += (mask + 1);
				DEBUGPRT(CE_CONT, "\tnew base=%" PRIx64 "\n",
				    base);

				/*
				 * Check to see if the new base is past
				 * the end of the resource.
				 */
				if (base >= (oldbase + rangeset->len + 1)) {
					continue;
				}
			}

			if (req->flags & PCIRM_REQ_ALLOC_PARTIAL_OK) {
				if ((upper - rangeset->base)  <  rangeset->len)
					remlen = upper - base;
				else
					remlen = rangeset->len -
					    (base - rangeset->base);

				if ((backlargestp == NULL) ||
				    (largestlen < remlen)) {

					backlargestp = backp;
					largestbase = base;
					largestlen = remlen;
				}
			}

			if (rangeset->len >= len) {
				/* a candidate -- apply constraints */
				if ((len > (rangeset->len -
				    (base - rangeset->base))) ||
				    ((len - 1 + base) > upper)) {
					continue;
				}

				/* we have a fit */

				DEBUGPRT(CE_CONT, "\thave a fit\n");
				/* resource found */
				flag = B_TRUE;

				break;
			}
		}
	} else {
		/* want an exact value/fit */
		base = req->addr;
		len = req->len;
		for (; rangeset != NULL && rangeset->base <= upper;
		    backp = &(rangeset->next), rangeset = rangeset->next) {
			if (base >= rangeset->base &&
			    ((base - rangeset->base) < rangeset->len)) {
				/*
				 * This is the node with he requested base in
				 * its range
				 */
				if ((len > rangeset->len) ||
				    (base - rangeset->base >
				    rangeset->len - len)) {
					/* length requirement not satisfied */
					if (req->flags &
					    PCIRM_REQ_ALLOC_PARTIAL_OK) {
						if ((upper - rangeset->base)
						    < rangeset->len)
							remlen = upper - base;
						else
							remlen =
							    rangeset->len -
							    (base -
							    rangeset->base);
					}
					backlargestp = backp;
					largestbase = base;
					largestlen = remlen;
					base = 0;
				} else {
					/* We have a match */
					DEBUGPRT(CE_CONT, "\thave a match\n");
					flag = B_TRUE;
				}
				break;
			}
		}
	}
	LIST_FREE(head, tmp);
	if (flag == B_TRUE) {
		retbase = base;
		retlen = len;
	} else if ((req->flags & PCIRM_REQ_ALLOC_PARTIAL_OK) &&
	    (backlargestp != NULL)) {
		/* Update the property after found the requested resource. */
		retbase = largestbase;
		retlen = largestlen;
		resource->flags |= PCIRM_RES_PARTIAL_COMPLETED;
	} else {
		/*
		 * None of the existing pieces of resource can satisfy the
		 * request.
		 */
		return (PCIRM_FAILURE);
	}
	resource->type = req->type;
	resource->base = retbase;
	resource->len = retlen;

	return (PCIRM_SUCCESS);
}

static boolean_t
pcirm_within_rangeset(uint64_t base, uint64_t len, pcirm_range_t *head)
{
	pcirm_range_t *range;

	for (range = head; range; range = range->next) {
		uint64_t range_base = range->base;
		uint64_t range_end = range->base + range->len - 1;
		if (base >= range_base &&
		    base + len -1 <= range_end) {
			return (B_TRUE);
		}
	}

	return (B_FALSE);
}

/*
 * Create a list for the available resource ranges according to the
 * properties of a bridge node. For IO/MEM/PMEM, create a rangeset according to
 * "available" property; for bus number, create an available bus number
 * rangeset according to "bus-range" properies of this node and the children's.
 */
static pcirm_range_t *
pcirm_create_rangeset(dev_info_t *dip, pcirm_type_t type)
{
	pcirm_range_t *head = NULL;

	if (type == PCIRM_TYPE_BUS) {
		(void) pcirm_create_rangeset_busnum(dip, &head);
		return (head);

	}
	/* For IO/MEM/PMEM */
	(void) pcirm_create_rangeset_from_avail(dip, type, &head, B_FALSE);
	return (head);
}

/*
 * For a given resource type, get the rangeset from the "assigned-addresses"
 * or "available" property. It is for IO/MEM/PMEM addresses.
 */
static int
pcirm_create_rangeset_reg(dev_info_t *dip, pcirm_type_t type, char *prop,
    pcirm_range_t **head, boolean_t is_append)
{
	pci_regspec_t *regs;
	int rlen, rcount, i, rval;

	if (!is_append)
		*head = NULL;

	/* read the property */
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS |
	    DDI_PROP_NOTPROM, prop, (caddr_t)&regs,
	    &rlen) != DDI_SUCCESS) {

		return (PCIRM_FAILURE);
	}

	/*
	 * create the rangeset for the given type of resource
	 */
	rcount = rlen / sizeof (pci_regspec_t);
	for (i = 0; i < rcount; i++) {
		boolean_t match;

		if (strcmp(prop, "assigned-addresses") == 0)
			match = pcirm_match_reg_type(dip, &regs[i], type);
		else
			match = (PCIRM_REQTYPE(regs[i].pci_phys_hi) == type);

		if (!match)
			continue;

		if (rval = pcirm_add_range(head, REG_TO_BASE(regs[i]),
		    REG_TO_SIZE(regs[i])))
			break;
	}
	kmem_free(regs, rlen);
	return (rval);
}

static int
pcirm_create_rangeset_from_avail(dev_info_t *dip, pcirm_type_t type,
    pcirm_range_t **head, boolean_t is_append)
{
	return (pcirm_create_rangeset_reg(dip, type,
	    "available", head, is_append));
}

static int
pcirm_create_rangeset_from_assigned(dev_info_t *dip, pcirm_type_t type,
    pcirm_range_t **head, boolean_t is_append)
{
	return (pcirm_create_rangeset_reg(dip, type,
	    "assigned-addresses", head, is_append));
}

/*
 * For a given resource type, get the rangeset from the "ranges"
 * property of bridges. It is for IO/MEM/PMEM addresses.
 */
static int
pcirm_create_rangeset_from_ranges(dev_info_t *dip, pcirm_type_t type,
    pcirm_range_t **head, boolean_t is_append)
{
	pci_ranges_t *ranges;
	uint64_t base, len;
	int ralen, count, i, rval = PCIRM_SUCCESS;

	if (!is_append)
		*head = NULL;

	if (pcirm_is_pci_root(dip))
		goto root_ranges;

	/* read the property */
	if (pcirm_get_curr_bridge_range(dip, type, &base, &len) !=
	    PCIRM_SUCCESS) {
		return (PCIRM_FAILURE);
	}
#if defined(__sparc)
	if (base == 0xfff00000 && len == 0xffffffff00200000)
		return (PCIRM_FAILURE);
#endif
	rval = pcirm_add_range(head, base, len);
	return (rval);

root_ranges:
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS |
	    DDI_PROP_NOTPROM, "ranges", (caddr_t)&ranges,
	    &ralen) != DDI_SUCCESS) {
		return (PCIRM_SUCCESS);
	}
	count = ralen / (sizeof (pci_ranges_t));
	for (i = 0; i < count; i++) {
		if (PCIRM_RESOURCETYPE(ranges[i].child_high) != type)
			continue;

		base = RANGES_TO_BASE(ranges[i]);
		len = RANGES_TO_SIZE(ranges[i]);
		if (rval = pcirm_add_range(head, base, len))
			break;
	}
	kmem_free(ranges, ralen);
	return (rval);
}

/*
 * Destroy a list of available resource ranges
 */
static void
pcirm_destroy_rangeset(pcirm_range_t *head)
{
	pcirm_range_t *tmp;

	for (; head != NULL; head = tmp) {
		tmp = head->next;
		kmem_free(head, sizeof (*head));
	}
}

/*
 * On some platforms (sparc/x64), the "ranges" property of RC includes some
 * system reserved addresses which can not be used as relocatable addresses.
 *
 * Calculate the ranges by sum all "available" properties and the immediate
 * children's "ranges" and "assigned-addresses".
 * Thus, we get the real relocatable ranges of root complex node.
 * There might be multiple sections for one type of resources.
 */
static pcirm_range_t *
pcirm_get_root_complex_ranges(dev_info_t *dip, pcirm_type_t type)
{
	pcirm_range_t *range = NULL;
	dev_info_t *cdip;

	(void) pcirm_create_rangeset_from_avail(dip, type, &range, B_TRUE);
	pcirm_debug_dump_ranges(range, "pcirm_get_root_complex_ranges");
	for (cdip = ddi_get_child(dip); cdip;
	    cdip = ddi_get_next_sibling(cdip)) {
		if (pcie_is_pci_bridge(cdip)) {
			(void) pcirm_create_rangeset_from_ranges(cdip, type,
			    &range, B_TRUE);
			pcirm_debug_dump_ranges(range, "pcirm_add_range");
		}

		(void) pcirm_create_rangeset_from_assigned(cdip, type,
		    &range, B_TRUE);
		pcirm_debug_dump_ranges(range, "pcirm_add_range");
	}
	pcirm_debug_dump_ranges(range, "pcirm_get_root_complex_ranges");
	return (range);
}

static int
pcirm_verify_rc_avail_type(dev_info_t *dip, pcirm_type_t type)
{
	pcirm_range_t *avail_set, *range_set;
	pcirm_range_t *avail, *range;
	uint64_t avail_base, avail_end;
	uint64_t range_base, range_end;
	int rval = PCIRM_SUCCESS;

	if (pcirm_create_rangeset_from_avail(dip, type, &avail_set, B_FALSE)
	    == PCIRM_BAD_RANGE)
		return (PCIRM_FAILURE);

	(void) pcirm_create_rangeset_from_ranges(dip, type,
	    &range_set, B_FALSE);

	for (avail = avail_set; avail; avail = avail->next) {
		for (range = range_set; range; range = range->next) {
			avail_base = avail->base;
			range_base = range->base;
			avail_end = avail->base + avail->len - 1;
			range_end = range->base + range->len - 1;
			if (avail_base >= range_base &&
			    avail_end <= range_end) {
				break;
			}

#if defined(__sparc)
			if (avail_base >= range_base &&
			    avail_base <= range_end &&
			    avail_end > range_end) {
				/*
				 * It was found some sparc platforms has
				 * available ranges which extends outside
				 * of the boundary of total ranges, in this
				 * case remove the address range ouside
				 * the boundary.
				 */
				if (pcirm_fix_rc_avail) {
					(void) pcirm_prop_rm_addr(dip,
					    "available", range_end + 1,
					    avail_end - range_end, type);
					break;
				}
			}
#endif
		}
		if (range == NULL) {
#if defined(__sparc)
			if (type == PCIRM_TYPE_PMEM && pcirm_fix_rc_avail) {
				/*
				 * It was seen some platforms have pre-
				 * fetchable available entries outside
				 * of ranges, in this case simply remove
				 * these entries instead of failing
				 * rebalance.
				 */
				(void) pcirm_prop_rm_addr(dip, "available",
				    avail->base, avail->len, type);
				continue;
			}
#endif
			/* failed to find a enclosing range */
			rval = PCIRM_FAILURE;
			goto done;
		}
	}
done:
	pcirm_destroy_rangeset(range_set);
	pcirm_destroy_rangeset(avail_set);
	return (rval);
}

static boolean_t
pcirm_check_bad_rc_avail(dev_info_t *dip, int types)
{
	pcirm_range_t *head = NULL;
	int rval;

	if ((types & (PCIRM_TYPE_MEM_M | PCIRM_TYPE_PMEM_M)) == 0)
		return (B_FALSE);

	if (types & PCIRM_TYPE_MEM_M) {
		if (pcirm_verify_rc_avail_type(dip, PCIRM_TYPE_MEM)
		    != PCIRM_SUCCESS)
			return (B_TRUE);
	}

	if (types & PCIRM_TYPE_PMEM_M) {
		if (pcirm_verify_rc_avail_type(dip, PCIRM_TYPE_PMEM)
		    != PCIRM_SUCCESS)
			return (B_TRUE);
	}

	/*
	 * Check overlap of 'available' property for memory
	 * and pre-fetchable memory entries.
	 */
	(void) pcirm_create_rangeset_from_avail(dip, PCIRM_TYPE_MEM,
	    &head, B_TRUE);
	rval = pcirm_create_rangeset_from_avail(dip, PCIRM_TYPE_PMEM,
	    &head, B_TRUE);

	pcirm_destroy_rangeset(head);
	return (rval == PCIRM_BAD_RANGE);
}

typedef struct {
	boolean_t 	disable;
} disable_arg_t;

/*ARGSUSED*/
static int
pcirm_do_check_amd_iommu(dev_info_t *dip, void *arg)
{
	disable_arg_t	*disable_arg = (disable_arg_t *)arg;
	const char *driver;

	driver = ddi_driver_name(dip);
	if (driver != NULL && strcmp(driver, "amd_iommu") == 0) {
		disable_arg->disable = B_TRUE;
		return (DDI_WALK_TERMINATE);
	}

	return (DDI_WALK_CONTINUE);
}

static int
pcirm_check_amd_iommu(dev_info_t *rcdip)
{
	int		circular_count;
	dev_info_t	*dip = ddi_get_child(rcdip);
	disable_arg_t	arg;

	arg.disable = B_FALSE;

	ndi_devi_enter(rcdip, &circular_count);
	ddi_walk_devs(dip, pcirm_do_check_amd_iommu, &arg);
	ndi_devi_exit(rcdip, circular_count);

	return (arg.disable);
}

static int
pcirm_is_rebalance_disabled(dev_info_t *rcdip, int types)
{
	return (pcirm_check_bad_rc_avail(rcdip, types) ||
	    pcirm_check_amd_iommu(rcdip));
}

/*
 * Add a range (base, len) to the rangeset list. It is not assumed that all
 * continuous pieces of resources are already merged. So this function will
 * merge the continuous ones when it add the range to the range set.
 */
static int
pcirm_add_range(pcirm_range_t **head, uint64_t base, uint64_t len)
{
	pcirm_range_t *headp, *curr, *prev, *new, *next_el = NULL;
	uint64_t newend, currend;

	ASSERT(len);

	headp = *head;
	if (headp == NULL) {
		/* stick on end */
		new = (pcirm_range_t *)
		    kmem_zalloc(sizeof (*new), KM_SLEEP);
		new->base = base;
		new->len = len;
		new->next = NULL;
		headp = new;
		*head = headp;

		return (PCIRM_SUCCESS);
	}
	curr = headp;
	prev = NULL;

	/*
	 * find where the new range should be placed, and merge it with
	 * its adjacent neighbours if possible.
	 */
	newend = base + len - 1;
	for (; curr != NULL; curr = curr->next) {

		currend = curr->base + curr->len - 1;

		/* check for overlap first */
		if ((base <= curr->base && newend >= curr->base) ||
		    (base >= curr->base && base <= currend)) {
			/* overlap with curr */
			goto overlap;
		}

		if ((newend + 1) == curr->base) {
			/* simple - on front */
			curr->base = base;
			curr->len += len;
			/*
			 * don't need to check if it merges with
			 * previous since that would match at end
			 */
			break;
		} else if (base == (currend + 1)) {
			/* simple - on end */
			curr->len += len;
			if (curr->next &&
			    ((newend + 1) == curr->next->base)) {
				/* merge with next node */
				next_el = curr->next;
				curr->len += next_el->len;
				LIST_REMOVE(headp, curr, next_el);
				kmem_free((caddr_t)next_el, sizeof (*next_el));
			}
			break;
		} else if (base < curr->base) {
			/* somewhere in between so just an insert */
			new = (pcirm_range_t *)
			    kmem_zalloc(sizeof (*new), KM_SLEEP);
			new->base = base;
			new->len = len;
			if (!prev) {
				LIST_INSERT_TO_HEAD(headp, new);
			} else {
				LIST_INSERT(prev, new)
			}
			break;
		}
		prev = curr;
	}
	if (curr == NULL) {
		/* stick on end */
		new = (pcirm_range_t *)
		    kmem_zalloc(sizeof (*new), KM_SLEEP);
		new->base = base;
		new->len = len;
		if (!prev) {
			LIST_INSERT_TO_HEAD(headp, new);
		} else {
			LIST_INSERT(prev, new)
		}
	}
	*head = headp;

	return (PCIRM_SUCCESS);

overlap:
	/*
	 * Overlap should not happlen. If happens, it does not necessarily
	 * means it is a fatal problem if the hardware functioning normally.
	 * We send such messages to syslog only.
	 */
	DEBUGPRT(CE_NOTE, "!pcirm_add_range: base 0x%" PRIx64 ", len 0x%"
	    PRIX64 " overlaps with base 0x%" PRIx64
	    ", len 0x%" PRIx64 "\n", base, len, curr->base, curr->len);

	return (PCIRM_BAD_RANGE);
}

/* Check if (base,len) fall into the bridge's range */
boolean_t
pcirm_within_range(dev_info_t *dip, uint64_t base, uint64_t len,
    pcirm_type_t type)
{
	uint64_t range_base, range_len;

	if (pcirm_get_curr_bridge_range(dip, type,
	    &range_base, &range_len) != DDI_SUCCESS) {

		return (B_FALSE);
	}

	if (base >= range_base &&
	    base + len <= range_base + range_len) {
		return (B_TRUE);
	}

	return (B_FALSE);
}

/* Check if a device node is a subtractive pci-pci bridge */
boolean_t
pcirm_is_subtractive_bridge(dev_info_t *dip)
{
	int len, *class;

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "class-code", (caddr_t)&class, &len) == DDI_SUCCESS) {
		if (*class == CLASS_CODE_PCI_SUB_BRIDGE) {
			kmem_free(class, len);
			return (B_TRUE);
		}

		kmem_free(class, len);
	}
	return (B_FALSE);
}

static boolean_t
pcirm_is_display(dev_info_t *dip)
{
	int len, *class;
	boolean_t answer = B_FALSE;

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "class-code", (caddr_t)&class, &len) == DDI_SUCCESS) {
		if (((*class >> 16) & 0xff) == PCI_CLASS_DISPLAY)
			answer = B_TRUE;
		kmem_free(class, len);
	}
	return (answer);
}

/*
 * For freeing a piece of resource from a bridge's "ranges".
 * Remove (base,len) from dip's "ranges" property. (base,len) must be on left
 * or right of the ranges, or it returns failure.
 *
 * This does not applicable to root complex's "ranges".
 */
int
pcirm_prop_rm_ranges(dev_info_t *dip, uint64_t base, uint64_t len,
    pcirm_type_t type)
{
	ppb_ranges_t *ranges;
	int rlen, count, i;
	uint64_t range_base, range_len;

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS |
	    DDI_PROP_NOTPROM, "ranges", (caddr_t)&ranges,
	    &rlen) != DDI_SUCCESS) {

		return (PCIRM_FAILURE);
	}
	count = rlen / sizeof (ppb_ranges_t);
	for (i = 0; i < count; i++) {
		/* Match the given type with the range entry */
		if (PCIRM_RESOURCETYPE(ranges[i].child_high) != type)
			continue;

		range_base = RANGES_TO_BASE(ranges[i]);
		range_len = RANGES_TO_SIZE(ranges[i]);
		if (range_len < len) {
			/* Too big length to free */
			goto fail;
		}
		if (range_base == base) {
			/* On right */
			range_base = range_base + len;
			range_len = range_len - len;
		} else if ((range_base + range_len) == (base + len)) {
			/* On left */
			range_len = range_len - len;
		} else {
			continue;
		}
		BASE_TO_RANGES(range_base, ranges[i]);
		SIZE_TO_RANGES(range_len, ranges[i]);
		(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		    "ranges", (int *)ranges, rlen / sizeof (int));
		kmem_free(ranges, rlen);

		return (PCIRM_SUCCESS);
	}
fail:
	kmem_free(ranges, rlen);
	return (PCIRM_FAILURE);
}

static void
pcirm_debug_dump_ranges(pcirm_range_t *head, const char *name)
{
	DEBUGPRT(CE_CONT, "%s: dumping ranges: "
	    "range head=%p \n", name, (void *)head);
	for (; head; head = head->next) {
		DEBUGPRT(CE_CONT,
		    "head->base=%" PRIx64 ","
		    "len=%" PRIx64 "\n",
		    head->base, head->len);
	}
}

static void
pcirm_debug_dump_relo_list(pcirm_size_list_t *relo_list, const char *name)
{
	pcirm_size_list_t *relo;

	DEBUGPRT(CE_CONT, "%s: dumping relo list\n", name);
	for (relo = relo_list; relo; relo = relo->next) {
		DEBUGPRT(CE_CONT, "relo_list->dip=%p, bar_off=%x\n"
		    "size=%" PRIx64 "\n"
		    "align=%" PRIx64 "\n"
		    "next=%p \n",
		    (void *)relo->dip, relo->bar_off,
		    relo->size, relo->align,
		    (void *)relo->next);
	}
}

static void
pcirm_debug_dump_bdgreq_list(pcirm_bridge_req_t *list_br, const char *name)
{
	pcirm_size_list_t *relo;
	pcirm_addr_list_t *fixed;

	DEBUGPRT(CE_CONT, "%s: dumping bridge req list: "
	    "list_br head=%p \n", name, (void *)list_br);
	for (; list_br; list_br = list_br->next) {
		DEBUGPRT(CE_CONT, "list_br->dip=%p, type=%x\n"
		    "relo_list=%p\n"
		    "fixed_list=%p\n"
		    "next=%p\n",
		    (void *)list_br->dip, list_br->type,
		    (void *)list_br->relo_list,
		    (void *)list_br->fixed_list,
		    (void *)list_br->next);
		for (relo = list_br->relo_list; relo; relo = relo->next) {
			DEBUGPRT(CE_CONT, "relo_list->dip=%p, bar_off=%x\n"
			    "size=%" PRIx64 "\n"
			    "align=%" PRIx64 "\n"
			    "next=%p \n",
			    (void *)relo->dip, relo->bar_off,
			    relo->size, relo->align,
			    (void *)relo->next);
		}
		for (fixed = list_br->fixed_list; fixed; fixed = fixed->next) {
			DEBUGPRT(CE_CONT, "fixed_list->dip=%p, bar_off=%x\n"
			    "base=%" PRIx64 "\n"
			    "end=%" PRIx64 "\n"
			    "next=%p\n",
			    (void *)fixed->dip, fixed->bar_off,
			    fixed->base, fixed->end,
			    (void *)fixed->next);
		}
	}
}

/*
 * Get the rangeset for the available bus numbers, from the bus-range property
 */
static int
pcirm_create_rangeset_busnum(dev_info_t *dip, pcirm_range_t **head)
{
	int len;
	pci_bus_range_t *pci_bus_range;
	dev_info_t *cdip;

	*head = NULL;
	/*
	 * Firstly find out the bus-range of this bridge (dip), then remove
	 * those used by its immediate children.
	 */
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "bus-range", (caddr_t)&pci_bus_range, &len) == DDI_SUCCESS) {
		if (pci_bus_range->lo != pci_bus_range->hi) {
			/*
			 * Add bus numbers other than the secondary
			 * bus number to the free list.
			 */
			if (pcirm_add_range(head,
			    (uint64_t)pci_bus_range->lo + 1,
			    (uint64_t)pci_bus_range->hi -
			    (uint64_t)pci_bus_range->lo) != PCIRM_SUCCESS) {

				kmem_free(pci_bus_range, len);
				return (PCIRM_FAILURE);
			}

#if defined(PCIRM_DEBUG)
			pcirm_debug_dump_ranges(*head,
			    "pcirm_create_rangeset_busnum");
#endif
			/* scan for pci-pci bridges in immediate children */
			cdip = ddi_get_child(dip);
			for (; cdip; cdip = ddi_get_next_sibling(cdip)) {

				pcirm_claim_busnum(cdip, head);
			}

#if defined(PCIRM_DEBUG)
			pcirm_debug_dump_ranges(*head,
			    "pcirm_create_rangeset_busnum");
#endif
			kmem_free(pci_bus_range, len);
			return (PCIRM_SUCCESS);
		}
		kmem_free(pci_bus_range, len);
	}
	return (PCIRM_SUCCESS);
}

/*
 * If the device is a PCI bridge device then
 * claim the bus numbers used by the device from the parent's bus range.
 */
static void
pcirm_claim_busnum(dev_info_t *dip, pcirm_range_t **head)
{
	pci_bus_range_t *pci_bus_range;
	int len, rval;
	pcirm_range_t *tmp = *head;

	if (!pcie_is_pci_bridge(dip))
		return;

	/* look for the bus-range property */
	rval = ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "bus-range", (caddr_t)&pci_bus_range, &len);
	if (rval == DDI_SUCCESS) {

		pcirm_remove_bus_range(&tmp, pci_bus_range->lo,
		    pci_bus_range->hi - pci_bus_range->lo + 1);
		kmem_free(pci_bus_range, len);
		*head = tmp;

		return;
	}
	DEBUGPRT(CE_CONT, "pcirm_claim_busnum: "
	    "failed to get bus-range, dip=%p, rval=%d \n", (void *) dip, rval);
}

/*
 * Remove (base, len) from the range set. Assume it is within the
 * range set.
 *
 * For a range in the range set, either it covers the (base,len) or
 * it does not cover it at all. No partial covering case here. This is for
 * bus numbers only.
 */
static void
pcirm_remove_bus_range(pcirm_range_t **head, uint64_t base, uint64_t len)
{
	pcirm_range_t *headp, *newrange, *prev = NULL;
	uint64_t end, range_end;

	if (len == 0) {
		return;
	}

	headp = *head;

	/* now find where range lies and fix things up */
	end = base + len;
	for (; headp != NULL; prev = headp, headp = headp->next) {
		range_end = headp->base + headp->len;
		if ((headp->base > base && headp->base < end) ||
		    (range_end > base && range_end < end)) {
			/* Invalide cases */
			DEBUGPRT(CE_WARN, "pcirm_remove_bus_range: "
			    "Invalide (base,len)! base=%" PRIx64 ","
			    " len=%" PRIx64 ", "
			    "range base=%" PRIx64 ", len=%" PRIx64 " \n",
			    base, len, headp->base, headp->len);
			return;
		}

		/* (base,len) does not fall into this range. */
		if ((end <= headp->base) || (base >= range_end)) {
			continue;
		}
		/* (base,len) is exactly same as the range. */
		if ((base == headp->base) && (end == range_end)) {
			if (!prev) { /* if the first one */
				*head = headp->next;
			} else {
				LIST_REMOVE(headp, prev, headp);
			}
			kmem_free((caddr_t)headp, sizeof (*headp));
			return;
		}
		if ((base == headp->base) && (end < range_end)) {
			/* simple - on front */
			headp->base = end;
			headp->len -= len;

			return;
		}
		if ((base > headp->base) && (end == range_end)) {
			/* simple - on end */
			headp->len -= len;

			return;
		}
		/* in the middle, it split the range to two */
		newrange = (pcirm_range_t *)
		    kmem_zalloc(sizeof (*newrange), KM_SLEEP);
		headp->len = base - headp->base;
		newrange->base = end;
		newrange->len = range_end - newrange->base;

		LIST_INSERT(headp, newrange);
		return;
	}
}

int
pcirm_prop_merge_available(dev_info_t *dip, pcirm_type_t type)
{
	pcirm_range_t *rangeset, *head;
	pci_regspec_t *regs, *newregs;
	int rlen, rcount, i, j, rval;
	int pci_phys_hi = 0;
	uint64_t base, len;

	/*
	 * First, create range set for this type of resource, this effectively
	 * merges entries of this type together.
	 */
	rangeset = pcirm_create_rangeset(dip, type);
	if (!rangeset)
		return (PCIRM_NO_RESOURCE);

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS |
	    DDI_PROP_NOTPROM, "available", (caddr_t)&regs,
	    &rlen) != DDI_SUCCESS) {

		pcirm_destroy_rangeset(rangeset);
		return (PCIRM_FAILURE);
	}

	newregs = kmem_alloc(rlen, KM_SLEEP);

	/*
	 * Then, copy entries of other types to the new array, from which
	 * the property will be built.
	 */
	rcount = rlen / sizeof (pci_regspec_t);
	for (i = 0, j = 0; i < rcount; i++) {
		/* Match with the given type */
		if (PCIRM_REQTYPE(regs[i].pci_phys_hi) == type) {
			pci_phys_hi = regs[i].pci_phys_hi;
			continue;
		}

		newregs[j] = regs[i];
		j++;
	}

	/*
	 * Then, add the rangeset entris into the new array, plus the
	 * already-copied entries above, so the new array now holds the
	 * updated property values.
	 */
	ASSERT(pci_phys_hi != 0);
	for (head = rangeset; head != NULL; head = head->next) {
		/*
		 * Since we're merging property entries of the same type,
		 * the number of new entries should be no more than the
		 * original.
		 */
		ASSERT(j <= rcount);

		base = head->base;
		len = head->len;

		newregs[j].pci_phys_hi = pci_phys_hi;
		newregs[j].pci_phys_mid = (uint32_t)(base >> 32);
		newregs[j].pci_phys_low = (uint32_t)base;
		newregs[j].pci_size_hi = (uint32_t)(len >> 32);
		newregs[j].pci_size_low = (uint32_t)len;

		j++;
	}
	pcirm_destroy_rangeset(rangeset);

#if defined(PCIRM_DEBUG)
	DEBUGPRT(CE_WARN, "pcirm_prop_merge_available: old props\n");
	pcirm_debug_dump_regs(regs, rcount);
	DEBUGPRT(CE_WARN, "pcirm_prop_merge_available: new props\n");
	pcirm_debug_dump_regs(newregs, j);
#endif
	/*
	 * Remove old "available" property and create a new one
	 */
	(void) ddi_prop_remove(DDI_DEV_T_NONE, dip, "available");

	rval = ndi_prop_update_int_array(DDI_DEV_T_NONE, dip, "available",
	    (int *)newregs, j * (sizeof (pci_regspec_t) / sizeof (int)));

	kmem_free(regs, rlen);
	kmem_free(newregs, rlen);

	return (rval);
}

static void
pcirm_debug_dump_regs(pci_regspec_t *regs, int n)
{
	int i;

	DEBUGPRT(CE_CONT, "pcirm_debug_dump_regs \n");

	for (i = 0; i < n; i++) {
		DEBUGPRT(CE_CONT, "%8x.%8x.%8x.%8x.%8x\n", regs[i].pci_phys_hi,
		    regs[i].pci_phys_mid, regs[i].pci_phys_low,
		    regs[i].pci_size_hi, regs[i].pci_size_low);
	}
}

/*
 * For resource allocate or free.
 * From the "available" (when allocate) or "assigned-addresses" (when free)
 * property, get the address resource (IO/MEM/PMEM) by removing or updating
 * the corresponding property elements.
 *
 * Note:
 * This function assumes for the specified property "prop_name", entries
 * of the same type have been merged already (e.g. by pcirm_prop_merge
 * _available()), which simplies the code quite a bit.
 */
int
pcirm_prop_rm_addr(dev_info_t *dip, char *prop_name, uint64_t base,
    uint64_t len, pcirm_type_t type)
{
	pci_regspec_t *regs, *newregs;
	int rlen, rcount;
	int i, j, k, rval;
	uint64_t dlen;
	boolean_t is_available;
	int flags = DDI_PROP_DONTPASS;

	is_available = (strcmp(prop_name, "available") == 0);
	if (is_available)
		flags |= DDI_PROP_NOTPROM;

	rval = ddi_getlongprop(DDI_DEV_T_ANY, dip, flags,
	    prop_name, (caddr_t)&regs, &rlen);
	if (rval != DDI_SUCCESS)
		return (rval);

	/*
	 * The updated property will at most have one more entry
	 * than existing ones (when the requested range is in the
	 * middle of the matched property entry)
	 */
	newregs = kmem_alloc(rlen + sizeof (pci_regspec_t), KM_SLEEP);
	/*
	 * Find in the available/assigned-addresses regs for
	 * (base, len, type), then update the reges.
	 */
	rcount = rlen / sizeof (pci_regspec_t);

#if 0
	pcirm_debug_dump_regs(regs, rcount);
#endif
	for (i = 0, j = 0; i < rcount; i++) {
		if (PCIRM_REQTYPE(regs[i].pci_phys_hi) == type) {
			uint64_t range_base, range_len;

			pcirm_addr_to_range(&regs[i], &range_base, &range_len);
			if ((base < range_base) || (base + len >
			    range_base + range_len)) {
				/* not matched with this entry */
				goto copy_entry;
			}

			/*
			 * range_base	base	base+len	range_base
			 *					+range_len
			 *   +------------+-----------+----------+
			 *   |		  |///////////|		 |
			 *   +------------+-----------+----------+
			 */
			dlen = base - range_base;
			if (dlen != 0) {
				newregs[j].pci_phys_hi = regs[i].pci_phys_hi;
				newregs[j].pci_phys_mid =
				    (uint32_t)(range_base >> 32);
				newregs[j].pci_phys_low =
				    (uint32_t)(range_base);
				newregs[j].pci_size_hi = (uint32_t)(dlen >> 32);
				newregs[j].pci_size_low = (uint32_t)dlen;
				j++;
			}

			dlen = (range_base + range_len) - (base + len);
			if (dlen != 0) {
				newregs[j].pci_phys_hi = regs[i].pci_phys_hi;
				newregs[j].pci_phys_mid =
				    (uint32_t)((base + len)>> 32);
				newregs[j].pci_phys_low =
				    (uint32_t)(base + len);
				newregs[j].pci_size_hi = (uint32_t)(dlen >> 32);
				newregs[j].pci_size_low = (uint32_t)dlen;
				j++;
			}

			/*
			 * We've allocated the resource from the matched
			 * entry, almost finished but still need to copy
			 * the rest entries from the original property
			 * array.
			 */
			for (k = i + 1; k < rcount; k++) {
				newregs[j] = regs[k];
				j++;
			}

#if 0
			pcirm_debug_dump_regs(newregs, j);
#endif
			goto done;
		} /* if */
copy_entry:
		newregs[j] = regs[i];
		j++;
	}
	/*
	 * In resource allocation case, (base,len) should be able to
	 * be get from the property because this function is
	 * called after the resource is found. If it happens,
	 * print a message for logging.
	 *
	 * In resource free case, this will cause a free
	 * failure because it is freeing something not exists.
	 */
	if (i == rcount) {
		if (is_available) {
			/*
			 * Resource allocation case. It was just calculated that
			 * this node should have enough resource for allocating.
			 */
			DEBUGPRT(CE_WARN, "Failed to remove enough expected "
			    "resources from available property! dip=%p,"
			    "type=%x, base=%" PRIx64 ", len=%" PRIx64 "\n",
			    (void *)dip, type, base, len);
		}
		/* Abort without updating any properties */
		rlen = rcount * sizeof (pci_regspec_t);
		kmem_free(newregs, rlen + sizeof (pci_regspec_t));
		kmem_free(regs, rlen);

		return (PCIRM_FAILURE);
	}

done:
	/* update the property */
	(void) ndi_prop_remove(DDI_DEV_T_NONE, dip, prop_name);

	if (j != 0) {
		rval = ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		    prop_name, (int *)newregs,
		    (j * sizeof (pci_regspec_t)) / sizeof (int));
	}
	kmem_free(newregs, rlen + sizeof (pci_regspec_t));
	kmem_free(regs, rlen);

	return (rval);
}

/*
 * check whether (base, len) is a piece of resource to be assigned to a device
 * according to the "reg" and "assigned-addresses" of the device.
 */
int
pcirm_prop_add_assigned(dev_info_t *dip, uint64_t base, uint64_t len,
    pcirm_type_t type)
{

	pci_regspec_t *regs, *assigned = NULL;
	int rlen, rcount = 0, alen, acount = 0, i, j, rval;

	/* read the "reg" property */
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "reg", (caddr_t)&regs, &rlen) != DDI_SUCCESS) {
		/*
		 * No "reg" available, so no need to populate
		 * "assigned-addresses" for it.
		 */
		return (PCIRM_FAILURE);
	}
	rcount = rlen / sizeof (pci_regspec_t);

	/* read the "assigned-addresses" property */
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "assigned-addresses", (caddr_t)&assigned, &alen) == DDI_SUCCESS) {
		acount = alen / sizeof (pci_regspec_t);
	}
	DEBUGPRT(CE_CONT, "pcirm_prop_add_assigned: rcount=%d, acount=%d",
	    rcount, acount);
	if (rcount <= acount) {
		/* All regs are already assigned */
		DEBUGPRT(CE_CONT, "pcirm_prop_add_assigned: "
		    "all regs are already assigned");
		goto cleanup;
	}
	for (i = 1; i < rcount; i++) {
#if 0
		DEBUGPRT(CE_CONT, "pcirm_prop_add_assigned: "
		    "type=%x, regs[i]pci_phys_hi=%x", type,
		    regs[i].pci_phys_hi);
#endif
		/* Match the type and size */
		if (pcirm_match_reg_entry(len, type, &regs[i])) {
			/*
			 * Check if the matched "reg" entry has been already
			 * assigned, by matching the entry with all existing
			 * "assigned-addresses" entries.
			 */
			for (j = 0; j < acount; j++) {
				if (pcirm_match_reg_assigned_entry(
				    &regs[i], &assigned[j]))
#if 0
					goto cleanup;
#endif
					/*
					 * Note that it's possible to have
					 * multiple reg entries with the same
					 * resource type and same size, thus
					 * we should continue the matching with
					 * the next reg entry instead of
					 * failing the match.
					 */
					goto next_reg;
			}

			/*
			 * So we have a matching "reg" entry which is
			 * not assigned yet, assign it now.
			 */
			regs[i].pci_phys_hi |= PCI_REG_REL_M;
			regs[i].pci_phys_low = PCIRM_LOADDR(base);
			regs[i].pci_phys_mid = PCIRM_HIADDR(base);
			/*
			 * currently support 32b address space
			 * assignments only.
			 */
			if (PCI_REG_ADDR_G(regs[i].pci_phys_hi) ==
			    PCI_REG_ADDR_G(PCI_ADDR_MEM64)) {
				regs[i].pci_phys_hi ^=
				    PCI_ADDR_MEM64 ^ PCI_ADDR_MEM32;
			}

			rval = pcirm_append_assigned_prop(dip, &regs[i]);

			kmem_free(regs, rlen);
			if (assigned) {
				kmem_free(assigned, alen);
			}

			return (rval);

		} /* if */
next_reg:
		continue;

	} /* for */
cleanup:
	if (assigned) {
		kmem_free(assigned, alen);
	}
	kmem_free(regs, rlen);
	return (PCIRM_FAILURE);
}

static boolean_t
pcirm_match_reg_assigned_entry(pci_regspec_t *reg, pci_regspec_t *assigned)
{
	return (PCI_REG_REG_G(reg->pci_phys_hi) ==
	    PCI_REG_REG_G(assigned->pci_phys_hi));
}

static boolean_t
pcirm_match_reg_entry(uint64_t len, pcirm_type_t type, pci_regspec_t *reg)
{
	/* match type and size */
	if (PCIRM_REQTYPE(reg->pci_phys_hi) == type) {
#if 0
		DEBUGPRT(CE_CONT, "len=%x, regsize=%x %x",
		    (uint32_t)len, reg->pci_size_hi, reg->pci_size_low);
#endif
		if (len == REG_TO_SIZE(*reg))
			return (B_TRUE);
	}

	return (B_FALSE);
}

static uint_t
pcirm_type2prop(pcirm_type_t type, uint64_t base)
{
	uint_t prop_type = 0;

	switch (type) {
	case PCIRM_TYPE_PMEM:
	case PCIRM_TYPE_MEM:
		prop_type |= (base > PCIRM_4GIG_LIMIT) ?
		    PCI_ADDR_MEM64 :PCI_ADDR_MEM32;
		break;
	case PCIRM_TYPE_IO:
		prop_type |= PCI_ADDR_IO;
		break;
	default:
		break;
	}

	if (type == PCIRM_TYPE_PMEM)
		prop_type |= PCI_REG_PF_M;

	return (prop_type);
}

static int
pcirm_append_assigned_prop(dev_info_t *dip, pci_regspec_t *newone)
{
	int		alen;
	pci_regspec_t	*assigned;
	pci_regspec_t	*newreg;
	uint_t		status;

	status = ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "assigned-addresses", (caddr_t)&assigned, &alen);
	switch (status) {
		case DDI_PROP_SUCCESS:
		break;
		case DDI_PROP_NO_MEMORY:
			return (PCIRM_FAILURE);
		default:
			(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
			    "assigned-addresses", (int *)newone,
			    sizeof (*newone)/sizeof (int));

			return (PCIRM_SUCCESS);
	}

	/*
	 * Allocate memory for the existing
	 * assigned-addresses(s) plus one and then
	 * build it.
	 */

	newreg = kmem_zalloc(alen+sizeof (*newone), KM_SLEEP);

	bcopy(assigned, newreg, alen);
	bcopy(newone, (char *)newreg + alen, sizeof (*newone));

	/*
	 * Write out the new "assigned-addresses" spec
	 */
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
	    "assigned-addresses", (int *)newreg,
	    (alen + sizeof (*newone))/sizeof (int));

	kmem_free((caddr_t)newreg, alen+sizeof (*newone));
	kmem_free((caddr_t)assigned, alen);

	return (PCIRM_SUCCESS);
}

/* Put the specified resource to "ranges" property */
int
pcirm_prop_add_ranges(dev_info_t *dip, uint64_t base, uint64_t len,
    pcirm_type_t type)
{
	ppb_ranges_t *ranges, *new_ranges;
	int i, rlen, count, new_rlen, rval;

	/* this map is for bridge range change */
	rval = ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS |
	    DDI_PROP_NOTPROM, "ranges", (caddr_t)&ranges, &rlen);
	if (rval == DDI_PROP_NOT_FOUND) {
		/* add the new range and create "ranges" property */
		ranges = kmem_alloc(sizeof (ppb_ranges_t), KM_SLEEP);
		ranges->child_high = ranges->parent_high = type;
		BASE_TO_RANGES(base, ranges[0]);
		SIZE_TO_RANGES(len, ranges[0]);
		rval = ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		    "ranges", (int *)ranges, sizeof (ppb_ranges_t) /
		    sizeof (int));
		kmem_free(ranges, sizeof (ppb_ranges_t));

		return (rval);
	}
	if (rval != DDI_PROP_SUCCESS) {
		return (rval);
	}
	/* Update dip's "ranges" */
	count = rlen / sizeof (ppb_ranges_t);
	for (i = 0; i < count; i++) {
		/* Match the given type with the range entry */
		if (PCIRM_RESOURCETYPE(ranges[i].child_high) == type) {
			/*
			 * There is an existing range with the same type,
			 * try to merge (base,len) with it.
			 */
			if ((base + len) == (RANGES_TO_BASE(ranges[i]))) {
				/* At head, update the base of "ranges" */
				BASE_TO_RANGES(base, ranges[i]);
				break;
			}
			if (base == ((RANGES_TO_BASE(ranges[i])) +
			    (RANGES_TO_SIZE(ranges[i])))) {
				/* At end, update the len of "ranges" */
				len += (RANGES_TO_SIZE(ranges[i]));
				SIZE_TO_RANGES(len, ranges[i]);
				break;
			}
			kmem_free(ranges, rlen);
			DEBUGPRT(CE_WARN, "Failed to merge resource to ranges:"
			    "dip=%p, type=%x, base=%" PRIx64 ","
			    " len=%" PRIx64 "",
			    (void *)dip, type, base, len);

			return (PCIRM_FAILURE);
		}
	}
	if (i < count) {
		/* ranges is merged */
		rval = ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		    "ranges", (int *)ranges, count);
		kmem_free(ranges, rlen);

		return (rval);
	}
	/* Append the new range and update "ranges" property */
	new_rlen = rlen + sizeof (ppb_ranges_t);
	new_ranges = kmem_alloc(new_rlen, KM_SLEEP);
	bcopy(ranges, new_ranges, rlen);
	new_ranges[count].child_high = ranges[count].parent_high = type;
	BASE_TO_RANGES(base, new_ranges[count]);
	SIZE_TO_RANGES(len, new_ranges[count]);
	rval = ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
	    "ranges", (int *)new_ranges, new_rlen);
	kmem_free(new_ranges, new_rlen);
	kmem_free(ranges, rlen);

	return (rval);
}

/*
 * For a bridge node, add a range of bus numbers to its bus-range property.
 */
int
pcirm_prop_add_bus_range(dev_info_t *dip, uint64_t base, uint64_t len)
{
	pci_bus_range_t *bus_rangep, bus_range;
	int rlen, rval;

	rval = ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "bus-range", (caddr_t)&bus_rangep, &rlen);
	if (rval == DDI_SUCCESS) {
		/* Merge the newly allocated bus range to the old one */
		if (base < bus_rangep->lo && (base + len) == bus_rangep->hi) {
			/* on the left */
			bus_rangep->lo = (uint32_t)base;
		} else if (base == (uint64_t)(bus_rangep->hi + 1)) {
			/* on the right */
			bus_rangep->hi += len;
		} else {
			/* not a proper (base,len) to add to this bus-range */

			DEBUGPRT(CE_WARN, "pcirm_prop_add_bus_range: "
			    "failed to free bus number, dip=%p "
			    "base=%" PRIx64 ", len=%" PRIx64 " \n",
			    (void *) dip, base, len);

			kmem_free(bus_rangep, rlen);
			return (PCIRM_FAILURE);
		}
		rval = ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		    "bus-range", (int *)bus_rangep, rlen / sizeof (int));
		kmem_free(bus_rangep, rlen);
	} else if (rval == DDI_PROP_NOT_FOUND) {
		/* the device has no existing bus-range property */
		bus_rangep = &bus_range;
		bus_rangep->lo = (uint32_t)base;
		bus_rangep->hi = (uint32_t)(base + len - 1);
		rval = ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		    "bus-range", (int *)bus_rangep,
		    (sizeof (pci_bus_range_t)) / sizeof (int));
	}

	return (rval);
}

/*
 * Add a piece of resource to property "assigned-addresses" (when allocate)
 * or "available" (when free resource).
 */
int
pcirm_prop_add_addr(dev_info_t *dip, char *prop_name,
    uint64_t base, uint64_t len, pcirm_type_t type)
{
	pci_regspec_t *regs, *new_regs;
	int rlen, i, rval = PCIRM_FAILURE;
	boolean_t is_available;
	int flags = DDI_PROP_DONTPASS;

	if (!dip || !len)
		return (rval);

	is_available = strcmp(prop_name, "available") == 0;
	if (is_available)
		flags |= DDI_PROP_NOTPROM;

	/*
	 * read the property and put the new reg
	 * at the tail
	 */
	rval = ddi_getlongprop(DDI_DEV_T_ANY, dip, flags,
	    prop_name, (caddr_t)&regs, &rlen);
	if (rval == DDI_SUCCESS) {
		new_regs = kmem_zalloc(rlen + sizeof (pci_regspec_t), KM_SLEEP);
		bcopy(regs, new_regs, rlen);
		kmem_free(regs, rlen);
		i = rlen /  sizeof (pci_regspec_t);
		rlen += sizeof (pci_regspec_t); /* length of new_regs */
	} else if (rval == DDI_PROP_NOT_FOUND) {
		/* no "assigned-addresses" property before, create one */
		rlen = sizeof (pci_regspec_t);
		new_regs = kmem_zalloc(rlen, KM_SLEEP);
		i = 0;
	} else {
		return (PCIRM_FAILURE);
	}
	new_regs[i].pci_phys_hi = pcirm_type2prop(type, base);

	/*
	 * According to page 15 of 1275 spec, bit "n" of "assigned-addresses"
	 * and "available" should be set to 1.
	 */
	new_regs[i].pci_phys_hi |= PCI_REG_REL_M;

	new_regs[i].pci_phys_mid = (uint32_t)(base >> 32);
	new_regs[i].pci_phys_low = (uint32_t)base;
	new_regs[i].pci_size_hi = (uint32_t)(len >> 32);
	new_regs[i].pci_size_low = (uint32_t)len;

	rval = ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
	    prop_name, (int *)new_regs, rlen / sizeof (int));
	kmem_free(new_regs, rlen);

	if (is_available)
		(void) pcirm_prop_merge_available(dip, type);

	return (PCIRM_SUCCESS);
}
/*
 * Utilities
 */

/*
 * pcirm_access_handle:
 *    Get the serial synchronization object before returning.
 */
pcirm_handle_impl_t *
pcirm_access_handle(dev_info_t *rcdip)
{
	mutex_enter(&DIP2RCRBL(rcdip)->pcirm_mutex);
	while (DIP2RCHDL(rcdip)) {
		/* handle in use, wait for it being released */
		if (cv_wait_sig(&DIP2RCRBL(rcdip)->pcirm_cv,
		    &DIP2RCRBL(rcdip)->pcirm_mutex) == 0) {
			/* interrupted by signal */
			mutex_exit(&DIP2RCRBL(rcdip)->pcirm_mutex);
			return (NULL);
		}
	}

	DIP2RCHDL(rcdip) = kmem_zalloc(sizeof (pcirm_handle_impl_t), KM_SLEEP);
	mutex_exit(&DIP2RCRBL(rcdip)->pcirm_mutex);

	return (DIP2RCHDL(rcdip));
}

void
pcirm_release_handle(dev_info_t *rcdip)
{
	mutex_enter(&DIP2RCRBL(rcdip)->pcirm_mutex);
	kmem_free(DIP2RCHDL(rcdip), sizeof (pcirm_handle_impl_t));
	DIP2RCHDL(rcdip) = NULL;
	cv_broadcast(&DIP2RCRBL(rcdip)->pcirm_cv);
	mutex_exit(&DIP2RCRBL(rcdip)->pcirm_mutex);
}

/* Get parent. If parent is not a PCI bridge node, then return NULL. */
dev_info_t *
pcirm_get_pci_parent(dev_info_t *dip)
{
	dip = ddi_get_parent(dip);
	if (pcie_is_pci_bridge(dip)) {
		return (dip);
	}
	return (NULL);
}

/* Check if a node is a pci/pcie bridge node */
boolean_t
pcirm_is_pci_root(dev_info_t *dip)
{
	if (!pcie_is_pci_bridge(dip)) {
		return (B_FALSE);
	}
	for (dip = ddi_get_parent(dip); dip != NULL;
	    dip = ddi_get_parent(dip)) {
		if (pcie_is_pci_bridge(dip)) {
			return (B_FALSE);
		}
	}
	return (B_TRUE);
}

/* Check if the pdip is the parent/grandpanrent of dip */
boolean_t
pcirm_is_on_parent_path(dev_info_t *dip, dev_info_t *pdip)
{

	for (dip = ddi_get_parent(dip); dip != NULL;
	    dip = ddi_get_parent(dip)) {
		if (pdip == dip) {
			return (B_TRUE);
		}
	}
	return (B_FALSE);
}

static void
pcirm_addr_to_range(pci_regspec_t *reg, uint64_t *range_base,
    uint64_t *range_len)
{
	*range_base = ((uint64_t)(reg->pci_phys_mid) << 32) |
	    ((uint64_t)(reg->pci_phys_low));
	*range_len = ((uint64_t)(reg->pci_size_hi) << 32) |
	    (uint64_t)(reg->pci_size_low);
}

/*
 * Free resource
 */

/*
 * Remove a piece of bus number range (base,len) from bus-range property
 */
int
pcirm_prop_rm_bus_range(dev_info_t *dip, uint64_t base, uint64_t len)
{
	pci_bus_range_t *range;
	int rlen, end, rval;

	/*
	 * read the "bus-range" property.
	 */
	rval = ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "bus-range", (caddr_t)&range, &rlen);
	if (rval != DDI_SUCCESS) {
		return (rval);
	}
	end = base + len - 1;
	if (base == range->hi) {
		if (end == range->lo) { /* free all range */
			rval = ndi_prop_remove(DDI_DEV_T_NONE, dip,
			    "bus-range");
			return (rval);
		} else {
			/* new base */
			range->hi = end + 1;
		}
	} else if (end == range->lo) {
		/* remove right part */
		range->lo = base - 1;
	} else {
		/*
		 * It does not make sense to free a range in the middle
		 * in the bus number case.
		 */
		return (PCIRM_FAILURE);
	}
	rval = ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
	    "bus-range", (int *)range, rlen / sizeof (int));
	kmem_free(range, rlen);

	return (rval);
}

/*
 *         PCIRM algorithm overall description
 *
 * -pcirm_find_resource(): resource rebalance interface
 * |
 * +-> pcirm_find_resource_impl(): implementation of rebalance interface
 *      |
 *      |
 *      +-> pcirm_calculate_tree_requests()
 *      |      recusive function, calculate the resource requirement for the
 *      |      device sub-tree underneath, not including this node. It calls
 *      |      pcirm_calculate_bridge_req() for each bridge underneath
 *      |
 *      +-> pcirm_calculate_bridge_requests()
 *      |      | calculate the resource requirement for this bridge node
 *      |      |
 *      |      +-> pcirm_calculate_bridge_req()
 *      |             calculate for a given type
 *      |
 *      |
 *      +-> for each type of resource requested,
 *	     pcirm_find_for_one_type()
 *               |  try to find the specified resources of the given type
 *               |  in the device tree. It cascades up the device tree
 *               |  from the given dip, trying to find a bridge node under
 *               |  which resource requirements can be met by shuffling
 *               |  resources, the process stops at root complex node.
 *               |
 *               +-> pcirm_try_curr_bridge()
 *                      | get the resource range of current bridge node, and
 *                      | try to fit the calculated resource requirement into
 *                      | this range.
 *                      |
 *                      +-> pcirm_get_curr_bridge_range()
 *                      |
 *                      +-> pcirm_fit_into_range()
 *
 * Key functions:
 *
 * - pcirm_calculate_bridge_req():  resource requirement calculation
 *    |
 *    +-> pcirm_sort_immediate_children_reqs()
 *    |    |  calculate resource requirements for all immediate chilren nodes,
 *    |    |  and insert the reqs into two sorted list depending on whether
 *    |    |  the address requested is relocatable
 *    |    |    - relo list: sorted by align/size, desending order
 *    |    |    - fixed list: sorted by address, ascending order
 *    |    |  two list of pcirm_bridge_req_t are returned when done
 *    |    |
 *    |    +-> for each immediate children node,
 *    |         pcirm_get_size_from_dip() calcualtes its resource req,
 *    |           - leaf node:   from the properties
 *    |           - bridge node: from cached calculation results in devinfo node
 *    |         the resource req is expressed via a list of pcirm_bridge_req_t,
 *    |           - leaf node:   one pcirm_bridge_req_t for each BAR#
 *    |           - bridge node: only one pcirm_bridge_req_t, which could have
 *    |                          a list of relo entries describing the resource
 *    |                          reqs of its children
 *    |
 *    +-> fixed_list == NULL ?
 *          |
 *          |
 *          |Y
 *          +-> all requests are relocatable
 *          |      |
 *          |      +-> pcirm_merge_relo_brgreq_in_list()
 *          |      |     each pcirm_bridge_req_t in the list has a relo list
 *          |      |     describing its resource requirements, for each bridge
 *          |      |     br (pcirm_bridge_req_t) merge its relo list to one
 *          |      |     entry as each bridge has a single range
 *          |      |
 *          |      +-> pcirm_merge_relo_brgreq_list()
 *          |      |     merge this relo pcirm_bridge_req_t list into a single
 *          |      |     pcirm_bridge_req_t, who has only one relo range
 *          |      |
 *          |      +-> pcirm_update_bridge_req()
 *          |            cache the pcirm_bridge_req_t created in the previous
 *          |            step in devinfo node, pcirm_get_size_from_dip() will
 *          |            use this as the bridge resource req.
 *          |N
 *          +-> there are fixed requests
 *                 |
 *                 +-> pcirm_cal_fixed_bdgreq()
 *                 |      | calculate address range for a bridge, which consists
 *                 |      | of a fixed range of resource, and optionally a list
 *                 |      | of relocatable resources.
 *                 |      |
 *                 |      +-> pcirm_merge_relo_in_fixed_brs()
 *                 |      |      for all brs in the fixed_list which has both
 *                 |      |      fixed addresses and relo addresses, transformat
 *                 |      |      their relo addresses to fixed.
 *                 |      |
 *                 |      +-> pcirm_fill_relobrs_to_fixed_holes()
 *                 |      |      layout the fixed addresses in the fixed_list,
 *                 |      |      and then fill the brs in relo_list to the
 *                 |      |      holes between fixed addresses.
 *                 |      |
 *                 |      +-> pcirm_merge_fixed_bdgreq_list()
 *                 |      |      now that we have a range of fixed addresses,
 *                 |      |      merge the fixed_list into a single fixed br
 *                 |      |
 *                 |      +-> pcirm_merge_relo_bdgreq_list()
 *                 |             if there are still relo brs which cannot be
 *                 |             filled into the holes, merge them into the
 *                 |             fixed br created, so we end up having a fixed
 *                 |             range and a list of relo ranges as the bridge
 *                 |             request size.
 *                 |
 *                 +-> pcirm_update_bridge_req()
 *                       cache the pcirm_bridge_req_t created in the previous
 *                       step in devinfo node
 *
 * - pcirm_fit_into_range()
 *    |
 *    +-> fixed_list == NULL ?
 *          |
 *          |Y
 *          +-> all requests are relocatable
 *          |     |
 *          |     +-> pcirm_relo2fixed_at_low_addr()
 *          |            layout the relo brs in relo_list according to the
 *          |            range base, turn all of them to fixed brs
 *          |
 *          |N
 *          +-> there are fixed requests
 *                |
 *                +-> pcirm_relo2fixed_at_high_addr()
 *                |      fill relo brs into the hole of (range_base, fixed_base)
 *                |
 *                +-> pcirm_relo2fixed_at_low_addr()
 *                |      fill relo brs into the hole of (fixed_end, range_end)
 *                |
 *                +-> pcirm_mark_rebalance_map()
 *                +-> pcirm_mark_rebalance_map_for_subtree()
 *                       mark the resource map info on the device nodes whose
 *                       resources have been changed, and pcirm_commit_resource
 *                       will update properties according to these resource
 *                       maps. Also these maps are exported to caller to re-
 *                       program the H/W accordingly.
 */
/*
 * Rebalance implementation.
 */
boolean_t
pcirm_find_resource_impl(dev_info_t *dip, uint_t dev_num, int types,
    pcirm_handle_impl_t **handle)
{
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);
	pcirm_bridge_req_t *br_list;
	pcirm_handle_impl_t *hdl_impl;
	dev_info_t *rcdip;

	if (types & PCIRM_TYPE_IO_M) {
		/* IO resource rebalance is currently not supported */
		DEBUGPRT(CE_WARN, "pcirm_find_resource_impl: "
		    "Bad argument, rebalance of IO resource is "
		    "not supported, passed in types=%x", types);
		return (PCIRM_INVAL_ARG);
	}
	if (!pcie_is_pci_bridge(dip)) {
		/*
		 * dip must be pci bridge because only bridge nodes have
		 * resources ranges for children.
		 */
		DEBUGPRT(CE_WARN, "pcirm_find_resource_impl: "
		    "Bad argument, dip (%p) is not a PCI bridge node. \n",
		    (void *)dip);
		return (PCIRM_INVAL_ARG);
	}

	rcdip = PCIE_GET_RC_DIP(bus_p);
	ASSERT(rcdip);

	/* Check if resource rebalance should be disabled for this fabric */
	if (pcirm_is_rebalance_disabled(rcdip, types)) {
		DEBUGPRT(CE_WARN, "pcirm_find_resource_impl: "
		    "Resource rebalance is disabled for root complex "
		    "dip=%p", (void *)rcdip);
		return (PCIRM_FAILURE);
	}

	/* get the rebalance handle of this root complex */
	hdl_impl = pcirm_access_handle(rcdip);
	if (!hdl_impl)
		return (PCIRM_FAILURE);

	hdl_impl->rc_dip = rcdip;
	hdl_impl->top_dip = dip;
	hdl_impl->ap_dip = dip;
	hdl_impl->types = types;
	/*
	 * In hotplug case, when a new device is plugged to a SHPC slot which
	 * has some existing siblings, then try to find resource from available
	 * resources first.
	 */
	if (dev_num != PCIRM_ALL_DEVICES) {
		if (pcirm_try_available_resources(dip, dev_num, types) ==
		    PCIRM_SUCCESS) {
			return (PCIRM_SUCCESS);
		}
	}
	/*
	 * For dip's children devices, caculate the resource requirement for
	 * them and mark the br_list on the bridge nodes among them.
	 */
	if (pcirm_calculate_tree_requests(dip, types) != PCIRM_SUCCESS)
		return (PCIRM_FAILURE);
	if (pcirm_calculate_bridge_requests(dip, types) != PCIRM_SUCCESS)
		return (PCIRM_FAILURE);

	br_list = DIP2RBL(dip)->pci_bdg_req;
	if (!br_list) {
		DEBUGPRT(CE_WARN, "pcirm_find_resource_impl: "
		    "No resource requirement found, "
		    "hdl_impl=%p \n", (void *)hdl_impl);
		return (PCIRM_SUCCESS);
	}

#if defined(PCIRM_DEBUG)
	pcirm_debug_dump_bdgreq_list(br_list, "pcirm_find_resource_impl");
#endif
	for (; br_list != NULL; br_list = br_list->next) {

		if (pcirm_find_for_one_type(br_list) !=
		    PCIRM_SUCCESS) {

#if defined(PCIRM_DEBUG)
			pcirm_debug_dump_rbl_map(hdl_impl->rbl_head);
#endif
			/*
			 * If any of the resource request can not be satisfied,
			 * cleanup everything including the handle.
			 */
			DEBUGPRT(CE_WARN, "pcirm_find_resource_impl: "
			    "failed to find resource for this type=%x, "
			    "hdl_impl=%p, clean up! \n",
			    br_list->type, (void *)hdl_impl);
			(void) pcirm_free_handle((pcirm_handle_t)hdl_impl);
			return (PCIRM_NO_RESOURCE);
		}
	}
#if defined(PCIRM_DEBUG)
	pcirm_debug_dump_rbl_map(hdl_impl->rbl_head);
#endif
	DEBUGPRT(CE_WARN, "pcirm_find_resource_impl: "
	    "Rebalance succeed: hdl_impl=%p \n", (void *)hdl_impl);
	*handle = hdl_impl;
	return (PCIRM_SUCCESS);
}

static void
pcirm_free_bridge_req_list(pcirm_bridge_req_t *br)
{
	pcirm_bridge_req_t *next;

	while (br) {
		next = br->next;
		pcirm_free_bridge_req(br);
		br = next;
	}
}

static void
pcirm_free_bridge_req(pcirm_bridge_req_t *req)
{
	pcirm_addr_list_t *addr_tmp;
	pcirm_size_list_t *size_tmp;

	LIST_FREE(req->relo_list, size_tmp);
	LIST_FREE(req->fixed_list, addr_tmp);
	kmem_free(req, sizeof (*req));
}

/* Free all struct pcirm_bridge_req_t on a node */
static int
pcirm_free_bridge_req_list_walker(dev_info_t *dip, void *arg)
{
	_NOTE(ARGUNUSED(arg))

	pcirm_free_bridge_req_list(DIP2RBL(dip)->pci_bdg_req);
	DIP2RBL(dip)->pci_bdg_req = NULL;

	if (!pcie_is_pci_bridge(dip)) {
		/* pcirm_bridge_req_t is allocated for pci bridge nodes only */
		return (DDI_WALK_PRUNECHILD);
	}
	return (DDI_WALK_CONTINUE);
}

int
pcirm_free_handle_impl(pcirm_handle_impl_t *handle)
{
	pcirm_handle_impl_t *hdim = handle;
	pcirm_rbl_map_t *tmp;
	dev_info_t *dip, *prev;
	int circular_count, i;

	/* free all rebalance map */
	dip = hdim->rbl_head;
	for (; (dip && DIP2RBL(dip)); ) {
		prev = dip;
		dip = DIP2RBL(dip)->next;
		LIST_FREE((DIP2RBL(prev)->pci_rbl_map), tmp);
		DIP2RBL(prev)->next = NULL;
	}

	/* free all calculated information (size, alignment) */
	dip = hdim->top_dip;
	if (dip && ddi_get_child(dip)) {
		ndi_devi_enter(dip, &circular_count);
		ddi_walk_devs(ddi_get_child(dip),
		    pcirm_free_bridge_req_list_walker, NULL);
		ndi_devi_exit(dip, circular_count);
	}

	/* free rc rangeset */
	for (i = 0; i < sizeof (hdim->rc_rangeset)/
	    sizeof (pcirm_range_t *); i++) {
		pcirm_destroy_rangeset(hdim->rc_rangeset[i]);
	}
	if (dip) {
		pcirm_free_bridge_req_list(DIP2RBL(dip)->pci_bdg_req);
	}
	pcirm_release_handle(hdim->rc_dip);

	return (PCIRM_SUCCESS);
}

/*
 * For commit rebalance, update the impacted properties for a pcirm_rbl_map_t
 * in a device node in rebalance map.
 */
int
pcirm_rbl_update_property(dev_info_t *dip, pcirm_rbl_map_t *rbl)
{
	int rval;

	if (rbl->type == PCIRM_TYPE_BUS) {
		ASSERT((rbl->bar_off == PCIRM_BAR_OFF_BRIDGE_RANGE) ||
		    (rbl->bar_off == PCIRM_BAR_OFF_BUS_NUM) ||
		    (rbl->bar_off == PCIRM_BAR_OFF_IOV_VF));

		if (rbl->bar_off != PCIRM_BAR_OFF_BRIDGE_RANGE) {
			/* only bridge has "bus-range" prop */
			return (PCIRM_SUCCESS);
		}

		rval = pcirm_prop_modify_bus_range(dip,
		    rbl->new_base, rbl->new_len);
		if (rval != PCIRM_SUCCESS)
			goto fail;

		/* Propogate the bus number change to children */
		rval = pcirm_update_bus_number_impacted(dip, rbl);
		if (rval != PCIRM_SUCCESS)
			goto fail;

		return (rval);
	}
	/*
	 * Modify "ranges" or "assigned-addresses" of dip.
	 * Modify "available" of the parent of dip.
	 */
	rval = pcirm_prop_modify_addr(dip, rbl);
	/* When commit it, it must succeed. Or, unknown problems happen. */
	if (rval != PCIRM_SUCCESS) {
		/*
		 * Unexpect issue happen.
		 */
fail:
		DEBUGPRT(CE_WARN, "Failed to update property for committing "
		    "rebalance! dip=%p, type=%x\n", (void *) dip, rbl->type);
	}

	return (rval);
}

void
pcirm_update_bus_props(dev_info_t *dip)
{
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);

	if (bus_p->bus_props_inited) {
		pcie_fini_bus_props(dip);
		pcie_init_bus_props(dip);
	}
}

/*
 * For a bridge node, update bus-range property according to new base,len.
 */
static int
pcirm_prop_modify_bus_range(dev_info_t *dip, uint64_t base, uint64_t len)
{
	pci_bus_range_t bus_range;

	bus_range.lo = (uint32_t)base;
	bus_range.hi = (uint32_t)(base + len - 1);

	if (ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
	    "bus-range", (int *)&bus_range,
	    sizeof (pci_bus_range_t) / sizeof (int)) == DDI_SUCCESS) {

		return (PCIRM_SUCCESS);
	}

	return (PCIRM_FAILURE);
}

/*
 * Update "reg" and "assigned-addresses" properties of the children,
 * and also bus_bdf in bus_t structures, with new bus number.
 * Also update x86 bad bridge records which are incapable of MMIO
 * config access.
 *
 * This function assumes the "bus-range" property and bus_bdf of
 * 'dip' has been updated.
 */

static int
pcirm_update_bus_number_impacted(dev_info_t *dip, pcirm_rbl_map_t *rbl)
{
	dev_info_t *cdip, *rp_dip;
	pcie_bus_t *bus_p, *rp_bus_p;
	pci_regspec_t *regs;
	int rlen, rcount, i;
	uint32_t bus = (uint32_t)rbl->new_base;

#if defined(__i386) || defined(__amd64)
	uint32_t len = (uint32_t)rbl->new_len;

	bus_p = PCIE_DIP2BUS(dip);
	ASSERT(bus_p != NULL);

	/* update bus range of bad bridge, if it is */
	pci_cfgacc_update_bad_bridge_range(bus_p->bus_bdf,
	    (uchar_t)bus, (uchar_t)(bus + len - 1));
#endif /* defined(__i386) || defined(__amd64) */

	cdip = ddi_get_child(dip);
	for (; cdip != NULL; cdip = ddi_get_next_sibling(cdip)) {
		/*
		 * Update bus_t structure
		 */
		bus_p = PCIE_DIP2BUS(cdip);
		ASSERT(bus_p != NULL);
		if (bus_p) {
			bus_p->bus_bdf &= 0xff;
			bus_p->bus_bdf |= bus << 8;
		}

		rp_dip = bus_p->bus_rp_dip;
		if (rp_dip != NULL) {
			/* root port bus_bdf has been updated */
			rp_bus_p = PCIE_DIP2BUS(rp_dip);
			bus_p->bus_rp_bdf = rp_bus_p->bus_bdf;
		}

#if defined(__i386) || defined(__amd64)
		/* update bdf of bad bridge, if it is */
		pci_cfgacc_update_bad_bridge(rbl->base, bus);
#endif /* defined(__i386) || defined(__amd64) */

		/*
		 * Update "reg" property
		 */
		if (ddi_getlongprop(DDI_DEV_T_ANY, cdip, DDI_PROP_DONTPASS,
		    "reg", (caddr_t)&regs, &rlen) != DDI_SUCCESS) {

			goto update_assigned;
		}
		rcount = rlen / sizeof (pci_regspec_t);
		for (i = 0; i < rcount; i++) {
			regs[i].pci_phys_hi &= ~PCI_REG_BUS_M;
			regs[i].pci_phys_hi |= bus << PCI_REG_BUS_SHIFT;
		}

		if (ndi_prop_update_int_array(DDI_DEV_T_NONE, cdip,
		    "reg", (int *)regs, rlen/sizeof (int)) == PCIRM_SUCCESS)
		kmem_free((caddr_t)regs, rlen);
update_assigned:
		/*
		 * Update "assigned-addresses" property, if exists
		 */
		if (ddi_getlongprop(DDI_DEV_T_ANY, cdip, DDI_PROP_DONTPASS,
		    "assigned-addresses", (caddr_t)&regs, &rlen)
		    != DDI_SUCCESS) {

			continue;
		}
		rcount = rlen / sizeof (pci_regspec_t);
		for (i = 0; i < rcount; i++) {
			regs[i].pci_phys_hi &= ~PCI_REG_BUS_M;
			regs[i].pci_phys_hi |= bus << PCI_REG_BUS_SHIFT;
		}

		(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, cdip,
		    "assigned-addresses", (int *)regs,
		    rlen/sizeof (int));
		kmem_free((caddr_t)regs, rlen);
	}

	return (PCIRM_SUCCESS);
}

/*
 * For rebalance, commit the moving by updating the impacted properties.
 * Modify "assigned-addresses" of dip. If dip is bridge, also check if
 * need to modify its "ranges".
 * Modify "available" of the parent of dip.
 */
static int
pcirm_prop_modify_addr(dev_info_t *dip, pcirm_rbl_map_t *rbl)
{
	pci_regspec_t *regs, *new_regs;
	ppb_ranges_t *ranges, *new_ranges;
	int rlen, count, i, j, rval, new_rlen;
	uint64_t old_base;

	if (rbl->bar_off == PCIRM_BAR_OFF_BRIDGE_RANGE) {
		/* this map is for bridge range change */
		rval = ddi_getlongprop(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM,
		    "ranges", (caddr_t)&ranges, &rlen);
		if (rval != DDI_SUCCESS) {
			if (rbl->len) {
				/*
				 * This is not a new range entry to add,
				 * something wrong.
				 */
				DEBUGPRT(CE_WARN,
				    "Failed to get ranges property"
				    "dip=%p, rval=%d\n", (void *) dip, rval);

				return (PCIRM_FAILURE);
			}
			/* add the new range and create "ranges" property */
			ranges = kmem_alloc(sizeof (ppb_ranges_t), KM_SLEEP);
			ranges->child_high = ranges->parent_high =
			    pcirm_type2prop(rbl->type, rbl->new_base);
			BASE_TO_RANGES(rbl->new_base, ranges[0]);
			SIZE_TO_RANGES(rbl->new_len, ranges[0]);
			(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
			    "ranges", (int *)ranges, sizeof (ppb_ranges_t) /
			    sizeof (int));
			kmem_free(ranges, sizeof (ppb_ranges_t));

			return (PCIRM_SUCCESS);
		}

		if (rbl->new_len == 0) {
			/* Remove this entry is new length is 0 */
			new_ranges = kmem_zalloc(rlen, KM_SLEEP);

			count = rlen / sizeof (ppb_ranges_t);
			for (i = 0, j = 0; i < count; i++) {
				/* Match the given type with the range entry */
				if (PCIRM_RESOURCETYPE(ranges[i].child_high)
				    == rbl->type) {

					old_base = RANGES_TO_BASE(ranges[i]);
					if (old_base == rbl->base)
						continue;

				}

				new_ranges[j] = ranges[i];
				j++;
			}

			(void) ndi_prop_remove(DDI_DEV_T_NONE, dip, "ranges");
			if (j != 0) {
				(void) ndi_prop_update_int_array(
				    DDI_DEV_T_NONE, dip, "ranges",
				    (int *)new_ranges,
				    (j * sizeof (ppb_ranges_t)) / sizeof (int));
			}
			kmem_free(new_ranges, rlen);
			kmem_free(ranges, rlen);

			return (PCIRM_SUCCESS);
		}

		/* Update dip's "ranges" */
		count = rlen / sizeof (ppb_ranges_t);
		for (i = 0; i < count; i++) {
			/* Match the given type with the range entry */
			if (PCIRM_RESOURCETYPE(ranges[i].child_high) ==
			    rbl->type) {

				old_base = RANGES_TO_BASE(ranges[i]);
				if (old_base == rbl->base) {
					/*
					 * Matched, modify it with new base/len
					 */
					BASE_TO_RANGES(rbl->new_base,
					    ranges[i]);
					SIZE_TO_RANGES(rbl->new_len, ranges[i]);
					(void) ndi_prop_update_int_array(
					    DDI_DEV_T_NONE, dip, "ranges",
					    (int *)ranges, rlen / sizeof (int));

					break;
				}
			}
		}

		if (i >= count) {
			/* no matched ranges entry found */
			if (rbl->len) {
				/*
				 * This is not a new range entry to add,
				 * something wrong.
				 */
				DEBUGPRT(CE_WARN, "Failed to match an old range"
				    "dip=%p, count=%d\n",
				    (void *) dip, count);

				return (PCIRM_FAILURE);
			}
			/* append a new range entry */
			new_rlen = rlen + sizeof (ppb_ranges_t);
			new_ranges = kmem_zalloc(new_rlen, KM_SLEEP);
			bcopy(ranges, new_ranges, rlen);
			new_ranges[count].child_high =
			    new_ranges[count].parent_high =
			    pcirm_type2prop(rbl->type, rbl->new_base);
			BASE_TO_RANGES(rbl->new_base, new_ranges[count]);
			SIZE_TO_RANGES(rbl->new_len, new_ranges[count]);
			(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
			    "ranges", (int *)new_ranges,
			    new_rlen / sizeof (int));
			kmem_free(new_ranges, new_rlen);
		}
		kmem_free(ranges, rlen);

	} else if (rbl->bar_off == PCIRM_BAR_OFF_IOV_VF) {
		DEBUGPRT(CE_WARN, "Found IOV VF reg in rebalance map,"
		    "dip=%p, rval=%d", (void *) dip, rval);

	} else { /* for addresses assigned to a leaf or a bridge device */
		rval = ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		    "assigned-addresses", (caddr_t)&regs, &rlen);
		if (rval != DDI_SUCCESS) {
			if (rbl->len) {
				/*
				 * This is not a new assigned-addresses entry
				 *  to add, something wrong.
				 */
				DEBUGPRT(CE_WARN, "Failed to get"
				    " assigned-addresses property"
				    "dip=%p, rval=%d\n", (void *) dip, rval);

				return (PCIRM_FAILURE);
			}
			/*
			 * add the new assigned-addresses and create
			 * "assigned-addresses" property.
			 */
			regs = kmem_alloc(sizeof (pci_regspec_t), KM_SLEEP);
			regs->pci_phys_hi =
			    pcirm_type2prop(rbl->type, rbl->new_base);
			regs->pci_phys_hi |=  PCI_REG_REL_M;
			BASE_TO_REG(rbl->new_base, regs[0]);
			SIZE_TO_REG(rbl->new_len, regs[0]);
			(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
			    "assigned-addresses", (int *)regs,
			    sizeof (pci_regspec_t) / sizeof (int));
			kmem_free(regs, sizeof (pci_regspec_t));

			return (PCIRM_SUCCESS);
		}

		count = rlen / sizeof (pci_regspec_t);
		/* Update "assigned-addresses" */
		for (i = 0; i < count; i++) {
			/* Match the given type with the reg */
			pcirm_type_t regtype =
			    PCIRM_REQTYPE(regs[i].pci_phys_hi);
			if (regtype == rbl->type ||
			    (regtype == PCIRM_TYPE_PMEM &&
			    rbl->type == PCIRM_TYPE_MEM)) {
				old_base = REG_TO_BASE(regs[i]);
				if (old_base == rbl->base) {
					BASE_TO_REG(rbl->new_base, regs[i]);
					SIZE_TO_REG(rbl->new_len, regs[i]);
					(void) ndi_prop_update_int_array(
					    DDI_DEV_T_NONE, dip,
					    "assigned-addresses",
					    (int *)regs, rlen / sizeof (int));

					break;
				}
			}
		}

		if (i >= count) {
			/* no matched assigned-addresses entry found */
			if (rbl->len) {
				/*
				 * This is not a new range entry to add,
				 * something wrong.
				 */
				kmem_free(regs, rlen);
				DEBUGPRT(CE_WARN, "Failed to match an old"
				    " assigned-addresses entry. "
				    "dip=%p, count=%d\n", (void *) dip, count);

				return (PCIRM_FAILURE);
			}
			/* append a new range entry */
			new_rlen = rlen + sizeof (pci_regspec_t);
			new_regs = kmem_alloc(new_rlen, KM_SLEEP);
			bcopy(regs, new_regs, rlen);
			new_regs[count].pci_phys_hi =
			    pcirm_type2prop(rbl->type, rbl->new_base);
			new_regs[count].pci_phys_hi |=	PCI_REG_REL_M;
			BASE_TO_REG(rbl->new_base, new_regs[count]);
			SIZE_TO_REG(rbl->new_len, new_regs[count]);
			(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
			    "assigned-addresses", (int *)new_regs, new_rlen /
			    sizeof (int));
			kmem_free(new_regs, new_rlen);
		}
		kmem_free(regs, rlen);
	}

	return (PCIRM_SUCCESS);
}

/*
 * Remove all "available" property entries of dip for a specific
 * resource type.
 */
static int
pcirm_prop_remove_avail(dev_info_t *dip, pcirm_type_t type)
{
	pci_regspec_t *regs, *newregs;
	int rlen, rcount;
	int i, j;

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS |
	    DDI_PROP_NOTPROM, "available", (caddr_t)&regs,
	    &rlen) != DDI_SUCCESS) {
		return (PCIRM_FAILURE);
	}
	rcount = rlen / sizeof (pci_regspec_t);

	newregs = kmem_alloc(rlen, KM_SLEEP);
#if 0
	pcirm_debug_dump_regs(regs, rcount);
#endif
	for (i = 0, j = 0; i < rcount; i++) {
		if (PCIRM_REQTYPE(regs[i].pci_phys_hi) == type) {
			/* Remove entries of the matched type */
			continue;
		}

		newregs[j] = regs[i];
		j++;
	}

	/* update the property */
	(void) ndi_prop_remove(DDI_DEV_T_NONE, dip, "available");

	if (j != 0) {
		(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		    "available", (int *)newregs,
		    (j * sizeof (pci_regspec_t))/sizeof (int));
	}
	kmem_free((caddr_t)newregs, rlen);
	kmem_free((caddr_t)regs, rlen);

	return (PCIRM_SUCCESS);
}

typedef struct pcirm_avail_arg {
	pcirm_type_t	type;
	dev_info_t	*rcdip;
} pcirm_avail_arg_t;

static int
pcirm_do_update_avail_prop(dev_info_t *dip, void *arg)
{
	pcirm_range_t *rangeset;
	pci_regspec_t *regs;
	ppb_ranges_t *ranges;
	int rlen, count, i;
	uint64_t base, len;
	dev_info_t *cdip;
	pcirm_type_t type = ((pcirm_avail_arg_t *)arg)->type;
	dev_info_t *rcdip = ((pcirm_avail_arg_t *)arg)->rcdip;

	/* Only bridges have "available" property */
	if (!pcie_is_pci_bridge(dip))
		return (DDI_WALK_PRUNECHILD);

	/* Remove "available" prop of type */
	(void) pcirm_prop_remove_avail(dip, type);

	/*
	 * Put all resources from "ranges" to "available"
	 */
	if (pcirm_is_pci_root(dip)) {
		/*
		 * Get root complex's ranges and add them into the
		 * "available" property.
		 */
		rangeset = DIP2RCHDL(rcdip)->rc_rangeset[type];
		for (; rangeset != NULL; rangeset = rangeset->next) {
			(void) pcirm_prop_add_addr(dip, "available",
			    rangeset->base, rangeset->len, type);
		}

	} else {
		if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS |
		    DDI_PROP_NOTPROM, "ranges", (caddr_t)&ranges,
		    &rlen) != DDI_SUCCESS) {

			return (DDI_WALK_CONTINUE);
		}
		count = rlen / sizeof (ppb_ranges_t);
		for (i = 0; i < count; i++) {
			/* Match the given type with the range entry */
			if (PCIRM_RESOURCETYPE(ranges[i].child_high) == type) {
				base = RANGES_TO_BASE(ranges[i]);
				len = RANGES_TO_SIZE(ranges[i]);

				(void) pcirm_prop_add_addr(dip, "available",
				    base, len, type);
			}
		}
		kmem_free(ranges, rlen);
	}

	/*
	 * Walk through all first-level children and remove their used
	 * resources from "available" prop.
	 */
	cdip = ddi_get_child(dip);
	for (; cdip != NULL; cdip = ddi_get_next_sibling(cdip)) {
		/* "assigned-addresses" property */
		if (ddi_getlongprop(DDI_DEV_T_ANY, cdip, DDI_PROP_DONTPASS,
		    "assigned-addresses", (caddr_t)&regs, &rlen) !=
		    DDI_SUCCESS) {
			goto check_ranges;
		}

		count = rlen / sizeof (pci_regspec_t);
		for (i = 0; i < count; i++) {
			/* Match the given type with the range entry */
			if (pcirm_match_reg_type(cdip, &regs[i], type)) {
				base = REG_TO_BASE(regs[i]);
				len = REG_TO_SIZE(regs[i]);

				(void) pcirm_prop_rm_addr(dip, "available",
				    base, len, type);
			}
		}
		kmem_free(regs, rlen);
check_ranges:
		/* "ranges" property */
		if (pcie_is_pci_bridge(cdip)) {
			if (pcirm_get_curr_bridge_range(cdip, type,
			    &base, &len) != PCIRM_SUCCESS) {
				continue;
			}

			(void) pcirm_prop_rm_addr(dip, "available",
			    base, len, type);
		}

	}

	return (DDI_WALK_CONTINUE);
}

void
pcirm_update_avail_props(dev_info_t *head, pcirm_type_t type)
{
	pcie_bus_t *bus_p = PCIE_DIP2BUS(head);
	pcirm_avail_arg_t arg;
	dev_info_t *dip;
	pcirm_rbl_map_t *rbl;
	int circular_count;

	for (; head != NULL; head = DIP2RBL(head)->next) {
		rbl = DIP2RBL(head)->pci_rbl_map;
		for (; rbl != NULL; rbl = rbl->next) {
			if (rbl->type == type)
				goto update;
		}
	}

	if (head == NULL)
		return;

update:
	dip = ddi_get_parent(head);

	arg.type = type;
	arg.rcdip = PCIE_GET_RC_DIP(bus_p);

	(void) pcirm_do_update_avail_prop(dip, &arg);

	ndi_devi_enter(dip, &circular_count);
	ddi_walk_devs(ddi_get_child(dip), pcirm_do_update_avail_prop,
	    &arg);
	ndi_devi_exit(dip, circular_count);
}

void
pcirm_debug_dump_rbl_map(dev_info_t *head)
{
	dev_info_t *dip = NULL;
	pcirm_rbl_map_t *rbl = NULL;

	DEBUGPRT(CE_CONT, "pcirm_debug_dump_rbl_map: "
	    "dumping rebalance map, head_dip=%p \n", (void *)head);

	for (dip = head; dip; dip = DIP2RBL(dip)->next) {
		rbl = DIP2RBL(dip)->pci_rbl_map;
		if (!rbl) {
			DEBUGPRT(CE_CONT, "pcirm_debug_dump_rbl_map: "
			    "dip=%p, no rbl setup! \n", (void *) dip);
		} else {
			for (; rbl; rbl = rbl->next) {
				DEBUGPRT(CE_CONT, "pcirm_debug_dump_rbl_map: "
				    "dip=%p, rbl->type=%x, bar_off=%x\n"
				    " base=%" PRIx64 ","
				    " len=%" PRIx64 "\n"
				    " new_base=%" PRIx64 "\n"
				    " new_len=%" PRIx64 "\n"
				    " next=%p\n",
				    (void *) rbl->dip, rbl->type, rbl->bar_off,
				    rbl->base,
				    rbl->len,
				    rbl->new_base,
				    rbl->new_len,
				    (void *)rbl->next);
			}

		}
	}
}

void
pcirm_debug_dump_reqs(pcirm_req_t *req)
{
	DEBUGPRT(CE_CONT, "dumping reqs: req head=%p \n", (void *)req);
	for (; req; req = req->next) {
		DEBUGPRT(CE_CONT,
		    "req->type=%x,boundbase=%" PRIx64 ",boundlen=%" PRIx64 ","
		    "addr=%" PRIx64 ",len=%" PRIx64 ",align_mask=%" PRIx64 ","
		    "flags=%x \n",
		    req->type, req->boundbase, req->boundlen, req->addr,
		    req->len, req->align_mask, req->flags);
	}
}

/* check if the dip is at pci device number dev_num */
static boolean_t
pcirm_match_device_num(dev_info_t *dip, uint_t dev_num)
{
	pci_regspec_t	*regspec;
	int		reglen, bdf;

	if (dev_num == PCIRM_ALL_DEVICES)
		return (B_TRUE);

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "reg", (int **)&regspec, (uint_t *)&reglen) != DDI_SUCCESS)
		return (B_FALSE);

	if (reglen < (sizeof (pci_regspec_t) / sizeof (int))) {
		ddi_prop_free(regspec);
		return (B_FALSE);
	}

	/* Get phys_hi from first element.  All have same bdf. */
	bdf = (regspec->pci_phys_hi & (PCI_REG_BDFR_M ^ PCI_REG_REG_M)) >> 8;

	ddi_prop_free(regspec);
	if (((bdf & 0xff) >> 3) == dev_num)
		return (B_TRUE);

	return (B_FALSE);
}

/*
 * In hotplug case, if the new device is plugged in one of the slots of the
 * bridge (in SHPC case, for example), then try available resources first.
 * TODO: this function is not completed but it is for hotplug rebalance only.
 *       It will be completed when developing hotplug rebalance feature.
 */
int
pcirm_try_available_resources(dev_info_t *dip, uint_t dev_num,
    int types)
{
	dev_info_t *cdip;

	cdip = ddi_get_child(dip);
	for (; cdip; cdip = ddi_get_next_sibling(cdip)) {
		if (!pcirm_match_device_num(cdip, dev_num)) {
			/* Not the new device we want to count */
			continue;
		}
		if (pcie_is_pci_bridge(cdip)) {
			/*
			 * Traverse the sub-tree under cdip
			 * (if cdip has any children)
			 */
			if (pcirm_calculate_tree_requests(cdip, types)
			    != PCIRM_SUCCESS)
				return (PCIRM_FAILURE);

			/* Calculate the resource req of this cdip */
			if (pcirm_calculate_bridge_requests(cdip, types)
			    != PCIRM_SUCCESS)
				return (PCIRM_FAILURE);
		}
	}
	/*
	 * TODO: for hotplug rebalance, try to find resouce from dip's
	 * available resources.
	 */

	return (PCIRM_SUCCESS);
}

/*
 * Recursive function.
 * Calculation the (size, alignment) for all the device nodes under dip (not
 * including dip).
 */
static int
pcirm_calculate_tree_requests(dev_info_t *dip, int types)
{
	if (!dip)
		return (PCIRM_FAILURE);

	dip = ddi_get_child(dip); /* Get the first child */
	for (; dip != NULL; dip = ddi_get_next_sibling(dip)) {

		if (DIP2RBL(dip)->pci_bdg_req) {
			/*
			 * The resource requirement of this new device is
			 * already calculated before. Don't need to calculate
			 * it again. All the children of this node are
			 * already calculated if this node is calculated.
			 */
			continue;
		}

		if (pcie_is_pci_bridge(dip)) {
			if (pcirm_calculate_tree_requests(dip, types)
			    != PCIRM_SUCCESS)
				return (PCIRM_FAILURE);
			if (pcirm_calculate_bridge_requests(dip, types)
			    != PCIRM_SUCCESS)
				return (PCIRM_FAILURE);
		}
	}

	return (PCIRM_SUCCESS);
}

static int
pcirm_calculate_bridge_requests(dev_info_t *dip, int types)
{
	if (types & PCIRM_TYPE_BUS_M) {
		if (pcirm_calculate_bridge_req(dip, PCIRM_TYPE_BUS)
		    != PCIRM_SUCCESS)
			return (PCIRM_FAILURE);
	}
	if (types & PCIRM_TYPE_IO_M) {
		if (pcirm_calculate_bridge_req(dip, PCIRM_TYPE_IO)
		    != PCIRM_SUCCESS)
			return (PCIRM_FAILURE);
	}
	if (types & PCIRM_TYPE_MEM_M) {
		if (pcirm_calculate_bridge_req(dip, PCIRM_TYPE_MEM)
		    != PCIRM_SUCCESS)
			return (PCIRM_FAILURE);
	}
	if (types & PCIRM_TYPE_PMEM_M) {
		if (pcirm_calculate_bridge_req(dip, PCIRM_TYPE_PMEM)
		    != PCIRM_SUCCESS)
			return (PCIRM_FAILURE);
	}
	return (PCIRM_SUCCESS);
}

/*
 * Sort the resources of dip's immediate children.
 * Return two sorted lists:
 *   1. head is for relocatable resources (bigger size first);
 *   2. fixed_head is for non-relocatable resources (sort with addresses).
 */
static void
pcirm_sort_immediate_children_reqs(dev_info_t *dip, pcirm_type_t type,
    pcirm_bridge_req_t **head, pcirm_bridge_req_t **fixed_head)
{
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);
	pcirm_bridge_req_t *list_br, *tmp;
	dev_info_t *cdip = ddi_get_child(dip);
	dev_info_t *rcdip = PCIE_GET_RC_DIP(bus_p);

	*head = *fixed_head = NULL;

	for (; cdip != NULL; cdip = ddi_get_next_sibling(cdip)) {
		list_br = pcirm_get_size_from_dip(cdip, type);
		/* Insert the sizes to a sorted list, larger first. */
		for (; list_br; ) {
			tmp = list_br->next;

			/*
			 * clear the next pointer before insert, so that the
			 * last node->next of the list will always be NULL.
			 */
			list_br->next = NULL;
			if (list_br->fixed_list) {
				/*
				 * Insert to the fixed address list,
				 * lower base first. This is for
				 * non-relocatable addresses.
				 */
				pcirm_list_insert_fixed(fixed_head, list_br);
			} else {
				/*
				 * insert to the relocatable list,
				 * larger size first.
				 */
				pcirm_list_insert_size(head, list_br);
			}
			list_br = tmp;
		}
	}

	/*
	 * For root complex node, we need to deal with the system reserved
	 * addresses by taking them as non-relocatable addresses.
	 * Firstly, get the multiple sections of ranges. Between them, there
	 * are system reserved addresses. Create a br for each of them and
	 * insert to fixed_head list.
	 */
	if (type != PCIRM_TYPE_BUS && pcirm_is_pci_root(dip)) {
		pcirm_range_t *ranges;

		ranges = pcirm_get_root_complex_ranges(dip, type);
		if (ranges == NULL) {
			/* This is not usual, but in case it happens. */
			DEBUGPRT(CE_WARN, "pcirm_sort_immediate_children_reqs: "
			    "dip=%p, RC range is NULL!\n", (void *)dip);
			return;
		}
		DIP2RCHDL(rcdip)->rc_rangeset[type] = ranges;
		DIP2RCHDL(rcdip)->rc_range[type].base = ranges->base;
		for (; ranges->next != NULL; ranges = ranges->next) {
			tmp = kmem_zalloc(sizeof (pcirm_bridge_req_t),
			    KM_SLEEP);
			tmp->dip = 0;
			tmp->type = type;
			PCIRM_CREATE_ADDR(tmp->fixed_list, NULL,
			    PCIRM_BAR_OFF_NONE,
			    ranges->base + ranges->len,
			    ranges->next->base - 1);
			pcirm_list_insert_fixed(fixed_head, tmp);
		}
		DIP2RCHDL(rcdip)->rc_range[type].len = ranges->base +
		    ranges->len - DIP2RCHDL(rcdip)->rc_range[type].base;
	}
}

/*
 * Merge all the brs in relo br list into one, and relo_list of each
 * br becomes an entry of the relo_list of the new br.
 */
static void
pcirm_merge_relo_bdgreq_list(dev_info_t *dip, pcirm_bridge_req_t **relo_br)
{
	pcirm_size_list_t *tail, *new;
	pcirm_bridge_req_t *curr, *head = *relo_br;
	pcirm_bridge_req_t *new_br;
	int bar_off, size;

	new_br = kmem_zalloc(sizeof (pcirm_bridge_req_t), KM_SLEEP);
	new_br->dip = dip;
	new_br->type = head->type;

	for (curr = head; curr; curr = head) {
		DEBUGPRT(CE_WARN, "pcirm_merge_relo_bdgreq_list: "
		    "head=%p, curr=%p\n", (void *)head, (void *)curr);

		LIST_REMOVE(head, head, curr);
		PCIRM_GET_BAR_OFF(curr, bar_off);
		size = pcirm_sum_aligned_size(curr->relo_list);

		PCIRM_CREATE_SIZE(new, curr->dip, bar_off,
		    size, curr->relo_list->align);
		LIST_GET_TAIL(new_br->relo_list, tail);
		LIST_APPEND(new_br->relo_list, tail, new);
		pcirm_free_bridge_req(curr);
	}

	*relo_br = new_br;
}

/*
 * For each br in relo br list, merge its relo_list into a single entry.
 */
static void
pcirm_merge_relo_bdgreq_in_list(pcirm_bridge_req_t *relo_br)
{
	pcirm_size_list_t *new, *tmp;
	pcirm_bridge_req_t *curr;
	uint64_t base, len, size;
	int bar_off;

	for (curr = relo_br; curr; curr = curr->next) {
		DEBUGPRT(CE_WARN, "pcirm_merge_relo_bdgreq_in_list: "
		    "relo_br=%p, curr=%p\n", (void *)relo_br, (void *)curr);

		PCIRM_GET_BAR_OFF(curr, bar_off);
		size = pcirm_sum_aligned_size(curr->relo_list);
		/* align bridge req size properly */
		if (pcie_is_pci_bridge(curr->dip) &&
		    curr->dip != curr->relo_list->dip) {
			bar_off = PCIRM_BAR_OFF_BRIDGE_RANGE;
			size = pcirm_cal_bridge_size(curr->type, size);
		}

		if (ddi_prop_exists(DDI_DEV_T_ANY, curr->dip,
		    DDI_PROP_DONTPASS, "hotplug-capable"))  {
			/*
			 * If there are hotplug slots underneath, make sure
			 * rebalance will not reduce the resources for this
			 * bridge.
			 */
			if (pcirm_get_curr_bridge_range(curr->dip, curr->type,
			    &base, &len) != PCIRM_SUCCESS) {
				goto create_entry;
			}

			if (len > size)
				size = len;
		}
create_entry:
		PCIRM_CREATE_SIZE(new, curr->dip, bar_off,
		    size, curr->relo_list->align);
		LIST_FREE(curr->relo_list, tmp);
		curr->relo_list = new;

	}
}

/*
 * Calculate the given type of resource requirement for a bridge dip.
 * It is called for bridge nodes only. When it is called, it assumes that all
 * the children bridges of dip are already calculated before.
 */
static int
pcirm_calculate_bridge_req(dev_info_t *dip, pcirm_type_t type)
{
	pcirm_bridge_req_t *relo_br, *fixed_br, *br_bus;
	uint64_t base, len;

	relo_br = fixed_br = NULL;

	/*
	 * Sort all the requests of children and get two list:
	 * - relo_br: reallocatable addresses, larger size first
	 * - fixed_br: non-reallocatable addresses, lower base first
	 */
	pcirm_sort_immediate_children_reqs(dip, type, &relo_br, &fixed_br);

	DEBUGPRT(CE_CONT, "pcirm_calculate_bridge_req: "
	    "dip=%p, type=%x, relo_br=%p, fixed_br=%p \n",
	    (void *)dip, type, (void *)relo_br, (void *)fixed_br);
#if defined(PCIRM_DEBUG)
	pcirm_debug_dump_bdgreq_list(relo_br,
	    "pcirm_calculate_bridge_req: relo_br");
	pcirm_debug_dump_bdgreq_list(fixed_br,
	    "pcirm_calculate_bridge_req: fixed_br");
#endif
	if (type == PCIRM_TYPE_BUS) {
		/*
		 * Create a br for this dip's bus number requirement, because
		 * the bridge itself also need a bus number.
		 */
		br_bus = kmem_zalloc(sizeof (pcirm_bridge_req_t), KM_SLEEP);
		br_bus->dip = dip;
		br_bus->type = PCIRM_TYPE_BUS;
		if (pcirm_is_fixed_device(dip, br_bus->type)) {
			/* Non-relocatable bus number */
			if (pcirm_get_curr_bridge_range(dip, type,
			    &base, &len) != PCIRM_SUCCESS) {
				DEBUGPRT(CE_WARN,
				    "pcirm_calculate_bridge_req: "
				    "failed to get current bus range for fixed"
				    " device. \n");
				kmem_free(br_bus, sizeof (pcirm_bridge_req_t));
				goto fail;
			}

			PCIRM_CREATE_ADDR(br_bus->fixed_list, dip,
			    PCIRM_BAR_OFF_BRIDGE_RANGE, base, base);
			LIST_INSERT_TO_HEAD(fixed_br,  br_bus);
		} else {
			/*
			 * Insert to head so the bus number assigned for
			 * the bridge would be smaller than its children.
			 */
			PCIRM_CREATE_SIZE(br_bus->relo_list, dip,
			    PCIRM_BAR_OFF_BRIDGE_RANGE, 1, 0);
			LIST_INSERT_TO_HEAD(relo_br, br_bus);

		}
	}
	if (!relo_br && !fixed_br) {
		/* This bridge has no resource requirement */
		DEBUGPRT(CE_CONT, "pcirm_calculate_bridge_req: "
		    "No resource requirement under this bridge, "
		    "dip=%p, type=%x \n", (void *)dip, type);

		if (pcirm_is_pci_root(dip))
			return (PCIRM_SUCCESS);

		if (pcirm_get_curr_bridge_range(dip, type, &base, &len)
		    != PCIRM_SUCCESS) {
			/* No resources assigned, just return. */
			return (PCIRM_SUCCESS);
		}

		/*
		 * The bridge has been assigned some resources, create
		 * a br so that it will show up in the resource map to get
		 * its resources recycled.
		 */
		relo_br = kmem_zalloc(sizeof (pcirm_bridge_req_t), KM_SLEEP);
		relo_br->dip = dip;
		relo_br->type = type;
		PCIRM_CREATE_SIZE(relo_br->relo_list, dip,
		    PCIRM_BAR_OFF_BRIDGE_RANGE, 0, 0);
	}

	pcirm_merge_relo_bdgreq_in_list(relo_br);

	if (!fixed_br) {
		/*
		 * No fixed addresses.
		 * merge the relo brs so that the bridge req will have
		 * a single br with a list of relo entries.
		 */
		pcirm_merge_relo_bdgreq_list(dip, &relo_br);
		relo_br->dip = dip;
		pcirm_set_bridge_req(relo_br);

		return (PCIRM_SUCCESS);
	}

	/*
	 * Have fixed addresses.
	 * Now the bridge has a list of brs which have fixed addresses
	 * and maybe also relocatable addresses.
	 */
	DEBUGPRT(CE_CONT, "pcirm_calculate_bridge_req: "
	    "There are fixed addresses, "
	    "lowest base=%" PRIx64 ", fixed_br=%p \n",
	    fixed_br->fixed_list->base, (void *)fixed_br);
	if (pcirm_cal_fixed_bdgreq(dip, relo_br, &fixed_br) != PCIRM_SUCCESS) {
		DEBUGPRT(CE_WARN, "pcirm_calculate_bridge_req: "
		    "failed to calculate the fixed address range.\n");
		goto fail;
	}
	/*
	 * Relocatable addresses are either transfered to fixed_br->fixed_list
	 * (filled into the holes between fixed addresses), or moved to
	 * fixed_br->relo_list.
	 * Update the bridge dip's size for the minimum range requirement.
	 */
	fixed_br->dip = dip;
	pcirm_set_bridge_req(fixed_br);
	/*
	 * The bridge br contains a single fixed req and (optionally)
	 * a list of relo reqs.
	 */
	return (PCIRM_SUCCESS);
fail:
	pcirm_free_bridge_req_list(relo_br);
	pcirm_free_bridge_req_list(fixed_br);

	return (PCIRM_FAILURE);
}

pcirm_bridge_req_t *
pcirm_find_fixed_with_relo(pcirm_bridge_req_t *fixed_head)
{
	pcirm_bridge_req_t *curr;

	if (!fixed_head)
		return (NULL);
	for (curr = fixed_head; curr; curr = curr->next) {
		if (curr->relo_list)
			return (curr);
	}
	return (NULL);
}

/*
 * Calculate a fixed address list to find out the fixed range of this bridge.
 * When done, the middle holes between the fixed addresses are also filled with
 * relocatable addresses if there are any.
 */
int
pcirm_cal_fixed_bdgreq(dev_info_t *dip, pcirm_bridge_req_t *relo_br,
    pcirm_bridge_req_t **fixed_br)
{
	pcirm_bridge_req_t *fixed_head = *fixed_br;
	pcirm_bridge_req_t *fixed_with_relo;

	/*
	 * Check for brs with both fixed requests and relo requests.
	 */
	fixed_with_relo = pcirm_find_fixed_with_relo(fixed_head);
	if (fixed_with_relo) {
		/*
		 * Transform brs with both fixed and relo resources
		 * to brs with fixed addresses.
		 */
		if (pcirm_merge_relo_in_fixed_brs(dip, &relo_br, &fixed_head) !=
		    PCIRM_SUCCESS) {

			*fixed_br = fixed_head;
#if defined(PCIRM_DEBUG)
			pcirm_debug_dump_bdgreq_list(fixed_head,
			    "pcirm_cal_fixed_bdgreq");
#endif
			DEBUGPRT(CE_WARN, "pcirm_cal_fixed_bdgreq: "
			    "Cannot merge relo_list: fail. \n");

			return (PCIRM_FAILURE);
		}
	}
	/*
	 * All brs with both fixed and relo resources have been
	 * transformated to brs with only fixed resources, there
	 * could be some holdes between these fixed brs, so fill
	 * the relo brs into these holes.
	 */
	pcirm_fill_relobrs_to_fixed_holes(&relo_br, fixed_head);
	/* Collapse the fixed br list into a single fixed br */
	pcirm_merge_fixed_bdgreq_list(dip, &fixed_head);
	if (relo_br) {
		/*
		 * If there are still relo brs which haven't been
		 * filled into the holes between fixed brs, merge
		 * them into a list of relo entries, and turn the
		 * list into the relo_list element of the fixed
		 * br created above.
		 */
		pcirm_merge_relo_bdgreq_list(dip, &relo_br);
		fixed_head->relo_list = relo_br->relo_list;
		kmem_free(relo_br, sizeof (pcirm_bridge_req_t));
	}

	*fixed_br = fixed_head;

	return (PCIRM_SUCCESS);
}

/*
 * Merge all the brs in fixed br list into one, and fixed_list of each
 * br becomes an entry of the fixed_list of the new br.
 */
static void
pcirm_merge_fixed_bdgreq_list(dev_info_t *dip, pcirm_bridge_req_t **fixed_br)
{
	pcirm_addr_list_t *tail, *tail_addr, *new;
	pcirm_bridge_req_t *curr, *head = *fixed_br;
	pcirm_bridge_req_t *new_br;
	int bar_off;

	new_br = kmem_zalloc(sizeof (pcirm_bridge_req_t), KM_SLEEP);
	new_br->dip = dip;
	new_br->type = head->type;

	for (curr = head; curr; curr = head) {
		DEBUGPRT(CE_WARN, "pcirm_merge_fixed_bdgreq_list: "
		    "head=%p, curr=%p\n", (void *)head, (void *)curr);

		LIST_REMOVE(head, head, curr);
		PCIRM_GET_BAR_OFF(curr, bar_off);
		if (curr->dip && pcie_is_pci_bridge(curr->dip) &&
		    curr->dip != dip)
			bar_off = PCIRM_BAR_OFF_BRIDGE_RANGE;

		LIST_GET_TAIL(curr->fixed_list, tail_addr);
		PCIRM_CREATE_ADDR(new, curr->dip, bar_off,
		    curr->fixed_list->base, tail_addr->end);

		LIST_GET_TAIL(new_br->fixed_list, tail);
		LIST_APPEND(new_br->fixed_list, tail, new);
		pcirm_free_bridge_req(curr);
	}

	*fixed_br = new_br;
}

/* Try to fill relo br to middle holes as much as possible */
static void
pcirm_fill_relobrs_to_fixed_holes(pcirm_bridge_req_t **relo_br,
    pcirm_bridge_req_t *fixed_br)
{
	pcirm_bridge_req_t *relo_head = *relo_br;
	pcirm_bridge_req_t *curr_fixed = fixed_br;
	pcirm_bridge_req_t *curr_relo, *prev_relo;
	pcirm_addr_list_t *tail;
	uint64_t base, end, base_tmp, end_tmp;

	if (!relo_head)
		return;
#if defined(PCIRM_DEBUG)
	pcirm_debug_dump_bdgreq_list(fixed_br,
	    "pcirm_fill_relobrs_to_fixed_holes: fixed_br");
	pcirm_debug_dump_bdgreq_list(relo_head,
	    "pcirm_fill_relobrs_to_fixed_holes: relo_head");
#endif

	while (curr_fixed && curr_fixed->next) {
		DEBUGPRT(CE_WARN, "pcirm_fill_relobrs_to_fixed_holes:"
		    "curr_fixed=%p, curr_fixed->relo_list=%p\n",
		    (void *)curr_fixed,  (void *)curr_fixed->relo_list);

		LIST_GET_TAIL(curr_fixed->fixed_list, tail);
		base = tail->end;
		base += 1;
		end = (curr_fixed->next)->fixed_list->base - 1;
		if (base == end) {
			/* Next hole */
			curr_fixed = curr_fixed->next;
			continue;
		}
		/* Fill the hole (base, end) with relos one by one */
		for (curr_relo = prev_relo = relo_head; curr_relo;
		    curr_relo = curr_relo->next) {
			DEBUGPRT(CE_WARN, "pcirm_fill_relobrs_to_fixed_holes:"
			    "curr_relo=%p, curr_relo->relo_list=%p\n",
			    (void *)curr_relo,  (void *)curr_relo->relo_list);

			base_tmp = base;
			PCIRM_MAKE_ALIGN(base_tmp, curr_relo->relo_list->align);
			end_tmp = pcirm_cal_aligned_end(
			    curr_relo->relo_list, base_tmp);
			if (end_tmp <= end) {
				/* This one can fit the hole */

				break;
			}
			prev_relo = curr_relo;
		}
		if (curr_relo == NULL) {
			/* Next hole */
			curr_fixed = curr_fixed->next;
			continue;
		}
		/*
		 * Remove curr_relo from relo_br list and put it into
		 * fixed_br list
		 */
		LIST_REMOVE(relo_head, prev_relo, curr_relo);
		pcirm_turn_relo_to_fixed_br(curr_relo, base_tmp);
		DEBUGPRT(CE_WARN, "pcirm_fill_relobrs_to_fixed_holes: "
		    "curr_relo=%p\n", (void *)curr_relo);
		/*
		 * Insert to fixed br list. A new hole might be created, so we
		 * don't move to next fixed br in this case.
		 */
		LIST_INSERT(curr_fixed, curr_relo);
	}
	*relo_br = relo_head;
#if defined(PCIRM_DEBUG)
	pcirm_debug_dump_bdgreq_list(fixed_br,
	    "pcirm_fill_relobrs_to_fixed_holes: fixed_br");
	pcirm_debug_dump_bdgreq_list(*relo_br,
	    "pcirm_fill_relobrs_to_fixed_holes: relo_br");
#endif
}

/*
 * Try to layout relo list to (base, end) as much as possible;
 * Layout at high addresses first.
 */
pcirm_addr_list_t *
pcirm_relo2fixed_at_high_addr(pcirm_type_t type, pcirm_size_list_t **relo_list,
    uint64_t base, uint64_t end)
{
	pcirm_size_list_t *curr, *tail, *prev, *relo_dup, *tmp1, *tmp2, *tmp3;
	pcirm_size_list_t *head = *relo_list;
	pcirm_addr_list_t *fixed = NULL;
	uint64_t base_tmp;

	/*
	 * Try to fill as many relos in relo_list as possible into range
	 * [base, end].
	 */
	for (curr = head; curr; curr = curr->next) {
		/* Try the list starting with curr */
		LIST_DUPLICATE(curr, relo_dup, pcirm_size_list_t, tmp1, tmp2,
		    tmp3);
		LIST_GET_TAIL(relo_dup, tail);
		for (; tail != NULL; ) {
			if (pcirm_get_aligned_base(relo_dup, end,
			    type, &base_tmp) == PCIRM_SUCCESS &&
			    base_tmp >= base) {
				/* Fit */
				break;
			}
			/* Remove tail and try again */
			LIST_FIND_PREV(relo_dup, tail, prev);
			LIST_REMOVE(relo_dup, prev, tail);
			kmem_free(tail, sizeof (pcirm_size_list_t));
			LIST_GET_TAIL(relo_dup, tail);
		}
		if (relo_dup) {
			/*
			 * The relos in relo_dup can be filled into the hole,
			 * that is, can be turned to fixed addresses.
			 */
			break;
		}
	}
	if (relo_dup == NULL) {
		/* None of the relos can be filled into the hole */
		return (NULL);
	}
	/* Turn this relo_dup list to a fixed list according to base_tmp */
	fixed = pcirm_turn_relo_to_fixed(relo_dup, base_tmp);


	DEBUGPRT(CE_CONT, "pcirm_relo2fixed_at_high_addr: "
	    "base=%" PRIx64 ", end=%" PRIx64 "\n", base, end);
	DEBUGPRT(CE_CONT, "pcirm_relo2fixed_at_high_addr: "
	    "base_tmp=%" PRIx64 "\n", base_tmp);
	pcirm_debug_dump_relo_list(head, "pcirm_relo2fixed_at_high_addr");
	pcirm_debug_dump_relo_list(relo_dup, "pcirm_relo2fixed_at_high_addr");

	/* Remove the relos which can be filled in from relo_list */
	LIST_FIND_PREV(head, curr, prev);
	for (tmp1 = relo_dup; tmp1; tmp1 = tmp1->next) {
		LIST_REMOVE(head, prev, curr);
		LIST_FREE(curr, tmp2);
		curr = prev ? (prev->next) : head;
	}
	/* Free relo_dup list */
	LIST_FREE(relo_dup, tmp2);

	*relo_list = head;

	return (fixed);
}

uint64_t
pcirm_cal_bridge_align(pcirm_type_t type, uint64_t align)
{
	if (type == PCIRM_TYPE_IO) {
		/* IO, make 4K alignment */
		if (align < PPB_ALIGNMENT_IO)
			align = PPB_ALIGNMENT_IO;
	} else if (type == PCIRM_TYPE_MEM || type == PCIRM_TYPE_PMEM) {
		/* If memory, make 1M alignment */
		if (align < PPB_ALIGNMENT_MEM)
			align = PPB_ALIGNMENT_MEM;
	}
	return (align);
}

uint64_t
pcirm_cal_bridge_size(pcirm_type_t type, uint64_t size)
{
	if (type == PCIRM_TYPE_IO) {
		PCIRM_MAKE_ALIGN(size, PPB_ALIGNMENT_IO);

	} else if (type == PCIRM_TYPE_MEM || type == PCIRM_TYPE_PMEM) {
		PCIRM_MAKE_ALIGN(size, PPB_ALIGNMENT_MEM);
	}

	return (size);
}

/*
 * Layout relo list to (base, end). Return FAILURE if (base, end) can not
 * contain all elements in relo list.
 * Layout at low address first.
 */
int
pcirm_relo2fixed_at_low_addr(pcirm_bridge_req_t *brg_req, pcirm_type_t type,
    pcirm_size_list_t *relo_list, uint64_t base, uint64_t end,
    pcirm_addr_list_t **fixed)
{
	uint64_t align, new_end;

	align = pcirm_cal_bridge_align(type, relo_list->align);
	PCIRM_MAKE_ALIGN(base, align);
	new_end = pcirm_cal_aligned_end(relo_list, base);
	if (new_end > end) {
		if (type == PCIRM_TYPE_BUS) {
			dev_info_t *pdip = brg_req->dip;

			if (pcirm_is_pci_root(pdip)) {
				if (pcirm_extend_rc_bus_range(pdip,
				    new_end) == PCIRM_SUCCESS) {
					/*
					 * For RC node, modify "bus-range"
					 * propoerty to extend bus number range.
					 */
					*fixed = pcirm_turn_relo_to_fixed(
					    relo_list, base);
					return (PCIRM_SUCCESS);
				}
			}
		}
		DEBUGPRT(CE_WARN, "pcirm_relo2fixed_at_low_addr:"
		    " the hole is too small"
		    " to take the relocatable addresses.");
		return (PCIRM_FAILURE);
	}
	*fixed = pcirm_turn_relo_to_fixed(relo_list, base);

	return (PCIRM_SUCCESS);
}

/*
 * Turn a relocatable address list to a fixed address list according to a base
 */
static pcirm_addr_list_t *
pcirm_turn_relo_to_fixed(pcirm_size_list_t *relo, uint64_t base)
{
	pcirm_addr_list_t *fixed = NULL, *addr, *tail;

	for (; relo; relo = relo->next) {
		PCIRM_MAKE_ALIGN(base, relo->align);
		addr = kmem_alloc(sizeof (pcirm_addr_list_t), KM_SLEEP);
		addr->bar_off = relo->bar_off;
		addr->dip = relo->dip;
		addr->base = base;
		addr->end = base + relo->size - 1;
		LIST_GET_TAIL(fixed, tail);
		LIST_APPEND(fixed, tail, addr);
		base = addr->end + 1;
	}
	return (fixed);
}

/* Start from a "base" address, transfer a relo br to a fixed br */
static void
pcirm_turn_relo_to_fixed_br(pcirm_bridge_req_t *relo, uint64_t base)
{
	pcirm_size_list_t *r, *tmp;
	pcirm_addr_list_t *tail, *f;

	ASSERT(relo->fixed_list == NULL);
	for (r = relo->relo_list, tail = NULL; r; r = r->next) {
		DEBUGPRT(CE_WARN, "pcirm_fill_relobrs_to_fixed_holes: "
		    "r=%p, tail=%p\n", (void *)r, (void *)tail);
		f = kmem_alloc(sizeof (pcirm_addr_list_t), KM_SLEEP);
		PCIRM_MAKE_ALIGN(base, r->align);
		f->base = base;
		base += r->size;
		f->end = base - 1;
		f->dip = r->dip;
		f->bar_off = r->bar_off;
		LIST_GET_TAIL(relo->fixed_list, tail);
		LIST_APPEND(relo->fixed_list, tail, f);
	}
	LIST_FREE(relo->relo_list, tmp);
}

/*
 * Find the hole at the left of "curr", and then try to fill curr->relo_list
 * to the hole. Update the curr->relo_list and curr->fixed_list after filling.
 * Return SUCCESS if all elements in relo_list are filled into the hole.
 * Otherwise, find the hole at the right of "curr", and then try to fill the
 * rest of curr->relo_list to the hole.
 *
 * Return FAILURE if the holes are too small to take all relo_list.
 */
int
pcirm_grow_fixed_with_relo_list(pcirm_bridge_req_t *head,
    pcirm_bridge_req_t *curr, uint64_t rbase)
{
	pcirm_bridge_req_t *prev, *next;
	pcirm_addr_list_t *fixed, *tail;
	pcirm_size_list_t *size_tmp;
	uint64_t base, end, new_end;

	if (!curr)
		return (PCIRM_FAILURE);

	/* Calculate the hole at the left of the current br */
	LIST_FIND_PREV(head, curr, prev);
	if (!prev) {
		/* curr is the head */
		ASSERT(curr->fixed_list->base >= rbase);
		if (curr->fixed_list->base == rbase)
			goto grow_right;

		base = rbase;
	} else {
		/* prev relo_list has been changed to fixed */
		ASSERT(prev->relo_list == NULL);
		LIST_GET_TAIL(prev->fixed_list, tail);
		ASSERT(tail->end + 1 <= curr->fixed_list->base);
		if (tail->end + 1 == curr->fixed_list->base)
			goto grow_right; /* ajacent fixed addresses */

		base = tail->end + 1;
	}
	end = curr->fixed_list->base -1;
	DEBUGPRT(CE_WARN, "pcirm_grow_fixed_with_relo_list: "
	    "base=%" PRIx64 ", end=%" PRIx64 "\n", base, end);
	ASSERT(base <= end);

	/* Try to find the largest relos to grow to the left of fixed_list */
	fixed = pcirm_relo2fixed_at_high_addr(head->type, &curr->relo_list,
	    base, end);
	if (fixed) {
		LIST_GET_TAIL(fixed, tail);
		LIST_APPEND_LIST(fixed, tail, curr->fixed_list);
		curr->fixed_list = fixed;
	}

	if (curr->relo_list == NULL) {
		/* There are no relos left. */
		return (PCIRM_SUCCESS);
	}
grow_right:
	/*
	 * Grow the hole at right.
	 */
	next = curr->next;

	LIST_GET_TAIL(curr->fixed_list, tail);
	base = tail->end + 1;
	if (next) {
		/* Get next br's base as end of the hole */
		end = next->fixed_list->base - 1;
		PCIRM_MAKE_ALIGN(base, curr->relo_list->align);
		new_end = pcirm_cal_aligned_end(curr->relo_list, base);
		if (new_end > end) {
			DEBUGPRT(CE_WARN, "pcirm_grow_fixed_at_right:"
			    " the hole is too small to take all relo_list, "
			    "hole_size=%" PRIx64 "\n", base - end + 1);

			return (PCIRM_FAILURE);
		}
	}
	fixed = pcirm_turn_relo_to_fixed(curr->relo_list, base);

	LIST_GET_TAIL(curr->fixed_list, tail);
	LIST_APPEND_LIST(curr->fixed_list, tail, fixed);
	LIST_FREE(curr->relo_list, size_tmp);

	return (PCIRM_SUCCESS);
}

/*
 * Search the list of fixed_br, for each br which has relo_list, try to merge
 * the relo_list to the fixed_br list, that is, turn the relocatable addresses
 * to non-relocatable addresses.
 */
static int
pcirm_merge_relo_in_fixed_brs(dev_info_t *dip, pcirm_bridge_req_t **relo_br,
    pcirm_bridge_req_t **fixed_br)
{
	pcirm_bridge_req_t *relo_head, *head, *curr, *br;
	dev_info_t *pdip;
	uint64_t rbase, rlen, rv;
	pcirm_addr_list_t *addr_tmp, *tmp1, *tmp2, *tmp3;
	pcirm_size_list_t *size_tmp;
	boolean_t bdg_relo_bus;

	relo_head = *relo_br;
	head = *fixed_br;
	pdip = dip;
	/* Get the existing bridge range base */
	if (head->type != PCIRM_TYPE_BUS && pcirm_is_pci_root(pdip)) {
		/* Get root complex's range */
		rbase = DIP2RCHDL(pdip)->rc_range[head->type].base;
		DEBUGPRT(CE_WARN, "pcirm_merge_relo_in_fixed_brs: "
		    "Reach to root complex, rbase=%" PRIx64 "\n",
		    rbase);
	} else if (pcirm_get_curr_bridge_range(pdip, head->type, &rbase, &rlen)
	    != PCIRM_SUCCESS) {
		/*
		 * This is unexpected.
		 * - For boot rebalance, "ranges" should already be populated.
		 * - For hotplug rebalance case, newly plugged bridges should
		 *   not have non-relocatable addresses in the children.
		 */
		DEBUGPRT(CE_WARN, "pcirm_merge_relo_in_fixed_brs: "
		    "Failed to get current bridge range, pdip=%p\n",
		    (void *)pdip);
		return (PCIRM_FAILURE);
	}

	/*
	 * Special handling for bus numbers. The current bridge requires
	 * a relocatable bus number itself, and this bus number should be
	 * reserved so that it won't get used by its child bridges.
	 */
	bdg_relo_bus = (head->type == PCIRM_TYPE_BUS) &&
	    !pcirm_is_pci_root(pdip);
	if (bdg_relo_bus)
		rbase++;

	curr = pcirm_find_fixed_with_relo(head);
	for (; curr; curr = pcirm_find_fixed_with_relo(head)) {
		rv = pcirm_grow_fixed_with_relo_list(head, curr, rbase);
		if (rv == PCIRM_SUCCESS) {
			/* Update the corresponding br on the dip */
			PCIRM_FIND_BDGREQ_TYPE(curr->dip, curr->type, br);
			ASSERT(br);
			LIST_FREE(br->relo_list, size_tmp);
			LIST_FREE(br->fixed_list, addr_tmp);
			/*
			 * overwrite the original fixed_list in br
			 * with this fixed_list.
			 */
			LIST_DUPLICATE(curr->fixed_list, br->fixed_list,
			    pcirm_addr_list_t, tmp1, tmp2, tmp3);

			continue;
		} else {
			*fixed_br = head;

			return (PCIRM_FAILURE);
		}
	}

	if (bdg_relo_bus) {
		/*
		 * Transform the relo bus number for this bridge itself to
		 * a fix one and put it at the head of its children buses
		 */
		curr = relo_head;
		LIST_REMOVE(relo_head, relo_head, curr);
		ASSERT(curr->relo_list->next == NULL &&
		    curr->relo_list->size == 1);
		pcirm_turn_relo_to_fixed_br(curr, head->fixed_list->base - 1);
		LIST_INSERT_TO_HEAD(head, curr);
	}
	*relo_br = relo_head;
	*fixed_br = head;

	return (PCIRM_SUCCESS);
}

/*
 * Find resource for one request which is the total request for a given
 * resource type.
 */
int
pcirm_find_for_one_type(pcirm_bridge_req_t *br)
{
	int rval = PCIRM_FAILURE;
	pcie_bus_t *bus_p = PCIE_DIP2BUS(br->dip);
	dev_info_t *curr_dip;
	dev_info_t *rcdip = PCIE_GET_RC_DIP(bus_p);
	pcirm_bridge_req_t *brg_req;
	pcirm_type_t	type = br->type;

	/*
	 * Find resources from parent/grandparent to meet the resource need,
	 * which will invoke rebalance of existing resources in device tree.
	 * Note the process stops at root complex, as the rebalance functions
	 * within a root complex, it will not touch resources in other
	 * root complex.
	 */
	for (curr_dip = br->dip, brg_req = br; curr_dip != NULL; curr_dip =
	    pcirm_get_pci_parent(curr_dip)) {
		DIP2RCHDL(rcdip)->top_dip = pcie_find_common_parent(
		    DIP2RCHDL(rcdip)->top_dip, curr_dip);
		if (curr_dip != br->dip) {
			/*
			 * Here it already go up to parent/grandparent;
			 * Need to calculate their size/alignment requirement.
			 */
			if (pcirm_calculate_tree_req(curr_dip, type) !=
			    PCIRM_SUCCESS) {
				DEBUGPRT(CE_CONT, "pcirm_find_for_one_type: "
				    "failed to calculate req for tree dip=%p, "
				    "type=%x\n", (void*)curr_dip, type);
			}
			/*
			 * Calculate sizes & alignment for this dip, and
			 * append to list DIP2RBL(dip)->pci_bdg_req
			 */
			if (pcirm_calculate_bridge_req(curr_dip, type) !=
			    PCIRM_SUCCESS) {
				DEBUGPRT(CE_CONT, "pcirm_find_for_one_type: "
				    "failed to calculate req for dip=%p, "
				    "type=%x\n", (void*)curr_dip, type);
			}
			brg_req = pcirm_get_size_calculated(curr_dip, type);
			ASSERT(brg_req);
		}
		/*
		 * Get the resource requirement of each immediate
		 * child and mark it at the node. Sort the immediate
		 * children and calculate the total requirement after
		 * sorting. Return success if the total requirement
		 * after sorting can be satisfied by the existing range
		 * of curr_dip.
		 */
		rval = pcirm_try_curr_bridge(brg_req);
		if (rval == PCIRM_SUCCESS) {
			return (rval);
		}
	}
	return (rval);
}


/*
 * Try to fit into the bridge range
 */
static int
pcirm_try_curr_bridge(pcirm_bridge_req_t *br)
{
	int rv = PCIRM_SUCCESS;
	uint64_t range_base, range_len;
	pcie_bus_t *bus_p = PCIE_DIP2BUS(br->dip);
	dev_info_t *rcdip = PCIE_GET_RC_DIP(bus_p);

	if (br->type != PCIRM_TYPE_BUS && pcirm_is_pci_root(br->dip)) {
		/* Get root complex's range */
		range_base = DIP2RCHDL(rcdip)->rc_range[br->type].base;
		range_len = DIP2RCHDL(rcdip)->rc_range[br->type].len;
	} else {
		/*
		 * According to "ranges" or "bus-range" property, get the
		 * (base,len) of this bridge for a given type of resource.
		 */
		rv = pcirm_get_curr_bridge_range(br->dip, br->type, &range_base,
		    &range_len);
		if (rv != PCIRM_SUCCESS) {
			return (rv);
		}
	}
	/*
	 * Fit the dip's size/align into dip's range by sorting the relocatable
	 * addresses and the fixed address. The rebalance map will be deploied
	 * in the case that it is fited.
	 */
	rv = pcirm_fit_into_range(br, range_base, range_len);

	return (rv);
}

/*
 * For all the addresses under the bridge, try to calculate a sequenced addr
 * list which can be fit into the current range of this bridge.
 */
static int
pcirm_fit_into_range(pcirm_bridge_req_t *brg_req, uint64_t range_base,
    uint64_t range_len)
{
	pcirm_size_list_t *relo = NULL, *size_tmp, *r1, *r2, *r3;
	pcirm_addr_list_t *layout_list = NULL, *layout_list2 = NULL;
	pcirm_addr_list_t *fixed = NULL, *tail, *addr_tmp, *f1, *f2, *f3;
	pcirm_type_t type = brg_req->type;
	int rv;

	if (range_len == 0)
		return (PCIRM_FAILURE);

	if (brg_req->fixed_list == NULL) {
		/* There are only relocatable addresses */
		rv = pcirm_relo2fixed_at_low_addr(brg_req, type,
		    brg_req->relo_list, range_base, range_base +
		    range_len - 1, &layout_list);
		if (rv != PCIRM_SUCCESS)
			return (rv);

		goto done;
	}
	/* Deal with fixed addresses */
	LIST_GET_TAIL(brg_req->fixed_list, tail);
	if (brg_req->fixed_list->base < range_base)
		return (PCIRM_FAILURE);

	if (tail->end >= (range_base + range_len)) {
		if (brg_req->type == PCIRM_TYPE_BUS &&
		    pcirm_is_pci_root(brg_req->dip) &&
		    pcirm_extend_rc_bus_range(brg_req->dip, tail->end)
		    == PCIRM_SUCCESS) {
			/* continue processing */
			goto range_valid;
		} else {
			return (PCIRM_FAILURE);
		}
	}
range_valid:
	if (!brg_req->relo_list) {
		/* only have fixed_list */
		LIST_DUPLICATE(brg_req->fixed_list, layout_list,
		    pcirm_addr_list_t, f1, f2, f3);
		goto done;
	}

	LIST_DUPLICATE(brg_req->relo_list, relo, pcirm_size_list_t,
	    r1, r2, r3);
	LIST_DUPLICATE(brg_req->fixed_list, fixed, pcirm_addr_list_t,
	    f1, f2, f3);
	/*
	 * Fill into the hole of (range_base, fixed_base). Try to fill as much
	 * as possible.
	 */
	layout_list = pcirm_relo2fixed_at_high_addr(type, &relo, range_base,
	    fixed->base - 1);
	if (relo == NULL) {
		/* All relo are filled to the hole at left */
		ASSERT(layout_list);
		LIST_GET_TAIL(layout_list, tail);
		LIST_APPEND_LIST(layout_list, tail, fixed);

		goto done;
	}
	/* Fill into the hole of (fixed_end, range_end) */
	rv = pcirm_relo2fixed_at_low_addr(brg_req, type, relo, tail->end + 1,
	    range_base + range_len - 1, &layout_list2);
	if (rv != PCIRM_SUCCESS) {
		LIST_FREE(layout_list, addr_tmp);
		LIST_FREE(fixed, addr_tmp);
		LIST_FREE(relo, size_tmp);
		return (PCIRM_FAILURE);
	}
	LIST_GET_TAIL(layout_list, tail);
	LIST_APPEND_LIST(layout_list, tail, fixed);
	LIST_GET_TAIL(layout_list, tail);
	LIST_APPEND_LIST(layout_list, tail, layout_list2);
done:
	pcirm_mark_rebalance_map(brg_req->dip, type, layout_list);
	pcirm_mark_rebalance_map_for_subtree(brg_req->dip, type);

	LIST_FREE(layout_list, addr_tmp);
	LIST_FREE(relo, size_tmp);
	return (PCIRM_SUCCESS);
}

static void
pcirm_mark_rebalance_map_for_subtree(dev_info_t *dip, pcirm_type_t type)
{
	int circular_count = 0;

	ndi_devi_enter(dip, &circular_count);
	ddi_walk_devs(ddi_get_child(dip), pcirm_mark_rebalance_map_walk,
	    (void *)&type);
	ndi_devi_exit(dip, circular_count);
}

static int
pcirm_mark_rebalance_map_walk(dev_info_t *dip, void *arg)
{
	pcirm_type_t type;
	pcirm_bridge_req_t *br;

	type = *(pcirm_type_t *)arg;
	if (pcie_is_pci_bridge(dip)) {
		br = pcirm_get_size_calculated(dip, type);
		if (br) {
			pcirm_mark_rebalance_map(dip, type,
			    br->fixed_list);
		}
		return (DDI_WALK_CONTINUE);
	} else {
		/*
		 * Leaf devices have been marked by pcirm_mark_rebalance_map()
		 * when its parent node is traversed.
		 */
		return (DDI_WALK_PRUNECHILD);
	}
}

/*
 * Allocate a struct pcirm_rbl_map_t and mark it on the device for
 * moving a piece of resource.
 */
static void
pcirm_mark_rebalance_map(dev_info_t *dip, pcirm_type_t type,
    pcirm_addr_list_t *layout_list)
{
	pcirm_addr_list_t *curr;
	uint64_t old_base, old_len;

	if (!layout_list)
		return;

	for (curr = layout_list; curr; curr = curr->next) {
		if (curr->bar_off == PCIRM_BAR_OFF_NONE) {
			/* System reserved resources on root complex. */
			continue;
		} else if (curr->bar_off == PCIRM_BAR_OFF_BRIDGE_RANGE) {
			if (dip == curr->dip) {
				/*
				 * This entry records the secondary bus
				 * number of this bridge, thus shouldn't
				 * be added to rebalance map.
				 */
				/*
				 * or this entry is for the bridge which
				 * doesn't has any children and whose
				 * resources would be reclaimed.
				 */
				continue;
			}
			pcirm_update_bridge_req(curr, type);
			pcirm_mark_rebalance_map_for_resource(curr, type);
		} else {
			pcirm_mark_rebalance_map_for_resource(curr, type);
		}
	}

	if (type != PCIRM_TYPE_BUS)
		return;

	for (curr = layout_list; curr; curr = curr->next) {
		if (curr->bar_off == PCIRM_BAR_OFF_BRIDGE_RANGE) {
			if (dip == curr->dip)
				continue;

			(void) pcirm_get_curr_bridge_range(curr->dip,
			    type, &old_base, &old_len);

			if (curr->base != old_base) {
				/*
				 * Change of a bridge's second bus number
				 * impacts all its immediate children, so
				 * mark them in the rebalance map.
				 */
				pcirm_mark_insert_children_bus_rbl_map(
				    curr->dip, curr->base, old_base);
			}
		}
	}
}

/* Mark the rebalance map for BARs and also range base/limit of bridges */
static void
pcirm_mark_rebalance_map_for_resource(pcirm_addr_list_t *layout,
    pcirm_type_t type)
{
	uint64_t old_base, old_len;
	pcirm_rbl_map_t *rbl, *tail;
	pcie_bus_t *bus_p = PCIE_DIP2BUS(layout->dip);
	dev_info_t *rcdip = PCIE_GET_RC_DIP(bus_p);

	if (layout->bar_off >= 0) {
		/*
		 * Find the old base corresponding to this layout.
		 */
		(void) pcirm_rbl_match_addr(layout, &old_base,
		    &old_len);
	} else if (layout->bar_off == PCIRM_BAR_OFF_BRIDGE_RANGE) {
		/* rebalance bridge range (base,limit) */

		/*
		 * Get the existing range from "bus-range" or
		 * "ranges" property
		 */
		(void) pcirm_get_curr_bridge_range(layout->dip,
		    type, &old_base, &old_len);

	} else  {
		/* REG_NUM_IOV_VF */
		return;
	}

	if ((layout->end == old_base + old_len - 1) &&
	    (layout->base == old_base)) {
		/*
		 * Only mark rebalance map if old_base & old_len
		 * don't match new base and new len.
		 */
		return;
	}

	rbl = kmem_zalloc(sizeof (pcirm_rbl_map_t), KM_SLEEP);
	PCIRM_FILL_REBALANCE_MAP(rbl, layout, type, old_base, old_len);
	if (rbl->new_len == 0)
		rbl->new_base = 0;
	LIST_GET_TAIL((DIP2RBL(layout->dip)->pci_rbl_map), tail);
	LIST_APPEND((DIP2RBL(layout->dip)->pci_rbl_map), tail, rbl);

	pcirm_insert_rbl_node(DIP2RCHDL(rcdip), layout->dip);
}

static void
pcirm_mark_insert_children_bus_rbl_map(dev_info_t *dip,
    uint64_t base, uint64_t old_base)
{
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);
	dev_info_t *rcdip = PCIE_GET_RC_DIP(bus_p);
	pcirm_rbl_map_t *rbl, *tail;
	pcirm_addr_list_t layout;
	dev_info_t *cdip;

	cdip = ddi_get_child(dip);
	for (; cdip; cdip = ddi_get_next_sibling(cdip)) {
		layout.dip = cdip;
		layout.bar_off = PCIRM_BAR_OFF_BUS_NUM;
		layout.base = base;
		layout.end = base;
		layout.next = NULL;

		rbl = kmem_zalloc(sizeof (pcirm_rbl_map_t), KM_SLEEP);
		PCIRM_FILL_REBALANCE_MAP(rbl, &layout, PCIRM_TYPE_BUS,
		    old_base, 0);
		rbl->new_len = 0;
		LIST_GET_TAIL((DIP2RBL(layout.dip)->pci_rbl_map),
		    tail);
		LIST_APPEND((DIP2RBL(layout.dip)->pci_rbl_map),
		    tail, rbl);

		pcirm_insert_rbl_node(DIP2RCHDL(rcdip), layout.dip);
	}
}

/*
 * According to "addr", update the fixed_list of bridge.
 * This will convert elements in relo_list to fixed_list.
 */
static void
pcirm_update_bridge_req(pcirm_addr_list_t *addr, pcirm_type_t type)
{
	pcirm_size_list_t *relo = NULL, *tmp;
	dev_info_t *dip = addr->dip;
	pcirm_addr_list_t *tail, *new;
	pcirm_bridge_req_t *br;
	uint64_t base, end;

	PCIRM_FIND_BDGREQ_TYPE(dip, type, br);
	ASSERT(br != NULL);
	relo = br->relo_list;
	if (relo == NULL) {
		/* It is already a fixed address */
		return;
	}
	/* transformat entries in relo_list to fixed_list */
	base = addr->base;
	end = addr->end;
	for (; relo; relo = relo->next) {
		PCIRM_MAKE_ALIGN(base, relo->align);
		ASSERT((base + relo->size - 1) <= end);

		PCIRM_CREATE_ADDR(new, relo->dip, relo->bar_off,
		    base, base + relo->size - 1);
		LIST_GET_TAIL(br->fixed_list, tail);
		LIST_APPEND(br->fixed_list, tail, new);
		base += relo->size;
	}
	/* free relo_list */
	LIST_FREE(br->relo_list, tmp);
}

/*
 * If bus number, get (base,len) from "bus-range" property;
 * If IO/MEM, get from "ranges" property.
 */
static int
pcirm_get_curr_bridge_range(dev_info_t *dip, pcirm_type_t type,
    uint64_t *base, uint64_t *len)
{
	ppb_ranges_t *ranges = NULL;
	int buslen, ralen, count, i;
	pci_bus_range_t *pci_bus_range;
	int rval = PCIRM_FAILURE;

	DEBUGPRT(CE_CONT, "pcirm_get_curr_bridge_range: dip=%p, "
	    "type=%x\n", (void*)dip, type);
	*base = *len = 0;
	if (type == PCIRM_TYPE_BUS) {
		if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		    "bus-range", (caddr_t)&pci_bus_range, &buslen) !=
		    DDI_SUCCESS) {
			DEBUGPRT(CE_CONT, "pcirm_get_curr_bridge_range: "
			    "dip=%p, rv=%x, failed to get range.\n",
			    (void*)dip, rval);

			return (rval);
		}
		*base = (uint64_t)pci_bus_range->lo;
		*len = (uint64_t)(pci_bus_range->hi - pci_bus_range->lo + 1);
		kmem_free(pci_bus_range, buslen);

		return (PCIRM_SUCCESS);
	}
	/*
	 * For io/mem/pmem addresses
	 */
	/*
	 * Root complex node can have multiple non-continuous ranges,
	 * use pcirm_get_root_complex_ranges() in that case.
	 */
	ASSERT(!pcirm_is_pci_root(dip));

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS |
	    DDI_PROP_NOTPROM, "ranges", (caddr_t)&ranges,
	    &ralen) != DDI_SUCCESS) {
		DEBUGPRT(CE_CONT, "pcirm_get_curr_bridge_range: dip=%p, "
		    "rv=%x, failed to get range.\n", (void*)dip, rval);

		return (rval);
	}
	count = ralen / (sizeof (ppb_ranges_t));
	for (i = 0; i < count; i++) {
		if (PCIRM_RESOURCETYPE(ranges[i].child_high) == type) {
			*base = RANGES_TO_BASE(ranges[i]);
			*len = RANGES_TO_SIZE(ranges[i]);

#if defined(__i386) || defined(__amd64)
			/*
			 * Jump over the entries under 1M on x86.
			 */
			if ((type == PCIRM_TYPE_MEM) &&
			    (*base + *len - 1 < 0x100000)) {
				*base = *len = 0;
				continue;
			}
#endif /* __i386 || __amd64 */

			break;
		}
	}
	kmem_free(ranges, ralen);

	if (i < count) {
		return (PCIRM_SUCCESS);
	}
	return (PCIRM_FAILURE);
}

/*
 * Fix the "bus-range" property of root complex, if the bus range require-
 * ments exceeds the current RC bus range, and extending it would not impact
 * other RCs, since on x86 all RCs share a single 256 bus number space.
 */
static int
pcirm_extend_rc_bus_range(dev_info_t *dip, uint64_t new_end)
{
	pci_bus_range_t *bus_range;
	uint64_t old_base;
	int buslen;

	ASSERT(pcirm_is_pci_root(dip));

	if (new_end > 255)
		return (PCIRM_FAILURE);

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS, "bus-range",
	    (caddr_t)&bus_range, &buslen) != DDI_SUCCESS) {
		return (PCIRM_FAILURE);
	}
	old_base = (uint64_t)bus_range->lo;
	kmem_free(bus_range, buslen);

#if defined(__i386) || defined(__amd64)
	/*
	 * On x86 all RCs in the system share a single bus range
	 * 0~255, thus go over all RCs and check their bus ranges,
	 * to see if extending the bus range of current RC would
	 * cause any conflicts.
	 * On SPARC each RC has its own 256 bus numbers so there
	 * is no such issue.
	 */
	dev_info_t *cdip;
	uint64_t base;

	cdip = ddi_get_child(ddi_root_node());
	for (; cdip; cdip = ddi_get_next_sibling(cdip)) {
		if (dip == cdip)
			continue;

		if (ddi_getlongprop(DDI_DEV_T_ANY, cdip,
		    DDI_PROP_DONTPASS, "bus-range",
		    (caddr_t)&bus_range, &buslen) != DDI_SUCCESS) {

			continue;
		}
		base = (uint64_t)bus_range->lo;
		kmem_free(bus_range, buslen);

		if ((old_base < base) && (new_end >= base))
			return (PCIRM_FAILURE);
	}
#elif defined(__sparc)
	/*
	 * On sparc rely on "available-bus-range" property of root complex
	 * to determine the maximum bus number this RC can support, if it's
	 * not available, use the max bus number which all current PCIe
	 * based sparc systems could support.
	 *
	 * sun4v systems:
	 * N1: [2, 255]
	 * N2: [2, 63] with SDIO firmware, [2, 255] otherwise
	 * RF: [0, 255]
	 * sun4u systems:
	 * [0, 255]
	 */
	int maxbus = 255;

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "available-bus-range", (caddr_t)&bus_range, &buslen) !=
	    DDI_SUCCESS) {
		maxbus = 63;
	} else {
		maxbus = bus_range->hi;
	}

	if (new_end > maxbus)
		return (PCIRM_FAILURE);
#endif

	(void) pcirm_prop_modify_bus_range(dip, old_base,
	    new_end - old_base + 1);

	DEBUGPRT(CE_CONT, "pcirm_extend_rc_bus_range: dip=%p, "
	    "new bus_range=[%" PRIx64 ",%" PRIx64 "]\n",
	    (void*)dip, old_base, new_end);

	return (PCIRM_SUCCESS);
}


/*
 * Link a new dip to the rebalance map. Put it to the right hierarchical
 * place. The top dip is at the head of the rebalance map.
 * The rebalance map is linked by pointer DIP2RBL(dip)->next.
 */
static void
pcirm_insert_rbl_node(pcirm_handle_impl_t *hdim, dev_info_t *dip)
{
	dev_info_t **phead = &hdim->rbl_head;
	dev_info_t **ptail = &hdim->rbl_tail;

	dev_info_t *head = *phead;
	dev_info_t *prev;
#if 0
	DEBUGPRT(CE_CONT, "pcirm_insert_rbl_node: "
	    "dip=%p, rbl_head=%p \n",
	    (void *) dip, (void *)hdim->rbl_head);
#endif
	if (*phead == NULL) {
		/* The first dip in rebalance map */
		*phead = dip;
		*ptail = dip;
		DIP2RBL(dip)->next = NULL;
	} else {
		if (*ptail == dip) {
			/* have a quick check with tail */
			return;
		}
		/*
		 * Might be a new dip for the rebalance map, insert it
		 * to the right hierarchical place.
		 */
		prev = head;
		for (; head; head = DIP2RBL(head)->next) {
			if (head == dip) {
				/* already in the map */
				return;
			}
			if (pcirm_is_on_parent_path(head, dip)) {
				/*
				 * dip is a parent of head, insert it
				 * before head
				 */
				if (head == *phead) {
					/*
					 * insert to the head of the whole
					 * list
					 */
					DIP2RBL(dip)->next = head;
					*phead = dip;
				} else {
					/*
					 * insert before the current dip (head)
					 */
					DIP2RBL(dip)->next = head;
					DIP2RBL(prev)->next = dip;
				}

				return;
			}
			prev = head;
		}
		/* lowest dip, put it at the tail */
		DIP2RBL(dip)->next = NULL;
		DIP2RBL(prev)->next = dip;

		*ptail = dip;
	}
}

/*
 * Find the cooresponding assigned address for creating the struct
 * pcirm_rbl_map_t.
 * Return a matched base address.
 */
static int
pcirm_rbl_match_addr(pcirm_addr_list_t *addr, uint64_t *base,
    uint64_t *len)
{
	pci_regspec_t *assigned;
	int alen, acount, i;
	int offset = addr->bar_off;

	*base = *len = 0;

	if (ddi_getlongprop(DDI_DEV_T_ANY, addr->dip, DDI_PROP_DONTPASS,
	    "assigned-addresses", (caddr_t)&assigned, &alen) != DDI_SUCCESS) {
		return (PCIRM_FAILURE);
	}
	/*
	 * According to BAR #, find the old base address
	 */
	acount = alen / sizeof (pci_regspec_t);
	for (i = 0; i < acount; i++) {
		if (PCI_REG_REG_G(assigned[i].pci_phys_hi) == offset)
			break;
	}

	if (i == acount) {
		kmem_free(assigned, alen);
		return (PCIRM_FAILURE);
	}

	*base = REG_TO_BASE(assigned[i]);
	*len = REG_TO_SIZE(assigned[i]);
	kmem_free(assigned, alen);

	return (PCIRM_SUCCESS);
}

/*
 * Recursive function.
 * Calculation the (size, alignment) for all the device nodes under dip (not
 * including dip).
 */
static int
pcirm_calculate_tree_req(dev_info_t *dip, pcirm_type_t type)
{
	DEBUGPRT(CE_CONT, "pcirm_calculate_tree_req: "
	    "dip=%p, type=%x \n", (void *)dip, type);
	if (!dip) {
		return (PCIRM_FAILURE);
	}
	dip = ddi_get_child(dip); /* Get the first child */
	for (; dip != NULL; dip = ddi_get_next_sibling(dip)) {
		if (pcirm_size_calculated(dip, type)) {
			/*
			 * The size of this type of resource is already
			 * calculated before. Don't need to calculate it
			 * again. All the children of this node are
			 * already calculated if this node is calculated.
			 */
			continue;
		}

		if (pcie_is_pci_bridge(dip)) {
			if (pcirm_calculate_tree_req(dip, type)
			    != PCIRM_SUCCESS)
				return (PCIRM_FAILURE);
			if (pcirm_calculate_bridge_req(dip, type)
			    != PCIRM_SUCCESS)
				return (PCIRM_FAILURE);
		}
	}
	return (PCIRM_SUCCESS);
}

/* Check if the given type of resource already calculated for this dip */
static boolean_t
pcirm_size_calculated(dev_info_t *dip, pcirm_type_t type)
{
	pcirm_bridge_req_t *br;

	br = DIP2RBL(dip)->pci_bdg_req;
	for (; br; br = br->next) {
		if (br->type == type) {
			return (B_TRUE);
		}
	}

	return (B_FALSE);
}

/* Find the given type of resource already calculated for this dip */
static pcirm_bridge_req_t *
pcirm_get_size_calculated(dev_info_t *dip, pcirm_type_t type)
{
	pcirm_bridge_req_t *br;

	br = DIP2RBL(dip)->pci_bdg_req;
	for (; br; br = br->next) {
		if (br->type == type) {
			return (br);
		}
	}

	return (NULL);
}

/* update pcirm_bridge_req_t for a bridge node */
static void
pcirm_set_bridge_req(pcirm_bridge_req_t *br)
{
	pcirm_bridge_req_t *curr, *prev, *next, *tail;

	if (!br)
		return;

#if defined(PCIRM_DEBUG)
	pcirm_debug_dump_bdgreq_list(br,
	    "pcirm_set_bridge_req");
#endif
	prev = curr = DIP2RBL(br->dip)->pci_bdg_req;
	while (curr) {
		/* there is an existing calculation result, remove it */
		next = curr->next;
		if (curr->type == br->type) {
			LIST_REMOVE(DIP2RBL(br->dip)->pci_bdg_req,
			    prev, curr);
			pcirm_free_bridge_req(curr);
			curr = next;
			continue;
		}
		prev = curr;
		curr = next;
	}
	if (br->relo_list && br->relo_list->size != 0) {
		br->relo_list->align = pcirm_cal_bridge_align(br->type,
		    br->relo_list->align);
	}
	/*
	 * note the req size list pci_bdg_req for each bridge contains
	 * a single entry for each type of resource calculated, append to
	 * the list so that the entries in the list follows the order
	 * the reqs are calculated.
	 */
	LIST_GET_TAIL(DIP2RBL(br->dip)->pci_bdg_req, tail);
	LIST_APPEND(DIP2RBL(br->dip)->pci_bdg_req, tail, br);
}

static uint64_t
pcirm_sum_aligned_size(pcirm_size_list_t *head)
{
	uint64_t size = 0;

	/*
	 * Adds up all sizes with alignment consideration.
	 * Note this may leave gaps between different ranges. The ideal
	 * way is to calculate gaps as we proceed and try to fit new reqs
	 * into these gaps.
	 */
	for (; head != NULL; head = head->next) {
		PCIRM_MAKE_ALIGN(size, head->align);
		size += head->size;
	}
	return (size);
}

static uint64_t
pcirm_cal_aligned_end(pcirm_size_list_t *head, uint64_t base)
{
	/*
	 * Adds up all sizes with alignment consideration.
	 * Note this may leave gaps between different ranges. The ideal
	 * way is to calculate gaps as we proceed and try to fit new reqs
	 * into these gaps.
	 */
	for (; head != NULL; head = head->next) {
		PCIRM_MAKE_ALIGN(base, head->align);
		base += head->size;
	}
	return (base - 1);
}

static int
pcirm_get_aligned_base(pcirm_size_list_t *head, uint64_t end,
    pcirm_type_t type, uint64_t *pbase)
{
	uint64_t base, size, align, tmp_end;
	uint64_t bridge_align = pcirm_cal_bridge_align(type, 0);

	*pbase = 0;
	/*
	 * Adds up all sizes with alignment consideration.
	 * Note this may leave gaps between different ranges. The ideal
	 * way is to calculate gaps as we proceed and try to fit new reqs
	 * into these gaps.
	 */
	size = pcirm_sum_aligned_size(head);
	if (size > end + 1)
		return (PCIRM_FAILURE);

	base = end - size + 1;
	if (head->bar_off == PCIRM_BAR_OFF_BRIDGE_RANGE)
		align = pcirm_cal_bridge_align(type, head->align);
	else
		align = head->align;

	PCIRM_MAKE_ALIGN_TO_LEFT(base, align);

	tmp_end = pcirm_cal_aligned_end(head, base);
	while (tmp_end > end && base >= bridge_align) {
		base -= bridge_align;
		PCIRM_MAKE_ALIGN_TO_LEFT(base, align);

		tmp_end = pcirm_cal_aligned_end(head, base);
	}

	if (tmp_end <= end) {
		*pbase = base;
		return (PCIRM_SUCCESS);
	}

	return (PCIRM_FAILURE);
}
/*
 * Create/insert to the list for fixed addresses, lower base first.
 */
static void
pcirm_list_insert_fixed(pcirm_bridge_req_t **head, pcirm_bridge_req_t *node)
{
	pcirm_bridge_req_t *tmp, *prev = NULL;
	uint64_t node_base, tmp_base;

	if (*head == NULL) {
		/* The first element of the list */
		*head = node;
		return;
	}

	node_base = node->fixed_list->base;
	for (tmp = *head; tmp != NULL; prev = tmp, tmp = tmp->next) {
		tmp_base = tmp->fixed_list->base;
		if (node_base <= tmp_base) {
			if (tmp == *head) {
				LIST_INSERT_TO_HEAD((*head), node);
			} else {
				LIST_INSERT(prev, node)
			}
			return;
		}
	}
	/* the biggest base, insert to the tail */
	prev->next = node;
}

/* Create/insert a sorted list, larger first */
static void
pcirm_list_insert_size(pcirm_bridge_req_t **head, pcirm_bridge_req_t *node)
{
	pcirm_bridge_req_t *tmp, *tail, *left = NULL;
	uint64_t node_size, tmp_size, node_align, tmp_align;

	if (*head == NULL) {
		/* The first element of the list */
		*head = node;
		return;
	}

	if (node->type == PCIRM_TYPE_BUS) {
		/* For bus numbers, we don't sort them as size. */
		LIST_GET_TAIL(*head, tail);
		LIST_APPEND(*head, tail, node);
		return;
	}
	node_size = pcirm_sum_aligned_size(node->relo_list);
	node_align = node->relo_list->align;
	for (tmp = *head, left = tmp; tmp != NULL;
	    left = tmp, tmp = tmp->next) {
		tmp_align = tmp->relo_list->align;
		tmp_size = pcirm_sum_aligned_size(tmp->relo_list);
		if (node_align > tmp_align ||
		    (node_align == tmp_align && node_size >= tmp_size)) {
			/* First compare on alignment then size */
			if (tmp == *head) {
				LIST_INSERT_TO_HEAD((*head), node);
			} else {
				LIST_INSERT(left, node);
			}
			return;
		}
	}
	/* the smallest size, insert to the tail */
	LIST_APPEND(*head, left, node);
}

static pcirm_bridge_req_t *
pcirm_duplicate_bdgreq(pcirm_bridge_req_t *br)
{
	pcirm_bridge_req_t *new;
	pcirm_size_list_t *tmp1, *tmp2, *tmp3;
	pcirm_addr_list_t *atmp1, *atmp2, *atmp3;
	new = kmem_zalloc(sizeof (pcirm_bridge_req_t),
	    KM_SLEEP);
	bcopy(br, new, sizeof (pcirm_bridge_req_t));
	if (br->relo_list) {
		LIST_DUPLICATE(br->relo_list, new->relo_list,
		    pcirm_size_list_t, tmp1, tmp2, tmp3);
	}
	if (br->fixed_list) {
		LIST_DUPLICATE(br->fixed_list, new->fixed_list,
		    pcirm_addr_list_t, atmp1, atmp2, atmp3);
	}
	new->next = NULL;
	return (new);
}

static boolean_t
pcirm_is_amd_8132(dev_info_t *dip)
{
	uint32_t  devid, venid;

	devid = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "device-id", -1);
	venid = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "vendor-id", -1);

	if (devid == 0x7458 && venid == 0x1022)
		return (B_TRUE);

	return (B_FALSE);
}

static boolean_t
pcirm_is_lsi_2008(dev_info_t *dip)
{
	uint32_t  devid, venid;

	devid = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "device-id", -1);
	venid = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "vendor-id", -1);

	if (devid == 0x72 && venid == 0x1000)
		return (B_TRUE);

	return (B_FALSE);
}

boolean_t
pcirm_is_fixed_device(dev_info_t *dip, pcirm_type_t type)
{
	dev_info_t *cdip, *pdip;

	if (type == PCIRM_TYPE_BUS) {
		/*
		 * For bus numbers, check dip's children to decide if dip's bus
		 * number could be relocated or not.
		 */
		if (!pcie_is_pci_bridge(dip))
			return (B_FALSE);
		if (pcirm_is_amd_8132(dip)) {
			/* AMD 8132 PCI-X bridge */
			return (B_TRUE);
		}
		if (ddi_get_child(dip) &&
		    pcirm_is_lsi_2008(ddi_get_child(dip))) {
			/* LSI 2008 HBA */
			return (B_TRUE);
		}
		if (pcirm_is_pci_root(dip)) {
			/*
			 * If root complex, then take its base bus number as
			 * non-relocatable because its immediate leaf devices
			 * are relying on it and it can not be changed.
			 */
			return (B_TRUE);
		}
		for (cdip = ddi_get_child(dip); cdip;
		    cdip = ddi_get_next_sibling(cdip)) {
			/*
			 * Treat bus number as fixed for boot console
			 * and boot disk device.
			 */
			if (cdip == pcie_get_boot_usb_hc() ||
			    cdip == pcie_get_boot_disk()) {
				DEBUGPRT(CE_CONT, "pcirm_is_fixed_device: "
				    "fixed bus number, dip=%p \n",
				    (void *)cdip);
				return (B_TRUE);
			}
		}
		return (B_FALSE);
	} else {
		if (pcirm_is_display(dip))
			return (B_TRUE);

		/* For resource types other than bus number, check dip. */
		if (dip == pcie_get_boot_usb_hc()) {
			DEBUGPRT(CE_CONT, "pcirm_is_fixed_device: "
			    "fixed addresses, dip=%p \n", (void *)dip);
			return (B_TRUE);
		}
		pdip = ddi_get_parent(dip);
		ASSERT(pdip);
		if (pcirm_is_amd_8132(pdip)) {
			/* AMD 8132 PCI-X bridge */
			return (B_TRUE);
		}

	}
#if defined(__i386) || defined(__amd64)
	/*
	 * On x86, it will be safe if take all the immediate leaf deivces
	 * under root complex as fixed device.
	 */
	if (pcie_is_pci_bridge(dip)) {
		/* Not a pci leaf */
		return (B_FALSE);
	}
	pdip = ddi_get_parent(dip);
	ASSERT(pdip);
	if (pcirm_is_pci_root(pdip)) {
		return (B_TRUE);
	} else {
		return (B_FALSE);
	}
#else
	return (B_FALSE);
#endif /* __i386 || __amd64 */
}

/*
 * Get a list of a given type of resources needed by a PCI bridge node or
 * a leaf node.
 * If a bridge node, then also include the bridge's range needed to satisfy
 * its children's resource requirement.
 * All the BARs (either on a bridge node or a leaf node) are traversed by
 * reading the "reg" property.
 */
static pcirm_bridge_req_t *
pcirm_get_size_from_dip(dev_info_t *dip, pcirm_type_t type)
{
	pcirm_bridge_req_t *br = NULL;
	pcirm_bridge_req_t *reg_head = NULL, *head = NULL;
	pcirm_bridge_req_t *tail;

	if (pcie_is_pci_bridge(dip)) {
		/* Get resource list by reading bridge req */
		PCIRM_FIND_BDGREQ_TYPE(dip, type, br);
		if (br) {
			head = pcirm_duplicate_bdgreq(br);
		}
	} else if (PCIE_IS_IOV_PF(dip)) {
		/* Get resource req list by reading IOV Cap for PF */
		head = pcirm_get_vf_resource_req(dip, type);
	}
	if (type == PCIRM_TYPE_BUS) {
		/* Only bridge or IOV PF require bus numbers */
		return (head);
	}

	if (!pcie_is_pci_bridge(ddi_get_parent(dip))) {
		/*
		 * dip is a root complex or a non-pci device.
		 */
		return (head);
	}
	/*
	 * Get a resource list by reading "reg". Either pci-pci bridge
	 * or a pci leaf may have "reg" property.
	 */
	reg_head = pcirm_get_size_from_reg(dip, type);

	LIST_GET_TAIL(head, tail);
	LIST_APPEND_LIST(head, tail, reg_head);

#if defined(PCIRM_DEBUG)
	pcirm_debug_dump_bdgreq_list(head, "pcirm_get_size_from_dip");
#endif
	return (head);
}

static pcirm_bridge_req_t *
pcirm_get_size_from_reg_entry(dev_info_t *dip, pci_regspec_t *reg,
    pcirm_type_t type, boolean_t is_fixed)
{
	uint64_t base, size;
	pcirm_bridge_req_t *curr;

	base = REG_TO_BASE(*reg);
	size = REG_TO_SIZE(*reg);

	if (type == PCIRM_TYPE_MEM && (base + size <= 0x100000)) {
#if defined(__i386) || defined(__amd64)
		if (PCI_REG_REG_G(reg->pci_phys_hi) == 0) {
			/*
			 * For memory addresses within 1MB and not
			 * programmed on BAR, leave them aside when
			 * calculating resource requirements.
			 */
			return (NULL);
		}
#else
		if (strcmp(ddi_node_name(dip), "isa") == 0) {
			/* For memory addresses within 1MB on ISA node */
			return (NULL);
		}
#endif
	}
	DEBUGPRT(CE_CONT, "pcirm_get_size_from_reg_entry: "
	    "dip=%p, type=%x, is_fixed=%x, base=0x%" PRIx64 ","
	    "size=0x%" PRIx64 "\n",
	    (void *)dip, type, is_fixed, base, size);

	curr = kmem_zalloc(sizeof (pcirm_bridge_req_t), KM_SLEEP);
	curr->type = type;
	curr->dip = dip;
	/* relocatable bit */
	if (is_fixed) {
		/* Non-relocatable address */
		PCIRM_CREATE_ADDR(curr->fixed_list, dip,
		    PCI_REG_REG_G(reg->pci_phys_hi), base, base + size - 1);

	} else {
		/*
		 * For a BAR, the alignment requirement is the same
		 * as the size because the size is always power of 2
		 * according to PCI spec.
		 */
		PCIRM_CREATE_SIZE(curr->relo_list, dip,
		    PCI_REG_REG_G(reg->pci_phys_hi), size, size);
	}
	return (curr);
}

/*
 * Determine if an assigned-addresses entry is of particular resource
 * type. Deal with firmware/booter giving improper property settings:
 *
 *   Device with Prefetchable(P) BARs can be given Non-P addresses
 *   but is labeled in assigned-addresses with P bit regardless.
 *   Double check parent's P range.
 *
 *   RC node may have overlapping P and non-P ranges (bridges and
 *   RPs never do). If the request is for P BARs, allow it to
 *   succeed if it falls within parent's P ranges.
 */
static boolean_t
pcirm_match_reg_type(dev_info_t *dip, pci_regspec_t *assigned,
    pcirm_type_t type)
{
	pcirm_type_t regtype = PCIRM_REQTYPE(assigned->pci_phys_hi);

	if (regtype != PCIRM_TYPE_PMEM) /* IO or MEM requests */
		return (type == regtype);

	pcirm_range_t *range = NULL;
	if (pcirm_create_rangeset_from_ranges(ddi_get_parent(dip),
	    PCIRM_TYPE_PMEM, &range, B_TRUE) != PCIRM_SUCCESS)
		return (type == PCIRM_TYPE_MEM);

	regtype = pcirm_within_rangeset(REG_TO_BASE(*assigned),
	    REG_TO_SIZE(*assigned), range) ? PCIRM_TYPE_PMEM :
	    PCIRM_TYPE_MEM;

	pcirm_destroy_rangeset(range);
	return (type == regtype);
}

/*
 * Read "reg" property to get a list of resources needed by the device
 */
static pcirm_bridge_req_t *
pcirm_get_size_from_reg(dev_info_t *dip, pcirm_type_t type)
{
	pci_regspec_t *regs, *assigned;
	boolean_t is_fixed = B_FALSE;
	int rlen, rcount;
	int alen, acount;
	int i, j;
	pcirm_bridge_req_t *curr, *head = NULL, *tail = NULL;
	boolean_t no_assigned = B_FALSE;

	/* read "reg" property */
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "reg", (caddr_t)&regs, &rlen) != DDI_SUCCESS) {

		return (NULL);
	}
	rcount = rlen / sizeof (pci_regspec_t);

	/*
	 * On sparc, rely on "assigned-addresses" property to calculate
	 * a leaf device's resource requirements as it's found "reg"
	 * property has various issues on many machines.
	 */
	/* read "assigned-addresses" property */
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "assigned-addresses", (caddr_t)&assigned, &alen) != DDI_SUCCESS) {

		no_assigned = B_TRUE;
		goto check_regs;
	}
	acount = alen / sizeof (pci_regspec_t);

#if defined(__i386) || defined(__amd64)
	/*
	 * On x86, rely on "reg" property to calculate the resource
	 * requirements if this device doesn't require fixed resources.
	 */
	if (!pcirm_is_fixed_device(dip, type))
		goto check_regs;
#endif

	/*
	 * Use "assigned-addresses" as a basis to calculate device node
	 * resource requirements, but also cross check "reg" property
	 * for non-relocatable bit .etc, if avaiable.
	 */
	for (i = 0; i < acount; i++) {
		/* match with type */
		if (!pcirm_match_reg_type(dip, &assigned[i], type))
			continue;

		is_fixed = pcirm_is_fixed_device(dip, type);
		DEBUGPRT(CE_CONT, "pcirm_get_size_from_reg: "
		    "dip=%p, type=%x, is_fixed=%x \n",
		    (void *)dip, type, is_fixed);
		if (!is_fixed) {
			/*
			 * Check non-relocatable bit in "reg"
			 */
			for (j = 1; j < rcount; j++) {
				if (PCI_REG_REG_G(assigned[i].pci_phys_hi) ==
				    PCI_REG_REG_G(regs[j].pci_phys_hi)) {
					/*
					 * Found matched reg entry, check its
					 * non-relocatable bit
					 */
					is_fixed = regs[j].pci_phys_hi &
					    PCI_REG_REL_M;
					break;
				}
			}
		}

		curr = pcirm_get_size_from_reg_entry(dip,
		    &assigned[i], type, is_fixed);
		LIST_GET_TAIL(head, tail);
		LIST_APPEND(head, tail, curr);
	}

	kmem_free(assigned, alen);
	kmem_free(regs, rlen);
	return (head);

check_regs:

	for (j = 1; j < rcount; j++) {
		/* match with type */
		boolean_t sametype =
		    (PCIRM_REQTYPE(regs[j].pci_phys_hi) == type);
		if (no_assigned)
			goto add_reg;

		/*
		 * Cross check "assigned-addresses" to handle the case
		 * where prefetchable memory request is satisfied by
		 * non-prefetchable memory resource.
		 */
		for (i = 0; i < acount; i++) {
			if (PCI_REG_REG_G(assigned[i].pci_phys_hi) ==
			    PCI_REG_REG_G(regs[j].pci_phys_hi)) {
				break;
			}
		}
		if (i < acount) {
			sametype = pcirm_match_reg_type(dip,
			    &assigned[i], type);
		}
add_reg:
		if (!sametype)
			continue;

		is_fixed = regs[j].pci_phys_hi & PCI_REG_REL_M;
		curr = pcirm_get_size_from_reg_entry(dip,
		    &regs[j], type, is_fixed);
		LIST_GET_TAIL(head, tail);
		LIST_APPEND(head, tail, curr);
	}

	kmem_free(regs, rlen);
	return (head);
}

static pcirm_req_t *
pcirm_get_vf_res(dev_info_t *dip, pcirm_type_t type)
{
	int i, busnum = 0, num_vf;
	pci_regspec_t vf_bars[PCI_BASE_NUM];
	pci_regspec_t *phys_spec;
	pcirm_req_t *head = NULL, *tail = NULL;
	pcirm_req_t *req;

	ASSERT(PCIE_IS_IOV_PF(dip));
	if (type == PCIRM_TYPE_BUS) {
		if ((busnum = pcie_get_vf_busnum(dip)) == PCI_CAP_EINVAL16)
			return (NULL);

		req = kmem_zalloc(sizeof (pcirm_req_t), KM_SLEEP);
		req->type = PCIRM_TYPE_BUS;
		req->len = busnum;
		LIST_APPEND(head, tail, req);

	} else {
		if (pcie_get_vf_bars(dip, vf_bars, &num_vf, B_TRUE) !=
		    DDI_SUCCESS)
			return (NULL);

		for (i = 0; i < PCI_BASE_NUM; i++) {
			phys_spec = vf_bars + i;
			if (phys_spec->pci_size_low == 0)
				continue;

			if (PCIRM_REQTYPE(phys_spec->pci_phys_hi) != type)
				continue;

			req = kmem_zalloc(sizeof (pcirm_req_t), KM_SLEEP);
			req->type = type;
			req->len = phys_spec->pci_size_low;
			req->align_mask = req->len/num_vf - 1;

			LIST_APPEND(head, tail, req);

			PCIE_DBG("pcie_get_vf_resource_req: dip=%p, "
			    "req->type=%x,boundbase=%" PRIx64 ",boundlen=%"
			    PRIx64 "," "addr=%" PRIx64 ",len=%" PRIx64
			    ",align_mask=%" PRIx64 "," "flags=%x \n", dip,
			    req->type, req->boundbase, req->boundlen,
			    req->addr, req->len, req->align_mask,
			    req->flags);
		}
	}

	return (head);
}

static void
pcirm_vf_req_to_bdgreq(pcirm_req_t *req, dev_info_t *dip,
    pcirm_bridge_req_t *list_br)
{
	ASSERT(req != NULL);
	uint64_t align, len = req->len;

	bzero((caddr_t)list_br, sizeof (pcirm_bridge_req_t));
	list_br->dip = dip;
	list_br->type = req->type;

	if (list_br->type == PCIRM_TYPE_BUS)
		align = 0;
	else
		align = req->align_mask + 1;
	PCIRM_CREATE_SIZE(list_br->relo_list, dip, PCIRM_BAR_OFF_IOV_VF,
	    len, align);
}

/* Get virtual function's resource requirement */
static pcirm_bridge_req_t *
pcirm_get_vf_resource_req(dev_info_t *dip, pcirm_type_t type)
{
	pcirm_req_t	*req, *tmp;
	pcirm_bridge_req_t	*list_br, *head = NULL, *tail = NULL;

	req = pcirm_get_vf_res(dip, type);

	for (; req != NULL; req = tmp) {
		tmp = req->next;

		list_br = kmem_zalloc(sizeof (pcirm_bridge_req_t), KM_SLEEP);
		pcirm_vf_req_to_bdgreq(req, dip, list_br);
		LIST_GET_TAIL(head, tail);
		LIST_APPEND(head, tail, list_br);

		kmem_free(req, sizeof (*req));
	}

	return (head);
}
