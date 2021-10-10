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

#ifndef _LIBZONECFG_H
#define	_LIBZONECFG_H

/*
 * INTERFACES DEFINED IN THIS FILE DO NOT CONSTITUTE A PUBLIC INTERFACE.
 *
 * Do not consume these interfaces; your program will break in the future
 * (even in a patch) if you do.
 */

/*
 * Zone configuration header file.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* sys/socket.h is required by net/if.h, which has a constant needed here */
#include <sys/param.h>
#include <sys/fstyp.h>
#include <sys/mount.h>
#include <priv.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <net/if.h>
#include <stdio.h>
#include <rctl.h>
#include <zone.h>
#include <libbrand.h>
#include <sys/uuid.h>
#include <libuutil.h>
#include <sys/mnttab.h>
#include <limits.h>
#include <utmpx.h>
#include <sys/mac.h>
#include <sys/mac_flow.h>

#define	ZONE_ID_UNDEFINED	-1

#define	Z_OK			0
#define	Z_EMPTY_DOCUMENT	1	/* XML doc root element is null */
#define	Z_WRONG_DOC_TYPE	2	/* top-level XML doc element != zone */
#define	Z_BAD_PROPERTY		3	/* libxml-level property problem */
#define	Z_TEMP_FILE		4	/* problem creating temporary file */
#define	Z_SAVING_FILE		5	/* libxml error saving or validating */
#define	Z_NO_ENTRY		6	/* no such entry */
#define	Z_BOGUS_ZONE_NAME	7	/* illegal zone name */
#define	Z_REQD_RESOURCE_MISSING	8	/* required resource missing */
#define	Z_REQD_PROPERTY_MISSING	9	/* required property missing */
#define	Z_BAD_HANDLE		10	/* bad document handle */
#define	Z_NOMEM			11	/* out of memory (like ENOMEM) */
#define	Z_INVAL			12	/* invalid argument (like EINVAL) */
#define	Z_ACCES			13	/* permission denied (like EACCES) */
#define	Z_TOO_BIG		14	/* string won't fit in char array */
#define	Z_MISC_FS		15	/* miscellaneous file-system error */
#define	Z_NO_ZONE		16	/* no such zone */
#define	Z_NO_RESOURCE_TYPE	17	/* no/wrong resource type */
#define	Z_NO_RESOURCE_ID	18	/* no/wrong resource id */
#define	Z_NO_PROPERTY_TYPE	19	/* no/wrong property type */
#define	Z_NO_PROPERTY_ID	20	/* no/wrong property id */
#define	Z_BAD_ZONE_STATE	21	/* zone state invalid for given task */
#define	Z_INVALID_DOCUMENT	22	/* libxml can't validate against DTD */
#define	Z_NAME_IN_USE		23	/* zone name already in use (rename) */
#define	Z_NO_SUCH_ID		24	/* delete_index: no old ID */
#define	Z_UPDATING_INDEX	25	/* add/modify/delete_index problem */
#define	Z_LOCKING_FILE		26	/* problem locking index file */
#define	Z_UNLOCKING_FILE	27	/* problem unlocking index file */
#define	Z_SYSTEM		28	/* consult errno instead */
#define	Z_INSUFFICIENT_SPEC	29	/* resource insufficiently specified */
#define	Z_RESOLVED_PATH		34	/* resolved path mismatch */
#define	Z_IPV6_ADDR_PREFIX_LEN	35	/* IPv6 address prefix length needed */
#define	Z_BOGUS_ADDRESS		36	/* not IPv[4|6] address or host name */
#define	Z_PRIV_PROHIBITED	37	/* specified privilege is prohibited */
#define	Z_PRIV_REQUIRED		38	/* required privilege is missing */
#define	Z_PRIV_UNKNOWN		39	/* specified privilege is unknown */
#define	Z_BRAND_ERROR		40	/* brand-specific error */
#define	Z_INCOMPATIBLE		41	/* incompatible settings */
#define	Z_ALIAS_DISALLOW	42	/* rctl alias disallowed */
#define	Z_CLEAR_DISALLOW	43	/* clear property disallowed */
#define	Z_POOL			44	/* generic libpool error */
#define	Z_POOLS_NOT_ACTIVE	45	/* pool service not enabled */
#define	Z_POOL_ENABLE		46	/* pools enable failed */
#define	Z_NO_POOL		47	/* no such pool configured */
#define	Z_POOL_CREATE		48	/* pool create failed */
#define	Z_POOL_BIND		49	/* pool bind failed */
#define	Z_INVALID_PROPERTY	50	/* invalid property value */
#define	Z_POSSIBLE_CONFLICT	51	/* possible conflicts in /dev/zvol */

/*
 * Warning: these are shared with the admin/install consolidation.
 * Do not insert states between any of the currently defined states,
 * and any new states must be evaluated for impact on range comparisons.
 */
#define	ZONE_STATE_CONFIGURED		0
#define	ZONE_STATE_INCOMPLETE		1
#define	ZONE_STATE_INSTALLED		2
#define	ZONE_STATE_READY		3
#define	ZONE_STATE_RUNNING		4
#define	ZONE_STATE_SHUTTING_DOWN	5
#define	ZONE_STATE_DOWN			6
#define	ZONE_STATE_MOUNTED		7

#define	ZONE_STATE_MAXSTRLEN	14

#define	LIBZONECFG_PATH		"libzonecfg.so.1"

#define	ZONE_CONFIG_ROOT	"/etc/zones"
#define	ZONE_INDEX_FILE		ZONE_CONFIG_ROOT "/index"

#define	MAXUSERNAME		(sizeof (((struct utmpx *)0)->ut_name))
#define	MAXAUTHS		4096
#define	ZONE_MGMT_PROF		"Zone Management"

/* Owner, group, and mode (defined by packaging) for the config directory */
#define	ZONE_CONFIG_UID		0		/* root */
#define	ZONE_CONFIG_GID		3		/* sys */
#define	ZONE_CONFIG_MODE	0755

/* Owner, group, and mode (defined by packaging) for the index file */
#define	ZONE_INDEX_UID		0		/* root */
#define	ZONE_INDEX_GID		3		/* sys */
#define	ZONE_INDEX_MODE		0644

/* The maximum length of the VERSION string in the pkginfo(4) file. */
#define	ZONE_PKG_VERSMAX	256

/*
 * Shortened alias names for the zones rctls.
 */
#define	ALIAS_MAXLWPS		"max-lwps"
#define	ALIAS_MAXSHMMEM		"max-shm-memory"
#define	ALIAS_MAXSHMIDS		"max-shm-ids"
#define	ALIAS_MAXMSGIDS		"max-msg-ids"
#define	ALIAS_MAXSEMIDS		"max-sem-ids"
#define	ALIAS_MAXLOCKEDMEM	"locked"
#define	ALIAS_MAXSWAP		"swap"
#define	ALIAS_SHARES		"cpu-shares"
#define	ALIAS_CPUCAP		"cpu-cap"
#define	ALIAS_MAXPROCS		"max-processes"

/* Default name for zone detached manifest */
#define	ZONE_DETACHED	"SUNWdetached.xml"

/*
 * Bit flag definitions for passing into libzonecfg functions.
 */
#define	ZONE_DRY_RUN		0x01

#define	DEVANN_LEN		128
/* null-terminated max size of allow-partition=... */
#define	DEVANN_PROPVAL_LEN	(sizeof (ZONE_ALLOW_PARTITION) + DEVANN_LEN + 1)

/* The empty string and "none" are equivalent and indicate a r/w zone. */
#define	ZONECFG_READ_WRITE_PROFNAME(prof)	\
	((prof)[0] == '\0' || strcmp((prof), "none") == 0)

/*
 * Macros representing max. sizes for net and anet resource properties
 */

/*
 * Our limit of 50 allowed-addresses should be more than enough. Generally,
 * users may need at most two allowed-addresses. Same for default routers.
 */
#define	ALLOWED_ADDRS_BUFSZ	(50 * INET6_ADDRSTRLEN)
#define	DEFROUTERS_BUFSZ	ALLOWED_ADDRS_BUFSZ

/* Allow at the most 20 allowed-dhcp-cids. */
#define	ALLOWED_DHCP_CIDS_BUFSZ	(20 * MPT_MAXCIDLEN)

/*
 * Today we have 4 protection. Our limit of 256 is more than
 * enough to handle them.
 */
#define	MAX_LINK_PROTECTIONS	256

/*
 * Properties such priority, vlan-id, mtu, maxbw, rings are eventually
 * stored as uint64_t.
 */
#define	MAX_NET_UINT64_STR	23

/*
 * The integer field expresses the current values on a get.
 * On a put, it represents the new values if >= 0 or "don't change" if < 0.
 */
struct zoneent {
	char	zone_name[ZONENAME_MAX];	/* name of the zone */
	int	zone_state;	/* configured | incomplete | installed */
	char	zone_path[MAXPATHLEN];		/* path to zone storage */
	uuid_t	zone_uuid;			/* unique ID for zone */
	char	zone_newname[ZONENAME_MAX];	/* for doing renames */
};

typedef struct zone_dochandle *zone_dochandle_t;	/* opaque handle */

typedef uint_t zone_state_t;

typedef struct zone_fsopt {
	struct zone_fsopt *zone_fsopt_next;
	char		   zone_fsopt_opt[MAX_MNTOPT_STR];
} zone_fsopt_t;

struct zone_fstab {
	char		zone_fs_special[MAXPATHLEN]; 	/* special file */
	char		zone_fs_dir[MAXPATHLEN];	/* mount point */
	char		zone_fs_type[FSTYPSZ];		/* e.g. ufs */
	zone_fsopt_t   *zone_fs_options;		/* mount options */
	char		zone_fs_raw[MAXPATHLEN];	/* device to fsck */
};

struct zone_nettab {
	char	zone_net_address[INET6_ADDRSTRLEN]; /* shared-ip only */
	char	zone_net_allowed_addr[ALLOWED_ADDRS_BUFSZ]; /* excl-ip only */
	char	zone_net_physical[LIFNAMSIZ];
	char	zone_net_defrouter[DEFROUTERS_BUFSZ];
	boolean_t	zone_net_configure_allowed_addr;
};

struct zone_anettab {
	char	zone_anet_linkname[MAXLINKNAMELEN];
	char	zone_anet_lower_link[MAXLINKNAMELEN];
	char	zone_anet_allowed_addr[ALLOWED_ADDRS_BUFSZ];
	char	zone_anet_defrouter[DEFROUTERS_BUFSZ];
	boolean_t	zone_anet_configure_allowed_addr;
	char	zone_anet_allowed_dhcp_cids[ALLOWED_DHCP_CIDS_BUFSZ];
	char	zone_anet_link_protection[MAX_LINK_PROTECTIONS];
	char	zone_anet_auto_mac_addr[MAXMACADDRLEN];
	char	zone_anet_mac_addr[MAXMACADDRLEN];
	char	zone_anet_mac_prefix[MAXMACADDRLEN];
	char	zone_anet_mac_slot[MAX_NET_UINT64_STR];
	char	zone_anet_vlan_id[MAX_NET_UINT64_STR];
	char	zone_anet_priority[MAX_NET_UINT64_STR];
	char	zone_anet_rxrings[MAX_NET_UINT64_STR];
	char	zone_anet_txrings[MAX_NET_UINT64_STR];
	char	zone_anet_maxbw[MAX_NET_UINT64_STR];
	char	zone_anet_mtu[MAX_NET_UINT64_STR];
	char	zone_anet_rxfanout[MAX_NET_UINT64_STR];
};

struct zone_devtab {
	char	zone_dev_match[MAXPATHLEN];
	char	zone_dev_partition[DEVANN_LEN];
	char	zone_dev_raw_io[DEVANN_LEN];
};

struct zone_rctlvaltab {
	char	zone_rctlval_priv[MAXNAMELEN];
	char	zone_rctlval_limit[MAXNAMELEN];
	char	zone_rctlval_action[MAXNAMELEN];
	struct zone_rctlvaltab *zone_rctlval_next;
};

struct zone_rctltab {
	char	zone_rctl_name[MAXNAMELEN];
	struct zone_rctlvaltab *zone_rctl_valptr;
};

struct zone_attrtab {
	char	zone_attr_name[MAXNAMELEN];
	char	zone_attr_type[MAXNAMELEN];
	char	zone_attr_value[2 * BUFSIZ];
};

struct zone_dstab {
	char	zone_dataset_name[MAXNAMELEN];
	char	zone_dataset_alias[MAXNAMELEN];
};

struct zone_psettab {
	char	zone_ncpu_min[MAXNAMELEN];
	char	zone_ncpu_max[MAXNAMELEN];
	char	zone_importance[MAXNAMELEN];
};

struct zone_mcaptab {
	char	zone_physmem_cap[MAXNAMELEN];
};

struct zone_pkgtab {
	char	zone_pkg_name[MAXNAMELEN];
	char	zone_pkg_version[ZONE_PKG_VERSMAX];
};

struct zone_patchtab {
	char	zone_patch_id[MAXNAMELEN];
};

struct zone_devpermtab {
	char	zone_devperm_name[MAXPATHLEN];
	uid_t	zone_devperm_uid;
	gid_t	zone_devperm_gid;
	mode_t	zone_devperm_mode;
	char	*zone_devperm_acl;
};

struct zone_admintab {
	char	zone_admin_user[MAXUSERNAME];
	char	zone_admin_auths[MAXAUTHS];
};

typedef struct zone_userauths {
	char			user[MAXUSERNAME];
	char			zonename[ZONENAME_MAX];
	struct zone_userauths	*next;
} zone_userauths_t;

typedef struct {
	uu_avl_node_t	zpe_entry;
	char		*zpe_name;
	char		*zpe_vers;
	uu_avl_t	*zpe_patches_avl;
} zone_pkg_entry_t;

typedef enum zone_iptype {
	ZS_SHARED,
	ZS_EXCLUSIVE
} zone_iptype_t;

typedef struct {
	char		*zmac_list[2];
	size_t		zmac_size[2];
} zone_maclist_t;

typedef enum zone_mactype {
	ZS_BLACK,
	ZS_WHITE
} zone_mactype_t;


/*
 * Basic configuration management routines.
 */
extern	zone_dochandle_t	zonecfg_init_handle(void);
extern	int	zonecfg_get_handle(const char *, zone_dochandle_t);
extern	int	zonecfg_get_snapshot_handle(const char *, zone_dochandle_t);
extern	int	zonecfg_get_template_handle(const char *, const char *,
    zone_dochandle_t);
extern	int	zonecfg_get_xml_handle(const char *, zone_dochandle_t);
extern	int	zonecfg_check_handle(zone_dochandle_t);
extern	void	zonecfg_fini_handle(zone_dochandle_t);
extern	int	zonecfg_destroy(const char *, boolean_t);
extern	int	zonecfg_destroy_snapshot(const char *);
extern	int	zonecfg_save(zone_dochandle_t);
extern	int	zonecfg_create_snapshot(const char *);
extern	char	*zonecfg_strerror(int);
extern	int	zonecfg_access(const char *, int);
extern	void	zonecfg_set_root(const char *);
extern	const char *zonecfg_get_root(void);
extern	int	zonecfg_simplify_path(const char *, char *, size_t);
extern	boolean_t zonecfg_in_alt_root(void);
extern	int	zonecfg_num_resources(zone_dochandle_t, char *);
extern	int	zonecfg_del_all_resources(zone_dochandle_t, char *);
extern	boolean_t zonecfg_valid_ncpus(char *, char *);
extern	boolean_t zonecfg_valid_importance(char *);
extern	int	zonecfg_str_to_bytes(char *, uint64_t *);
extern	boolean_t zonecfg_valid_memlimit(char *, uint64_t *);
extern	boolean_t zonecfg_valid_alias_limit(char *, char *, uint64_t *);

/*
 * Zone name, path to zone directory, autoboot setting, pool, boot
 * arguments, and scheduling-class.
 */
extern	int	zonecfg_validate_zonename(const char *);
extern	int	zonecfg_get_name(zone_dochandle_t, char *, size_t);
extern	int	zonecfg_set_name(zone_dochandle_t, char *);
extern	int	zonecfg_get_zonepath(zone_dochandle_t, char *, size_t);
extern	int	zonecfg_set_zonepath(zone_dochandle_t, char *);
extern	int	zonecfg_get_autoboot(zone_dochandle_t, boolean_t *);
extern	int	zonecfg_set_autoboot(zone_dochandle_t, boolean_t);
extern	boolean_t zonecfg_is_readonly(zone_dochandle_t);
extern	int	zonecfg_get_mac_profile(zone_dochandle_t, char *, size_t);
extern	int	zonecfg_set_mac_profile(zone_dochandle_t, char *);
extern	int	zonecfg_get_iptype(zone_dochandle_t, zone_iptype_t *);
extern	int	zonecfg_set_iptype(zone_dochandle_t, zone_iptype_t);
extern	int	zonecfg_get_pool(zone_dochandle_t, char *, size_t);
extern	int	zonecfg_set_pool(zone_dochandle_t, char *);
extern	int	zonecfg_get_bootargs(zone_dochandle_t, char *, size_t);
extern	int	zonecfg_set_bootargs(zone_dochandle_t, char *);
extern	int	zonecfg_get_sched_class(zone_dochandle_t, char *, size_t);
extern	int	zonecfg_set_sched(zone_dochandle_t, char *);
extern	int	zonecfg_get_dflt_sched_class(zone_dochandle_t, char *, int);

/*
 * Set/retrieve the brand for the zone
 */
extern	int	zonecfg_get_brand(zone_dochandle_t, char *, size_t);
extern	int	zonecfg_set_brand(zone_dochandle_t, char *);

/*
 * Filesystem configuration.
 */
extern	int	zonecfg_add_filesystem(zone_dochandle_t, struct zone_fstab *);
extern	int	zonecfg_delete_filesystem(zone_dochandle_t,
    struct zone_fstab *);
extern	int	zonecfg_modify_filesystem(zone_dochandle_t,
    struct zone_fstab *, struct zone_fstab *);
extern	int	zonecfg_lookup_filesystem(zone_dochandle_t,
    struct zone_fstab *);
extern	int	zonecfg_add_fs_option(struct zone_fstab *, char *);
extern	int	zonecfg_remove_fs_option(struct zone_fstab *, char *);
extern	void	zonecfg_free_fs_option_list(zone_fsopt_t *);
extern	int 	zonecfg_find_mounts(char *, int(*)(const struct mnttab *,
    void *), void *);

/*
 * Network interface configuration.
 */
extern	int	zonecfg_add_net(zone_dochandle_t, struct zone_nettab *);
extern	int	zonecfg_delete_net(zone_dochandle_t, struct zone_nettab *);
extern	int	zonecfg_modify_net(zone_dochandle_t, struct zone_nettab *,
    struct zone_nettab *);
extern	int	zonecfg_lookup_net(zone_dochandle_t, struct zone_nettab *);

/*
 * Automatic network interface configuration.
 */
extern	int	zonecfg_add_anet(zone_dochandle_t, struct zone_anettab *);
extern	int	zonecfg_delete_anet(zone_dochandle_t, struct zone_anettab *);
extern	int	zonecfg_modify_anet(zone_dochandle_t, struct zone_anettab *,
    struct zone_anettab *);
extern	int	zonecfg_lookup_anet(zone_dochandle_t, struct zone_anettab *);
extern	boolean_t zonecfg_lookup_linkname(zone_dochandle_t, const char *);
extern	int	zonecfg_csscmp(const char *, const char *);

/*
 * Hostid emulation configuration.
 */
extern	int	zonecfg_get_hostid(zone_dochandle_t, char *, size_t);
extern	int	zonecfg_set_hostid(zone_dochandle_t, const char *);

/*
 * Allowed FS mounts configuration.
 */
extern int	zonecfg_get_fs_allowed(zone_dochandle_t, char *, size_t);
extern int	zonecfg_set_fs_allowed(zone_dochandle_t, const char *);

/*
 * Device configuration and rule matching.
 */
extern	int	zonecfg_add_dev(zone_dochandle_t, struct zone_devtab *);
extern	int	zonecfg_delete_dev(zone_dochandle_t, struct zone_devtab *);
extern	int	zonecfg_modify_dev(zone_dochandle_t, struct zone_devtab *,
    struct zone_devtab *);
extern	int	zonecfg_lookup_dev(zone_dochandle_t, struct zone_devtab *);

/*
 * Resource control configuration.
 */
extern	int	zonecfg_add_rctl(zone_dochandle_t, struct zone_rctltab *);
extern	int	zonecfg_delete_rctl(zone_dochandle_t, struct zone_rctltab *);
extern	int	zonecfg_modify_rctl(zone_dochandle_t, struct zone_rctltab *,
    struct zone_rctltab *);
extern	int	zonecfg_lookup_rctl(zone_dochandle_t, struct zone_rctltab *);
extern	int	zonecfg_add_rctl_value(struct zone_rctltab *,
    struct zone_rctlvaltab *);
extern	int	zonecfg_remove_rctl_value(struct zone_rctltab *,
    struct zone_rctlvaltab *);
extern	void	zonecfg_free_rctl_value_list(struct zone_rctlvaltab *);
extern	boolean_t zonecfg_aliased_rctl_ok(zone_dochandle_t, char *);
extern	int	zonecfg_set_aliased_rctl(zone_dochandle_t, char *, uint64_t);
extern	int	zonecfg_get_aliased_rctl(zone_dochandle_t, char *, uint64_t *);
extern	int	zonecfg_rm_aliased_rctl(zone_dochandle_t, char *);
extern	int	zonecfg_apply_rctls(char *, zone_dochandle_t);

/*
 * Generic attribute configuration and type/value extraction.
 */
extern	int	zonecfg_add_attr(zone_dochandle_t, struct zone_attrtab *);
extern	int	zonecfg_delete_attr(zone_dochandle_t, struct zone_attrtab *);
extern	int	zonecfg_modify_attr(zone_dochandle_t, struct zone_attrtab *,
    struct zone_attrtab *);
extern	int	zonecfg_lookup_attr(zone_dochandle_t, struct zone_attrtab *);
extern	int	zonecfg_get_attr_boolean(const struct zone_attrtab *,
    boolean_t *);
extern	int	zonecfg_get_attr_int(const struct zone_attrtab *, int64_t *);
extern	int	zonecfg_get_attr_string(const struct zone_attrtab *, char *,
    size_t);
extern	int	zonecfg_get_attr_uint(const struct zone_attrtab *, uint64_t *);

/*
 * ZFS configuration.
 */
extern	int	zonecfg_add_ds(zone_dochandle_t, struct zone_dstab *);
extern	int	zonecfg_delete_ds(zone_dochandle_t, struct zone_dstab *);
extern	int	zonecfg_modify_ds(zone_dochandle_t, struct zone_dstab *,
    struct zone_dstab *);
extern	int	zonecfg_lookup_ds(zone_dochandle_t, struct zone_dstab *);

/*
 * cpu-set configuration.
 */
extern	int	zonecfg_add_pset(zone_dochandle_t, struct zone_psettab *);
extern	int	zonecfg_delete_pset(zone_dochandle_t);
extern	int	zonecfg_modify_pset(zone_dochandle_t, struct zone_psettab *);
extern	int	zonecfg_lookup_pset(zone_dochandle_t, struct zone_psettab *);

/*
 * mem-cap configuration.
 */
extern	int	zonecfg_delete_mcap(zone_dochandle_t);
extern	int	zonecfg_modify_mcap(zone_dochandle_t, struct zone_mcaptab *);
extern	int	zonecfg_lookup_mcap(zone_dochandle_t, struct zone_mcaptab *);

/*
 * Temporary pool support functions.
 */
extern	int	zonecfg_destroy_tmp_pool(char *, char *, int);
extern	int	zonecfg_bind_tmp_pool(zone_dochandle_t, zoneid_t, char *, int);
extern	int	zonecfg_bind_pool(zone_dochandle_t, zoneid_t, char *, int);
extern	boolean_t zonecfg_warn_poold(zone_dochandle_t);
extern	int	zonecfg_get_poolname(zone_dochandle_t, char *, char *, size_t);

/*
 * Miscellaneous utility functions.
 */
extern	int	zonecfg_enable_rcapd(char *, int);

/*
 * attach/detach support.
 */
extern	int	zonecfg_get_attach_handle(const char *, const char *,
    const char *, boolean_t, zone_dochandle_t);
extern	int	zonecfg_attach_manifest(int, zone_dochandle_t,
    zone_dochandle_t);
extern	int	zonecfg_detach_save(zone_dochandle_t, uint_t);
extern	void	zonecfg_rm_detached(zone_dochandle_t, boolean_t forced);
extern	void	zonecfg_set_swinv(zone_dochandle_t);
extern	int	zonecfg_add_pkg(zone_dochandle_t, char *, char *);
extern	int	zonecfg_add_patch(zone_dochandle_t, char *, void **);
extern	int	zonecfg_add_patch_obs(char *, void *);

/*
 * External zone verification support.
 */
extern	int	zonecfg_verify_save(zone_dochandle_t, char *);

/*
 * '*ent' iterator routines.
 */
extern	int	zonecfg_setfsent(zone_dochandle_t);
extern	int	zonecfg_getfsent(zone_dochandle_t, struct zone_fstab *);
extern	int	zonecfg_endfsent(zone_dochandle_t);
extern	int	zonecfg_setnetent(zone_dochandle_t);
extern	int	zonecfg_getnetent(zone_dochandle_t, struct zone_nettab *);
extern	int	zonecfg_endnetent(zone_dochandle_t);
extern	int	zonecfg_setanetent(zone_dochandle_t);
extern	int	zonecfg_getanetent(zone_dochandle_t, struct zone_anettab *);
extern	int	zonecfg_endanetent(zone_dochandle_t);
extern	int	zonecfg_setdevent(zone_dochandle_t);
extern	int	zonecfg_getdevent(zone_dochandle_t, struct zone_devtab *);
extern	int	zonecfg_enddevent(zone_dochandle_t);
extern	int	zonecfg_setattrent(zone_dochandle_t);
extern	int	zonecfg_getattrent(zone_dochandle_t, struct zone_attrtab *);
extern	int	zonecfg_endattrent(zone_dochandle_t);
extern	int	zonecfg_setrctlent(zone_dochandle_t);
extern	int	zonecfg_getrctlent(zone_dochandle_t, struct zone_rctltab *);
extern	int	zonecfg_endrctlent(zone_dochandle_t);
extern	int	zonecfg_setdsent(zone_dochandle_t);
extern	int	zonecfg_getdsent(zone_dochandle_t, struct zone_dstab *);
extern	int	zonecfg_enddsent(zone_dochandle_t);
extern	int	zonecfg_getpsetent(zone_dochandle_t, struct zone_psettab *);
extern	int	zonecfg_getmcapent(zone_dochandle_t, struct zone_mcaptab *);
extern	int	zonecfg_getpkgdata(zone_dochandle_t, uu_avl_pool_t *,
    uu_avl_t *);
extern	int	zonecfg_setadminent(zone_dochandle_t);
extern	int	zonecfg_getadminent(zone_dochandle_t, struct zone_admintab *);
extern	int	zonecfg_endadminent(zone_dochandle_t);

/*
 * Privilege-related functions.
 */
extern	int	zonecfg_default_privset(priv_set_t *, const char *);
extern	int	zonecfg_get_privset(zone_dochandle_t, priv_set_t *,
    char **);
extern	int	zonecfg_get_limitpriv(zone_dochandle_t, char **);
extern	int	zonecfg_set_limitpriv(zone_dochandle_t, char *);

/*
 * Higher-level routines.
 */
extern  int	zone_get_brand(char *, char *, size_t);
extern	int	zone_get_rootpath(char *, char *, size_t);
extern	int	zone_get_devroot(char *, char *, size_t);
extern	int	zone_get_zonepath(char *, char *, size_t);
extern	int	zone_get_state(char *, zone_state_t *);
extern	int	zone_set_state(char *, zone_state_t);
extern	char	*zone_state_str(zone_state_t);
extern	int	zonecfg_get_name_by_uuid(const uuid_t, char *, size_t);
extern	int	zonecfg_get_uuid(const char *, uuid_t);
extern	int	zonecfg_default_brand(char *, size_t);
extern	int	zonecfg_default_template(char *, size_t);

/*
 * Iterator for configured zones.
 */
extern FILE		*setzoneent(void);
extern char		*getzoneent(FILE *);
extern struct zoneent	*getzoneent_private(FILE *);
extern void		endzoneent(FILE *);

/*
 * File-system-related convenience functions.
 */
extern boolean_t zonecfg_valid_fs_type(const char *);

/*
 * Network-related convenience functions.
 */
extern boolean_t zonecfg_same_net_address(char *, char *);
extern int zonecfg_valid_net_address(char *, struct lifreq *);
extern boolean_t zonecfg_ifname_exists(sa_family_t, char *);

/*
 * Rctl-related common functions.
 */
extern boolean_t zonecfg_is_rctl(const char *);
extern boolean_t zonecfg_valid_rctlname(const char *);
extern boolean_t zonecfg_valid_rctlblk(const rctlblk_t *);
extern boolean_t zonecfg_valid_rctl(const char *, const rctlblk_t *);
extern int zonecfg_construct_rctlblk(const struct zone_rctlvaltab *,
    rctlblk_t *);

/*
 * Live Upgrade support functions.  Shared between ON and install gate.
 */
extern FILE *zonecfg_open_scratch(const char *, boolean_t);
extern int zonecfg_lock_scratch(FILE *);
extern void zonecfg_close_scratch(FILE *);
extern int zonecfg_get_scratch(FILE *, char *, size_t, char *, size_t, char *,
    size_t);
extern int zonecfg_find_scratch(FILE *, const char *, const char *, char *,
    size_t);
extern int zonecfg_reverse_scratch(FILE *, const char *, char *, size_t,
    char *, size_t);
extern int zonecfg_add_scratch(FILE *, const char *, const char *,
    const char *);
extern int zonecfg_delete_scratch(FILE *, const char *);
extern boolean_t zonecfg_is_scratch(const char *);

/*
 * zoneadmd support functions.  Shared between zoneadm and brand hook code.
 */
extern void zonecfg_init_lock_file(const char *, char **);
extern void zonecfg_release_lock_file(const char *, int);
extern int zonecfg_grab_lock_file(const char *, int *);
extern boolean_t zonecfg_lock_file_held(int *);
extern int zonecfg_ping_zoneadmd(const char *);
extern int zonecfg_call_zoneadmd(const char *, zone_cmd_arg_t *, char *);
extern int zonecfg_start_zoneadmd(const char *);
extern int zonecfg_insert_userauths(zone_dochandle_t, char *, char *);
extern int zonecfg_remove_userauths(zone_dochandle_t, char *, char *,
    boolean_t);
extern int zonecfg_add_admin(zone_dochandle_t, struct zone_admintab *,
    char *);
extern int zonecfg_delete_admin(zone_dochandle_t,
    struct zone_admintab *, char *);
extern int zonecfg_modify_admin(zone_dochandle_t, struct zone_admintab *,
    struct zone_admintab *, char *);
extern int zonecfg_delete_admins(zone_dochandle_t, char *);
extern int zonecfg_lookup_admin(zone_dochandle_t, struct zone_admintab *);
extern int zonecfg_authorize_users(zone_dochandle_t, char *);
extern int zonecfg_update_userauths(zone_dochandle_t, char *);
extern int zonecfg_deauthorize_user(zone_dochandle_t, char *, char *);
extern int zonecfg_deauthorize_users(zone_dochandle_t, char *);
extern boolean_t zonecfg_valid_auths(const char *, const char *);
extern int zonecfg_get_mac_lists(zone_dochandle_t, zone_maclist_t *);

#ifdef __cplusplus
}
#endif

#endif	/* _LIBZONECFG_H */
