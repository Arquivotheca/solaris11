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

#ifndef	_SYS_IOMMU_H
#define	_SYS_IOMMU_H

#include <sys/types.h>
#include <sys/sunddi.h>
#include <sys/memlist.h>
#include <sys/kmem.h>
#include <sys/vmem.h>
#include <sys/sdt.h>

/*
 * Some general constants.
 */
#define	IOMMU_ISTRLEN		11	/* log10(2^31) + 1 */
#define	IOMMU_MAXNAMELEN	64
#define	IOMMU_UNITY_DID		1
#define	IOMMU_MAX_DID		65535

/*
 * Locations that iommu_setup can be called from
 * (the 'where' parameter to iommu_setup).
 */
#define	IOMMU_SETUP_PCIEX_ROOT		0
#define	IOMMU_SETUP_PCIEX_BRIDGE	1
#define	IOMMU_SETUP_PCI_ROOT		2
#define	IOMMU_SETUP_PCI_BRIDGE		3
#define	IOMMU_SETUP_ISA			4

/*
 * DVMA mapping types
 */
#define	IOMMU_DVMA_DEFAULT	0
#define	IOMMU_DVMA_UNITY	1
#define	IOMMU_DVMA_XLATE	2

/*
 * Per-unit structure.
 */
typedef struct iommu {
	kmutex_t		iommu_lock;
	dev_info_t		*iommu_dip;

	kmem_cache_t		*iommu_hdl_cache;
	vmem_t			*iommu_domid_arena;
	int			iommu_dom_maxdom;
	int			iommu_dom_width;

	kmem_cache_t		*iommu_pgtable_cache;
	int			iommu_pgtable_width;
	int			iommu_pgtable_nlevels;
	ddi_dma_handle_t	iommu_pgtable_dmahdl;

	list_node_t		iommu_node;
	uint64_t		iommu_flags;

	void			*iommu_unity_domain;

	uintptr_t		iommu_dma_cbid;

	int			iommu_seg;
} iommu_t;


/*
 * Masks for iommu_flags.
 */
#define	IOMMU_FLAGS_STRICTFLUSH		0x0001	/* always flush TLB */
#define	IOMMU_FLAGS_COHERENT		0x0002	/* pgtable access is coherent */
#define	IOMMU_FLAGS_PASSTHROUGH		0x0004	/* PT supported */
#define	IOMMU_FLAGS_DVMA_ENABLE		0x0008
#define	IOMMU_FLAGS_INTRMAP_ENABLE	0x0010
#define	IOMMU_FLAGS_WRITEBACK		0x0020	/* WB buffer flush needed */


/*
 * Software page table.
 */
typedef struct iommu_pgtable {
	krwlock_t		pgt_rwlock;
	uint64_t		*pgt_vaddr;
	paddr_t			pgt_paddr;
	struct iommu_pgtable	**pgt_next_array;
	int64_t			*pgt_timestamps;
	ddi_acc_handle_t	pgt_memhdl;
} iommu_pgtable_t;

/*
 * Page structure used when allocatin pages.
 */
typedef struct iommu_page {
	void			*ip_vaddr;
	paddr_t			ip_paddr;
	ddi_acc_handle_t	ip_memhdl;
} iommu_page_t;

/*
 * Translation domain.
 */
typedef struct iommu_domain {
	uint_t			dom_did;
	iommu_t			*dom_iommu;

	int			dom_maptype;
	int			dom_domtype;

	vmem_t			*dom_arena;
	char			dom_arena_name[IOMMU_MAXNAMELEN];

	iommu_pgtable_t		*dom_pgtable_root;

	dev_info_t		*dom_dip;

	volatile uint_t		dom_ref;

	/*
	 * Generation value used to avoid doing too many IOTLB flushes.
	 *
	 * A signed 64 bit value will wrap in about 293 years when incremented
	 * every nanosecond (which is an impossibly high I/O rate for
	 * a single device currently), so we're not concerned about that.
	 */
	volatile int64_t	dom_flush_gen;
	kmutex_t		dom_flush_lock;

	kmem_cache_t		*dom_pre_cache;
} iommu_domain_t;

#define	IOMMU_GET_FLUSHGEN(d, g) \
	(g) = (d)->dom_flush_gen; \
	if ((g) < 0) \
		(g) = ((g) & ~((uint64_t)1 << 63)) + 1;

/*
 * Types for dom_maptype
 */
#define	IOMMU_MAPTYPE_UNITY	0
#define	IOMMU_MAPTYPE_XLATE	1

/*
 * Types for dom_domtype.
 */
#define	IOMMU_DOMTYPE_SHARED	0
#define	IOMMU_DOMTYPE_EXCL	1

/*
 * Structure used to gather information on a PCI device,
 * passed to some setup functions.
 */
typedef struct iommu_pci_info {
	/*
	 * Information available for all PCI devices.
	 */
	uint_t pi_seg;
	uint_t pi_init_bus;	/* original bus before any rebalancing */
	uint_t pi_bus;		/* PCI BDF values */
	uint_t pi_dev;
	uint_t pi_func;
	uint_t pi_vendorid;	/* PCI vendor/dev ID */
	uint_t pi_devid;
	uint_t pi_class;
	uint_t pi_subclass;

	uint_t pi_type;		/* PCI(E) endpoint, PCI(E) bridge, etc */

	/*
	 * The fields below are only valid if pi_type != IOMMU_DEV_UNKNOWN,
	 * and indicates a bridge.
	 */
	uint_t pi_secbus;
	uint_t pi_subbus;
} iommu_pci_info_t;

/*
 * Types for pi_devtype
 */
#define	IOMMU_PCI_UNKNOWN	0
#define	IOMMU_PCI_PCI		1
#define	IOMMU_PCI_PCIEX		2

/*
 * Types for pi_type
 */
#define	IOMMU_DEV_PCI		0	/* endpoint (PCI) */
#define	IOMMU_DEV_PCIE		1	/* endpoint (PCIE) */
#define	IOMMU_DEV_PCIE_PCI	2	/* PCIE-PCI bridge */
#define	IOMMU_DEV_PCI_PCIE	3	/* PCI-PCIE bridge */
#define	IOMMU_DEV_PCIE_PCIE	4	/* PCIE-PCIE bridge */
#define	IOMMU_DEV_PCI_PCI	5	/* PCI-PCI bridge */
#define	IOMMU_DEV_PCI_ISA	6	/* PCI-ISA bridge */
#define	IOMMU_DEV_PCI_ROOT	7	/* PCI root complex */
#define	IOMMU_DEV_PCIE_ROOT	8	/* PCIE root complex */
#define	IOMMU_DEV_ISA		9	/* ISA device */
#define	IOMMU_DEV_OTHER		10	/* anything else */

/*
 * Structure used to wait for IOMMU command completion.
 */
typedef struct iommu_wait {
	volatile uint32_t	iwp_vstatus;
	uint64_t		iwp_pstatus;
	boolean_t		iwp_sync;
	const char		*iwp_name; /* ID for debugging/statistics */
} iommu_wait_t;

#define	IOMMU_WAIT_PENDING	1
#define	IOMMU_WAIT_DONE		2

/*
 * Structure used for DMA handles that have no attribute limitations
 * (true for modern PCI-Express devices). When a handle is allocated,
 * one of these structures is allocated (either from a free list,
 * or explicitly), and, in the latter case, populated with DVMA space.
 * When a handle is freed, the structure is released to a free list.
 *
 * This will eventually result in a set of preallocated DVMA ranges
 * which can be accessed efficiently. There will be plenty of space
 * to go around if the device has no limits (48 bits of DVMA space).
 * There will be some overhead for PTE pages that remain allocated,
 * but for a typical device, this isn't large.
 */
typedef struct iommu_dmap {
	uint64_t	idm_dvma;
	uint_t		idm_npages;
	uint64_t	*idm_ptes;
	int64_t		idm_unmap_time;
	int64_t		*idm_timep;
} iommu_dmap_t;


/*
 * Structure pointed to from struct dev_info, containing
 * IOMMU information for a device. NULL if an IOMMU is not
 * used, or if the device has passthrough / 1:1 mapping.
 */
typedef struct iommu_devi {
	iommu_t			*id_iommu;
	iommu_domain_t		*id_domain;
	iommu_pci_info_t	id_pci;
	uint_t			id_presize;
	uint_t			id_npreptes;
} iommu_devi_t;

#define	IOMMU_DEVI(dip)		((iommu_devi_t *)(DEVI(dip)->devi_iommu))

/*
 * DVMA space to preallocate for a handle.
 * Needs to be a power of 2, so that it can be guaranteed
 * not to cross a PTE page boundary (since we use this size
 * as an alignment constraint too).
 */
#define	IOMMU_PRESIZE_STORAGE	262144
#define	IOMMU_NPREPTES_STORAGE	(IOMMU_PRESIZE_STORAGE / MMU_PAGESIZE)
#define	IOMMU_PRESIZE_ETHERNET	16384
#define	IOMMU_NPREPTES_ETHERNET	(IOMMU_PRESIZE_ETHERNET / MMU_PAGESIZE)
#define	IOMMU_PRESIZE_DEFAULT	32768
#define	IOMMU_NPREPTES_DEFAULT	(IOMMU_PRESIZE_DEFAULT / MMU_PAGESIZE)

/*
 * This will still fit on one PTE page.
 */
#define	IOMMU_PRESIZE_MAX	(2 * 1024 * 1024)

/*
 * kmem cache size passed to vmem_create. Really only used when preallocated
 * DVMA space is not used.
 */
#define	IOMMU_CACHED_SIZE_SMALL	16384
#define	IOMMU_CACHED_SIZE_LARGE	65536

/*
 * Number of DMA cookies stored in the handle itself.
 */
#define	IOMMU_PRECOOKIES 8

typedef struct iommu_hdl_private {
	ddi_dma_impl_t		ihp_impl;

	iommu_dmap_t		*ihp_prealloc;
	iommu_wait_t		ihp_inv_wait;

	uint64_t		ihp_sdvma;
	uint_t			ihp_npages;
	boolean_t		ihp_canfast;

	void			*ihp_minaddr;
	void			*ihp_maxaddr;
	uint64_t		ihp_align;
	uint64_t		ihp_boundary;

	/* Pointer to timestamps to see if an IOTLB flush is needed */
	int64_t			*ihp_timep;

	/* Pointer to PTEs, iff this is a contiguous stretch of PTEs */
	uint64_t		*ihp_ptep;

	uint32_t		ihp_maxcsize;

	int			ihp_curwin;
	ddi_dma_cookie_t	*ihp_nextwinp;

	/*
	 * Normally, IOMMU_PRECOOKIES cookies are preallocated. If more
	 * than that are needed, ihp_cookies will point to an allocated
	 * space. Otherwise, it points to ihp_precookies. In either case,
	 * ihp_ncalloc is the available space for cookies.
	 */
	uint_t			ihp_ncalloc;
	uint_t			ihp_ccount;
	ddi_dma_cookie_t	*ihp_cookies;
	ddi_dma_cookie_t	ihp_precookies[IOMMU_PRECOOKIES];
} iommu_hdl_priv_t;

/*
 * Convenience structure for pagetable handling.
 */
/* structure used to store idx into each level of the page tables */
typedef struct iommu_xlate {
	int xlt_level;
	uint_t xlt_idx;
	iommu_pgtable_t *xlt_pgtable;
} iommu_xlate_t;

/*
 * Pagetable related constants.
 */
#define	IOMMU_PGTABLE_MAXIDX 		511	/* 512 entries in one PTP */
#define	IOMMU_PGTABLE_MAXLEVELS		6	/* 64 bits */
#define	IOMMU_PGTABLE_LEVEL_STRIDE	9
#define	IOMMU_PGTABLE_LEVEL_MASK	((1 << IOMMU_PGTABLE_LEVEL_STRIDE) - 1)



/*
 * Global IOMMU ops structure. Set by the low-level module
 * (amd_iommu or intel_iommu).
 */
struct iommu_ops {
	void (*ioo_start_units)(void);
	int (*ioo_quiesce_units)(int reboot);
	int (*ioo_unquiesce_units)(void);
	void *(*ioo_find_unit)(dev_info_t *dip, iommu_pci_info_t *pinfo);
	void (*ioo_dev_reserved)(dev_info_t *dip,
	    iommu_pci_info_t *pinfo, struct memlist **resvpp,
	    struct memlist **unitypp);
	void (*ioo_iommu_reserved)(iommu_t *iommu, struct memlist **mlpp);
	void (*ioo_set_root_table)(dev_info_t *dip, iommu_t *iommu,
	    uint_t bus, uint_t dev, uint_t func, iommu_domain_t *domain);
	void (*ioo_flush_domain)(iommu_t *iommu, uint_t domid,
	    iommu_wait_t *iwp);
	void (*ioo_flush_pages)(iommu_t *iommu, uint_t domid,
	    uint64_t sdvma, uint_t npages, iommu_wait_t *iwp);
	void (*ioo_flush_buffers)(iommu_t *iommu);

	uint64_t ioo_rmask;
	uint64_t ioo_wmask;

	uint64_t ioo_ptemask[IOMMU_PGTABLE_MAXLEVELS];

	int (*ioo_dma_allochdl)(dev_info_t *rdip, ddi_dma_attr_t *attr,
	    int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *handlep);
	int (*ioo_dma_freehdl)(dev_info_t *rdip, ddi_dma_handle_t handle);
	int (*ioo_dma_bindhdl)(dev_info_t *rdip, ddi_dma_handle_t handle,
	    struct ddi_dma_req *dmareq, ddi_dma_cookie_t *cookiep,
	    uint_t *ccountp);
	int (*ioo_dma_unbindhdl)(dev_info_t *rdip, ddi_dma_handle_t handle);
	int (*ioo_dma_win)(dev_info_t *rdip, ddi_dma_handle_t handle,
	    uint_t win, off_t *offp, size_t *lenp, ddi_dma_cookie_t *cookiep,
	    uint_t *ccountp);
};

extern struct iommu_ops *iommu_ops;

/*
 * Quirk handling structure. Used to match devices to quirks that
 * require special handling (usually 1:1 mappings).
 */
typedef struct iommu_quirk {
	int		iq_type;
	const char	*iq_dname;
	uint_t		iq_vendorid;
	uint_t		iq_devid;
	uint_t		iq_class;
	uint_t		iq_subclass;
	void		(*iq_func)(dev_info_t *dip, void *arg);
} iommu_quirk_t;

/*
 * Match types used with the quirk structure above.
 */
#define	IQMATCH_DEFAULT	-1	/* default case, must be last in the table */
#define	IQMATCH_DNAME	0
#define	IQMATCH_CLASS	1
#define	IQMATCH_PCIID	2

/*
 * DEBUG DTrace probes.
 */
#ifdef DEBUG
#define	IOMMU_DPROBE1(name, type1, arg1) \
	DTRACE_PROBE1(name, type1, arg1)
#define	IOMMU_DPROBE2(name, type1, arg1, type2, arg2) \
	DTRACE_PROBE2(name, type1, arg1, type2, arg2)
#define	IOMMU_DPROBE3(name, type1, arg1, type2, arg2, type3, arg3) \
	DTRACE_PROBE3(name, type1, arg1, type2, arg2, type3, arg3)
#else
#define	IOMMU_DPROBE1(name, type1, arg1)
#define	IOMMU_DPROBE2(name, type1, arg1, type2, arg2)
#define	IOMMU_DPROBE3(name, type1, arg1, type2, arg2, type3, arg3)
#endif

/*
 * Helper definitions for IOMMU list walking (iommulib_list_walk)
 */
#define	IWALK_SUCCESS_CONTINUE	0	/* success, continue walk */
#define	IWALK_SUCCESS_STOP	1	/* success, stop walk */
#define	IWALK_FAILURE_CONTINUE	2	/* failure, contine walk */
#define	IWALK_FAILURE_STOP	3	/* failure, stop walk */
#define	IWALK_REMOVE		4	/* remove from list, continue walk */

/*
 * Setup entry points, called from the PCI code.
 */
void iommu_setup(dev_info_t *dip, int where);
void iommu_teardown(dev_info_t *dip);

/*
 * Domain functions, used in the iommu module itself.
 */
int iommu_domain_mergelist(struct memlist **mlpp, struct memlist *mlp);
void iommu_domain_freelist(struct memlist *mlp);
iommu_domain_t *iommu_domain_alloc(dev_info_t *dip, iommu_t *iommu, int type,
    struct memlist *unityp, struct memlist *resvp);
iommu_domain_t *iommu_domain_dup(iommu_domain_t *domain);
void iommu_domain_free(dev_info_t *dip, iommu_domain_t *domain);
int iommu_domain_create_unity(iommu_t *iommu);

/*
 * Page table functions, used in the iommu module itself.
 */
int iommu_pgtable_ctor(void *buf, void *arg, int kmflag);
void iommu_pgtable_dtor(void *buf, void *arg);
iommu_pgtable_t *iommu_pgtable_alloc(iommu_t *iommu, int kmflags);
void iommu_pgtable_free(iommu_t *iommu, iommu_pgtable_t *pgtable);
void iommu_pgtable_map_unity(iommu_domain_t *domain, uint64_t dvma,
    uint64_t npages, int kfl);
void iommu_pgtable_alloc_pdps(iommu_t *iommu, iommu_domain_t *domain,
    iommu_xlate_t *xlate, int kfl);
int iommu_pgtable_get_ptes(iommu_domain_t *domain, uint64_t sdvma,
    uint_t snvpages, int kfl, int64_t **timep, uint64_t **ptep);
int iommu_pgtable_lookup_ptes(iommu_domain_t *domain, uint64_t sdvma,
    uint_t snvpages, int64_t **timep, uint64_t **ptep);
boolean_t iommu_pgtable_lookup_pdps(iommu_domain_t *domain,
    iommu_xlate_t *xlate, int nlevels);
void iommu_pgtable_alloc_pdps(iommu_t *iommu, iommu_domain_t *domain,
    iommu_xlate_t *xlate, int kfl);
int iommu_pgtable_get_ptes(iommu_domain_t *domain, uint64_t sdvma,
    uint_t snvpages, int kfl, int64_t **timep, uint64_t **ptep);
int iommu_pgtable_lookup_ptes(iommu_domain_t *domain, uint64_t sdvma,
    uint_t snvpages, int64_t **timep, uint64_t **ptep);
void iommu_pgtable_teardown(iommu_t *iommu, iommu_pgtable_t *pgtable);
void iommu_pgtable_xlate_setup(uint64_t dvma, iommu_xlate_t *xlate,
    int nlevels);


/*
 * "library" functions, called from both the main IOMMU code
 * and the machine-specific modules.
 */
int iommulib_pci_base_info(dev_info_t *dip, iommu_pci_info_t *pinfo);
int iommulib_pci_info(dev_info_t *dip, iommu_pci_info_t *pinfo);
void iommulib_init_wait(iommu_wait_t *iwp, const char *name, boolean_t sync);
void iommulib_quirk_match(dev_info_t *dip, iommu_pci_info_t *pinfo, void *arg,
    iommu_quirk_t *table);
char *iommulib_alloc_name(const char *str, int instance, size_t *lenp);
int iommulib_init_iommu_dvma(iommu_t *iommu);
void iommulib_teardown_iommu_dvma(iommu_t *iommu);
int iommulib_page_alloc(iommu_t *iommu, int kfl, iommu_page_t *ip);
void iommulib_page_free(iommu_page_t *ip);
void iommulib_list_insert(iommu_t *iommu);
void iommulib_list_walk(int (*func)(iommu_t *, void *), void *arg, int *nfail);
dev_info_t *iommulib_top_pci_bridge(dev_info_t *dip, iommu_pci_info_t *pinfop);
uint32_t iommulib_bdf_to_sid(uint_t seg, uint_t bus, uint_t dev, uint_t func);
void iommulib_sid_to_bdf(uint32_t sid, uint_t *seg, uint_t *bus, uint_t *dev,
    uint_t *func);
void iommulib_domhash_create(void);
void iommulib_domhash_insert(uint32_t sid, iommu_domain_t *dom);
iommu_domain_t *iommulib_domhash_find(uint32_t sid);
void iommulib_domhash_remove(uint32_t sid);
uint64_t *iommulib_find_pte(iommu_domain_t *dom, uint64_t dvma);
iommu_t *iommulib_find_iommu(dev_info_t *dip);
void iommulib_bootops(char *driver, int *enable, int *dvma_enable,
    int *intrmap_enable);



/*
 * DDI DMA functions, called through iommu_ops
 */
int
iommu_dma_allochdl(dev_info_t *rdip, ddi_dma_attr_t *attr,
    int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *dma_handlep);
int iommu_dma_freehdl(dev_info_t *rdip, ddi_dma_handle_t dma_handle);
int iommu_dma_bindhdl(dev_info_t *rdip, ddi_dma_handle_t dma_handle,
    struct ddi_dma_req *dmareq, ddi_dma_cookie_t *cookiep, uint_t *ccountp);
int iommu_dma_unbindhdl(dev_info_t *rdip, ddi_dma_handle_t dma_handle);
int iommu_dma_win(dev_info_t *rdip, ddi_dma_handle_t handle, uint_t win,
    off_t *offp, size_t *lenp, ddi_dma_cookie_t *cookiep, uint_t *ccountp);

/*
 * Other.
 */
int iommu_dmap_ctor(void *buf, void *arg, int kfl);
void iommu_dmap_dtor(void *buf, void *arg);

/*
 * Define this as long as there are some drivers that do not correctly
 * specify DDI_DMA_READ / DDI_DMA_WRITE.
 */
#define	IOMMU_BUGGY_DRIVERS

#endif	/* _SYS_IOMMU_H */
