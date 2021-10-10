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
 *  Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * mcxnex_misc.c
 *    Mcxnex Miscellaneous routines - Address Handle, Multicast, Protection
 *    Domain, and port-related operations
 *
 *    Implements all the routines necessary for allocating, freeing, querying
 *    and modifying Address Handles and Protection Domains.  Also implements
 *    all the routines necessary for adding and removing Queue Pairs to/from
 *    Multicast Groups.  Lastly, it implements the routines necessary for
 *    port-related query and modify operations.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/bitmap.h>
#include <sys/sysmacros.h>

#include "mcxnex.h"

extern uint32_t mcxnex_kernel_data_ro;

/* used for helping uniquify fmr pool taskq name */
static uint_t mcxnex_debug_fmrpool_cnt = 0x00000000;

static int mcxnex_mcg_qplist_add(mcxnex_state_t *state, mcxnex_mcghdl_t mcg,
    mcxnex_hw_mcg_qp_list_t *mcg_qplist, mcxnex_qphdl_t qp, uint_t *qp_found);
static int mcxnex_mcg_qplist_remove(mcxnex_mcghdl_t mcg,
    mcxnex_hw_mcg_qp_list_t *mcg_qplist, mcxnex_qphdl_t qp);
static void mcxnex_qp_mcg_refcnt_inc(mcxnex_qphdl_t qp);
static void mcxnex_qp_mcg_refcnt_dec(mcxnex_qphdl_t qp);
static uint_t mcxnex_mcg_walk_mgid_hash(mcxnex_state_t *state,
    uint64_t start_indx, ib_gid_t mgid, uint_t *prev_indx);
static void mcxnex_mcg_setup_new_hdr(mcxnex_mcghdl_t mcg,
    mcxnex_hw_mcg_t *mcg_hdr, ib_gid_t mgid, mcxnex_rsrc_t *mcg_rsrc);
static int mcxnex_mcg_hash_list_remove(mcxnex_state_t *state, uint_t curr_indx,
    uint_t prev_indx, mcxnex_hw_mcg_t *mcg_entry);
static int mcxnex_mcg_entry_invalidate(mcxnex_state_t *state,
    mcxnex_hw_mcg_t *mcg_entry, uint_t indx);
static int mcxnex_mgid_is_valid(ib_gid_t gid);
static int mcxnex_mlid_is_valid(ib_lid_t lid);
static void mcxnex_fmr_processing(void *fmr_args);
static int mcxnex_fmr_cleanup(mcxnex_state_t *state, mcxnex_fmrhdl_t pool);
static void mcxnex_fmr_cache_init(mcxnex_fmrhdl_t fmr);
static void mcxnex_fmr_cache_fini(mcxnex_fmrhdl_t fmr);
static int mcxnex_fmr_avl_compare(const void *q, const void *e);


#define	MCXNEX_MAX_DBR_PAGES_PER_USER	64
#define	MCXNEX_DBR_KEY(index, page) \
	(((uint64_t)index) * MCXNEX_MAX_DBR_PAGES_PER_USER + (page))

static mcxnex_udbr_page_t *
mcxnex_dbr_new_user_page(mcxnex_state_t *state, uint_t index,
    uint_t page)
{
	mcxnex_udbr_page_t *pagep;
	ddi_dma_attr_t dma_attr;
	uint_t cookiecnt;
	int status;
	mcxnex_umap_db_entry_t *umapdb;

	pagep = kmem_alloc(sizeof (*pagep), KM_SLEEP);
	pagep->upg_index = page;
	pagep->upg_nfree = PAGESIZE / sizeof (mcxnex_dbr_t);

	/* Allocate 1 bit per dbr for free/alloc management (0 => "free") */
	pagep->upg_free = kmem_zalloc(PAGESIZE / sizeof (mcxnex_dbr_t) / 8,
	    KM_SLEEP);
	pagep->upg_kvaddr = ddi_umem_alloc(PAGESIZE, DDI_UMEM_SLEEP,
	    &pagep->upg_umemcookie); /* not MCXNEX_PAGESIZE here */

	pagep->upg_buf = ddi_umem_iosetup(pagep->upg_umemcookie, 0,
	    PAGESIZE, B_WRITE, 0, 0, NULL, DDI_UMEM_SLEEP);

	mcxnex_dma_attr_init(state, &dma_attr);
	status = ddi_dma_alloc_handle(state->hs_dip, &dma_attr,
	    DDI_DMA_SLEEP, NULL, &pagep->upg_dmahdl);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "ddi_dma_alloc_handle failed: %d", status);
		return (NULL);
	}
	status = ddi_dma_buf_bind_handle(pagep->upg_dmahdl,
	    pagep->upg_buf, DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    DDI_DMA_SLEEP, NULL, &pagep->upg_dmacookie, &cookiecnt);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "ddi_dma_buf_bind_handle failed: %d", status);
		ddi_dma_free_handle(&pagep->upg_dmahdl);
		return (NULL);
	}
	ASSERT(cookiecnt == 1);

	/* create db entry for mmap */
	umapdb = mcxnex_umap_db_alloc(state->hs_instance,
	    MCXNEX_DBR_KEY(index, page), MLNX_UMAP_DBRMEM_RSRC,
	    (uint64_t)(uintptr_t)pagep);
	mcxnex_umap_db_add(umapdb);
	return (pagep);
}


/*ARGSUSED*/
static int
mcxnex_user_dbr_alloc(mcxnex_state_t *state, uint_t index,
    ddi_acc_handle_t *acchdl, mcxnex_dbr_t **vdbr, uint64_t *pdbr,
    uint64_t *mapoffset)
{
	mcxnex_user_dbr_t *udbr;
	mcxnex_udbr_page_t *pagep;
	uint_t next_page;
	int dbr_index;
	int i1, i2, i3, last;
	uint64_t u64, mask;

	mutex_enter(&state->hs_dbr_lock);
	for (udbr = state->hs_user_dbr; udbr != NULL; udbr = udbr->udbr_link)
		if (udbr->udbr_index == index)
			break;
	if (udbr == NULL) {
		udbr = kmem_alloc(sizeof (*udbr), KM_SLEEP);
		udbr->udbr_link = state->hs_user_dbr;
		state->hs_user_dbr = udbr;
		udbr->udbr_index = index;
		udbr->udbr_pagep = NULL;
	}
	pagep = udbr->udbr_pagep;
	next_page = (pagep == NULL) ? 0 : (pagep->upg_index + 1);
	while (pagep != NULL)
		if (pagep->upg_nfree > 0)
			break;
		else
			pagep = pagep->upg_link;
	if (pagep == NULL) {
		pagep = mcxnex_dbr_new_user_page(state, index, next_page);
		if (pagep == NULL) {
			mutex_exit(&state->hs_dbr_lock);
			return (DDI_FAILURE);
		}
		pagep->upg_link = udbr->udbr_pagep;
		udbr->udbr_pagep = pagep;
	}

	/* Since nfree > 0, we're assured the loops below will succeed */

	/* First, find a 64-bit (not ~0) that has a free dbr */
	last = PAGESIZE / sizeof (uint64_t) / 64;
	mask = ~0ull;
	for (i1 = 0; i1 < last; i1++)
		if ((pagep->upg_free[i1] & mask) != mask)
			break;
	u64 = pagep->upg_free[i1];

	/* Second, find a byte (not 0xff) that has a free dbr */
	last = sizeof (uint64_t) / sizeof (uint8_t);
	for (i2 = 0, mask = 0xff; i2 < last; i2++, mask <<= 8)
		if ((u64 & mask) != mask)
			break;

	/* Third, find a bit that is free (0) */
	for (i3 = 0; i3 < sizeof (uint64_t) / sizeof (uint8_t); i3++)
		if ((u64 & (1ul << (i3 + 8 * i2))) == 0)
			break;

	/* Mark it as allocated */
	pagep->upg_free[i1] |= (1ul << (i3 + 8 * i2));

	dbr_index = ((i1 * sizeof (uint64_t)) + i2) * sizeof (uint64_t) + i3;
	pagep->upg_nfree--;
	((uint64_t *)(void *)pagep->upg_kvaddr)[dbr_index] = 0;	/* clear dbr */
	*mapoffset = ((MCXNEX_DBR_KEY(index, pagep->upg_index) <<
	    MLNX_UMAP_RSRC_TYPE_SHIFT) | MLNX_UMAP_DBRMEM_RSRC) << PAGESHIFT;
	*vdbr = (mcxnex_dbr_t *)((uint64_t *)(void *)pagep->upg_kvaddr +
	    dbr_index);
	*pdbr = pagep->upg_dmacookie.dmac_laddress + dbr_index *
	    sizeof (uint64_t);

	mutex_exit(&state->hs_dbr_lock);
	return (DDI_SUCCESS);
}

static void
mcxnex_user_dbr_free(mcxnex_state_t *state, uint_t index, mcxnex_dbr_t *record)
{
	mcxnex_user_dbr_t	*udbr;
	mcxnex_udbr_page_t	*pagep;
	caddr_t			kvaddr;
	uint_t			dbr_index;
	uint_t			max_free = PAGESIZE / sizeof (mcxnex_dbr_t);
	int			i1, i2;

	dbr_index = (uintptr_t)record & PAGEOFFSET; /* offset (not yet index) */
	kvaddr = (caddr_t)record - dbr_index;
	dbr_index /= sizeof (mcxnex_dbr_t); /* now it's the index */

	mutex_enter(&state->hs_dbr_lock);
	for (udbr = state->hs_user_dbr; udbr != NULL; udbr = udbr->udbr_link)
		if (udbr->udbr_index == index)
			break;
	if (udbr == NULL) {
		cmn_err(CE_CONT, "?free user dbr: udbr struct not "
		    "found for index %x\n", index);
		mutex_exit(&state->hs_dbr_lock);
		return;
	}
	for (pagep = udbr->udbr_pagep; pagep != NULL; pagep = pagep->upg_link)
		if (pagep->upg_kvaddr == kvaddr)
			break;
	if (pagep == NULL) {
		cmn_err(CE_CONT, "?free user dbr: pagep struct not"
		    " found for index %x, kvaddr %p, DBR index %x\n",
		    index, (void *)kvaddr, dbr_index);
		mutex_exit(&state->hs_dbr_lock);
		return;
	}
	if (pagep->upg_nfree >= max_free) {
		cmn_err(CE_CONT, "?free user dbr: overflow: "
		    "UCE index %x, DBR index %x\n", index, dbr_index);
		mutex_exit(&state->hs_dbr_lock);
		return;
	}
	ASSERT(dbr_index < max_free);
	i1 = dbr_index / 64;
	i2 = dbr_index % 64;
	ASSERT((pagep->upg_free[i1] & (1ul << i2)) == (1ul << i2));
	pagep->upg_free[i1] &= ~(1ul << i2);
	pagep->upg_nfree++;
	mutex_exit(&state->hs_dbr_lock);
}

/*
 * mcxnex_dbr_page_alloc()
 *	first page allocation - called from attach or open
 *	in this case, we want exactly one page per call, and aligned on a
 *	page - and may need to be mapped to the user for access
 */
int
mcxnex_dbr_page_alloc(mcxnex_state_t *state, mcxnex_dbr_info_t **dinfo)
{
	int			status;
	ddi_dma_handle_t	dma_hdl;
	ddi_acc_handle_t	acc_hdl;
	ddi_dma_attr_t		dma_attr;
	ddi_dma_cookie_t	cookie;
	uint_t			cookie_cnt;
	int			i;
	mcxnex_dbr_info_t 	*info;
	caddr_t			dmaaddr;
	uint64_t		dmalen;

	info = kmem_zalloc(sizeof (mcxnex_dbr_info_t), KM_SLEEP);

	/*
	 * Initialize many of the default DMA attributes.  Then set additional
	 * alignment restrictions if necessary for the dbr memory, meaning
	 * page aligned.  Also use the configured value for IOMMU bypass
	 */
	mcxnex_dma_attr_init(state, &dma_attr);
	dma_attr.dma_attr_align = PAGESIZE;
	dma_attr.dma_attr_sgllen = 1;	/* make sure only one cookie */

	status = ddi_dma_alloc_handle(state->hs_dip, &dma_attr,
	    DDI_DMA_SLEEP, NULL, &dma_hdl);
	if (status != DDI_SUCCESS) {
		kmem_free((void *)info, sizeof (mcxnex_dbr_info_t));
		cmn_err(CE_NOTE, "dbr DMA handle alloc failed\n");
		return (DDI_FAILURE);
	}

	status = ddi_dma_mem_alloc(dma_hdl, PAGESIZE,
	    &state->hs_reg_accattr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &dmaaddr, (size_t *)&dmalen, &acc_hdl);
	if (status != DDI_SUCCESS)	{
		ddi_dma_free_handle(&dma_hdl);
		cmn_err(CE_CONT, "dbr DMA mem alloc failed(status %d)", status);
		kmem_free((void *)info, sizeof (mcxnex_dbr_info_t));
		return (DDI_FAILURE);
	}

	/* this memory won't be IB registered, so do the bind here */
	status = ddi_dma_addr_bind_handle(dma_hdl, NULL,
	    dmaaddr, (size_t)dmalen, DDI_DMA_RDWR |
	    DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL, &cookie, &cookie_cnt);
	if (status != DDI_SUCCESS) {
		ddi_dma_mem_free(&acc_hdl);
		ddi_dma_free_handle(&dma_hdl);
		kmem_free((void *)info, sizeof (mcxnex_dbr_info_t));
		cmn_err(CE_CONT, "dbr DMA bind handle failed (status %d)",
		    status);
		return (DDI_FAILURE);
	}
	*dinfo = info;		/* Pass back the pointer */

	/* init the info structure with returned info */
	info->dbr_dmahdl = dma_hdl;
	info->dbr_acchdl = acc_hdl;
	info->dbr_page   = (mcxnex_dbr_t *)(void *)dmaaddr;
	info->dbr_link = NULL;
	/* extract the phys addr from the cookie */
	info->dbr_paddr = cookie.dmac_laddress;
	info->dbr_firstfree = 0;
	info->dbr_nfree = MCXNEX_NUM_DBR_PER_PAGE;
	/* link all DBrs onto the free list */
	for (i = 0; i < MCXNEX_NUM_DBR_PER_PAGE; i++) {
		info->dbr_page[i] = i + 1;
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_dbr_alloc()
 *	DBr record allocation - called from alloc cq/qp/srq
 *	will check for available dbrs in current
 *	page - if needed it will allocate another and link them
 */

int
mcxnex_dbr_alloc(mcxnex_state_t *state, uint_t index, ddi_acc_handle_t *acchdl,
    mcxnex_dbr_t **vdbr, uint64_t *pdbr, uint64_t *mapoffset)
{
	mcxnex_dbr_t		*record = NULL;
	mcxnex_dbr_info_t	*info = NULL;
	uint32_t		idx;
	int			status;

	if (index != state->hs_kernel_uar_index)
		return (mcxnex_user_dbr_alloc(state, index, acchdl, vdbr, pdbr,
		    mapoffset));

	mutex_enter(&state->hs_dbr_lock);
	for (info = state->hs_kern_dbr; info != NULL; info = info->dbr_link)
		if (info->dbr_nfree != 0)
			break;		/* found a page w/ one available */

	if (info == NULL) {	/* did NOT find a page with one available */
		status = mcxnex_dbr_page_alloc(state, &info);
		if (status != DDI_SUCCESS) {
			/* do error handling */
			mutex_exit(&state->hs_dbr_lock);
			return (DDI_FAILURE);
		}
		/* got a new page, so link it in. */
		info->dbr_link = state->hs_kern_dbr;
		state->hs_kern_dbr = info;
	}
	idx = info->dbr_firstfree;
	record = info->dbr_page + idx;
	info->dbr_firstfree = *record;
	info->dbr_nfree--;
	*record = 0;

	*acchdl = info->dbr_acchdl;
	*vdbr = record;
	*pdbr = info->dbr_paddr + idx * sizeof (mcxnex_dbr_t);
	mutex_exit(&state->hs_dbr_lock);
	return (DDI_SUCCESS);
}

/*
 * mcxnex_dbr_free()
 *	DBr record deallocation - called from free cq/qp
 *	will update the counter in the header, and invalidate
 *	the dbr, but will NEVER free pages of dbrs - small
 *	price to pay, but userland access never will anyway
 */
void
mcxnex_dbr_free(mcxnex_state_t *state, uint_t indx, mcxnex_dbr_t *record)
{
	mcxnex_dbr_t		*page;
	mcxnex_dbr_info_t	*info;

	if (indx != state->hs_kernel_uar_index) {
		mcxnex_user_dbr_free(state, indx, record);
		return;
	}
	page = (mcxnex_dbr_t *)(uintptr_t)((uintptr_t)record & PAGEMASK);
	mutex_enter(&state->hs_dbr_lock);
	for (info = state->hs_kern_dbr; info != NULL; info = info->dbr_link)
		if (info->dbr_page == page)
			break;
	ASSERT(info != NULL);
	*record = info->dbr_firstfree;
	info->dbr_firstfree = record - info->dbr_page;
	info->dbr_nfree++;
	mutex_exit(&state->hs_dbr_lock);
}

/*
 * mcxnex_dbr_kern_free()
 *    Context: Can be called only from detach context.
 *
 *	Free all kernel dbr pages.  This includes the freeing of all the dma
 *	resources acquired during the allocation of the pages.
 *
 *	Also, free all the user dbr pages.
 */
void
mcxnex_dbr_kern_free(mcxnex_state_t *state)
{
	mcxnex_dbr_info_t	*info, *link;
	mcxnex_user_dbr_t	*udbr, *next;
	mcxnex_udbr_page_t	*pagep, *nextp;
	mcxnex_umap_db_entry_t	*umapdb;
	int			instance, status;
	uint64_t		value;
	extern			mcxnex_umap_db_t mcxnex_userland_rsrc_db;

	mutex_enter(&state->hs_dbr_lock);
	for (info = state->hs_kern_dbr; info != NULL; info = link) {
		(void) ddi_dma_unbind_handle(info->dbr_dmahdl);
		ddi_dma_mem_free(&info->dbr_acchdl);	/* free page */
		ddi_dma_free_handle(&info->dbr_dmahdl);
		link = info->dbr_link;
		kmem_free(info, sizeof (mcxnex_dbr_info_t));
	}

	udbr = state->hs_user_dbr;
	instance = state->hs_instance;
	mutex_enter(&mcxnex_userland_rsrc_db.hdl_umapdb_lock);
	while (udbr != NULL) {
		pagep = udbr->udbr_pagep;
		while (pagep != NULL) {
			/* probably need to remove "db" */
			(void) ddi_dma_unbind_handle(pagep->upg_dmahdl);
			ddi_dma_free_handle(&pagep->upg_dmahdl);
			freerbuf(pagep->upg_buf);
			ddi_umem_free(pagep->upg_umemcookie);
			status = mcxnex_umap_db_find_nolock(instance,
			    MCXNEX_DBR_KEY(udbr->udbr_index,
			    pagep->upg_index), MLNX_UMAP_DBRMEM_RSRC,
			    &value, MCXNEX_UMAP_DB_REMOVE, &umapdb);
			if (status == DDI_SUCCESS)
				mcxnex_umap_db_free(umapdb);
			kmem_free(pagep->upg_free,
			    PAGESIZE / sizeof (mcxnex_dbr_t) / 8);
			nextp = pagep->upg_link;
			kmem_free(pagep, sizeof (*pagep));
			pagep = nextp;
		}
		next = udbr->udbr_link;
		kmem_free(udbr, sizeof (*udbr));
		udbr = next;
	}
	mutex_exit(&mcxnex_userland_rsrc_db.hdl_umapdb_lock);
	mutex_exit(&state->hs_dbr_lock);
}

/*
 * mcxnex_ah_alloc()
 *    Context: Can be called only from user or kernel context.
 */
int
mcxnex_ah_alloc(mcxnex_state_t *state, mcxnex_pdhdl_t pd,
    ibt_adds_vect_t *attr_p, mcxnex_ahhdl_t *ahhdl, uint_t sleepflag)
{
	mcxnex_rsrc_t		*rsrc;
	mcxnex_hw_udav_t	*udav;
	mcxnex_ahhdl_t		ah;
	int			status;

	/*
	 * Someday maybe the "ibt_adds_vect_t *attr_p" will be NULL to
	 * indicate that we wish to allocate an "invalid" (i.e. empty)
	 * address handle XXX
	 */

	/* Validate that specified port number is legal */
	if (!mcxnex_portnum_is_valid(state, attr_p->av_port_num)) {
		return (IBT_HCA_PORT_INVALID);
	}

	/*
	 * Allocate the software structure for tracking the address handle
	 * (i.e. the Mcxnex Address Handle struct).
	 */
	status = mcxnex_rsrc_alloc(state, MCXNEX_AHHDL, 1, sleepflag, &rsrc);
	if (status != DDI_SUCCESS) {
		return (IBT_INSUFF_RESOURCE);
	}
	ah = (mcxnex_ahhdl_t)rsrc->hr_addr;
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*ah))

	/* Increment the reference count on the protection domain (PD) */
	mcxnex_pd_refcnt_inc(pd);

	udav = (mcxnex_hw_udav_t *)kmem_zalloc(sizeof (mcxnex_hw_udav_t),
	    KM_SLEEP);
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*udav))

	/*
	 * Fill in the UDAV data. We first zero out the UDAV, then populate
	 * it by then calling mcxnex_set_addr_path() to fill in the common
	 * portions that can be pulled from the "ibt_adds_vect_t" passed in
	 */
	status = mcxnex_set_addr_path(state, attr_p,
	    (mcxnex_hw_addr_path_t *)udav, MCXNEX_ADDRPATH_UDAV);
	if (status != DDI_SUCCESS) {
		mcxnex_pd_refcnt_dec(pd);
		mcxnex_rsrc_free(state, &rsrc);
		return (status);
	}
	udav->pd	= pd->pd_pdnum;
	udav->sl	= attr_p->av_srvl;

	/*
	 * Fill in the rest of the Mcxnex Address Handle struct.
	 *
	 * NOTE: We are saving away a copy of the "av_dgid.gid_guid" field
	 * here because we may need to return it later to the IBTF (as a
	 * result of a subsequent query operation).  Unlike the other UDAV
	 * parameters, the value of "av_dgid.gid_guid" is not always preserved.
	 * The reason for this is described in mcxnex_set_addr_path().
	 */
	ah->ah_rsrcp	 = rsrc;
	ah->ah_pdhdl	 = pd;
	ah->ah_udav	 = udav;
	ah->ah_save_guid = attr_p->av_dgid.gid_guid;
	*ahhdl = ah;

	return (DDI_SUCCESS);
}


/*
 * mcxnex_ah_free()
 *    Context: Can be called only from user or kernel context.
 */
/* ARGSUSED */
int
mcxnex_ah_free(mcxnex_state_t *state, mcxnex_ahhdl_t *ahhdl, uint_t sleepflag)
{
	mcxnex_rsrc_t		*rsrc;
	mcxnex_pdhdl_t		pd;
	mcxnex_ahhdl_t		ah;

	/*
	 * Pull all the necessary information from the Mcxnex Address Handle
	 * struct.  This is necessary here because the resource for the
	 * AH is going to be freed up as part of this operation.
	 */
	ah    = *ahhdl;
	mutex_enter(&ah->ah_lock);
	rsrc  = ah->ah_rsrcp;
	pd    = ah->ah_pdhdl;
	mutex_exit(&ah->ah_lock);
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*ah))

	/* Free the UDAV memory */
	kmem_free(ah->ah_udav, sizeof (mcxnex_hw_udav_t));

	/* Decrement the reference count on the protection domain (PD) */
	mcxnex_pd_refcnt_dec(pd);

	/* Free the Mcxnex Address Handle structure */
	mcxnex_rsrc_free(state, &rsrc);

	/* Set the ahhdl pointer to NULL and return success */
	*ahhdl = NULL;

	return (DDI_SUCCESS);
}


/*
 * mcxnex_ah_query()
 *    Context: Can be called from interrupt or base context.
 */
/* ARGSUSED */
int
mcxnex_ah_query(mcxnex_state_t *state, mcxnex_ahhdl_t ah, mcxnex_pdhdl_t *pd,
    ibt_adds_vect_t *attr_p)
{
	mutex_enter(&ah->ah_lock);
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*attr_p))

	/*
	 * Pull the PD and UDAV from the Mcxnex Address Handle structure
	 */
	*pd = ah->ah_pdhdl;

	/*
	 * Fill in "ibt_adds_vect_t".  We call mcxnex_get_addr_path() to fill
	 * the common portions that can be pulled from the UDAV we pass in.
	 *
	 * NOTE: We will also fill the "av_dgid.gid_guid" field from the
	 * "ah_save_guid" field we have previously saved away.  The reason
	 * for this is described in mcxnex_ah_alloc() and mcxnex_ah_modify().
	 */
	mcxnex_get_addr_path(state, (mcxnex_hw_addr_path_t *)ah->ah_udav,
	    attr_p, MCXNEX_ADDRPATH_UDAV);

	attr_p->av_dgid.gid_guid = ah->ah_save_guid;

	mutex_exit(&ah->ah_lock);
	return (DDI_SUCCESS);
}


/*
 * mcxnex_ah_modify()
 *    Context: Can be called from interrupt or base context.
 */
/* ARGSUSED */
int
mcxnex_ah_modify(mcxnex_state_t *state, mcxnex_ahhdl_t ah,
    ibt_adds_vect_t *attr_p)
{
	mcxnex_hw_udav_t	old_udav;
	uint64_t		data_old;
	int			status, size, i;

	/* Validate that specified port number is legal */
	if (!mcxnex_portnum_is_valid(state, attr_p->av_port_num)) {
		return (IBT_HCA_PORT_INVALID);
	}

	mutex_enter(&ah->ah_lock);

	/* Save a copy of the current UDAV data in old_udav. */
	bcopy(ah->ah_udav, &old_udav, sizeof (mcxnex_hw_udav_t));

	/*
	 * Fill in the new UDAV with the caller's data, passed in via the
	 * "ibt_adds_vect_t" structure.
	 *
	 * NOTE: We also need to save away a copy of the "av_dgid.gid_guid"
	 * field here (just as we did during mcxnex_ah_alloc()) because we
	 * may need to return it later to the IBTF (as a result of a
	 * subsequent query operation).  As explained in mcxnex_ah_alloc(),
	 * unlike the other UDAV parameters, the value of "av_dgid.gid_guid"
	 * is not always preserved. The reason for this is described in
	 * mcxnex_set_addr_path().
	 */
	status = mcxnex_set_addr_path(state, attr_p,
	    (mcxnex_hw_addr_path_t *)ah->ah_udav, MCXNEX_ADDRPATH_UDAV);
	if (status != DDI_SUCCESS) {
		mutex_exit(&ah->ah_lock);
		return (status);
	}
	ah->ah_save_guid = attr_p->av_dgid.gid_guid;
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*(ah->ah_udav)))
	ah->ah_udav->sl  = attr_p->av_srvl;

	/*
	 * Copy changes into the new UDAV.
	 *    Note:  We copy in 64-bit chunks.  For the first two of these
	 *    chunks it is necessary to read the current contents of the
	 *    UDAV, mask off the modifiable portions (maintaining any
	 *    of the "reserved" portions), and then mask on the new data.
	 */
	size = sizeof (mcxnex_hw_udav_t) >> 3;
	for (i = 0; i < size; i++) {
		data_old = ((uint64_t *)&old_udav)[i];

		/*
		 * Apply mask to change only the relevant values.
		 */
		if (i == 0) {
			data_old = data_old & MCXNEX_UDAV_MODIFY_MASK0;
		} else if (i == 1) {
			data_old = data_old & MCXNEX_UDAV_MODIFY_MASK1;
		} else {
			data_old = 0;
		}

		/* Store the updated values to the UDAV */
		((uint64_t *)ah->ah_udav)[i] |= data_old;
	}

	/*
	 * Put the valid PD number back into the UDAV entry, as it
	 * might have been clobbered above.
	 */
	ah->ah_udav->pd = old_udav.pd;


	mutex_exit(&ah->ah_lock);
	return (DDI_SUCCESS);
}

/*
 * mcxnex_mcg_attach()
 *    Context: Can be called only from user or kernel context.
 */
int
mcxnex_mcg_attach(mcxnex_state_t *state, mcxnex_qphdl_t qp, ib_gid_t gid,
    ib_lid_t lid)
{
	mcxnex_rsrc_t		*rsrc;
	mcxnex_hw_mcg_t		*mcg_entry;
	mcxnex_hw_mcg_qp_list_t	*mcg_entry_qplist;
	mcxnex_mcghdl_t		mcg, newmcg;
	uint64_t		mgid_hash;
	uint32_t		end_indx;
	int			status;
	uint_t			qp_found;

	/*
	 * It is only allowed to attach MCG to UD queue pairs.  Verify
	 * that the intended QP is of the appropriate transport type
	 */
	if (qp->qp_serv_type != MCXNEX_QP_UD) {
		return (IBT_QP_SRV_TYPE_INVALID);
	}

	/*
	 * Check for invalid Multicast DLID.  Specifically, all Multicast
	 * LIDs should be within a well defined range.  If the specified LID
	 * is outside of that range, then return an error.
	 */
	if (mcxnex_mlid_is_valid(lid) == 0) {
		return (IBT_MC_MLID_INVALID);
	}
	/*
	 * Check for invalid Multicast GID.  All Multicast GIDs should have
	 * a well-defined pattern of bits and flags that are allowable.  If
	 * the specified GID does not meet the criteria, then return an error.
	 */
	if (mcxnex_mgid_is_valid(gid) == 0) {
		return (IBT_MC_MGID_INVALID);
	}

	/*
	 * Compute the MGID hash value.  Since the MCG table is arranged as
	 * a number of separate hash chains, this operation converts the
	 * specified MGID into the starting index of an entry in the hash
	 * table (i.e. the index for the start of the appropriate hash chain).
	 * Subsequent operations below will walk the chain searching for the
	 * right place to add this new QP.
	 */
	status = mcxnex_mgid_hash_cmd_post(state, gid.gid_prefix, gid.gid_guid,
	    &mgid_hash, MCXNEX_SLEEPFLAG_FOR_CONTEXT());
	if (status != MCXNEX_CMD_SUCCESS) {
		cmn_err(CE_CONT, "Mcxnex: MGID_HASH command failed: %08x\n",
		    status);
		if (status == MCXNEX_CMD_INVALID_STATUS) {
			mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_SRV_LOST);
		}
		return (DDI_FAILURE);
	}

	/*
	 * Grab the multicast group mutex.  Then grab the pre-allocated
	 * temporary buffer used for holding and/or modifying MCG entries.
	 * Zero out the temporary MCG entry before we begin.
	 */
	mutex_enter(&state->hs_mcglock);
	mcg_entry = state->hs_mcgtmp;
	mcg_entry_qplist = MCXNEX_MCG_GET_QPLIST_PTR(mcg_entry);
	bzero(mcg_entry, MCXNEX_MCGMEM_SZ(state));

	/*
	 * Walk through the array of MCG entries starting at "mgid_hash".
	 * Try to find the appropriate place for this new QP to be added.
	 * This could happen when the first entry of the chain has MGID == 0
	 * (which means that the hash chain is empty), or because we find
	 * an entry with the same MGID (in which case we'll add the QP to
	 * that MCG), or because we come to the end of the chain (in which
	 * case this is the first QP being added to the multicast group that
	 * corresponds to the MGID.  The mcxnex_mcg_walk_mgid_hash() routine
	 * walks the list and returns an index into the MCG table.  The entry
	 * at this index is then checked to determine which case we have
	 * fallen into (see below).  Note:  We are using the "shadow" MCG
	 * list (of mcxnex_mcg_t structs) for this lookup because the real
	 * MCG entries are in hardware (and the lookup process would be much
	 * more time consuming).
	 */
	end_indx = mcxnex_mcg_walk_mgid_hash(state, mgid_hash, gid, NULL);
	mcg	 = &state->hs_mcghdl[end_indx];

	/*
	 * If MGID == 0, then the hash chain is empty.  Just fill in the
	 * current entry.  Note:  No need to allocate an MCG table entry
	 * as all the hash chain "heads" are already preallocated.
	 */
	if ((mcg->mcg_mgid_h == 0) && (mcg->mcg_mgid_l == 0)) {

		/* Fill in the current entry in the "shadow" MCG list */
		mcxnex_mcg_setup_new_hdr(mcg, mcg_entry, gid, NULL);

		/*
		 * Try to add the new QP number to the list.  This (and the
		 * above) routine fills in a temporary MCG.  The "mcg_entry"
		 * and "mcg_entry_qplist" pointers simply point to different
		 * offsets within the same temporary copy of the MCG (for
		 * convenience).  Note:  If this fails, we need to invalidate
		 * the entries we've already put into the "shadow" list entry
		 * above.
		 */
		status = mcxnex_mcg_qplist_add(state, mcg, mcg_entry_qplist, qp,
		    &qp_found);
		if (status != DDI_SUCCESS) {
			bzero(mcg, sizeof (struct mcxnex_sw_mcg_list_s));
			mutex_exit(&state->hs_mcglock);
			return (status);
		}
		if (!qp_found)
			mcg_entry->member_cnt = (mcg->mcg_num_qps + 1);
			    /* set the member count */

		/*
		 * Once the temporary MCG has been filled in, write the entry
		 * into the appropriate location in the Mcxnex MCG entry table.
		 * If it's successful, then drop the lock and return success.
		 * Note: In general, this operation shouldn't fail.  If it
		 * does, then it is an indication that something (probably in
		 * HW, but maybe in SW) has gone seriously wrong.  We still
		 * want to zero out the entries that we've filled in above
		 * (in the mcxnex_mcg_setup_new_hdr() routine).
		 */
		status = mcxnex_write_mgm_cmd_post(state, mcg_entry, end_indx,
		    DDI_NOSLEEP);
		if (status != MCXNEX_CMD_SUCCESS) {
			bzero(mcg, sizeof (struct mcxnex_sw_mcg_list_s));
			mutex_exit(&state->hs_mcglock);
			MCXNEX_WARNING(state, "failed to write MCG entry");
			cmn_err(CE_CONT, "Mcxnex: WRITE_MGM command failed: "
			    "%08x\n", status);
			if (status == MCXNEX_CMD_INVALID_STATUS) {
				mcxnex_fm_ereport(state, HCA_SYS_ERR,
				    HCA_ERR_SRV_LOST);
			}
			return (DDI_FAILURE);
		}

		/*
		 * Now that we know all the Mcxnex firmware accesses have been
		 * successful, we update the "shadow" MCG entry by incrementing
		 * the "number of attached QPs" count.
		 *
		 * We increment only if the QP is not already part of the
		 * MCG by checking the 'qp_found' flag returned from the
		 * qplist_add above.
		 */
		if (!qp_found) {
			mcg->mcg_num_qps++;

			/*
			 * Increment the refcnt for this QP.  Because the QP
			 * was added to this MCG, the refcnt must be
			 * incremented.
			 */
			mcxnex_qp_mcg_refcnt_inc(qp);
		}

		/*
		 * We drop the lock and return success.
		 */
		mutex_exit(&state->hs_mcglock);
		return (DDI_SUCCESS);
	}

	/*
	 * If the specified MGID matches the MGID in the current entry, then
	 * we need to try to add the QP to the current MCG entry.  In this
	 * case, it means that we need to read the existing MCG entry (into
	 * the temporary MCG), add the new QP number to the temporary entry
	 * (using the same method we used above), and write the entry back
	 * to the hardware (same as above).
	 */
	if ((mcg->mcg_mgid_h == gid.gid_prefix) &&
	    (mcg->mcg_mgid_l == gid.gid_guid)) {

		/*
		 * Read the current MCG entry into the temporary MCG.  Note:
		 * In general, this operation shouldn't fail.  If it does,
		 * then it is an indication that something (probably in HW,
		 * but maybe in SW) has gone seriously wrong.
		 */
		status = mcxnex_read_mgm_cmd_post(state, mcg_entry, end_indx,
		    DDI_NOSLEEP);
		if (status != MCXNEX_CMD_SUCCESS) {
			mutex_exit(&state->hs_mcglock);
			MCXNEX_WARNING(state, "failed to read MCG entry");
			cmn_err(CE_CONT, "Mcxnex: READ_MGM command failed: "
			    "%08x\n", status);
			if (status == MCXNEX_CMD_INVALID_STATUS) {
				mcxnex_fm_ereport(state, HCA_SYS_ERR,
				    HCA_ERR_SRV_LOST);
			}
			return (DDI_FAILURE);
		}

		/*
		 * Try to add the new QP number to the list.  This routine
		 * fills in the necessary pieces of the temporary MCG.  The
		 * "mcg_entry_qplist" pointer is used to point to the portion
		 * of the temporary MCG that holds the QP numbers.
		 *
		 * Note: mcxnex_mcg_qplist_add() returns SUCCESS if it
		 * already found the QP in the list.  In this case, the QP is
		 * not added on to the list again.  Check the flag 'qp_found'
		 * if this value is needed to be known.
		 *
		 */
		status = mcxnex_mcg_qplist_add(state, mcg, mcg_entry_qplist, qp,
		    &qp_found);
		if (status != DDI_SUCCESS) {
			mutex_exit(&state->hs_mcglock);
			return (status);
		}
		if (!qp_found)
			mcg_entry->member_cnt = (mcg->mcg_num_qps + 1);
			    /* set the member count */

		/*
		 * Once the temporary MCG has been updated, write the entry
		 * into the appropriate location in the Mcxnex MCG entry table.
		 * If it's successful, then drop the lock and return success.
		 * Note: In general, this operation shouldn't fail.  If it
		 * does, then it is an indication that something (probably in
		 * HW, but maybe in SW) has gone seriously wrong.
		 */
		status = mcxnex_write_mgm_cmd_post(state, mcg_entry, end_indx,
		    DDI_NOSLEEP);
		if (status != MCXNEX_CMD_SUCCESS) {
			mutex_exit(&state->hs_mcglock);
			MCXNEX_WARNING(state, "failed to write MCG entry");
			cmn_err(CE_CONT, "Mcxnex: WRITE_MGM command failed: "
			    "%08x\n", status);
			if (status == MCXNEX_CMD_INVALID_STATUS) {
				mcxnex_fm_ereport(state, HCA_SYS_ERR,
				    HCA_ERR_SRV_LOST);
			}
			return (DDI_FAILURE);
		}

		/*
		 * Now that we know all the Mcxnex firmware accesses have been
		 * successful, we update the current "shadow" MCG entry by
		 * incrementing the "number of attached QPs" count.
		 *
		 * We increment only if the QP is not already part of the
		 * MCG by checking the 'qp_found' flag returned
		 * mcxnex_mcg_walk_mgid_hashfrom the qplist_add above.
		 */
		if (!qp_found) {
			mcg->mcg_num_qps++;

			/*
			 * Increment the refcnt for this QP.  Because the QP
			 * was added to this MCG, the refcnt must be
			 * incremented.
			 */
			mcxnex_qp_mcg_refcnt_inc(qp);
		}

		/*
		 * We drop the lock and return success.
		 */
		mutex_exit(&state->hs_mcglock);
		return (DDI_SUCCESS);
	}

	/*
	 * If we've reached here, then we're at the end of the hash chain.
	 * We need to allocate a new MCG entry, fill it in, write it to Mcxnex,
	 * and update the previous entry to link the new one to the end of the
	 * chain.
	 */

	/*
	 * Allocate an MCG table entry.  This will be filled in with all
	 * the necessary parameters to define the multicast group.  Then it
	 * will be written to the hardware in the next-to-last step below.
	 */
	status = mcxnex_rsrc_alloc(state, MCXNEX_MCG, 1, DDI_NOSLEEP, &rsrc);
	if (status != DDI_SUCCESS) {
		mutex_exit(&state->hs_mcglock);
		return (IBT_INSUFF_RESOURCE);
	}

	/*
	 * Fill in the new entry in the "shadow" MCG list.  Note:  Just as
	 * it does above, mcxnex_mcg_setup_new_hdr() also fills in a portion
	 * of the temporary MCG entry (the rest of which will be filled in by
	 * mcxnex_mcg_qplist_add() below)
	 */
	newmcg = &state->hs_mcghdl[rsrc->hr_indx];
	mcxnex_mcg_setup_new_hdr(newmcg, mcg_entry, gid, rsrc);

	/*
	 * Try to add the new QP number to the list.  This routine fills in
	 * the final necessary pieces of the temporary MCG.  The
	 * "mcg_entry_qplist" pointer is used to point to the portion of the
	 * temporary MCG that holds the QP numbers.  If we fail here, we
	 * must undo the previous resource allocation.
	 *
	 * Note: mcxnex_mcg_qplist_add() can we return SUCCESS if it already
	 * found the QP in the list.  In this case, the QP is not added on to
	 * the list again.  Check the flag 'qp_found' if this value is needed
	 * to be known.
	 */
	status = mcxnex_mcg_qplist_add(state, newmcg, mcg_entry_qplist, qp,
	    &qp_found);
	if (status != DDI_SUCCESS) {
		bzero(newmcg, sizeof (struct mcxnex_sw_mcg_list_s));
		mcxnex_rsrc_free(state, &rsrc);
		mutex_exit(&state->hs_mcglock);
		return (status);
	}
	mcg_entry->member_cnt = (newmcg->mcg_num_qps + 1);
	    /* set the member count */

	/*
	 * Once the temporary MCG has been updated, write the entry into the
	 * appropriate location in the Mcxnex MCG entry table.  If this is
	 * successful, then we need to chain the previous entry to this one.
	 * Note: In general, this operation shouldn't fail.  If it does, then
	 * it is an indication that something (probably in HW, but maybe in
	 * SW) has gone seriously wrong.
	 */
	status = mcxnex_write_mgm_cmd_post(state, mcg_entry, rsrc->hr_indx,
	    DDI_NOSLEEP);
	if (status != MCXNEX_CMD_SUCCESS) {
		bzero(newmcg, sizeof (struct mcxnex_sw_mcg_list_s));
		mcxnex_rsrc_free(state, &rsrc);
		mutex_exit(&state->hs_mcglock);
		MCXNEX_WARNING(state, "failed to write MCG entry");
		cmn_err(CE_CONT, "Mcxnex: WRITE_MGM command failed: %08x\n",
		    status);
		if (status == MCXNEX_CMD_INVALID_STATUS) {
			mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_SRV_LOST);
		}
		return (DDI_FAILURE);
	}

	/*
	 * Now read the current MCG entry (the one previously at the end of
	 * hash chain) into the temporary MCG.  We are going to update its
	 * "next_gid_indx" now and write the entry back to the MCG table.
	 * Note:  In general, this operation shouldn't fail.  If it does, then
	 * it is an indication that something (probably in HW, but maybe in SW)
	 * has gone seriously wrong.  We will free up the MCG entry resource,
	 * but we will not undo the previously written MCG entry in the HW.
	 * This is OK, though, because the MCG entry is not currently attached
	 * to any hash chain.
	 */
	status = mcxnex_read_mgm_cmd_post(state, mcg_entry, end_indx,
	    DDI_NOSLEEP);
	if (status != MCXNEX_CMD_SUCCESS) {
		bzero(newmcg, sizeof (struct mcxnex_sw_mcg_list_s));
		mcxnex_rsrc_free(state, &rsrc);
		mutex_exit(&state->hs_mcglock);
		MCXNEX_WARNING(state, "failed to read MCG entry");
		cmn_err(CE_CONT, "Mcxnex: READ_MGM command failed: %08x\n",
		    status);
		if (status == MCXNEX_CMD_INVALID_STATUS) {
			mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_SRV_LOST);
		}
		return (DDI_FAILURE);
	}

	/*
	 * Finally, we update the "next_gid_indx" field in the temporary MCG
	 * and attempt to write the entry back into the Mcxnex MCG table.  If
	 * this succeeds, then we update the "shadow" list to reflect the
	 * change, drop the lock, and return success.  Note:  In general, this
	 * operation shouldn't fail.  If it does, then it is an indication
	 * that something (probably in HW, but maybe in SW) has gone seriously
	 * wrong.  Just as we do above, we will free up the MCG entry resource,
	 * but we will not try to undo the previously written MCG entry.  This
	 * is OK, though, because (since we failed here to update the end of
	 * the chain) that other entry is not currently attached to any chain.
	 */
	mcg_entry->next_gid_indx = rsrc->hr_indx;
	status = mcxnex_write_mgm_cmd_post(state, mcg_entry, end_indx,
	    DDI_NOSLEEP);
	if (status != MCXNEX_CMD_SUCCESS) {
		bzero(newmcg, sizeof (struct mcxnex_sw_mcg_list_s));
		mcxnex_rsrc_free(state, &rsrc);
		mutex_exit(&state->hs_mcglock);
		MCXNEX_WARNING(state, "failed to write MCG entry");
		cmn_err(CE_CONT, "Mcxnex: WRITE_MGM command failed: %08x\n",
		    status);
		if (status == MCXNEX_CMD_INVALID_STATUS) {
			mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_SRV_LOST);
		}
		return (DDI_FAILURE);
	}
	mcg = &state->hs_mcghdl[end_indx];
	mcg->mcg_next_indx = rsrc->hr_indx;

	/*
	 * Now that we know all the Mcxnex firmware accesses have been
	 * successful, we update the new "shadow" MCG entry by incrementing
	 * the "number of attached QPs" count.  Then we drop the lock and
	 * return success.
	 */
	newmcg->mcg_num_qps++;

	/*
	 * Increment the refcnt for this QP.  Because the QP
	 * was added to this MCG, the refcnt must be
	 * incremented.
	 */
	mcxnex_qp_mcg_refcnt_inc(qp);

	mutex_exit(&state->hs_mcglock);
	return (DDI_SUCCESS);
}


/*
 * mcxnex_mcg_detach()
 *    Context: Can be called only from user or kernel context.
 */
int
mcxnex_mcg_detach(mcxnex_state_t *state, mcxnex_qphdl_t qp, ib_gid_t gid,
    ib_lid_t lid)
{
	mcxnex_hw_mcg_t		*mcg_entry;
	mcxnex_hw_mcg_qp_list_t	*mcg_entry_qplist;
	mcxnex_mcghdl_t		mcg;
	uint64_t		mgid_hash;
	uint32_t		end_indx, prev_indx;
	int			status;

	/*
	 * Check for invalid Multicast DLID.  Specifically, all Multicast
	 * LIDs should be within a well defined range.  If the specified LID
	 * is outside of that range, then return an error.
	 */
	if (mcxnex_mlid_is_valid(lid) == 0) {
		return (IBT_MC_MLID_INVALID);
	}

	/*
	 * Compute the MGID hash value.  As described above, the MCG table is
	 * arranged as a number of separate hash chains.  This operation
	 * converts the specified MGID into the starting index of an entry in
	 * the hash table (i.e. the index for the start of the appropriate
	 * hash chain).  Subsequent operations below will walk the chain
	 * searching for a matching entry from which to attempt to remove
	 * the specified QP.
	 */
	status = mcxnex_mgid_hash_cmd_post(state, gid.gid_prefix, gid.gid_guid,
	    &mgid_hash, MCXNEX_SLEEPFLAG_FOR_CONTEXT());
	if (status != MCXNEX_CMD_SUCCESS) {
		cmn_err(CE_CONT, "Mcxnex: MGID_HASH command failed: %08x\n",
		    status);
		if (status == MCXNEX_CMD_INVALID_STATUS) {
			mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_SRV_LOST);
		}
		return (DDI_FAILURE);
	}

	/*
	 * Grab the multicast group mutex.  Then grab the pre-allocated
	 * temporary buffer used for holding and/or modifying MCG entries.
	 */
	mutex_enter(&state->hs_mcglock);
	mcg_entry = state->hs_mcgtmp;
	mcg_entry_qplist = MCXNEX_MCG_GET_QPLIST_PTR(mcg_entry);

	/*
	 * Walk through the array of MCG entries starting at "mgid_hash".
	 * Try to find an MCG entry with a matching MGID.  The
	 * mcxnex_mcg_walk_mgid_hash() routine walks the list and returns an
	 * index into the MCG table.  The entry at this index is checked to
	 * determine whether it is a match or not.  If it is a match, then
	 * we continue on to attempt to remove the QP from the MCG.  If it
	 * is not a match (or not a valid MCG entry), then we return an error.
	 */
	end_indx = mcxnex_mcg_walk_mgid_hash(state, mgid_hash, gid, &prev_indx);
	mcg	 = &state->hs_mcghdl[end_indx];

	/*
	 * If MGID == 0 (the hash chain is empty) or if the specified MGID
	 * does not match the MGID in the current entry, then return
	 * IBT_MC_MGID_INVALID (to indicate that the specified MGID is not
	 * valid).
	 */
	if (((mcg->mcg_mgid_h == 0) && (mcg->mcg_mgid_l == 0)) ||
	    ((mcg->mcg_mgid_h != gid.gid_prefix) ||
	    (mcg->mcg_mgid_l != gid.gid_guid))) {
		mutex_exit(&state->hs_mcglock);
		return (IBT_MC_MGID_INVALID);
	}

	/*
	 * Read the current MCG entry into the temporary MCG.  Note: In
	 * general, this operation shouldn't fail.  If it does, then it is
	 * an indication that something (probably in HW, but maybe in SW)
	 * has gone seriously wrong.
	 */
	status = mcxnex_read_mgm_cmd_post(state, mcg_entry, end_indx,
	    DDI_NOSLEEP);
	if (status != MCXNEX_CMD_SUCCESS) {
		mutex_exit(&state->hs_mcglock);
		MCXNEX_WARNING(state, "failed to read MCG entry");
		cmn_err(CE_CONT, "Mcxnex: READ_MGM command failed: %08x\n",
		    status);
		if (status == MCXNEX_CMD_INVALID_STATUS) {
			mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_SRV_LOST);
		}
		return (DDI_FAILURE);
	}

	/*
	 * Search the QP number list for a match.  If a match is found, then
	 * remove the entry from the QP list.  Otherwise, if no match is found,
	 * return an error.
	 */
	status = mcxnex_mcg_qplist_remove(mcg, mcg_entry_qplist, qp);
	if (status != DDI_SUCCESS) {
		mutex_exit(&state->hs_mcglock);
		return (status);
	}

	/*
	 * Decrement the MCG count for this QP.  When the 'qp_mcg'
	 * field becomes 0, then this QP is no longer a member of any
	 * MCG.
	 */
	mcxnex_qp_mcg_refcnt_dec(qp);

	/*
	 * If the current MCG's QP number list is about to be made empty
	 * ("mcg_num_qps" == 1), then remove the entry itself from the hash
	 * chain.  Otherwise, just write the updated MCG entry back to the
	 * hardware.  In either case, once we successfully update the hardware
	 * chain, then we decrement the "shadow" list entry's "mcg_num_qps"
	 * count (or zero out the entire "shadow" list entry) before returning
	 * success.  Note:  Zeroing out the "shadow" list entry is done
	 * inside of mcxnex_mcg_hash_list_remove().
	 */
	if (mcg->mcg_num_qps == 1) {

		/* Remove an MCG entry from the hash chain */
		status = mcxnex_mcg_hash_list_remove(state, end_indx, prev_indx,
		    mcg_entry);
		if (status != DDI_SUCCESS) {
			mutex_exit(&state->hs_mcglock);
			return (status);
		}

	} else {
		/*
		 * Write the updated MCG entry back to the Mcxnex MCG table.
		 * If this succeeds, then we update the "shadow" list to
		 * reflect the change (i.e. decrement the "mcg_num_qps"),
		 * drop the lock, and return success.  Note:  In general,
		 * this operation shouldn't fail.  If it does, then it is an
		 * indication that something (probably in HW, but maybe in SW)
		 * has gone seriously wrong.
		 */
		mcg_entry->member_cnt = (mcg->mcg_num_qps - 1);
		status = mcxnex_write_mgm_cmd_post(state, mcg_entry, end_indx,
		    DDI_NOSLEEP);
		if (status != MCXNEX_CMD_SUCCESS) {
			mutex_exit(&state->hs_mcglock);
			MCXNEX_WARNING(state, "failed to write MCG entry");
			cmn_err(CE_CONT, "Mcxnex: WRITE_MGM command failed: "
			    "%08x\n", status);
			if (status == MCXNEX_CMD_INVALID_STATUS) {
				mcxnex_fm_ereport(state, HCA_SYS_ERR,
				    HCA_ERR_SRV_LOST);
			}
			return (DDI_FAILURE);
		}
		mcg->mcg_num_qps--;
	}

	mutex_exit(&state->hs_mcglock);
	return (DDI_SUCCESS);
}

/*
 * mcxnex_qp_mcg_refcnt_inc()
 *    Context: Can be called from interrupt or base context.
 */
static void
mcxnex_qp_mcg_refcnt_inc(mcxnex_qphdl_t qp)
{
	/* Increment the QP's MCG reference count */
	mutex_enter(&qp->qp_lock);
	qp->qp_mcg_refcnt++;
	mutex_exit(&qp->qp_lock);
}


/*
 * mcxnex_qp_mcg_refcnt_dec()
 *    Context: Can be called from interrupt or base context.
 */
static void
mcxnex_qp_mcg_refcnt_dec(mcxnex_qphdl_t qp)
{
	/* Decrement the QP's MCG reference count */
	mutex_enter(&qp->qp_lock);
	qp->qp_mcg_refcnt--;
	mutex_exit(&qp->qp_lock);
}


/*
 * mcxnex_mcg_qplist_add()
 *    Context: Can be called from interrupt or base context.
 */
static int
mcxnex_mcg_qplist_add(mcxnex_state_t *state, mcxnex_mcghdl_t mcg,
    mcxnex_hw_mcg_qp_list_t *mcg_qplist, mcxnex_qphdl_t qp,
    uint_t *qp_found)
{
	uint_t		qplist_indx;

	ASSERT(MUTEX_HELD(&state->hs_mcglock));

	qplist_indx = mcg->mcg_num_qps;

	/*
	 * Determine if we have exceeded the maximum number of QP per
	 * multicast group.  If we have, then return an error
	 */
	if (qplist_indx >= state->hs_cfg_profile->cp_num_qp_per_mcg) {
		return (IBT_HCA_MCG_QP_EXCEEDED);
	}

	/*
	 * Determine if the QP is already attached to this MCG table.  If it
	 * is, then we break out and treat this operation as a NO-OP
	 */
	for (qplist_indx = 0; qplist_indx < mcg->mcg_num_qps;
	    qplist_indx++) {
		if (mcg_qplist[qplist_indx].qpn == qp->qp_qpnum) {
			break;
		}
	}

	/*
	 * If the QP was already on the list, set 'qp_found' to TRUE.  We still
	 * return SUCCESS in this case, but the qplist will not have been
	 * updated because the QP was already on the list.
	 */
	if (qplist_indx < mcg->mcg_num_qps) {
		*qp_found = 1;
	} else {
		/*
		 * Otherwise, append the new QP number to the end of the
		 * current QP list.  Note: We will increment the "mcg_num_qps"
		 * field on the "shadow" MCG list entry later (after we know
		 * that all necessary Mcxnex firmware accesses have been
		 * successful).
		 *
		 * Set 'qp_found' to 0 so we know the QP was added on to the
		 * list for sure.
		 */
		mcg_qplist[qplist_indx].qpn =
		    (qp->qp_qpnum | MCXNEX_MCG_QPN_BLOCK_LB);
		*qp_found = 0;
	}

	return (DDI_SUCCESS);
}



/*
 * mcxnex_mcg_qplist_remove()
 *    Context: Can be called from interrupt or base context.
 */
static int
mcxnex_mcg_qplist_remove(mcxnex_mcghdl_t mcg,
    mcxnex_hw_mcg_qp_list_t *mcg_qplist, mcxnex_qphdl_t qp)
{
	uint_t		i, qplist_indx;

	/*
	 * Search the MCG QP list for a matching QPN.  When
	 * it's found, we swap the last entry with the current
	 * one, set the last entry to zero, decrement the last
	 * entry, and return.  If it's not found, then it's
	 * and error.
	 */
	qplist_indx = mcg->mcg_num_qps;
	for (i = 0; i < qplist_indx; i++) {
		if (mcg_qplist[i].qpn == qp->qp_qpnum) {
			mcg_qplist[i] = mcg_qplist[qplist_indx - 1];
			mcg_qplist[qplist_indx - 1].qpn = 0;

			return (DDI_SUCCESS);
		}
	}

	return (IBT_QP_HDL_INVALID);
}


/*
 * mcxnex_mcg_walk_mgid_hash()
 *    Context: Can be called from interrupt or base context.
 */
static uint_t
mcxnex_mcg_walk_mgid_hash(mcxnex_state_t *state, uint64_t start_indx,
    ib_gid_t mgid, uint_t *p_indx)
{
	mcxnex_mcghdl_t	curr_mcghdl;
	uint_t		curr_indx, prev_indx;

	ASSERT(MUTEX_HELD(&state->hs_mcglock));

	/* Start at the head of the hash chain */
	curr_indx   = (uint_t)start_indx;
	prev_indx   = curr_indx;
	curr_mcghdl = &state->hs_mcghdl[curr_indx];

	/* If the first entry in the chain has MGID == 0, then stop */
	if ((curr_mcghdl->mcg_mgid_h == 0) &&
	    (curr_mcghdl->mcg_mgid_l == 0)) {
		goto end_mgid_hash_walk;
	}

	/* If the first entry in the chain matches the MGID, then stop */
	if ((curr_mcghdl->mcg_mgid_h == mgid.gid_prefix) &&
	    (curr_mcghdl->mcg_mgid_l == mgid.gid_guid)) {
		goto end_mgid_hash_walk;
	}

	/* Otherwise, walk the hash chain looking for a match */
	while (curr_mcghdl->mcg_next_indx != 0) {
		prev_indx = curr_indx;
		curr_indx = curr_mcghdl->mcg_next_indx;
		curr_mcghdl = &state->hs_mcghdl[curr_indx];

		if ((curr_mcghdl->mcg_mgid_h == mgid.gid_prefix) &&
		    (curr_mcghdl->mcg_mgid_l == mgid.gid_guid)) {
			break;
		}
	}

end_mgid_hash_walk:
	/*
	 * If necessary, return the index of the previous entry too.  This
	 * is primarily used for detaching a QP from a multicast group.  It
	 * may be necessary, in that case, to delete an MCG entry from the
	 * hash chain and having the index of the previous entry is helpful.
	 */
	if (p_indx != NULL) {
		*p_indx = prev_indx;
	}
	return (curr_indx);
}


/*
 * mcxnex_mcg_setup_new_hdr()
 *    Context: Can be called from interrupt or base context.
 */
static void
mcxnex_mcg_setup_new_hdr(mcxnex_mcghdl_t mcg, mcxnex_hw_mcg_t *mcg_hdr,
    ib_gid_t mgid, mcxnex_rsrc_t *mcg_rsrc)
{
	/*
	 * Fill in the fields of the "shadow" entry used by software
	 * to track MCG hardware entry
	 */
	mcg->mcg_mgid_h	   = mgid.gid_prefix;
	mcg->mcg_mgid_l	   = mgid.gid_guid;
	mcg->mcg_rsrcp	   = mcg_rsrc;
	mcg->mcg_next_indx = 0;
	mcg->mcg_num_qps   = 0;

	/*
	 * Fill the header fields of the MCG entry (in the temporary copy)
	 */
	mcg_hdr->mgid_h		= mgid.gid_prefix;
	mcg_hdr->mgid_l		= mgid.gid_guid;
	mcg_hdr->next_gid_indx	= 0;
}


/*
 * mcxnex_mcg_hash_list_remove()
 *    Context: Can be called only from user or kernel context.
 */
static int
mcxnex_mcg_hash_list_remove(mcxnex_state_t *state, uint_t curr_indx,
    uint_t prev_indx, mcxnex_hw_mcg_t *mcg_entry)
{
	mcxnex_mcghdl_t		curr_mcg, prev_mcg, next_mcg;
	uint_t			next_indx;
	int			status;

	/* Get the pointer to "shadow" list for current entry */
	curr_mcg = &state->hs_mcghdl[curr_indx];

	/*
	 * If this is the first entry on a hash chain, then attempt to replace
	 * the entry with the next entry on the chain.  If there are no
	 * subsequent entries on the chain, then this is the only entry and
	 * should be invalidated.
	 */
	if (curr_indx == prev_indx) {

		/*
		 * If this is the only entry on the chain, then invalidate it.
		 * Note:  Invalidating an MCG entry means writing all zeros
		 * to the entry.  This is only necessary for those MCG
		 * entries that are the "head" entries of the individual hash
		 * chains.  Regardless of whether this operation returns
		 * success or failure, return that result to the caller.
		 */
		next_indx = curr_mcg->mcg_next_indx;
		if (next_indx == 0) {
			status = mcxnex_mcg_entry_invalidate(state, mcg_entry,
			    curr_indx);
			bzero(curr_mcg, sizeof (struct mcxnex_sw_mcg_list_s));
			return (status);
		}

		/*
		 * Otherwise, this is just the first entry on the chain, so
		 * grab the next one
		 */
		next_mcg = &state->hs_mcghdl[next_indx];

		/*
		 * Read the next MCG entry into the temporary MCG.  Note:
		 * In general, this operation shouldn't fail.  If it does,
		 * then it is an indication that something (probably in HW,
		 * but maybe in SW) has gone seriously wrong.
		 */
		status = mcxnex_read_mgm_cmd_post(state, mcg_entry, next_indx,
		    DDI_NOSLEEP);
		if (status != MCXNEX_CMD_SUCCESS) {
			MCXNEX_WARNING(state, "failed to read MCG entry");
			cmn_err(CE_CONT, "Mcxnex: READ_MGM command failed: "
			    "%08x\n", status);
			if (status == MCXNEX_CMD_INVALID_STATUS) {
				mcxnex_fm_ereport(state, HCA_SYS_ERR,
				    HCA_ERR_SRV_LOST);
			}
			return (DDI_FAILURE);
		}

		/*
		 * Copy/Write the temporary MCG back to the hardware MCG list
		 * using the current index.  This essentially removes the
		 * current MCG entry from the list by writing over it with
		 * the next one.  If this is successful, then we can do the
		 * same operation for the "shadow" list.  And we can also
		 * free up the Mcxnex MCG entry resource that was associated
		 * with the (old) next entry.  Note:  In general, this
		 * operation shouldn't fail.  If it does, then it is an
		 * indication that something (probably in HW, but maybe in SW)
		 * has gone seriously wrong.
		 */
		status = mcxnex_write_mgm_cmd_post(state, mcg_entry, curr_indx,
		    DDI_NOSLEEP);
		if (status != MCXNEX_CMD_SUCCESS) {
			MCXNEX_WARNING(state, "failed to write MCG entry");
			cmn_err(CE_CONT, "Mcxnex: WRITE_MGM command failed: "
			    "%08x\n", status);
			if (status == MCXNEX_CMD_INVALID_STATUS) {
				mcxnex_fm_ereport(state, HCA_SYS_ERR,
				    HCA_ERR_SRV_LOST);
			}
			return (DDI_FAILURE);
		}

		/*
		 * Copy all the software tracking information from the next
		 * entry on the "shadow" MCG list into the current entry on
		 * the list.  Then invalidate (zero out) the other "shadow"
		 * list entry.
		 */
		bcopy(next_mcg, curr_mcg, sizeof (struct mcxnex_sw_mcg_list_s));
		bzero(next_mcg, sizeof (struct mcxnex_sw_mcg_list_s));

		/*
		 * Free up the Mcxnex MCG entry resource used by the "next"
		 * MCG entry.  That resource is no longer needed by any
		 * MCG entry which is first on a hash chain (like the "next"
		 * entry has just become).
		 */
		mcxnex_rsrc_free(state, &curr_mcg->mcg_rsrcp);

		return (DDI_SUCCESS);
	}

	/*
	 * Else if this is the last entry on the hash chain (or a middle
	 * entry, then we update the previous entry's "next_gid_index" field
	 * to make it point instead to the next entry on the chain.  By
	 * skipping over the removed entry in this way, we can then free up
	 * any resources associated with the current entry.  Note:  We don't
	 * need to invalidate the "skipped over" hardware entry because it
	 * will no be longer connected to any hash chains, and if/when it is
	 * finally re-used, it will be written with entirely new values.
	 */

	/*
	 * Read the next MCG entry into the temporary MCG.  Note:  In general,
	 * this operation shouldn't fail.  If it does, then it is an
	 * indication that something (probably in HW, but maybe in SW) has
	 * gone seriously wrong.
	 */
	status = mcxnex_read_mgm_cmd_post(state, mcg_entry, prev_indx,
	    DDI_NOSLEEP);
	if (status != MCXNEX_CMD_SUCCESS) {
		MCXNEX_WARNING(state, "failed to read MCG entry");
		cmn_err(CE_CONT, "Mcxnex: READ_MGM command failed: %08x\n",
		    status);
		if (status == MCXNEX_CMD_INVALID_STATUS) {
			mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_SRV_LOST);
		}
		return (DDI_FAILURE);
	}

	/*
	 * Finally, we update the "next_gid_indx" field in the temporary MCG
	 * and attempt to write the entry back into the Mcxnex MCG table.  If
	 * this succeeds, then we update the "shadow" list to reflect the
	 * change, free up the Mcxnex MCG entry resource that was associated
	 * with the current entry, and return success.  Note:  In general,
	 * this operation shouldn't fail.  If it does, then it is an indication
	 * that something (probably in HW, but maybe in SW) has gone seriously
	 * wrong.
	 */
	mcg_entry->next_gid_indx = curr_mcg->mcg_next_indx;
	status = mcxnex_write_mgm_cmd_post(state, mcg_entry, prev_indx,
	    DDI_NOSLEEP);
	if (status != MCXNEX_CMD_SUCCESS) {
		MCXNEX_WARNING(state, "failed to write MCG entry");
		cmn_err(CE_CONT, "Mcxnex: WRITE_MGM command failed: %08x\n",
		    status);
		if (status == MCXNEX_CMD_INVALID_STATUS) {
			mcxnex_fm_ereport(state, HCA_SYS_ERR,
			    HCA_ERR_SRV_LOST);
		}
		return (DDI_FAILURE);
	}

	/*
	 * Get the pointer to the "shadow" MCG list entry for the previous
	 * MCG.  Update its "mcg_next_indx" to point to the next entry
	 * the one after the current entry. Note:  This next index may be
	 * zero, indicating the end of the list.
	 */
	prev_mcg = &state->hs_mcghdl[prev_indx];
	prev_mcg->mcg_next_indx = curr_mcg->mcg_next_indx;

	/*
	 * Free up the Mcxnex MCG entry resource used by the current entry.
	 * This resource is no longer needed because the chain now skips over
	 * the current entry.  Then invalidate (zero out) the current "shadow"
	 * list entry.
	 */
	mcxnex_rsrc_free(state, &curr_mcg->mcg_rsrcp);
	bzero(curr_mcg, sizeof (struct mcxnex_sw_mcg_list_s));

	return (DDI_SUCCESS);
}


/*
 * mcxnex_mcg_entry_invalidate()
 *    Context: Can be called only from user or kernel context.
 */
static int
mcxnex_mcg_entry_invalidate(mcxnex_state_t *state, mcxnex_hw_mcg_t *mcg_entry,
    uint_t indx)
{
	int		status;

	/*
	 * Invalidate the hardware MCG entry by zeroing out this temporary
	 * MCG and writing it the the hardware.  Note: In general, this
	 * operation shouldn't fail.  If it does, then it is an indication
	 * that something (probably in HW, but maybe in SW) has gone seriously
	 * wrong.
	 */
	bzero(mcg_entry, MCXNEX_MCGMEM_SZ(state));
	status = mcxnex_write_mgm_cmd_post(state, mcg_entry, indx,
	    DDI_NOSLEEP);
	if (status != MCXNEX_CMD_SUCCESS) {
		MCXNEX_WARNING(state, "failed to write MCG entry");
		cmn_err(CE_CONT, "Mcxnex: WRITE_MGM command failed: %08x\n",
		    status);
		if (status == MCXNEX_CMD_INVALID_STATUS) {
			mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_SRV_LOST);
		}
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_mgid_is_valid()
 *    Context: Can be called from interrupt or base context.
 */
static int
mcxnex_mgid_is_valid(ib_gid_t gid)
{
	uint_t		topbits, flags, scope;

	/*
	 * According to IBA 1.1 specification (section 4.1.1) a valid
	 * "multicast GID" must have its top eight bits set to all ones
	 */
	topbits = (gid.gid_prefix >> MCXNEX_MCG_TOPBITS_SHIFT) &
	    MCXNEX_MCG_TOPBITS_MASK;
	if (topbits != MCXNEX_MCG_TOPBITS) {
		return (0);
	}

	/*
	 * The next 4 bits are the "flag" bits.  These are valid only
	 * if they are "0" (which correspond to permanently assigned/
	 * "well-known" multicast GIDs) or "1" (for so-called "transient"
	 * multicast GIDs).  All other values are reserved.
	 */
	flags = (gid.gid_prefix >> MCXNEX_MCG_FLAGS_SHIFT) &
	    MCXNEX_MCG_FLAGS_MASK;
	if (!((flags == MCXNEX_MCG_FLAGS_PERM) ||
	    (flags == MCXNEX_MCG_FLAGS_NONPERM))) {
		return (0);
	}

	/*
	 * The next 4 bits are the "scope" bits.  These are valid only
	 * if they are "2" (Link-local), "5" (Site-local), "8"
	 * (Organization-local) or "E" (Global).  All other values
	 * are reserved (or currently unassigned).
	 */
	scope = (gid.gid_prefix >> MCXNEX_MCG_SCOPE_SHIFT) &
	    MCXNEX_MCG_SCOPE_MASK;
	if (!((scope == MCXNEX_MCG_SCOPE_LINKLOC) ||
	    (scope == MCXNEX_MCG_SCOPE_SITELOC)	 ||
	    (scope == MCXNEX_MCG_SCOPE_ORGLOC)	 ||
	    (scope == MCXNEX_MCG_SCOPE_GLOBAL))) {
		return (0);
	}

	/*
	 * If it passes all of the above checks, then we will consider it
	 * a valid multicast GID.
	 */
	return (1);
}


/*
 * mcxnex_mlid_is_valid()
 *    Context: Can be called from interrupt or base context.
 */
static int
mcxnex_mlid_is_valid(ib_lid_t lid)
{
	/*
	 * According to IBA 1.1 specification (section 4.1.1) a valid
	 * "multicast DLID" must be between 0xC000 and 0xFFFE.
	 */
	if ((lid < IB_LID_MC_FIRST) || (lid > IB_LID_MC_LAST)) {
		return (0);
	}

	return (1);
}


/*
 * mcxnex_pd_alloc()
 *    Context: Can be called only from user or kernel context.
 */
int
mcxnex_pd_alloc(mcxnex_state_t *state, mcxnex_pdhdl_t *pdhdl, uint_t sleepflag)
{
	mcxnex_rsrc_t	*rsrc;
	mcxnex_pdhdl_t	pd;
	int		status;

	/*
	 * Allocate the software structure for tracking the protection domain
	 * (i.e. the Mcxnex Protection Domain handle).  By default each PD
	 * structure will have a unique PD number assigned to it.  All that
	 * is necessary is for software to initialize the PD reference count
	 * (to zero) and return success.
	 */
	status = mcxnex_rsrc_alloc(state, MCXNEX_PDHDL, 1, sleepflag, &rsrc);
	if (status != DDI_SUCCESS) {
		return (IBT_INSUFF_RESOURCE);
	}
	pd = (mcxnex_pdhdl_t)rsrc->hr_addr;
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*pd))

	pd->pd_refcnt = 0;
	*pdhdl = pd;

	return (DDI_SUCCESS);
}


/*
 * mcxnex_pd_free()
 *    Context: Can be called only from user or kernel context.
 */
int
mcxnex_pd_free(mcxnex_state_t *state, mcxnex_pdhdl_t *pdhdl)
{
	mcxnex_rsrc_t	*rsrc;
	mcxnex_pdhdl_t	pd;

	/*
	 * Pull all the necessary information from the Mcxnex Protection Domain
	 * handle.  This is necessary here because the resource for the
	 * PD is going to be freed up as part of this operation.
	 */
	pd   = *pdhdl;
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*pd))
	rsrc = pd->pd_rsrcp;

	/*
	 * Check the PD reference count.  If the reference count is non-zero,
	 * then it means that this protection domain is still referenced by
	 * some memory region, queue pair, address handle, or other IB object
	 * If it is non-zero, then return an error.  Otherwise, free the
	 * Mcxnex resource and return success.
	 */
	if (pd->pd_refcnt != 0) {
		return (IBT_PD_IN_USE);
	}

	/* Free the Mcxnex Protection Domain handle */
	mcxnex_rsrc_free(state, &rsrc);

	/* Set the pdhdl pointer to NULL and return success */
	*pdhdl = (mcxnex_pdhdl_t)NULL;

	return (DDI_SUCCESS);
}


/*
 * mcxnex_pd_refcnt_inc()
 *    Context: Can be called from interrupt or base context.
 */
void
mcxnex_pd_refcnt_inc(mcxnex_pdhdl_t pd)
{
	/* Increment the protection domain's reference count */
	atomic_inc_32(&pd->pd_refcnt);
}


/*
 * mcxnex_pd_refcnt_dec()
 *    Context: Can be called from interrupt or base context.
 */
void
mcxnex_pd_refcnt_dec(mcxnex_pdhdl_t pd)
{
	/* Decrement the protection domain's reference count */
	atomic_dec_32(&pd->pd_refcnt);
}



/*
 * mcxnex_set_addr_path()
 *    Context: Can be called from interrupt or base context.
 *
 * Note: This routine is used for two purposes.  It is used to fill in the
 * Mcxnex UDAV fields, and it is used to fill in the address path information
 * for QPs.  Because the two Mcxnex structures are similar, common fields can
 * be filled in here.  Because they are different, however, we pass
 * an additional flag to indicate which type is being filled and do each one
 * uniquely
 */

int mcxnex_srate_override = -1;	/* allows ease of testing */

int
mcxnex_set_addr_path(mcxnex_state_t *state, ibt_adds_vect_t *av,
    mcxnex_hw_addr_path_t *path, uint_t type)
{
	uint_t		gidtbl_sz;
	mcxnex_hw_udav_t *udav;

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*av))
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*path))

	udav = (mcxnex_hw_udav_t *)(void *)path;
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*udav))
	path->mlid	= av->av_src_path;
	path->rlid	= av->av_dlid;

	switch (av->av_srate) {
	case IBT_SRATE_2:	/* 1xSDR-2.5Gb/s injection rate */
		path->max_stat_rate = 7; break;
	case IBT_SRATE_10:	/* 4xSDR-10.0Gb/s injection rate */
		path->max_stat_rate = 8; break;
	case IBT_SRATE_30:	/* 12xSDR-30Gb/s injection rate */
		path->max_stat_rate = 9; break;
	case IBT_SRATE_5:	/* 1xDDR-5Gb/s injection rate */
		path->max_stat_rate = 10; break;
	case IBT_SRATE_20:	/* 4xDDR-20Gb/s injection rate */
		path->max_stat_rate = 11; break;
	case IBT_SRATE_40:	/* 4xQDR-40Gb/s injection rate */
		path->max_stat_rate = 12; break;
	case IBT_SRATE_60:	/* 12xDDR-60Gb/s injection rate */
		path->max_stat_rate = 13; break;
	case IBT_SRATE_80:	/* 8xQDR-80Gb/s injection rate */
		path->max_stat_rate = 14; break;
	case IBT_SRATE_120:	/* 12xQDR-120Gb/s injection rate */
		path->max_stat_rate = 15; break;
	case IBT_SRATE_NOT_SPECIFIED:	/* Max */
		path->max_stat_rate = 0; break;
	default:
		return (IBT_STATIC_RATE_INVALID);
	}
	if (mcxnex_srate_override != -1) /* for evaluating HCA firmware */
		path->max_stat_rate = mcxnex_srate_override;

	/* If "grh" flag is set, then check for valid SGID index too */
	gidtbl_sz = (1 << state->hs_queryport.log_max_gid);
	if ((av->av_send_grh) && (av->av_sgid_ix > gidtbl_sz)) {
		return (IBT_SGID_INVALID);
	}

	/*
	 * Fill in all "global" values regardless of the value in the GRH
	 * flag.  Because "grh" is not set unless "av_send_grh" is set, the
	 * hardware will ignore the other "global" values as necessary.  Note:
	 * SW does this here to enable later query operations to return
	 * exactly the same params that were passed when the addr path was
	 * last written.
	 */
	path->grh = av->av_send_grh;
	if (type == MCXNEX_ADDRPATH_QP) {
		path->mgid_index = av->av_sgid_ix;
	} else {
		/*
		 * For Mcxnex UDAV, the "mgid_index" field is the index into
		 * a combined table (not a per-port table), but having sections
		 * for each port. So some extra calculations are necessary.
		 */

		path->mgid_index = ((av->av_port_num - 1) * gidtbl_sz) +
		    av->av_sgid_ix;

		udav->portnum = av->av_port_num;
	}

	/*
	 * According to Mcxnex PRM, the (31:0) part of rgid_l must be set to
	 * "0x2" if the 'grh' or 'g' bit is cleared.  It also says that we
	 * only need to do it for UDAV's.  So we enforce that here.
	 *
	 * NOTE: The entire 64 bits worth of GUID info is actually being
	 * preserved (for UDAVs) by the callers of this function
	 * (mcxnex_ah_alloc() and mcxnex_ah_modify()) and as long as the
	 * 'grh' bit is not set, the upper 32 bits (63:32) of rgid_l are
	 * "don't care".
	 */
	if ((path->grh) || (type == MCXNEX_ADDRPATH_QP)) {
		path->flow_label = av->av_flow;
		path->tclass	 = av->av_tclass;
		path->hop_limit	 = av->av_hop;
		bcopy(&(av->av_dgid.gid_prefix), &(path->rgid_h),
		    sizeof (uint64_t));
		bcopy(&(av->av_dgid.gid_guid), &(path->rgid_l),
		    sizeof (uint64_t));
	} else {
		path->rgid_l	 = 0x2;
		path->flow_label = 0;
		path->tclass	 = 0;
		path->hop_limit	 = 0;
		path->rgid_h	 = 0;
	}
	/* extract the default service level */
	udav->sl = (MCXNEX_DEF_SCHED_SELECTION & 0x3C) >> 2;

	return (DDI_SUCCESS);
}


/*
 * mcxnex_get_addr_path()
 *    Context: Can be called from interrupt or base context.
 *
 * Note: Just like mcxnex_set_addr_path() above, this routine is used for two
 * purposes.  It is used to read in the Mcxnex UDAV fields, and it is used to
 * read in the address path information for QPs.  Because the two Mcxnex
 * structures are similar, common fields can be read in here.  But because
 * they are slightly different, we pass an additional flag to indicate which
 * type is being read.
 */
void
mcxnex_get_addr_path(mcxnex_state_t *state, mcxnex_hw_addr_path_t *path,
    ibt_adds_vect_t *av, uint_t type)
{
	uint_t		gidtbl_sz;

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*path))
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*av))

	av->av_src_path	= path->mlid;
	av->av_dlid	= path->rlid;

	/* Set "av_ipd" value from max_stat_rate */
	switch (path->max_stat_rate) {
	case 7:				/* 1xSDR-2.5Gb/s injection rate */
		av->av_srate = IBT_SRATE_2; break;
	case 8:				/* 4xSDR-10.0Gb/s injection rate */
		av->av_srate = IBT_SRATE_10; break;
	case 9:				/* 12xSDR-30Gb/s injection rate */
		av->av_srate = IBT_SRATE_30; break;
	case 10:			/* 1xDDR-5Gb/s injection rate */
		av->av_srate = IBT_SRATE_5; break;
	case 11:			/* 4xDDR-20Gb/s injection rate */
		av->av_srate = IBT_SRATE_20; break;
	case 12:			/* xQDR-40Gb/s injection rate */
		av->av_srate = IBT_SRATE_40; break;
	case 13:			/* 12xDDR-60Gb/s injection rate */
		av->av_srate = IBT_SRATE_60; break;
	case 14:			/* 8xQDR-80Gb/s injection rate */
		av->av_srate = IBT_SRATE_80; break;
	case 15:			/* 12xQDR-120Gb/s injection rate */
		av->av_srate = IBT_SRATE_120; break;
	case 0:				/* max */
		av->av_srate = IBT_SRATE_NOT_SPECIFIED; break;
	default:			/* 1x injection rate */
		av->av_srate = IBT_SRATE_1X;
	}

	/*
	 * Extract all "global" values regardless of the value in the GRH
	 * flag.  Because "av_send_grh" is set only if "grh" is set, software
	 * knows to ignore the other "global" values as necessary.  Note: SW
	 * does it this way to enable these query operations to return exactly
	 * the same params that were passed when the addr path was last written.
	 */
	av->av_send_grh		= path->grh;
	if (type == MCXNEX_ADDRPATH_QP) {
		av->av_sgid_ix  = path->mgid_index;
	} else {
		/*
		 * For Mcxnex UDAV, the "mgid_index" field is the index into
		 * a combined table (not a per-port table).
		 */
		gidtbl_sz = (1 << state->hs_queryport.log_max_gid);
		av->av_sgid_ix = path->mgid_index - ((av->av_port_num - 1) *
		    gidtbl_sz);

		av->av_port_num = ((mcxnex_hw_udav_t *)(void *)path)->portnum;
	}
	av->av_flow		= path->flow_label;
	av->av_tclass		= path->tclass;
	av->av_hop		= path->hop_limit;
	/* this is for alignment issue w/ the addr path struct in Mcxnex */
	bcopy(&(path->rgid_h), &(av->av_dgid.gid_prefix), sizeof (uint64_t));
	bcopy(&(path->rgid_l), &(av->av_dgid.gid_guid), sizeof (uint64_t));
}


/*
 * mcxnex_portnum_is_valid()
 *    Context: Can be called from interrupt or base context.
 */
int
mcxnex_portnum_is_valid(mcxnex_state_t *state, uint_t portnum)
{
	uint_t	max_port;

	max_port = state->hs_cfg_profile->cp_num_ports;
	if ((portnum <= max_port) && (portnum != 0)) {
		return (1);
	} else {
		return (0);
	}
}


/*
 * mcxnex_pkeyindex_is_valid()
 *    Context: Can be called from interrupt or base context.
 */
int
mcxnex_pkeyindex_is_valid(mcxnex_state_t *state, uint_t pkeyindx)
{
	uint_t	max_pkeyindx;

	max_pkeyindx = 1 << state->hs_cfg_profile->cp_log_max_pkeytbl;
	if (pkeyindx < max_pkeyindx) {
		return (1);
	} else {
		return (0);
	}
}


/*
 * mcxnex_queue_alloc()
 *    Context: Can be called from interrupt or base context.
 */
int
mcxnex_queue_alloc(mcxnex_state_t *state, mcxnex_qalloc_info_t *qa_info,
    uint_t sleepflag)
{
	ddi_dma_attr_t		dma_attr;
	int			(*callback)(caddr_t);
	uint64_t		realsize, alloc_mask;
	uint_t			type;
	int			flag, status;

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*qa_info))

	/* Set the callback flag appropriately */
	callback = (sleepflag == DDI_SLEEP) ? DDI_DMA_SLEEP :
	    DDI_DMA_DONTWAIT;

	/*
	 * Initialize many of the default DMA attributes.  Then set additional
	 * alignment restrictions as necessary for the queue memory.  Also
	 * respect the configured value for IOMMU bypass
	 */
	mcxnex_dma_attr_init(state, &dma_attr);
	dma_attr.dma_attr_align = qa_info->qa_bind_align;
	type = state->hs_cfg_profile->cp_iommu_bypass;
	if (type == MCXNEX_BINDMEM_BYPASS) {
		dma_attr.dma_attr_flags = DDI_DMA_FORCE_PHYSICAL;
	}

	/* Allocate a DMA handle */
	status = ddi_dma_alloc_handle(state->hs_dip, &dma_attr, callback, NULL,
	    &qa_info->qa_dmahdl);
	if (status != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	/*
	 * Determine the amount of memory to allocate, depending on the values
	 * in "qa_bind_align" and "qa_alloc_align".  The problem we are trying
	 * to solve here is that allocating a DMA handle with IOMMU bypass
	 * (DDI_DMA_FORCE_PHYSICAL) constrains us to only requesting alignments
	 * that are less restrictive than the page size.  Since we may need
	 * stricter alignments on the memory allocated by ddi_dma_mem_alloc()
	 * (e.g. in Mcxnex QP work queue memory allocation), we use the
	 * following method to calculate how much additional memory to request,
	 * and we enforce our own alignment on the allocated result.
	 */
	alloc_mask = qa_info->qa_alloc_align - 1;
	if (qa_info->qa_bind_align == qa_info->qa_alloc_align) {
		realsize = qa_info->qa_size;
	} else {
		realsize = qa_info->qa_size + alloc_mask;
	}

	/*
	 * If we are to allocate the queue from system memory, then use
	 * ddi_dma_mem_alloc() to find the space.  Otherwise, this is a
	 * host memory allocation, use ddi_umem_alloc(). In either case,
	 * return a pointer to the memory range allocated (including any
	 * necessary alignment adjustments), the "real" memory pointer,
	 * the "real" size, and a ddi_acc_handle_t to use when reading
	 * from/writing to the memory.
	 */
	if (qa_info->qa_location == MCXNEX_QUEUE_LOCATION_NORMAL) {
		/* Allocate system memory for the queue */
		status = ddi_dma_mem_alloc(qa_info->qa_dmahdl, realsize,
		    &state->hs_reg_accattr, DDI_DMA_CONSISTENT, callback, NULL,
		    (caddr_t *)&qa_info->qa_buf_real,
		    (size_t *)&qa_info->qa_buf_realsz, &qa_info->qa_acchdl);
		if (status != DDI_SUCCESS) {
			ddi_dma_free_handle(&qa_info->qa_dmahdl);
			return (DDI_FAILURE);
		}

		/*
		 * Save temporary copy of the real pointer.  (This may be
		 * modified in the last step below).
		 */
		qa_info->qa_buf_aligned = qa_info->qa_buf_real;

		bzero(qa_info->qa_buf_real, qa_info->qa_buf_realsz);

	} else { /* MCXNEX_QUEUE_LOCATION_USERLAND */

		/* Allocate userland mappable memory for the queue */
		flag = (sleepflag == DDI_SLEEP) ? DDI_UMEM_SLEEP :
		    DDI_UMEM_NOSLEEP;
		qa_info->qa_buf_real = ddi_umem_alloc(realsize, flag,
		    &qa_info->qa_umemcookie);
		if (qa_info->qa_buf_real == NULL) {
			ddi_dma_free_handle(&qa_info->qa_dmahdl);
			return (DDI_FAILURE);
		}

		/*
		 * Save temporary copy of the real pointer.  (This may be
		 * modified in the last step below).
		 */
		qa_info->qa_buf_aligned = qa_info->qa_buf_real;

	}

	/*
	 * The next to last step is to ensure that the final address
	 * ("qa_buf_aligned") has the appropriate "alloc" alignment
	 * restriction applied to it (if necessary).
	 */
	if (qa_info->qa_bind_align != qa_info->qa_alloc_align) {
		qa_info->qa_buf_aligned = (uint32_t *)(uintptr_t)(((uintptr_t)
		    qa_info->qa_buf_aligned + alloc_mask) & ~alloc_mask);
	}
	/*
	 * The last step is to figure out the offset of the start relative
	 * to the first page of the region - will be used in the eqc/cqc
	 * passed to the HW
	 */
	qa_info->qa_pgoffs = (uint_t)((uintptr_t)
	    qa_info->qa_buf_aligned & MCXNEX_PAGEMASK);

	return (DDI_SUCCESS);
}


/*
 * mcxnex_queue_free()
 *    Context: Can be called from interrupt or base context.
 */
void
mcxnex_queue_free(mcxnex_qalloc_info_t *qa_info)
{
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*qa_info))

	/*
	 * Depending on how (i.e. from where) we allocated the memory for
	 * this queue, we choose the appropriate method for releasing the
	 * resources.
	 */
	if (qa_info->qa_location == MCXNEX_QUEUE_LOCATION_NORMAL) {

		ddi_dma_mem_free(&qa_info->qa_acchdl);

	} else if (qa_info->qa_location == MCXNEX_QUEUE_LOCATION_USERLAND) {

		ddi_umem_free(qa_info->qa_umemcookie);

	}

	/* Always free the dma handle */
	ddi_dma_free_handle(&qa_info->qa_dmahdl);
}

/*
 * mcxnex_destroy_fmr_pool()
 * Create a pool of FMRs.
 *     Context: Can be called from kernel context only.
 */
int
mcxnex_create_fmr_pool(mcxnex_state_t *state, mcxnex_pdhdl_t pd,
    ibt_fmr_pool_attr_t *fmr_attr, mcxnex_fmrhdl_t *fmrpoolp)
{
	mcxnex_fmrhdl_t	fmrpool;
	mcxnex_fmr_list_t *fmr, *fmr_next;
	mcxnex_mrhdl_t   mr;
	char		taskqname[48];
	int		status;
	int		sleep;
	int		i;

	sleep = (fmr_attr->fmr_flags & IBT_MR_SLEEP) ? DDI_SLEEP : DDI_NOSLEEP;
	if ((sleep == DDI_SLEEP) && (sleep != MCXNEX_SLEEPFLAG_FOR_CONTEXT())) {
		return (IBT_INVALID_PARAM);
	}

	fmrpool = kmem_zalloc(sizeof (*fmrpool), sleep);
	if (fmrpool == NULL) {
		status = IBT_INSUFF_RESOURCE;
		goto fail;
	}
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*fmrpool))

	mutex_init(&fmrpool->fmr_lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(state->hs_intrmsi_pri));

	fmrpool->fmr_state	    = state;
	fmrpool->fmr_flush_function = fmr_attr->fmr_func_hdlr;
	fmrpool->fmr_flush_arg	    = fmr_attr->fmr_func_arg;
	fmrpool->fmr_pool_size	    = 0;
	fmrpool->fmr_cache	    = 0;
	fmrpool->fmr_max_pages	    = fmr_attr->fmr_max_pages_per_fmr;
	fmrpool->fmr_page_sz	    = fmr_attr->fmr_page_sz;
	fmrpool->fmr_dirty_watermark = fmr_attr->fmr_dirty_watermark;
	fmrpool->fmr_dirty_len	    = 0;
	fmrpool->fmr_flags	    = fmr_attr->fmr_flags;

	/* Create taskq to handle cleanup and flush processing */
	(void) snprintf(taskqname, 50, "fmrpool/%d/%d @ 0x%" PRIx64,
	    fmr_attr->fmr_pool_size, mcxnex_debug_fmrpool_cnt,
	    (uint64_t)(uintptr_t)fmrpool);
	fmrpool->fmr_taskq = ddi_taskq_create(state->hs_dip, taskqname,
	    1, TASKQ_DEFAULTPRI, 0);
	if (fmrpool->fmr_taskq == NULL) {
		status = IBT_INSUFF_RESOURCE;
		goto fail1;
	}

	fmrpool->fmr_free_list = NULL;
	fmrpool->fmr_dirty_list = NULL;

	if (fmr_attr->fmr_cache) {
		mcxnex_fmr_cache_init(fmrpool);
	}

	for (i = 0; i < fmr_attr->fmr_pool_size; i++) {
		status = mcxnex_mr_alloc_fmr(state, pd, fmrpool, &mr);
		if (status != DDI_SUCCESS) {
			goto fail2;
		}

		fmr = (mcxnex_fmr_list_t *)kmem_zalloc(
		    sizeof (mcxnex_fmr_list_t), sleep);
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*fmr))

		fmr->fmr = mr;
		fmr->fmr_refcnt = 0;
		fmr->fmr_remaps = 0;
		fmr->fmr_pool = fmrpool;
		fmr->fmr_in_cache = 0;
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*mr))
		mr->mr_fmr = fmr;

		fmr->fmr_next = fmrpool->fmr_free_list;
		fmrpool->fmr_free_list = fmr;
		fmrpool->fmr_pool_size++;
	}

	/* Set to return pool */
	*fmrpoolp = fmrpool;

	return (IBT_SUCCESS);
fail2:
	mcxnex_fmr_cache_fini(fmrpool);
	for (fmr = fmrpool->fmr_free_list; fmr != NULL; fmr = fmr_next) {
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*fmr))
		fmr_next = fmr->fmr_next;
		(void) mcxnex_mr_dealloc_fmr(state, &fmr->fmr);
		kmem_free(fmr, sizeof (mcxnex_fmr_list_t));
	}
	ddi_taskq_destroy(fmrpool->fmr_taskq);
fail1:
	kmem_free(fmrpool, sizeof (*fmrpool));
fail:
	if (status == DDI_FAILURE) {
		return (DDI_FAILURE);
	} else {
		return (status);
	}
}

/*
 * mcxnex_destroy_fmr_pool()
 * Destroy an FMR pool and free all associated resources.
 *     Context: Can be called from kernel context only.
 */
int
mcxnex_destroy_fmr_pool(mcxnex_state_t *state, mcxnex_fmrhdl_t fmrpool)
{
	mcxnex_fmr_list_t	*fmr, *fmr_next;
	int			status;

	mutex_enter(&fmrpool->fmr_lock);
	status = mcxnex_fmr_cleanup(state, fmrpool);
	if (status != DDI_SUCCESS) {
		mutex_exit(&fmrpool->fmr_lock);
		return (status);
	}

	if (fmrpool->fmr_cache) {
		mcxnex_fmr_cache_fini(fmrpool);
	}

	for (fmr = fmrpool->fmr_free_list; fmr != NULL; fmr = fmr_next) {
		fmr_next = fmr->fmr_next;

		(void) mcxnex_mr_dealloc_fmr(state, &fmr->fmr);
		kmem_free(fmr, sizeof (mcxnex_fmr_list_t));
	}
	mutex_exit(&fmrpool->fmr_lock);

	ddi_taskq_destroy(fmrpool->fmr_taskq);
	mutex_destroy(&fmrpool->fmr_lock);

	kmem_free(fmrpool, sizeof (*fmrpool));
	return (DDI_SUCCESS);
}

/*
 * mcxnex_flush_fmr_pool()
 * Ensure that all unmapped FMRs are fully invalidated.
 *     Context: Can be called from kernel context only.
 */
int
mcxnex_flush_fmr_pool(mcxnex_state_t *state, mcxnex_fmrhdl_t fmrpool)
{
	int		status;

	/*
	 * Force the unmapping of all entries on the dirty list, regardless of
	 * whether the watermark has been hit yet.
	 */
	/* grab the pool lock */
	mutex_enter(&fmrpool->fmr_lock);
	status = mcxnex_fmr_cleanup(state, fmrpool);
	mutex_exit(&fmrpool->fmr_lock);
	return (status);
}

/*
 * mcxnex_deregister_fmr()
 * Map memory into FMR
 *    Context: Can be called from interrupt or base context.
 */
int
mcxnex_register_physical_fmr(mcxnex_state_t *state, mcxnex_fmrhdl_t fmrpool,
    ibt_pmr_attr_t *mem_pattr, mcxnex_mrhdl_t *mr,
    ibt_pmr_desc_t *mem_desc_p)
{
	mcxnex_fmr_list_t	*fmr;
	mcxnex_fmr_list_t	query;
	avl_index_t		where;
	int			status;

	/* Check length */
	mutex_enter(&fmrpool->fmr_lock);
	if (mem_pattr->pmr_len < 1 || (mem_pattr->pmr_num_buf >
	    fmrpool->fmr_max_pages)) {
		mutex_exit(&fmrpool->fmr_lock);
		return (IBT_MR_LEN_INVALID);
	}

	mutex_enter(&fmrpool->fmr_cachelock);
	/* lookup in fmr cache */
	/* if exists, grab it, and return it */
	if (fmrpool->fmr_cache) {
		query.fmr_desc.pmd_iova = mem_pattr->pmr_iova;
		query.fmr_desc.pmd_phys_buf_list_sz = mem_pattr->pmr_len;
		fmr = (mcxnex_fmr_list_t *)avl_find(&fmrpool->fmr_cache_avl,
		    &query, &where);

		/*
		 * If valid FMR was found in cache, return that fmr info
		 */
		if (fmr != NULL) {
			fmr->fmr_refcnt++;
			/* Store pmr desc for use in cache */
			(void) memcpy(mem_desc_p, &fmr->fmr_desc,
			    sizeof (ibt_pmr_desc_t));
			*mr = (mcxnex_mrhdl_t)fmr->fmr;
			mutex_exit(&fmrpool->fmr_cachelock);
			mutex_exit(&fmrpool->fmr_lock);
			return (DDI_SUCCESS);
		}
	}

	/* FMR does not exist in cache, proceed with registration */

	/* grab next free entry */
	fmr = fmrpool->fmr_free_list;
	if (fmr == NULL) {
		mutex_exit(&fmrpool->fmr_cachelock);
		mutex_exit(&fmrpool->fmr_lock);
		return (IBT_INSUFF_RESOURCE);
	}

	fmrpool->fmr_free_list = fmrpool->fmr_free_list->fmr_next;
	fmr->fmr_next = NULL;

	status = mcxnex_mr_register_physical_fmr(state, mem_pattr, fmr->fmr,
	    mem_desc_p);
	if (status != DDI_SUCCESS) {
		mutex_exit(&fmrpool->fmr_cachelock);
		mutex_exit(&fmrpool->fmr_lock);
		return (status);
	}

	fmr->fmr_refcnt = 1;
	fmr->fmr_remaps++;

	/* Store pmr desc for use in cache */
	(void) memcpy(&fmr->fmr_desc, mem_desc_p, sizeof (ibt_pmr_desc_t));
	*mr = (mcxnex_mrhdl_t)fmr->fmr;

	/* Store in cache */
	if (fmrpool->fmr_cache) {
		if (!fmr->fmr_in_cache) {
			avl_insert(&fmrpool->fmr_cache_avl, fmr, where);
			fmr->fmr_in_cache = 1;
		}
	}

	mutex_exit(&fmrpool->fmr_cachelock);
	mutex_exit(&fmrpool->fmr_lock);
	return (DDI_SUCCESS);
}

/*
 * mcxnex_deregister_fmr()
 * Unmap FMR
 *    Context: Can be called from kernel context only.
 */
int
mcxnex_deregister_fmr(mcxnex_state_t *state, mcxnex_mrhdl_t mr)
{
	mcxnex_fmr_list_t	*fmr;
	mcxnex_fmrhdl_t		fmrpool;
	int			status;

	fmr = mr->mr_fmr;
	fmrpool = fmr->fmr_pool;

	/* Grab pool lock */
	mutex_enter(&fmrpool->fmr_lock);
	fmr->fmr_refcnt--;

	if (fmr->fmr_refcnt == 0) {
		/*
		 * First, do some bit of invalidation, reducing our exposure to
		 * having this region still registered in hardware.
		 */
		(void) mcxnex_mr_invalidate_fmr(state, mr);

		/*
		 * If we've exhausted our remaps then add the FMR to the dirty
		 * list, not allowing it to be re-used until we have done a
		 * flush.  Otherwise, simply add it back to the free list for
		 * re-mapping.
		 */
		if (fmr->fmr_remaps <
		    state->hs_cfg_profile->cp_fmr_max_remaps) {
			/* add to free list */
			fmr->fmr_next = fmrpool->fmr_free_list;
			fmrpool->fmr_free_list = fmr;
		} else {
			/* add to dirty list */
			fmr->fmr_next = fmrpool->fmr_dirty_list;
			fmrpool->fmr_dirty_list = fmr;
			fmrpool->fmr_dirty_len++;

			status = ddi_taskq_dispatch(fmrpool->fmr_taskq,
			    mcxnex_fmr_processing, fmrpool, DDI_NOSLEEP);
			if (status == DDI_FAILURE) {
				mutex_exit(&fmrpool->fmr_lock);
				return (IBT_INSUFF_RESOURCE);
			}
		}
	}
	/* Release pool lock */
	mutex_exit(&fmrpool->fmr_lock);

	return (DDI_SUCCESS);
}


/*
 * mcxnex_fmr_processing()
 * If required, perform cleanup.
 *     Context: Called from taskq context only.
 */
static void
mcxnex_fmr_processing(void *fmr_args)
{
	mcxnex_fmrhdl_t		fmrpool;
	int			status;

	ASSERT(fmr_args != NULL);

	fmrpool = (mcxnex_fmrhdl_t)fmr_args;

	/* grab pool lock */
	mutex_enter(&fmrpool->fmr_lock);
	if (fmrpool->fmr_dirty_len >= fmrpool->fmr_dirty_watermark) {
		status = mcxnex_fmr_cleanup(fmrpool->fmr_state, fmrpool);
		if (status != DDI_SUCCESS) {
			mutex_exit(&fmrpool->fmr_lock);
			return;
		}

		if (fmrpool->fmr_flush_function != NULL) {
			(void) fmrpool->fmr_flush_function(
			    (ibc_fmr_pool_hdl_t)fmrpool,
			    fmrpool->fmr_flush_arg);
		}
	}

	/* let pool lock go */
	mutex_exit(&fmrpool->fmr_lock);
}

/*
 * mcxnex_fmr_cleanup()
 * Perform cleaning processing, walking the list and performing the MTT sync
 * operation if required.
 *    Context: can be called from taskq or base context.
 */
static int
mcxnex_fmr_cleanup(mcxnex_state_t *state, mcxnex_fmrhdl_t fmrpool)
{
	mcxnex_fmr_list_t	*fmr;
	mcxnex_fmr_list_t	*fmr_next;
	int			sync_needed;
	int			status;

	ASSERT(MUTEX_HELD(&fmrpool->fmr_lock));

	sync_needed = 0;
	for (fmr = fmrpool->fmr_dirty_list; fmr; fmr = fmr_next) {
		fmr_next = fmr->fmr_next;
		fmr->fmr_remaps = 0;

		(void) mcxnex_mr_deregister_fmr(state, fmr->fmr);

		/*
		 * Update lists.
		 * - add fmr back to free list
		 * - remove fmr from dirty list
		 */
		fmr->fmr_next = fmrpool->fmr_free_list;
		fmrpool->fmr_free_list = fmr;


		/*
		 * Because we have updated the dirty list, and deregistered the
		 * FMR entry, we do need to sync the TPT, so we set the
		 * 'sync_needed' flag here so we sync once we finish dirty_list
		 * processing.
		 */
		sync_needed = 1;
	}

	fmrpool->fmr_dirty_list = NULL;
	fmrpool->fmr_dirty_len = 0;

	if (sync_needed) {
		status = mcxnex_sync_tpt_cmd_post(state, DDI_NOSLEEP);
		if (status != MCXNEX_CMD_SUCCESS) {
			return (status);
		}
	}

	return (DDI_SUCCESS);
}

/*
 * mcxnex_fmr_avl_compare()
 *    Context: Can be called from user or kernel context.
 */
static int
mcxnex_fmr_avl_compare(const void *q, const void *e)
{
	mcxnex_fmr_list_t *entry, *query;

	entry = (mcxnex_fmr_list_t *)e;
	query = (mcxnex_fmr_list_t *)q;

	if (query->fmr_desc.pmd_iova < entry->fmr_desc.pmd_iova) {
		return (-1);
	} else if (query->fmr_desc.pmd_iova > entry->fmr_desc.pmd_iova) {
		return (+1);
	} else {
		return (0);
	}
}


/*
 * mcxnex_fmr_cache_init()
 *    Context: Can be called from user or kernel context.
 */
static void
mcxnex_fmr_cache_init(mcxnex_fmrhdl_t fmr)
{
	/* Initialize the lock used for FMR cache AVL tree access */
	mutex_init(&fmr->fmr_cachelock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(fmr->fmr_state->hs_intrmsi_pri));

	/* Initialize the AVL tree for the FMR cache */
	avl_create(&fmr->fmr_cache_avl, mcxnex_fmr_avl_compare,
	    sizeof (mcxnex_fmr_list_t),
	    offsetof(mcxnex_fmr_list_t, fmr_avlnode));

	fmr->fmr_cache = 1;
}


/*
 * mcxnex_fmr_cache_fini()
 *    Context: Can be called from user or kernel context.
 */
static void
mcxnex_fmr_cache_fini(mcxnex_fmrhdl_t fmr)
{
	void			*cookie;

	/*
	 * Empty all entries (if necessary) and destroy the AVL tree.
	 * The FMRs themselves are freed as part of destroy_pool()
	 */
	cookie = NULL;
	while (((void *)(mcxnex_fmr_list_t *)avl_destroy_nodes(
	    &fmr->fmr_cache_avl, &cookie)) != NULL) {
		/* loop through */
	}
	avl_destroy(&fmr->fmr_cache_avl);

	/* Destroy the lock used for FMR cache */
	mutex_destroy(&fmr->fmr_cachelock);
}

/*
 * mcxnex_get_dma_cookies()
 * Return DMA cookies in the pre-allocated paddr_list_p based on the length
 * needed.
 *    Context: Can be called from interrupt or base context.
 */
int
mcxnex_get_dma_cookies(mcxnex_state_t *state, ibt_phys_buf_t *paddr_list_p,
    ibt_va_attr_t *va_attrs, uint_t list_len, uint_t *cookiecnt,
    ibc_ma_hdl_t *ibc_ma_hdl_p)
{
	ddi_dma_handle_t	dma_hdl;
	ddi_dma_attr_t		dma_attr;
	ddi_dma_cookie_t	dmacookie;
	int			(*callback)(caddr_t);
	int			status;
	int			i;

	/* Set the callback flag appropriately */
	callback = (va_attrs->va_flags & IBT_VA_NOSLEEP) ? DDI_DMA_DONTWAIT :
	    DDI_DMA_SLEEP;
	if ((callback == DDI_DMA_SLEEP) &&
	    (DDI_SLEEP != MCXNEX_SLEEPFLAG_FOR_CONTEXT())) {
		return (IBT_INVALID_PARAM);
	}

	/*
	 * Initialize many of the default DMA attributes and allocate the DMA
	 * handle.  Then, if we're bypassing the IOMMU, set the
	 * DDI_DMA_FORCE_PHYSICAL flag.
	 */
	mcxnex_dma_attr_init(state, &dma_attr);

#ifdef __x86
	/*
	 * On x86 we can specify a maximum segment length for our returned
	 * cookies.
	 */
	if (va_attrs->va_flags & IBT_VA_FMR) {
		dma_attr.dma_attr_seg = PAGESIZE - 1;
	}
#endif

	/*
	 * Check to see if the RO flag is set, and if so,
	 * set that bit in the attr structure as well.
	 *
	 * NOTE 1:  This function is ONLY called by consumers, and only for
	 *	    data buffers
	 */
	if (mcxnex_kernel_data_ro == MCXNEX_RO_ENABLED) {
		dma_attr.dma_attr_flags |= DDI_DMA_RELAXED_ORDERING;
	}

	status = ddi_dma_alloc_handle(state->hs_dip, &dma_attr,
	    callback, NULL, &dma_hdl);
	if (status != DDI_SUCCESS) {
		switch (status) {
		case DDI_DMA_NORESOURCES:
			return (IBT_INSUFF_RESOURCE);
		case DDI_DMA_BADATTR:
		default:
			return (DDI_FAILURE);
		}
	}

	/*
	 * Now bind the handle with the correct DMA attributes.
	 */
	if (va_attrs->va_flags & IBT_VA_BUF) {
		status = ddi_dma_buf_bind_handle(dma_hdl, va_attrs->va_buf,
		    DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_DONTWAIT,
		    NULL, &dmacookie, cookiecnt);
	} else {
		status = ddi_dma_addr_bind_handle(dma_hdl, NULL,
		    (caddr_t)(uintptr_t)va_attrs->va_vaddr, va_attrs->va_len,
		    DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_DONTWAIT,
		    NULL, &dmacookie, cookiecnt);
	}
	if (status != DDI_SUCCESS) {
		ddi_dma_free_handle(&dma_hdl);

		switch (status) {
		case DDI_DMA_NORESOURCES:
			return (IBT_INSUFF_RESOURCE);
		case DDI_DMA_TOOBIG:
			return (IBT_INVALID_PARAM);
		case DDI_DMA_PARTIAL_MAP:
		case DDI_DMA_INUSE:
		case DDI_DMA_NOMAPPING:
		default:
			return (DDI_FAILURE);
		}
	}

	/*
	 * Verify our physical buffer list (PBL) is large enough to handle the
	 * number of cookies that were returned.
	 */
	if (*cookiecnt > list_len) {
		(void) ddi_dma_unbind_handle(dma_hdl);
		ddi_dma_free_handle(&dma_hdl);
		return (IBT_PBL_TOO_SMALL);
	}

	/*
	 * We store the cookies returned by the DDI into our own PBL.  This
	 * sets the cookies up for later processing (for example, if we want to
	 * split up the cookies into smaller chunks).  We use the laddr and
	 * size fields in each cookie to create each individual entry (PBE).
	 */

	/*
	 * Store first cookie info first
	 */
	paddr_list_p[0].p_laddr = dmacookie.dmac_laddress;
	paddr_list_p[0].p_size = dmacookie.dmac_size;

	/*
	 * Loop through each cookie, storing each cookie into our physical
	 * buffer list.
	 */
	for (i = 1; i < *cookiecnt; i++) {
		ddi_dma_nextcookie(dma_hdl, &dmacookie);

		paddr_list_p[i].p_laddr = dmacookie.dmac_laddress;
		paddr_list_p[i].p_size  = dmacookie.dmac_size;
	}

	/* return handle */
	*ibc_ma_hdl_p = (ibc_ma_hdl_t)dma_hdl;
	return (DDI_SUCCESS);
}

/*
 * mcxnex_split_dma_cookies()
 * Split up cookies passed in from paddr_list_p, returning the new list in the
 * same buffers, based on the pagesize to split the cookies into.
 *    Context: Can be called from interrupt or base context.
 */
/* ARGSUSED */
int
mcxnex_split_dma_cookies(mcxnex_state_t *state, ibt_phys_buf_t *paddr_list,
    ib_memlen_t *paddr_offset, uint_t list_len, uint_t *cookiecnt,
    uint_t pagesize)
{
	uint64_t	pageoffset;
	uint64_t	pagemask;
	uint_t		pageshift;
	uint_t		current_cookiecnt;
	uint_t		cookies_needed;
	uint64_t	last_size, extra_cookie;
	int		i_increment;
	int		i, k;
	int		status;

	/* Setup pagesize calculations */
	pageoffset = pagesize - 1;
	pagemask = (~pageoffset);
	pageshift = highbit(pagesize) - 1;

	/*
	 * Setup first cookie offset based on pagesize requested.
	 */
	*paddr_offset = paddr_list[0].p_laddr & pageoffset;
	paddr_list[0].p_laddr &= pagemask;

	/* Save away the current number of cookies that are passed in */
	current_cookiecnt = *cookiecnt;

	/* Perform splitting up of current cookies into pagesize blocks */
	for (i = 0; i < current_cookiecnt; i += i_increment) {
		/*
		 * If the cookie is smaller than pagesize, or already is
		 * pagesize, then we are already within our limits, so we skip
		 * it.
		 */
		if (paddr_list[i].p_size <= pagesize) {
			i_increment = 1;
			continue;
		}

		/*
		 * If this is our first cookie, then we have to deal with the
		 * offset that may be present in the first address.  So add
		 * that to our size, to calculate potential change to the last
		 * cookie's size.
		 *
		 * Also, calculate the number of cookies that we'll need to
		 * split up this block into.
		 */
		if (i == 0) {
			last_size = (paddr_list[i].p_size + *paddr_offset) &
			    pageoffset;
			cookies_needed = (paddr_list[i].p_size +
			    *paddr_offset) >> pageshift;
		} else {
			last_size = 0;
			cookies_needed = paddr_list[i].p_size >> pageshift;
		}

		/*
		 * If our size is not a multiple of pagesize, we need one more
		 * cookie.
		 */
		if (last_size) {
			extra_cookie = 1;
		} else {
			extra_cookie = 0;
		}

		/*
		 * Split cookie into pagesize chunks, shifting list of cookies
		 * down, using more cookie slots in the PBL if necessary.
		 */
		status = mcxnex_dma_cookie_shift(paddr_list, i, list_len,
		    current_cookiecnt - i, cookies_needed + extra_cookie);
		if (status != 0) {
			return (status);
		}

		/*
		 * If the very first cookie, we must take possible offset into
		 * account.
		 */
		if (i == 0) {
			paddr_list[i].p_size = pagesize - *paddr_offset;
		} else {
			paddr_list[i].p_size = pagesize;
		}

		/*
		 * We have shifted the existing cookies down the PBL, now fill
		 * in the blank entries by splitting up our current block.
		 */
		for (k = 1; k < cookies_needed; k++) {
			paddr_list[i + k].p_laddr =
			    paddr_list[i + k - 1].p_laddr + pagesize;
			paddr_list[i + k].p_size = pagesize;
		}

		/* If we have one extra cookie (of less than pagesize...) */
		if (extra_cookie) {
			paddr_list[i + k].p_laddr =
			    paddr_list[i + k - 1].p_laddr + pagesize;
			paddr_list[i + k].p_size = (size_t)last_size;
		}

		/* Increment cookiecnt appropriately based on cookies used */
		i_increment = cookies_needed + extra_cookie;
		current_cookiecnt += i_increment - 1;
	}

	/* Update to new cookie count */
	*cookiecnt = current_cookiecnt;
	return (DDI_SUCCESS);
}

/*
 * mcxnex_dma_cookie_shift()
 *    Context: Can be called from interrupt or base context.
 */
int
mcxnex_dma_cookie_shift(ibt_phys_buf_t *paddr_list, int start, int end,
    int cookiecnt, int num_shift)
{
	int shift_start;
	int i;

	/* Calculating starting point in the PBL list */
	shift_start = start + cookiecnt - 1;

	/* Check if we're at the end of our PBL list */
	if ((shift_start + num_shift - 1) >= end) {
		return (IBT_PBL_TOO_SMALL);
	}

	for (i = shift_start; i > start; i--) {
		paddr_list[i + num_shift - 1] = paddr_list[i];
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_free_dma_cookies()
 *    Context: Can be called from interrupt or base context.
 */
int
mcxnex_free_dma_cookies(ibc_ma_hdl_t ma_hdl)
{
	ddi_dma_handle_t	dma_hdl;
	int			status;

	dma_hdl = (ddi_dma_handle_t)ma_hdl;

	status = ddi_dma_unbind_handle(dma_hdl);
	if (status != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	ddi_dma_free_handle(&dma_hdl);

	return (DDI_SUCCESS);
}
