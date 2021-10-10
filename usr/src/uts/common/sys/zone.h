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

#ifndef _SYS_ZONE_H
#define	_SYS_ZONE_H

#include <sys/types.h>
#include <sys/mutex.h>
#include <sys/param.h>
#include <sys/rctl.h>
#include <sys/ipc_rctl.h>
#include <sys/pset.h>
#include <sys/tsol/label.h>
#include <sys/cred.h>
#include <sys/netstack.h>
#include <sys/uadmin.h>
#include <sys/ksynch.h>
#include <sys/socket_impl.h>
#include <sys/modhash.h>
#include <sys/paths.h>
#include <sys/nvpair.h>
#include <netinet/in.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * NOTE
 *
 * The contents of this file are private to the implementation of
 * Solaris and are subject to change at any time without notice.
 * Applications and drivers using these interfaces may fail to
 * run on future releases.
 */

/* Available both in kernel and for user space */

/* zone id restrictions and special ids */
#define	MAX_ZONEID	9999
#define	MIN_USERZONEID	1	/* lowest user-creatable zone ID */
#define	MIN_ZONEID	0	/* minimum zone ID on system */
#define	GLOBAL_ZONEID	0
#define	ZONEID_WIDTH	4	/* for printf */
#define	SYS_ZONE_PREFIX	"SYS"	/* system-reserved zone name prefix. */

/*
 * Special zoneid_t token to refer to all zones.
 */
#define	ALL_ZONES	(-1)

/* system call subcodes */
#define	ZONE_CREATE		0
#define	ZONE_DESTROY		1
#define	ZONE_GETATTR		2
#define	ZONE_ENTER		3
#define	ZONE_LIST		4
#define	ZONE_SHUTDOWN		5
#define	ZONE_LOOKUP		6
#define	ZONE_BOOT		7
/* 8 is reserved to match the S10 subcodes */
#define	ZONE_SETATTR		9
#define	ZONE_ADD_DATALINK	10
#define	ZONE_DEL_DATALINK	11
#define	ZONE_CHECK_DATALINK	12
#define	ZONE_LIST_DATALINK	13
#define	ZONE_LIST_DEFUNCT	14
#define	ZONE_GETATTR_DEFUNCT	15

/* zone attributes */
#define	ZONE_ATTR_ROOT		1
#define	ZONE_ATTR_NAME		2
#define	ZONE_ATTR_STATUS	3
#define	ZONE_ATTR_PRIVSET	4
#define	ZONE_ATTR_UNIQID	5
#define	ZONE_ATTR_POOLID	6
#define	ZONE_ATTR_INITPID	7
#define	ZONE_ATTR_SLBL		8
#define	ZONE_ATTR_INITNAME	9
#define	ZONE_ATTR_BOOTARGS	10
#define	ZONE_ATTR_BRAND		11
#define	ZONE_ATTR_PHYS_MCAP	12
#define	ZONE_ATTR_SCHED_CLASS	13
#define	ZONE_ATTR_FLAGS		14
#define	ZONE_ATTR_HOSTID	15
#define	ZONE_ATTR_FS_ALLOWED	16
#define	ZONE_ATTR_NETWORK	17
#define	ZONE_ATTR_MAC_PROFILE	18

/* In support of dataset aliasing */
#define	ZONE_INVISIBLE_SOURCE	"$globalzone"

/* Start of the brand-specific attribute namespace */
#define	ZONE_ATTR_BRAND_ATTRS	32768

#define	ZONE_FS_ALLOWED_MAX	1024

#define	ZONE_EVENT_CHANNEL	"com.sun:zones:status"
#define	ZONE_EVENT_STATUS_CLASS	"status"
#define	ZONE_EVENT_STATUS_SUBCLASS	"change"

#define	ZONE_EVENT_UNINITIALIZED	"uninitialized"
#define	ZONE_EVENT_INITIALIZED		"initialized"
#define	ZONE_EVENT_READY		"ready"
#define	ZONE_EVENT_RUNNING		"running"
#define	ZONE_EVENT_SHUTTING_DOWN	"shutting_down"

#define	ZONE_CB_NAME		"zonename"
#define	ZONE_CB_NEWSTATE	"newstate"
#define	ZONE_CB_OLDSTATE	"oldstate"
#define	ZONE_CB_TIMESTAMP	"when"
#define	ZONE_CB_ZONEID		"zoneid"

/*
 * Exit values that may be returned by scripts or programs invoked by various
 * zone commands.
 *
 * These are defined as:
 *
 *	ZONE_SUBPROC_OK
 *	===============
 *	The subprocess completed successfully.
 *
 *	ZONE_SUBPROC_DETACHED
 *	=====================
 *	The install or attach operation left a not yet attached zone within
 *	the zonepath dataset.  It is ok to try to attach the zone or mark it
 *	incomplete and try again.
 *
 *	ZONE_SUBPROC_USAGE
 *	==================
 *	The subprocess failed with a usage message, or a usage message should
 *	be output in its behalf.
 *
 *	ZONE_SUBPROC_NOTCOMPLETE
 *	========================
 *	The subprocess did not complete, but the actions performed by the
 *	subprocess require no recovery actions by the user.
 *
 *	For example, if the subprocess were called by "zoneadm install," the
 *	installation of the zone did not succeed but the user need not perform
 *	a "zoneadm uninstall" before attempting another install.
 *
 *	ZONE_SUBPROC_FATAL
 *	==================
 *	The subprocess failed in a fatal manner, usually one that will require
 *	some type of recovery action by the user.
 *
 *	For example, if the subprocess were called by "zoneadm install," the
 *	installation of the zone did not succeed and the user will need to
 *	perform a "zoneadm uninstall" before another install attempt is
 *	possible.
 *
 *	The non-success exit values are large to avoid accidental collision
 *	with values used internally by some commands (e.g. "Z_ERR" and
 *	"Z_USAGE" as used by zoneadm.)
 */
#define	ZONE_SUBPROC_OK			0
#define	ZONE_SUBPROC_DETACHED		252
#define	ZONE_SUBPROC_USAGE		253
#define	ZONE_SUBPROC_NOTCOMPLETE	254
#define	ZONE_SUBPROC_FATAL		255

#ifdef _SYSCALL32
typedef struct {
	caddr32_t zone_name;
	caddr32_t zone_root;
	caddr32_t zone_privs;
	size32_t zone_privssz;
	caddr32_t rctlbuf;
	size32_t rctlbufsz;
	caddr32_t extended_error;
	caddr32_t zfsbuf;
	size32_t  zfsbufsz;
	caddr32_t bezfsbuf;
	size32_t  bezfsbufsz;
	int match;			/* match level */
	uint32_t doi;			/* DOI for label */
	caddr32_t label;		/* label associated with zone */
	int flags;
} zone_def32;
#endif
typedef struct {
	const char *zone_name;
	const char *zone_root;
	const struct priv_set *zone_privs;
	size_t zone_privssz;
	const char *rctlbuf;
	size_t rctlbufsz;
	int *extended_error;
	const char *zfsbuf;
	size_t zfsbufsz;
	const char *bezfsbuf;
	size_t bezfsbufsz;
	int match;			/* match level */
	uint32_t doi;			/* DOI for label */
	const bslabel_t *label;		/* label associated with zone */
	int flags;
} zone_def;

/* extended error information */
#define	ZE_UNKNOWN	0	/* No extended error info */
#define	ZE_CHROOTED	1	/* tried to zone_create from chroot */
#define	ZE_AREMOUNTS	2	/* there are mounts within the zone */
#define	ZE_LABELINUSE	3	/* label is already in use by some other zone */
#define	ZE_ARESHARES	4	/* there are GZ shares in the zone's root */

/*
 * zone_status values
 *
 * You must modify zone_status_names in mdb(1M)'s genunix module
 * (genunix/zone.c) when you modify this enum.
 */
typedef enum {
	ZONE_IS_UNINITIALIZED = 0,
	ZONE_IS_INITIALIZED,
	ZONE_IS_READY,
	ZONE_IS_BOOTING,
	ZONE_IS_RUNNING,
	ZONE_IS_SHUTTING_DOWN,
	ZONE_IS_EMPTY,
	ZONE_IS_DOWN,
	ZONE_IS_DYING,
	ZONE_IS_DEAD
} zone_status_t;
#define	ZONE_MIN_STATE		ZONE_IS_UNINITIALIZED
#define	ZONE_MAX_STATE		ZONE_IS_DEAD

/*
 * Valid commands which may be issued by zoneadm to zoneadmd.  The kernel also
 * communicates with zoneadmd, but only uses Z_REBOOT and Z_HALT.
 * While these are enums, do not renumber them as the interface between
 * zoneadmd and several (brand) scripts uses the enum as number argument.
 * Add new commands before Z_AFTER_LAST_CMD or replace Z_UNUSED1.
 * Zoneadmd has a zone_cmd_t to string routine; it needs to follow the
 * changes in this file.
 */
typedef enum zone_cmd {
	Z_READY = 0,
	Z_MIN_CMD = 0,
	Z_BOOT,
	Z_SHUTDOWN,
	Z_REBOOT,
	Z_HALT,
	Z_NOTE_UNINSTALLING,
	Z_MOUNT,
	Z_UNUSED1,
	Z_UNMOUNT,
	Z_AFTER_LAST_CMD		/* must be last */
} zone_cmd_t;

/*
 * The structure of a request to zoneadmd.
 */
typedef struct zone_cmd_arg {
	uint64_t	uniqid;		/* unique "generation number" */
	zone_cmd_t	cmd;		/* requested action */
	uint32_t	flags;		/* flags for mounts and (re)boot */
	char locale[MAXPATHLEN];	/* locale in which to render messages */
	char bootbuf[BOOTARGS_MAX];	/* arguments passed to zone_boot() */
} zone_cmd_arg_t;

#define	Z_CMD_FLAG_NONE		0x0
#define	Z_CMD_FLAG_FORCE	0x1
#define	Z_CMD_FLAG_RW		0x2
#define	Z_CMD_FLAG_TRANSIENT_RW	0x4
#define	Z_CMD_FLAG_REBOOT	0x8

/*
 * Structure of zoneadmd's response to a request.  A NULL return value means
 * the caller should attempt to restart zoneadmd and retry.
 */
typedef struct zone_cmd_rval {
	int rval;			/* return value of request */
	char errbuf[1];	/* variable-sized buffer containing error messages */
} zone_cmd_rval_t;

/*
 * The zone support infrastructure uses the zone name as a component
 * of unix domain (AF_UNIX) sockets, which are limited to 108 characters
 * in length, so ZONENAME_MAX is limited by that.
 */
#define	ZONENAME_MAX		64

#define	GLOBAL_ZONENAME		"global"

/*
 * Extended Regular expression (see regex(5)) which matches all valid zone
 * names.
 */
#define	ZONENAME_REGEXP		"[a-zA-Z0-9][-_.a-zA-Z0-9]{0,62}"

/*
 * Where the zones support infrastructure places temporary files.
 */
#define	ZONES_TMPDIR		_PATH_SYSVOL "/zones"

/*
 * The path to the door used by clients to communicate with zoneadmd.
 */
#define	ZONE_DOOR_PATH		ZONES_TMPDIR "/%s.zoneadmd_door"


/* zone_flags */

/*
 * The following flags are set when the zone is created and are never modified.
 * Threads that test for these flags don't have to hold zone_lock.
 */
#define	ZF_HASHED_LABEL		0x2	/* zone has a unique label */
#define	ZF_IS_SCRATCH		0x4	/* scratch zone */
#define	ZF_NET_EXCL		0x8	/* Zone has an exclusive IP stack */
#define	ZF_LOCKDOWN		0x10	/* mwac is enabled for this zone */
#define	ZF_LOCKDOWN_TEMP	0x20	/* mwac can be disabled */
#define	ZF_TRANSIENT_RW		0x40	/* Read-only zone transient r/w */
#define	ZF_FORCED_RW		0x80	/* Read-only zone forced r/w */

/* zone_create flags */
#define	ZCF_NET_EXCL		0x1	/* Create a zone with exclusive IP */

/* zone network properties */
#define	ZONE_NETWORK_ADDRESS			1
#define	ZONE_NETWORK_DEFROUTER			2

#define	ZONE_NET_ADDRNAME	"address"
#define	ZONE_NET_RTRNAME	"route"

typedef struct zone_net_data {
	int zn_type;
	int zn_len;
	datalink_id_t zn_linkid;
	uint8_t zn_val[1];
} zone_net_data_t;


#define	ZONE_ALLOW_PARTITION	"allow-partition"
#define	ZONE_ALLOW_RAW_IO	"allow-raw-io"

/*
 * For dataset aliasing.  See comments above zone_dataset_t.
 */
typedef enum zone_ds_cfg {
	ZONE_DS_CFG_INVALID = 0,
	ZONE_DS_CFG_PLATFORM,		/* Configured via platform.xml */
	ZONE_DS_CFG_DELEGATED,		/* Configured via zonecfg */
	ZONE_DS_CFG_MAX
} zone_ds_cfg_t;
#define	ZONE_DS_CFG_VALID(x) \
	(((x) > ZONE_DS_CFG_INVALID) && ((x) < ZONE_DS_CFG_MAX))

#define	ZONE_DS_ALIAS_STR	"alias"
#define	ZONE_DS_CFGSRC_STR	"cfgsrc"

#ifdef _KERNEL

/*
 * We need to protect the definition of 'list_t' from userland applications and
 * libraries which may be defining ther own versions.
 */
#include <sys/list.h>

/*
 * uniqid of the Global Zone.
 *
 * We initialize its value to be higher than the highest valid zoneid
 * to keep the uniqid space from overlapping with the zoneid_t space
 */
#define	GLOBAL_ZONEUNIQID	(MAX_ZONEID + 1)

struct pool;
struct brand;

/*
 * Each of these constants identifies a kernel subsystem that acquires and
 * releases zone references.  Each subsystem that invokes
 * zone_hold_ref() and zone_rele_ref() should specify the
 * zone_ref_subsys_t constant associated with the subsystem.  Tracked holds
 * help users and developers quickly identify subsystems that stall zone
 * shutdowns indefinitely.
 *
 * NOTE: You must modify zone_ref_subsys_names in usr/src/uts/common/os/zone.c
 * when you modify this enumeration.
 */
typedef enum zone_ref_subsys {
	ZONE_REF_NFS,			/* NFS client */
	ZONE_REF_NFSV4,			/* NFSv4 client */
	ZONE_REF_SMBFS,			/* SMBFS */
	ZONE_REF_LOFI,			/* LOFI devices */
	ZONE_REF_VFS,			/* VFS infrastructure */
	ZONE_REF_IPC,			/* IPC infrastructure */
	ZONE_REF_NFSSRV,		/* NFS server */
	ZONE_REF_SHAREFS,		/* SHAREFS */
	ZONE_REF_NUM_SUBSYS		/* This must be the last entry. */
} zone_ref_subsys_t;

/*
 * zone_ref represents a general-purpose references to a zone.  Each zone's
 * references are linked into the zone's zone_t::zone_ref_list.  This allows
 * debuggers to walk zones' references.
 */
typedef struct zone_ref {
	struct zone	*zref_zone; /* the zone to which the reference refers */
	list_node_t	zref_linkage; /* linkage for zone_t::zone_ref_list */
} zone_ref_t;

/*
 * Dataset aliasing and read only zone roots make use of zone_dataset_t
 * list nodes.  Each list node corresponds to an nvlist item passed
 * into the kernel via zone_create().  That nvlist looks like:
 *
 *  <dataset name> => {
 *	alias => <alias>
 *	cfgsrc => <ZONE_DS_CFG_PLATFORM|ZONE_DS_CFG_DELEGATED>
 *  }
 *
 * For example, a zone that has the platform dataset in the 'zones' pool and
 * another delegated dataset in the 'tank' pool would look like:
 *
 * 'zones/zone1/rpool' => { alias => 'rpool'; cfgsrc => ZONE_DS_CFG_PLATFORM }
 * 'tank/zone1' => { alias => 'tank'; cfgsrc => ZONE_DS_CFG_DELEGATED }
 *
 * All of the delegated datasets are subject to aliasing.  See
 * zone_dataset_alias() and zone_dataset_unalias().  Only those configured as
 * platform datasets are subject to mandatory write access controls (MWAC).
 * See zone_dataset_visible().
 */
typedef struct zone_dataset {
	char		*zd_dataset;	/* Real dataset name */
	char		*zd_alias;	/* Aliased zpool name */
	zone_ds_cfg_t	zd_cfgsrc;	/* How was it configured */
	list_node_t	zd_linkage;
} zone_dataset_t;

/*
 * structure for zone kstats
 */
typedef struct zone_kstat {
	kstat_named_t zk_zonename;
	kstat_named_t zk_usage;
	kstat_named_t zk_value;
} zone_kstat_t;

struct cpucap;

typedef struct zone {
	/*
	 * zone_name is never modified once set.
	 */
	char		*zone_name;	/* zone's configuration name */
	/*
	 * zone_nodename and zone_domain are never freed once allocated.
	 */
	char		*zone_nodename;	/* utsname.nodename equivalent */
	char		*zone_domain;	/* srpc_domain equivalent */
	/*
	 * zone_hostid is used for per-zone hostid emulation.
	 * Currently it isn't modified after it's set (so no locks protect
	 * accesses), but that might have to change when we allow
	 * administrators to change running zones' properties.
	 *
	 * The global zone's zone_hostid must always be HW_INVALID_HOSTID so
	 * that zone_get_hostid() will function correctly.
	 */
	uint32_t	zone_hostid;	/* zone's hostid, HW_INVALID_HOSTID */
					/* if not emulated */
	/*
	 * zone_lock protects the following fields of a zone_t:
	 * 	zone_ref
	 * 	zone_cred_ref
	 *	zone_status
	 * 	zone_subsys_ref
	 * 	zone_ref_list
	 * 	zone_ntasks
	 *	zone_kthreads
	 * 	zone_flags
	 * 	zone_zsd
	 *	zone_pfexecd
	 *	zone_mwac_white_list
	 *	zone_mwac_black_list
	 *	zone_brand_name
	 *	zone_brand
	 *	zone_dl_list
	 */
	kmutex_t	zone_lock;
	/*
	 * zone_linkage is the zone's linkage into the active or
	 * death-row list.  The field is protected by zonehash_lock.
	 */
	list_node_t	zone_linkage;
	zoneid_t	zone_id;	/* ID of zone */
	uint_t		zone_ref;	/* count of zone_hold()s on zone */
	uint_t		zone_cred_ref;	/* count of zone_hold_cred()s on zone */
	/*
	 * Fixed-sized array of subsystem-specific reference counts
	 * The sum of all of the counts must be less than or equal to zone_ref.
	 * The array is indexed by the counts' subsystems' zone_ref_subsys_t
	 * constants.
	 */
	uint_t		zone_subsys_ref[ZONE_REF_NUM_SUBSYS];
	list_t		zone_ref_list;	/* list of zone_ref_t structs */
	/*
	 * zone_rootvp and zone_rootpath can never be modified once set.
	 */
	struct vnode	*zone_rootvp;	/* zone's root vnode */
	char		*zone_rootpath;	/* Path to zone's root + '/' */
	ushort_t	zone_flags;	/* misc flags */
	zone_status_t	zone_status;	/* ZONE_IS_{READY|RUNNING|etc} */
	uint_t		zone_ntasks;	/* number of tasks executing in zone */
	kmutex_t	zone_nlwps_lock; /* protects zone_nlwps, and *_nlwps */
					/* counters in projects and tasks */
					/* that are within the zone */
	rctl_qty_t	zone_nlwps;	/* number of lwps in zone */
	rctl_qty_t	zone_nlwps_ctl; /* protected by zone_rctls->rcs_lock */
	rctl_qty_t	zone_shmmax;	/* System V shared memory usage */
	ipc_rqty_t	zone_ipc;	/* System V IPC id resource usage */

	uint_t		zone_rootpathlen; /* strlen(zone_rootpath) + 1 */
	uint32_t	zone_shares;	/* FSS shares allocated to zone */
	rctl_set_t	*zone_rctls;	/* zone-wide (zone.*) rctls */
	kmutex_t	zone_mem_lock;	/* protects zone_locked_mem and */
					/* kpd_locked_mem for all */
					/* projects in zone. */
					/* Also protects zone_max_swap */
					/* grab after p_lock, before rcs_lock */
	rctl_qty_t	zone_locked_mem;	/* bytes of locked memory in */
						/* zone */
	rctl_qty_t	zone_locked_mem_ctl;	/* Current locked memory */
						/* limit.  Protected by */
						/* zone_rctls->rcs_lock */
	rctl_qty_t	zone_max_swap; /* bytes of swap reserved by zone */
	rctl_qty_t	zone_max_swap_ctl;	/* current swap limit. */
						/* Protected by */
						/* zone_rctls->rcs_lock */
	kmutex_t	zone_rctl_lock;	/* protects zone_max_lofi */
	rctl_qty_t	zone_max_lofi; /* lofi devs for zone */
	rctl_qty_t	zone_max_lofi_ctl;	/* current lofi limit. */
						/* Protected by */
						/* zone_rctls->rcs_lock */
	list_t		zone_zsd;	/* list of Zone-Specific Data values */
	kcondvar_t	zone_cv;	/* used to signal state changes */
	struct proc	*zone_zsched;	/* Dummy kernel "zsched" process */
	pid_t		zone_proc_initpid; /* pid of "init" for this zone */
	char		*zone_initname;	/* fs path to 'init' */
	int		zone_boot_err;  /* for zone_boot() if boot fails */
	char		*zone_bootargs;	/* arguments passed via zone_boot() */
	char		*zone_macprofile; /* mac-profile */
	uint64_t	zone_phys_mcap;	/* physical memory cap */
	kthread_t	*zone_kthreads;	/* kernel threads in zone */
	struct priv_set	*zone_privset;	/* limit set for zone */
	/*
	 * zone_vfslist is protected by vfs_list_lock().
	 */
	struct vfs	*zone_vfslist;	/* list of FS's mounted in zone */
	uint64_t	zone_uniqid;	/* unique zone generation number */
	struct cred	*zone_kcred;	/* kcred-like, zone-limited cred */
	/*
	 * zone_pool is protected by pool_lock().
	 */
	struct pool	*zone_pool;	/* pool the zone is bound to */
	hrtime_t	zone_pool_mod;	/* last pool bind modification time */
	/* zone_psetid is protected by cpu_lock */
	psetid_t	zone_psetid;	/* pset the zone is bound to */
	/*
	 * The following two can be read without holding any locks.  They are
	 * updated under cpu_lock.
	 */
	int		zone_ncpus;  /* zone's idea of ncpus */
	int		zone_ncpus_online; /* zone's idea of ncpus_online */
	/*
	 * List of ZFS datasets exported to this zone.
	 */
	list_t		zone_datasets;		/* list of datasets */

	ts_label_t	*zone_slabel;	/* zone sensitivity label */
	int		zone_match;	/* require label match for packets */
	tsol_mlp_list_t zone_mlps;	/* MLPs on zone-private addresses */

	boolean_t	zone_restart_init;	/* Restart init if it dies? */
	char		*zone_brand_name;	/* zone brand name */
	struct brand	*zone_brand;		/* zone brand module ptr */
	void		*zone_brand_data;	/* store brand specific data */
	id_t		zone_defaultcid;	/* dflt scheduling class id */
	kstat_t		*zone_swapresv_kstat;
	kstat_t		*zone_lockedmem_kstat;
	list_t		zone_dl_list;
	netstack_t	*zone_netstack;
	struct cpucap	*zone_cpucap;	/* CPU caps data */
	/*
	 * Solaris Auditing per-zone audit context
	 */
	struct au_kcontext	*zone_audit_kctxt;
	/*
	 * For private use by mntfs.
	 */
	struct mntelem	*zone_mntfs_db;
	krwlock_t	zone_mntfs_db_lock;

	struct klpd_reg		*zone_pfexecd;

	char		*zone_fs_allowed;
	struct avl_tree		*zone_mwac_white_list;
	struct avl_tree		*zone_mwac_black_list;
	rctl_qty_t	zone_nprocs;	/* number of processes in the zone */
	rctl_qty_t	zone_nprocs_ctl;	/* current limit protected by */
						/* zone_rctls->rcs_lock */
	kstat_t		*zone_nprocs_kstat;
	mod_hash_t	*zone_devann_hash;
	kmutex_t	zone_devann_lock;
	time_t		zone_destroy_time;  /* when the zone went defunct */
} zone_t;

/*
 * Special value of zone_psetid to indicate that pools are disabled.
 */
#define	ZONE_PS_INVAL	PS_MYID


extern zone_t zone0;
extern zone_t *global_zone;
extern uint_t maxzones;
extern rctl_hndl_t rc_zone_nlwps;
extern rctl_hndl_t rc_zone_nprocs;

extern long zone(int, void *, void *, void *, void *);
extern void zone_zsd_init(void);
extern void zone_init(void);
extern void zone_hold(zone_t *);
extern void zone_rele(zone_t *);
extern void zone_init_ref(zone_ref_t *);
extern void zone_hold_ref(zone_t *, zone_ref_t *, zone_ref_subsys_t);
extern void zone_rele_ref(zone_ref_t *, zone_ref_subsys_t);
extern void zone_cred_hold(zone_t *);
extern void zone_cred_rele(zone_t *);
extern void zone_task_hold(zone_t *);
extern void zone_task_rele(zone_t *);
extern zone_t *zone_find_by_id(zoneid_t);
extern zone_t *zone_find_by_label(const ts_label_t *);
extern zone_t *zone_find_by_name(const char *);
extern zone_t *zone_find_by_any_path(const char *, boolean_t);
extern zone_t *zone_find_by_path(const char *);
extern zoneid_t getzoneid(void);
extern int zone_datalink_walk(zoneid_t, int (*)(datalink_id_t, void *), void *);
extern int zone_check_datalink(zoneid_t *, datalink_id_t);

/*
 * Zone-specific data (ZSD) APIs
 */
/*
 * The following is what code should be initializing its zone_key_t to if it
 * calls zone_getspecific() without necessarily knowing that zone_key_create()
 * has been called on the key.
 */
#define	ZONE_KEY_UNINITIALIZED	0

typedef uint_t zone_key_t;

extern void	zone_key_create(zone_key_t *, void *(*)(zoneid_t),
    void (*)(zoneid_t, void *), void (*)(zoneid_t, void *));
extern int	zone_key_delete(zone_key_t);
extern void	*zone_getspecific(zone_key_t, zone_t *);
extern int	zone_setspecific(zone_key_t, zone_t *, const void *);

/*
 * The definition of a zsd_entry is truly private to zone.c and is only
 * placed here so it can be shared with mdb.
 *
 * State maintained for each zone times each registered key, which tracks
 * the state of the create, shutdown and destroy callbacks.
 *
 * zsd_flags is used to keep track of pending actions to avoid holding locks
 * when calling the create/shutdown/destroy callbacks, since doing so
 * could lead to deadlocks.
 */
struct zsd_entry {
	zone_key_t		zsd_key;	/* Key used to lookup value */
	void			*zsd_data;	/* Caller-managed value */
	/*
	 * Callbacks to be executed when a zone is created, shutdown, and
	 * destroyed, respectively.
	 */
	void			*(*zsd_create)(zoneid_t);
	void			(*zsd_shutdown)(zoneid_t, void *);
	void			(*zsd_destroy)(zoneid_t, void *);
	list_node_t		zsd_linkage;
	uint16_t		zsd_flags;	/* See below */
	kcondvar_t		zsd_cv;
};

/*
 * zsd_flags
 */
#define	ZSD_CREATE_NEEDED	0x0001
#define	ZSD_CREATE_INPROGRESS	0x0002
#define	ZSD_CREATE_COMPLETED	0x0004
#define	ZSD_SHUTDOWN_NEEDED	0x0010
#define	ZSD_SHUTDOWN_INPROGRESS	0x0020
#define	ZSD_SHUTDOWN_COMPLETED	0x0040
#define	ZSD_DESTROY_NEEDED	0x0100
#define	ZSD_DESTROY_INPROGRESS	0x0200
#define	ZSD_DESTROY_COMPLETED	0x0400

#define	ZSD_CREATE_ALL	\
	(ZSD_CREATE_NEEDED|ZSD_CREATE_INPROGRESS|ZSD_CREATE_COMPLETED)
#define	ZSD_SHUTDOWN_ALL	\
	(ZSD_SHUTDOWN_NEEDED|ZSD_SHUTDOWN_INPROGRESS|ZSD_SHUTDOWN_COMPLETED)
#define	ZSD_DESTROY_ALL	\
	(ZSD_DESTROY_NEEDED|ZSD_DESTROY_INPROGRESS|ZSD_DESTROY_COMPLETED)

#define	ZSD_ALL_INPROGRESS \
	(ZSD_CREATE_INPROGRESS|ZSD_SHUTDOWN_INPROGRESS|ZSD_DESTROY_INPROGRESS)

/*
 * Macros to help with zone visibility restrictions.
 */

/*
 * Is process in the global zone?  Or is the zone the global zone)?
 * NB: The userland zfs implementation has its own definitions of these.
 * See sys/zfs_context.h.
 */
#define	INGLOBALZONE(p) \
	((p)->p_zone == global_zone)
#define	ISGLOBALZONE(z) \
	((z) == global_zone)
/*
 * Can process view objects in given zone?
 */
#define	HASZONEACCESS(p, zoneid) \
	((p)->p_zone->zone_id == (zoneid) || INGLOBALZONE(p))

/*
 * Convenience macro to see if a resolved path is visible from within a
 * given zone. The path argument must have previously been resolved.
 *
 * zone_rootpath has a trailing '/', and zone_rootpathlen is
 * (strlen(zone_rootpath) + 1), so we compare the first
 * (zone_rootpathlen - 2) bytes, and then check if the next character is
 * either '\0' (ie, path is the rootpath),
 * or '/' (path is a subdirectory of the zone's rootpath).
 */
#define	ZONE_PATH_VISIBLE(path, zone)			\
	(strncmp((path), (zone)->zone_rootpath,		\
	    (zone)->zone_rootpathlen - 2) == 0 &&	\
	((path)[(zone)->zone_rootpathlen - 2] == '/' ||	\
	(path)[(zone)->zone_rootpathlen - 2] == '\0'))

/*
 * Convenience macro to go from the global view of a path to that seen
 * from within said zone.  It is the responsibility of the caller to
 * ensure that the path is a resolved one (ie, no '..'s or '.'s), and is
 * in fact visible from within the zone.
 *
 * We special-case the zone's rootdir, since that won't have a trailing
 * '/': explicitly return "/".
 *
 * Otherwise we strip off the first (zone_rootpathlen - 2) characters
 * from "path".
 */
#define	ZONE_PATH_TRANSLATE(path, zone)					\
	(ASSERT(ZONE_PATH_VISIBLE(path, zone)),				\
	*((path) + (zone)->zone_rootpathlen - 2) == '\0' ?  "/" :	\
	(ASSERT(*((path) + (zone)->zone_rootpathlen - 2) == '/'),	\
	(path) + (zone)->zone_rootpathlen - 2))

/*
 * Special processes visible in all zones.
 */
#define	ZONE_SPECIALPID(x)	 ((x) == 0 || (x) == 1)

/*
 * Zone-safe version of thread_create() to be used when the caller wants to
 * create a kernel thread to run within the current zone's context.
 */
extern kthread_t *zthread_create(caddr_t, size_t, void (*)(), void *, size_t,
    pri_t);
extern void zthread_exit(void);

extern void zproc_enter(zone_t *);

/*
 * Functions for an external observer to register interest in a zone's status
 * change.  Observers will be woken up when the zone status equals the status
 * argument passed in (in the case of zone_status_timedwait, the function may
 * also return because of a timeout; zone_status_wait_sig may return early due
 * to a signal being delivered; zone_status_timedwait_sig may return for any of
 * the above reasons).
 *
 * Otherwise these behave identically to cv_timedwait(), cv_wait(), and
 * cv_wait_sig() respectively.
 */
extern clock_t zone_status_timedwait(zone_t *, clock_t, zone_status_t);
extern clock_t zone_status_timedwait_sig(zone_t *, clock_t, zone_status_t);
extern void zone_status_wait(zone_t *, zone_status_t);
extern int zone_status_wait_sig(zone_t *, zone_status_t);

/*
 * Get the status  of the zone (at the time it was called).  The state may
 * have progressed by the time it is returned.
 */
extern zone_status_t zone_status_get(zone_t *);

/*
 * Safely get the hostid of the specified zone (defaults to machine's hostid
 * if the specified zone doesn't emulate a hostid).  Passing NULL retrieves
 * the global zone's (i.e., physical system's) hostid.
 */
extern uint32_t zone_get_hostid(zone_t *);

/*
 * Get the "kcred" credentials corresponding to the given zone.
 */
extern struct cred *zone_get_kcred(zoneid_t);

/*
 * Get/set the pool the zone is currently bound to.
 */
extern struct pool *zone_pool_get(zone_t *);
extern void zone_pool_set(zone_t *, struct pool *);

/*
 * Get/set the pset the zone is currently using.
 */
extern psetid_t zone_pset_get(zone_t *);
extern void zone_pset_set(zone_t *, psetid_t);

/*
 * Get the number of cpus/online-cpus visible from the given zone.
 */
extern int zone_ncpus_get(zone_t *);
extern int zone_ncpus_online_get(zone_t *);

/*
 * Returns true if the named pool/dataset is visible in the given zone.
 */
extern int zone_dataset_visible(zone_t *, const char *, int *);

/*
 * Translates dataset names to/from aliases.
 */
extern int zone_dataset_alias(zone_t *, const char *, char *, size_t);
extern int zone_dataset_unalias(zone_t *, const char *, char *, size_t);

/*
 * zone version of kadmin()
 */
extern int zone_kadmin(int, int, const char *, cred_t *);
extern void zone_shutdown_global(void);

extern int zone_walk(int (*)(zone_t *, void *), void *);

extern rctl_hndl_t rc_zone_locked_mem;
extern rctl_hndl_t rc_zone_max_swap;
extern rctl_hndl_t rc_zone_max_lofi;

/*
 * Device annotations.
 *
 * The devnames zone profile can include PROFILE_TYPE_ANNOTATE entries,
 * which consist of a string key/value pair.  On lookup, devnames
 * annotates the dev_t for the sdev_node by calling into
 * zone_devann_insert() for each annotation entry.  Annotations are
 * per-zone and per-dev_t.  Drivers wishing to check for annotations
 * call zone_devann_find().  When a minor node is removed, the DDI code
 * calls zone_devann_clear() to remove all references to the matching dev_t.
 */

extern int zone_devann_present(zone_t *, dev_t);
extern char *zone_devann_find(zone_t *, dev_t, const char *);
extern int zone_devann_insert(zone_t *, dev_t, const char *, const char *);
extern void zone_devann_clear(dev_t);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZONE_H */
