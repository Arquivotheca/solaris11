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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>

#include <sys/fm/protocol.h>
#include <sys/fm/smb/fmsmb.h>
#include <sys/devfm.h>

#include <sys/cpu_module.h>

#define	ANY_ID		(uint_t)-1

/*
 * INIT_HDLS is the initial size of cmi_hdl_t array.  We fill the array
 * during cmi_hdl_walk, if the array overflows, we will reallocate
 * a new array twice the size of the old one.
 */
#define	INIT_HDLS	16

typedef struct fm_cmi_walk_t
{
	uint_t	chipid;		/* chipid to match during walk */
	uint_t	coreid;		/* coreid to match */
	uint_t	strandid;	/* strandid to match */
	int	(*cbfunc)(cmi_hdl_t, void *, void *);  	/* callback function */
	cmi_hdl_t *hdls;	/* allocated array to save the handles */
	int	nhdl_max;	/* allocated array size */
	int	nhdl;		/* handles saved */
} fm_cmi_walk_t;

extern int x86gentopo_legacy;

int
fm_get_paddr(nvlist_t *nvl, uint64_t *paddr)
{
	uint8_t version;
	uint64_t pa;
	char *scheme;
	int err;

	/* Verify FMRI scheme name and version number */
	if ((nvlist_lookup_string(nvl, FM_FMRI_SCHEME, &scheme) != 0) ||
	    (strcmp(scheme, FM_FMRI_SCHEME_HC) != 0) ||
	    (nvlist_lookup_uint8(nvl, FM_VERSION, &version) != 0) ||
	    version > FM_HC_SCHEME_VERSION) {
		return (EINVAL);
	}

	if ((err = cmi_mc_unumtopa(NULL, nvl, &pa)) != CMI_SUCCESS &&
	    err != CMIERR_MC_PARTIALUNUMTOPA)
		return (EINVAL);

	*paddr = pa;
	return (0);
}

/*
 * Routines for cmi handles walk.
 */

static void
walk_init(fm_cmi_walk_t *wp, uint_t chipid, uint_t coreid, uint_t strandid,
    int (*cbfunc)(cmi_hdl_t, void *, void *))
{
	wp->chipid = chipid;
	wp->coreid = coreid;
	wp->strandid = strandid;
	/*
	 * If callback is not set, we allocate an array to save the
	 * cmi handles.
	 */
	if ((wp->cbfunc = cbfunc) == NULL) {
		wp->hdls = kmem_alloc(sizeof (cmi_hdl_t) * INIT_HDLS, KM_SLEEP);
		wp->nhdl_max = INIT_HDLS;
		wp->nhdl = 0;
	}
}

static void
walk_fini(fm_cmi_walk_t *wp)
{
	if (wp->cbfunc == NULL)
		kmem_free(wp->hdls, sizeof (cmi_hdl_t) * wp->nhdl_max);
}

static int
select_cmi_hdl(cmi_hdl_t hdl, void *arg1, void *arg2, void *arg3)
{
	fm_cmi_walk_t *wp = (fm_cmi_walk_t *)arg1;

	if (wp->chipid != ANY_ID && wp->chipid != cmi_hdl_chipid(hdl))
		return (CMI_HDL_WALK_NEXT);
	if (wp->coreid != ANY_ID && wp->coreid != cmi_hdl_coreid(hdl))
		return (CMI_HDL_WALK_NEXT);
	if (wp->strandid != ANY_ID && wp->strandid != cmi_hdl_strandid(hdl))
		return (CMI_HDL_WALK_NEXT);

	/*
	 * Call the callback function if any exists, otherwise we hold a
	 * reference of the handle and push it to preallocated array.
	 * If the allocated array is going to overflow, reallocate a
	 * bigger one to replace it.
	 */
	if (wp->cbfunc != NULL)
		return (wp->cbfunc(hdl, arg2, arg3));

	if (wp->nhdl == wp->nhdl_max) {
		size_t sz = sizeof (cmi_hdl_t) * wp->nhdl_max;
		cmi_hdl_t *newarray = kmem_alloc(sz << 1, KM_SLEEP);

		bcopy(wp->hdls, newarray, sz);
		kmem_free(wp->hdls, sz);
		wp->hdls = newarray;
		wp->nhdl_max <<= 1;
	}

	cmi_hdl_hold(hdl);
	wp->hdls[wp->nhdl++] = hdl;

	return (CMI_HDL_WALK_NEXT);
}

static void
populate_cpu(nvlist_t **nvlp, cmi_hdl_t hdl)
{
	uint_t	fm_chipid;
	uint16_t smbios_id;

	(void) nvlist_alloc(nvlp, NV_UNIQUE_NAME, KM_SLEEP);

	/*
	 * If SMBIOS satisfies FMA Topology needs, gather
	 * more information on the chip's physical roots
	 * like /chassis=x/motherboard=y/cpuboard=z and
	 * set the chip_id to match the SMBIOS' Type 4
	 * ordering & this has to match the ereport's chip
	 * resource instance derived off of SMBIOS.
	 * Multi-Chip-Module support should set the chipid
	 * in terms of the processor package rather than
	 * the die/node in the processor package, for FM.
	 */

	if (!x86gentopo_legacy) {
		smbios_id = cmi_hdl_smbiosid(hdl);
		fm_chipid = cmi_hdl_smb_chipid(hdl);
		(void) nvlist_add_nvlist(*nvlp, FM_PHYSCPU_INFO_CHIP_ROOTS,
		    cmi_hdl_smb_bboard(hdl));
		(void) nvlist_add_uint16(*nvlp, FM_PHYSCPU_INFO_SMBIOS_ID,
		    (uint16_t)smbios_id);
	} else
		fm_chipid = cmi_hdl_chipid(hdl);

	fm_payload_set(*nvlp,
	    FM_PHYSCPU_INFO_VENDOR_ID, DATA_TYPE_STRING,
	    cmi_hdl_vendorstr(hdl),
	    FM_PHYSCPU_INFO_FAMILY, DATA_TYPE_INT32,
	    (int32_t)cmi_hdl_family(hdl),
	    FM_PHYSCPU_INFO_MODEL, DATA_TYPE_INT32,
	    (int32_t)cmi_hdl_model(hdl),
	    FM_PHYSCPU_INFO_STEPPING, DATA_TYPE_INT32,
	    (int32_t)cmi_hdl_stepping(hdl),
	    FM_PHYSCPU_INFO_CHIP_ID, DATA_TYPE_INT32,
	    (int32_t)fm_chipid,
	    FM_PHYSCPU_INFO_NPROCNODES, DATA_TYPE_INT32,
	    (int32_t)cmi_hdl_procnodes_per_pkg(hdl),
	    FM_PHYSCPU_INFO_PROCNODE_ID, DATA_TYPE_INT32,
	    (int32_t)cmi_hdl_procnodeid(hdl),
	    FM_PHYSCPU_INFO_CORE_ID, DATA_TYPE_INT32,
	    (int32_t)cmi_hdl_coreid(hdl),
	    FM_PHYSCPU_INFO_STRAND_ID, DATA_TYPE_INT32,
	    (int32_t)cmi_hdl_strandid(hdl),
	    FM_PHYSCPU_INFO_STRAND_APICID, DATA_TYPE_INT32,
	    (int32_t)cmi_hdl_strand_apicid(hdl),
	    FM_PHYSCPU_INFO_CHIP_REV, DATA_TYPE_STRING,
	    cmi_hdl_chiprevstr(hdl),
	    FM_PHYSCPU_INFO_SOCKET_TYPE, DATA_TYPE_UINT32,
	    (uint32_t)cmi_hdl_getsockettype(hdl),
	    FM_PHYSCPU_INFO_CPU_ID, DATA_TYPE_INT32,
	    (int32_t)cmi_hdl_logical_id(hdl),
	    NULL);
}

/*ARGSUSED*/
int
fm_ioctl_physcpu_info(int cmd, nvlist_t *invl, nvlist_t **onvlp)
{
	nvlist_t **cpus, *nvl;
	int i, err;
	fm_cmi_walk_t wk;

	/*
	 * Do a walk to save all the cmi handles in the array.
	 */
	walk_init(&wk, ANY_ID, ANY_ID, ANY_ID, NULL);
	cmi_hdl_walk(select_cmi_hdl, &wk, NULL, NULL);

	if (wk.nhdl == 0) {
		walk_fini(&wk);
		return (ENOENT);
	}

	cpus = kmem_alloc(sizeof (nvlist_t *) * wk.nhdl, KM_SLEEP);
	for (i = 0; i < wk.nhdl; i++) {
		populate_cpu(cpus + i, wk.hdls[i]);
		cmi_hdl_rele(wk.hdls[i]);
	}

	walk_fini(&wk);

	(void) nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP);
	err = nvlist_add_nvlist_array(nvl, FM_PHYSCPU_INFO_CPUS,
	    cpus, wk.nhdl);

	for (i = 0; i < wk.nhdl; i++)
		nvlist_free(cpus[i]);
	kmem_free(cpus, sizeof (nvlist_t *) * wk.nhdl);

	if (err != 0) {
		nvlist_free(nvl);
		return (err);
	}

	*onvlp = nvl;
	return (0);
}

int
fm_ioctl_cpu_retire(int cmd, nvlist_t *invl, nvlist_t **onvlp)
{
	int32_t chipid, coreid, strandid;
	int rc, new_status, old_status;
	cmi_hdl_t hdl;
	nvlist_t *nvl;

	switch (cmd) {
	case FM_IOC_CPU_RETIRE:
		new_status = P_FAULTED;
		break;
	case FM_IOC_CPU_STATUS:
		new_status = P_STATUS;
		break;
	case FM_IOC_CPU_UNRETIRE:
		new_status = P_ONLINE;
		break;
	default:
		return (ENOTTY);
	}

	if (nvlist_lookup_int32(invl, FM_CPU_RETIRE_CHIP_ID, &chipid) != 0 ||
	    nvlist_lookup_int32(invl, FM_CPU_RETIRE_CORE_ID, &coreid) != 0 ||
	    nvlist_lookup_int32(invl, FM_CPU_RETIRE_STRAND_ID, &strandid) != 0)
		return (EINVAL);

	hdl = cmi_hdl_lookup(CMI_HDL_NATIVE, chipid, coreid, strandid);
	if (hdl == NULL)
		return (EINVAL);

	rc = cmi_hdl_online(hdl, new_status, &old_status);
	cmi_hdl_rele(hdl);

	if (rc == 0) {
		(void) nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP);
		(void) nvlist_add_int32(nvl, FM_CPU_RETIRE_OLDSTATUS,
		    old_status);
		*onvlp = nvl;
	}

	return (rc);
}

/*
 * Retrun the value of x86gentopo_legacy variable as an nvpair.
 *
 * The caller is responsible for freeing the nvlist.
 */
/* ARGSUSED */
int
fm_ioctl_gentopo_legacy(int cmd, nvlist_t *invl, nvlist_t **onvlp)
{
	nvlist_t *nvl;

	if (cmd != FM_IOC_GENTOPO_LEGACY) {
		return (ENOTTY);
	}

	/*
	 * Inform the caller of the intentions of the ereport generators to
	 * generate either a "generic" or "legacy" x86 topology.
	 */

	(void) nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP);
	(void) nvlist_add_int32(nvl, FM_GENTOPO_LEGACY, x86gentopo_legacy);
	*onvlp = nvl;

	return (0);
}
