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

#ifndef	_LIBBE_PRIV_H
#define	_LIBBE_PRIV_H

#include <libnvpair.h>
#include <libzfs.h>
#include <instzones_api.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	ARCH_LENGTH		MAXNAMELEN
#define	BE_AUTO_NAME_MAX_TRY	3
#define	BE_AUTO_NAME_DELIM	'-'
#define	BE_CONTAINER_DS_NAME	"ROOT"
#define	BE_ZONE_RPOOL_NAME	"rpool"
#define	BE_POLICY_PROPERTY	"org.opensolaris.libbe:policy"
#define	BE_UUID_PROPERTY	"org.opensolaris.libbe:uuid"
#define	BE_PLCY_STATIC		"static"
#define	BE_PLCY_VOLATILE	"volatile"

#define	ZFS_CLOSE(_zhp) \
	if (_zhp) { \
		zfs_close(_zhp); \
		_zhp = NULL; \
	}

#define	BE_ZONE_PARENTBE_PROPERTY	"org.opensolaris.libbe:parentbe"
#define	BE_ZONE_ACTIVE_PROPERTY		"org.opensolaris.libbe:active"
#define	BE_ZONE_SUPPORTED_BRANDS	"solaris labeled"
#define	BE_ZONE_SUPPORTED_BRANDS_DELIM	" "

#define	BE_NBE_HANDLE_PROPERTY	"com.oracle.libbe:nbe_handle"

/* Maximum length for the BE name. */
#define	BE_NAME_MAX_LEN		64

#define	MAX(a, b) ((a) > (b) ? (a) : (b))
#define	MIN(a, b) ((a) < (b) ? (a) : (b))

typedef struct be_transaction_data {
	char		*obe_name;	/* Original BE name */
	char		*obe_root_ds;	/* Original BE root dataset */
	char		*obe_zpool;	/* Original BE pool */
	char		*obe_snap_name;	/* Original BE snapshot name */
	char		*obe_altroot;	/* Original BE altroot */
	char 		*nbe_name;	/* New BE name */
	char		*nbe_root_ds;	/* New BE root dataset */
	char		*nbe_zpool;	/* New BE pool */
	char		*nbe_desc;	/* New BE description */
	nvlist_t	*nbe_zfs_props;	/* New BE dataset properties */
	char		*policy;	/* BE policy type */
	char		*zoneroot;	/* zoneroot, relative to altroots */
} be_transaction_data_t;

typedef struct be_mount_data {
	char		*altroot;	/* Location of where to mount BE */
	boolean_t	shared_fs;	/* Mount shared file sytsems */
	boolean_t	shared_rw;	/* Mount shared file systems rw */
} be_mount_data_t;

typedef struct be_unmount_data {
	char		*altroot;	/* Location of where BE is mounted */
	boolean_t	force;		/* Forcibly unmount */
} be_unmount_data_t;

typedef struct be_destroy_data {
	boolean_t	destroy_snaps;	/* Destroy snapshots of BE */
	boolean_t	force_unmount;	/* Forcibly unmount BE if mounted */
	uuid_t		gz_be_uuid;	/* UUID of the global zone BE */
} be_destroy_data_t;

typedef struct be_demote_data {
	zfs_handle_t	*clone_zhp;	/* clone dataset to promote */
	time_t		origin_creation; /* snapshot creation time of clone */
	const char	*snapshot;	/* snapshot of dataset being demoted */
	boolean_t	find_in_BE;	/* flag noting to find clone in BE */
} be_demote_data_t;

typedef struct be_fs_list_data {
	char		*altroot;
	char		**fs_list;
	int		fs_num;
} be_fs_list_data_t;

typedef struct be_plcy_list {
	char			*be_plcy_name;
	int			be_num_max;
	int			be_num_min;
	time_t			be_age_max;
	int			be_usage_pcnt;
	struct be_plcy_list	*be_next_plcy;
} be_plcy_list_t;

typedef struct rpool_data {
	char		**pool_list;
	int		pool_count;
} rpool_data_t;

/* Library globals */
extern libzfs_handle_t *g_zfs;
extern boolean_t do_print;

/* be_create.c */
int be_set_uuid(char *);
int be_get_uuid(const char *, uuid_t *);
int be_find_zpool_by_bename(char *, char **, int *, char **);

/* be_list.c */
int _be_list(char *, be_node_list_t **);
int be_get_zone_be_list(char *, char *, be_node_list_t **);

/* be_mount.c */
int _be_mount(char *, char *, char **, int);
int _be_unmount(char *, char *, int);
int be_tmp_mount_ds(zfs_handle_t *);
int be_mount_zone_root(zfs_handle_t *, be_mount_data_t *);
int be_unmount_zone_root(zfs_handle_t *, be_unmount_data_t *);
int be_get_legacy_fs(char *, char *, char *, char *, be_fs_list_data_t *);
void be_free_fs_list(be_fs_list_data_t *);
char *be_get_ds_from_dir(char *);
int be_make_tmp_mountpoint(char **);

/* be_snapshot.c */
int _be_create_snapshot(char *, char *, char **, char *);
int _be_destroy_snapshot(char *, char *);

/* be_utils.c */
boolean_t be_zfs_init(void);
void be_zfs_fini(void);
void be_make_root_ds(const char *, const char *, char *, int);
void be_make_nested_container_ds(const char *, char *, int);
void be_make_container_ds(const char *, char *, int);
char *be_make_name_from_ds(const char *, char *);
int be_run_cmd(char *, int *, char *, int, char *, int);
int be_update_vfstab(char *, char *, char *, be_fs_list_data_t *, char *);
int be_maxsize_avail(zfs_handle_t *, uint64_t *);
char *be_auto_snap_name(void);
char *be_auto_be_name(char *);
char *be_auto_zone_be_name(char *, char *);
char *be_default_policy(void);
boolean_t valid_be_policy(char *);
boolean_t be_valid_auto_snap_name(char *);
boolean_t be_valid_be_name(const char *);
void be_print_err(char *, ...);
int be_find_current_be(be_transaction_data_t *);
int zfs_err_to_be_err(libzfs_handle_t *);
int errno_to_be_err(int);
int be_check_rozr(void);
int be_find_root_pools(char ***, int *);

/* be_activate.c */
int _be_activate(char *);
int _be_activate_bh(char *, char *, char *);
int be_activate_current_be(void);
boolean_t be_is_active_on_boot(char *);
int _be_get_boot_device_list(char *, char ***, int *);

/* be_zones.c */
void be_make_zoneroot(char *, char *, int);
int be_find_active_zone_root(zfs_handle_t *, char *, char *, int);
int be_find_mounted_zone_root(char *, char *, char *, int);
boolean_t be_zone_supported(char *);
zoneBrandList_t *be_get_supported_brandlist(void);
int be_zone_get_parent_id(const char *, uuid_t *);
int be_zone_set_parent_id(const char *, uuid_t);
int be_zone_toggle_active(char *root_ds);
int be_zone_get_zpool_analog(char *p_analog);
boolean_t be_zone_is_bootable(char *root_ds);
boolean_t be_zone_is_active(zfs_handle_t *);

/* callback functions */
int be_exists_callback(zpool_handle_t *, void *);
int be_find_zpool_callback(zpool_handle_t *, void *);
int be_zpool_find_current_be_callback(zpool_handle_t *, void *);
int be_zfs_find_current_be_callback(zfs_handle_t *, void *);
int be_check_be_roots_callback(zpool_handle_t *, void *);

#ifdef __cplusplus
}
#endif

#endif	/* _LIBBE_PRIV_H */
