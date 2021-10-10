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

#include <strings.h>
#include <libintl.h>
#include <libnvpair.h>
#include <sys/stat.h>
#include <errno.h>
#include <synch.h>
#include <note.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/mnttab.h>

#include <libshare.h>
#include <libshare_impl.h>
#include <sharefs/share.h>
#include <sharefs/sharetab.h>

static int sa_legacy_init(void);
static void sa_legacy_fini(void);
static void sa_legacy_cleanup_smf(void);
static int sa_legacy_share_write(nvlist_t *);
static int sa_legacy_share_read(const char *, const char *, nvlist_t **);
static int sa_legacy_share_read_init(sa_read_hdl_t *);
static int sa_legacy_share_read_next(sa_read_hdl_t *, nvlist_t **);
static int sa_legacy_share_read_fini(sa_read_hdl_t *);
static int sa_legacy_share_remove(const char *, const char *);
static int sa_legacy_share_get_acl(const char *, const char *, acl_t **);
static int sa_legacy_share_set_acl(const char *, const char *, acl_t *);
static int sa_legacy_get_mntpnt_for_path(const char *, char *, size_t,
    char *, size_t, char *, size_t);
static void sa_legacy_mnttab_cache(boolean_t);
static int sa_legacy_sharing_enabled(const char *, sa_proto_t *);
static int sa_legacy_sharing_get_prop(const char *, sa_proto_t, char **);
static int sa_legacy_sharing_set_prop(const char *, sa_proto_t, char *);
static int sa_legacy_is_legacy(const char *, boolean_t *);
static int sa_legacy_is_zoned(const char *, boolean_t *);
static int sa_legacy_check_mounts(char *, char *, size_t, char *, size_t);
static void sa_legacy_cleanup_mounts();

sa_fs_ops_t sa_plugin_ops = {
	.saf_hdr = {
		.pi_ptype = SA_PLUGIN_FS,
		.pi_type = SA_FS_LEGACY,
		.pi_name = "legacy",
		.pi_version = SA_LIBSHARE_VERSION,
		.pi_flags = 0,
		.pi_init = sa_legacy_init,
		.pi_fini = sa_legacy_fini
	},
	.saf_share_write = sa_legacy_share_write,
	.saf_share_read = sa_legacy_share_read,
	.saf_share_read_init = sa_legacy_share_read_init,
	.saf_share_read_next = sa_legacy_share_read_next,
	.saf_share_read_fini = sa_legacy_share_read_fini,
	.saf_share_remove = sa_legacy_share_remove,
	.saf_share_get_acl = sa_legacy_share_get_acl,
	.saf_share_set_acl = sa_legacy_share_set_acl,
	.saf_get_mntpnt_for_path = sa_legacy_get_mntpnt_for_path,
	.saf_mnttab_cache = sa_legacy_mnttab_cache,
	.saf_sharing_enabled = sa_legacy_sharing_enabled,
	.saf_sharing_get_prop = sa_legacy_sharing_get_prop,
	.saf_sharing_set_prop = sa_legacy_sharing_set_prop,
	.saf_is_legacy = sa_legacy_is_legacy,
	.saf_is_zoned = sa_legacy_is_zoned
};

static scf_handle_t *g_smfhandlep = NULL;
static scf_instance_t *g_smfp = NULL;
static scf_scope_t *g_scopep = NULL;
static scf_service_t *g_servicep = NULL;
static mutex_t g_legacy_lock;
static int initialized = 0;

#define	LOCK_LIBLEGACY_HDL()	(void) mutex_lock(&g_legacy_lock)
#define	UNLOCK_LIBLEGACY_HDL()	(void) mutex_unlock(&g_legacy_lock)

static struct mounts {
	struct mounts *next;
	char *mntpnt;
	char *mntopts;
} *legacy_mounts = NULL;
static mutex_t legacy_mounts_lock;
#define	LOCK_MOUNTS()		(void) mutex_lock(&legacy_mounts_lock)
#define	UNLOCK_MOUNTS()		(void) mutex_unlock(&legacy_mounts_lock)

static int
sa_legacy_init(void)
{
	LOCK_LIBLEGACY_HDL();
	if (initialized) {
		UNLOCK_LIBLEGACY_HDL();
		return (SA_OK);
	}

	initialized = 1;
	if (g_smfhandlep != NULL)
		goto done;

	g_smfhandlep = scf_handle_create(SCF_VERSION);
	if (g_smfhandlep == NULL)
		goto err;

	if (scf_handle_bind(g_smfhandlep) != 0)
		goto err;

	g_smfp = scf_instance_create(g_smfhandlep);
	if (g_smfp == NULL)
		goto err;

	g_scopep = scf_scope_create(g_smfhandlep);
	if (g_scopep == NULL)
		goto err;

	if (scf_handle_get_scope(g_smfhandlep, SCF_SCOPE_LOCAL, g_scopep) != 0)
		goto err;

	g_servicep = scf_service_create(g_smfhandlep);
	if (g_servicep == NULL)
		goto err;

	if (scf_scope_get_service(g_scopep, "network/shares", g_servicep) != 0)
		goto err;

	if (scf_service_get_instance(g_servicep, "default", g_smfp) != 0)
		goto err;

	UNLOCK_LIBLEGACY_HDL();
	return (SA_OK);
err:
	sa_legacy_cleanup_smf();
done:
	UNLOCK_LIBLEGACY_HDL();
	return (SA_SYSTEM_ERR);
}

static void
sa_legacy_fini(void)
{
	LOCK_LIBLEGACY_HDL();
	if (!initialized) {
		UNLOCK_LIBLEGACY_HDL();
		return;
	}

	initialized = 0;
	sa_legacy_cleanup_smf();
	LOCK_MOUNTS();
	sa_legacy_cleanup_mounts();
	UNLOCK_MOUNTS();

	UNLOCK_LIBLEGACY_HDL();
}

/*
 * routine to free the smf resources
 * MUST be called with g_legacy_lock held
 */
static void
sa_legacy_cleanup_smf(void)
{
	if (g_scopep != NULL) {
		scf_scope_destroy(g_scopep);
		g_scopep = NULL;
	}

	if (g_smfhandlep != NULL) {
		scf_handle_destroy(g_smfhandlep);
		g_smfhandlep = NULL;
	}

	if (g_smfp != NULL) {
		scf_instance_destroy(g_smfp);
		g_smfp = NULL;
	}

	if (g_servicep != NULL) {
		scf_service_destroy(g_servicep);
		g_servicep = NULL;
	}
}

/*
 * sa_legacy_fix_share_name
 *
 * some valid SMB share names contain characters illegal in SMF. This
 * function maps to a valid character set. Since it is only used when
 * referencing SMF, it only needs a one way mapping.
 */
static char *
sa_legacy_fix_share_name(const char *sharename)
{
	static char hex[] = "0123456789abcdef";
	static char invalid[] = " .&,#$^(){}~_";
	char buf[MAXNAMELEN];
	char *newshare = buf;
	char *limit = buf + MAXNAMELEN - 1;

	newshare += snprintf(buf, sizeof (buf), "S-");
	while (*sharename != '\0' && newshare < limit) {
		if (strchr(invalid, *sharename) != NULL) {
			*newshare++ = '_';
			*newshare++ = hex[(*sharename & 0xF0) >> 4];
			*newshare++ = hex[*sharename & 0xF];
		} else {
			*newshare++ = *sharename;
		}
		sharename++;
	}

	*newshare = '\0';
	return (strdup(buf));
}

/*
 * legacy_common_transaction
 *
 * implements both store and remove of SMF property. Must be called
 * with locks held.
 */

static int
legacy_common_transaction(const char *shname,
    char *buff, size_t bufflen, boolean_t dowrite)
{
	scf_transaction_t *trans = NULL;
	scf_propertygroup_t *pg = NULL;
	scf_value_t *value = NULL;
	scf_transaction_entry_t *entry = NULL;
	int ret = SA_OK;
	char *sharename;

	sharename = sa_legacy_fix_share_name(shname);
	if (sharename == NULL) {
		ret = SA_NO_MEMORY;
		goto err;
	}

	trans = scf_transaction_create(g_smfhandlep);
	if (trans == NULL) {
		ret = SA_SYSTEM_ERR;
		goto err;
	}

	pg = scf_pg_create(g_smfhandlep);
	if (pg == NULL) {
		ret = SA_SYSTEM_ERR;
		goto err;
	}

	if (scf_instance_get_pg(g_smfp, LEGACY_PG, pg) != 0) {
		if (scf_instance_add_pg(g_smfp, LEGACY_PG,
		    SCF_GROUP_FRAMEWORK, 0, pg) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_PERMISSION_DENIED:
				ret = SA_NO_PERMISSION;
				break;
			default:
				ret = SA_SYSTEM_ERR;
				break;
			}
			goto err;
		}
	}

	if (scf_transaction_start(trans, pg) != 0) {
		ret = SA_SYSTEM_ERR;
		goto err;
	}

	value = scf_value_create(g_smfhandlep);
	entry = scf_entry_create(g_smfhandlep);
	if (value == NULL || entry == NULL) {
		ret = SA_SYSTEM_ERR;
		goto err;
	}

	if (dowrite) {
		if (scf_transaction_property_change(trans, entry,
		    sharename, SCF_TYPE_OPAQUE) == 0 ||
		    scf_transaction_property_new(trans, entry,
		    sharename, SCF_TYPE_OPAQUE) == 0) {
			if (scf_value_set_opaque(value, buff, bufflen) == 0) {
				if (scf_entry_add_value(entry, value) != 0) {
					ret = SA_SYSTEM_ERR;
					scf_value_destroy(value);
				}
				/* The value is in the transaction */
				value = NULL;
			} else {
				/* Value couldn't be constructed */
				ret = SA_SYSTEM_ERR;
			}
			/* The entry is in the transaction or NULL */
			entry = NULL;
		} else {
			ret = SA_SYSTEM_ERR;
		}
	} else {
		if (scf_transaction_property_delete(trans, entry,
		    sharename) != 0)
			ret = SA_SYSTEM_ERR;
		else
			entry = NULL;
	}

	if (ret == SA_SYSTEM_ERR)
		goto err;

	if (scf_transaction_commit(trans) < 0)
		ret = SA_SYSTEM_ERR;

	scf_transaction_destroy_children(trans);
	scf_transaction_destroy(trans);

err:
	if (ret == SA_SYSTEM_ERR &&
	    scf_error() == SCF_ERROR_PERMISSION_DENIED)
		ret = SA_NO_PERMISSION;

	/*
	 * Cleanup if there were any errors that didn't leave these
	 * values where they would be cleaned up later.
	 */
	if (value != NULL)
		scf_value_destroy(value);
	if (entry != NULL)
		scf_entry_destroy(entry);
	if (pg != NULL)
		scf_pg_destroy(pg);

	switch (ret) {
	case SA_SYSTEM_ERR:
		salog_error(ret, "legacy_share_rsrc_write: SMF error: %s",
		    scf_strerror(scf_error()));
		break;
	default:
		if (ret != SA_OK)
			salog_error(ret, "legacy_share_rsrc_write");
		break;
	}

	free(sharename);

	return (ret);
}

static int
legacy_share_rsrc_write(char *shname,
	char *buff, size_t bufflen)
{
	int ret;

	LOCK_LIBLEGACY_HDL();
	ret = legacy_common_transaction(shname, buff, bufflen, B_TRUE);
	UNLOCK_LIBLEGACY_HDL();
	return (ret);
}


static int
sa_legacy_share_write(nvlist_t *share)
{
	int rc;
	char *sh_name;
	size_t buflen;
	char *bufp = NULL;

	if ((sh_name = sa_share_get_name(share)) == NULL) {
		rc = SA_NO_SHARE_NAME;
		salog_error(rc, "sa_legacy_share_write");
		goto out;
	}

	if (nvlist_pack(share, &bufp, &buflen, NV_ENCODE_XDR, 0) != 0) {
		rc = SA_XDR_ENCODE_ERR;
		salog_error(rc, "sa_legacy_share_write: %s", sh_name);
		goto out;
	}

	rc = legacy_share_rsrc_write(sh_name, bufp, buflen);

	if (rc < 0) {
		rc = SA_SYSTEM_ERR;
		salog_error(0, "sa_legacy_share_write: "
		    "error writing share '%s': %s",
		    sh_name, strerror(errno));
		goto out;
	} else {
		rc = SA_OK;
	}

out:
	if (bufp != NULL)
		free(bufp);

	return (rc);
}

static int
sa_legacy_share_read(const char *fs_name, const char *sh_name, nvlist_t **share)
{
	nvlist_t *shareval = NULL;
	sa_read_hdl_t hdl;
	int rc = SA_SHARE_NOT_FOUND;
	char *path;
	char *sharename;
	char *mntpnt;

	rc = sa_legacy_share_read_init(&hdl);
	if (rc != SA_OK)
		return (rc);

	mntpnt = malloc(MAXPATHLEN);
	if (mntpnt == NULL)
		goto done;

	/* While fs_name should be a mountpoint, make sure */
	rc = sa_legacy_get_mntpnt_for_path(fs_name, mntpnt, MAXPATHLEN, NULL, 0,
	    NULL, 0);
	if (rc != SA_OK)
		goto done;

	hdl.srh_mntpnt = mntpnt;
	for (rc = sa_legacy_share_read_next(&hdl, &shareval);
	    rc == SA_OK && shareval != NULL;
	    rc = sa_legacy_share_read_next(&hdl, &shareval)) {
		path = sa_share_get_path(shareval);
		sharename = sa_share_get_name(shareval);
		if (path == NULL || sharename == NULL) {
			nvlist_free(shareval);
			continue;
		}
		if (strcmp(sharename, sh_name) == 0) {
			rc = SA_OK;
			break;
		}
		nvlist_free(shareval);
	}
done:
	if (rc == SA_OK)
		*share = shareval;
	(void) sa_legacy_share_read_fini(&hdl);
	free(mntpnt);
	return (rc);
}

static int
sa_legacy_share_read_fini(sa_read_hdl_t *hdl)
{
	scf_pg_destroy(hdl->srh_smf_pg);
	hdl->srh_smf_pg = NULL;
	scf_iter_destroy(hdl->srh_smf_iter);
	hdl->srh_smf_iter = NULL;
	scf_value_destroy(hdl->srh_smf_value);
	hdl->srh_smf_value = NULL;
	scf_property_destroy(hdl->srh_smf_prop);
	hdl->srh_smf_prop = NULL;
	return (SA_OK);
}

static int
sa_legacy_share_read_init(sa_read_hdl_t *hdl)
{
	int ret = SA_OK;

	hdl->srh_smf_pg = scf_pg_create(g_smfhandlep);
	hdl->srh_smf_iter = scf_iter_create(g_smfhandlep);
	hdl->srh_smf_value = scf_value_create(g_smfhandlep);
	hdl->srh_smf_prop = scf_property_create(g_smfhandlep);
	if (hdl->srh_smf_iter == NULL || hdl->srh_smf_value == NULL ||
	    hdl->srh_smf_prop == NULL || hdl->srh_smf_pg == NULL) {
		ret = SA_SYSTEM_ERR;
	} else {
		if (scf_instance_get_pg(g_smfp, LEGACY_PG,
		    hdl->srh_smf_pg) != 0) {
			if (scf_instance_add_pg(g_smfp, LEGACY_PG,
			    SCF_GROUP_FRAMEWORK, 0,
			    hdl->srh_smf_pg) != 0) {
				ret = SA_SYSTEM_ERR;
			}
		}
		if (scf_iter_pg_properties(hdl->srh_smf_iter,
		    hdl->srh_smf_pg) != 0) {
			ret = SA_SYSTEM_ERR;
		}
	}

	if (ret != SA_OK)
		(void) sa_legacy_share_read_fini(hdl);
	return (ret);
}

static int
sa_legacy_share_read_next(sa_read_hdl_t *hdl, nvlist_t **share)
{
	int ret = SA_OK;
	void *nvlist;
	size_t nvlistsize;
	char *name;
	char *mntpnt;

	name = malloc(scf_limit(SCF_LIMIT_MAX_NAME_LENGTH));
	if (name == NULL)
		return (SA_NO_MEMORY);

	nvlistsize = scf_limit(SCF_LIMIT_MAX_VALUE_LENGTH);
	nvlist = malloc(nvlistsize);

	if (nvlist == NULL) {
		ret = SA_NO_MEMORY;
		goto done;
	}
	while (ret == SA_OK) {
		if (scf_iter_next_property(hdl->srh_smf_iter,
		    hdl->srh_smf_prop) == 0) {
			ret = SA_SHARE_NOT_FOUND;
			break;
		}

		if (scf_property_get_name(hdl->srh_smf_prop, name,
		    scf_limit(SCF_LIMIT_MAX_NAME_LENGTH)) > 0) {
			if (strcmp(name, "action_authorization") == 0 ||
			    strcmp(name, "value_authorization") == 0)
				continue;

			if (scf_property_get_value(hdl->srh_smf_prop,
			    hdl->srh_smf_value) == 0) {
				if (scf_value_get_opaque(hdl->srh_smf_value,
				    nvlist, nvlistsize) >= 0) {
					if (nvlist_unpack(nvlist,
					    nvlistsize, share, 0) != 0)
						ret = SA_SYSTEM_ERR;
				} else {
					ret = SA_SYSTEM_ERR;
				}

				if (ret != SA_OK)
					break;

				mntpnt = sa_share_get_mntpnt(*share);
				if (mntpnt != NULL &&
				    strcmp(mntpnt, hdl->srh_mntpnt) == 0)
					break;

				/* skip since on other file system */
				nvlist_free(*share);

				*share = NULL;
			} else {
				ret = SA_SYSTEM_ERR;
			}
		} else {
			ret = SA_SYSTEM_ERR;
		}
	}

done:
	free(name);
	if (nvlist != NULL)
		free(nvlist);

	return (ret);
}

static int
sa_legacy_share_remove(const char *fs_name, const char *sh_name)
{
	NOTE(ARGUNUSED(fs_name))

	int ret = SA_OK;
	scf_property_t *prop;

	LOCK_LIBLEGACY_HDL();
	prop = scf_property_create(g_smfhandlep);
	if (prop == NULL) {
		ret = SA_SYSTEM_ERR;
		goto err;
	}

	ret = legacy_common_transaction(sh_name, NULL, 0, B_FALSE);
err:
	UNLOCK_LIBLEGACY_HDL();

	if (ret == SA_SYSTEM_ERR &&
	    scf_error() == SCF_ERROR_PERMISSION_DENIED)
		ret = SA_NO_PERMISSION;

	if (prop != NULL)
		scf_property_destroy(prop);

	return (ret);
}

static int
sa_legacy_share_get_acl(const char *sh_name, const char *sh_path, acl_t **aclp)
{
	NOTE(ARGUNUSED(sh_name))
	NOTE(ARGUNUSED(sh_path))
	NOTE(ARGUNUSED(aclp))

	return (SA_NOT_SUPPORTED);
}

static int
sa_legacy_share_set_acl(const char *sh_name, const char *sh_path, acl_t *acl)
{
	NOTE(ARGUNUSED(sh_name))
	NOTE(ARGUNUSED(sh_path))
	NOTE(ARGUNUSED(acl))

	return (SA_NOT_SUPPORTED);
}

/*
 * sa_legacy_get_mntpnt_for_path
 *
 * A path identifies its mount point by its st_dev field in stat. To
 * find the mount point, work backward up the path until the st_dev
 * doesn't match. The last to match was the root (mountpoint).
 */
static int
sa_legacy_get_mntpnt_for_path(const char *sh_path, char *mntpnt, size_t mp_len,
    char *volname, size_t vn_len, char *mntopts, size_t opt_len)
{
	char *path = NULL;
	struct stat st;
	FILE *mnttab;
	struct mnttab entry, result;
	int ret = SA_INVALID_SHARE_MNTPNT;
#if defined(lint)
	volname = volname;
	vn_len = vn_len;
#endif

	if (stat(sh_path, &st) < 0)
		return (SA_PATH_NOT_FOUND);

	mnttab = fopen(MNTTAB, "r");
	if (mnttab == NULL)
		return (SA_SYSTEM_ERR);
	(void) memset(&entry, '\0', sizeof (entry));
	entry.mnt_mountp = (char *)sh_path;

	LOCK_MOUNTS();
	/* special case of mount point being the path */
	if (getmntany(mnttab, &result, &entry) == 0) {
		(void) fclose(mnttab);
		if (mntpnt != NULL)
			(void) strlcpy(mntpnt, result.mnt_mountp, mp_len);
		if (mntopts != 0)
			(void) strlcpy(mntopts, result.mnt_mntopts, opt_len);
		UNLOCK_MOUNTS();
		return (SA_OK);
	}
	UNLOCK_MOUNTS();

	path = strdup(sh_path);
	if (path == NULL) {
		ret = SA_NO_MEMORY;
		goto done;
	}

	while (*path != '\0') {
		char *work;
		work = strrchr(path, '/');
		if (work != NULL) {
			*work = '\0';
			if (strlen(path) == 0) {
				/*
				 * lookup mnttab entry for "/"
				 */
				ret = sa_legacy_check_mounts("/",
				    mntpnt, mp_len, mntopts, opt_len);
			} else {
				ret = sa_legacy_check_mounts(path,
				    mntpnt, mp_len, mntopts, opt_len);
			}

			if (ret == SA_OK)
				break;

			if (ret == SA_NO_SHARE_DIR)
				ret = SA_PATH_NOT_FOUND;
		}
	}

done:
	(void) fclose(mnttab);
	free(path);

	return (ret);

}

static void
sa_legacy_mnttab_cache(boolean_t enable)
{
	NOTE(ARGUNUSED(enable))
}

static int
sa_legacy_sharing_enabled(const char *sh_path, sa_proto_t *protos)
{
	NOTE(ARGUNUSED(sh_path))
	/*
	 * There isn't a way to mark a non-ZFS file system as
	 * shareable. Assume ALL for now
	 */

	*protos = SA_PROT_ALL;
	return (SA_OK);
}

static int
sa_legacy_sharing_get_prop(const char *mntpnt, sa_proto_t protos, char **props)
{
	NOTE(ARGUNUSED(mntpnt))
	NOTE(ARGUNUSED(protos))

	*props = strdup("on");
	if (*props == NULL)
		return (SA_NO_MEMORY);
	return (SA_OK);
}

static int
sa_legacy_sharing_set_prop(const char *mntpnt, sa_proto_t protos, char *props)
{
	NOTE(ARGUNUSED(mntpnt))
	NOTE(ARGUNUSED(protos))
	NOTE(ARGUNUSED(props))

	return (SA_OK);
}

static int
sa_legacy_is_legacy(const char *sh_path, boolean_t *legacy)
{
	NOTE(ARGUNUSED(sh_path))

	*legacy = B_TRUE;
	return (SA_OK);
}

/*
 * sa_legacy_mntpnt_is_zoned
 *
 * always return negative
 */
static int
sa_legacy_is_zoned(const char *mntpnt, boolean_t *zoned)
{
	NOTE(ARGUNUSED(mntpnt))

	*zoned = B_FALSE;
	return (0);
}

static int
sa_legacy_check_mounts(char *path, char *mntpnt, size_t mp_len,
    char *mntopts, size_t opt_len)
{
	struct mounts *mount;
	struct mounts *prevmount;
	FILE *mnttab;
	int ret = SA_NO_SHARE_DIR;
	struct mnttab entry;
	struct stat st;
	static struct stat check;

	LOCK_MOUNTS();

	if (check.st_mtime == 0)
		(void) stat(MNTTAB, &check);

	/*
	 * since sa_legacy_cleanup_mounts() sets legacy_mounts to NULL
	 * when cleaning so we use that to force a reload of the mount
	 * table.
	 */
	if (legacy_mounts != NULL) {
		(void) stat(MNTTAB, &st);
		if (st.st_mtime != check.st_mtime)
			sa_legacy_cleanup_mounts();
		check = st;
	}
	if (legacy_mounts == NULL) {
		mnttab = fopen(MNTTAB, "r");
		if (mnttab == NULL) {
			ret = SA_SYSTEM_ERR;
			goto done;
		}
		prevmount = mount = legacy_mounts = NULL;
		while (getmntent(mnttab, &entry) == 0) {
			mount = calloc(1, sizeof (struct mounts));
			if (mount == NULL) {
				ret = SA_NO_MEMORY;
				break;
			}
			mount->mntpnt = strdup(entry.mnt_mountp);
			if (mount->mntpnt == NULL) {
				free(mount);
				ret = SA_NO_MEMORY;
				break;
			}
			mount->mntopts = strdup(entry.mnt_mntopts);
			if (mount->mntopts == NULL) {
				free(mount->mntpnt);
				free(mount);
				ret = SA_NO_MEMORY;
				break;
			}
			if (prevmount == NULL) {
				legacy_mounts = mount;
				prevmount = mount;
			} else {
				prevmount->next = mount;
				prevmount = mount;
			}
		}
		(void) fclose(mnttab);
	}
	for (mount = legacy_mounts; mount != NULL; mount = mount->next) {
		if (strcmp(path, mount->mntpnt) == 0) {
			if (mntpnt != NULL)
				(void) strlcpy(mntpnt, mount->mntpnt, mp_len);
			if (mntopts != NULL)
				(void) strlcpy(mntopts, mount->mntopts,
				    opt_len);
			ret = SA_OK;
			break;
		}
	}
done:
	UNLOCK_MOUNTS();
	if (ret != SA_OK && ret != SA_NO_SHARE_DIR)
		sa_legacy_cleanup_mounts();
	return (ret);
}

static void
sa_legacy_cleanup_mounts()
{
	struct mounts *mount;

	while ((mount = legacy_mounts) != NULL) {
		legacy_mounts = mount->next;
		free(mount->mntpnt);
		free(mount->mntopts);
		free(mount);
	}
}
