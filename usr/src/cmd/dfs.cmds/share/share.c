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

/*
 * Syntax: share [-F proto] -a
 *	   share [-F proto] [-o optionlist] [-d desc] path [sharename]
 *         share [-F proto] [-A]
 *
 *	   unshare [-F proto] -a
 *         unshare [-F proto] [-t] path | sharename
 *
 * where:
 *	-a means all shares
 *	-t means a temporary share
 *	-A means list permanent shares from repository
 *	proto is one of nfs, smb or all
 *	optionlist is defined in share_smb and share_nfs
 *
 *	sharename is required when sharing an SMB share. NFS should
 *	have a sharename but if one is not provided, one will be
 *	constructed.
 *
 *	path follows requirements of the protocol being shared
 */

#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <libgen.h>
#include <strings.h>
#include <limits.h>
#include <pthread.h>
#include <synch.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>
#include <sys/param.h>

#include <libshare.h>
#include <libzfs.h>
#include <sharefs/share.h>

typedef struct sa_mntinfo {
	struct mnttab mi_mntent;
	sa_proto_t mi_protos;
	libzfs_handle_t *mi_zfshdl;
} sa_mntinfo_t;

static int list(char *, boolean_t, boolean_t);
static int share(char *, boolean_t, char *, char *, char *, char *);
static int unshare(char *, boolean_t, boolean_t, char *, char *);
static int upgrade(char *, boolean_t);
static char *get_default_fstype();
#ifdef DEBUG
static void do_atexit(void);
#endif

extern int getshare(FILE *, share_t **);

#ifdef DEBUG
const char *
_umem_debug_init(void)
{
	return ("default,verbose"); /* $UMEM_DEBUG setting */
}

const char *
_umem_logging_init(void)
{
	return ("fail,contents"); /* $UMEM_LOGGING setting */
}
#endif

#define	DEFAULT_FSTYPE		"/etc/dfs/fstypes"


static void
usage(char *cmd)
{
	(void) fprintf(stderr, "%s: %s\n", gettext("usage"),
	    strcmp(cmd, "share") == 0 ?
	    "share [-F proto] [-a | [-o options] [-d desc] path "
	    "[sharename] | [-A]]" :
	    "unshare [-F proto] -a | [-t] path | sharename");
}

int
main(int argc, char *argv[])
{
	int c;
	int ret;
	char *command = NULL;
	boolean_t cmd_share = B_FALSE;
	boolean_t cmd_unshare = B_FALSE;
	boolean_t allshares = B_FALSE;
	boolean_t tmp_unshare = B_FALSE;
	boolean_t do_upgrade = B_FALSE;
	boolean_t force_upgrade = B_FALSE;
	boolean_t active_only = B_TRUE;
	boolean_t proto_allocd = B_FALSE;
	char *protocol = NULL;
	char *path = NULL;
	char *sharename = NULL;
	char *options = NULL;
	char *group = NULL;
	char *description = NULL;

	/*
	 * make sure locale and gettext domain is setup
	 */
	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);
#ifdef DEBUG
	(void) atexit(do_atexit);
#endif

	/*
	 * first sort out which command we are doing (share/unshare)
	 */
	command = basename(argv[0]);
	if (strcmp(command, "share") == 0)
		cmd_share = B_TRUE;
	else if (strcmp(command, "unshare") == 0)
		cmd_unshare = B_TRUE;
	else {
		usage("share");
		exit(1);
	}

	/*
	 * now get the command line options
	 */
	while ((c = getopt(argc, argv, "ad:F:h?o:AtfU:")) != EOF) {
		switch (c) {
		case 'a':
			allshares = B_TRUE;
			break;
		case 'd':
			if (!cmd_share) {
				(void) fprintf(stderr, "unshare: %s\n",
				    gettext("illegal option -- d"));
				usage(command);
				exit(1);
			}
			description = optarg;
			break;
		case 't':
			if (!cmd_unshare) {
				(void) fprintf(stderr, "share: %s\n",
				    gettext("illegal option -- t"));
				usage(command);
				exit(1);
			}
			tmp_unshare = B_TRUE;
			break;
		case 'F':
			protocol = optarg;
			/*
			 * check proto validity
			 */
			if (strcmp(protocol, "all") != 0 &&
			    ((ret = sa_protocol_valid(protocol)) != SA_OK)) {
				(void) fprintf(stderr, "%s: %s\n",
				    sa_strerror(ret),
				    protocol);
				exit(1);
			}
			break;
		case 'o':
			if (!cmd_share) {
				(void) fprintf(stderr, "unshare: %s\n",
				    gettext("illegal option -- o"));
				usage(command);
				exit(1);
			}
			options = optarg;
			break;
		case 'A':
			if (!cmd_share) {
				(void) fprintf(stderr, "unshare: %s\n",
				    gettext("illegal option -- A"));
				usage(command);
				exit(1);
			}
			active_only = B_FALSE;
			break;
		case 'f':
			if (!cmd_share) {
				(void) fprintf(stderr, "unshare: %s\n",
				    gettext("illegal option -- f"));
				usage(command);
				exit(1);
			}
			force_upgrade = B_TRUE;
			break;
		case 'U':
			if (!cmd_share) {
				(void) fprintf(stderr, "unshare: %s\n",
				    gettext("illegal option -- U"));
				usage(command);
				exit(1);
			}
			group = optarg;
			do_upgrade = B_TRUE;
			break;
		case 'h':
		case '?':
			switch (optopt) {
			case 'h':
			case '?':
				usage(command);
				exit(0);
				break;
			}
			/* FALLTHROUGH */
		default:
			usage(command);
			exit(1);
		}
	}

	if (optind > argc) {
		usage(command);
		exit(2);
	}

	if (optind < argc) {
		path = argv[optind++];
		if (optind < argc)
			sharename = argv[optind++];
		if (optind < argc) {
			usage(command);
			exit(2);
		}
	}

	if ((do_upgrade || allshares) && path != NULL) {
		(void) fprintf(stderr, "%s: %s\n",
		    cmd_share ? "share" : "unshare",
		    gettext("too many parameters"));
		usage(command);
		exit(3);
	}

	if (!do_upgrade && force_upgrade) {
		(void) fprintf(stderr, "share: %s\n",
		    gettext("illegal option -- f"));
		usage(command);
		exit(1);
	}

	if (cmd_share && path == NULL &&
	    (options != NULL || description != NULL)) {
		(void) fprintf(stderr, "share: %s\n",
		    gettext("path is required"));
		usage(command);
		exit(3);
	}

	/*
	 * Have basic parse done so call the appropriate subfunction:
	 * share, unshare or list. list has a different default
	 * protocol of "all" when none is provided so don't lookup the
	 * default in this case.
	 */

	if (protocol == NULL && path != NULL && !allshares) {
		protocol = get_default_fstype();
		if (protocol == NULL) {
			(void) fprintf(stderr, "%s: %s\n",
			    cmd_share ? "share" : "unshare",
			    sa_strerror(SA_NO_MEMORY));
			exit(4);
		}

		if ((ret = sa_protocol_valid(protocol)) != SA_OK) {
			(void) fprintf(stderr, "%s: %s\n", sa_strerror(ret),
			    protocol);
			exit(4);
		}
		proto_allocd = B_TRUE;
	}

	sa_mnttab_cache(B_TRUE);

	if (cmd_unshare)
		ret = unshare(protocol, allshares, tmp_unshare, path,
		    sharename);
	else if (path != NULL || allshares)
		ret = share(protocol, allshares, options, path,
		    sharename, description);
	else if (do_upgrade)
		ret = upgrade(group, force_upgrade);
	else
		ret = list(protocol, allshares, active_only);

	if (proto_allocd)
		free(protocol);

	sa_mnttab_cache(B_FALSE);

	return (ret);
}

/*
 * Take the data from the command line and create a share. The properties
 * specified in 'options' are parsed and added to the protocol nvlist.
 * 'sharename' and 'desc' have already been converted to utf-8
 */
static int
simple_parse(nvlist_t **share, char *sharename, char *path,
    char *protocol, char *options, char *desc, char *errbuf, size_t errlen)
{
	char *propstr = NULL;
	char *mntpnt = NULL;
	size_t propstrlen;
	int ret = SA_OK;
	boolean_t errbuf_set = B_FALSE;

	/*
	 * construct a ZFS style protocol string since this is the
	 * easiest form to use. (ie "prot=nfs,sec=sys,ro=*")
	 * Once constructed, parse into a share.
	 */
	*share = NULL;

	propstrlen = strlen("prot=") + strlen(protocol);
	if (options != NULL)
		propstrlen += (strlen(options) + 1);	/* 1 for the comma */

	propstrlen++;			/* extra byte for terminating null */
	propstr = malloc(propstrlen);
	if (propstr == NULL) {
		ret = SA_NO_MEMORY;
		goto err_ret;
	}

	if (options == NULL || *options == '\0')
		(void) snprintf(propstr, propstrlen, "prot=%s", protocol);
	else
		(void) snprintf(propstr, propstrlen, "prot=%s,%s",
		    protocol, options);

	/*
	 * convert property string to nvlist via libshare
	 */
	errbuf[0] = '\0';
	ret = sa_share_parse(propstr, B_FALSE, share, errbuf, errlen);
	if (ret != SA_OK) {
		*share = NULL; /* just to be safe */
		errbuf_set = B_TRUE;
		goto err_ret;
	}

	/*
	 * add name, path, description and mountpoint to share
	 */
	if ((sharename != NULL) &&
	    ((ret = sa_share_set_name(*share, sharename)) != SA_OK))
		goto err_ret;

	if ((path != NULL) &&
	    ((ret = sa_share_set_path(*share, path)) != SA_OK))
		goto err_ret;

	if ((desc != NULL) &&
	    ((ret = sa_share_set_desc(*share, desc)) != SA_OK))
		goto err_ret;

	if ((mntpnt = malloc(MAXPATHLEN)) == NULL) {
		ret = SA_NO_MEMORY;
		goto err_ret;
	}

	ret = sa_get_mntpnt_for_path(path, mntpnt, MAXPATHLEN,
	    NULL, 0, NULL, 0);
	if (ret != SA_OK) {
		(void) snprintf(errbuf, errlen, "%s: %s",
		    gettext("path not found"), path);
		errbuf_set = B_TRUE;
		goto err_ret;
	}

	if ((ret = sa_share_set_mntpnt(*share, mntpnt)) != SA_OK)
		goto err_ret;

	free(propstr);
	free(mntpnt);

	return (SA_OK);

err_ret:
	if (propstr != NULL)
		free(propstr);

	if (mntpnt != NULL)
		free(mntpnt);

	if (*share != NULL) {
		sa_share_free(*share);
		*share = NULL;
	}

	if (!errbuf_set)
		(void) snprintf(errbuf, errlen, "%s", sa_strerror(ret));

	return (ret);
}

static int
unshareall(char *protocol)
{
	sa_proto_t protos;
	int ret;
	struct mnttab entry;
	FILE *fp;

	fp = fopen(MNTTAB, "r");
	if (fp == NULL) {
		(void) fprintf(stderr, "unshare: %s %s: %s\n",
		    gettext("error opening"), MNTTAB, strerror(errno));
		return (SA_SYSTEM_ERR);
	}

	if (strcmp(protocol, "all") == 0)
		protos = SA_PROT_ALL;
	else
		protos = sa_val_to_proto(protocol);
	/*
	 * Don't bother to try a file system that isn't
	 * shareable.
	 */
	while (getmntent(fp, &entry) == 0) {
		char *mntpnt;

		if (sa_mntent_is_shareable(&entry) != SA_OK)
			continue;

		if ((mntpnt = strdup(entry.mnt_mountp)) == NULL) {
			(void) fprintf(stderr, "unshare: %s %s: %s\n",
			    gettext("failed to unshare"), entry.mnt_mountp,
			    sa_strerror(SA_NO_MEMORY));
			(void) fclose(fp);
			return (SA_NO_MEMORY);
		}

		ret = sa_fs_unpublish(mntpnt, protos, B_FALSE);
		if (ret != SA_OK) {
			(void) fprintf(stderr, "unshare: %s %s: %s\n",
			    gettext("failed to unshare"),
			    mntpnt, sa_strerror(ret));
		}
		free(mntpnt);
	}
	(void) fclose(fp);

	return (SA_OK);
}

/*
 * update_share
 *
 * remove protocol properties specified in proto from share.
 * If no properties remain, delete the share, otherwise
 * update the share in permanent storage.
 */
static int
update_share(nvlist_t *share, sa_proto_t proto)
{
	sa_proto_t p;
	char *utf8_name;
	char *disp_name = NULL;
	char *sh_path;
	char *sh_mntpnt;
	boolean_t share_updated = B_FALSE;
	int rc = SA_OK;

	if (share == NULL)
		return (SA_INVALID_SHARE);

	utf8_name = sa_share_get_name(share);
	if (utf8_name &&
	    sa_utf8_to_locale(utf8_name, &disp_name) != SA_OK)
		disp_name = strdup(utf8_name);
	sh_path = sa_share_get_path(share);
	sh_mntpnt = sa_share_get_mntpnt(share);

	for (p = sa_proto_first(); p != SA_PROT_NONE;
	    p = sa_proto_next(p)) {
		if (!(proto & p) ||
		    sa_share_get_proto(share, p) == NULL) {
			/* not a requested protocol */
			continue;
		}

		/* remove this protocol from the share */
		if (nvlist_remove(share, sa_proto_to_val(p),
		    DATA_TYPE_NVLIST) != 0) {
			(void) fprintf(stderr, "unshare: %s %s:%s:%s\n",
			    gettext("failed to remove"),
			    disp_name ? disp_name : "-",
			    sh_path ? sh_path : "-",
			    sa_proto_to_val(p));
			rc = SA_PARTIAL_UNPUBLISH;
		} else {
			share_updated = B_TRUE;
		}
	}

	if (share_updated) {
		if (sa_share_proto_count(share) == 0) {
			rc = sa_share_remove(utf8_name, sh_mntpnt);
			if (rc != SA_OK) {
				(void) fprintf(stderr,
				    "unshare: %s %s:%s: %s\n",
				    gettext("failed to remove share"),
				    disp_name ? disp_name : "-",
				    sh_path ? sh_path : "-",
				    sa_strerror(rc));
			}
		} else {
			rc = sa_share_write(share);
			if (rc != SA_OK) {
				(void) fprintf(stderr,
				    "unshare: %s %s:%s: %s\n",
				    gettext("failed to update share"),
				    disp_name ? disp_name : "-",
				    sh_path ? sh_path : "-",
				    sa_strerror(rc));
			}
		}
	} else {
		rc = SA_SHARE_NOT_FOUND;
	}

	if (disp_name)
		free(disp_name);

	return (rc);
}

static int
unshare_by_name(char *sh_name, char *sh_path, sa_proto_t proto,
    boolean_t tmp_unshare)
{
	nvlist_t *share;
	char *mntpnt;
	char *utf8_name;
	sa_proto_t p, status;
	boolean_t share_found = B_FALSE;
	int rc = SA_OK;

	if (sa_locale_to_utf8(sh_name, &utf8_name) != SA_OK)
		utf8_name = strdup(sh_name);
	if (utf8_name == NULL) {
		rc = SA_NO_MEMORY;
		(void) fprintf(stderr, "unshare: %s\n", sa_strerror(rc));
		return (rc);
	}

	/*
	 * first unshare if currently shared
	 */
	rc = sa_share_lookup(utf8_name, sh_path, proto, &share);
	if (rc == SA_OK) {
		/*
		 * since sh_path may be NULL, use
		 * the path from the located share
		 */
		char *path = sa_share_get_path(share);

		/*
		 * no need for zone check here, sharetab for
		 * current zone will only contain shares that
		 * were published in this zone.
		 */

		status = sa_share_get_status(share);
		for (p = sa_proto_first(); p != SA_PROT_NONE;
		    p = sa_proto_next(p)) {
			/*
			 * Must be a requested protocol (proto) and
			 * share must be currently shared for protocol (status)
			 * and share must be configured for protocol.
			 */
			if (!(proto & p) || !(status & p) ||
			    sa_share_get_proto(share, p) == NULL) {
				continue;
			}

			share_found = B_TRUE;
			rc = sa_share_unpublish(share, p, 1);
			if (rc != SA_OK) {
				(void) fprintf(stderr,
				    "unshare: %s %s:%s: %s\n",
				    gettext("failed to unshare"),
				    sh_name, path ? path : "", sa_strerror(rc));
				rc = SA_PARTIAL_UNPUBLISH;
			}
		}

		sa_share_free(share);
	}

	if (tmp_unshare) {
		if (!share_found) {
			(void) fprintf(stderr, "unshare: %s: %s:%s\n",
			    gettext("share not found"), sh_name,
			    sh_path ? sh_path : "");
			rc = SA_SHARE_NOT_FOUND;
		}
		free(utf8_name);
		return (rc);
	}

	if (sh_path != NULL) {
		if ((mntpnt = malloc(MAXPATHLEN + 1)) == NULL) {
			(void) fprintf(stderr, "unshare: %s\n",
			    sa_strerror(SA_NO_MEMORY));
			free(utf8_name);
			return (SA_NO_MEMORY);
		}
		rc = sa_get_mntpnt_for_path(sh_path, mntpnt, MAXPATHLEN,
		    NULL, 0, NULL, 0);
		/*
		 * If the mountpoint cannot be determined from the path,
		 * then set mntpnt to NULL to search all repositories
		 * for a matching share.
		 */
		if (rc != SA_OK) {
			free(mntpnt);
			mntpnt = NULL;
			rc = SA_OK;
		}
	} else {
		mntpnt = NULL;
	}

	/*
	 * now update permanent storage
	 */
	if (sa_share_read(mntpnt, utf8_name, &share) != SA_OK) {
		if (!share_found) {
			(void) fprintf(stderr, "unshare: %s: %s:%s\n",
			    gettext("share not found"),
			    sh_name, sh_path ? sh_path : "");
			rc = SA_SHARE_NOT_FOUND;
		}
		if (mntpnt != NULL)
			free(mntpnt);
		free(utf8_name);
		return (rc);
	}

	if (mntpnt != NULL)
		free(mntpnt);
	free(utf8_name);

	/*
	 * Now that we have a path, check for zone privileges
	 * Use the mountpoint from the share because the path
	 * may no longer exist.
	 */
	if (!sa_path_in_current_zone(sa_share_get_mntpnt(share))) {
		(void) fprintf(stderr, "unshare: %s: %s\n",
		    gettext("permission denied"),
		    sa_strerror(SA_SHARE_OTHERZONE));
		sa_share_free(share);
		return (SA_NO_PERMISSION);
	}

	if ((rc = update_share(share, proto)) == SA_OK)
		share_found = B_TRUE;

	sa_share_free(share);

	if (!share_found) {
		(void) fprintf(stderr, "unshare: %s: %s:%s\n",
		    gettext("share not found"),
		    sh_name, sh_path ? sh_path : "");
		rc = SA_SHARE_NOT_FOUND;
	}

	return (rc);
}

/*
 * unshare_by_path
 *
 * This routine unpublishes all shares for the specified path and protocol.
 * If the persist flag is set, then the share is also updated in permanent
 * storage.
 *
 */
static int
unshare_by_path(char *sh_path, boolean_t tmp_unshare, sa_proto_t proto)
{
	char *mntpnt;
	sa_proto_t p;
	sa_proto_t status;
	char *utf8_name;
	char *path;
	void *hdl;
	nvlist_t *share;
	nvlist_t *share_list = NULL;
	nvpair_t *nvp;
	boolean_t path_exists = B_TRUE;
	boolean_t share_found = B_FALSE;
	int ret;
	int rc = SA_OK;

	if ((mntpnt = malloc(MAXPATHLEN + 1)) == NULL) {
		(void) fprintf(stderr, "unshare: %s\n",
		    sa_strerror(SA_NO_MEMORY));
		return (SA_NO_MEMORY);
	}

	rc = sa_get_mntpnt_for_path(sh_path, mntpnt, MAXPATHLEN,
	    NULL, 0, NULL, 0);

	/*
	 * If the mountpoint cannot be determined from the path,
	 * then set mntpnt to NULL to search all published shares.
	 */
	if (rc != SA_OK) {
		free(mntpnt);
		mntpnt = NULL;
		path_exists = B_FALSE;
		rc = SA_OK;
	}

	if (nvlist_alloc(&share_list, NV_UNIQUE_NAME, 0) != 0) {
		rc = SA_NO_MEMORY;
		(void) fprintf(stderr, "unshare: %s\n", sa_strerror(rc));
		goto done;
	}

	/*
	 * search the share cache for shares matching sh_path
	 * and add to share list.
	 */
	if (sa_share_find_init(mntpnt, proto, &hdl) == SA_OK) {
		while (sa_share_find_next(hdl, &share) == SA_OK) {

			if ((utf8_name = sa_share_get_name(share)) == NULL ||
			    (path = sa_share_get_path(share)) == NULL ||
			    strcmp(sh_path, path) != 0) {
				sa_share_free(share);
				continue;
			}

			/*
			 * no need to do zone check here, If the share is
			 * found in sharetab, then it is ok to unpublish
			 * because there is a separate sharetab for each zone.
			 *
			 * So go ahead and add the share to the list.
			 */
			if (nvlist_add_nvlist(share_list, utf8_name, share)
			    != 0) {
				sa_share_free(share);
				sa_share_find_fini(hdl);
				rc = SA_NO_MEMORY;
				(void) fprintf(stderr, "unshare: %s\n",
				    sa_strerror(rc));
				goto done;
			}
			sa_share_free(share);
		}
		sa_share_find_fini(hdl);
	}

	/*
	 * we now have a list of published shares for path.
	 * Unpublish for proto.
	 */
	for (nvp = nvlist_next_nvpair(share_list, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(share_list, nvp)) {
		if (nvpair_type(nvp) != DATA_TYPE_NVLIST)
			continue;

		if (nvpair_value_nvlist(nvp, &share) != 0) {
			rc = SA_PARTIAL_UNPUBLISH;
			continue;
		}

		utf8_name = sa_share_get_name(share);
		path = sa_share_get_path(share);
		status = sa_share_get_status(share);

		for (p = sa_proto_first(); p != SA_PROT_NONE;
		    p = sa_proto_next(p)) {
			/*
			 * Must be a requested protocol (proto) and
			 * share must be currently shared for protocol (status)
			 * and share must be configured for protocol.
			 */
			if (!(proto & p) || !(status & p) ||
			    sa_share_get_proto(share, p) == NULL) {
				continue;
			}

			share_found = B_TRUE;
			ret = sa_share_unpublish(share, p, 1);
			if (ret != SA_OK) {
				char *disp_name;

				if (sa_utf8_to_locale(utf8_name,
				    &disp_name) != SA_OK)
					disp_name = strdup(utf8_name);

				(void) fprintf(stderr,
				    "unshare: %s %s:%s: %s\n",
				    gettext("failed to unshare"),
				    disp_name ? disp_name : "",
				    path, sa_strerror(ret));

				if (disp_name)
					free(disp_name);
				/*
				 * bail out early if not permitted to unshare
				 */
				if (ret == SA_NO_PERMISSION) {
					nvlist_free(share_list);
					rc = ret;
					goto done;
				}
				rc = SA_PARTIAL_UNPUBLISH;
			}
		}
	}

	nvlist_free(share_list);
	share_list = NULL;

	/*
	 * if only a temporary unshare, then we are done
	 */
	if (tmp_unshare) {
		if (!share_found) {
			(void) fprintf(stderr, "unshare: %s %s\n",
			    gettext("share not found for"), sh_path);
			rc = SA_SHARE_NOT_FOUND;
		}
		goto done;
	}

	/*
	 * Make sure path is shareable in the current zone
	 * If the path does not exist, we will need to do this
	 * check on each share before updating them.
	 */
	if (path_exists &&
	    !sa_path_in_current_zone(sh_path)) {
		(void) fprintf(stderr, "unshare: %s: %s\n",
		    gettext("permission denied"),
		    sa_strerror(SA_SHARE_OTHERZONE));
		rc = SA_NO_PERMISSION;
		goto done;
	}

	if (nvlist_alloc(&share_list, NV_UNIQUE_NAME, 0) != 0) {
		rc = SA_NO_MEMORY;
		(void) fprintf(stderr, "unshare: %s\n", sa_strerror(rc));
		goto done;
	}

	/*
	 * Now go through permanent shares and get a list
	 * of shares that need to be updated.
	 */
	if (sa_share_read_init(mntpnt, &hdl) == SA_OK) {
		while (sa_share_read_next(hdl, &share) == SA_OK) {

			if ((utf8_name = sa_share_get_name(share)) == NULL ||
			    (path = sa_share_get_path(share)) == NULL ||
			    strcmp(sh_path, path) != 0) {
				sa_share_free(share);
				continue;
			}

			/*
			 * if the path does not exist, then we did not do
			 * the zone check previously, so do it now
			 * Use the mountpoint from the share, because
			 * the path is invalid.
			 */
			if (!path_exists &&
			    !sa_path_in_current_zone(
			    sa_share_get_mntpnt(share))) {
				sa_share_free(share);
				continue;
			}

			if (nvlist_add_nvlist(share_list, utf8_name, share)
			    != 0) {
				sa_share_free(share);
				sa_share_read_fini(hdl);
				rc = SA_NO_MEMORY;
				(void) fprintf(stderr, "unshare: %s\n",
				    sa_strerror(rc));
				goto done;
			}
			sa_share_free(share);
		}
		sa_share_read_fini(hdl);
	}

	/*
	 * now we have a complete list of shares for path.
	 * Remove specified protocol properties and update
	 */
	for (nvp = nvlist_next_nvpair(share_list, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(share_list, nvp)) {
		if (nvpair_type(nvp) != DATA_TYPE_NVLIST)
			continue;

		if (nvpair_value_nvlist(nvp, &share) != 0)
			continue;

		utf8_name = sa_share_get_name(share);
		path = sa_share_get_path(share);

		/*
		 * found a share for this path, but path no longer exists
		 * so remove it by name.
		 */
		if (!path_exists) {
			ret = sa_share_remove(utf8_name, NULL);
			if (ret != SA_OK) {
				char *disp_name = NULL;

				if (utf8_name &&
				    sa_utf8_to_locale(utf8_name,
				    &disp_name) != SA_OK)
					disp_name = strdup(utf8_name);

				(void) fprintf(stderr,
				    "unshare: %s %s:%s: %s\n",
				    gettext("failed to remove share"),
				    disp_name ? disp_name : "-",
				    path ? path : "-", sa_strerror(ret));

				if (disp_name)
					free(disp_name);
			}
			continue;
		}

		if ((ret = update_share(share, proto)) != SA_OK)
			rc = SA_PARTIAL_UNPUBLISH;
		else
			share_found = B_TRUE;
	}

	nvlist_free(share_list);

	if (!share_found) {
		(void) fprintf(stderr, "unshare: %s %s\n",
		    gettext("share not found for"),
		    sh_path);
		rc = SA_SHARE_NOT_FOUND;
	}
done:
	if (mntpnt != NULL)
		free(mntpnt);

	return (rc);
}

static int
unshare(char *protocol, boolean_t allshares, boolean_t tmp_unshare, char *path,
    char *sharename)
{
	int ret;
	sa_proto_t proto;
	char *pvalue = NULL;
	char cleanpath[PATH_MAX + 1];

	if (allshares)
		return (unshareall(protocol == NULL ? "all" : protocol));

	if (protocol == NULL) {
		protocol = pvalue = get_default_fstype();
		if (pvalue == NULL) {
			(void) fprintf(stderr, "unshare: %s\n",
			    sa_strerror(SA_NO_MEMORY));
			return (SA_NO_MEMORY);
		}

		if ((ret = sa_protocol_valid(protocol)) != SA_OK) {
			(void) fprintf(stderr, "%s: %s\n", sa_strerror(ret),
			    protocol);
			return (ret);
		}
	}

	if (strcmp(protocol, "all") == 0)
		proto = SA_PROT_ALL;
	else
		proto = sa_val_to_proto(protocol);

	if (proto == SA_PROT_NONE) {
		(void) fprintf(stderr, "unshare: %s: %s\n",
		    gettext("unknown protocol"), protocol);
		ret = SA_NO_SUCH_PROTO;
		goto done;

	}

	if (path == NULL) {
		(void) fprintf(stderr, "unshare: %s\n",
		    gettext("path is required"));
		ret = SA_NO_SHARE_PATH;
		goto done;
	}

	/*
	 * if path doesn't start with /, it may be a sharename. Assume
	 * that if sharename is NULL otherwise it is an error.
	 */
	if (*path != '/') {
		if (sharename == NULL) {
			sharename = path;
			path = NULL;
		} else {
			(void) fprintf(stderr, "unshare: %s: %s\n",
			    gettext("absolute path required"), path);
			ret = SA_INVALID_SHARE_PATH;
			goto done;
		}
	} else {
		if (realpath(path, cleanpath) != NULL)
			path = cleanpath;
	}

	if (sharename == NULL) {
		/*
		 * only have a path so remove all shares associated with
		 * the path obeying the protocol specification.
		 */
		ret = unshare_by_path(path, tmp_unshare, proto);
	} else {
		/*
		 * have a normal share specification which includes the
		 * sharename so just remove one share (for each protocol
		 * specified).
		 */
		ret = unshare_by_name(sharename, path, proto, tmp_unshare);
	}

done:
	free(pvalue);

	return (ret);
}

static void
sa_mntinfo_free(sa_mntinfo_t *mntinfop)
{
	if (mntinfop == NULL)
		return;

	if (mntinfop->mi_mntent.mnt_special != NULL)
		free(mntinfop->mi_mntent.mnt_special);
	if (mntinfop->mi_mntent.mnt_mountp != NULL)
		free(mntinfop->mi_mntent.mnt_mountp);
	if (mntinfop->mi_mntent.mnt_fstype != NULL)
		free(mntinfop->mi_mntent.mnt_fstype);
	if (mntinfop->mi_mntent.mnt_mntopts != NULL)
		free(mntinfop->mi_mntent.mnt_mntopts);
	free(mntinfop);
}

static sa_mntinfo_t *
sa_mntinfo_alloc(struct mnttab *entryp)
{
	sa_mntinfo_t *mntinfop;

	mntinfop = malloc(sizeof (sa_mntinfo_t));
	if (mntinfop == NULL)
		return (NULL);
	bzero(mntinfop, sizeof (sa_mntinfo_t));
	if (entryp->mnt_special != NULL) {
		mntinfop->mi_mntent.mnt_special = strdup(entryp->mnt_special);
		if (mntinfop->mi_mntent.mnt_special == NULL) {
			sa_mntinfo_free(mntinfop);
			return (NULL);
		}
	}

	if (entryp->mnt_mountp != NULL) {
		mntinfop->mi_mntent.mnt_mountp = strdup(entryp->mnt_mountp);
		if (mntinfop->mi_mntent.mnt_mountp == NULL) {
			sa_mntinfo_free(mntinfop);
			return (NULL);
		}
	}

	if (entryp->mnt_fstype != NULL) {
		mntinfop->mi_mntent.mnt_fstype = strdup(entryp->mnt_fstype);
		if (mntinfop->mi_mntent.mnt_fstype == NULL) {
			sa_mntinfo_free(mntinfop);
			return (NULL);
		}
	}

	if (entryp->mnt_mntopts != NULL) {
		mntinfop->mi_mntent.mnt_mntopts = strdup(entryp->mnt_mntopts);
		if (mntinfop->mi_mntent.mnt_mntopts == NULL) {
			sa_mntinfo_free(mntinfop);
			return (NULL);
		}
	}

	return (mntinfop);
}

static int
share_zfs_publish(void *arg)
{
	sa_mntinfo_t *mntinfop = (sa_mntinfo_t *)arg;
	struct mnttab *mntentp = &mntinfop->mi_mntent;
	zfs_handle_t *zhp;
	int rc, ret = SA_OK;
	char mountpoint[MAXPATHLEN];

	zhp = zfs_open(mntinfop->mi_zfshdl, mntentp->mnt_special,
	    ZFS_TYPE_DATASET);

	if (zhp != NULL &&
	    zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) == 0 &&
	    strcmp(mountpoint, ZFS_MOUNTPOINT_LEGACY) != 0) {
		if (mntinfop->mi_protos == SA_PROT_ALL)
			rc = zfs_shareall(zhp);
		else if (mntinfop->mi_protos == SA_PROT_NFS)
			rc = zfs_share_nfs(zhp);
		else if (mntinfop->mi_protos == SA_PROT_SMB)
			rc = zfs_share_smb(zhp);
		else {
			rc = 0;
			ret = SA_INVALID_PROTO;
		}

		if (rc != 0)
			ret = SA_INTERNAL_ERR;
	} else {
		/*
		 * If zfs_open fails, it is likely this is a zfs dataset
		 * that was added to a non-global zone and it is not
		 * accessible. Treat this and any datasets with the
		 * mountpoint property set to 'legacy' as a legacy filesystem
		 * and look in SMF for shares.
		 */
		if (sa_mntent_is_shareable(mntentp) == SA_OK)
			ret = sa_fs_publish(mntentp->mnt_mountp,
			    mntinfop->mi_protos, B_FALSE);
	}

	if (zhp != NULL)
		zfs_close(zhp);
	return (ret);
}

static int
shareall(char *protocol)
{
	sa_proto_t protos;
	int err = SA_OK;
	int ret;
	struct mnttab entry;
	FILE *fp;
	libzfs_handle_t *zfs_hdl;
	sa_mntinfo_t *mntinfop;

	if ((zfs_hdl = libzfs_init()) == NULL) {
		(void) fprintf(stderr, "share: %s libzfs\n",
		    gettext("error initializing"));
		return (SA_SYSTEM_ERR);
	}

	fp = fopen(MNTTAB, "r");
	if (fp == NULL) {
		libzfs_fini(zfs_hdl);
		(void) fprintf(stderr, "share: %s %s: %s\n",
		    gettext("error opening"), MNTTAB, strerror(errno));
		return (SA_SYSTEM_ERR);
	}

	if (strcmp(protocol, "all") == 0)
		protos = SA_PROT_ALL;
	else
		protos = sa_val_to_proto(protocol);
	/*
	 * Don't bother to try a file system that isn't shareable
	 */
	while (getmntent(fp, &entry) == 0) {
		char *mntpnt;

		if ((mntpnt = strdup(entry.mnt_mountp)) == NULL) {
			err = SA_NO_MEMORY;
			(void) fprintf(stderr, "share: %s\n",
			    sa_strerror(err));
			break;
		}

		/* TODO: Create a pthread here. */
		if (strcmp(entry.mnt_fstype, MNTTYPE_ZFS) == 0) {
			mntinfop = sa_mntinfo_alloc(&entry);
			if (mntinfop == NULL) {
				err = SA_NO_MEMORY;
				(void) fprintf(stderr, "share: %s\n",
				    sa_strerror(err));
				free(mntpnt);
				break;
			}

			mntinfop->mi_protos = protos;
			mntinfop->mi_zfshdl = zfs_hdl;

			ret = share_zfs_publish(mntinfop);

			sa_mntinfo_free(mntinfop);
		} else {
			/*
			 * legacy mountpoint, use libshare directly
			 */
			if (sa_mntent_is_shareable(&entry) == SA_OK)
				ret = sa_fs_publish(mntpnt, protos, B_FALSE);
			else
				ret = SA_OK;
		}

		if (ret != SA_OK && ret != SA_SHARE_NOT_FOUND) {
			(void) fprintf(stderr, "share: %s %s: %s\n",
			    gettext("error sharing"), mntpnt,
			    sa_strerror(ret));
		}
		free(mntpnt);
	}
	(void) fclose(fp);
	libzfs_fini(zfs_hdl);

	return (SA_OK);
}

/*
 * oneshareonly
 *
 * If there is one, and only one, share with the given path, return
 * it. Return NULL if the condition isn't met. share will always be
 * called with a non-NULL value.
 */
static int
oneshareonly(char *path, nvlist_t **share)
{
	nvlist_t *curshare;
	nvlist_t *firstshare = NULL;
	char *mntpnt;
	int err;
	void *hdl;

	*share = NULL;

	if ((mntpnt = malloc(MAXPATHLEN + 1)) == NULL)
		return (SA_NO_MEMORY);

	err = sa_get_mntpnt_for_path(path, mntpnt, MAXPATHLEN,
	    NULL, 0, NULL, 0);
	if (err != SA_OK) {
		free(mntpnt);
		return (err);
	}
	if (sa_share_read_init(mntpnt, &hdl) == SA_OK) {
		while (sa_share_read_next(hdl, &curshare) == SA_OK) {
			char *curpath;

			/* only look at those that match */
			curpath = sa_share_get_path(curshare);
			if (curpath != NULL && strcmp(curpath, path) == 0) {
				if (firstshare == NULL) {
					firstshare = curshare;
					continue;
				} else {
					sa_share_free(curshare);
					sa_share_free(firstshare);
					firstshare = NULL;
					err = SA_DUPLICATE_PATH;
					break;
				}
			}
			sa_share_free(curshare);
		}
		sa_share_read_fini(hdl);
	}
	free(mntpnt);
	*share = firstshare;
	if (firstshare == NULL && err == SA_OK)
		err = SA_SHARE_NOT_FOUND;

	return (err);
}

static int
share(char *protocol, boolean_t allshares, char *options,
    char *path, char *sharename, char *desc)
{
	int ret;
	sa_proto_t proto;
	nvlist_t *new_share = NULL;
	nvlist_t *props;
	nvlist_t *new_props = NULL;
	nvlist_t *old_share = NULL;
	char *old_path = NULL;
	boolean_t new = B_TRUE;
	char errbuf[128];
	boolean_t name_alloc = B_FALSE;
	boolean_t utf8_name_alloc = B_FALSE;
	char cleanpath[PATH_MAX + 1];
	char *utf8_name;
	char *utf8_desc = NULL;

	/*
	 * Make sure args are sane before going too far
	 */

	if (allshares)
		return (shareall(protocol == NULL ? "all" : protocol));

	if (strcmp(protocol, "all") == 0) {
		(void) fprintf(stderr, "share: %s \"all\"\n",
		    gettext("unsupported protocol"));
		return (SA_INVALID_PROTO);
	}

	proto = sa_val_to_proto(protocol);
	if (proto == SA_PROT_NONE) {
		(void) fprintf(stderr, "share: %s \"%s\"\n",
		    gettext("unknown protocol"), protocol);
		return (SA_NO_SUCH_PROTO);
	}

	if (realpath(path, cleanpath) != NULL)
		path = cleanpath;

	if (*path != '/') {
		(void) fprintf(stderr, "share: %s: %s\n",
		    gettext("relative path not supported"), path);
		return (SA_INVALID_SHARE_PATH);
	}

	ret = sa_path_is_shareable(path);
	if (ret != SA_OK) {
		switch (ret) {
		case SA_SHARE_OTHERZONE:
			(void) fprintf(stderr, "share: %s: %s\n",
			    gettext("permission denied"),
			    sa_strerror(SA_SHARE_OTHERZONE));
			return (SA_NO_PERMISSION);

		default:
			(void) fprintf(stderr, "share: %s: %s\n",
			    path, sa_strerror(ret));
			return (ret);
		}
	}

	if (sharename == NULL) {
		sharename = strdup(path);
		if (sharename == NULL)
			return (SA_NO_MEMORY);
		sa_path_to_shr_name(sharename);
		name_alloc = B_TRUE;
	}

	if (sa_locale_to_utf8(sharename, &utf8_name) != SA_OK) {
		if ((utf8_name = strdup(sharename)) == NULL) {
			ret = SA_NO_MEMORY;
			(void) fprintf(stderr, "share: %s: %s\n",
			    sharename, sa_strerror(ret));
			goto done;
		}
	}
	utf8_name_alloc = B_TRUE;

	if (desc != NULL &&
	    sa_locale_to_utf8(desc, &utf8_desc) != SA_OK) {
		if ((utf8_desc = strdup(desc)) == NULL) {
			ret = SA_NO_MEMORY;
			(void) fprintf(stderr, "share: %s: %s\n",
			    sharename, sa_strerror(ret));
			goto done;
		}
	}

	ret = simple_parse(&new_share, utf8_name, path, protocol, options,
	    utf8_desc, errbuf, sizeof (errbuf));
	if (ret != SA_OK) {
		(void) fprintf(stderr, "share: %s\n", errbuf);
		goto done;
	}

	/*
	 * now check for preexistence to determine what to do. Some
	 * protocols support multiple shares per path while others
	 * don't.
	 */

	ret = sa_share_read(path, utf8_name, &old_share);
	if (ret == SA_SHARE_NOT_FOUND && name_alloc) {
		/*
		 * We are using the "default" name since one wasn't
		 * provided on the command line. To make things a bit more
		 * intuitive, check to see if there is another share for
		 * the path. If there is and there is only one, we will
		 * use that one rather than trying to create a new one.
		 */
		ret = oneshareonly(path, &old_share);
		if (ret != SA_OK && ret != SA_DUPLICATE_PATH &&
		    ret != SA_SHARE_NOT_FOUND) {
			(void) fprintf(stderr, "share: %s: %s\n",
			    path, sa_strerror(ret));
			goto done;
		}
		if (ret == SA_OK) {
			/*
			 * found a share to use, update sharename and utf8_name
			 */
			free(sharename);
			name_alloc = B_FALSE;
			free(utf8_name);
			utf8_name_alloc = B_FALSE;

			utf8_name = sa_share_get_name(old_share);
			if (utf8_name == NULL) {
				ret = SA_NO_SHARE_NAME;
				(void) fprintf(stderr, "share: %s: %s\n",
				    path, sa_strerror(ret));
				goto done;
			}
			ret = sa_utf8_to_locale(utf8_name, &sharename);
			if (ret != SA_OK) {
				if ((sharename = strdup(utf8_name)) == NULL) {
					ret = SA_NO_MEMORY;
					(void) fprintf(stderr,
					    "share: %s: %s\n",
					    path, sa_strerror(ret));
					goto done;
				}
			}
			name_alloc = B_TRUE;
		}
	}

	if (ret == SA_OK) {
		/*
		 * make sure this is for the same path
		 */
		old_path = sa_share_get_path(old_share);
		if (old_path != NULL && strcmp(old_path, path) != 0) {
			sa_share_free(old_share);
			ret = SA_DUPLICATE_NAME;
			(void) fprintf(stderr, "share: %s: %s\n",
			    sa_strerror(ret), sharename);
			goto done;
		}

		/*
		 * Found a share so update the properties with new
		 * sa_share_set_proto will replace any existing properties
		 * with the new ones.
		 */
		if ((props = sa_share_get_proto(new_share, proto)) == NULL ||
		    nvlist_dup(props, &new_props, 0) != 0 ||
		    sa_share_set_proto(old_share, proto, new_props) != SA_OK) {
			if (new_props != NULL)
				nvlist_free(new_props);
			sa_share_free(old_share);
			ret = SA_NO_MEMORY;
			(void) fprintf(stderr, "share: %s\n",
			    sa_strerror(ret));
			goto done;
		}
		nvlist_free(new_props);

		/*
		 * update description
		 */
		if (utf8_desc != NULL) {
			if (sa_share_set_desc(old_share, utf8_desc) != SA_OK) {
				sa_share_free(old_share);
				ret = SA_NO_MEMORY;
				(void) fprintf(stderr, "share: %s\n",
				    sa_strerror(ret));
				goto done;
			}
		}

		sa_share_free(new_share);
		new_share = old_share;
		new = B_FALSE;
	}

	ret = sa_share_validate(new_share, new, errbuf, sizeof (errbuf));
	if (ret != SA_OK) {
		(void) fprintf(stderr, "share: %s\n", errbuf);
		goto done;
	}

	/*
	 * now a valid share
	 * save share, this will also publish
	 */
	if ((ret = sa_share_write(new_share)) != SA_OK) {
		(void) fprintf(stderr, "share: %s %s:%s: %s\n",
		    gettext("error writing share"),
		    sharename, path, sa_strerror(ret));
		goto done;
	}

	if ((sa_sharing_enabled(path) & proto) == 0) {
		ret = sa_sharing_set_prop(path, proto, "on");
		if (ret != SA_OK) {
			(void) fprintf(stderr, "share: %s%s%s %s:%s: %s\n",
			    gettext("error setting "),
			    proto == SA_PROT_NFS ? "sharenfs" : "sharesmb",
			    gettext(" property"),
			    sharename, path, sa_strerror(ret));
		}
	}
done:

	if (name_alloc)
		free(sharename);
	if (utf8_name_alloc)
		free(utf8_name);
	if (utf8_desc)
		free(utf8_desc);
	if (new_share != NULL)
		sa_share_free(new_share);

	return (ret);
}

static void
printshare(nvlist_t *share, sa_proto_t proto, boolean_t printproto)
{
	char *utf8_name;
	char *disp_name = NULL;
	char *path;
	char *utf8_desc;
	char *disp_desc = NULL;
	char *opts = NULL;
	sa_proto_t p;
	nvlist_t *protocol;
	int ret;

	utf8_name = sa_share_get_name(share);
	if (utf8_name) {
		if (sa_utf8_to_locale(utf8_name, &disp_name) != SA_OK)
			disp_name = strdup(utf8_name);
	}

	path = sa_share_get_path(share);

	utf8_desc = sa_share_get_desc(share);
	if (utf8_desc) {
		if (sa_utf8_to_locale(utf8_desc, &disp_desc) != SA_OK)
			disp_desc = strdup(utf8_desc);
	}

	for (p = sa_proto_first(); p != SA_PROT_NONE; p = sa_proto_next(p)) {
		if (!(p & proto))
			continue;
		protocol = sa_share_get_proto(share, p);
		if (protocol == NULL)
			continue;
		ret = sa_share_format_props(protocol, p, &opts);
		if (ret != SA_OK)
			continue;
		if (printproto) {
			(void) printf("%s\t%s\t%s\t%s\t%s\n",
			    disp_name == NULL ? "-" : disp_name,
			    path == NULL ? "" : path,
			    sa_proto_to_val(p),
			    ((opts == NULL) || (*opts == '\0'))
			    ? "-" : opts,
			    disp_desc == NULL ? "" : disp_desc);
		} else {
			(void) printf("%s\t%s\t%s\t%s\n",
			    disp_name == NULL ? "-" : disp_name,
			    path == NULL ? "" : path,
			    ((opts == NULL) || (*opts == '\0'))
			    ? "-" : opts,
			    disp_desc == NULL ? "" : disp_desc);
		}
		free(opts);
		opts = NULL;
	}

	if (disp_name)
		free(disp_name);
	if (disp_desc)
		free(disp_desc);
}

static int
list(char *protocol, boolean_t allshares, boolean_t active_only)
{
	int ret = SA_OK;
	sa_proto_t proto, status;
	void *hdl;
	nvlist_t *share = NULL;

	/* default to ALL shares */
	if (allshares || protocol == NULL || strcmp(protocol, "all") == 0)
		proto = SA_PROT_ALL;
	else
		proto = sa_val_to_proto(protocol);
	/*
	 * If active_only flag is set, get from sharetab else
	 * get from repository.
	 */
	if (active_only) {
		/*
		 * return list of active shares from sharetab
		 */
		if (sa_share_find_init(NULL, proto, &hdl) == SA_OK) {
			while (sa_share_find_next(hdl, &share) == SA_OK) {
				status = sa_share_get_status(share);
				printshare(share, proto & status,
				    proto == SA_PROT_ALL);
				sa_share_free(share);
			}
			sa_share_find_fini(hdl);
		}
	} else {
		/*
		 * return all configured shares for this protocol
		 */
		if (sa_share_read_init(NULL, &hdl) == SA_OK) {
			while (sa_share_read_next(hdl, &share) == SA_OK) {
				if (proto != SA_PROT_ALL &&
				    sa_share_get_proto(share, proto) == NULL)
					continue;
				printshare(share, proto, proto == SA_PROT_ALL);
				sa_share_free(share);
			}
			sa_share_read_fini(hdl);
		}
	}

	return (ret);
}

static int
upgrade(char *group, boolean_t force_upgrade)
{
	int ret;

	ret = sa_upgrade_smf_share_group(group, force_upgrade);
	if (ret != SA_OK)
		(void) fprintf(stderr, "share: %s: %s\n",
		    gettext("failed to upgrade group"), group);

	return (ret);
}

static char *
get_default_fstype()
{
	FILE *fp;
	char buff[128];
	int i;

	fp = fopen(DEFAULT_FSTYPE, "r");
	if (fp == NULL)
		return (strdup("nfs"));
	i = fscanf(fp, "%127s", buff);
	(void) fclose(fp);
	if (i == 1)
		return (strdup(buff));
	return (strdup("nfs"));
}

#ifdef DEBUG
static void
do_atexit(void)
{
	/*
	 * The 'SHARE_ABORT' environment variable causes us to dump core on exit
	 * for the purposes of running ::findleaks.
	 */
	if (getenv("SHARE_ABORT") != NULL) {
		(void) printf("dumping core by request\n");
		abort();
	}
}
#endif
