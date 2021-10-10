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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "lint.h"
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/zone.h>
#include <sys/priv.h>
#include <priv_private.h>
#include <zone.h>
#include <sys/tsol/label.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <errno.h>

zoneid_t
zone_create(const char *name, const char *root, const struct priv_set *privs,
    const char *rctls, size_t rctlsz, const char *zfs, size_t zfssz,
    int *extended_error, int match, int doi, const bslabel_t *label, int flags)
{
	zone_def  zd;
	priv_data_t *d;

	LOADPRIVDATA(d);

	zd.zone_name = name;
	zd.zone_root = root;
	zd.zone_privs = privs;
	zd.zone_privssz = d->pd_setsize;
	zd.rctlbuf = rctls;
	zd.rctlbufsz = rctlsz;
	zd.zfsbuf = zfs;
	zd.zfsbufsz = zfssz;
	zd.extended_error = extended_error;
	zd.match = match;
	zd.doi = doi;
	zd.label = label;
	zd.flags = flags;

	return ((zoneid_t)syscall(SYS_zone, ZONE_CREATE, &zd));
}

int
zone_boot(zoneid_t zoneid)
{
	return (syscall(SYS_zone, ZONE_BOOT, zoneid));
}

int
zone_shutdown(zoneid_t zoneid)
{
	return (syscall(SYS_zone, ZONE_SHUTDOWN, zoneid));
}

int
zone_destroy(zoneid_t zoneid)
{
	return (syscall(SYS_zone, ZONE_DESTROY, zoneid));
}

ssize_t
zone_getattr_defunct(uint64_t uniqid, int attr, void *valp, size_t size)
{
	sysret_t rval;
	int error;

	error = __systemcall(&rval, SYS_zone, ZONE_GETATTR_DEFUNCT, &uniqid,
	    attr, valp, size);
	if (error)
		(void) __set_errno(error);
	return ((ssize_t)rval.sys_rval1);
}

ssize_t
zone_getattr(zoneid_t zoneid, int attr, void *valp, size_t size)
{
	sysret_t rval;
	int error;

	error = __systemcall(&rval, SYS_zone, ZONE_GETATTR, zoneid,
	    attr, valp, size);
	if (error)
		(void) __set_errno(error);
	return ((ssize_t)rval.sys_rval1);
}

int
zone_setattr(zoneid_t zoneid, int attr, void *valp, size_t size)
{
	return (syscall(SYS_zone, ZONE_SETATTR, zoneid, attr, valp, size));
}

int
zone_enter(zoneid_t zoneid)
{
	return (syscall(SYS_zone, ZONE_ENTER, zoneid));
}

/*
 * Get id (if any) for specified zone.
 *
 * Call the real zone_get_id() in libzonecfg.so.1 if it can be found.
 * Otherwise, perform a stripped-down version of the function.
 * Any changes in one version should probably be reflected in the other.
 *
 * This stripped-down version of the function only checks for active
 * (booted) zones, by numeric id or name.
 */

typedef	int (*zone_get_id_t)(const char *, zoneid_t *);
static zone_get_id_t real_zone_get_id = NULL;

int
zone_get_id(const char *str, zoneid_t *zip)
{
	zoneid_t zoneid;
	char *cp;

	/*
	 * The first time we are called, attempt to dlopen() libzonecfg.so.1
	 * and get a pointer to the real zone_get_id().
	 * If we fail, set our pointer to -1 so we won't try again.
	 */
	if (real_zone_get_id == NULL) {
		/*
		 * There's no harm in doing this more than once, even
		 * concurrently.  We will get the same result each time,
		 * and the dynamic linker will single-thread the dlopen()
		 * with its own internal lock.  The worst that can happen
		 * is that the handle gets a reference count greater than
		 * one, which doesn't matter since we never dlclose()
		 * the handle if we successfully find the symbol; the
		 * library just stays in the address space until exit().
		 */
		void *dlhandle = dlopen("libzonecfg.so.1", RTLD_LAZY);
		void *sym = (void *)(-1);

		if (dlhandle != NULL &&
		    (sym = dlsym(dlhandle, "zone_get_id")) == NULL) {
			sym = (void *)(-1);
			(void) dlclose(dlhandle);
		}
		real_zone_get_id = (zone_get_id_t)sym;
	}

	/*
	 * If we've successfully loaded it, call the real zone_get_id().
	 * Otherwise, perform our stripped-down version of the code.
	 */
	if (real_zone_get_id != (zone_get_id_t)(-1))
		return (real_zone_get_id(str, zip));

	/* first try looking for active zone by id */
	errno = 0;
	zoneid = (zoneid_t)strtol(str, &cp, 0);
	if (errno == 0 && cp != str && *cp == '\0' &&
	    getzonenamebyid(zoneid, NULL, 0) != -1) {
		*zip = zoneid;
		return (0);
	}

	/* then look for active zone by name */
	if ((zoneid = getzoneidbyname(str)) != -1) {
		*zip = zoneid;
		return (0);
	}

	/* not an active zone, return error */
	return (-1);
}

int
zone_list_defunct(uint64_t *uniqidlist, uint_t *numzones)
{
	return (syscall(SYS_zone, ZONE_LIST_DEFUNCT, uniqidlist, numzones));
}

int
zone_list(zoneid_t *zonelist, uint_t *numzones)
{
	return (syscall(SYS_zone, ZONE_LIST, zonelist, numzones));
}

/*
 * Underlying implementation for getzoneid and getzoneidbyname.
 */
static zoneid_t
zone_lookup(const char *name)
{
	return ((zoneid_t)syscall(SYS_zone, ZONE_LOOKUP, name));
}

zoneid_t
getzoneid(void)
{
	return (zone_lookup(NULL));
}

zoneid_t
getzoneidbyname(const char *zonename)
{
	return (zone_lookup(zonename));
}

ssize_t
getzonenamebyid(zoneid_t zoneid, char *buf, size_t buflen)
{
	return (zone_getattr(zoneid, ZONE_ATTR_NAME, buf, buflen));
}

int
zone_add_datalink(zoneid_t zoneid, datalink_id_t linkid)
{
	return (syscall(SYS_zone, ZONE_ADD_DATALINK, zoneid, linkid));
}

int
zone_remove_datalink(zoneid_t zoneid, datalink_id_t linkid)
{
	return (syscall(SYS_zone, ZONE_DEL_DATALINK, zoneid, linkid));
}

int
zone_check_datalink(zoneid_t *zoneidp, datalink_id_t linkid)
{
	return (syscall(SYS_zone, ZONE_CHECK_DATALINK, zoneidp, linkid));
}

int
zone_list_datalink(zoneid_t zoneid, int *dlnump, datalink_id_t *linkids)
{
	return (syscall(SYS_zone, ZONE_LIST_DATALINK, zoneid, dlnump, linkids));
}


/*
 * Do a dance with the kernel to allocate sufficient memory to hold an
 * array of zoneids and to gather the list of zoneids into said array.
 *
 * This is a convenience wrapper around zone_list.
 *
 * The number of zones may change while we're doing this, but the loop
 * below guarantees that we'll keep going until we have a consistent
 * snapshot.
 *
 * NOTE: This routine allocates memory, which you must free!
 */
int
zone_get_zoneids(zoneid_t **zoneids, uint_t *nzoneids)
{
	zoneid_t *zids = NULL;
	uint_t nzids, nzids_alloced;

	nzids = 1;
	for (;;) {
		/* Shouldn't happen, since the global zone always exists */
		if (nzids == 0) {
			*zoneids = NULL;
			*nzoneids = 0;
			return (0);
		}

		/*
		 * Pad the number of zones a bit, in case the number of zones on
		 * the system is increasing rapidly.
		 */
		nzids_alloced = nzids + 10;
		if ((zids = (zoneid_t *)calloc(nzids_alloced,
		    sizeof (zoneid_t))) == NULL)
			return (-1);

		nzids = nzids_alloced;	/* tell kernel how big zids is */
		if (zone_list(zids, &nzids) != 0) {
			free(zids);
			return (-1);
		}
		if (nzids > nzids_alloced || nzids == 0) {
			/* list grew or something weird happened, try again */
			free(zids);
			continue;
		}
		*zoneids = zids;
		*nzoneids = nzids;
		break;
	}
	return (0);
}
