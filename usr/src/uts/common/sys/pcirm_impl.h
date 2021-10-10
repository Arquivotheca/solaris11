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
 * File:   pcirm_impl.h
 * Internal data structures and utilites of pcirm module.
 */

#ifndef _SYS_PCIRM_IMPL_H
#define	_SYS_PCIRM_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/pcie_impl.h>

#if defined(DEBUG)
#define	PCIRM_DEBUG 1
#else
#define	PCIRM_DEBUG 0
#endif

#define	PCIRM_NO_MEM -2   	/* failed to allocate memory (by kmem_alloc) */
#define	PCIRM_BAD_HANDLE -3	/* bad handle passed to in function */
#define	PCIRM_BAD_REQ -4	/* bad resource request passed to function */
#define	PCIRM_NO_RESOURCE -5	/* no enough resource available for allocate */
#define	PCIRM_BAD_RANGE -6	/* range overlap */
#define	PCIRM_INVAL_ARG	-7	/* invalid arguments */

#define	PCIRM_RESOURCETYPE(phys_hi)    ((phys_hi) & PCI_REG_PF_M ? \
    PCIRM_TYPE_PMEM : (pcirm_type_t)PCI_REG_ADDR_G(phys_hi))
#define	PCIRM_REQTYPE(phys_hi)    ((PCIRM_RESOURCETYPE(phys_hi) == \
    PCIRM_TYPE_PMEM) ? ((phys_hi) & PCI_REG_PF_M ? PCIRM_TYPE_PMEM : \
    PCIRM_TYPE_MEM) : PCIRM_RESOURCETYPE(phys_hi))

/*
 * According to PCI-PCI bridge spec, for the base of the range on the bridge
 * node, IO range must be 4K alignment; Memory range must be 1M alignment.
 */
#define	PPB_ALIGNMENT_IO	0x1000
#define	PPB_ALIGNMENT_MEM	0x100000

#define	PCIRM_1M_LIMIT		0xFFFFFUL
#define	PCIRM_4GIG_LIMIT	-1UL
#define	PCIRM_64BIT_LIMIT	-1ULL
/*
 * Insert el to head of the list. Update head after insertion.
 * struct list *head;
 * struct list *el;
 */
#define	LIST_INSERT_TO_HEAD(head, el) \
	if (!(head)) { \
		(el)->next = NULL; \
		head = el; \
	} else { \
		(el)->next = head; \
		head = el; \
	}

/*
 * Insert el to the place after a non-NULL node in list.
 * struct list *prev;
 * struct list *el;
 */
#define	LIST_INSERT(prev, el) \
	(el)->next = prev->next; \
	prev->next = el;

/*
 * Remove el which is at the place after a non-NULL node.
 * struct list *prev;
 * struct list *el;
 */
#define	LIST_REMOVE(head, prev, el)	\
	if (head == el) {		\
		head = head->next;	\
		el->next = NULL;	\
	} else {			\
		prev->next = el->next;	\
		el->next = NULL;	\
	}

/*
 * Get the tail of list.
 * struct list *head;
 * struct list *tail;
 */
#define	LIST_GET_TAIL(head, tail) \
	for (tail = head; tail && tail->next; tail = tail->next);
/*
 * Find the previous element of curr in list head.
 * struct list *head;
 * struct list *curr;
 * struct list *prev;
 */
#define	LIST_FIND_PREV(head, curr, prev)	\
	if (!head || head == curr)	\
		prev = NULL;	\
	else {	\
		for (prev = head; prev; prev = prev->next) {	\
			if (prev->next == curr)	\
				break;	\
		}	\
	}
/*
 * Add el to the tail of list *head.
 * struct list *head;
 * struct list *el;
 * struct list *tail;
 */
#define	LIST_APPEND(head, tail, el) \
	if (el) { \
		(el)->next = NULL; \
		if (!(head)) { \
			head = el; \
			tail = head; \
		} else { \
			tail->next = el; \
			tail = el; \
		}	\
	}

/*
 * Add a list lst to the tail of list *head.
 * struct list *head;
 * struct list *lst;
 * struct list *tail;
 */
#define	LIST_APPEND_LIST(head, tail, lst)	\
	if (lst) { 					\
		if (!(head)) { 				\
			head = lst; 			\
			tail = head; 			\
		} else { 				\
			tail->next = lst; 		\
		}					\
		LIST_GET_TAIL(lst, tail); 	\
	}

/*
 * Free the whole list and set head to NULL.
 * struct list *head;
 * struct list *tmp;
 */
#define	LIST_FREE(head, tmp) \
	for (; (head) != NULL; ) {    \
		tmp = (head)->next;   \
		kmem_free(head, sizeof (*(head)));	\
		head = tmp; \
	}

/*
 * Duplicate the whole source list to target list.
 * struct list *source;
 * struct list *target;
 * type: struct type.
 */
#define	LIST_DUPLICATE(source, target, type, curr, tmp, tail)     \
	curr = source;          \
	target = tail = NULL;  \
	for (; (curr) != NULL; curr = (curr)->next) {     \
		tmp = (type *)kmem_alloc(sizeof (type), KM_SLEEP); \
		bcopy(curr, tmp, sizeof (type));	\
		LIST_APPEND(target, tail, tmp);             \
	}
/*
 * Get the base address from "reg" or "assigned-addresses" or "available"
 * properties which can be represented by structure pci_regspec_t.
 */
#define	REG_TO_BASE(reg) \
	    (((uint64_t)((reg).pci_phys_mid) << 32) | \
	    ((uint64_t)((reg).pci_phys_low)))
/*
 * Get the size from "reg" or "assigned-addresses" or "available"
 * properties.
 */
#define	REG_TO_SIZE(reg) \
	    (((uint64_t)((reg).pci_size_hi) << 32) | \
	    ((uint64_t)((reg).pci_size_low)))

#define	BASE_TO_REG(base, reg) \
	    (reg).pci_phys_mid = (uint32_t)((base) >> 32); \
	    (reg).pci_phys_low = (uint32_t)(base);

#define	SIZE_TO_REG(size, reg) \
		(reg).pci_size_hi = (uint32_t)((size) >> 32); \
		(reg).pci_size_low = (uint32_t)(size);

/*
 * Get the base address from "ranges" property. This can be used for either
 * PCI-PCI ranges or root complex ranges.
 */
#define	RANGES_TO_BASE(reg) \
	    (((uint64_t)((reg).child_mid) << 32) | \
	    ((uint64_t)((reg).child_low)))

/* Get the base address from PCI-ISA "ranges" property */
#define	ISA_RANGES_TO_BASE(reg) \
	    (((uint64_t)((reg).parent_mid) << 32) | \
	    ((uint64_t)((reg).parent_low)))

/*
 * Get the size from "ranges" property. This can be used for either
 * PCI-PCI ranges or root complex ranges.
 */
#define	RANGES_TO_SIZE(reg) \
	    (((uint64_t)((reg).size_high) << 32) | \
	    ((uint64_t)((reg).size_low)))

/*
 * Modify the base value for "ranges" property. For pci-pci bridges,
 * the child base is mapped to the same parent base.
 * See memlist_to_range() in pci_boot.c
 */
#define	BASE_TO_RANGES(base, reg) \
	    (reg).child_mid = (uint32_t)((base) >> 32); \
	    (reg).child_low = (uint32_t)(base);	    \
	    (reg).parent_mid = (reg).child_mid; \
	    (reg).parent_low = (reg).child_low;

#define	SIZE_TO_RANGES(size, reg) \
	    (reg).size_high = (uint32_t)((size) >> 32); \
	    (reg).size_low = (uint32_t)(size);

/*
 * For bar_off element in struct pcirm_bridge_req. If struct pcirm_bridge_req
 * is for a bridge range, then bar_off is set to PCIRM_BAR_OFF_BRIDGE_RANGE.
 * Or, it is the x entry in "reg" property.
 */
/* resources required by SR-IOV virtual function (VF) */
#define	PCIRM_BAR_OFF_IOV_VF		(PCIRM_BAR_OFF_BUS_NUM - 1)
/* misc resources */
#define	PCIRM_BAR_OFF_NONE		(PCIRM_BAR_OFF_BUS_NUM - 2)

/* Make "base" align to "align" */
#define	PCIRM_MAKE_ALIGN(base, align)			 \
	if ((align) != 0 && ((base) % (align))) {	         \
		base = ((base) / (align) + 1) * (align);	 \
	}

/*
 * Make "base" align to "align";
 * The new base will be less or equal to current base.
 */
#define	PCIRM_MAKE_ALIGN_TO_LEFT(base, align)			 \
	if ((align) != 0 && ((base) % (align))) {	         \
		base = ((base) / (align)) * (align);	 \
	}

#define	PCIRM_HIADDR(n) ((uint32_t)(((uint64_t)(n) & \
	0xFFFFFFFF00000000ULL)>> 32))
#define	PCIRM_LOADDR(n) ((uint32_t)((uint64_t)(n) & 0x00000000FFFFFFFF))

/*
 * resource range
 */
typedef struct pcirm_range {
	uint64_t	base;
	uint64_t	len;
	struct pcirm_range *next;
} pcirm_range_t;

/*
 * rebalance
 */

/* Relocatable resources */
typedef struct pcirm_size_list {
	dev_info_t	*dip;		/* Device where this address exist */
	int		bar_off;    	/* BAR offset */
	uint64_t	size;		/* Size of this relocatable resource */
	uint64_t	align;		/* Alignment requirement */
	struct pcirm_size_list *next;	/* Next piece of relocatable resource */
} pcirm_size_list_t;

/* Non-relocatable resources */
typedef struct pcirm_addr_list {
	dev_info_t	*dip;		/* Device where this address exist */
	int		bar_off;    	/* BAR offset */
	uint64_t	base;
	uint64_t	end;
	struct pcirm_addr_list *next;	/* Next piece of fixed resource */
} pcirm_addr_list_t;

/*
 * The resource requirement on a bridge node:
 * 	1. Relocatable addresses
 * 	2. Non-relocatable addresses
 */
typedef struct pcirm_bridge_req {
	dev_info_t	*dip;	/* The current node with resource ranges */
	pcirm_type_t	type;	/* resource type */
	/*
	 * Sorted list of relocatable resources; larger sizes first.
	 * If there are non-relocatable addresses (fixed_list != NULL), the
	 * relocatable addresses must be put closely around the non-relocatable
	 * addresses because they together composes the bridge range.
	 */
	pcirm_size_list_t *relo_list;

	/* Sorted non-relocatable addresses (from fixed_base to fixed_end). */
	pcirm_addr_list_t *fixed_list;

	struct pcirm_bridge_req *next;
} pcirm_bridge_req_t;

/* rebalance structure for each dip */
typedef struct pcirm_rbl {
	/*
	 * List for size/alignment requirement of this node.
	 * If it is a bridge node, then the list includes the total resource
	 * requirement of all the children.
	 */
	pcirm_bridge_req_t *pci_bdg_req;
	pcirm_rbl_map_t *pci_rbl_map; /* rebalance map for this device node */
	dev_info_t *next; /* next device node in the rebalance map */
} pcirm_rbl_t;

/* Resource rebalance handle */
typedef struct pcirm_handle_impl {
	dev_info_t *rc_dip;   /* the root complex node this handle belongs */
	dev_info_t *top_dip;  /* the top node this rebalance session reaches */
	dev_info_t *ap_dip;   /* the attachment point device node */
	dev_info_t *rbl_head; /* The first node of rebalance map */
	dev_info_t *rbl_tail; /* The last node of rebalance map */
	int	types;		/* The resource type filter */
	pcirm_range_t rc_range[PCIRM_TYPE_PMEM + 1]; /* Root complex ranges */
	pcirm_range_t *rc_rangeset[PCIRM_TYPE_PMEM + 1]; /* RC range set */
} pcirm_handle_impl_t;

/* rebalance structure for root complex dip */
typedef	struct pcirm_rc_rbl {
	pcirm_handle_impl_t *pcirm_hdl;	/* PCIRM access handle */
	kmutex_t	pcirm_mutex;	/* protects pcirm_hdl */
	kcondvar_t	pcirm_cv;	/* protects pcirm_hdl */
} pcirm_rc_rbl_t;

#define	DIP2RBL(dip)	\
	(PCIE_DIP2BUS(dip) ? (PCIE_DIP2BUS(dip)->bus_rbl) : NULL)
#define	DIP2RCRBL(dip)	\
	(PCIE_DIP2BUS(dip) ? (PCIE_DIP2BUS(dip)->bus_rc_rbl) : NULL)

#define	PCIRM_FILL_REBALANCE_MAP(rbl, addr, rbltype, old_base, old_len)	\
	(rbl)->type = rbltype;	    \
	(rbl)->bar_off = (addr)->bar_off;    \
	(rbl)->base = old_base;		    \
	(rbl)->len = old_len;		    \
	(rbl)->new_base = (addr)->base;	    \
	(rbl)->new_len = (addr)->end - (addr)->base + 1;  \
	(rbl)->dip = (addr)->dip;	\
	(rbl)->next = NULL;

/* Fill the list of pcirm_size_list_t */
#define	PCIRM_CREATE_SIZE(relo_list, sdip, sbar_off, sz, salign)	\
	(relo_list) = kmem_zalloc(sizeof (pcirm_size_list_t), KM_SLEEP); \
	(relo_list)->dip = sdip;				\
	(relo_list)->bar_off = sbar_off;	\
	(relo_list)->size = sz;				\
	(relo_list)->align = salign;

/* Fill the list of pcirm_addr_list_t */
#define	PCIRM_CREATE_ADDR(fixed_list, fdip, fbar_off, fbase, fend)	\
	fixed_list = kmem_zalloc(sizeof (pcirm_addr_list_t), KM_SLEEP); \
	fixed_list->dip = fdip;				\
	fixed_list->bar_off = fbar_off;	\
	fixed_list->base = fbase;				\
	fixed_list->end = fend;

/* Check if a bridge req is for a BAR or a range */
#define	PCIRM_GET_BAR_OFF(br, offset)	\
	if (br->fixed_list) {					\
		offset = br->fixed_list->bar_off;		\
	} else if (br->relo_list) {				\
		offset = br->relo_list->bar_off;		\
	}

/*
 * Resource allocation functions
 */
int pcirm_get_resource(dev_info_t *, pcirm_req_t *, pcirm_res_t *);
boolean_t pcirm_within_range(dev_info_t *, uint64_t, uint64_t, pcirm_type_t);
boolean_t pcirm_is_subtractive_bridge(dev_info_t *);
int pcirm_prop_add_addr(dev_info_t *, char *, uint64_t, uint64_t, pcirm_type_t);
int pcirm_prop_rm_addr(dev_info_t *, char *, uint64_t, uint64_t, pcirm_type_t);
int pcirm_prop_rm_ranges(dev_info_t *, uint64_t, uint64_t, pcirm_type_t);
int pcirm_prop_add_assigned(dev_info_t *, uint64_t, uint64_t, pcirm_type_t);
int pcirm_prop_add_bus_range(dev_info_t *, uint64_t, uint64_t);
int pcirm_prop_rm_bus_range(dev_info_t *, uint64_t, uint64_t);
int pcirm_prop_add_ranges(dev_info_t *, uint64_t, uint64_t, pcirm_type_t);
int pcirm_prop_rm_ranges(dev_info_t *, uint64_t, uint64_t, pcirm_type_t);
int pcirm_prop_add_resource(dev_info_t *, pcirm_res_t *);
int pcirm_prop_remove_resource(dev_info_t *, pcirm_res_t *);
int pcirm_prop_merge_available(dev_info_t *, pcirm_type_t);

/*
 * Resource rebalance functions
 */
boolean_t pcirm_find_resource_impl(dev_info_t *, uint_t, int,
    pcirm_handle_impl_t **);
int pcirm_free_handle_impl(pcirm_handle_impl_t *);
int pcirm_rbl_update_property(dev_info_t *, pcirm_rbl_map_t *);
void pcirm_debug_dump_reqs(pcirm_req_t *);
void pcirm_debug_dump_rbl_map(dev_info_t *);
void pcirm_update_avail_props(dev_info_t *, pcirm_type_t);
void pcirm_update_bus_props(dev_info_t *);

/*
 * Misc routines
 */
pcirm_handle_impl_t *pcirm_access_handle(dev_info_t *);
void pcirm_release_handle(dev_info_t *);
dev_info_t *pcirm_get_pci_parent(dev_info_t *);
boolean_t pcirm_is_pci_root(dev_info_t *dip);


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PCIRM_IMPL_H */
