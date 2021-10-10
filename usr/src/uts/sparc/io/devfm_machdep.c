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
#include <sys/systm.h>
#include <sys/cmn_err.h>
#include <sys/kstat.h>

#include <sys/fm/protocol.h>
#include <sys/devfm.h>

#ifdef sun4v

#include <sys/hypervisor_api.h>
#include <sys/hsvc.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/atomic.h>
#include <sys/note.h>

/*
 * This file contains kernel interfaces to perform Cache Line Retire (CLR)
 * operations under sun4v.  The interfaces are built above a set of
 * hypercalls (API group 0x020),
 *
 * 	hv_clm_retire
 * 	hv_clm_unretire
 * 	hv_clm_status
 *
 * In some circumstances, eg. the cpu node owns the line is outside of the
 * primary domain, the operations will be scheduled and done asynchronously.
 * In this case, the above hcalls will return pending status in ret1.  Then
 * it's up to the OS to poll the cache line's state until the expected state
 * is seen (see fm_clr_poll()).   The guest will pause briefly before
 * retrying to read the line state.  Such implementation also limit the number
 * of outstanding or concurrent operations to as few as 1 operation at a time.
 *
 */

#define	FM_CLR_DEF_DELAY	20
#define	FM_CLR_DEF_MAXTRIES	50

struct fm_clr_kstat {
	kstat_named_t	clr_l2_retired;
	kstat_named_t	clr_l2_retire_request;
	kstat_named_t	clr_l2_unretired;
	kstat_named_t	clr_l2_unretire_request;
	kstat_named_t	clr_l2_status_request;
	kstat_named_t	clr_l2_status_fail;
	kstat_named_t	clr_l3_retired;
	kstat_named_t	clr_l3_retire_request;
	kstat_named_t	clr_l3_unretired;
	kstat_named_t	clr_l3_unretire_request;
	kstat_named_t	clr_l3_status_request;
	kstat_named_t	clr_l3_status_fail;
};

static struct fm_clr_kstat fm_clr_kstat = {
	{ "l2line_retired",		KSTAT_DATA_UINT64 },
	{ "l2line_retire_request",	KSTAT_DATA_UINT64 },
	{ "l2line_unretired",		KSTAT_DATA_UINT64 },
	{ "l2line_unretire_request",	KSTAT_DATA_UINT64 },
	{ "l2line_status_request",	KSTAT_DATA_UINT64 },
	{ "l2line_status_fail",		KSTAT_DATA_UINT64 },
	{ "l3line_retired",		KSTAT_DATA_UINT64 },
	{ "l3line_retire_request",	KSTAT_DATA_UINT64 },
	{ "l3line_unretired",		KSTAT_DATA_UINT64 },
	{ "l3line_unretire_request",	KSTAT_DATA_UINT64 },
	{ "l3line_status_request",	KSTAT_DATA_UINT64 },
	{ "l3line_status_fail",		KSTAT_DATA_UINT64 }
};

static kstat_t *fm_clr_ksp = NULL;

#define	CLR_INC_KSTAT(level, stat)					\
	do {								\
		if (level == FM_CLR_L2_CACHE)				\
			atomic_inc_64(&fm_clr_kstat.clr_l2_##stat.value.ui64);\
		else if (level == FM_CLR_L3_CACHE)			\
			atomic_inc_64(&fm_clr_kstat.clr_l3_##stat.value.ui64);\
		_NOTE(CONSTANTCONDITION)				\
	} while (0)

kmutex_t fm_clr_hcall_mutex;

/* Tunables */
int fm_clr_delay;		/* delay between polls */
int fm_clr_maxtries;		/* maximum retries for polling line status */

int fm_clr_enable = 1;		/* set to 0 to disable cacheline retire */

/* CLR hcall API group */
static hsvc_info_t fm_clr_hsvc =
	{ HSVC_REV_1, NULL, HSVC_GROUP_CLM, 1, 0, "fm" };

#endif /* sun4v */

extern int cpu_get_mem_addr(char *, char *, uint64_t, uint64_t *);

int
fm_get_paddr(nvlist_t *nvl, uint64_t *paddr)
{
	uint8_t version;
	uint64_t pa;
	char *scheme;
	int err;
	uint64_t offset;
	char *unum;
	char **serids;
	uint_t nserids;

	/* Verify FMRI scheme name and version number */
	if ((nvlist_lookup_string(nvl, FM_FMRI_SCHEME, &scheme) != 0) ||
	    (strcmp(scheme, FM_FMRI_SCHEME_MEM) != 0) ||
	    (nvlist_lookup_uint8(nvl, FM_VERSION, &version) != 0) ||
	    version > FM_MEM_SCHEME_VERSION) {
		return (EINVAL);
	}

	/*
	 * There are two ways a physical address can be  obtained from a mem
	 * scheme FMRI.  One way is to use the "offset" and  "serial"
	 * members, if they are present, together with the "unum" member to
	 * calculate a physical address.  This is the preferred way since
	 * it is independent of possible changes to the programming of
	 * underlying hardware registers that may change the physical address.
	 * If the "offset" member is not present, then the address is
	 * retrieved from the "physaddr" member.
	 */
	if (nvlist_lookup_uint64(nvl, FM_FMRI_MEM_OFFSET, &offset) != 0) {
		if (nvlist_lookup_uint64(nvl, FM_FMRI_MEM_PHYSADDR, &pa) !=
		    0) {
			return (EINVAL);
		}
	} else if (nvlist_lookup_string(nvl, FM_FMRI_MEM_UNUM, &unum) != 0 ||
	    nvlist_lookup_string_array(nvl, FM_FMRI_MEM_SERIAL_ID, &serids,
	    &nserids) != 0) {
		return (EINVAL);
	} else {
		err = cpu_get_mem_addr(unum, serids[0], offset, &pa);
		if (err != 0) {
			if (err == ENOTSUP) {
				/* Fall back to physaddr */
				if (nvlist_lookup_uint64(nvl,
				    FM_FMRI_MEM_PHYSADDR, &pa) != 0)
					return (EINVAL);
			} else
				return (err);
		}
	}

	*paddr = pa;
	return (0);
}

#ifdef sun4v

void
fm_clr_attach(dev_info_t *dip)
{
	int propval;

	ASSERT(fm_clr_ksp == NULL);

	fm_clr_ksp = kstat_create("fm", 0, "cache_retire", "misc",
	    KSTAT_TYPE_NAMED,
	    sizeof (fm_clr_kstat) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);

	if (fm_clr_ksp == NULL) {
		cmn_err(CE_WARN,
		    "kstat_create for cache_retire failed");
	} else {
		fm_clr_ksp->ks_data = &fm_clr_kstat;
		fm_clr_ksp->ks_update = nulldev;
		kstat_install(fm_clr_ksp);
	}

	mutex_init(&fm_clr_hcall_mutex, NULL, MUTEX_DEFAULT, NULL);

	if ((propval = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "delay", FM_CLR_DEF_DELAY)) >= 0)
		fm_clr_delay = propval;
	if ((propval = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "maxtries", FM_CLR_DEF_MAXTRIES)) >= 0)
		fm_clr_maxtries = propval;
}

void
fm_clr_detach(void)
{
	if (fm_clr_ksp != NULL) {
		kstat_delete(fm_clr_ksp);
		fm_clr_ksp = NULL;
	}

	mutex_destroy(&fm_clr_hcall_mutex);
}

/*
 * Map the errors returned by CLR hypercalls to the kernel errors.
 *
 * These errors can be returned from CLR hcalls:
 *
 * H_EINVAL		Invalid argument(s)
 * H_ENOACCESS		Operation only valid in control domain
 * H_EWOULDBLOCK	Operation already in progress
 * H_ENOCPU		CPU is not active
 *
 */
static int
herr2kerr(int herr)
{
	switch (herr) {
	case H_EOK:
		return (0);
	case H_EINVAL:
		return (EINVAL);
	case H_ENOACCESS:
		return (EACCES);
	case H_EWOULDBLOCK:
		return (EWOULDBLOCK);
	case H_ENOCPU:
		/*
		 * Somehow the chosen strand became unavailable, return
		 * EAGAIN so caller will choose another strand and retry.
		 */
	default:
		return (EAGAIN);
	}
}

static const char *
cachetype(uint64_t cachetype, uint64_t cachelevel)
{
	static char cache[4];

	(void) snprintf(cache, sizeof (cache), "%s%s",
	    cachelevel == FM_CLR_L1_CACHE ? "L1" :
	    cachelevel == FM_CLR_L2_CACHE ? "L2" :
	    cachelevel == FM_CLR_L3_CACHE ? "L3" : "L?",
	    cachetype == FM_CLR_INSTR_CACHE ? "i" :
	    cachetype == FM_CLR_DATA_CACHE ? "d" : "");

	return (cache);
}

static const char *
opname(int op)
{
	return (op == FM_IOC_CACHE_RETIRE ? "retire" :
	    op == FM_IOC_CACHE_UNRETIRE ? "unretire" :
	    op == FM_IOC_CACHE_STATUS ? "status" : "unknown");
}

/*
 * The hsvc is registered on demand to increase the success rate of
 * live migration to legacy platforms.
 */
static int
cpu_clr_status(uint64_t strand, uint64_t cachetype,
    uint64_t level, uint64_t index, uint64_t way, uint64_t *status)
{
	uint64_t clr_minor_ver;
	int ret;

	if (hsvc_register(&fm_clr_hsvc, &clr_minor_ver) != 0)
		return (ENOTSUP);

	ret = herr2kerr(hv_clm_status(strand, cachetype, level, index, way,
	    status));

	(void) hsvc_unregister(&fm_clr_hsvc);

	return (ret);
}

static int
cpu_clr_retire(uint64_t strand, uint64_t cachetype,
    uint64_t level, uint64_t index, uint64_t way, uint64_t *status)
{
	uint64_t clr_minor_ver;
	int ret;

	if (hsvc_register(&fm_clr_hsvc, &clr_minor_ver) != 0)
		return (ENOTSUP);

	ret = herr2kerr(hv_clm_retire(strand, cachetype, level, index, way,
	    status));

	(void) hsvc_unregister(&fm_clr_hsvc);

	return (ret);
}

static int
cpu_clr_unretire(uint64_t strand, uint64_t cachetype,
    uint64_t level, uint64_t index, uint64_t way, uint64_t *status)
{
	uint64_t clr_minor_ver;
	int ret;

	if (hsvc_register(&fm_clr_hsvc, &clr_minor_ver) != 0)
		return (ENOTSUP);

	ret = herr2kerr(hv_clm_unretire(strand, cachetype, level, index, way,
	    status));

	(void) hsvc_unregister(&fm_clr_hsvc);

	return (ret);
}

/*
 * If *pollstatus is FM_CLR_STATUS_QUERY, then store line status in it.
 * Otherwise poll line status until it becomes *pollstatus or times out.
 */
static int
fm_clr_poll(uint64_t strand, uint64_t type, uint64_t level, uint64_t index,
    uint64_t way, uint64_t *pollstatus)
{
	int ret;
	uint64_t newstatus;
	int tries = 1;

	for (;;) {
		ret = cpu_clr_status(strand, type, level, index, way,
		    &newstatus);

		fm_debug(FM_DBG_CLR, "CLR poll (%d, %llu), tries = %d: "
		    "strand%llu/%s/index0x%llx/way0x%llx",
		    ret, ret == 0 ? *pollstatus : 0, tries, strand,
		    cachetype(type, level), index, way);

		if (ret != 0)
			break;

		ASSERT(newstatus != FM_CLR_STATUS_QUERY);

		if (*pollstatus == FM_CLR_STATUS_QUERY &&
		    newstatus != FM_CLR_STATUS_PENDING) {
			*pollstatus = newstatus;	/* return line status */
			break;
		}

		/* return EBUSY if we're trying to retire a reserved line */
		if (*pollstatus == FM_CLR_STATUS_RETIRED &&
		    newstatus == FM_CLR_STATUS_RESERVED) {
			ret = EBUSY;
			break;
		}

		/* check if poll can end now, reserved line is always active. */
		if (*pollstatus == newstatus ||
		    (*pollstatus == FM_CLR_STATUS_ACTIVE &&
		    newstatus == FM_CLR_STATUS_RESERVED))
			break;

		/* delay a bit and retry again */
		if (++tries > fm_clr_maxtries) {
			fm_debug(FM_DBG_CLR,
			    "cache line retire poll timed out");
			ret = ETIMEDOUT;
			break;
		}

		delay(fm_clr_delay);
	}

	return (ret);
}

static int
fm_clr_retire(uint64_t strand, uint64_t type, uint64_t level, uint64_t index,
    uint64_t way)
{
	int ret;
	uint64_t status;

	CLR_INC_KSTAT(level, retire_request);

	ret = cpu_clr_retire(strand, type, level, index, way,
	    &status);

	if (ret == 0) {
		if (status == FM_CLR_STATUS_PENDING) {
			status = FM_CLR_STATUS_RETIRED;
			ret = fm_clr_poll(strand, type, level, index, way,
			    &status);
		} else if (status == FM_CLR_STATUS_RESERVED) {
			ret = EBUSY;
		}
	}

	ASSERT(ret != 0 || status == FM_CLR_STATUS_RETIRED);

	if (ret == 0)
		CLR_INC_KSTAT(level, retired);

	return (ret);
}

static int
fm_clr_unretire(uint64_t strand, uint64_t type, uint64_t level, uint64_t index,
    uint64_t way)
{
	int ret;
	uint64_t status;

	CLR_INC_KSTAT(level, unretire_request);

	ret = cpu_clr_unretire(strand, type, level, index, way,
	    &status);

	if (ret == 0 && status == FM_CLR_STATUS_PENDING) {
		status = FM_CLR_STATUS_ACTIVE;
		ret = fm_clr_poll(strand, type, level, index, way, &status);
	}

	ASSERT(ret != 0 || status == FM_CLR_STATUS_ACTIVE);

	if (ret == 0)
		CLR_INC_KSTAT(level, unretired);

	return (ret);
}

static int
fm_clr_status(uint64_t strand, uint64_t type, uint64_t level, uint64_t index,
    uint64_t way, uint64_t *status)
{
	int ret;
	uint64_t st;

	CLR_INC_KSTAT(level, status_request);

	st = FM_CLR_STATUS_QUERY;
	ret = fm_clr_poll(strand, type, level, index, way, &st);

	if (ret == 0) {
		ASSERT(st != FM_CLR_STATUS_QUERY);
		*status = st;
	} else {
		CLR_INC_KSTAT(level, status_fail);
	}

	return (ret);
}

int
fm_ioctl_cache_retire(int cmd, nvlist_t *invl, nvlist_t **onvlp)
{
	int ret;
	uint64_t strand, type, level, index, way;
	uint64_t status;
	nvlist_t *nvl = NULL;

	if (! fm_clr_enable)
		return (ENOTSUP);

	ret = nvlist_lookup_uint64(invl, FM_CLR_STRAND_ID, &strand);
	ret |= nvlist_lookup_uint64(invl, FM_CLR_CACHE_TYPE, &type);
	ret |= nvlist_lookup_uint64(invl, FM_CLR_CACHE_LEVEL, &level);
	ret |= nvlist_lookup_uint64(invl, FM_CLR_CACHE_INDEX, &index);
	ret |= nvlist_lookup_uint64(invl, FM_CLR_CACHE_WAY, &way);

	if (ret != 0)
		return (EINVAL);

	/* Enter global lock since HV only allows one request at a time */
	mutex_enter(&fm_clr_hcall_mutex);

	fm_debug(FM_DBG_CLR,
	    "CLR %s enter: strand%llu/%s/index0x%llx/way0x%llx",
	    opname(cmd), strand, cachetype(type, level), index, way);

	switch (cmd) {
	case FM_IOC_CACHE_RETIRE:
		ret = fm_clr_retire(strand, type, level, index, way);
		break;

	case FM_IOC_CACHE_UNRETIRE:
		ret = fm_clr_unretire(strand, type, level, index, way);
		break;

	case FM_IOC_CACHE_STATUS:
		ret = fm_clr_status(strand, type, level, index, way, &status);

		if (ret == 0 &&
		    (ret = nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP)) == 0 &&
		    (ret = nvlist_add_int32(nvl, FM_CLR_STATUS, status)) == 0)
			*onvlp = nvl;
		else if (nvl != NULL)
			nvlist_free(nvl);

		break;

	default:
		ret = ENOTTY;
	}

	fm_debug(FM_DBG_CLR,
	    "CLR %s exit (%d, %llu): strand%llu/%s/index0x%llx/way0x%llx",
	    opname(cmd), ret,
	    (cmd == FM_IOC_CACHE_STATUS && ret == 0) ? status : 0,
	    strand, cachetype(type, level), index, way);

	mutex_exit(&fm_clr_hcall_mutex);

	return (ret);
}

#endif /* sun4v */
