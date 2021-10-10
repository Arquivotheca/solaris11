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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_PCIEV_H
#define	_SYS_PCIEV_H

#ifdef	__cplusplus
extern "C" {
#endif

typedef	enum {
	FUNC_TYPE_REGULAR = 0,
	FUNC_TYPE_PF = 1,
	FUNC_TYPE_VF = 2
} pcie_fn_type_t;

/* Definitions for per-RC/RP domain list hash table */
typedef struct pcie_dom_hash {
	mod_hash_t *dh_hashp;
	krwlock_t dh_lock;
} pcie_dom_hash_t;

#define	PCIEV_DOM_HASHSZ	32

typedef struct pcie_domain_entry {
	dom_id_t domain_id;
	uint_t domain_prop;
} pcie_domain_entry_t;

/* Definition of domain properties */
#define	PCIE_DOM_PROP_FMA	0x00000001 /* Domain supports FMA channel */
#define	PCIE_DOM_PROP_FB	0x00000002 /* Domain fallback to nfma */

typedef struct pcie_domains {
	dom_id_t domain_id;
	uint_t cached_count;	/* Reference Count of cached dom id list */
	uint_t faulty_count;	/* Reference Count of faulty dom id list */
	struct pcie_domains *cached_next; /* Next on cached dom id list */
	struct pcie_domains *faulty_prev; /* Prev on faulty dom id list */
	struct pcie_domains *faulty_next; /* Next on faulty dom id list */
} pcie_domains_t;

typedef struct pcie_req_id_list {
	pcie_req_id_t		bdf;
	struct pcie_req_id_list	*next;
} pcie_req_id_list_t;

typedef struct pcie_child_domains {
	pcie_domains_t *ids;
	pcie_req_id_list_t *bdfs;
} pcie_child_domains_t;

typedef struct pcie_channel_cb {
	dev_info_t *dip;
	dom_id_t dom_id;
} pcie_channel_cb_t;

/*
 * IOV data structure:
 * This data strucutre is now statically allocated during bus_p
 * initializing time for both physical and virtual fabrics,
 * but most of the fields are only meaningful for physical fabric.
 * If not specially noted, all the notes below only apply for
 * physical fabric.
 */
typedef struct pcie_domain {
	/*
	 * Bridges:
	 * Cache the domain/channel id and bdfs of all it's children.
	 *
	 * Leaves:
	 * Cache just the domain/channel id of self.
	 * Bridges will contain 0 <= N <= NumChild
	 *
	 * Note:
	 * there is no lock to protect the access to
	 * pcie_domains_t data struture. Currently we don't see
	 * the need for lock. But we need to pay attention if there
	 * might be issues when hotplug is enabled.
	 *
	 * Special note:
	 * Virtual fabric uses this field to record its controlling
	 * domain id in the RC node.
	 */
	union {
		pcie_child_domains_t ids;
		pcie_domains_t id;
	} domain;

	/*
	 * Reference count of the domain type for this device and it's children.
	 * For leaf devices, fmadom + nfma + root = 1
	 * For bridges, the sum of the counts = number of LEAF children.
	 *
	 * All devices start with a count of 1 for either nfmadom or rootdom.
	 *
	 * Special note:
	 * Devices in virtual fabric are all deemed rootdom.
	 */
	uint_t		fmadom_count;	/* Error channel capable domain */
	uint_t		nfmadom_count;	/* Non-error channel domain */
	uint_t		rootdom_count;	/* Root domain */

	/* flag if the affected dev will cause guest domains to panic */
	boolean_t	nfma_panic;
	/* used for fallback to legacy behavior incase of channel errors */
	boolean_t	nfma_panic_backup;

	/* faulty domain info, valid for RC or RP only */
	boolean_t	faulty_all;	/* If all child domains are faulty */
	pcie_domains_t	*faulty_domains; /* Faulty domain list */
	uint_t		nfma_domid_cnt;	/* count of fallbacked domains */

	/* Domain list hash table, valid for RC or RP only */
	pcie_dom_hash_t *dom_hashp;
} pcie_domain_t;

#define	PCIE_ASSIGNED_TO_FMA_DOM(bus_p)	\
	(!PCIE_IS_BDG(bus_p) && PCIE_BUS2DOM(bus_p)->fmadom_count > 0)
#define	PCIE_ASSIGNED_TO_NFMA_DOM(bus_p)	\
	(!PCIE_IS_BDG(bus_p) && PCIE_BUS2DOM(bus_p)->nfmadom_count > 0)
#define	PCIE_ASSIGNED_TO_ROOT_DOM(bus_p)			\
	(PCIE_IS_BDG(bus_p) || PCIE_BUS2DOM(bus_p)->rootdom_count > 0)
#define	PCIE_BDG_HAS_CHILDREN_FMA_DOM(bus_p)			\
	(PCIE_IS_BDG(bus_p) && PCIE_BUS2DOM(bus_p)->fmadom_count > 0)
#define	PCIE_BDG_HAS_CHILDREN_NFMA_DOM(bus_p)			\
	(PCIE_IS_BDG(bus_p) && PCIE_BUS2DOM(bus_p)->nfmadom_count > 0)
#define	PCIE_BDG_HAS_CHILDREN_ROOT_DOM(bus_p)			\
	(PCIE_IS_BDG(bus_p) && PCIE_BUS2DOM(bus_p)->rootdom_count > 0)
#define	PCIE_IS_ASSIGNED(bus_p)	\
	(!PCIE_ASSIGNED_TO_ROOT_DOM(bus_p))
#define	PCIE_BDG_IS_UNASSIGNED(bus_p)	\
	(PCIE_IS_BDG(bus_p) &&		\
	(!PCIE_BDG_HAS_CHILDREN_NFMA_DOM(bus_p)) &&	\
	(!PCIE_BDG_HAS_CHILDREN_FMA_DOM(bus_p)))


#define	PCIE_IN_DOMAIN(bus_p, id) (pcie_in_domain((bus_p), (id)))

#define	PCIE_UNKNOWN_DOMAIN_ID	0

/* Following macros are only valid for devices on virtual fabric */
#define	PCIE_RC_DOMAIN_ID_SET(bus_p, new_id) \
	if (PCIE_IS_RC(bus_p)) \
		PCIE_BUS2DOM(bus_p)->domain.id.domain_id = (uint_t)(new_id)

#define	PCIE_RC_DOMAIN_ID_GET(bus_p) \
	((PCIE_IS_RC(bus_p)	\
	? PCIE_BUS2DOM(bus_p)->domain.id.domain_id : PCIE_UNKNOWN_DOMAIN_ID))

/* Below are for physical fabric */

/* Following macros are only valid for leaf devices */
#define	PCIE_DOMAIN_ID_GET(bus_p) \
	((uint_t)(PCIE_IS_ASSIGNED(bus_p)			\
	? PCIE_BUS2DOM(bus_p)->domain.id.domain_id : PCIE_UNKNOWN_DOMAIN_ID))
#define	PCIE_DOMAIN_ID_SET(bus_p, new_id) \
	if (!PCIE_IS_BDG(bus_p)) \
		PCIE_BUS2DOM(bus_p)->domain.id.domain_id = (uint_t)(new_id)
#define	PCIE_DOMAIN_ID_INCR_REF_COUNT(bus_p)	\
	if (!PCIE_IS_BDG(bus_p))	\
		PCIE_BUS2DOM(bus_p)->domain.id.cached_count = 1;
#define	PCIE_DOMAIN_ID_DECR_REF_COUNT(bus_p)	\
	if (!PCIE_IS_BDG(bus_p))	\
		PCIE_BUS2DOM(bus_p)->domain.id.cached_count = 0;
#define	PCIE_DOMAIN_ID_GET_REF_COUNT(bus_p)	\
	(PCIE_BUS2DOM(bus_p)->domain.id.cached_count)

/* Following macros are only valid for bridges */
#define	PCIE_DOMAIN_LIST_GET(bus_p) \
	((pcie_domains_t *)(PCIE_IS_BDG(bus_p) ?	\
	    PCIE_BUS2DOM(bus_p)->domain.ids.ids : NULL))
#define	PCIE_DOMAIN_LIST_ADD(bus_p, domain_id) \
	if (PCIE_IS_BDG(bus_p)) \
	    pcie_domain_list_add(domain_id, \
		&PCIE_BUS2DOM(bus_p)->domain.ids.ids)
#define	PCIE_DOMAIN_LIST_REMOVE(bus_p, domain_id) \
	if (PCIE_IS_BDG(bus_p)) \
	    pcie_domain_list_remove(domain_id, \
		PCIE_BUS2DOM(bus_p)->domain.ids.ids)

/* XXX below macros only valid/used for nfmadom devices? */
#define	PCIE_BDF_LIST_GET(bus_p) \
	((pcie_req_id_list_t *)(PCIE_IS_BDG(bus_p) ? \
	    PCIE_BUS2DOM(bus_p)->domain.ids.bdfs : NULL))
#define	PCIE_BDF_LIST_ADD(bus_p, bdf) \
	if (PCIE_IS_BDG(bus_p)) \
		pcie_bdf_list_add(bdf, &PCIE_BUS2DOM(bus_p)->domain.ids.bdfs)
#define	PCIE_BDF_LIST_REMOVE(bus_p, bdf) \
	if (PCIE_IS_BDG(bus_p)) \
		pcie_bdf_list_remove(bdf, &PCIE_BUS2DOM(bus_p)->domain.ids.bdfs)

/* Data structure for error channel communication */
typedef struct pcie_fm_buf_hdr {
	uint32_t fb_type;	/* data type in the error buffer */
	uint32_t fb_seq_num;	/* sequence number or total seg count */
	uint64_t fb_data_id;	/* unique ID for data in one scan */
	uint32_t fb_data_size;	/* size of error data */
	pcie_req_id_t fb_bdf;	/* BDF associated with the error data */
	uint16_t fb_pad;	/* padding */
	uint32_t fb_io_sts;	/* response from IO domain */
	uint32_t fb_rsvd[5];	/* reserved */
} pcie_fm_buf_hdr_t;

/* Data structure for error telemetry version negotiation */
typedef struct pcie_fm_ver {
	uint16_t fv_root_maj_ver;	/* root domain major version */
	uint16_t fv_root_min_ver;	/* root domain minor version */
	uint16_t fv_io_maj_ver;		/* IO domain major version */
	uint16_t fv_io_min_ver;		/* IO domain minor version */
} pcie_fm_ver_t;

#define	PCIE_FM_BUF_TYPE_PFD	1		/* data type is pfd */
#define	PCIE_FM_BUF_TYPE_CTRL	2		/* data type is ctrl */
#define	PCIE_FM_BUF_TYPE_VER	3		/* get peer pfd version */

#define	PCIE_FM_PEER_SUCCESS		0
#define	PCIE_FM_PEER_ERR		1

typedef struct pcie_fm_buf {
	pcie_fm_buf_hdr_t fb_header;	/* error channel buffer header */
	char fb_data[1];		/* error data, variable length */
} pcie_fm_buf_t;

/* Error handling status in IO domain */
#define	PCIE_FM_IDLE	0	/* Error data not received */
#define	PCIE_FM_BUSY	1	/* Error data in receiving */
#define	PCIE_FM_ERR	2	/* Error data receiving error */

/* Argument for FMA taskq in IO domain */
typedef struct pcie_fm_tq_arg {
	char *tq_buf;
	dev_info_t *tq_dip;
} pcie_fm_tq_arg_t;

extern boolean_t pcie_enable_io_dom_erpt;

int pcie_assign_device(char *devpath, pcie_fn_type_t type,
    dom_id_t domain_id);
void pcie_unassign_devices(dom_id_t domain_id);
dom_id_t pcie_get_domain_id(dev_info_t *dip);
int pcie_get_assigned_dev_props_by_devpath(char *devpath,
    pcie_fn_type_t *type, dom_id_t *domain_id);
int pcie_rc_taskq_create(dev_info_t *dip);
void pcie_rc_taskq_destroy(dev_info_t *dip);
void pcie_scan_assigned_devs(dev_info_t *rdip);
boolean_t pcie_is_physical_fabric(dev_info_t *dip);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PCIEV_H */
