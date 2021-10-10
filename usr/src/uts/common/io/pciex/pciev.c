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

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/dditypes.h>
#include <sys/ddifm.h>
#include <sys/sunndi.h>
#include <sys/devops.h>
#include <sys/modhash.h>
#include <sys/pcie.h>
#include <sys/pci_cap.h>
#include <sys/pcie_impl.h>
#include <sys/pciev_impl.h>
#include <sys/pci_cfgacc.h>
#include <sys/pathname.h>
#include <sys/time.h>
#include <sys/modhash.h>
#include <sys/instance.h>


#define	PCIV_DEVC_NEW		1 /* assigned device is new */
#define	PCIV_DEVC_UPDATE	2 /* assigned device has changed domain id */
/* no change to assigne device, inicates a spurious request */
#define	PCIV_DEVC_NOCHANGE	3

pciv_assigned_dev_t *pciv_dev_cache = NULL;
static kmutex_t pciv_devcache_mutex;
/* Flag to enable ereports in IO domain */
boolean_t pcie_enable_io_dom_erpt = B_FALSE;

#define	PCIE_ASSIGNED_ROOT	0x1 /* Has device in root dom */
#define	PCIE_ASSIGNED_NFMA	0x2 /* Has device in non-FMA capable dom */
#define	PCIE_ASSIGNED_FMA	0x4 /* Has device in FMA capable dom */

static int pciev_err_find_VFs(pf_impl_t *, pf_data_t *, int);
static pcie_bus_t *pciev_get_affected_dev(pf_impl_t *, pf_data_t *,
    uint16_t, uint16_t, int, int *);

static void pcie_domain_list_destroy(pcie_domains_t *domain_ids);
static void pcie_bdf_list_add(pcie_req_id_t bdf,
    pcie_req_id_list_t **rlist_p);
static void pcie_bdf_list_remove(pcie_req_id_t bdf,
    pcie_req_id_list_t **rlist_p);
static void pcie_cache_domain_info(pcie_bus_t *bus_p);
static void pcie_uncache_domain_info(pcie_bus_t *bus_p);

static void pcie_faulty_list_clear(pcie_bus_t *root_bus_p);
static void pcie_faulty_list_update(pcie_domains_t *pd,
    pcie_domains_t **headp);

static void pciev_err_send_cb(int rc, caddr_t buf, size_t size, caddr_t cb_arg);
static int pciev_err_send(dev_info_t *dip, dom_id_t domain_id, uint32_t type,
    uint32_t seq_num, uint64_t id, void *data);
static void pciev_reset_tree(dev_info_t *pdip, dom_id_t domain_id,
    uint_t prop);
static void pciev_update_nfma_domid(dev_info_t *root_dip,
    dom_id_t domain_id);

/*
 * Initialize locks and structures local to pciev
 */
void
pciev_init()
{
	mutex_init(&pciv_devcache_mutex, NULL, MUTEX_DEFAULT, NULL);
}

void
pciev_fini()
{
	mutex_destroy(&pciv_devcache_mutex);
}

dev_info_t *
pcie_find_dip_by_bdf(dev_info_t *rootp, pcie_req_id_t bdf)
{
	dev_info_t *dip;
	pcie_bus_t *bus_p;
	int bus_num;

	dip = rootp;
	while (dip) {
		if ((bus_p = PCIE_DIP2BUS(dip)) != NULL) {
			if (bus_p->bus_bdf == bdf)
				return (dip);

			/* Only rc/brige/switch nodes have a valid bus range */
			if (PCIE_IS_BDG(bus_p)) {
				dev_info_t *fdip;
				bus_num = (bdf >> 8) & 0xff;
				if (bus_num >= bus_p->bus_bus_range.lo &&
				    bus_num <= bus_p->bus_bus_range.hi) {
					dev_info_t *cdip = ddi_get_child(dip);
					if ((fdip =
					    pcie_find_dip_by_bdf(cdip, bdf)))
						return (fdip);
				}
			}
		}
		dip = ddi_get_next_sibling(dip);
	}
	return (NULL);
}

dev_info_t *
pcie_find_dip_by_unit_addr(char *path)
{
	struct		pathname pn;
	char		*component, *addrname;
	dev_info_t	*dip, *rcdip;
	pcie_bus_t	*bus_p;
	char		*device_type = NULL;

	if (pn_get(path, UIO_SYSSPACE, &pn)) {
		return (NULL);
	}
	pn_skipslash(&pn);
	component = kmem_alloc(MAXNAMELEN, KM_SLEEP);

	dip = ddi_root_node();

	while (pn_pathleft(&pn)) {
		dip = ddi_get_child(dip);
		(void) pn_getcomponent(&pn, component);
		i_ddi_parse_name(component, NULL, &addrname, NULL);

		while (dip) {
			char *name_addr = ddi_get_name_addr(dip);
			char unit_addr[MAXNAMELEN];

			/*
			 * Skip if it is not a device on the PCIe fabric. This
			 * routine could be called before pcie_bus_t populated.
			 */
			if (ddi_get_parent(dip) == ddi_root_node()) {
				if ((ddi_prop_lookup_string(DDI_DEV_T_ANY, dip,
				    DDI_PROP_DONTPASS, "device_type",
				    &device_type) != DDI_PROP_SUCCESS) ||
				    (strcmp(device_type, "pciex") != 0)) {
					dip = ddi_get_next_sibling(dip);
					rcdip = NULL;
					if (device_type) {
						ddi_prop_free(device_type);
						device_type = NULL;
					}
					continue;
				}
				if (device_type) {
					ddi_prop_free(device_type);
					device_type = NULL;
				}
				/* The dip is RC */
				rcdip = dip;
				bus_p = PCIE_DIP2BUS(rcdip);
			}

			if (!name_addr && bus_p &&
			    (bus_p->bus_cfgacc_base != INVALID_CFGACC_BASE)) {
				/*
				 * If RC dip's pcie_bus_t is not populated, or
				 * the bus_cfgacc_base is not initialized, the
				 * pcie_get_unit_addr can not be called.
				 */
				ASSERT(PCIE_IS_RC(PCIE_DIP2BUS(rcdip)));
				(void) pcie_get_unit_addr(dip, unit_addr);
				name_addr = &unit_addr[0];
			}

			if (name_addr && strcmp(addrname, name_addr) == 0)
				break;

			dip = ddi_get_next_sibling(dip);
		}
		if (!dip)
			break;
		pn_skipslash(&pn);
	}

	pn_free(&pn);
	kmem_free(component, MAXNAMELEN);

	return (dip);
}
/*
 * Add a device bdf to the bdf list.
 */
static void
pcie_bdf_list_add(pcie_req_id_t bdf, pcie_req_id_list_t **rlist_p)
{
	pcie_req_id_list_t *rl = PCIE_ZALLOC(pcie_req_id_list_t);

	rl->bdf = bdf;
	rl->next = *rlist_p;
	*rlist_p = rl;
}

/*
 * Remove a bdf from the bdf list.
 */
static void
pcie_bdf_list_remove(pcie_req_id_t bdf, pcie_req_id_list_t **rlist_p)
{
	pcie_req_id_list_t *rl_pre, *rl_next;

	rl_pre = *rlist_p;
	if (rl_pre == NULL)
		return;

	if (rl_pre->bdf == bdf) {
		*rlist_p = rl_pre->next;
		kmem_free(rl_pre, sizeof (pcie_req_id_list_t));
		return;
	}

	while (rl_pre->next) {
		rl_next = rl_pre->next;
		if (rl_next->bdf == bdf) {
			rl_pre->next = rl_next->next;
			kmem_free(rl_next, sizeof (pcie_req_id_list_t));
			break;
		} else
			rl_pre = rl_next;
	}
}

/*
 * Cache IOV domain info in all it's parent's pcie_domain_t
 *
 * The leaf devices's domain info must be set before calling this function.
 */
static void
pcie_cache_domain_info(pcie_bus_t *bus_p)
{
	boolean_t 	assigned = PCIE_IS_ASSIGNED(bus_p);
	boolean_t 	fma_dom = PCIE_ASSIGNED_TO_FMA_DOM(bus_p);
	dom_id_t	domain_id = PCIE_DOMAIN_ID_GET(bus_p);
	pcie_req_id_t	bdf = bus_p->bus_bdf;
	dev_info_t	*pdip;
	pcie_bus_t	*pbus_p;
	pcie_domain_t	*pdom_p;

	ASSERT(!PCIE_IS_BDG(bus_p));

	for (pdip = ddi_get_parent(PCIE_BUS2DIP(bus_p)); PCIE_DIP2BUS(pdip);
	    pdip = ddi_get_parent(pdip)) {
		pbus_p = PCIE_DIP2BUS(pdip);
		pdom_p = PCIE_BUS2DOM(pbus_p);

		if (assigned) {
			if (domain_id != PCIE_UNKNOWN_DOMAIN_ID)
				PCIE_DOMAIN_LIST_ADD(pbus_p, domain_id);

			if (fma_dom)
				pdom_p->fmadom_count++;
			else {
				PCIE_BDF_LIST_ADD(pbus_p, bdf);
				pdom_p->nfmadom_count++;
			}
		} else
			pdom_p->rootdom_count++;
	}
}

/*
 * Clear the leaf device's domain info and uncache IOV domain info in all it's
 * parent's pcie_domain_t
 *
 * The leaf devices's domain info is also cleared by calling this function.
 */
static void
pcie_uncache_domain_info(pcie_bus_t *bus_p)
{
	boolean_t 	assigned = PCIE_IS_ASSIGNED(bus_p);
	boolean_t 	fma_dom = PCIE_ASSIGNED_TO_FMA_DOM(bus_p);
	dom_id_t	domain_id = PCIE_DOMAIN_ID_GET(bus_p);
	pcie_domain_t	*dom_p = PCIE_BUS2DOM(bus_p), *pdom_p;
	pcie_bus_t	*pbus_p;
	dev_info_t	*pdip;

	ASSERT(!PCIE_IS_BDG(bus_p));
	ASSERT((dom_p->fmadom_count + dom_p->nfmadom_count +
	    dom_p->rootdom_count) == 1);

	/* Clear the domain information */
	if (domain_id != PCIE_UNKNOWN_DOMAIN_ID) {
		PCIE_DOMAIN_ID_SET(bus_p, PCIE_UNKNOWN_DOMAIN_ID);
		PCIE_DOMAIN_ID_DECR_REF_COUNT(bus_p);
	}

	dom_p->fmadom_count = 0;
	dom_p->nfmadom_count = 0;
	dom_p->rootdom_count = 0;

	for (pdip = ddi_get_parent(PCIE_BUS2DIP(bus_p)); PCIE_DIP2BUS(pdip);
	    pdip = ddi_get_parent(pdip)) {
		pbus_p = PCIE_DIP2BUS(pdip);
		pdom_p = PCIE_BUS2DOM(pbus_p);

		if (assigned) {
			if (domain_id != PCIE_UNKNOWN_DOMAIN_ID)
				PCIE_DOMAIN_LIST_REMOVE(pbus_p, domain_id);

			if (fma_dom)
				pdom_p->fmadom_count--;
			else {
				pdom_p->nfmadom_count--;
				PCIE_BDF_LIST_REMOVE(pbus_p, bus_p->bus_bdf);
			}
		} else
			pdom_p->rootdom_count--;
	}
}


/*
 * Initialize private data structure for IOV environments.
 * o Allocate memory for iov data
 * o Cache Domain ids.
 */
void
pcie_init_dom(dev_info_t *dip)
{
	pcie_domain_t	*dom_p;
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);

	if (PCIE_BUS2DOM(bus_p))
		return;

	dom_p = PCIE_ZALLOC(pcie_domain_t);
	PCIE_BUS2DOM(bus_p) = dom_p;

	/* init domain list hash table for RC/RP */
	if (PCIE_IS_ROOT(bus_p)) {
		char	name[32];

		(void) sprintf(name, "%s%d domain list hash",
		    ddi_driver_name(dip), ddi_get_instance(dip));
		dom_p->dom_hashp = kmem_zalloc(sizeof (pcie_dom_hash_t),
		    KM_SLEEP);
		pciev_domain_list_init(dom_p->dom_hashp, name);
	}

	/* Only leaf devices are assignable to IO Domains */
	if (PCIE_IS_BDG(bus_p))
		return;

	PCIE_DOMAIN_ID_SET(bus_p, PCIE_UNKNOWN_DOMAIN_ID);
	if (DEVI_IS_ASSIGNED(dip)) {
		/*
		 * This is an assigned device, but at this moment it is not
		 * known whether the assigned domain is FMA capable or not.
		 * Mark it as non-fma capable until otherwise notified.
		 */
		dom_p->nfmadom_count = 1;

	} else {
		dom_p->rootdom_count = 1;
	}
	pcie_cache_domain_info(bus_p);
}

void
pcie_fini_dom(dev_info_t *dip)
{
	pcie_domain_t	*dom_p = PCIE_DIP2DOM(dip);
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);

	if (PCIE_IS_BDG(bus_p))
		pcie_domain_list_destroy(PCIE_DOMAIN_LIST_GET(bus_p));
	else
		pcie_uncache_domain_info(bus_p);

	if (PCIE_IS_ROOT(bus_p)) {
		pciev_domain_list_fini(dom_p->dom_hashp);
		kmem_free(dom_p->dom_hashp, sizeof (pcie_dom_hash_t));
	}

	kmem_free(dom_p, sizeof (pcie_domain_t));
	PCIE_DIP2DOM(dip) = NULL;
}

/*
 * PCIe Severity:
 *
 * PF_ERR_NO_ERROR	: no IOV Action
 * PF_ERR_CE		: no IOV Action
 * PF_ERR_NO_PANIC	: contains error telemetry, log domain info
 * PF_ERR_MATCHED_DEVICE: contains error telemetry, log domain info
 * PF_ERR_MATCHED_RC	: Error already taken care of, no further IOV Action
 * PF_ERR_MATCHED_PARENT: Error already taken care of, no further IOV Action
 * PF_ERR_PANIC		: contains error telemetry, log domain info
 *
 * For NO_PANIC, MATCHED_DEVICE and PANIC, IOV wants to look at the affected
 * devices and find the domains involved.
 *
 * If root domain does not own an affected device, IOV EH should change
 * PF_ERR_PANIC to PF_ERR_MATCH_DOM.
 */
int
pciev_eh(pf_data_t *pfd_p, pf_impl_t *impl)
{
	int severity = pfd_p->pe_severity_flags;
	int iov_severity = severity;
	pcie_bus_t *a_bus_p;	/* Affected device's pcie_bus_t */
	pf_data_t *root_pfd_p = impl->pf_dq_head_p;
	pcie_bus_t *root_bus_p;
	pcie_domain_t *a_dom_p;

	/*
	 * check if all devices under the root device are unassigned.
	 * this function should quickly return in non-IOV environment.
	 */
	ASSERT(root_pfd_p != NULL);
	root_bus_p = PCIE_PFD2BUS(root_pfd_p);
	if (PCIE_BDG_IS_UNASSIGNED(root_bus_p))
		return (severity);

	if (severity & PF_ERR_PANIC_DEADLOCK) {
		PCIE_BUS2DOM(root_bus_p)->faulty_all = B_TRUE;

	} else if (severity & (PF_ERR_NO_PANIC | PF_ERR_MATCHED_DEVICE |
	    PF_ERR_PANIC | PF_ERR_PANIC_BAD_RESPONSE)) {

		uint16_t affected_flag, dev_affected_flags;
		uint_t is_panic = 0, is_aff_dev_found = 0;

		dev_affected_flags = PFD_AFFECTED_DEV(pfd_p)->pe_affected_flags;
		/* adjust affected flags to leverage cached domain ids */
		if (dev_affected_flags & PF_AFFECTED_CHILDREN) {
			dev_affected_flags |= PF_AFFECTED_SELF;
			dev_affected_flags &= ~PF_AFFECTED_CHILDREN;
		}

		for (affected_flag = 1;
		    affected_flag <= PF_MAX_AFFECTED_FLAG;
		    affected_flag <<= 1) {
			int	result;

			a_bus_p = pciev_get_affected_dev(impl, pfd_p,
			    affected_flag, dev_affected_flags,
			    severity, &result);

			if (((a_bus_p == NULL) &&
			    (affected_flag != PF_AFFECTED_ALL_VFS)) ||
			    ((affected_flag == PF_AFFECTED_ALL_VFS) &&
			    (result == 0)))
				continue;

			is_aff_dev_found++;

			if (affected_flag == PF_AFFECTED_ALL_VFS) {
				if ((result & PCIE_ASSIGNED_ROOT) &&
				    (severity & PF_ERR_FATAL_FLAGS)) {
					is_panic++;
				}
				if (((result & PCIE_ASSIGNED_NFMA) &&
				    (severity & PF_ERR_FATAL_FLAGS)) ||
				    (result & PCIE_ASSIGNED_FMA)) {
					iov_severity |= PF_ERR_MATCH_DOM;
				}
				continue;
			}

			a_dom_p = PCIE_BUS2DOM(a_bus_p);

			/*
			 * If a leaf device is assigned to the root domain or if
			 * a bridge has children assigned to a root domain
			 * panic.
			 *
			 * If a leaf device or a child of a bridge is assigned
			 * to NFMA domain mark it for panic.  If assigned to FMA
			 * domain save the domain id.
			 */
			if (!PCIE_IS_BDG(a_bus_p) &&
			    !PCIE_IS_ASSIGNED(a_bus_p)) {
				if (severity & PF_ERR_FATAL_FLAGS)
					is_panic++;
				continue;
			}

			if (PCIE_BDG_HAS_CHILDREN_ROOT_DOM(a_bus_p)) {
				if (severity & PF_ERR_FATAL_FLAGS)
					is_panic++;
			}

			if ((PCIE_ASSIGNED_TO_NFMA_DOM(a_bus_p) ||
			    PCIE_BDG_HAS_CHILDREN_NFMA_DOM(a_bus_p)) &&
			    (severity & PF_ERR_FATAL_FLAGS)) {
				a_dom_p->nfma_panic = B_TRUE;
				iov_severity |= PF_ERR_MATCH_DOM;
			}

			if (PCIE_ASSIGNED_TO_FMA_DOM(a_bus_p)) {
				pcie_save_domain_id(
				    &a_dom_p->domain.id, root_bus_p);
				iov_severity |= PF_ERR_MATCH_DOM;

				if (severity & PF_ERR_FATAL_FLAGS)
					a_dom_p->nfma_panic_backup = B_TRUE;
			}

			if (PCIE_BDG_HAS_CHILDREN_FMA_DOM(a_bus_p)) {
				pcie_save_domain_id(
				    PCIE_DOMAIN_LIST_GET(a_bus_p),
				    root_bus_p);
				iov_severity |= PF_ERR_MATCH_DOM;

				if (severity & PF_ERR_FATAL_FLAGS)
					a_dom_p->nfma_panic_backup = B_TRUE;
			}
		}

		/*
		 * Overwrite the severity only if affected device can be
		 * identified and root domain does not need to panic.
		 */
		if ((!is_panic) && is_aff_dev_found) {
			iov_severity &= ~PF_ERR_FATAL_FLAGS;
		}
	}

	return (iov_severity);
}

/* ARGSUSED */
int
pciev_eh_exit(pf_data_t *root_pfd_p, uint_t intr_type)
{
	pcie_bus_t *root_bus_p;
	pf_data_t *pfd_p;
	pcie_domains_t *dom_p;
	uint_t ref_count, seq_num;
	uint64_t id;
	int ret = 0, sts = 0;

	/*
	 * check if all devices under the root device are unassigned.
	 * this function should quickly return in non-IOV environment.
	 */
	root_bus_p = PCIE_PFD2BUS(root_pfd_p);
	if (PCIE_BDG_IS_UNASSIGNED(root_bus_p))
		return (sts);

	/* forward error data to its corresponding domain */
	dom_p = (PCIE_BUS2DOM(root_bus_p)->faulty_all) ?
	    PCIE_DOMAIN_LIST_GET(root_bus_p) :
	    PCIE_BUS2DOM(root_bus_p)->faulty_domains;

	while (dom_p) {
		ref_count = (PCIE_BUS2DOM(root_bus_p)->faulty_all) ?
		    dom_p->cached_count : dom_p->faulty_count;
		if (ref_count == 0)
			continue;

		seq_num = 0;
		id = (uint64_t)gethrtime();
		for (pfd_p = root_pfd_p; pfd_p; pfd_p = pfd_p->pe_next) {
			if (PCIE_IN_DOMAIN(PCIE_PFD2BUS(pfd_p),
			    dom_p->domain_id)) {
				ret = pciev_err_send(PCIE_BUS2DIP(root_bus_p),
				    dom_p->domain_id, PCIE_FM_BUF_TYPE_PFD,
				    seq_num, id, (void *)pfd_p);
				if (ret != 0) {
					sts |= ret;
					pciev_update_nfma_domid(
					    PCIE_BUS2DIP(root_bus_p),
					    dom_p->domain_id);
					break;
				}
				seq_num++;
			}
		}
		if ((ret == 0) && (seq_num > 0)) {
			/* send a ctrl packet with no data as stop sign */
			ret = pciev_err_send(PCIE_BUS2DIP(root_bus_p),
			    dom_p->domain_id, PCIE_FM_BUF_TYPE_CTRL,
			    seq_num, id, NULL);
			if (ret != 0) {
				sts |= ret;
				pciev_update_nfma_domid(
				    PCIE_BUS2DIP(root_bus_p),
				    dom_p->domain_id);
			}
		}

		dom_p = (PCIE_BUS2DOM(root_bus_p)->faulty_all) ?
		    dom_p->cached_next : dom_p->faulty_next;

	}

	pcie_faulty_list_clear(root_bus_p);
	return (sts);
}


/*
 * Search all VFs for affected domains or bdfs and return domain
 * assignment info of VFs
 */
static int
pciev_err_find_VFs(pf_impl_t *impl, pf_data_t *pfd_p, int severity)
{
	pcie_bus_t *temp_bus_p, *root_bus_p;
	pf_data_t *temp_pfd_p, *root_pfd_p = impl->pf_dq_head_p;
	pcie_domain_t *a_dom_p;
	dev_info_t *pf_dip = PCIE_PFD2DIP(pfd_p);
	int result = 0;

	ASSERT(root_pfd_p != NULL);
	root_bus_p = PCIE_PFD2BUS(root_pfd_p);

	if (!PCIE_IS_PF(PCIE_PFD2BUS(pfd_p)))
		return (result);

	for (temp_pfd_p = root_pfd_p; temp_pfd_p;
	    temp_pfd_p = temp_pfd_p->pe_next) {
		temp_bus_p = PCIE_PFD2BUS(temp_pfd_p);
		if (PCIE_GET_PF_DIP(temp_bus_p) != pf_dip)
			continue;

		/* VF is found */
		ASSERT(!PCIE_IS_BDG(temp_bus_p));
		a_dom_p = PCIE_BUS2DOM(temp_bus_p);

		if (!PCIE_IS_ASSIGNED(temp_bus_p)) {
			result |= PCIE_ASSIGNED_ROOT;
		} else if (PCIE_ASSIGNED_TO_NFMA_DOM(temp_bus_p)) {
			result |= PCIE_ASSIGNED_NFMA;

			if (severity & PF_ERR_FATAL_FLAGS)
				a_dom_p->nfma_panic = B_TRUE;
		} else if (PCIE_ASSIGNED_TO_FMA_DOM(temp_bus_p)) {
			result |= PCIE_ASSIGNED_FMA;
			pcie_save_domain_id(
			    &a_dom_p->domain.id, root_bus_p);

			if (severity & PF_ERR_FATAL_FLAGS)
				a_dom_p->nfma_panic_backup = B_TRUE;
		}
	}

	return (result);
}

/*
 * result - point to a bitmask value which returns affected device info
 * when affected flag is PF_AFFECTED_ALL_VFS:
 * PCIE_ASSIGNED_ROOT - has affected device in root domain
 * PCIE_ASSIGNED_NFMA - has affected device in non-FMA capable domain
 * PCIE_ASSIGNED_FMA - has affected device in FMA capable domain
 * 0 - affected flag is other value
 */
static pcie_bus_t *
pciev_get_affected_dev(pf_impl_t *impl, pf_data_t *pfd_p,
    uint16_t affected_flag, uint16_t dev_affected_flags,
    int severity, int *result)
{
	pcie_bus_t *bus_p = PCIE_PFD2BUS(pfd_p);
	uint16_t flag = affected_flag & dev_affected_flags;
	pcie_bus_t *temp_bus_p = NULL;
	pcie_req_id_t a_bdf;
	uint64_t a_addr;
	uint16_t cmd;

	*result = 0;
	if (!flag)
		return (NULL);

	switch (flag) {
	case PF_AFFECTED_ROOT:
		/* XXX RC or RP */
		temp_bus_p = PCIE_DIP2BUS(bus_p->bus_rp_dip);
		break;
	case PF_AFFECTED_SELF:
		temp_bus_p = bus_p;
		break;
	case PF_AFFECTED_PARENT:
		temp_bus_p = PCIE_DIP2BUS(ddi_get_parent(PCIE_BUS2DIP(bus_p)));
		break;
	case PF_AFFECTED_BDF: /* may only be used for RC */
		a_bdf = PFD_AFFECTED_DEV(pfd_p)->pe_affected_bdf;
		if (!PCIE_CHECK_VALID_BDF(a_bdf))
			break;

		temp_bus_p = pf_find_busp_by_bdf(impl, a_bdf);
		break;
	case PF_AFFECTED_AER:
		if (pf_tlp_decode(bus_p, PCIE_ADV_REG(pfd_p)) == DDI_SUCCESS) {
			temp_bus_p = pf_find_busp_by_aer(impl, pfd_p);
		}
		break;
	case PF_AFFECTED_SAER:
		if (pf_pci_decode(pfd_p, &cmd) == DDI_SUCCESS) {
			temp_bus_p = pf_find_busp_by_saer(impl, pfd_p);
		}
		break;
	case PF_AFFECTED_ADDR: /* ROOT only */
		a_addr = PFD_ROOT_ERR_INFO(pfd_p)->scan_addr;
		if (a_addr == 0)
			break;

		temp_bus_p = pf_find_busp_by_addr(impl, a_addr);
		break;
	case PF_AFFECTED_ALL_VFS:
		/*
		 * all VFs are affected, search all affected domains here
		 * instead of in pciev_eh because not a single bus_p can
		 * be returned.
		 */
		*result = pciev_err_find_VFs(impl, pfd_p, severity);
		break;
	}

	if (temp_bus_p != NULL)
		PFD_AFFECTED_DEV(pfd_p)->pe_affected_bdf = temp_bus_p->bus_bdf;

	return (temp_bus_p);
}

/* type used for pcie_domain_list_find() function */
typedef enum {
	PCIE_DOM_LIST_TYPE_CACHE = 1,
	PCIE_DOM_LIST_TYPE_FAULT = 2
} pcie_dom_list_type_t;

/*
 * Check if a domain id is already in the linked list
 */
static pcie_domains_t *
pcie_domain_list_find(dom_id_t domain_id, pcie_domains_t *pd_list_p,
    pcie_dom_list_type_t type)
{
	while (pd_list_p) {
		if (pd_list_p->domain_id == domain_id)
			return (pd_list_p);

		if (type == PCIE_DOM_LIST_TYPE_CACHE) {
			pd_list_p = pd_list_p->cached_next;
		} else if (type == PCIE_DOM_LIST_TYPE_FAULT) {
			pd_list_p = pd_list_p->faulty_next;
		} else {
			return (NULL);
		}
	}

	return (NULL);
}

/*
 * Return true if a leaf device is assigned to a domain or a bridge device
 * has children assigned to the domain
 */
boolean_t
pcie_in_domain(pcie_bus_t *bus_p, dom_id_t domain_id)
{
	if (!PCIE_BUS2DOM(bus_p)) {
		return (B_FALSE);
	}

	if (PCIE_IS_BDG(bus_p)) {
		pcie_domains_t *pd;
		pd = pcie_domain_list_find(domain_id,
		    PCIE_DOMAIN_LIST_GET(bus_p), PCIE_DOM_LIST_TYPE_CACHE);
		if (pd && pd->cached_count)
			return (B_TRUE);
		return (B_FALSE);
	} else {
		return (PCIE_DOMAIN_ID_GET(bus_p) == domain_id);
	}
}

/*
 * Add a domain id to a cached domain id list.
 * If the domain already exists in the list, increment the reference count.
 */
void
pcie_domain_list_add(dom_id_t domain_id, pcie_domains_t **pd_list_p)
{
	pcie_domains_t *pd;

	pd = pcie_domain_list_find(domain_id, *pd_list_p,
	    PCIE_DOM_LIST_TYPE_CACHE);

	if (pd == NULL) {
		pd = PCIE_ZALLOC(pcie_domains_t);
		pd->domain_id = domain_id;
		pd->cached_count = 1;
		pd->cached_next = *pd_list_p;
		*pd_list_p = pd;
	} else {
		pd->cached_count++;
	}
}

/*
 * Remove a domain id from a cached domain id list.
 * Decrement the reference count.
 */
void
pcie_domain_list_remove(dom_id_t domain_id, pcie_domains_t *pd_list_p)
{
	pcie_domains_t *pd;

	pd = pcie_domain_list_find(domain_id, pd_list_p,
	    PCIE_DOM_LIST_TYPE_CACHE);

	if (pd) {
		ASSERT((pd->cached_count)--);
	}
}

/* destroy cached domain id list */
static void
pcie_domain_list_destroy(pcie_domains_t *domain_ids)
{
	pcie_domains_t *p = domain_ids;
	pcie_domains_t *next;

	while (p) {
		next = p->cached_next;
		kmem_free(p, sizeof (pcie_domains_t));
		p = next;
	}
}

static void
pcie_faulty_list_update(pcie_domains_t *pd,
    pcie_domains_t **headp)
{
	if (pd == NULL)
		return;

	if (*headp == NULL) {
		*headp = pd;
		pd->faulty_prev = NULL;
		pd->faulty_next = NULL;
		pd->faulty_count = 1;
	} else {
		pd->faulty_next = *headp;
		(*headp)->faulty_prev = pd;
		pd->faulty_prev = NULL;
		pd->faulty_count = 1;
		*headp = pd;
	}
}

static void
pcie_faulty_list_clear(pcie_bus_t *root_bus_p)
{
	pcie_domains_t *pd = PCIE_BUS2DOM(root_bus_p)->faulty_domains;
	pcie_domains_t *next;

	/* unlink all domain structures from the faulty list */
	while (pd) {
		next = pd->faulty_next;
		pd->faulty_prev = NULL;
		pd->faulty_next = NULL;
		pd->faulty_count = 0;
		pd = next;
	}
	PCIE_BUS2DOM(root_bus_p)->faulty_domains = NULL;
	PCIE_BUS2DOM(root_bus_p)->faulty_all = B_FALSE;
}

void
pcie_save_domain_id(pcie_domains_t *domain_ids, pcie_bus_t *root_bus_p)
{
	pcie_domains_t *old_list_p, *new_list_p, *pd;
	pcie_dom_hash_t *dh;

	if (PCIE_BUS2DOM(root_bus_p)->faulty_all)
		return;

	if (domain_ids == NULL)
		return;

	old_list_p = PCIE_BUS2DOM(root_bus_p)->faulty_domains;
	dh = PCIE_BUS2DOM(root_bus_p)->dom_hashp;
	for (new_list_p = domain_ids; new_list_p;
	    new_list_p = new_list_p->cached_next) {
		if (!new_list_p->cached_count)
			continue;

		if (!(pciev_get_domain_prop(dh, new_list_p->domain_id) &
		    PCIE_DOM_PROP_FMA))
			continue;

		/* search domain id in the faulty domain list */
		pd = pcie_domain_list_find(new_list_p->domain_id,
		    old_list_p, PCIE_DOM_LIST_TYPE_FAULT);
		if (pd)
			pd->faulty_count++;
		else
			pcie_faulty_list_update(new_list_p,
			    &PCIE_BUS2DOM(root_bus_p)->faulty_domains);
	}
}

static void
pciev_err_send_cb(int rc, caddr_t buf, size_t size, caddr_t cb_arg)
{
	pcie_fm_buf_t *fm_buf = (pcie_fm_buf_t *)((uintptr_t)buf);
	pcie_fm_buf_hdr_t *fm_hdr = &fm_buf->fb_header;
	pcie_channel_cb_t *cb = (pcie_channel_cb_t *)(void *)cb_arg;

	if ((rc != DDI_SUCCESS) ||
	    (fm_hdr->fb_io_sts && PCIE_FM_PEER_ERR)) {
		PCIE_DBG("pciev_err_send_cb error, dip 0x%p, rc=%d, "
		    "io_sts 0x%x\n", (void *)cb->dip, rc, fm_hdr->fb_io_sts);
		/* Mark domain as non channel capable */
		(void) pciev_update_domain_prop(cb->dip, cb->dom_id, 0);
	}

	kmem_free(buf, size);
	kmem_free(cb_arg, sizeof (pcie_channel_cb_t));
}

static int
pciev_err_send(dev_info_t *dip, dom_id_t domain_id, uint32_t type,
    uint32_t seq_num, uint64_t id, void *data)
{
	pcie_fm_buf_t *fm_buf;
	pcie_fm_buf_hdr_t *fm_hdr;
	pf_data_t *pfd_p;
	size_t len;
	char *start, *buf;
	int ret;
	pcie_channel_cb_t *cb_arg;

	if (type == PCIE_FM_BUF_TYPE_PFD) {
		if (data == NULL)
			return (EINVAL);
		pfd_p = (pf_data_t *)data;
		start = (char *)(PFD_BLK_HDR(pfd_p));
		len = pfd_p->pe_len + (uintptr_t)pfd_p  - (uintptr_t)start;
		if (PFD_IS_ROOT(pfd_p)) {
			PFD_ROOT_ERR_INFO(pfd_p)->severity_flags =
			    pfd_p->pe_orig_severity_flags;
		}
	} else if (type == PCIE_FM_BUF_TYPE_CTRL) {
		len = 1;
	} else
		return (EINVAL);

	fm_buf = kmem_zalloc((sizeof (pcie_fm_buf_t) + len - 1), KM_NOSLEEP);
	if (fm_buf == NULL)
		return (ENOMEM);
	buf = fm_buf->fb_data;

	fm_hdr = &fm_buf->fb_header;
	fm_hdr->fb_type = type;
	fm_hdr->fb_seq_num = seq_num;
	fm_hdr->fb_data_id = id;
	fm_hdr->fb_data_size = (uint32_t)len;
	fm_hdr->fb_io_sts = PCIE_FM_PEER_SUCCESS;

	if (type == PCIE_FM_BUF_TYPE_PFD) {
		bcopy(start, buf, len);
		fm_hdr->fb_bdf = PCIE_PFD2BUS(pfd_p)->bus_bdf;
	}

	cb_arg = kmem_zalloc(sizeof (pcie_channel_cb_t), KM_NOSLEEP);
	if (cb_arg == NULL) {
		kmem_free(fm_buf, (sizeof (pcie_fm_buf_t) + len - 1));
		return (ENOMEM);
	}
	cb_arg->dip = dip;
	cb_arg->dom_id = domain_id;
	ret = pciv_domain_send(dip, domain_id, PCIV_PKT_FABERR, (caddr_t)fm_buf,
	    (sizeof (pcie_fm_buf_t) + len - 1), pciev_err_send_cb,
	    (caddr_t)cb_arg);
	if (ret != DDI_SUCCESS) {
		PCIE_DBG("pciev_err_send failed, dip 0x%p, domain_id"
		    " %"PRIu64", ret=%d\n", (void *)dip, domain_id, ret);
		return (EIO);
	} else {
		PCIE_DBG("pciev_err_send succeed, dip 0x%p, domain_id"
		    " %"PRIu64", ret=%d\n", (void *)dip, domain_id, ret);
	}

	return (0);
}

/*
 * Get peer OS pfd version, used in root domain
 */
int
pciev_get_peer_err_ver(dev_info_t *dip, dom_id_t domain_id,
    uint16_t *peer_maj, uint16_t *peer_min)
{
	pcie_fm_ver_t fm_ver;
	pcie_fm_buf_t *fm_buf;
	pcie_fm_buf_hdr_t *fm_hdr;
	int ret;
	size_t len;

	fm_ver.fv_root_maj_ver = PCIE_PFD_MAJOR_VER;
	fm_ver.fv_root_min_ver = PCIE_PFD_MINOR_VER;
	fm_ver.fv_io_maj_ver = 0;
	fm_ver.fv_io_min_ver = 0;

	len = sizeof (pcie_fm_buf_t) + sizeof (pcie_fm_ver_t) - 1;

	fm_buf = kmem_zalloc(len, KM_NOSLEEP);
	if (fm_buf == NULL)
		return (-1);

	fm_hdr = &fm_buf->fb_header;
	fm_hdr->fb_type = PCIE_FM_BUF_TYPE_VER;
	fm_hdr->fb_data_size = sizeof (pcie_fm_ver_t);
	fm_hdr->fb_io_sts = PCIE_FM_PEER_SUCCESS;
	bcopy((char *)&fm_ver, fm_buf->fb_data, sizeof (pcie_fm_ver_t));

	ret = pciv_domain_send(dip, domain_id, PCIV_PKT_FABERR,
	    (caddr_t)fm_buf, len, NULL, NULL);
	if (ret != DDI_SUCCESS ||
	    (fm_hdr->fb_io_sts && PCIE_FM_PEER_ERR)) {
		PCIE_DBG("pciev_get_peer_err_ver failed, dip 0x%p, domain_id"
		    " %"PRIu64", ret=%d, io_sts=0x%x\n", (void *)dip, domain_id,
		    ret, fm_hdr->fb_io_sts);
		kmem_free(fm_buf, len);
		return (-1);
	}

	bcopy(fm_buf->fb_data, (char *)&fm_ver, sizeof (pcie_fm_ver_t));
	kmem_free(fm_buf, len);
	*peer_maj = fm_ver.fv_io_maj_ver;
	*peer_min = fm_ver.fv_io_min_ver;

	return (0);
}

void
pciev_set_negotiated_err_ver(pf_impl_t *impl, uint16_t minor_ver)
{
	impl->pf_neg_min_ver = minor_ver;
}

/*
 * copy error telemetry data to pfd, used in IO domain
 */
void
pciev_process_err_data(char *data, pf_data_t *pfd_p)
{
	char		*src_addr, *dst_addr;
	size_t		len;
	int		i;
	pf_blk_hdr_t	*blk_p = (pf_blk_hdr_t *)(uintptr_t)data;

	for (i = 0; i < PF_MAX_REG_BLOCK; i++) {
		len = min(blk_p[i].blk_len, PF_LEN(pfd_p, i));
		if (len == 0)
			continue;

		src_addr = data + blk_p[i].blk_off;
		dst_addr = PF_ADDR(pfd_p, i);
		bcopy(src_addr, dst_addr, len);
	}
}

/* Domain list operation functions */

static void
pciev_free_domain_list_entry(mod_hash_val_t val)
{
	pcie_domain_entry_t *de = (pcie_domain_entry_t *)val;

	kmem_free(de, sizeof (pcie_domain_entry_t));
}

void
pciev_domain_list_init(pcie_dom_hash_t *dh, char *name)
{
	if (dh == NULL)
		return;

	dh->dh_hashp = mod_hash_create_idhash(name,
	    PCIEV_DOM_HASHSZ, pciev_free_domain_list_entry);

	rw_init(&dh->dh_lock, NULL, RW_DEFAULT, NULL);
}

void
pciev_domain_list_fini(pcie_dom_hash_t *dh)
{
	if (dh == NULL)
		return;

	mod_hash_destroy_hash(dh->dh_hashp);
	rw_destroy(&dh->dh_lock);
}

/* the caller needs to grab lock */
boolean_t
pciev_in_domain_list(pcie_dom_hash_t *dh, dom_id_t domain_id,
    pcie_domain_entry_t **entry_p)
{
	mod_hash_key_t key;

	if (dh == NULL)
		return (B_FALSE);

	key = (mod_hash_key_t)(uintptr_t)domain_id;
	if (mod_hash_find(dh->dh_hashp, key,
	    (mod_hash_val_t *)entry_p) == 0)
		return (B_TRUE);
	else
		return (B_FALSE);
}

/* ARGSUSED */
static uint_t
clear_prop(mod_hash_key_t key, mod_hash_val_t *val, void *arg)
{
	pcie_domain_entry_t *de = (pcie_domain_entry_t *)val;
	uint_t *prop_p = arg;
	uint_t prop = *prop_p;

	if (de->domain_prop & prop)
		de->domain_prop &= ~prop;

	return (MH_WALK_CONTINUE);
}

void
pciev_clear_domain_prop(pcie_dom_hash_t *dh, uint_t prop)
{
	if (dh == NULL)
		return;

	rw_enter(&dh->dh_lock, RW_WRITER);
	mod_hash_walk(dh->dh_hashp, clear_prop, &prop);
	rw_exit(&dh->dh_lock);
}

uint_t
pciev_get_domain_prop(pcie_dom_hash_t *dh, dom_id_t domain_id)
{
	pcie_domain_entry_t *de;
	uint_t prop = 0;

	if (dh == NULL)
		return (prop);

	rw_enter(&dh->dh_lock, RW_READER);
	if (pciev_in_domain_list(dh, domain_id, &de)) {
		prop = de->domain_prop;
	}
	rw_exit(&dh->dh_lock);

	return (prop);
}

void
pciev_set_domain_prop(pcie_dom_hash_t *dh, dom_id_t domain_id, uint_t prop)
{
	pcie_domain_entry_t *de;

	ASSERT(dh);

	rw_enter(&dh->dh_lock, RW_WRITER);
	if (pciev_in_domain_list(dh, domain_id, &de)) {
		de->domain_prop = prop;
	}
	rw_exit(&dh->dh_lock);
}

/*
 * Update the global domain list with new domain or new property,
 * and update the cached info in the device tree accordingly.
 * devi -- root dip
 */
int
pciev_update_domain_prop(dev_info_t *devi, dom_id_t domain_id, uint_t prop)
{
	pcie_domain_entry_t *de;
	mod_hash_key_t key;
	int ret = 0, changed = 0;
	pcie_dom_hash_t *dh;

	dh = PCIE_DIP2DOM(devi)->dom_hashp;

	if (dh == NULL)
		return (1);

	key = (mod_hash_key_t)(uintptr_t)domain_id;

	rw_enter(&dh->dh_lock, RW_WRITER);
	if (pciev_in_domain_list(dh, domain_id, &de)) {
		/* the domain exists in the list, update its property */
		if ((de->domain_prop & PCIE_DOM_PROP_FMA) !=
		    (prop & PCIE_DOM_PROP_FMA)) {
			changed = 1;
		}

		de->domain_prop = prop;
	} else {
		de = kmem_alloc(sizeof (pcie_domain_entry_t), KM_SLEEP);
		de->domain_id = domain_id;
		de->domain_prop = prop;
		if (mod_hash_insert(dh->dh_hashp, key,
		    (mod_hash_val_t)de) != 0) {
			PCIE_DBG("pciev_update_domain_prop: "
			    "mod_hash_insert failed\n");
			kmem_free(de, sizeof (pcie_domain_entry_t));
			ret = 1;
		} else if (prop & PCIE_DOM_PROP_FMA) {
			changed = 1;
		}
	}
	rw_exit(&dh->dh_lock);

	/*
	 * Refresh the cached domain info under the Root node
	 * when and only when a domain changes between fma
	 * capable and non-fma capable domain. Other domain
	 * property changes won't need this.
	 */
	if (changed == 1) {
		pciev_reset_tree(devi, domain_id, prop);
	}

	return (ret);
}

/* Refresh the cached domain info under the Root node */
static void
pciev_reset_tree(dev_info_t *pdip, dom_id_t domain_id, uint_t prop)
{
	dev_info_t *dip;
	pcie_bus_t *bus_p;
	dom_id_t myid;

	/* Check validity of domain_id */
	if (domain_id == PCIE_UNKNOWN_DOMAIN_ID)
		return;

	for (dip = ddi_get_child(pdip); dip; dip = ddi_get_next_sibling(dip)) {
		bus_p = PCIE_DIP2BUS(dip);
		if (bus_p && !PCIE_IS_BDG(bus_p)) {
			/*
			 *  Check if this leaf device has been loaned
			 *  and update the cached info accordingly.
			 */
			if (!PCIE_IS_ASSIGNED(bus_p))
				continue;

			myid = PCIE_DOMAIN_ID_GET(bus_p);
			if (myid != domain_id)
				continue;

			if (PCIE_ASSIGNED_TO_NFMA_DOM(bus_p) &&
			    (prop & PCIE_DOM_PROP_FMA)) {
				/* First clear the old domain info */
				pcie_uncache_domain_info(bus_p);

				/* Set the new domain info */
				PCIE_BUS2DOM(bus_p)->fmadom_count = 1;
				PCIE_DOMAIN_ID_SET(bus_p, domain_id);
				PCIE_DOMAIN_ID_INCR_REF_COUNT(bus_p);

				/* Cache the new domain info in parents */
				pcie_cache_domain_info(bus_p);
			} else if (PCIE_ASSIGNED_TO_FMA_DOM(bus_p) &&
			    (!(prop & PCIE_DOM_PROP_FMA))) {
				/* First clear the old domain info */
				pcie_uncache_domain_info(bus_p);

				/* Set the new domain info */
				PCIE_BUS2DOM(bus_p)->nfmadom_count = 1;
				PCIE_DOMAIN_ID_SET(bus_p, domain_id);
				PCIE_DOMAIN_ID_INCR_REF_COUNT(bus_p);

				/* Cache the new domain info in parents */
				pcie_cache_domain_info(bus_p);
			}
		} else if (bus_p && PCIE_IS_BDG(bus_p)) {
			pciev_reset_tree(dip, domain_id, prop);
		}
	}
}

pcie_req_id_t
pciev_get_leaf(dev_info_t *pdip, dom_id_t domain_id)
{
	dev_info_t *dip;
	pcie_bus_t *bus_p;
	pcie_req_id_t bdf;

	for (dip = ddi_get_child(pdip); dip; dip = ddi_get_next_sibling(dip)) {
		bus_p = PCIE_DIP2BUS(dip);
		if (bus_p && !PCIE_IS_BDG(bus_p)) {
			if (!PCIE_IS_ASSIGNED(bus_p))
				continue;

			if (domain_id == PCIE_DOMAIN_ID_GET(bus_p))
				return (bus_p->bus_bdf);
		} else if (bus_p && PCIE_IS_BDG(bus_p)) {
			bdf = pciev_get_leaf(dip, domain_id);
			if (PCIE_CHECK_VALID_BDF(bdf))
				return (bdf);
		}
	}

	return (PCIE_INVALID_BDF);
}

static void
pciev_update_nfma_domid(dev_info_t *root_dip, dom_id_t domain_id)
{
	pcie_domain_t *dom_p = PCIE_DIP2DOM(root_dip);

	/* Mark domain as nfma and need to check whether to panic */
	(void) pciev_update_domain_prop(root_dip, domain_id,
	    PCIE_DOM_PROP_FB);

	dom_p->nfma_domid_cnt++;
}

static int
pcie_set_assigned(dev_info_t *dip, void *arg)
{
	dom_id_t peer_domain_id = *(dom_id_t *)arg;
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);
	pcie_domain_t	*dom_p = NULL;

	if (bus_p && PCIE_IS_PCIE_LEAF(bus_p)) {
		dom_p = PCIE_BUS2DOM(bus_p);
		ASSERT(dom_p);

		pcie_uncache_domain_info(bus_p);
		dom_p->nfmadom_count = 1;
		PCIE_DOMAIN_ID_SET(bus_p, peer_domain_id);
		PCIE_DOMAIN_ID_INCR_REF_COUNT(bus_p);
		pcie_cache_domain_info(bus_p);

		return (DDI_WALK_PRUNECHILD);
	}
	return (DDI_WALK_CONTINUE);
}

static int
pcie_unset_assigned(dev_info_t *dip, void *arg)
{
	dom_id_t peer_domain_id = *(dom_id_t *)arg;
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);
	pcie_dom_hash_t *dh;

	PCIE_DBG("pcie_unset_assigned: dip %p %"PRIu64"\n",
	    dip, peer_domain_id);

	/* update domain property to NFMA in the hash table */
	if (bus_p && PCIE_IS_ROOT(bus_p)) {
		dh = PCIE_BUS2DOM(bus_p)->dom_hashp;
		ASSERT(dh);
		pciev_set_domain_prop(dh, peer_domain_id, 0);
		return (DDI_WALK_CONTINUE);
	}

	/* update cached info */
	if (bus_p && PCIE_IS_PCIE_LEAF(bus_p) &&
	    (PCIE_DOMAIN_ID_GET(bus_p) == peer_domain_id)) {

		pcie_uncache_domain_info(bus_p);
		PCIE_BUS2DOM(bus_p)->nfmadom_count = 1;
		pcie_cache_domain_info(bus_p);

		return (DDI_WALK_PRUNECHILD);
	}
	return (DDI_WALK_CONTINUE);
}

/*
 * As all of devices on the virtual fabric must be assigned
 * from one IO Domain which owns the physical fabric, we just
 * cache domain id handle on the px driver's bus structure.
 */
static int
pcie_virtual_fab_domid_cache(dev_info_t *dip, dom_id_t peer_domain_id)
{
	dev_info_t	*rcdip = pcie_get_rc_dip(dip);
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(rcdip);
	pcie_domain_t	*dom_p = NULL;

	ASSERT(bus_p != NULL);

	dom_p = PCIE_BUS2DOM(bus_p);
	ASSERT(dom_p != NULL);

	PCIE_RC_DOMAIN_ID_SET(bus_p, peer_domain_id);

	return (DDI_SUCCESS);
}

/*
 * Walk physical fabric to cache the domain id handle.
 */
static int
pcie_physical_fab_domid_cache(dev_info_t *dip, dom_id_t peer_domain_id)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);

	ASSERT(bus_p != NULL);

	/* If passed a switch port, then all children devices are assigned */
	if (PCIE_IS_SWD(bus_p)) {
		int circular_count;
		ndi_devi_enter(dip, &circular_count);
		ddi_walk_devs(ddi_get_child(dip), pcie_set_assigned,
		    (void *)&peer_domain_id);
		ndi_devi_exit(dip, circular_count);
	} else if (PCIE_IS_PCIE_LEAF(bus_p)) {
		(void) pcie_set_assigned(dip, (void *)&peer_domain_id);
	} else {
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

void
pcie_scan_assigned_devs(dev_info_t *rdip)
{
	pcie_bus_t		*bus_p = PCIE_DIP2BUS(rdip);
	boolean_t		phys_fabric, vf_only;
	char			path[MAXPATHLEN];
	int			path_len;
	pciv_assigned_dev_t	*devc = pciv_dev_cache;
	dev_info_t		*dip;
	int			rv;

	ASSERT(bus_p != NULL);

	phys_fabric = pcie_is_physical_fabric(rdip);

	vf_only = PCIE_IS_PF(bus_p) ? B_TRUE : B_FALSE;
	if (vf_only) {
		/* Use PF's parent path for VF only scan */
		ASSERT(PCIE_IS_PF(bus_p));
		(void) pcie_pathname(ddi_get_parent(rdip), path);
	} else {
		/* Use RC path for the full scan, the rdip must be a RC dip */
		ASSERT(PCIE_IS_RC(bus_p));
		(void) pcie_pathname(rdip, path);
	}

	path_len = strlen(path);
	PCIE_DBG("pcie_scan_assigned_devs: scanning devices under path %s\n",
	    path);

	mutex_enter(&pciv_devcache_mutex);
	while (devc != NULL) {
		if (!devc->marked &&
		    (strncmp(path, devc->devpath, path_len) == 0)) {

			/* Skip physical functions for VF only scan */
			if (vf_only && devc->type != FUNC_TYPE_VF)
				goto next_dev;

			if ((dip = pcie_find_dip_by_unit_addr(devc->devpath))
			    == NULL) {
				/*
				 * Not an issue, if VF's device node is not
				 * created on the physical fbaric. On virtual
				 * fbaric, VF's dip should be always ready.
				 */
				if (devc->type != FUNC_TYPE_VF || !phys_fabric)
					cmn_err(CE_WARN,
					    "pcie_scan_assigned_devs: "
					    "invalid device path %s\n",
					    devc->devpath);
				goto next_dev;
			}

			rv = phys_fabric ?
			    pcie_physical_fab_domid_cache(dip, devc->domain_id):
			    pcie_virtual_fab_domid_cache(dip, devc->domain_id);
			if (rv != DDI_SUCCESS)
				PCIE_DBG("pcie_assign_device: invalid device "
				    "path %s\n", devc->devpath);
			else {
				devc->marked = B_TRUE;
				devc->dip = dip;
			}
		}
next_dev:
		devc = devc->next;
	}
	mutex_exit(&pciv_devcache_mutex);
}

static pciv_assigned_dev_t *
find_assigned_device(char *devpath)
{
	pciv_assigned_dev_t *devc = pciv_dev_cache;

	ASSERT(MUTEX_HELD(&pciv_devcache_mutex));

	while (devc != NULL) {
		if (strcmp(devc->devpath, devpath) == NULL)
			return (devc);
		devc = devc->next;
	}
	return (NULL);
}

static int
add_assigned_device(char *devpath, pcie_fn_type_t type, dom_id_t domain_id,
    dev_info_t *dip, pciv_assigned_dev_t **devcp)
{
	pciv_assigned_dev_t *devc;
	int rv;

	ASSERT(MUTEX_HELD(&pciv_devcache_mutex));

	devc = find_assigned_device(devpath);
	if (devc == NULL) {
		int pathlen;
		/* not already in the cache */
		devc = kmem_zalloc(sizeof (pciv_assigned_dev_t), KM_SLEEP);
		devc->next = pciv_dev_cache;
		devc->domain_id = domain_id;
		devc->marked = B_FALSE;
		devc->dip = dip;
		pathlen = strlen(devpath) + 1;
		devc->devpath = kmem_alloc(pathlen, KM_SLEEP);
		(void) strlcpy(devc->devpath, devpath, pathlen);
		devc->type = type;
		pciv_dev_cache = devc;
		PCIE_DBG("add_assigned_device: added dev %s "
		    "dom %"PRIu64"\n", devc->devpath, devc->domain_id);
		rv = PCIV_DEVC_NEW;
	} else {
		if (devc->domain_id != domain_id ||
		    devc->type != type ||
		    devc->dip != dip) {
			devc->domain_id = domain_id;
			devc->type = type;
			devc->marked = B_FALSE;
			devc->dip = dip;
			rv = PCIV_DEVC_UPDATE;
		} else
			rv = PCIV_DEVC_NOCHANGE;
	}

	if (devcp)
		*devcp = devc;
	return (rv);
}

static void
remove_assigned_device_by_domid(dom_id_t domain_id)
{
	pciv_assigned_dev_t *devc, *prev, *next;

	mutex_enter(&pciv_devcache_mutex);
	devc = pciv_dev_cache;
	prev = NULL;
	while (devc) {
		next = devc->next;
		if (devc->domain_id == domain_id) {
			if (prev)
				prev->next = next;
			else
				pciv_dev_cache = next;
			kmem_free(devc->devpath, strlen(devc->devpath) + 1);
			kmem_free(devc, sizeof (pciv_assigned_dev_t));
		} else
			prev = devc;
		devc = next;
	}
	mutex_exit(&pciv_devcache_mutex);
}


/*
 * On sun4v this is called when devices are bound to an I/O domain
 */
int
pcie_assign_device(char *devpath, pcie_fn_type_t type, dom_id_t domain_id)
{
	dev_info_t	*dip;
	int		rv;
	pciv_assigned_dev_t *devcp;

	PCIE_DBG("pcie_assign_device: path %s dom %"PRIu64"\n", devpath,
	    domain_id);

	dip = pcie_find_dip_by_unit_addr(devpath);
	mutex_enter(&pciv_devcache_mutex);
	if ((rv = add_assigned_device(devpath, type, domain_id, dip, &devcp)) ==
	    PCIV_DEVC_NOCHANGE) {
		mutex_exit(&pciv_devcache_mutex);
		return (DDI_SUCCESS);
	}

	if (dip != NULL && PCIE_DIP2BUS(dip)) {
		rv = pcie_is_physical_fabric(dip) ?
		    pcie_physical_fab_domid_cache(dip, domain_id) :
		    pcie_virtual_fab_domid_cache(dip, domain_id);

		if (rv != DDI_SUCCESS)
			PCIE_DBG("pcie_assign_device: invalid device path %s\n",
			    devpath);
		else
			devcp->marked = B_TRUE;
	} else {
		PCIE_DBG("pcie_assign_device: assigned defered %s\n",
		    devpath);
		rv = DDI_SUCCESS;
	}

	mutex_exit(&pciv_devcache_mutex);
	return (rv);
}

/*
 * Walk the virtual fabric's px nodes to uncache the domain id handle
 */
static void
pcie_virtual_fab_domid_clean(dom_id_t peer_domain_id, dev_info_t *rcdip)
{
	pcie_bus_t	*bus_p = NULL;
	pcie_domain_t	*dom_p = NULL;

	bus_p = PCIE_DIP2BUS(rcdip);
	if (bus_p && PCIE_IS_RC(bus_p)) {
		dom_p = PCIE_BUS2DOM(bus_p);
		ASSERT(dom_p != NULL);
		if (peer_domain_id == PCIE_RC_DOMAIN_ID_GET(bus_p))
			PCIE_RC_DOMAIN_ID_SET(bus_p,
			    PCIE_UNKNOWN_DOMAIN_ID);
	}
}

/*
 * Walk the physical fabric to uncache the domain id handle
 */
static void
pcie_physical_fab_domid_clean(dom_id_t peer_domain_id, dev_info_t *rcdip)
{
	int circular_count;

	ndi_devi_enter(ddi_get_parent(rcdip), &circular_count);
	ddi_walk_devs(rcdip, pcie_unset_assigned,
	    (void *)&peer_domain_id);
	ndi_devi_exit(ddi_get_parent(rcdip), circular_count);
}

/*
 * On sun4v unassign is called when the domain is unbound.
 */
void
pcie_unassign_devices(dom_id_t peer_domain_id)
{
	dev_info_t *dip;
	pcie_bus_t *bus_p;

	PCIE_DBG("pcie_unassign_device: domain %"PRIu64"\n", peer_domain_id);

	for (dip = ddi_get_child(ddi_root_node()); dip;
	    dip = ddi_get_next_sibling(dip)) {
		bus_p = PCIE_DIP2BUS(dip);
		if (!bus_p)	/* not PCIE fabric */
			continue;
		if (!pcie_is_physical_fabric(dip))
			pcie_virtual_fab_domid_clean(peer_domain_id, dip);
		else
			pcie_physical_fab_domid_clean(peer_domain_id, dip);
	}

	remove_assigned_device_by_domid(peer_domain_id);
}

/*
 * Get a domain id for a specific device.
 *
 * return values
 * >0				Domain id
 * PCIE_UNKNOWN_DOMAIN_ID	Domain id is unknown or device is not assigned
 */
dom_id_t
pcie_get_domain_id(dev_info_t *dip)
{
	pcie_bus_t *bus_p = NULL;

	if (pcie_is_physical_fabric(dip)) {
		bus_p = PCIE_DIP2BUS(dip);
		ASSERT(bus_p != NULL);
		return ((bus_p) ? PCIE_DOMAIN_ID_GET(bus_p):
		    PCIE_UNKNOWN_DOMAIN_ID);
	} else {
		bus_p = PCIE_DIP2BUS(pcie_get_rc_dip(dip));
		return ((bus_p) ? PCIE_RC_DOMAIN_ID_GET(bus_p):
		    PCIE_UNKNOWN_DOMAIN_ID);
	}
}

/*
 * Get the assigned device properties for a specific device path.
 */
int
pcie_get_assigned_dev_props_by_devpath(char *devpath, pcie_fn_type_t *type,
    dom_id_t *domain_id)
{
	pciv_assigned_dev_t *devc = pciv_dev_cache;

	mutex_enter(&pciv_devcache_mutex);
	devc = find_assigned_device(devpath);
	mutex_exit(&pciv_devcache_mutex);

	if (devc == NULL)
		return (DDI_FAILURE);

	*type = devc->type;
	*domain_id = devc->domain_id;

	return (DDI_SUCCESS);
}

/*
 * Check if the fabric is physical by dip, it could be called
 * before the RC's pcie_bus_t is initialized.
 */
boolean_t
pcie_is_physical_fabric(dev_info_t *dip)
{
	dev_info_t *rcdip;
	pcie_bus_t *bus_p;

	rcdip = pcie_get_rc_dip(dip);
	ASSERT(rcdip != NULL);

	bus_p = PCIE_DIP2BUS(rcdip);
	if (bus_p) {
		/* The fabric type has been cached */
		return (bus_p->virtual_fabric ? B_FALSE : B_TRUE);
	}

	/*
	 * The fabric type not cached, check if "virtual-root-complex"
	 * property exists. To be compatible with SDIO firmware, check
	 * "pci-intx-not-supported" property too.
	 */
	if (ddi_prop_exists(DDI_DEV_T_ANY, rcdip, DDI_PROP_DONTPASS,
	    "virtual-root-complex") ||
	    ddi_prop_exists(DDI_DEV_T_ANY, rcdip, DDI_PROP_DONTPASS,
	    "pci-intx-not-supported"))
		return (B_FALSE);
	else
		return (B_TRUE);
}
