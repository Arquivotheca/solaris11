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

#include <sys/types.h>
#include <sys/time.h>
#include <sys/nvpair.h>
#include <sys/cmn_err.h>
#include <sys/cred.h>
#include <sys/open.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/cyclic.h>
#include <sys/errorq.h>
#include <sys/stat.h>
#include <sys/cpuvar.h>
#include <sys/mc_intel.h>
#include <sys/mc.h>
#include <sys/fm/protocol.h>
#include <sys/mem_config.h>
#include "nhmex.h"
#include "nhmex_log.h"

nvlist_t **inhmex_mc_nvl;
krwlock_t inhmex_mc_lock;

char **inhmex_mc_snapshot;
uint_t nhmex_config_gen;
uint_t inhmex_mc_snapshotgen;
size_t *inhmex_mc_snapshotsz;
static dev_info_t *inhmex_dip;

int max_bus_number = 0xff;

extern int nhmex_patrol_scrub;
extern int nhmex_demand_scrub;
extern int nhmex_no_smbios;
extern int nhmex_smbios_serial;
extern int nhmex_smbios_manufacturer;
extern int nhmex_smbios_part_number;
extern int nhmex_smbios_version;
extern int nhmex_smbios_label;
extern int nhmex_max_cpu_nodes;

extern void inhmex_create_nvl(int);
extern char *inhmex_mc_name(void);
extern void init_dimms(void);
extern void nhmex_smbios();

static void nhmex_mem_dr_post_add(void *arg, pgcnt_t delta_pages);
static int nhmex_mem_dr_pre_del(void *arg, pgcnt_t delta);
static void nhmex_mem_dr_post_del(void *arg, pgcnt_t delta_pages,
    int cancelled);

static const cmi_mc_ops_t nhmex_mc_ops = {
	nhmex_patounum,
	nhmex_unumtopa,
	nhmex_error_trap  /* cmi_mc_logout */
};

static kphysm_setup_vector_t nhmex_mem_dr_callback = {
	KPHYSM_SETUP_VECTOR_VERSION,
	nhmex_mem_dr_post_add,
	nhmex_mem_dr_pre_del,
	nhmex_mem_dr_post_del,
};

#pragma weak plat_dr_support_memory
extern boolean_t plat_dr_support_memory(void);

static void
inhmex_mc_snapshot_flush(void)
{
	int i;

	ASSERT(RW_LOCK_HELD(&inhmex_mc_lock));

	for (i = 0; i < nhmex_max_cpu_nodes; i++) {
		if (inhmex_mc_snapshot[i] == NULL)
			continue;

		kmem_free(inhmex_mc_snapshot[i], inhmex_mc_snapshotsz[i]);
		inhmex_mc_snapshot[i] = NULL;
		inhmex_mc_snapshotsz[i] = 0;
	}
}

static int
inhmex_mc_snapshot_update(void)
{
	int i;
	int rt = 0;

	ASSERT(RW_LOCK_HELD(&inhmex_mc_lock));

	for (i = 0; i < nhmex_max_cpu_nodes; i++) {
		if (inhmex_mc_snapshot[i] != NULL || inhmex_mc_nvl[i] == NULL)
			continue;

		if (nvlist_pack(inhmex_mc_nvl[i], &inhmex_mc_snapshot[i],
		    &inhmex_mc_snapshotsz[i], NV_ENCODE_XDR, KM_SLEEP) != 0)
			rt = -1;
	}

	return (rt);
}

static void
inhmex_mc_cache_destroy(void)
{
	int i;

	ASSERT(RW_LOCK_HELD(&inhmex_mc_lock));

	/* No nvlists or snapshots when the topology is unchanged */
	if (inhmex_mc_nvl == NULL)
		return;

	/* Destroy snapshots */
	inhmex_mc_snapshot_flush();
	kmem_free(inhmex_mc_snapshot, sizeof (char *) * nhmex_max_cpu_nodes);
	inhmex_mc_snapshot = NULL;
	kmem_free(inhmex_mc_snapshotsz, sizeof (size_t) * nhmex_max_cpu_nodes);
	inhmex_mc_snapshotsz = NULL;

	/* Destroy nvlists */
	for (i = 0; i < nhmex_max_cpu_nodes; i++) {
		if (inhmex_mc_nvl[i] != NULL) {
			nvlist_free(inhmex_mc_nvl[i]);
			inhmex_mc_nvl[i] = NULL;
		}
	}
	kmem_free(inhmex_mc_nvl, sizeof (nvlist_t *) * nhmex_max_cpu_nodes);
	inhmex_mc_nvl = NULL;
}

/*
 * If there's any memory devices being dynamicly
 * changed, we need to make related data structures
 * up-to-date. So the  topology snapshot could
 * reflect the changes and memory translation could
 * give out the correct result.
 */
static void
nhmex_memory_update(void)
{
	rw_enter(&inhmex_mc_lock, RW_WRITER);

	/*
	 * release all the data
	 */
	nhmex_memtrans_fini();
	inhmex_mc_cache_destroy();
	nhmex_unload();

	/*
	 * rebuild all structures
	 */
	init_dimms();
	(void) nhmex_memtrans_init();

	/*
	 * Increase snapshot generation number to sync with ioctl
	 */
	inhmex_mc_snapshotgen++;

	rw_exit(&inhmex_mc_lock);
}

/*
 * invoked when the hot added memory is put into connected trasition
 */
/*ARGSUSED*/
static void
nhmex_mem_dr_post_add(void *arg, pgcnt_t delta_pages)
{
	nhmex_memory_update();
}

/*ARGSUSED*/
static int
nhmex_mem_dr_pre_del(void *arg, pgcnt_t delta)
{
	return (0);
}

/*
 * invoked right after "remove memory" stage of memory hot-removing process
 */
/*ARGSUSED*/
static void
nhmex_mem_dr_post_del(void *arg, pgcnt_t delta_pages, int cancelled)
{
	nhmex_memory_update();
}


/*ARGSUSED*/
static int
inhmex_mc_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
    int *rvalp)
{
	int rc = 0;
	int chip;
	mc_snapshot_info_t mcs;

	if (cmd != MC_IOC_SNAPSHOT_INFO && cmd != MC_IOC_SNAPSHOT)
		return (EINVAL);

	rw_enter(&inhmex_mc_lock, RW_READER);
	if (inhmex_mc_nvl == NULL) {
		inhmex_mc_nvl =
		    kmem_zalloc(sizeof (nvlist_t *) * nhmex_max_cpu_nodes,
		    KM_SLEEP);
		inhmex_mc_snapshot =
		    kmem_zalloc(sizeof (char *) * nhmex_max_cpu_nodes,
		    KM_SLEEP);
		inhmex_mc_snapshotsz =
		    kmem_zalloc(sizeof (size_t) * nhmex_max_cpu_nodes,
		    KM_SLEEP);
	}
	chip = getminor(dev);
	if (chip >= nhmex_max_cpu_nodes) {
		rw_exit(&inhmex_mc_lock);
		return (ENOENT);
	}
	if (inhmex_mc_nvl[chip] == NULL ||
	    inhmex_mc_snapshotgen != nhmex_config_gen) {
		if (!rw_tryupgrade(&inhmex_mc_lock)) {
			rw_exit(&inhmex_mc_lock);
			return (EAGAIN);
		}
		if (inhmex_mc_snapshotgen != nhmex_config_gen) {
			inhmex_mc_snapshot_flush();
			nhmex_config_gen = inhmex_mc_snapshotgen;
		}
		inhmex_create_nvl(chip);
		(void) inhmex_mc_snapshot_update();
	}
	switch (cmd) {
	case MC_IOC_SNAPSHOT_INFO:
		mcs.mcs_size = (uint32_t)inhmex_mc_snapshotsz[chip];
		mcs.mcs_gen = inhmex_mc_snapshotgen;

		if (ddi_copyout(&mcs, (void *)arg, sizeof (mc_snapshot_info_t),
		    mode) < 0)
			rc = EFAULT;
		break;
	case MC_IOC_SNAPSHOT:
		if (ddi_copyout(inhmex_mc_snapshot[chip], (void *)arg,
		    inhmex_mc_snapshotsz[chip], mode) < 0)
			rc = EFAULT;
		break;
	}
	rw_exit(&inhmex_mc_lock);
	return (rc);
}

/*ARGSUSED*/
static int
inhmex_mc_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
    void **result)
{
	if ((infocmd != DDI_INFO_DEVT2DEVINFO &&
	    infocmd != DDI_INFO_DEVT2INSTANCE) || inhmex_dip == NULL) {
		*result = NULL;
		return (DDI_FAILURE);
	}
	if (infocmd == DDI_INFO_DEVT2DEVINFO)
		*result = inhmex_dip;
	else
		*result = (void *)(uintptr_t)ddi_get_instance(inhmex_dip);
	return (0);
}

static int
inhmex_mc_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int i, rt;
	char buf[64];

	if (cmd == DDI_RESUME) {
		nhmex_dev_reinit();
		nhmex_scrubber_enable();
		nhmex_smbios();
		return (DDI_SUCCESS);
	}
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	rw_enter(&inhmex_mc_lock, RW_WRITER);
	if (inhmex_dip == NULL) {
		inhmex_dip = dip;
		(void) ddi_prop_update_string(DDI_DEV_T_NONE, dip, "model",
		    inhmex_mc_name());
		nhmex_pci_cfg_setup(dip);
		if (nhmex_dev_init() || nhmex_memtrans_init() == 0xffffffff) {
			nhmex_pci_cfg_free();
			inhmex_dip = NULL;
			rw_exit(&inhmex_mc_lock);
			return (DDI_FAILURE);
		}
		ddi_set_name_addr(dip, "1");
		for (i = 0; i < MAX_CPU_NODES; i++) {
			(void) snprintf(buf, sizeof (buf), "mc-intel-%d", i);
			if (ddi_create_minor_node(dip, buf, S_IFCHR,
			    i, "ddi_mem_ctrl", 0) != DDI_SUCCESS) {
				cmn_err(CE_WARN, "failed to create minor node"
				    " for memory controller %d\n", i);
			}
		}
		if (cmi_mc_register_global(&nhmex_mc_ops, NULL) !=
		    CMI_SUCCESS) {
			cmn_err(CE_WARN, "failed to register global mc_ops\n");
		}
		nhmex_patrol_scrub = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "patrol-scrub", 0);
		nhmex_demand_scrub = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "demand-scrub", 0);
		nhmex_no_smbios = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "no-smbios", 0);

		/*
		 * SMBIOS information is static, it will not be updated
		 * to reflect the dynamic change of some hardware
		 * components. When the system is hotplug
		 * capable, we need to surpress the usage of SMBIOS
		 * because after hot-add/hot-remove some items
		 * will be obsolete or missing.
		 */

		/*
		 * check if the system support memory hotplug
		 */
		if (&plat_dr_support_memory != NULL &&
		    plat_dr_support_memory()) {
			/*
			 * This system is hotplug capable,
			 * we need to surpress smbios information
			 */
			nhmex_no_smbios = 1;
		}

		nhmex_smbios_serial = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "smbios-dimm-serial", 1);
		nhmex_smbios_manufacturer = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "smbios-dimm-manufacturer", 1);
		nhmex_smbios_part_number = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "smbios-dimm-part-number", 1);
		nhmex_smbios_version = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "smbios-dimme-version", 1);
		nhmex_smbios_label = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "smbios-dimm-label", 1);
		nhmex_scrubber_enable();
		nhmex_smbios();
		rt = kphysm_setup_func_register(&nhmex_mem_dr_callback,
		    (void *)NULL);
		ASSERT(rt == 0);
	}
	rw_exit(&inhmex_mc_lock);

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
inhmex_mc_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	if (cmd == DDI_DETACH && dip == inhmex_dip) {
		return (DDI_FAILURE);
	} else if (cmd == DDI_SUSPEND) {
		return (DDI_SUCCESS);
	} else {
		return (DDI_FAILURE);
	}
}

/*ARGSUSED*/
static int
inhmex_mc_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	int slot;

	if (otyp != OTYP_CHR)
		return (EINVAL);

	slot = getminor(*devp);
	if (slot >= MAX_CPU_NODES) {
		return (EINVAL);
	}
	rw_enter(&inhmex_mc_lock, RW_READER);
	if (slot >= nhmex_max_cpu_nodes) {
		rw_exit(&inhmex_mc_lock);
		return (ENOENT);
	}
	rw_exit(&inhmex_mc_lock);

	return (0);
}

/*ARGSUSED*/
static int
inhmex_mc_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	return (0);
}


static struct cb_ops inhmex_mc_cb_ops = {
	inhmex_mc_open,
	inhmex_mc_close,
	nodev,		/* not a block driver */
	nodev,		/* no print routine */
	nodev,		/* no dump routine */
	nodev,		/* no read routine */
	nodev,		/* no write routine */
	inhmex_mc_ioctl,
	nodev,		/* no devmap routine */
	nodev,		/* no mmap routine */
	nodev,		/* no segmap routine */
	nochpoll,	/* no chpoll routine */
	ddi_prop_op,
	0,		/* not a STREAMS driver */
	D_NEW | D_MP,	/* safe for multi-thread/multi-processor */
};

static struct dev_ops inhmex_mc_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	inhmex_mc_getinfo,		/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	inhmex_mc_attach,		/* devo_attach */
	inhmex_mc_detach,		/* devo_detach */
	nodev,			/* devo_reset */
	&inhmex_mc_cb_ops,		/* devo_cb_ops */
	NULL,			/* devo_bus_ops */
	NULL,			/* devo_power */
	ddi_quiesce_not_needed,	/* devo_quiesce */
};

static struct modldrv modldrv = {
	&mod_driverops,
	"Intel QuickPath Memory Controller Hub Module",
	&inhmex_mc_ops
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modldrv,
	NULL
};

int
_init(void)
{
	int err;

	err = nhmex_init();
	if (err == 0) {
		rw_init(&inhmex_mc_lock, NULL, RW_DRIVER, NULL);
		err = mod_install(&modlinkage);
		if (err != 0) {
			rw_destroy(&inhmex_mc_lock);
		}
	}

	return (err);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	int err;

	if ((err = mod_remove(&modlinkage)) == 0) {
		nhmex_unload();
		rw_destroy(&inhmex_mc_lock);
	}

	return (err);
}
