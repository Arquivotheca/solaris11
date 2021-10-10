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

#ifndef	_MCXNEX_MR_H
#define	_MCXNEX_MR_H

/*
 * mcxnex_mr.h
 *    Contains all of the prototypes, #defines, and structures necessary
 *    for the Mcxnex Memory Region/Window routines.
 *    Specifically it contains #defines, macros, and prototypes for each of
 *    the required memory region/window verbs that can be accessed through
 *    the IBTF's CI interfaces.  In particular each of the prototypes defined
 *    below is called from a corresponding CI interface routine (as specified
 *    in the mcxnex_ci.c file).
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The following defines specify the default number of MPT entries to
 * configure. This value is controllable through the "mcxnex_log_num_mpt"
 * configuration variable.
 */
#define	MCXNEX_NUM_DMPT_SHIFT		0x06 /* 0x16 for IB */

/*
 * The following defines specify the default number of MPT entries to
 * configure. This value is controllable through the "mcxnex_log_num_mtt"
 * configuration variable. This default value expects an averages of 8
 * MTTs per MPT. We also define a log MTT size, since it's not likely
 * to change.
 */
#define	MCXNEX_NUM_MTT_SHIFT		0x0d /* 0x1a for IB */
#define	MCXNEX_MTT_SIZE_SHIFT		0x3

/*
 * This define is the maximum size of a memory region or window (log 2), which
 * is used to initialize the "mcxnex_log_max_mrw_sz" configuration variable.
 */
#define	MCXNEX_MAX_MEM_MPT_SHIFT			0x24

/*
 * Defines used by mcxnex_mr_deregister() to specify how much/to what extent
 * a given memory regions resources should be freed up.  MCXNEX_MR_DEREG_ALL
 * says what it means, free up all the resources associated with the region.
 * MCXNEX_MR_DEREG_NO_HW2SW_MPT indicates that it is unnecessary to attempt
 * the ownership transfer (from hardware to software) for the given MPT entry.
 * And MCXNEX_MR_DEREG_NO_HW2SW_MPT_OR_UNBIND indicates that it is not only
 * unnecessary to attempt the ownership transfer for MPT, but it is also
 * unnecessary to attempt to unbind the memory.
 * In general, these last two are specified when mcxnex_mr_deregister() is
 * called from mcxnex_mr_reregister(), where the MPT ownership transfer or
 * memory unbinding may have already been successfully performed.
 */
#define	MCXNEX_MR_DEREG_ALL			3
#define	MCXNEX_MR_DEREG_NO_HW2SW_MPT		2
#define	MCXNEX_MR_DEREG_NO_HW2SW_MPT_OR_UNBIND	1

/*
 * The following define is used by mcxnex_mr_rereg_xlat_helper() to determine
 * whether or not a given DMA handle can be reused.  If the DMA handle was
 * previously initialized for IOMMU bypass mapping, then it can not be reused
 * to reregister a region for DDI_DMA_STREAMING access.
 */
#define	MCXNEX_MR_REUSE_DMAHDL(mr, flags)				\
	(((mr)->mr_bindinfo.bi_bypass != MCXNEX_BINDMEM_BYPASS) ||	\
	    !((flags) & IBT_MR_NONCOHERENT))

/*
 * The mcxnex_sw_refcnt_t structure is used internally by the Mcxnex driver to
 * track all the information necessary to manage shared memory regions.  Since
 * a shared memory region _will_ have its own distinct MPT entry, but will
 * _share_ its MTT entries with another region, it is necessary to track the
 * number of times a given MTT structure is shared.  This ensures that it will
 * not be prematurely freed up and that can be destroyed only when it is
 * appropriate to do so.
 *
 * Each mcxnex_sw_refcnt_t structure contains a lock and a reference count
 * variable which are used to track the necessary information.
 *
 * The following macros (below) are used to manipulate and query the MTT
 * reference count parameters.  MCXNEX_MTT_REFCNT_INIT() is used to initialize
 * a newly allocated mcxnex_sw_refcnt_t struct (setting the "swrc_refcnt" to 1).
 * And the MCXNEX_MTT_IS_NOT_SHARED() and MCXNEX_MTT_IS_SHARED() macros are
 * used to query the current status of mcxnex_sw_refcnt_t struct to determine
 * if its "swrc_refcnt" is one or not.
 */
typedef struct mcxnex_sw_refcnt_s {
	kmutex_t		swrc_lock;
	uint_t			swrc_refcnt;
} mcxnex_sw_refcnt_t;
_NOTE(DATA_READABLE_WITHOUT_LOCK(mcxnex_sw_refcnt_t::swrc_refcnt))
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_sw_refcnt_t::swrc_lock,
    mcxnex_sw_refcnt_t::swrc_refcnt))
#define	MCXNEX_MTT_REFCNT_INIT(swrc_tmp)	((swrc_tmp)->swrc_refcnt = 1)
#define	MCXNEX_MTT_IS_NOT_SHARED(swrc_tmp)	((swrc_tmp)->swrc_refcnt == 1)
#define	MCXNEX_MTT_IS_SHARED(swrc_tmp)		((swrc_tmp)->swrc_refcnt != 1)


/*
 * The mcxnex_bind_info_t structure is used internally by the Mcxnex driver to
 * track all the information necessary to perform the DMA mappings necessary
 * for memory registration.  It is specifically passed into both the
 * mcxnex_mr_mem_bind() and mcxnex_mr_mtt_write() functions which perform most
 * of the necessary operations for Mcxnex memory registration.
 *
 * This structure is used to pass all the information necessary for a call
 * to either ddi_dma_addr_bind_handle() or ddi_dma_buf_bind_handle().  Note:
 * the fields which need to be valid for each type of binding are slightly
 * different and that it indicated by the value in the "bi_type" field.  The
 * "bi_type" field may be set to either of the following defined values:
 * MCXNEX_BINDHDL_VADDR (to indicate an "addr" bind) or MCXNEX_BINDHDL_BUF (to
 * indicate a "buf" bind).
 *
 * Upon return from mcxnex_mr_mem_bind(), the mcxnex_bind_info_t struct will
 * have its "bi_dmahdl", "bi_dmacookie", and "bi_cookiecnt" fields filled in.
 * It is these values which are of particular interest to the
 * mcxnex_mr_mtt_write() routine (they hold the PCI mapped addresses).
 *
 * Once initialized and used in this way, the mcxnex_bind_info_t will not to be
 * modified in anyway until it is subsequently passed to mcxnex_mr_mem_unbind()
 * where the memory and resources will be unbound and reclaimed.  Note:  the
 * "bi_free_dmahdl" flag indicated whether the ddi_dma_handle_t should be
 * freed as part of the mcxnex_mr_mem_unbind() operation or whether it will
 * be freed later elsewhere.
 */
typedef struct mcxnex_bind_info_s {
	uint64_t		bi_addr;
	uint64_t		bi_len;
	struct as		*bi_as;
	struct buf		*bi_buf;
	ddi_dma_handle_t	bi_dmahdl;
	ddi_dma_cookie_t	bi_dmacookie;
	uint_t			bi_cookiecnt;
	uint_t			bi_type;
	uint_t			bi_flags;
	uint_t			bi_bypass;
	uint_t			bi_free_dmahdl;
} mcxnex_bind_info_t;
#define	MCXNEX_BINDHDL_NONE		0
#define	MCXNEX_BINDHDL_VADDR		1
#define	MCXNEX_BINDHDL_BUF		2
#define	MCXNEX_BINDHDL_UBUF		3

/*
 * The mcxnex_sw_mr_s structure is also referred to using the "mcxnex_mrhdl_t"
 * typedef (see mcxnex_typedef.h).  It encodes all the information necessary
 * to track the various resources needed to register, reregister, deregister,
 * and perform all the myriad other operations on both memory regions _and_
 * memory windows.
 *
 * A pointer to this structure is returned from many of the IBTF's CI verbs
 * interfaces for memory registration.
 *
 * It contains pointers to the various resources allocated for a memory
 * region, i.e. MPT resource, MTT resource, and MTT reference count resource.
 * In addition it contains the mcxnex_bind_info_t struct used for the memory
 * bind operation on a given memory region.
 *
 * It also has a pointers to the associated PD handle, placeholders for access
 * flags, memory keys, and suggested page size for the region.  It also has
 * the necessary backpointer to the resource that corresponds to the structure
 * itself.  And lastly, it contains a placeholder for a callback which should
 * be called on memory region unpinning.
 */
struct mcxnex_sw_mr_s {
	kmutex_t		mr_lock;
	mcxnex_rsrc_t		*mr_mptrsrcp;
	mcxnex_rsrc_t		*mr_mttrsrcp;
	mcxnex_rsrc_t		*mr_mttrefcntp;
	mcxnex_pdhdl_t		mr_pdhdl;
	mcxnex_bind_info_t	mr_bindinfo;
	ibt_mr_attr_flags_t	mr_accflag;
	uint32_t		mr_lkey;
	uint32_t		mr_rkey;
	uint32_t		mr_logmttpgsz;
	mcxnex_mpt_rsrc_type_t	mr_mpt_type;
	uint64_t		mr_mttaddr;	/* for cMPTs */
	uint64_t		mr_log2_pgsz;
				/* entity_size (in bytes), for cMPTS */
	mcxnex_rsrc_t		*mr_rsrcp;
	uint_t			mr_is_fmr;
	mcxnex_fmr_list_t	*mr_fmr;
	uint_t			mr_is_umem;
	ddi_umem_cookie_t	mr_umemcookie;
	void 			(*mr_umem_cbfunc)(void *, void *);
	void			*mr_umem_cbarg1;
	void			*mr_umem_cbarg2;
};
_NOTE(DATA_READABLE_WITHOUT_LOCK(mcxnex_sw_mr_s::mr_bindinfo
    mcxnex_sw_mr_s::mr_lkey
    mcxnex_sw_mr_s::mr_is_umem
    mcxnex_sw_mr_s::mr_is_fmr
    mcxnex_sw_mr_s::mr_fmr))
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_sw_mr_s::mr_lock,
    mcxnex_sw_mr_s::mr_mptrsrcp
    mcxnex_sw_mr_s::mr_mttrsrcp
    mcxnex_sw_mr_s::mr_mttrefcntp
    mcxnex_sw_mr_s::mr_bindinfo
    mcxnex_sw_mr_s::mr_lkey
    mcxnex_sw_mr_s::mr_rkey
    mcxnex_sw_mr_s::mr_logmttpgsz
    mcxnex_sw_mr_s::mr_rsrcp
    mcxnex_sw_mr_s::mr_is_umem
    mcxnex_sw_mr_s::mr_umemcookie
    mcxnex_sw_mr_s::mr_umem_cbfunc
    mcxnex_sw_mr_s::mr_umem_cbarg1
    mcxnex_sw_mr_s::mr_umem_cbarg2))

/*
 * The mcxnex_mr_options_t structure is used in several of the Mcxnex memory
 * registration routines to provide additional option functionality.  When
 * a NULL pointer is passed in place of a pointer to this struct, it is a
 * way of specifying the "default" behavior.  Using this structure, however,
 * is a way of controlling any extended behavior.
 *
 * Currently, the only defined "extended" behaviors are for specifying whether
 * a given memory region should bypass the PCI IOMMU (MCXNEX_BINDMEM_BYPASS)
 * or be mapped into the IOMMU (MCXNEX_BINDMEM_NORMAL), for specifying whether
 * a given ddi_dma_handle_t should be used in the bind operation, and for
 * specifying whether a memory registration should attempt to return an IB
 * vaddr which is "zero-based" (aids in alignment contraints for QPs).
 *
 * This defaults today to always bypassing the IOMMU (can be changed by using
 * the "mcxnex_iommu_bypass" configuration variable), to always allocating
 * a new dma handle, and to using the virtual address passed in (i.e. not
 * "zero-based").
 */
typedef struct mcxnex_mr_options_s {
	ddi_dma_handle_t	mro_bind_dmahdl;
	uint_t			mro_bind_type;
	uint_t			mro_bind_override_addr;
} mcxnex_mr_options_t;
#define	MCXNEX_BINDMEM_NORMAL		1
#define	MCXNEX_BINDMEM_BYPASS		0

#define	MCXNEX_NO_MPT_OWNERSHIP		0	/* for cMPTs */
#define	MCXNEX_PASS_MPT_OWNERSHIP	1

/*
 * Memory Allocation/Deallocation
 *
 * Although this is not strictly related to "memory regions", this is
 * the most logical place to define the struct used for the memory
 * allocation/deallocation CI entry points.
 *
 * ibc_mem_alloc_s structure is used to store DMA handles for
 * for these allocations.
 */
struct ibc_mem_alloc_s {
	ddi_dma_handle_t ibc_dma_hdl;
	ddi_acc_handle_t ibc_acc_hdl;
};
_NOTE(SCHEME_PROTECTS_DATA("safe sharing",
    ibc_mem_alloc_s::ibc_dma_hdl
    ibc_mem_alloc_s::ibc_acc_hdl))

int mcxnex_mr_register(mcxnex_state_t *state, mcxnex_pdhdl_t pdhdl,
    ibt_mr_attr_t *attr_p, mcxnex_mrhdl_t *mrhdl, mcxnex_mr_options_t *op,
    mcxnex_mpt_rsrc_type_t mpt_type);
int mcxnex_mr_register_buf(mcxnex_state_t *state, mcxnex_pdhdl_t pdhdl,
    ibt_smr_attr_t *attrp, struct buf *buf, mcxnex_mrhdl_t *mrhdl,
    mcxnex_mr_options_t *op, mcxnex_mpt_rsrc_type_t mpt_type);
int mcxnex_mr_mtt_bind(mcxnex_state_t *state, mcxnex_bind_info_t *bind,
    ddi_dma_handle_t bind_dmahdl, mcxnex_rsrc_t **mtt, uint_t *mtt_pgsz_bits,
    uint_t is_buffer);
int mcxnex_mr_mtt_unbind(mcxnex_state_t *state, mcxnex_bind_info_t *bind,
    mcxnex_rsrc_t *mtt);
int mcxnex_mr_register_shared(mcxnex_state_t *state, mcxnex_mrhdl_t mrhdl,
    mcxnex_pdhdl_t pdhdl, ibt_smr_attr_t *attr_p, mcxnex_mrhdl_t *mrhdl_new);
int mcxnex_mr_deregister(mcxnex_state_t *state, mcxnex_mrhdl_t *mrhdl,
    uint_t level, uint_t sleep);
int mcxnex_mr_query(mcxnex_state_t *state, mcxnex_mrhdl_t mrhdl,
    ibt_mr_query_attr_t *attr);
int mcxnex_mr_reregister(mcxnex_state_t *state, mcxnex_mrhdl_t mrhdl,
    mcxnex_pdhdl_t pdhdl, ibt_mr_attr_t *attr_p, mcxnex_mrhdl_t *mrhdl_new,
    mcxnex_mr_options_t *op);
int mcxnex_mr_reregister_buf(mcxnex_state_t *state, mcxnex_mrhdl_t mr,
    mcxnex_pdhdl_t pd, ibt_smr_attr_t *mr_attr, struct buf *buf,
    mcxnex_mrhdl_t *mrhdl_new, mcxnex_mr_options_t *op);
int mcxnex_mr_sync(mcxnex_state_t *state, ibt_mr_sync_t *mr_segs,
    size_t num_segs);
int mcxnex_mw_alloc(mcxnex_state_t *state, mcxnex_pdhdl_t pdhdl,
    ibt_mw_flags_t flags, mcxnex_mwhdl_t *mwhdl);
int mcxnex_mw_free(mcxnex_state_t *state, mcxnex_mwhdl_t *mwhdl, uint_t sleep);
uint32_t mcxnex_mr_keycalc(uint32_t indx);
uint32_t mcxnex_mr_key_swap(uint32_t indx);
uint32_t mcxnex_index_to_mkey(uint32_t indx);
int mcxnex_mr_alloc_fmr(mcxnex_state_t *state, mcxnex_pdhdl_t pd,
    mcxnex_fmrhdl_t fmr_pool, mcxnex_mrhdl_t *mrhdl);
int mcxnex_mr_dealloc_fmr(mcxnex_state_t *state, mcxnex_mrhdl_t *mrhdl);
int mcxnex_mr_register_physical_fmr(mcxnex_state_t *state,
    ibt_pmr_attr_t *mem_pattr_p, mcxnex_mrhdl_t mr, ibt_pmr_desc_t *mem_desc_p);
int mcxnex_mr_invalidate_fmr(mcxnex_state_t *state, mcxnex_mrhdl_t mr);
int mcxnex_mr_deregister_fmr(mcxnex_state_t *state, mcxnex_mrhdl_t mr);


#ifdef __cplusplus
}
#endif

#endif	/* _MCXNEX_MR_H */
