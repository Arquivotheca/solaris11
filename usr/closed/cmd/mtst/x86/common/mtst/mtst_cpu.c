/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Management of mtst CPU support modules
 */

#include <umem.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/x86_archext.h>
#include <sys/devfm.h>
#include <fm/fmd_agent.h>
#include <libnvpair.h>

#include <mtst_debug.h>
#include <mtst_err.h>
#include <mtst_cmd.h>
#include <mtst_cpu.h>
#include <mtst_cpumod_api.h>
#include <mtst.h>

struct mtst_cpu_info {
	mtst_cpuid_t mci_cpuid;
	char mci_vendorstr[X86_VENDOR_STRLEN];
	uint_t mci_family;
	uint_t mci_model;
	uint_t mci_step;
};

#define	MTST_CPUINFOWALK_NEXT	0
#define	MTST_CPUINFOWALK_DONE	1
#define	MTST_CPUINFOWALK_ERR	2

static nvlist_t *
mtst_cpuinfo_walk_cb(int (*cbfunc)(nvlist_t *, void *), void *cbarg)
{
	static nvlist_t **cpus;
	static uint_t ncpu;
	int i;

	if (cbfunc == NULL && cbarg == NULL) {
		if (cpus != NULL) {
			for (i = 0; i < ncpu; i++)
				nvlist_free(cpus[i]);
			umem_free(cpus, sizeof (nvlist_t *) * ncpu);
			cpus = NULL;
			ncpu = 0;
		}
		return (NULL);
	}

	if (cpus == NULL) {
		fmd_agent_hdl_t *hdl;

		if ((hdl = fmd_agent_open(FMD_AGENT_VERSION)) == NULL)
			return (NULL);
		if (fmd_agent_physcpu_info(hdl, &cpus, &ncpu) != 0) {
			fmd_agent_close(hdl);
			return (NULL);
		}
		fmd_agent_close(hdl);
	}

	for (i = 0; i < ncpu; i++) {
		switch (cbfunc(cpus[i], cbarg)) {
		case MTST_CPUINFOWALK_NEXT:
			continue;
			/*NOTREACHED*/

		case MTST_CPUINFOWALK_DONE:
			return (cpus[i]);
			/*NOTREACHED*/

		case MTST_CPUINFOWALK_ERR:
			continue;
			/*NOTREACHED*/

		default:
			mtst_die("cpuinfo_walk_cb: unexpected return value\n");
			break;
		}
	}

	return (NULL);
}

static int
mtst_logicalid_cb(nvlist_t *nvl, void *arg)
{
	mtst_cpuid_t *cpuidp = arg;
	int32_t lgid;

	if (nvlist_lookup_int32(nvl, FM_PHYSCPU_INFO_CPU_ID, &lgid) != 0)
		return (MTST_CPUINFOWALK_ERR);

	return (lgid == cpuidp->mci_cpuid ?
	    MTST_CPUINFOWALK_DONE : MTST_CPUINFOWALK_NEXT);
}

static int
mtst_idtuple_cb(nvlist_t *nvl, void *arg)
{
	mtst_cpuid_t *cpuidp = arg;
	int32_t chp, cr, str;

	if (nvlist_lookup_int32(nvl, FM_PHYSCPU_INFO_CHIP_ID, &chp) != 0 ||
	    nvlist_lookup_int32(nvl, FM_PHYSCPU_INFO_CORE_ID, &cr) != 0 ||
	    nvlist_lookup_int32(nvl, FM_PHYSCPU_INFO_STRAND_ID, &str) != 0) {
		return (MTST_CPUINFOWALK_ERR);
	}

	return (chp == cpuidp->mci_hwchipid && cr == cpuidp->mci_hwcoreid &&
	    str == cpuidp->mci_hwstrandid ? MTST_CPUINFOWALK_DONE :
	    MTST_CPUINFOWALK_NEXT);
}

static mtst_cpu_info_t *
mtst_cpuinfo(nvlist_t *nvl)
{
	mtst_cpu_info_t *ci;
	int32_t chp, cr, str, lgid, family, model, step, nid, nn;
	char *ven;

	ci = umem_zalloc(sizeof (mtst_cpu_info_t), UMEM_NOFAIL);

	if (nvlist_lookup_int32(nvl, FM_PHYSCPU_INFO_CHIP_ID, &chp) != 0 ||
	    nvlist_lookup_int32(nvl, FM_PHYSCPU_INFO_CORE_ID, &cr) != 0 ||
	    nvlist_lookup_int32(nvl, FM_PHYSCPU_INFO_STRAND_ID, &str) != 0 ||
	    nvlist_lookup_int32(nvl, FM_PHYSCPU_INFO_CPU_ID, &lgid) != 0 ||
	    nvlist_lookup_int32(nvl, FM_PHYSCPU_INFO_PROCNODE_ID, &nid) != 0 ||
	    nvlist_lookup_int32(nvl, FM_PHYSCPU_INFO_NPROCNODES, &nn) != 0 ||
	    nvlist_lookup_string(nvl, FM_PHYSCPU_INFO_VENDOR_ID, &ven) != 0 ||
	    nvlist_lookup_int32(nvl, FM_PHYSCPU_INFO_FAMILY, &family) != 0 ||
	    nvlist_lookup_int32(nvl, FM_PHYSCPU_INFO_MODEL, &model) != 0 ||
	    nvlist_lookup_int32(nvl, FM_PHYSCPU_INFO_STEPPING, &step) != 0) {
		mtst_die("mtst_cpuinfo: cannot lookup all properties\n");
		return (NULL);
	}

	ci->mci_cpuid.mci_hwchipid = chp;
	ci->mci_cpuid.mci_hwcoreid = cr;
	ci->mci_cpuid.mci_hwstrandid = str;
	ci->mci_cpuid.mci_cpuid = lgid;
	ci->mci_cpuid.mci_hwprocnodeid = nid;
	ci->mci_cpuid.mci_procnodes_per_pkg = nn;

	(void) snprintf(ci->mci_vendorstr, X86_VENDOR_STRLEN, "%s", ven);

	ci->mci_family = family;
	ci->mci_model = model;
	ci->mci_step = step;

	mtst_dprintf("found CPU %d: chip/core/strand %d/%d/%d, "
	    "vid %s, f/m/s: %u/%u/%u\n",
	    lgid, chp, cr, str,
	    ven, family, model, step);

	return (ci);
}

mtst_cpu_info_t *
mtst_cpuinfo_read_logicalid(uint64_t cpuid)
{
	mtst_cpuid_t cpi;
	nvlist_t *nvl;

	cpi.mci_hwchipid = -1;
	cpi.mci_hwcoreid = -1;
	cpi.mci_hwstrandid = -1;
	cpi.mci_cpuid = cpuid;
	cpi.mci_hwprocnodeid = -1;
	cpi.mci_procnodes_per_pkg = -1;

	if ((nvl = mtst_cpuinfo_walk_cb(mtst_logicalid_cb, &cpi)) == NULL)
		return (NULL);

	return (mtst_cpuinfo(nvl));

}

mtst_cpu_info_t *
mtst_cpuinfo_read_idtuple(uint64_t chip, uint64_t core, uint64_t strand)
{
	mtst_cpuid_t cpuid;
	nvlist_t *nvl;

	cpuid.mci_hwchipid = chip;
	cpuid.mci_hwcoreid = core;
	cpuid.mci_hwstrandid = strand;
	cpuid.mci_cpuid = -1;

	if ((nvl = mtst_cpuinfo_walk_cb(mtst_idtuple_cb, &cpuid)) == NULL)
		return (NULL);

	return (mtst_cpuinfo(nvl));
}

void
mtst_ntv_cpuinfo_destroy(void)
{
	if (mtst.mtst_cpuinfo != NULL)
		umem_free(mtst.mtst_cpuinfo, sizeof (mtst_cpu_info_t));

	(void) mtst_cpuinfo_walk_cb(NULL, NULL);
}

mtst_cpuid_t *
mtst_cpuid(void)
{
	if (mtst.mtst_cpuinfo == NULL)
		mtst_die("mtst_cpuid called before info initialized");

	return (&mtst.mtst_cpuinfo->mci_cpuid);
}

static void
mtst_cpumod_tryload_one(const char *path)
{
	mtst_cpumod_impl_t *mcpu;
	const mtst_cpumod_t *apimcpu;
	const mtst_cpumod_t *(*func)(void);
	void *hdl;

	if ((hdl = dlopen(path, RTLD_NOW)) == NULL)
		return;

	if ((func = (const mtst_cpumod_t *(*)())dlsym(hdl,
	    "_mtst_cpumod_init")) == NULL) {
		(void) dlclose(hdl);
		mtst_dprintf("failed to locate _mtst_cpumod_init in %s\n",
		    path);
		return;
	}

	if ((apimcpu = func()) == NULL) {
		(void) dlclose(hdl);
		mtst_dprintf("_mtst_cpumod_init failed in %s\n", path);
		return;
	}

	if (apimcpu->mcpu_version != MTST_CPUMOD_VERSION) {
		(void) dlclose(hdl);
		mtst_warn("CPU module %s is of version %d, expected %d, and "
		    "can't be loaded\n", path, apimcpu->mcpu_version,
		    MTST_CPUMOD_VERSION);
		return;
	}

	if (apimcpu->mcpu_cmds == NULL) {
		(void) dlclose(hdl);
		mtst_warn("CPU module %s doesn't contain any commands\n",
		    apimcpu->mcpu_name);
		return;
	}

	mcpu = umem_zalloc(sizeof (mtst_cpumod_impl_t), UMEM_NOFAIL);
	mcpu->mcpu_hdl = hdl;
	mcpu->mcpu_name = apimcpu->mcpu_name;
	mcpu->mcpu_ops = apimcpu->mcpu_ops;

	mtst_cmd_register(mcpu, apimcpu->mcpu_cmds);

	mtst_dprintf("successfully loaded cpu module \"%s\"\n",
	    mcpu->mcpu_name);

	mtst_list_prepend(&mtst.mtst_cpumods, mcpu);
}

void
mtst_cpumod_load(void)
{
	static const char *const patterns[] = {
		"mtst_generic.so",
		"mtst_%s.so",		/* vendorstr */
		"mtst_%s_%u.so",	/* + family */
		"mtst_%s_%u_%u.so",	/* + model */
		"mtst_%s_%u_%u_%u.so",	/* + stepping */
		NULL
	};

	mtst_cpu_info_t *cpuinfo = mtst.mtst_cpuinfo;
	char path[MAXPATHLEN];
	size_t left;
	char *name;
	int n, i;

	n = snprintf(path, sizeof (path), "%s/%s/", mtst.mtst_rootdir,
	    MTST_CPUMOD_SUBDIR);
	if (n >= sizeof (path)) {
		mtst_die("invalid CPU module directory %s/%s",
		    mtst.mtst_rootdir, MTST_CPUMOD_SUBDIR);
	}
	name = path + n;
	left = sizeof (path) - n;

	for (i = 0; patterns[i] != NULL; i++) {
		(void) snprintf(name, left, patterns[i], cpuinfo->mci_vendorstr,
		    cpuinfo->mci_family, cpuinfo->mci_model, cpuinfo->mci_step);
		mtst_cpumod_tryload_one(path);
	}

	if (mtst_list_next(&mtst.mtst_cpumods) == NULL) {
		mtst_die("failed to locate a suitable CPU module for "
		    "chip %d core %d strand %d (cpuid %d)\n",
		    mtst.mtst_cpuid->mci_hwchipid,
		    mtst.mtst_cpuid->mci_hwcoreid,
		    mtst.mtst_cpuid->mci_hwstrandid,
		    mtst.mtst_cpuid->mci_cpuid);
	}
}

static void
mtst_cpumod_unload_one(mtst_cpumod_impl_t *mcpu)
{
	mtst_dprintf("unloading CPU module %s\n", mcpu->mcpu_name);

	mcpu->mcpu_ops->mco_fini();
	(void) dlclose(mcpu);

	mtst_list_delete(&mtst.mtst_cpumods, mcpu);
	umem_free(mcpu, sizeof (mtst_cpumod_impl_t));
}

void
mtst_cpumod_unload(void)
{
	mtst_cpumod_impl_t *mcpu;

	/* Unload in the reverse order of loading */
	while ((mcpu = mtst_list_prev(&mtst.mtst_cpumods)) != NULL)
		mtst_cpumod_unload_one(mcpu);
}
