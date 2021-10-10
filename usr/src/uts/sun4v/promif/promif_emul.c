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

#include <sys/promif_impl.h>
#include <sys/machsystm.h>
#include <sys/lpad.h>
#include <sys/vmsystm.h>
#include <sys/prom_plat.h>
#include <sys/ldoms.h>
#include <sys/kobj.h>
#include <sys/reboot.h>
#include <sys/hypervisor_api.h>
#include <sys/mdesc.h>
#include <sys/mach_descrip.h>
#include <sys/cpu_module.h>
#include <sys/types.h>
#include <vm/seg_kmem.h>
#include <io/px/px_ioapi.h>
#include <io/px/px_lib4v.h>

#ifndef _KMDB
#include <sys/pte.h>
#include <vm/hat_sfmmu.h>
#include <sys/memlist_impl.h>

static processorid_t cif_cpu;
static struct translation *cif_prom_trans;
static size_t cif_prom_ntrans;

int cif_cpu_mp_ready;
int (*prom_cif_handler)(void *) = NULL;

extern struct memlist *phys_avail;
extern void kdi_tlb_page_unlock(caddr_t, int);
extern boolean_t prom_emulated;

#define	COMBINE(hi, lo) (((uint64_t)(uint32_t)(hi) << 32) | (uint32_t)(lo))
#define	OFW_PT_START_ADDR	0xfffffffc00000000	/* OBP PT start */
#define	OFW_PT_END_ADDR		0xffffffffffffffff	/* OBP PT end */

#define	PROM_ADDR(a)	(((a) >= OFW_START_ADDR && (a) <= OFW_END_ADDR) || \
			((a) >= OFW_PT_START_ADDR && (a) <= OFW_PT_END_ADDR))
#endif

#ifdef DEBUG
uint_t cif_debug;
int prom_free_debug;
int prom_free_iommu_debug;
#define	PMFREE_DEBUG(args...) if (prom_free_debug) printf(args)
#define	PMFREE_IOMMU_DEBUG if (prom_free_iommu_debug) printf
#else
#define	PMFREE_DEBUG(args...)
#define	PMFREE_IOMMU_DEBUG
#endif

extern int (*cif_handler)(void *);

typedef struct {
	char		*name;
	cif_func_t	func;
} cif_callback_t;

static cif_callback_t cb_table[] = {
	{ "getprop",			promif_getprop		    },
	{ "getproplen",			promif_getproplen	    },
	{ "nextprop",			promif_nextprop		    },
	{ "peer",			promif_nextnode		    },
	{ "child",			promif_childnode	    },
	{ "parent",			promif_parentnode	    },
	{ "enter",			promif_enter_mon	    },
	{ "exit",			promif_exit_to_mon	    },
	{ "boot",			promif_reboot		    },
	{ "write",			promif_write		    },
	{ "read",			promif_read		    },
	{ "interpret",			promif_interpret	    },
	{ "finddevice",			promif_finddevice	    },
	{ "instance-to-package",	promif_instance_to_package  },
#ifndef _KMDB
	{ "setprop",			promif_setprop		    },
	{ "test",			promif_test		    },
	{ "instance-to-path",		promif_instance_to_path	    },
	{ "SUNW,power-off",		promif_power_off	    },
	{ "SUNW,asr-list-keys-len",	promif_asr_list_keys_len    },
	{ "SUNW,asr-list-keys",		promif_asr_list_keys	    },
	{ "SUNW,asr-export-len",	promif_asr_export_len	    },
	{ "SUNW,asr-export",		promif_asr_export	    },
	{ "SUNW,set-security-key",	promif_set_security_key	    },
	{ "SUNW,get-security-key",	promif_get_security_key	    },
	{ "SUNW,start-cpu-by-cpuid",	promif_start_cpu	    },
	{ "SUNW,set-trap-table",	promif_set_mmfsa_traptable  },
	{ "SUNW,set-sun4v-api-version",	promif_set_sun4v_api_version },
	{ "SUNW,get-sun4v-api-version",	promif_get_sun4v_api_version },
#endif
	{ NULL,				NULL			    }
};

typedef struct dma_list {
	uint64_t	ra;
	struct dma_list	*next;
} dma_list_t;

dma_list_t *dma_list = NULL;	/* PROM IOMMU active DMA list */

/*
 * hv_getmap_bug is set during DMA list build if we detect Hypervisor bug.
 * Once set we abort DMA list build.  In presence of Hypervisor bug none
 * (vs. selective) of the OBP mappings are reclaimed.
 */
int hv_getmap_bug = 0;

extern uint64_t	hvio_dma_ismapped(devhandle_t, tsbid_t, r_addr_t *);

cif_func_t
promif_find_cif_callback(char *opname)
{
	cif_callback_t	*cb;

	if (opname == NULL)
		return (NULL);

	for (cb = cb_table; cb->name; cb++) {
		if (prom_strcmp(cb->name, opname) == 0)
			break;
	}

	return (cb->func);
}

static int
kern_cif_handler(void *p)
{
	cell_t		*ci = (cell_t *)p;
	char		*opname;
	cif_func_t	func;
	int		rv;

	ASSERT(cif_handler == kern_cif_handler);

#ifndef _KMDB
	cif_cpu = getprocessorid();
#endif

	opname = p1275_cell2ptr(ci[0]);

	/* lookup the callback for the desired operation */
	func = promif_find_cif_callback(opname);

	if (func == NULL) {
#ifdef _KMDB
		prom_fatal_error("sun4v unsupported CIFs\n");
#else
		cmn_err(CE_CONT, "!sun4v unsupported CIF: %s\n", opname);
		return (-1);
#endif
	}

	/* callback found, execute it */
	rv = func(p);

#ifndef _KMDB
	cif_cpu = -1;
#endif

	return (rv);
}

#ifdef _KMDB

void
cif_init(char *pgmname, caddr_t root, ihandle_t in, ihandle_t out,
    phandle_t pin, phandle_t pout, pnode_t chosen, pnode_t options)
{
	/* initialize pointer to a copy of OBP device tree */
	promif_stree_setroot(root);

	promif_set_nodes(chosen, options);

	/* initialize io parameters */
	promif_io_init(in, out, pin, pout);

	/*
	 * Switch CIF handler to the kernel.
	 */
	if (pgmname != NULL)
		prom_init(pgmname, (void *)kern_cif_handler);
	else
		cif_handler = kern_cif_handler;
}

#else

static struct translation *
read_prom_mappings(size_t *ntransp)
{
	char *prop = "translations";
	pnode_t node;
	size_t translen;
	ihandle_t immu;
	struct translation *transroot;

	*ntransp = 0;

	/*
	 * the "translations" property is associated with the mmu node
	 */
	if ((immu = prom_mmu_ihandle()) == (ihandle_t)-1) {
		PMFREE_DEBUG("no mmu ihandle");
		return (NULL);
	}
	node = (pnode_t)prom_getphandle(immu);
	if (node == OBP_NONODE || node == OBP_BADNODE) {
		PMFREE_DEBUG("no mmu node");
		return (NULL);
	}

	if ((translen = prom_getproplen(node, prop)) == -1) {
		PMFREE_DEBUG("no translations property");
		return (NULL);
	}
	transroot = (struct translation *)kmem_zalloc(translen, KM_SLEEP);

	if (prom_getprop(node, prop, (caddr_t)transroot) == -1) {
		PMFREE_DEBUG("translations getprop failed");
		kmem_free(transroot, translen);
		return (NULL);
	}
	*ntransp = translen / sizeof (*transroot);

	return (transroot);
}

/*
 * USB hardware has active dma, if Solaris reclaims those pages and
 * uses them for something else, we'll get data corruption in certain
 * cases.  So to avoid that, we won't reclaim those pages if we can
 * find active mappings accurately.  If we can't find them accurately
 * (one such case is hv_getmap_bug), we won't reclaim any pages from
 * OBP's mapped region.
 */

/*
 * Following routines manage building, destroying, adding new entries and
 * searching of PROM IOMMU active DMA list.
 * active_dma_list_build() is called from main().  This routine calls
 * visit_prom_node() for each /pci node found in the platform, reads
 * "reg" and "virtual-dma" OBP properties to determine dvma size
 * and # of pages.  Finally Hypervisor call made for each IOMMU tsbid
 * to determine if page mapped for DMA and RA added to active DMA list.
 */
static void
active_dma_list_destroy()
{
	dma_list_t *next;

	while (dma_list != NULL) {
		next = dma_list->next;
		kmem_free(dma_list, sizeof (dma_list_t));
		dma_list = next;
	}
}

static void
active_dma_list_add(uint64_t ra)
{
	dma_list_t *new;

	new = kmem_zalloc(sizeof (dma_list_t), KM_SLEEP);
	new->next = dma_list;
	new->ra = ra;
	dma_list = new;
}

static void
visit_prom_node(pnode_t np)
{
	int		tsbid, reg_len, vdma_len;
	char		*reg_val, *vdma_val;
	devhandle_t	dev_hdl;
	uint_t		dvmasize, ndvmapages;


	/* construct devhandle, dvmasize, ndvmapages from nodeid */

	if ((reg_len = prom_getproplen(np, OBP_REG)) == -1)
		return;

	if ((vdma_len = prom_getproplen(np, "virtual-dma")) == -1)
		return;

	reg_val = kmem_zalloc(reg_len, KM_SLEEP);
	vdma_val = kmem_zalloc(vdma_len, KM_SLEEP);

	(void) prom_getprop(np, OBP_REG, reg_val);
	(void) prom_getprop(np, "virtual-dma", vdma_val);

	/*
	 * Device Handle: Uniquely identifies sun4v device.  It is
	 * lower 28-bits of the first 32-bit integer from 'reg'.
	 */
	dev_hdl = (devhandle_t)(((uint_t *)reg_val)[0] & DEVHDLE_MASK);

	/* dvmasize is 2nd 32-bit integer from "virtual-dma" */
	dvmasize = (uint_t)(((uint_t *)vdma_val)[1]);
	ndvmapages = dvmasize >> MMU_PAGESHIFT;

	PMFREE_IOMMU_DEBUG("PCI device handle 0x%lx\n", dev_hdl);
	PMFREE_IOMMU_DEBUG("ndvmapages %d\n", ndvmapages);

	kmem_free(reg_val, reg_len);
	kmem_free(vdma_val, vdma_len);

	for (tsbid = 0; tsbid < ndvmapages; tsbid++) {
		r_addr_t r_addr;

		if (hvio_dma_ismapped(dev_hdl, tsbid, &r_addr) == H_EOK) {
			if ((r_addr & 3) != 0) {
				hv_getmap_bug = 1;
				if (dma_list != NULL)
					active_dma_list_destroy();
				return;
			}
			active_dma_list_add((uint64_t)r_addr);
		}
	}
}

void
active_dma_list_build()
{
	pnode_t np;
	char	pname[OBP_MAXPROPNAME];

	np = prom_childnode(prom_rootnode());
	while (np != OBP_NONODE && np != OBP_BADNODE) {
		/* get the name from the current node */
		if (prom_getprop(np, OBP_NAME, pname) > 0) {
			if (prom_strcmp("pci", pname) == 0)
				visit_prom_node(np);
			if (hv_getmap_bug == 1)
				break;
		}
		np = prom_nextnode(np);
	}

#ifdef DEBUG
	dma_list_t *dma_listp = dma_list;
	while (dma_listp != NULL) {
		PMFREE_IOMMU_DEBUG("PROM page with IOMMU mapping 0x%lx\n",
		    dma_listp->ra);
		dma_listp = dma_listp->next;
	}
#endif
}

static int
active_dma_list_search(uint64_t ra)
{
	dma_list_t *dma_listp = dma_list;

	while (dma_listp != NULL) {
		if (dma_listp->ra == ra)
			return (1);
		dma_listp = dma_listp->next;
	}
	return (0);
}

static void
unmap_prom_mappings(struct translation *transroot, size_t ntransroot)
{
	int i, j, rv;
	int npgs, nunmapped, nfreed, nskipped, nskipped_io;
	char *p;
	tte_t tte;
	pfn_t pfn;
	page_t *pp;
	uint64_t vaddr;
	struct translation *promt;
	cpuset_t other_cpus;

	/*
	 * During startup isa_list is allocated in OBP address space
	 * so it needs to be re-allocated in kernel address space
	 * before OBP memory is unmapped.
	 *
	 * see cpu_setup_common().
	 */
	p = kmem_zalloc(strlen(isa_list) + 1, KM_SLEEP);
	(void) strcpy(p, isa_list);
	isa_list = p;

	nfreed = 0;
	nunmapped = 0;
	nskipped = 0;
	nskipped_io = 0;

	for (i = 0, promt = transroot; i < ntransroot; i++, promt++) {
		ASSERT(promt->tte_hi != 0);
		ASSERT32(promt->virt_hi == 0 && promt->size_hi == 0);

		vaddr = COMBINE(promt->virt_hi, promt->virt_lo);

		if (!PROM_ADDR(vaddr)) {
			nskipped++;
			continue;
		}

		npgs = mmu_btopr(COMBINE(promt->size_hi, promt->size_lo));

		if (npgs > 1) {
			PMFREE_DEBUG("large trans vaddr=0x%lx, npgs=%d\n",
			    vaddr, npgs);
		}
		for (j = 0; j < npgs; j++) {

			pfn = sfmmu_vatopfn((caddr_t)vaddr, KHATID, &tte);

			if (pfn == PFN_INVALID) {
				tte.tte_inthi = promt->tte_hi;
				tte.tte_intlo = promt->tte_lo;
				pfn = TTE_TO_PFN((caddr_t)COMBINE(
				    promt->virt_hi, promt->virt_lo), &tte);
				PMFREE_DEBUG(
				    "no mapping for vaddr=0x%lx (opfn=0x%lx)\n",
				    vaddr, pfn);
				break;
			}
			ASSERT(!TTE_IS_LOCKED(&tte));
			ASSERT(TTE_IS_8K(&tte));

			/*
			 * Don't unload if mapped by IOMMU.
			 */
			if (active_dma_list_search(mmu_ptob(pfn))) {
				PMFREE_IOMMU_DEBUG("Not unloading VA 0x%lx "
				    "PFN %ld\n", vaddr, pfn);
				vaddr += PAGESIZE;
				continue;
			}

			/*
			 * Unload the current mapping for the pfn and
			 * if it is the last mapping for a memory page,
			 * free the page.
			 */
			PMFREE_DEBUG("unmap vaddr=0x%lx pfn=0x%lx", vaddr, pfn);

			hat_unload(kas.a_hat, (caddr_t)vaddr, PAGESIZE,
			    HAT_UNLOAD_UNLOCK);

			if (pf_is_memory(pfn)) {
				pp = page_numtopp_nolock(pfn);
				PMFREE_DEBUG(" pp=0x%p", (void *)pp);
				ASSERT(pp);
				ASSERT(PAGE_EXCL(pp));
				ASSERT(PP_ISNORELOC(pp));
				ASSERT(!PP_ISFREE(pp));
				ASSERT(page_find(&promvp, mmu_ptob(pfn)));
				ASSERT(page_get_pagecnt(pp->p_szc) == 1);

				if (pp->p_mapping) {
					PMFREE_DEBUG(" skip\n");
				} else {
					PP_CLRNORELOC(pp);
					page_destroy(pp, 0);
					memlist_write_lock();
					rv = memlist_add_span(pfn << PAGESHIFT,
					    PAGESIZE, &phys_avail);
					ASSERT(rv == MEML_SPANOP_OK);
					memlist_write_unlock();
					PMFREE_DEBUG(" free\n");
					nfreed++;
				}
			} else {
				nskipped_io++;
				PMFREE_DEBUG(" skip IO\n");
			}
			nunmapped++;
			vaddr += PAGESIZE;
		}
	}

	active_dma_list_destroy();

	if (transroot) {
		PMFREE_DEBUG(
		    "nunmapped=%d nfreed=%d nskipped=%d nskipped_io=%d\n",
		    nunmapped, nfreed, nskipped, nskipped_io);
		kmem_free(transroot, ntransroot * sizeof (*transroot));
	}

	/*
	 * Unload OBP permanent mappings.
	 */
	kdi_tlb_page_unlock((caddr_t)OFW_START_ADDR, 1);
	kpreempt_disable();
	other_cpus = cpu_ready_set;
	CPUSET_DEL(other_cpus, CPU->cpu_id);
	xt_some(other_cpus, vtag_unmap_perm_tl1, (uint64_t)OFW_START_ADDR,
	    KCONTEXT);
	kpreempt_enable();
}

static void cache_prom_data(void);

/*
 * This function returns 1 if the current thread is executing in
 * the CIF and 0 otherwise. This is useful information to know
 * since code that implements CIF handlers can assume that it has
 * gone through the kern_preprom() entry point, implying it is
 * running single threaded, has preemption disabled, etc.
 */
int
promif_in_cif(void)
{
	int	mycpuid = getprocessorid();

	return ((cif_cpu == mycpuid) ? 1 : 0);
}

/*
 * Check that all cpus in the MD are within range (< NCPU).  Attempt
 * to stop any that aren't.
 */
static void
cif_check_cpus(void)
{
	md_t		*mdp;
	mde_cookie_t	rootnode;
	size_t		listsz;
	int		i;
	mde_cookie_t	*listp = NULL;
	int		num_nodes;
	uint64_t	cpuid;
	int		status;

	mdp = md_get_handle();
	ASSERT(mdp);

	rootnode = md_root_node(mdp);
	ASSERT(rootnode != MDE_INVAL_ELEM_COOKIE);

	num_nodes = md_node_count(mdp);
	ASSERT(num_nodes > 0);

	listsz = num_nodes * sizeof (mde_cookie_t);
	listp = kmem_zalloc(listsz, KM_SLEEP);

	num_nodes = md_scan_dag(mdp, rootnode, md_find_name(mdp, "cpu"),
	    md_find_name(mdp, "fwd"), listp);

	if (num_nodes <= 0)
		goto done;

	for (i = 0; i < num_nodes; i++) {
		if (md_get_prop_val(mdp, listp[i], "id", &cpuid)) {
			cmn_err(CE_WARN, "cif_check_cpus: "
			    "CPU instance %d has no 'id' property", i);
			continue;
		}

		mutex_enter(&cpu_lock);

		if (cpuid >= NCPU) {
			status = stopcpu_bycpuid(cpuid);
			if (status != 0 && status != ENOTSUP)
				cmn_err(CE_PANIC, "failed to stop cpu %lu (%d)",
				    cpuid, status);
		}

		mutex_exit(&cpu_lock);
	}

done:
	kmem_free(listp, listsz);
	(void) md_fini_handle(mdp);
}

void
cif_init(void)
{
	void (*kmdb_cb)(void);
	uint64_t rtba;
	uint64_t rv;
	size_t ntransroot;
	struct translation *transroot = NULL;
	extern void hsvc_boot_cleanup();

	/*
	 * Check if domaining is enabled. If not, do not
	 * initialize the kernel CIF handler.
	 */
	if (!domaining_enabled())
		return;

	if (hv_getmap_bug == 0)
		transroot = read_prom_mappings(&ntransroot);

	/*
	 * Cache PROM data that is needed later, e.g. a shadow
	 * copy of the device tree, IO mappings, etc.
	 */
	cache_prom_data();

	/*
	 * Prepare to take over the get/set of environmental variables.
	 */
	promif_prop_init();

	/*
	 * Our prom code emulation runs with preemption disabled
	 * (see kern_preprom) but it can also block in many places waiting
	 * for locks etc...  So we indicate that an explicit cpu binding
	 * is needed to match the "run on a single cpu" constraint.
	 */
	prom_emulated = B_TRUE;

	/*
	 * Switch CIF handler to the kernel.
	 */
	prom_cif_handler = cif_handler;

	promif_preprom();
	cif_handler = kern_cif_handler;

	/*
	 * Take over rtba for the boot CPU. The rtba for
	 * all other CPUs are set as they enter the system.
	 */
	rtba = va_to_pa(&trap_table);
	if ((rv = hv_cpu_set_rtba(&rtba)) != H_EOK)
		panic("hv_cpu_set_rtba failed: %ld\n", rv);

	promif_postprom();

	/*
	 * If the system has been booted with kmdb we need kmdb to
	 * use the kernel cif handler instead of the PROM cif handler.
	 */
	if (boothowto & RB_KMDB) {
		kmdb_cb = (void (*)(void))modlookup("misc/kmdbmod",
		    "kctl_switch_promif");
		ASSERT(kmdb_cb != NULL);
		(*kmdb_cb)();
	}

	cif_check_cpus();

	if (transroot != NULL)
		unmap_prom_mappings(transroot, ntransroot);

	hsvc_boot_cleanup();
}

static void
cache_prom_data(void)
{
	/* initialize copy of OBP device tree */
	promif_stree_init();

	/* initialize io parameters */
	promif_io_init();
}

#endif	/* _KMDB */
