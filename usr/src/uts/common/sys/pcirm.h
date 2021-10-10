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
 * File:   pcirm.h
 * All the interfaces exported by PCIRM
 */

#ifndef _SYS_PCIRM_H
#define	_SYS_PCIRM_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Data structures exported by pcirm
 */
#define	PCIRM_SUCCESS DDI_SUCCESS /* successful return */
#define	PCIRM_FAILURE DDI_FAILURE /* unsuccessful return */

/*
 * Device number
 */
#define	PCIRM_ALL_DEVICES	0xffffffff   /* All device numbers */

/*
 * Resource types
 */
typedef enum pcirm_type {	/* identical to space type by design */
	PCIRM_TYPE_UNKNOWN	= -1,
	PCIRM_TYPE_BUS		= 0u,
	PCIRM_TYPE_IO		= PCI_REG_ADDR_G(PCI_ADDR_IO),
	PCIRM_TYPE_MEM		= PCI_REG_ADDR_G(PCI_ADDR_MEM32),
	PCIRM_TYPE_PMEM		= PCI_REG_ADDR_G(PCI_ADDR_MEM64)
} pcirm_type_t;

/*
 * Resource type bitmask
 */
#define	PCIRM_TYPE_BUS_M	(1u << PCIRM_TYPE_BUS)
#define	PCIRM_TYPE_IO_M		(1u << PCIRM_TYPE_IO)
#define	PCIRM_TYPE_MEM_M	(1u << PCIRM_TYPE_MEM)
#define	PCIRM_TYPE_PMEM_M	(1u << PCIRM_TYPE_PMEM)

/*
 * Structure for specifying a resource request for a device (dip).
 */
typedef struct pcirm_req {
	pcirm_type_t	type;		/* IO/MEM/PMEM/Bus num */
	uint_t		flags;		/* General flags		*/
	uint64_t	len;		/* Requested allocation length	*/
	uint64_t	addr;		/* Specific base address requested */
	uint64_t	boundbase;	/* Base address of the area for	*/
					/* the allocated resource to be	*/
					/* restricted to.		*/
	uint64_t	boundlen;	/* Length of the area, starting	*/
					/* from boundbase, for the	*/
					/* allocated resource to be	*/
					/* restricted to.		*/
	uint64_t	align_mask;	/* Alignment mask used for	*/
					/* allocated base address	*/
	struct pcirm_req *next;
} pcirm_req_t;

/* pcirm_req flags bit definitions */

/*
 * Set the alignment of the allocated resource address according to
 * the len value (alignment mask will be (len - 1)). Value of len
 * has to be power of 2. If this flag is set, value of align_mask
 * will be ignored.
 */
#define	PCIRM_REQ_ALIGN_SIZE	0x0001

/*
 * Indicates that the resource should be restricted to the area
 * specified by boundbase and boundlen.
 */
#define	PCIRM_REQ_ALLOC_BOUNDED	0x0002

/*
 * Indicates that a specific address (addr value) is requested.
 */
#define	PCIRM_REQ_ALLOC_SPECIFIED 0x0004

/*
 * Indicates if requested size (len) chunk is not available then
 * allocate as big chunk as possible which is less than or equal
 * to len size.
 */
#define	PCIRM_REQ_ALLOC_PARTIAL_OK 0x0008

/*
 * Structure for specifying a piece of resource allocated.
 */
typedef struct pcirm_res {
	pcirm_type_t	type;	/* The type of resource */
	uint32_t	flags;	/* Flags */
	uint64_t	base;	/* The base of the resource */
	uint64_t	len;	/* The length of the resource */
	struct pcirm_res *next;
} pcirm_res_t;

/* pcirm_res_t flags bit definitions */

/*
 * Indicates only partial request is satisfied and allocated.
 * It is valid only when the corresponding request is a partial
 * ok request.
 */
#define	PCIRM_RES_PARTIAL_COMPLETED 0x0001

/*
 * Rebalance
 */

/* Opaque handle for a resource rebalance session */
typedef struct pcirm_handle *pcirm_handle_t;

/*
 * Specify the resources that will be moved for rebanlancing. If more than one
 * resource need to be moved, then a list of this structure will be created.
 * The list nodes are sorted with expected moving sequence. That is, the list
 * head points to the one which will be moved first.
 */
typedef struct pcirm_rbl_map {
	pcirm_type_t	type;		/* The type of resource to be moved */
	int		bar_off;	/* BAR offset */
	uint64_t	base;		/* The resource's base to be moved. */
	uint64_t	len;		/* The length to be moved. */
	uint64_t	new_base;	/* The target base after moving. */
	uint64_t	new_len;	/* The length after moving. */
	dev_info_t	*dip;		/* The node this resource belongs */
	struct pcirm_rbl_map	*next;	/* Next resource to be moved. */
} pcirm_rbl_map_t;

/*
 * Special bar_off values of pcirm_rbl_map_t
 */
#define	PCIRM_BAR_OFF_BRIDGE_RANGE	-1	/* bridge range */
#define	PCIRM_BAR_OFF_BUS_NUM		-2	/* device's bus number */

/*
 * Functions exported by pcirm
 */
/*
 * Allocate and free interfaces
 */
int pcirm_alloc_resource(dev_info_t *, dev_info_t *, pcirm_req_t *,
    pcirm_res_t *);
int pcirm_free_resource(dev_info_t *, dev_info_t *, pcirm_res_t *);
int pcirm_free_resource_all(dev_info_t *);

/*
 * Rebalance interfaces
 */
/* Calculate resource requests and try to find that required resources */
int pcirm_find_resource(dev_info_t *, uint_t, int, pcirm_handle_t *);
/* Walk the resource map */
int pcirm_walk_resource_map(pcirm_handle_t,
    int (*func)(dev_info_t *, pcirm_rbl_map_t *, void *), void *);
/* Update impacted properties for a re-balance session */
int pcirm_commit_resource(pcirm_handle_t);
/* End the re-balance/allocate session, cleanup. */
int pcirm_free_handle(pcirm_handle_t);


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PCIRM_H */
