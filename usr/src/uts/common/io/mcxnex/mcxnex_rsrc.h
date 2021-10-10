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

#ifndef	_MCXNEX_RSRC_H
#define	_MCXNEX_RSRC_H

/*
 * mcxnex_rsrc.h
 *    Contains all of the prototypes, #defines, and structures necessary
 *    for the Mcxnex Resource Management routines.
 *    Specifically it contains the resource names, resource types, and
 *    structures used for enabling both init/fini and alloc/free operations.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/disp.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The above extern and the following #defines and macro are used to determine
 * the current context for purposes of setting the sleepflag.  If the calling
 * thread is running in the interrupt context, then macro will return
 * MCXNEX_NOSLEEP (indicating that it is not appropriate to sleep in the current
 * context.  In all other cases, this macro will return MCXNEX_SLEEP.
 *
 * The DDI_SLEEP and MCXNEX_CMD_NOSLEEP_SPIN #defines from
 * mcxnex_cmd.h are set to use and be compatible with the following SLEEP
 * variables.  It is important that these remain in sync so that the
 * MCXNEX_SLEEPFLAG_FOR_CONTEXT() macro will work in all cases.
 */
#define	MCXNEX_SLEEPFLAG_FOR_CONTEXT()					\
	((servicing_interrupt() || ddi_in_panic()) ? DDI_NOSLEEP : DDI_SLEEP)

/*
 * The following #defines are used as the names for various resource pools.
 * They represent the kmem_cache and vmem_arena names, respectively.  In
 * order to provide for unique naming when multiple Mcxnex drivers are present,
 * the MCXNEX_RSRC_NAME macro below is used to append the driver's instance
 * number to the provided string.  Note: resource names should not be longer
 * than MCXNEX_RSRC_NAME_MAXLEN.
 */


#define	MCXNEX_RSRC_CACHE		"mcxnex_rsrc_cache"
#define	MCXNEX_PDHDL_CACHE		"mcxnex_pdhdl_cache"
#define	MCXNEX_MRHDL_CACHE		"mcxnex_mrhdl_cache"
#define	MCXNEX_EQHDL_CACHE		"mcxnex_eqhdl_cache"
#define	MCXNEX_CQHDL_CACHE		"mcxnex_cqhdl_cache"
#define	MCXNEX_SRQHDL_CACHE		"mcxnex_srqhdl_cache"
#define	MCXNEX_AHHDL_CACHE		"mcxnex_ahhdl_cache"
#define	MCXNEX_QPHDL_CACHE		"mcxnex_qphdl_cache"
#define	MCXNEX_REFCNT_CACHE		"mcxnex_refcnt_cache"

#define	MCXNEX_ICM_VMEM			"mcxnex_icm_vmem"
#define	MCXNEX_INMBOX_VMEM		"mcxnex_inmbox_vmem"
#define	MCXNEX_OUTMBOX_VMEM		"mcxnex_outmbox_vmem"
#define	MCXNEX_INTR_INMBOX_VMEM		"mcxnex_intr_inmbox_vmem"
#define	MCXNEX_INTR_OUTMBOX_VMEM	"mcxnex_intr_outmbox_vmem"
/* ICM based vmem */
#define	MCXNEX_CMPT_VMEM		"mcxnex_cmpt_vmem"
#define	MCXNEX_CMPT_QPC_VMEM		"mcxnex_cmpt_qpc_vmem"
#define	MCXNEX_CMPT_SRQ_VMEM		"mcxnex_cmpt_srq_vmem"
#define	MCXNEX_CMPT_CQC_VMEM		"mcxnex_cmpt_cqc_vmem"
#define	MCXNEX_CMPT_EQC_VMEM		"mcxnex_cmpt_eqc_vmem"
#define	MCXNEX_DMPT_VMEM		"mcxnex_dmpt_vmem"
#define	MCXNEX_MTT_VMEM			"mcxnex_mtt_vmem"
#define	MCXNEX_QPC_VMEM			"mcxnex_qpc_vmem"
#define	MCXNEX_SRQC_VMEM		"mcxnex_srqc_vmem"
#define	MCXNEX_RDB_VMEM			"mcxnex_rdb_vmem"
#define	MCXNEX_CQC_VMEM			"mcxnex_cqc_vmem"
#define	MCXNEX_ALTC_VMEM		"mcxnex_altc_vmem"
#define	MCXNEX_AUXC_VMEM		"mcxnex_auxc_vmem"
#define	MCXNEX_EQC_VMEM			"mcxnex_eqc_vmem"
#define	MCXNEX_MCG_VMEM			"mcxnex_mcg_vmem"
/* Add'd vmem arenas */
#define	MCXNEX_UAR_PAGE_VMEM_ATTCH	"mcxnex_uar_pg_vmem:a"
#define	MCXNEX_UAR_PAGE_VMEM_RUNTM	"mcxnex_uar_pg_vmem:r"
#define	MCXNEX_BLUEFLAME_VMEM		"mcxnex_blueflame_vmem"
#define	MCXNEX_PDHDL_VMEM		"mcxnex_pd_vmem"

/* Macro provided for building unique naming for multiple instance  */
#define	MCXNEX_RSRC_NAME(rsrc_name, string)		\
	(void) sprintf((rsrc_name), string"%08X",	\
	    state->hs_instance)
#define	MCXNEX_RSRC_NAME_MAXLEN		0x80

/* various cMPT types - need to concatenate w/ index to find it in ICM */
typedef enum {
	MCXNEX_QP_CMPT	= 0,
	MCXNEX_SRQ_CMPT	= 1,
	MCXNEX_CQ_CMPT	= 2,
	MCXNEX_EQ_CMPT	= 3,
	MCXNEX_MPT_DMPT	= 4
} mcxnex_mpt_rsrc_type_t;


/*
 * The following enumerated type is used to capture all the various types
 * of Mcxnex resources.  Note the MCXNEX_NUM_RESOURCES type is used as a marker
 * for the end of the resource types.  No additional resources should be
 * added after this. Note also that MCXNEX_NUM_ICM_RESOURCES is used similarly,
 * indicating the number of ICM resource types. If additional ICM types are
 * added, they should be added before MERMON_NUM_ICM_RESOURCES.
 */

typedef enum {
	MCXNEX_CMPT,		/* for sizing ICM space for control MPTs */
	MCXNEX_QPC,
	MCXNEX_SRQC,
	MCXNEX_CQC,
	MCXNEX_EQC,
	MCXNEX_DMPT,
	MCXNEX_MTT,
	MCXNEX_ALTC,		/* for allocation of ICM backing memory */
	MCXNEX_AUXC,		/* for allocation of ICM backing memory */
	MCXNEX_RDB,		/* for allocation of ICM backing memory */
	MCXNEX_CMPT_QPC,	/* for allocation of ICM backing memory */
	MCXNEX_CMPT_SRQC,	/* for allocation of ICM backing memory */
	MCXNEX_CMPT_CQC,	/* for allocation of ICM backing memory */
	MCXNEX_CMPT_EQC,	/* for allocation of ICM backing memory */
	MCXNEX_MCG,		/* type 0x0E */
	/* all types above are in ICM, all below are in non-ICM */
	MCXNEX_NUM_ICM_RESOURCES,
	MCXNEX_IN_MBOX = MCXNEX_NUM_ICM_RESOURCES,
	MCXNEX_OUT_MBOX,	/* type 0x10 */
	MCXNEX_PDHDL,
	MCXNEX_MRHDL,
	MCXNEX_EQHDL,
	MCXNEX_CQHDL,
	MCXNEX_QPHDL,
	MCXNEX_SRQHDL,
	MCXNEX_AHHDL,
	MCXNEX_REFCNT,
	MCXNEX_UARPG,
	MCXNEX_INTR_IN_MBOX,
	MCXNEX_INTR_OUT_MBOX,	/* type 0x1B */
	MCXNEX_NUM_RESOURCES
} mcxnex_rsrc_type_t;

/*
 * The following enumerated type and structures are used during resource
 * initialization.  Note: The MCXNEX_RSRC_CLEANUP_ALL type is used as a marker
 * for end of the cleanup steps.  No cleanup steps should be added after
 * MCXNEX_RSRC_CLEANUP_ALL.  Any addition steps should be added before it.
 */
typedef enum {
	MCXNEX_RSRC_CLEANUP_LEVEL0,
	MCXNEX_RSRC_CLEANUP_LEVEL1,
	MCXNEX_RSRC_CLEANUP_LEVEL2,
	MCXNEX_RSRC_CLEANUP_LEVEL3,
	MCXNEX_RSRC_CLEANUP_LEVEL4,
	MCXNEX_RSRC_CLEANUP_LEVEL5,
	MCXNEX_RSRC_CLEANUP_LEVEL6,
	MCXNEX_RSRC_CLEANUP_LEVEL7,
	MCXNEX_RSRC_CLEANUP_PHASE1_COMPLETE,
	MCXNEX_RSRC_CLEANUP_LEVEL8,
	MCXNEX_RSRC_CLEANUP_LEVEL9,
	MCXNEX_RSRC_CLEANUP_LEVEL10,
	MCXNEX_RSRC_CLEANUP_LEVEL10QP,
	MCXNEX_RSRC_CLEANUP_LEVEL10SRQ,
	MCXNEX_RSRC_CLEANUP_LEVEL10CQ,
	MCXNEX_RSRC_CLEANUP_LEVEL10EQ,
	MCXNEX_RSRC_CLEANUP_LEVEL11,
	MCXNEX_RSRC_CLEANUP_LEVEL12,
	MCXNEX_RSRC_CLEANUP_LEVEL13,
	MCXNEX_RSRC_CLEANUP_LEVEL14,
	MCXNEX_RSRC_CLEANUP_LEVEL15,
	MCXNEX_RSRC_CLEANUP_LEVEL16,
	MCXNEX_RSRC_CLEANUP_LEVEL17,
	MCXNEX_RSRC_CLEANUP_LEVEL18,
	MCXNEX_RSRC_CLEANUP_LEVEL19,
	MCXNEX_RSRC_CLEANUP_LEVEL20,
	MCXNEX_RSRC_CLEANUP_LEVEL21,
	MCXNEX_RSRC_CLEANUP_LEVEL22,
	MCXNEX_RSRC_CLEANUP_LEVEL23,
	MCXNEX_RSRC_CLEANUP_LEVEL24,
	MCXNEX_RSRC_CLEANUP_LEVEL25,
	MCXNEX_RSRC_CLEANUP_LEVEL26,
	MCXNEX_RSRC_CLEANUP_LEVEL27,
	MCXNEX_RSRC_CLEANUP_LEVEL28,
	MCXNEX_RSRC_CLEANUP_LEVEL29,
	MCXNEX_RSRC_CLEANUP_LEVEL30,
	MCXNEX_RSRC_CLEANUP_LEVEL31,
	/* No more cleanup steps below this point! */
	MCXNEX_RSRC_CLEANUP_ALL
} mcxnex_rsrc_cleanup_level_t;

/*
 * The mcxnex_rsrc_mbox_info_t structure is used when initializing the two
 * Mcxnex mailbox types ("In" and "Out").  This structure contains the
 * requested number and size of the mailboxes, and the resource pool from
 * which the other relevant properties will come.
 */
typedef struct mcxnex_rsrc_mbox_info_s {
	uint64_t		mbi_num;
	uint64_t		mbi_size;
	mcxnex_rsrc_pool_info_t *mbi_rsrcpool;
} mcxnex_rsrc_mbox_info_t;

/*
 * The mcxnex_rsrc_hw_entry_info_t structure is used when initializing the
 * Mcxnex HW entry types.  This structure contains the requested number of
 * entries for the resource.  That value is compared against the maximum
 * number (usually determined as a result of the Mcxnex QUERY_DEV_CAP command).
 * In addition it contains a number of requested entries to be "pre-allocated"
 * (this is generally because the Mcxnex hardware requires a certain number
 * for its own purposes).  Lastly the resource pool and resource name
 * information.
 */
typedef struct mcxnex_rsrc_hw_entry_info_s {
	uint64_t		hwi_num;
	uint64_t		hwi_max;
	uint64_t		hwi_prealloc;
	mcxnex_rsrc_pool_info_t *hwi_rsrcpool;
	char			*hwi_rsrcname;
} mcxnex_rsrc_hw_entry_info_t;

/*
 * The mcxnex_rsrc_sw_hdl_info_t structure is used when initializing the
 * Mcxnex software handle types.  This structure also contains the requested
 * number of handles for the resource.  That value is compared against a
 * maximum number passed in.  Because many of the software handle resource
 * types are managed through the use of kmem_cache, fields are provided for
 * specifying cache constructor and destructor methods.  Just like above,
 * there is space for resource pool and resource name information.  And,
 * somewhat like above, there is space to provide information (size, type,
 * pointer to table, etc). about any "pre-allocated" resources that need to
 * be set aside.
 * Note specifically that the "swi_flags" field may contain any of the flags
 * #define'd below.  The MCXNEX_SWHDL_KMEMCACHE_INIT flag indicates that the
 * given resource should have a kmem_cache setup for it, and the
 * MCXNEX_SWHDL_TABLE_INIT flag indicates that some preallocation (as defined
 * by the "swi_num" and "swi_prealloc_sz" fields) should be done, with the
 * resulting table pointer passed back in "swi_table_ptr".
 */
typedef struct mcxnex_rsrc_sw_hdl_info_s {
	uint64_t		swi_num;
	uint64_t		swi_max;
	uint64_t		swi_prealloc_sz;
	mcxnex_rsrc_pool_info_t 	*swi_rsrcpool;
	int (*swi_constructor)(void *, void *, int);
	void (*swi_destructor)(void *, void *);
	char			*swi_rsrcname;
	uint_t			swi_flags;
	void			*swi_table_ptr;
} mcxnex_rsrc_sw_hdl_info_t;
#define	MCXNEX_SWHDL_NOFLAGS		0
#define	MCXNEX_SWHDL_KMEMCACHE_INIT	(1 << 0)
#define	MCXNEX_SWHDL_TABLE_INIT		(1 << 1)


/*
 * The following structure is used to specify (at init time) and to track
 * (during allocation and freeing) all the useful information regarding a
 * particular resource type.  An array of these resources (indexed by
 * resource type) is allocated at driver startup time.  It is available
 * through the driver's soft state structure.
 * Each resource has an indication of its type and its location.  Resources
 * may be located in one of three possible places - in the Mcxnex ICM memory
 * (device virtual, backed by system memory),in system memory, or in
 * Mcxnex UAR memory (residing behind BAR2).
 * Each resource pool also has properties associated with it and the object
 * that make up the pool.  These include the pool's size, the size of the
 * individual objects (rsrc_quantum), any alignment restrictions placed on
 * the pool of objects, and the shift size (log2) of each object.
 * In addition (depending on object type) the "rsrc_ddr_offset" field may
 * indicate where in DDR memory a given resource pool is located (e.g. a
 * QP context table).  It may have a pointer to a vmem_arena for that table
 * and/or it may point to some other private information (rsrc_private)
 * specific to the given object type.
 * Always, though, the resource pool pointer provides a pointer back to the
 * soft state structure of the Mcxnex driver instance with which it is
 * associated.
 */
struct mcxnex_rsrc_pool_info_s {
	mcxnex_rsrc_type_t	rsrc_type;
	uint_t			rsrc_loc;
	uint64_t		rsrc_pool_size; /* table size (num x size) */
	uint64_t		rsrc_align;
	uint_t			rsrc_shift;
	uint_t			rsrc_quantum; /* size of each content */
	void			*rsrc_start; /* phys start addr of table */
	vmem_t			*rsrc_vmp; /* vmem arena for table */
	mcxnex_state_t		*rsrc_state;
	void			*rsrc_private;
};
#define	MCXNEX_IN_ICM			0x0
#define	MCXNEX_IN_SYSMEM		0x1
#define	MCXNEX_IN_UAR			0x2

/*
 * The mcxnex_rsrc_priv_mbox_t structure is used to pass along additional
 * information about the mailbox types.  Specifically, by containing the
 * DMA attributes, access handle, dev access handle, etc., it provides enough
 * information that each mailbox can be later by bound/unbound/etc. for
 * DMA access by the hardware.  Note: we can also specify (using the
 * "pmb_xfer_mode" field), whether a given mailbox type should be bound for
 * DDI_DMA_STREAMING or DDI_DMA_CONSISTENT operations.
 */
typedef struct mcxnex_rsrc_priv_mbox_s {
	dev_info_t		*pmb_dip;
	ddi_dma_attr_t		pmb_dmaattr;
	/* JBDB what is this handle for? */
	ddi_acc_handle_t	pmb_acchdl;
	ddi_device_acc_attr_t	pmb_devaccattr;
	uint_t			pmb_xfer_mode;
} mcxnex_rsrc_priv_mbox_t;

/*
 * The mcxnex_rsrc_t structure is the structure returned by the Mcxnex resource
 * allocation routines.  It contains all the necessary information about the
 * allocated object.  Specifically, it provides an address where the object
 * can be accessed.  It also provides the length and index (specifically, for
 * those resources that are accessed from tables).  In addition it can provide
 * an access handles and DMA handle to be used when accessing or setting DMA
 * to a specific object.  Note: not all of this information is valid for all
 * object types.  See the consumers of each object for more explanation of
 * which fields are used (and for what purpose).
 */
struct mcxnex_rsrc_s {
	mcxnex_rsrc_type_t	rsrc_type;
	void			*hr_addr;
	uint32_t		hr_len;
	uint32_t		hr_indx;
	ddi_acc_handle_t	hr_acchdl;
	ddi_dma_handle_t	hr_dmahdl;
};

/*
 * The following are the Mcxnex Resource Management routines that accessible
 * externally (i.e. throughout the rest of the Mcxnex driver software).
 * These include the alloc/free routines, the initialization routines, which
 * are broken into two phases (see mcxnex_rsrc.c for further explanation),
 * and the Mcxnex resource cleanup routines (which are used at driver detach()
 * time.
 */
int mcxnex_rsrc_alloc(mcxnex_state_t *state, mcxnex_rsrc_type_t rsrc,
    uint_t num, uint_t sleepflag, mcxnex_rsrc_t **hdl);
void mcxnex_rsrc_free(mcxnex_state_t *state, mcxnex_rsrc_t **hdl);
int mcxnex_rsrc_init_phase1(mcxnex_state_t *state);
int mcxnex_rsrc_init_phase2(mcxnex_state_t *state);
void mcxnex_rsrc_fini(mcxnex_state_t *state,
    mcxnex_rsrc_cleanup_level_t clean);


#ifdef __cplusplus
}
#endif

#endif	/* _MCXNEX_RSRC_H */
