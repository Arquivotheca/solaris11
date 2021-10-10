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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * zfs diff support
 */
#include <ctype.h>
#include <errno.h>
#include <libintl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <attr.h>
#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stropts.h>
#include <pthread.h>
#include <sys/zfs_ioctl.h>
#include <strings.h>
#include <libzfs.h>
#include "libzfs_impl.h"

#define	ZDIFF_SNAPDIR		"/.zfs/snapshot/"
#define	ZDIFF_SHARESDIR 	"/.zfs/shares/"
#define	ZDIFF_PREFIX		"zfs-diff-%d"

typedef struct differ_info {
	zfs_handle_t *zhp;
	char *fromsnap;
	char *frommnt;
	char *tosnap;
	char *tomnt;
	char *ds;
	char *dsmnt;
	char *tmpsnap;
	int zerr;
	boolean_t isclone;
	boolean_t enumerate_only;
	boolean_t frombase;
	boolean_t needname;
	boolean_t needoldname;
	uint64_t shares;
	int cleanupfd;
	int outputfd;
	int datafd;
	diff_scan_cb_t	*cb_func;
	void *cb_arg;
	char errbuf[1024];
} differ_info_t;

/*
 * Given a {dsname, object id}, get the object path
 */
static int
get_path_for_obj(differ_info_t *di, const char *dsname, uint64_t obj,
    char *pn, int maxlen, zfs_stat_t *zs_rec, zfs_stat_t *zs_base)
{
	zfs_cmd_t zc = { 0 };
	int error;
	char *name;

	if ((error = zs_rec->zs_nameerr) == 0) {
		ASSERT(zs_rec->zs_nameoff != 0);
		name = (char *)((uintptr_t)zs_base +
		    (uintptr_t)zs_rec->zs_nameoff);
	} else if (error == ZFS_NAMEERR_NO_NAME_REQUESTED) {
		(void) strlcpy(zc.zc_name, dsname, sizeof (zc.zc_name));
		zc.zc_obj = obj;
		if (ioctl(di->zhp->zfs_hdl->libzfs_fd, ZFS_IOC_OBJ_TO_PATH,
		    &zc) != 0) {
			error = errno;
		}
		name = zc.zc_value;
	}
	di->zerr = error;

	if (error == 0) {
		(void) strlcpy(pn, name, maxlen);
		return (0);
	}

	if (error == ESRCH) {
		(void) sprintf(pn, "/\?\?\?<object#%llu>", (longlong_t)obj);
		di->zerr = 0;
		return (0);
	}
	if (error == EPERM) {
		(void) snprintf(di->errbuf, sizeof (di->errbuf),
		    dgettext(TEXT_DOMAIN,
		    "The sys_config privilege or diff delegated permission "
		    "is needed\nto discover path names"));
		return (-1);
	} else {
		(void) snprintf(di->errbuf, sizeof (di->errbuf),
		    dgettext(TEXT_DOMAIN,
		    "Unable to determine path for "
		    "object %lld in %s"), obj, dsname);
		return (-1);
	}
}



static int
write_inuse_diffs_zero(differ_info_t *di, uint64_t dobj,
	int fobjerr, char *fobjname, zfs_stat_t *fsp,
	int tobjerr, char *tobjname, zfs_stat_t *tsp)
{
	mode_t fmode, tmode;
	int change;

	/*
	 * Unallocated object sharing the same meta dnode block
	 */
	if (fobjerr && tobjerr) {
		di->zerr = 0;
		return (0);
	}

	di->zerr = 0; /* negate get_stats_for_obj() from side that failed */

	if (di->frombase) {
		ASSERT(di->fromsnap == NULL);
		(void) memcpy(fsp, tsp, sizeof (*tsp));
	}

	fmode = fsp->zs_mode & S_IFMT;
	tmode = tsp->zs_mode & S_IFMT;
	if (fmode == S_IFDIR || tmode == S_IFDIR || fsp->zs_links == 0 ||
	    tsp->zs_links == 0)
		change = 0;
	else
		change = tsp->zs_links - fsp->zs_links;
	if (fobjerr) {
		(*di->cb_func) (di->dsmnt, dobj, ZDIFF_ADDED,
		    NULL, tobjname, NULL, tsp, 0, di->cb_arg);
		return (0);
	} else if (tobjerr) {
		/* Account for removing this link itself */
		if (fmode == S_IFDIR)
			fsp->zs_links = 0;
		else
			fsp->zs_links--;
		if (!di->enumerate_only) {
			(*di->cb_func)(di->dsmnt, dobj, ZDIFF_REMOVED,
			    fobjname, NULL, fsp, NULL, 0, di->cb_arg);
		}
		return (0);
	}

	if (fmode != tmode && fsp->zs_gen == tsp->zs_gen)
		tsp->zs_gen++;	/* Force a generational difference */

	/* Simple modification or no change */
	if (fsp->zs_gen == tsp->zs_gen &&
	    fsp->zs_ctime[0] == tsp->zs_ctime[0] &&
	    fsp->zs_ctime[1] == tsp->zs_ctime[1]) {
		/*
		 * No apparent changes.  This occurs when this unchanged
		 * dnode is included in a dnode of dnodes that has
		 * other changes.
		 */
		return (0);
	}
	if (!di->enumerate_only && fsp->zs_gen == tsp->zs_gen) {
		(*di->cb_func)(di->dsmnt, dobj, ZDIFF_MODIFIED,
		    fobjname, tobjname, tsp, fsp, change, di->cb_arg);
		return (0);
	} else {
		/* file re-created or object re-used */
		if (!di->enumerate_only) {
			(*di->cb_func)(di->dsmnt, dobj, ZDIFF_REMOVED,
			    fobjname, NULL, fsp, NULL, 0, di->cb_arg);
		}
		(*di->cb_func)(di->dsmnt, dobj, ZDIFF_ADDED,
		    NULL, tobjname, NULL, tsp, 0, di->cb_arg);
		return (0);
	}
}



static int
write_inuse_diffs_one(differ_info_t *di, uint64_t dobj,
	zfs_stat_t *zs2_from, zfs_stat_t *zs2_frombase,
	zfs_stat_t *zs2_to, zfs_stat_t *zs2_tobase)
{
	zfs_stat_t fsb, tsb;
	char fobjname[MAXPATHLEN], tobjname[MAXPATHLEN];
	int fobjerr = 0, tobjerr = 0;
	char *tosnap;

	if (dobj == di->shares)
		return (0);

	tosnap = di->tosnap;

	if (di->frombase) {
		ASSERT(di->fromsnap == NULL);
		fobjerr = -1;
		fobjname[0] = '\0';
	} else {
		if (zs2_from) {
			if (di->needoldname) {
				fobjerr = get_path_for_obj(di, di->fromsnap,
				    dobj, fobjname, MAXPATHLEN,
				    zs2_from, zs2_frombase);
			}
			(void) memcpy(&fsb, zs2_from, sizeof (fsb));
		} else {
			bzero(&fsb, sizeof (fsb));
			fobjerr = -1;
		}
	}

	if (zs2_to) {
		if (di->needname) {
			tobjerr = get_path_for_obj(di, tosnap, dobj,
			    tobjname, MAXPATHLEN, zs2_to, zs2_tobase);
		}
		(void) memcpy(&tsb, zs2_to, sizeof (tsb));
	} else {
		bzero(&tsb, sizeof (tsb));
		tobjerr = -1;
	}

	return (write_inuse_diffs_zero(di, dobj, fobjerr, fobjname, &fsb,
	    tobjerr, tobjname, &tsb));
}

#define	NUM_RECORDS	16384

static int
setup_diffs(differ_info_t *di, zfs_cmd_t *fromzp, zfs_cmd_t *tozp)
{
	char *tosnap;
	void *buffer_tods, *buffer_fromds;

	bzero(fromzp, sizeof (*fromzp));
	bzero(tozp, sizeof (*tozp));

	tosnap = di->tosnap;

	buffer_tods = malloc(sizeof (zfs_stat_t) * NUM_RECORDS);
	if (buffer_tods == NULL)
		return (ENOMEM);

	(void) strlcpy(tozp->zc_name, tosnap, sizeof (tozp->zc_name));
	tozp->zc_stat_buf = (uintptr_t)buffer_tods;
	tozp->zc_stat_buflen = sizeof (zfs_stat_t) * NUM_RECORDS;
	tozp->zc_fromobj = 0;
	tozp->zc_needname = di->needname;

	if (! di->frombase) {
		buffer_fromds = malloc(sizeof (zfs_stat_t) * NUM_RECORDS);
		if (buffer_fromds == NULL)
			return (ENOMEM);

		(void) strlcpy(fromzp->zc_name, di->fromsnap,
		    sizeof (fromzp->zc_name));
		fromzp->zc_stat_buf = (uintptr_t)buffer_fromds;
		fromzp->zc_stat_buflen =
		    sizeof (zfs_stat_t) * NUM_RECORDS;
		fromzp->zc_fromobj = 0;
		fromzp->zc_needname = di->needoldname;
	}

	return (0);
}

/*ARGSUSED*/
static void
teardown_diffs(differ_info_t *di, zfs_cmd_t *fromzp, zfs_cmd_t *tozp)
{
	free((void *)(uintptr_t)fromzp->zc_stat_buf);
	free((void *)(uintptr_t)tozp->zc_stat_buf);
}

static int
write_inuse_diffs(differ_info_t *di, dmu_diff_record_t *dr, zfs_cmd_t *fromzp,
    zfs_cmd_t *tozp)
{
	zfs_stat_t *zs_from = NULL, *zs_to = NULL;
	void *buffer_tods, *buffer_fromds;
	uint64_t to_o, from_o;
	int err;

	if (dr->ddr_first > tozp->zc_fromobj)
		tozp->zc_fromobj = dr->ddr_first;
	tozp->zc_obj = dr->ddr_last;
	tozp->zc_cookie = 0;
	to_o = tozp->zc_fromobj;
	buffer_tods = (void *)(uintptr_t)tozp->zc_stat_buf;

	if (! di->frombase) {
		if (dr->ddr_first > fromzp->zc_fromobj)
			fromzp->zc_fromobj = dr->ddr_first;
		fromzp->zc_obj = dr->ddr_last;
		fromzp->zc_cookie = 0;
		from_o = fromzp->zc_fromobj;
		buffer_fromds =
		    (void *)(uintptr_t)fromzp->zc_stat_buf;
	} else {
		from_o = dr->ddr_last + 1;
		buffer_fromds = NULL;
	}

	/*
	 * from_o -- current object in "from" dataset.
	 * fromzp->zc_fromobj -- the first object in next "from" chunk.
	 * fromzp->zc_cookie -- number of records left in "from" chunk
	 * to_o -- current object in "to" dataset.
	 * tozp->zc_fromobj -- the first object in next "to" chunk.
	 * tozp->zc_cookie - number of records left in "to" chunk
	 */
	while (from_o <= dr->ddr_last || to_o <= dr->ddr_last) {
		if (! di->frombase) {
			if ((fromzp->zc_cookie == 0) &&
			    (from_o <= dr->ddr_last)) {
				ASSERT(from_o == fromzp->zc_fromobj);
				/* Get next stats chunk from "from" dataset */
				if (err = ioctl(di->zhp->zfs_hdl->libzfs_fd,
				    ZFS_IOC_BULK_OBJ_TO_STATS, fromzp)) {
					return (err);
				}
				zs_from =
				    (zfs_stat_t *)buffer_fromds;
				from_o = fromzp->zc_cookie == 0 ?
				    fromzp->zc_fromobj : zs_from->zs_obj;
			}
		}

		if (tozp->zc_cookie == 0 && (to_o <= dr->ddr_last)) {
			/* Get next stats chunk from the "to" dataset */
			ASSERT(to_o == tozp->zc_fromobj);
			if (err = ioctl(di->zhp->zfs_hdl->libzfs_fd,
			    ZFS_IOC_BULK_OBJ_TO_STATS, tozp)) {
				return (err);
			}
			zs_to = (zfs_stat_t *)buffer_tods;
			to_o = tozp->zc_cookie == 0 ?
			    tozp->zc_fromobj : zs_to->zs_obj;
		}

		/*
		 * At this point, from_o and to_o refer to the next object in
		 * their respective datasets.  If the next object in both is
		 * past the requested range, then this range is complete. Note
		 * that we record the next object number in the zfs_cmd_t
		 * structure. We can short-circuit unnecessary stat calls where
		 * we already have the answer.
		 */
		if (from_o > dr->ddr_last && to_o > dr->ddr_last)
			break;

		/*
		 * At least one dataset has an object in range.
		 * Go tell the user what kind of diff it is.
		 */
		if (from_o < to_o) {
			/*
			 * Object exists in fromds but not tods.
			 * This object has been DELETED.
			 */
			ASSERT(fromzp->zc_cookie > 0);
			err = write_inuse_diffs_one(di, zs_from->zs_obj,
			    zs_from, (zfs_stat_t *)buffer_fromds,
			    NULL, NULL);
			/* Consume one object from the "from" list */
			zs_from++;
			fromzp->zc_cookie--;
			from_o = fromzp->zc_cookie == 0 ?
			    fromzp->zc_fromobj : zs_from->zs_obj;
		} else if (from_o > to_o) {
			/*
			 * Object exists in tods but not fromds.
			 * This object has been ADDED.
			 */
			ASSERT(tozp->zc_cookie > 0);
			err = write_inuse_diffs_one(di, zs_to->zs_obj,
			    NULL, NULL,
			    zs_to, (zfs_stat_t *)buffer_tods);
			/* Consume one object from the "to" list */
			tozp->zc_cookie--;
			zs_to++;
			to_o = tozp->zc_cookie == 0 ?
			    tozp->zc_fromobj : zs_to->zs_obj;

		} else {
			/*
			 * Object exists in both tods and fromds.
			 * Either this object has been MODIFIED,
			 * or else the object has been DELETED and a new
			 * object has been ADDED with the same object number.
			 */
			ASSERT(fromzp->zc_cookie > 0 && tozp->zc_cookie > 0);
			err = write_inuse_diffs_one(di, zs_from->zs_obj,
			    zs_from, (zfs_stat_t *)buffer_fromds,
			    zs_to, (zfs_stat_t *)buffer_tods);
			/* Consume one object from each list */
			zs_from++;
			zs_to++;
			tozp->zc_cookie--;
			fromzp->zc_cookie--;
			from_o = fromzp->zc_cookie == 0 ?
			    fromzp->zc_fromobj : zs_from->zs_obj;
			to_o = tozp->zc_cookie == 0 ?
			    tozp->zc_fromobj : zs_to->zs_obj;
		}

		if (err) {
			return (err);
		}
	}

	return (0);
}

static int
describe_free(differ_info_t *di, uint64_t object,
	zfs_stat_t *zs2, zfs_stat_t *zs2_base,
	char *namebuf, int maxlen)
{
	if (di->needoldname &&
	    get_path_for_obj(di, di->fromsnap, object, namebuf,
	    maxlen, zs2, zs2_base) != 0) {
		return (-1);
	}

	(*di->cb_func)(di->dsmnt, object, ZDIFF_REMOVED,
	    namebuf, NULL, zs2, NULL, 0, di->cb_arg);
	return (0);
}

static int
write_free_diffs(differ_info_t *di, dmu_diff_record_t *dr, zfs_cmd_t *fromzp)
{
	zfs_stat_t *zs_from = NULL;
	void * buffer_fromds;
	uint64_t from_o;
	int err;
	char fobjname[MAXPATHLEN];

	if (di->enumerate_only) {
		/* -e enumerate mode does not display deletes */
		return (0);
	}
	if (dr->ddr_last < fromzp->zc_fromobj) {
		/* we already know this range is empty */
		return (0);
	}

	if (dr->ddr_first > fromzp->zc_fromobj) {
		fromzp->zc_fromobj = dr->ddr_first;
	}
	fromzp->zc_obj = dr->ddr_last;
	fromzp->zc_cookie = 0;
	buffer_fromds = (void *)(uintptr_t)fromzp->zc_stat_buf;
	from_o = fromzp->zc_fromobj;

	while (from_o <= dr->ddr_last) {
		if (fromzp->zc_cookie == 0) {
			/* Get next stats chunk from the "from" dataset */
			if (err = ioctl(di->zhp->zfs_hdl->libzfs_fd,
			    ZFS_IOC_BULK_OBJ_TO_STATS, fromzp)) {
				return (err);
			}
			zs_from = (zfs_stat_t *)(uintptr_t)buffer_fromds;
		}

		if (fromzp->zc_cookie == 0)
			break;
		from_o = zs_from->zs_obj;

		err = describe_free(di, zs_from->zs_obj,
		    zs_from, (zfs_stat_t *)(uintptr_t)buffer_fromds,
		    fobjname, MAXPATHLEN);
		zs_from++;
		fromzp->zc_cookie--;

		if (err) {
			return (err);
		}
	}

	return (0);
}

static void *
differ(void *arg)
{
	zfs_cmd_t tozc, fromzc;
	differ_info_t *di = arg;
	dmu_diff_record_t dr;
	int err;

	err = setup_diffs(di, &fromzc, &tozc);
	if (err != 0)
		return ((void *)-1);

	for (;;) {
		char *cp = (char *)&dr;
		int len = sizeof (dr);
		int rv;

		do {
			rv = read(di->datafd, cp, len);
			cp += rv;
			len -= rv;
		} while (len > 0 && rv > 0);

		if (rv < 0 || (rv == 0 && len != sizeof (dr))) {
			di->zerr = EPIPE;
			break;
		} else if (rv == 0) {
			/* end of file at a natural breaking point */
			break;
		}

		switch (dr.ddr_type) {
		case DDR_FREE:
			err = write_free_diffs(di, &dr, &fromzc);
			break;
		case DDR_INUSE:
			err = write_inuse_diffs(di, &dr, &fromzc, &tozc);
			break;
		default:
			di->zerr = EPIPE;
			break;
		}

		if (err || di->zerr)
			break;
	}

	teardown_diffs(di, &fromzc, &tozc);

	(void) close(di->datafd);
	if (err)
		return ((void *)-1);
	if (di->zerr) {
		ASSERT(di->zerr == EINVAL);
		(void) snprintf(di->errbuf, sizeof (di->errbuf),
		    dgettext(TEXT_DOMAIN,
		    "Internal error: bad data from diff IOCTL"));
		return ((void *)-1);
	}
	return ((void *)0);
}

static void
find_shares_object(differ_info_t *di)
{
	char fullpath[MAXPATHLEN];
	struct stat64 sb = { 0 };

	(void) strlcpy(fullpath, di->dsmnt, MAXPATHLEN);
	(void) strlcat(fullpath, ZDIFF_SHARESDIR, MAXPATHLEN);

	if (stat64(fullpath, &sb) == 0)
		di->shares = (uint64_t)sb.st_ino;
	else
		/* some very old pools won't have shares object */
		di->shares = (uint64_t)-1;
}

static int
make_temp_snapshot(differ_info_t *di)
{
	libzfs_handle_t *hdl = di->zhp->zfs_hdl;
	zfs_cmd_t zc = { 0 };

	(void) snprintf(zc.zc_value, sizeof (zc.zc_value),
	    ZDIFF_PREFIX, getpid());
	(void) strlcpy(zc.zc_name, di->ds, sizeof (zc.zc_name));
	zc.zc_cleanup_fd = di->cleanupfd;

	if (ioctl(hdl->libzfs_fd, ZFS_IOC_TMP_SNAPSHOT, &zc) != 0) {
		int err = errno;
		if (err == EPERM) {
			(void) snprintf(di->errbuf, sizeof (di->errbuf),
			    dgettext(TEXT_DOMAIN, "The diff delegated "
			    "permission is needed in order\nto create a "
			    "just-in-time snapshot for diffing\n"));
			return (zfs_error(hdl, EZFS_DIFF, di->errbuf));
		} else {
			(void) snprintf(di->errbuf, sizeof (di->errbuf),
			    dgettext(TEXT_DOMAIN, "Cannot create just-in-time "
			    "snapshot of '%s'"), zc.zc_name);
			return (zfs_standard_error(hdl, err, di->errbuf));
		}
	}

	di->tmpsnap = zfs_strdup(hdl, zc.zc_value);
	di->tosnap = zfs_asprintf(hdl, "%s@%s", di->ds, di->tmpsnap);
	return (0);
}

static void
teardown_differ_info(differ_info_t *di)
{
	free(di->ds);
	free(di->dsmnt);
	free(di->fromsnap);
	free(di->frommnt);
	free(di->tosnap);
	free(di->tmpsnap);
	free(di->tomnt);
	(void) close(di->cleanupfd);
}

static int
get_snapshot_names(differ_info_t *di, const char *fromsnap,
    const char *tosnap)
{
	libzfs_handle_t *hdl = di->zhp->zfs_hdl;
	char *atptrf = NULL;
	char *atptrt = NULL;
	int fdslen, fsnlen;
	int tdslen, tsnlen;

	/*
	 * Can accept in the from-epoch case
	 *    dataset
	 *    dataset@snap1
	 */
	if (fromsnap == NULL) {
		di->fromsnap = NULL;
		atptrt = strchr(tosnap, '@');
		tdslen = atptrt ? atptrt - tosnap : strlen(tosnap);
		di->ds = zfs_strdup(hdl, tosnap);
		di->ds[tdslen] = '\0';
		if (atptrt == NULL) {
			return (make_temp_snapshot(di));
		} else {
			di->tosnap = zfs_strdup(hdl, tosnap);
			return (0);
		}
	}

	/*
	 * Can accept
	 *    dataset@snap1
	 *    dataset@snap1 dataset@snap2
	 *    dataset@snap1 @snap2
	 *    dataset@snap1 dataset
	 *    @snap1 dataset@snap2
	 */
	if (tosnap == NULL) {
		/* only a from snapshot given, must be valid */
		(void) snprintf(di->errbuf, sizeof (di->errbuf),
		    dgettext(TEXT_DOMAIN,
		    "Badly formed snapshot name %s"), fromsnap);

		if (!zfs_validate_name(hdl, fromsnap, ZFS_TYPE_SNAPSHOT,
		    B_FALSE)) {
			return (zfs_error(hdl, EZFS_INVALIDNAME,
			    di->errbuf));
		}

		atptrf = strchr(fromsnap, '@');
		ASSERT(atptrf != NULL);
		fdslen = atptrf - fromsnap;

		di->fromsnap = zfs_strdup(hdl, fromsnap);
		di->ds = zfs_strdup(hdl, fromsnap);
		di->ds[fdslen] = '\0';

		/* the to snap will be a just-in-time snap of the head */
		return (make_temp_snapshot(di));
	}

	(void) snprintf(di->errbuf, sizeof (di->errbuf),
	    dgettext(TEXT_DOMAIN, "diff error"));

	atptrf = strchr(fromsnap, '@');
	atptrt = strchr(tosnap, '@');
	fdslen = atptrf ? atptrf - fromsnap : strlen(fromsnap);
	tdslen = atptrt ? atptrt - tosnap : strlen(tosnap);
	fsnlen = strlen(fromsnap) - fdslen;	/* includes @ sign */
	tsnlen = strlen(tosnap) - tdslen;	/* includes @ sign */

	if (fsnlen <= 1 || tsnlen == 1 || (fdslen == 0 && tdslen == 0) ||
	    (fsnlen == 0 && tsnlen == 0)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "Acceptable requests are"
		    "\n\tdataset@snap1\n\tdataset@snap1 dataset@snap2"
		    "\n\tdataset@snap1 @snap2\n\tdataset@snap1 dataset"
		    "\n\t@snap1 dataset@snap2"));
		return (zfs_error(hdl, EZFS_DIFF, di->errbuf));
	} else if ((fdslen > 0 && tdslen > 0) &&
	    ((tdslen != fdslen || strncmp(fromsnap, tosnap, fdslen) != 0))) {
		/*
		 * not the same dataset name, might be okay if
		 * tosnap is a clone of a fromsnap descendant.
		 */
		char origin[ZFS_MAXNAMELEN];
		zprop_source_t src;
		zfs_handle_t *zhp;

		libzfs_print_on_error(hdl, B_FALSE);

		di->ds = zfs_alloc(di->zhp->zfs_hdl, tdslen + 1);
		(void) strncpy(di->ds, tosnap, tdslen);
		di->ds[tdslen] = '\0';

		zhp = zfs_open(hdl, di->ds, ZFS_TYPE_FILESYSTEM);
		while (zhp != NULL) {
			int err;
			err = zfs_prop_get(zhp, ZFS_PROP_ORIGIN,
			    origin, sizeof (origin), &src, NULL, 0, B_FALSE);

			if (err != 0) {
				zfs_close(zhp);
				zhp = NULL;
				break;
			} else if (strncmp(origin, fromsnap, fsnlen) == 0) {
				break;
			}
			(void) zfs_close(zhp);
			zhp = zfs_open(hdl, origin, ZFS_TYPE_FILESYSTEM);
		}

		libzfs_print_on_error(hdl, B_TRUE);

		if (zhp == NULL) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "%s is not a descendant dataset of %.*s\n"),
			    di->ds, fdslen, fromsnap);
			return (zfs_error(hdl, EZFS_DIFF, di->errbuf));
		} else {
			(void) zfs_close(zhp);
		}

		di->isclone = B_TRUE;
		di->fromsnap = zfs_strdup(hdl, fromsnap);
		if (tsnlen) {
			di->tosnap = zfs_strdup(hdl, tosnap);
		} else {
			return (make_temp_snapshot(di));
		}
	} else {
		int dslen = fdslen ? fdslen : tdslen;

		di->ds = zfs_alloc(hdl, dslen + 1);
		(void) strncpy(di->ds, fdslen ? fromsnap : tosnap, dslen);
		di->ds[dslen] = '\0';

		di->fromsnap = zfs_asprintf(hdl, "%s%s", di->ds, atptrf);
		if (tsnlen) {
			di->tosnap = zfs_asprintf(hdl, "%s%s", di->ds, atptrt);
		} else {
			return (make_temp_snapshot(di));
		}
	}
	return (0);
}

static int
get_mountpoint(differ_info_t *di, char *dsnm, char **mntpt)
{
	boolean_t mounted;

	mounted = is_mounted(di->zhp->zfs_hdl, dsnm, mntpt);
	if (mounted == B_FALSE) {
		(void) snprintf(di->errbuf, sizeof (di->errbuf),
		    dgettext(TEXT_DOMAIN,
		    "Cannot diff an unmounted snapshot"));
		return (zfs_error(di->zhp->zfs_hdl, EZFS_BADTYPE, di->errbuf));
	}

	/* Avoid a double slash at the beginning of root-mounted datasets */
	if (**mntpt == '/' && *(*mntpt + 1) == '\0')
		**mntpt = '\0';
	return (0);
}

static int
get_mountpoints(differ_info_t *di)
{
	char *strptr;
	char *frommntpt;

	/*
	 * first get the mountpoint for the parent dataset
	 */
	if (get_mountpoint(di, di->ds, &di->dsmnt) != 0)
		return (-1);

	strptr = strchr(di->tosnap, '@');
	ASSERT3P(strptr, !=, NULL);
	di->tomnt = zfs_asprintf(di->zhp->zfs_hdl, "%s%s%s", di->dsmnt,
	    ZDIFF_SNAPDIR, ++strptr);

	if (di->frombase)
		return (0);

	strptr = strchr(di->fromsnap, '@');
	ASSERT3P(strptr, !=, NULL);

	frommntpt = di->dsmnt;
	if (di->isclone) {
		char *mntpt;
		int err;

		*strptr = '\0';
		err = get_mountpoint(di, di->fromsnap, &mntpt);
		*strptr = '@';
		if (err != 0)
			return (-1);
		frommntpt = mntpt;
	}

	di->frommnt = zfs_asprintf(di->zhp->zfs_hdl, "%s%s%s", frommntpt,
	    ZDIFF_SNAPDIR, ++strptr);

	if (di->isclone)
		free(frommntpt);

	return (0);
}

static int
setup_differ_info(zfs_handle_t *zhp, const char *fromsnap,
    const char *tosnap, differ_info_t *di)
{
	di->zhp = zhp;

	di->cleanupfd = open(ZFS_DEV, O_RDWR|O_EXCL);
	VERIFY(di->cleanupfd >= 0);

	if (get_snapshot_names(di, fromsnap, tosnap) != 0)
		return (-1);

	if (get_mountpoints(di) != 0)
		return (-1);

	find_shares_object(di);

	return (0);
}

int
zfs_scan_diffs(zfs_handle_t *zhp, const char *fromsnap,
    const char *tosnap, int flags, diff_scan_cb_t *cb_func, void *cb_arg)
{
	zfs_cmd_t zc = { 0 };
	char errbuf[1024];
	differ_info_t di = { 0 };
	pthread_t tid;
	int pipefd[2];
	int iocerr;

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "zfs diff failed"));

	di.frombase = (flags & ZFS_DIFF_BASE) != 0;
	di.enumerate_only = ((flags & ZFS_DIFF_ENUMERATE) != 0) ||
	    di.frombase;

	di.needname = ! (flags & ZFS_DIFF_NONAME);
	di.needoldname = ! (flags & ZFS_DIFF_NOOLDNAME);

	di.cb_func = cb_func;
	di.cb_arg = cb_arg;

	if (setup_differ_info(zhp, fromsnap, tosnap, &di)) {
		teardown_differ_info(&di);
		return (-1);
	}

	if (pipe(pipefd)) {
		zfs_error_aux(zhp->zfs_hdl, strerror(errno));
		teardown_differ_info(&di);
		return (zfs_error(zhp->zfs_hdl, EZFS_PIPEFAILED, errbuf));
	}

	di.datafd = pipefd[0];

	if (pthread_create(&tid, NULL, differ, &di)) {
		zfs_error_aux(zhp->zfs_hdl, strerror(errno));
		(void) close(pipefd[0]);
		(void) close(pipefd[1]);
		teardown_differ_info(&di);
		return (zfs_error(zhp->zfs_hdl,
		    EZFS_THREADCREATEFAILED, errbuf));
	}

	/* do the ioctl() */
	if (di.fromsnap)
		(void) strlcpy(zc.zc_value, di.fromsnap,
		    strlen(di.fromsnap) + 1);
	(void) strlcpy(zc.zc_name, di.tosnap, strlen(di.tosnap) + 1);
	zc.zc_cookie = pipefd[1];

	iocerr = ioctl(zhp->zfs_hdl->libzfs_fd, ZFS_IOC_DIFF, &zc);
	if (iocerr != 0) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "Unable to obtain diffs"));
		if (errno == EPERM) {
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "\n   The sys_mount privilege or diff delegated "
			    "permission is needed\n   to execute the "
			    "diff ioctl"));
		} else if (errno == EXDEV) {
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "%s is not a descendant dataset of %s\n"),
			    di.tosnap, di.fromsnap);
		} else if (errno != EPIPE || di.zerr == 0) {
			zfs_error_aux(zhp->zfs_hdl, strerror(errno));
		}
		(void) close(pipefd[1]);
		(void) pthread_cancel(tid);
		(void) pthread_join(tid, NULL);
		teardown_differ_info(&di);
		if (di.zerr != 0 && di.zerr != EPIPE) {
			zfs_error_aux(zhp->zfs_hdl, strerror(di.zerr));
			return (zfs_error(zhp->zfs_hdl, EZFS_DIFF, errbuf));
		} else {
			return (zfs_error(zhp->zfs_hdl, EZFS_DIFFDATA, errbuf));
		}
	}

	(void) close(pipefd[1]);
	(void) pthread_join(tid, NULL);

	if (di.zerr != 0) {
		zfs_error_aux(zhp->zfs_hdl, strerror(di.zerr));
		return (zfs_error(zhp->zfs_hdl, EZFS_DIFF, errbuf));
	}
	teardown_differ_info(&di);
	return (0);
}

/* The rest of this file is support for zfs_show_diffs */

static char		*getname(uid_t);
static char		*getgroup(gid_t);

typedef enum {
	ZFIELD_NAME = 0,
	ZFIELD_SIZE,
	ZFIELD_CTIME,
	ZFIELD_ATIME,
	ZFIELD_MTIME,
	ZFIELD_CRTIME,
	ZFIELD_DELTA,
	ZFIELD_UID,
	ZFIELD_GID,
	ZFIELD_LINKS,
	ZFIELD_INODE,
	ZFIELD_PARENT_INODE,
	ZFIELD_OLD_NAME,
	/* last item */
	ZFIELD_MAX_FIELD
} zdiff_field_t;

char *zdiff_field_names[] = {
	"name",
	"size",
	"ctime",
	"atime",
	"mtime",
	"crtime",
	"linkschange",
	"user",
	"group",
	"links",
	"object",
	"parent",
	"oldname"
};

typedef struct zdiff_info {
	int		zerr;
	FILE		*pi_fp;
	diff_flags_t	zi_flags;
	int		nfields;
	zdiff_field_t	fields[ZFIELD_MAX_FIELD];
	char		errbuf[1024];
} zdiff_info_t;

/*
 * stream_bytes
 *
 * Prints a file name out a character at a time.  If the character is
 * not in the range of what we consider "printable" ASCII, display it
 * as an escaped 3-digit octal value.  ASCII values less than a space
 * are all control characters and we declare the upper end as the
 * DELete character.  This also is the last 7-bit ASCII character.
 * We choose to treat all 8-bit ASCII as not printable for this
 * application.
 */
static void
stream_bytes(FILE *fp, const char *string)
{


	while (*string) {
		if (*string > ' ' && *string != '\\' && *string < '\177')
			(void) fprintf(fp, "%c", *string++);
		else
			(void) fprintf(fp, "\\%03o", (unsigned char)*string++);
	}
}

int
get_what(zfs_stat_t *isb)
{
	mode_t	what = isb->zs_mode;
	int	symbol;

	switch (what & S_IFMT) {
	case S_IFBLK:
		symbol = 'B';
		break;
	case S_IFCHR:
		symbol = 'C';
		break;
	case S_IFDIR:
		symbol = '/';
		break;
	case S_IFDOOR:
		symbol = '>';
		break;
	case S_IFIFO:
		symbol = '|';
		break;
	case S_IFLNK:
		symbol = '@';
		break;
	case S_IFPORT:
		symbol = 'P';
		break;
	case S_IFSOCK:
		symbol = '=';
		break;
	case S_IFREG:
		symbol = 'F';
		break;
	default:
		symbol = '?';
		break;
	}
	return (symbol);
}

static void
print_what(FILE *fp, zfs_stat_t *isb)
{
	int	symbol = get_what(isb);

	(void) fprintf(fp, "%c", (char)symbol);
}

static void
print_cmn(FILE *fp, char *dsmnt, const char *file)
{
	stream_bytes(fp, dsmnt);
	stream_bytes(fp, file);
}

static void
print_fields(FILE *fp, zdiff_info_t *di, char *dsmnt, zfs_stat_t *isb,
    const char *oldfile, const char *file, uint64_t obj, int delta)

{
	int i;
	zdiff_field_t field;

	for (i = 0; i < di->nfields; i++) {
		if (i > 0) {
			(void) fprintf(fp, "\t");
		}
		field = di->fields[i];
		switch (field) {
		case ZFIELD_NAME:
			print_cmn(fp, dsmnt, file);
			break;
		case ZFIELD_OLD_NAME:
			if (oldfile)
				print_cmn(fp, dsmnt, oldfile);
			else
				(void) fprintf(fp, "-");
			break;
		case ZFIELD_DELTA:
			(void) fprintf(fp, "%+d", delta);
			break;
		case ZFIELD_INODE:
			(void) fprintf(fp, "%llu", (longlong_t)obj);
			break;
		case ZFIELD_PARENT_INODE:
			(void) fprintf(fp, "%llu", (longlong_t)isb->zs_parent);
			break;
		case ZFIELD_LINKS:
			(void) fprintf(fp, "%llu", (longlong_t)isb->zs_links);
			break;
		case ZFIELD_SIZE:
			(void) fprintf(fp, "%llu", (longlong_t)isb->zs_size);
			break;
		case ZFIELD_UID:
			(void) fprintf(fp, "%s", getname(isb->zs_uid));
			break;
		case ZFIELD_GID:
			(void) fprintf(fp, "%s", getgroup(isb->zs_gid));
			break;
		case ZFIELD_ATIME:
			(void) fprintf(fp, "%10lld.%09lld",
			    (longlong_t)isb->zs_atime[0],
			    (longlong_t)isb->zs_atime[1]);
			break;
		case ZFIELD_CTIME:
			(void) fprintf(fp, "%10lld.%09lld",
			    (longlong_t)isb->zs_ctime[0],
			    (longlong_t)isb->zs_ctime[1]);
			break;
		case ZFIELD_MTIME:
			(void) fprintf(fp, "%10lld.%09lld",
			    (longlong_t)isb->zs_mtime[0],
			    (longlong_t)isb->zs_mtime[1]);
			break;
		case ZFIELD_CRTIME:
			(void) fprintf(fp, "%10lld.%09lld",
			    (longlong_t)isb->zs_crtime[0],
			    (longlong_t)isb->zs_crtime[1]);
			break;
		}
	}
}

static int
setup_fields(zdiff_info_t *di, nvlist_t *fields)
{
	nvpair_t *elem;
	char *propstr;
	int	count, j;

	/* Process each field */
	elem = NULL;
	count = 0;
	while ((elem = nvlist_next_nvpair(fields, elem)) != NULL) {
		propstr = nvpair_name(elem);
		for (j = 0; j < ZFIELD_MAX_FIELD; j++) {
			if (0 == strcmp(propstr, zdiff_field_names[j])) {
				/* same name */
				break;
			}
		}
		if (j == ZFIELD_MAX_FIELD || count >= ZFIELD_MAX_FIELD)
			goto failfield;
		di->fields[count] = j;
		count++;
	}
	di->nfields = count;
	return (0);

failfield:
	di->zerr = EZFS_BADPROP;
	(void) snprintf(di->errbuf, sizeof (di->errbuf),
	    dgettext(TEXT_DOMAIN, "illegal field %s"), propstr);
	return (-1);
}

static void
print_rename(FILE *fp, zdiff_info_t *di, char *dsmnt, const char *old,
    const char *new, zfs_stat_t *isb, uint64_t obj, int delta)
{
	if (di->zi_flags & ZFS_DIFF_TIMESTAMP)
		(void) fprintf(fp, "%10lld.%09lld\t",
		    (longlong_t)isb->zs_ctime[0],
		    (longlong_t)isb->zs_ctime[1]);
	(void) fprintf(fp, "%c\t", ZDIFF_RENAMED);
	if (di->zi_flags & ZFS_DIFF_CLASSIFY) {
		print_what(fp, isb);
		(void) fprintf(fp, "\t");
	}
	if (di->nfields) {
		print_fields(fp, di, dsmnt, isb, old, new, obj, delta);
	} else {
		print_cmn(fp, dsmnt, old);
		if (di->zi_flags & ZFS_DIFF_PARSEABLE)
			(void) fprintf(fp, "\t");
		else
			(void) fprintf(fp, " -> ");
		print_cmn(fp, dsmnt, new);
		if (delta != 0)
			(void) fprintf(fp, "\t(%+d)", delta);
	}
	(void) fprintf(fp, "\n");
}

static void
print_file(FILE *fp, zdiff_info_t *di, char *dsmnt, char type,
    const char *file, zfs_stat_t *isb, uint64_t obj, int delta)
{
	if (di->zi_flags & ZFS_DIFF_TIMESTAMP)
		(void) fprintf(fp, "%10lld.%09lld\t",
		    (longlong_t)isb->zs_ctime[0],
		    (longlong_t)isb->zs_ctime[1]);
	(void) fprintf(fp, "%c\t", type);
	if (di->zi_flags & ZFS_DIFF_CLASSIFY) {
		print_what(fp, isb);
		(void) fprintf(fp, "\t");
	}
	if (di->nfields) {
		print_fields(fp, di, dsmnt, isb, NULL, file, obj, delta);
	} else {
		print_cmn(fp, dsmnt, file);
		if (delta != 0)
			(void) fprintf(fp, "\t(%+d)", delta);
	}
	(void) fprintf(fp, "\n");
}

/*ARGSUSED*/
void
zdiff_print_cb(char *dsmnt, uint64_t obj,
    char type, char *fobjname, char *tobjname, zfs_stat_t *fsp, zfs_stat_t *tsp,
    int delta, void *arg)
{

	zdiff_info_t *di = arg;
	FILE *fp = di->pi_fp;

	switch (type) {
	case ZDIFF_MODIFIED:
		if (0 == strcmp(fobjname, tobjname)) {
			/* same name */
			print_file(fp, di, dsmnt, ZDIFF_MODIFIED, tobjname,
			    tsp, obj, delta);
		} else {
			print_rename(fp, di, dsmnt, fobjname, tobjname,
			    tsp, obj, delta);
		}
		break;
	case ZDIFF_REMOVED:
		print_file(fp, di, dsmnt, type, fobjname, fsp, obj, delta);
		break;
	case ZDIFF_ADDED:
		print_file(fp, di, dsmnt, type, tobjname, tsp, obj, delta);
	}
}

int
zfs_show_diffs(zfs_handle_t *zhp, int outfd, const char *fromsnap,
    const char *tosnap, nvlist_t *fields, int flags)
{
	int	rc;
	zdiff_info_t di = {0};
	libzfs_handle_t *hdl = zhp->zfs_hdl;

	if ((di.pi_fp = fdopen(outfd, "w")) == NULL) {
		di.zerr = errno;
		(void) snprintf(di.errbuf, sizeof (di.errbuf),
		    dgettext(TEXT_DOMAIN, "zfs diff failed"));
		return (zfs_standard_error(hdl, di.zerr, di.errbuf));
	}

	if (fields != NULL && setup_fields(&di, fields) != 0) {
		/* di.errbuf is already set up */
		return (zfs_error(hdl, di.zerr, di.errbuf));
	}

	if (flags & (ZFS_DIFF_ENUMERATE | ZFS_DIFF_BASE)) {
		/*
		 * In normal (non-enumerate) mode, we always need both names
		 * (to distinguish rename from modify).  In enumerate mode,
		 * we need just the new name.  In enumerate mode with specific
		 * fields, we only need the names if they ask for it.
		 */
		if (di.nfields == 0) {
			flags |= ZFS_DIFF_NOOLDNAME;
		} else {
			int i;
			boolean_t needname = B_FALSE, needoldname = B_FALSE;
			for (i = 0; i < di.nfields; i++) {
				zdiff_field_t field;
				field = di.fields[i];
				if (field == ZFIELD_NAME)
					needname = B_TRUE;
				if (field == ZFIELD_OLD_NAME)
					needoldname = B_TRUE;
			}
			if (!needname)
				flags |= ZFS_DIFF_NONAME;
			if (!needoldname)
				flags |= ZFS_DIFF_NOOLDNAME;
		}
	}

	di.zi_flags = flags;
	rc = zfs_scan_diffs(zhp, fromsnap, tosnap, flags, zdiff_print_cb, &di);

	(void) fclose(di.pi_fp);
	return (rc);
}

#include <pwd.h>
#include <grp.h>
#include <utmpx.h>

struct	utmpx utmp;

static uid_t		lastuid	= (uid_t)-1;
static gid_t		lastgid = (gid_t)-1;
static char		*lastuname = NULL;
static char		*lastgname = NULL;

#define	NMAX	(sizeof (utmp.ut_name))
#define	SCPYN(a, b)	(void) strncpy(a, b, NMAX)


struct cachenode {		/* this struct must be zeroed before using */
	struct cachenode *lesschild;	/* subtree whose entries < val */
	struct cachenode *grtrchild;	/* subtree whose entries > val */
	long val;			/* the uid or gid of this entry */
	int initted;			/* name has been filled in */
	char name[NMAX+1];		/* the string that val maps to */
};
static struct cachenode *names, *groups;

static struct cachenode *
findincache(struct cachenode **head, long val)
{
	struct cachenode **parent = head;
	struct cachenode *c = *parent;

	while (c != NULL) {
		if (val == c->val) {
			/* found it */
			return (c);
		} else if (val < c->val) {
			parent = &c->lesschild;
			c = c->lesschild;
		} else {
			parent = &c->grtrchild;
			c = c->grtrchild;
		}
	}

	/* not in the cache, make a new entry for it */
	c = calloc(1, sizeof (struct cachenode));
	if (c == NULL) {
		perror("ls");
		exit(2);
	}
	*parent = c;
	c->val = val;
	return (c);
}

/*
 * get name from cache, or passwd file for a given uid;
 * lastuid is set to uid.
 */
static char *
getname(uid_t uid)
{
	struct passwd *pwent;
	struct cachenode *c;

	if ((uid == lastuid) && lastuname)
		return (lastuname);

	c = findincache(&names, uid);
	if (c->initted == 0) {
		if ((pwent = getpwuid(uid)) != NULL) {
			SCPYN(&c->name[0], pwent->pw_name);
		} else {
			(void) sprintf(&c->name[0], "%-8u", (int)uid);
		}
		c->initted = 1;
	}
	lastuid = uid;
	lastuname = &c->name[0];
	return (lastuname);
}

/*
 * get name from cache, or group file for a given gid;
 * lastgid is set to gid.
 */
static char *
getgroup(gid_t gid)
{
	struct group *grent;
	struct cachenode *c;

	if ((gid == lastgid) && lastgname)
		return (lastgname);

	c = findincache(&groups, gid);
	if (c->initted == 0) {
		if ((grent = getgrgid(gid)) != NULL) {
			SCPYN(&c->name[0], grent->gr_name);
		} else {
			(void) sprintf(&c->name[0], "%-8u", (int)gid);
		}
		c->initted = 1;
	}
	lastgid = gid;
	lastgname = &c->name[0];
	return (lastgname);
}
