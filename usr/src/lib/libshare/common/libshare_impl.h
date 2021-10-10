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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * basic declarations for implementation of the share management
 * libraries.
 */

#ifndef _LIBSHARE_IMPL_H
#define	_LIBSHARE_IMPL_H

#include <dirent.h>
#include <libshare.h>
#include <sharefs/share.h>
#include <libscf.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	NDEBUG
#define	sa_verify(x)	((void)(x))
#else
#define	sa_verify(x)	assert(x)
#endif

#ifdef	VERIFY
#undef	VERIFY
#endif
#define	VERIFY	sa_verify

#define	SA_LIBSHARE_VERSION	200

/* directory to find plugin modules in */

#define	SA_PLUGIN_ROOT_PROTO	"/lib/share/proto"
#define	SA_PLUGIN_ROOT_FS	"/lib/share/fs"
#define	SA_PLUGIN_ROOT_CACHE	"/lib/share/cache"

#define	PLUGIN_LIB_PREFIX	"libshare_"

#define	MAXISALEN	257	/* based on sysinfo(2) man page */

#define	SA_MAX_SHARE_NAME	80	/* Maximum length of share name */
#define	MAX_MANGLE_NUMBER	10000

/* SMF information for legacy shares */

#define	LEGACY_FMRI	"svc:/network/shares"
#define	LEGACY_PG	"shares"

/*
 * handle used by share read routines
 */
typedef struct sa_read_hdl_s {
	sa_proto_t	srh_proto;	/* protocol to find */
	char		*srh_mntpnt;	/* active fs mntpnt */
	char		*srh_dataset;   /* active ds name */
	FILE		*srh_mnttab_fp; /* file pointer for mnttab */
	DIR		*srh_dirp;	/* dir ptr for active share dir */
	scf_iter_t	*srh_smf_iter;
	scf_value_t	*srh_smf_value;
	scf_property_t	*srh_smf_prop;
	scf_propertygroup_t *srh_smf_pg;
} sa_read_hdl_t;

/*
 * file system plugin types
 */
typedef enum {
	SA_FS_NONE = 0,
	SA_FS_ZFS,
	SA_FS_LEGACY,
	SA_FS_NOTFOUND,
	SA_FS_ALL
} sa_fs_t;

/*
 * libshare plugin library types
 */
typedef enum {
	SA_PLUGIN_FS = 0,
	SA_PLUGIN_PROTO,
	SA_PLUGIN_CACHE
} sa_plugin_type_t;

/*
 * plugin library common ops
 */
typedef struct {
	sa_plugin_type_t pi_ptype;	/* plugin type */
	uint32_t	pi_type;	/* instance type */
	char		*pi_name;	/* instance name */
	uint32_t	pi_version;	/* plugin version */
	uint32_t	pi_flags;	/* Informational flags */
	int		(*pi_init)(void);
	void		(*pi_fini)(void);
} sa_plugin_ops_t;

/*
 * Each plugin library is maintained in a list
 * of sa_plugin_t structures. There is a list
 * for each plugin type.
 */
typedef struct sa_plugin_s {
	struct sa_plugin_s	*pi_next;
	sa_plugin_ops_t		*pi_ops;
	void			*pi_hdl;
} sa_plugin_t;

/*
 * Specific plugin library type ops tables
 * Each contains a sa_plugin_ops_t as first member.
 */

/*
 * File system plugin type ops table
 */
typedef	struct {
	sa_plugin_ops_t	saf_hdr;
	int		(*saf_share_write)(nvlist_t *);
	int		(*saf_share_read)(const char *, const char *,
	    nvlist_t **);
	int		(*saf_share_read_init)(sa_read_hdl_t *);
	int		(*saf_share_read_next)(sa_read_hdl_t *, nvlist_t **);
	int		(*saf_share_read_fini)(sa_read_hdl_t *);
	int		(*saf_share_remove)(const char *, const char *);
	int		(*saf_share_get_acl)(const char *, const char *,
	    acl_t **);
	int		(*saf_share_set_acl)(const char *, const char *,
	    acl_t *);
	int		(*saf_get_mntpnt_for_path)(const char *, char *, size_t,
	    char *, size_t, char *, size_t);
	void		(*saf_mnttab_cache)(boolean_t);
	int		(*saf_sharing_enabled)(const char *, sa_proto_t *);
	int		(*saf_sharing_get_prop)(const char *, sa_proto_t,
	    char **);
	int		(*saf_sharing_set_prop)(const char *, sa_proto_t,
	    char *);
	int		(*saf_is_legacy)(const char *, boolean_t *);
	int		(*saf_is_zoned)(const char *, boolean_t *);
} sa_fs_ops_t;

/*
 * Protocol plugin type ops table
 */
typedef	struct {
	sa_plugin_ops_t	sap_hdr;
	int		(*sap_share_parse)(const char *, int, nvlist_t **,
	    char *, size_t);
	int		(*sap_share_merge)(nvlist_t *, nvlist_t *, int,
	    char *, size_t);
	int		(*sap_share_set_def_proto)(nvlist_t *);
	int		(*sap_share_validate_name)(const char *, boolean_t,
	    char *, size_t);
	int		(*sap_share_validate)(nvlist_t *, boolean_t, char *,
	    size_t);
	int		(*sap_share_publish)(nvlist_t *, int);
	int		(*sap_share_unpublish)(nvlist_t *, int);
	int		(*sap_share_unpublish_byname)(const char *,
	    const char *, int);
	int		(*sap_share_publish_admin)(const char *);
	int		(*sap_fs_publish)(nvlist_t *, int);
	int		(*sap_fs_unpublish)(nvlist_t *, int);
	int		(*sap_share_prop_format)(nvlist_t *, char **);

	/* sharectl protocol property management routines */
	int		(*sap_proto_get_features)(uint64_t *);
	int		(*sap_proto_get_proplist)(nvlist_t **);
	int		(*sap_proto_get_status)(char **);
	int		(*sap_proto_get_property)(const char *, const char *,
	    char **);
	int		(*sap_proto_set_property)(const char *, const char *,
	    const char *);
	int		(*sap_proto_rem_section)(const char *);
} sa_proto_ops_t;

/*
 * Share cache plugin type ops table
 */
typedef	struct {
	sa_plugin_ops_t	sac_hdr;
	int		(*sac_init)(void *);
	void		(*sac_fini)(void *);
	int		(*sac_share_add)(nvlist_t *);
	int		(*sac_share_update)(nvlist_t *);
	int		(*sac_share_remove)(const char *);
	int		(*sac_flush)(void);
	int		(*sac_share_lookup)(const char *, const char *,
	    sa_proto_t, nvlist_t **);
	int		(*sac_share_find_init)(const char *, sa_proto_t,
	    void **);
	int		(*sac_share_find_next)(void *, nvlist_t **);
	int		(*sac_share_find_fini)(void *);
	int		(*sac_share_ds_find_init)(const char *, sa_proto_t,
	    void **);
	int		(*sac_share_ds_find_get)(void *, nvlist_t **);
	int		(*sac_share_ds_find_fini)(void *);

	int		(*sac_share_validate_name)(const char *,
	    boolean_t);
} sa_cache_ops_t;

/*
 * libshare_plugin.c
 */
extern void saplugin_unload_all(void);
extern sa_plugin_ops_t *saplugin_find_ops(sa_plugin_type_t, uint32_t);
extern sa_plugin_ops_t *saplugin_next_ops(sa_plugin_type_t, sa_plugin_ops_t *);
extern int saplugin_get_protos(sa_proto_t **);

/*
 * libshare_util.c
 */
extern int sa_fstype(const char *, char **);
extern void sa_free_fstype(char *);
extern char *sa_fixup_path(char *);
extern int sa_resolve_share_name_conflict(nvlist_t *, nvlist_t **, int);
extern int sa_share_from_path(const char *, nvlist_t **, boolean_t *);
extern char *sa_strchr_escape(char *, char);
extern char *sa_strip_escape(char *);
extern boolean_t sa_fstype_is_shareable(const char *);
extern boolean_t sa_mntpnt_in_current_zone(char *, char *);

/*
 * libshare_cache.c
 */

extern int sacache_init(void *);
extern void sacache_fini(void *);
extern int sacache_share_add(nvlist_t *);
extern int sacache_share_update(nvlist_t *);
extern int sacache_share_remove(const char *);
extern int sacache_flush(void);
extern int sacache_share_lookup(const char *, const char *, sa_proto_t,
    nvlist_t **);
extern int sacache_share_find_init(const char *, sa_proto_t, void **);
extern int sacache_share_find_next(void *, nvlist_t **);
extern int sacache_share_find_fini(void *);
extern int sacache_share_ds_find_init(const char *, sa_proto_t, void **);
extern int sacache_share_ds_find_get(void *, nvlist_t **);
extern int sacache_share_ds_find_fini(void *);
extern int sacache_share_validate_name(const char *, boolean_t);

/*
 * libshare_proto.c
 */
extern int saproto_init(void);
extern int saproto_fini(void);
extern int saproto_share_parse(sa_proto_t, const char *, int, nvlist_t **,
    char *, size_t);
extern int saproto_share_merge(sa_proto_t, nvlist_t *, nvlist_t *, int,
    char *, size_t);
extern int saproto_share_set_def_proto(sa_proto_t, nvlist_t *);
extern int saproto_share_validate_name(const char *, sa_proto_t, boolean_t,
    char *, size_t);
extern int saproto_share_validate(nvlist_t *, sa_proto_t, boolean_t, char *,
    size_t);
extern int saproto_share_format_props(nvlist_t *, sa_proto_t, char **);

extern int saproto_share_publish(nvlist_t *, sa_proto_t, int);
extern int saproto_share_unpublish(nvlist_t *, sa_proto_t, int);
extern int saproto_share_unpublish_byname(const char *, const char *,
    sa_proto_t, int);
extern int saproto_share_publish_admin(const char *, sa_proto_t);
extern int saproto_fs_publish(nvlist_t *, sa_proto_t, int);
extern int saproto_fs_unpublish(nvlist_t *, sa_proto_t, int);

extern int sa_proto_count(void);
extern sa_proto_t sa_proto_get_type(int);
extern char *sa_proto_get_name(int);

/*
 * libshare_fs.c
 */

extern int safs_init(void);
extern int safs_fini(void);
extern int safs_share_write(nvlist_t *);
extern int safs_share_read(const char *, const char *, nvlist_t **);
extern int safs_share_read_init(sa_read_hdl_t *);
extern int safs_share_read_next(sa_read_hdl_t *, nvlist_t **);
extern int safs_share_read_fini(sa_read_hdl_t *);
extern int safs_share_remove(const char *, const char *);
extern boolean_t safs_is_zoned(const char *);
extern int safs_share_get_acl(const char *, const char *, acl_t **);
extern int safs_share_set_acl(const char *, const char *, acl_t *);
extern int safs_get_mntpnt_for_path(const char *, char *, size_t, char *,
    size_t, char *, size_t);
extern void safs_mnttab_cache(boolean_t);
extern int safs_sharing_get_prop(const char *, sa_proto_t, char **);
extern int safs_sharing_set_prop(const char *, sa_proto_t, char *);
extern sa_proto_t safs_sharing_enabled(const char *);

#ifdef	__cplusplus
}
#endif

#endif /* _LIBSHARE_IMPL_H */
