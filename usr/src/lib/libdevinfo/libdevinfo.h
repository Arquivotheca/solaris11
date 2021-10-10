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
 * Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_LIBDEVINFO_H
#define	_LIBDEVINFO_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <errno.h>
#include <libnvpair.h>
#include <sys/param.h>
#include <sys/sunddi.h>
#include <sys/sunmdi.h>
#include <sys/openpromio.h>
#include <sys/ddi_impldefs.h>
#include <sys/devinfo_impl.h>
#include <limits.h>

/*
 * flags for di_walk_node
 */
#define	DI_WALK_CLDFIRST	0
#define	DI_WALK_SIBFIRST	1
#define	DI_WALK_LINKGEN		2

#define	DI_WALK_MASK		0xf

/*
 * flags for di_walk_link
 */
#define	DI_LINK_SRC		1
#define	DI_LINK_TGT		2

/*
 * return code for node_callback
 */
#define	DI_WALK_CONTINUE	0
#define	DI_WALK_PRUNESIB	-1
#define	DI_WALK_PRUNECHILD	-2
#define	DI_WALK_TERMINATE	-3

/*
 * flags for di_walk_minor
 */
#define	DI_CHECK_ALIAS		0x10
#define	DI_CHECK_INTERNAL_PATH	0x20

#define	DI_CHECK_MASK		0xf0

/*
 * flags for di_walk_hp
 */
#define	DI_HP_CONNECTOR		0x1
#define	DI_HP_PORT		0x2

/* nodeid types */
#define	DI_PSEUDO_NODEID	-1
#define	DI_SID_NODEID		-2
#define	DI_PROM_NODEID		-3

/* node & device states */
#define	DI_DRIVER_DETACHED	0x8000
#define	DI_DEVICE_OFFLINE	0x1
#define	DI_DEVICE_DOWN		0x2
#define	DI_DEVICE_DEGRADED	0x4
#define	DI_DEVICE_REMOVED	0x8
#define	DI_BUS_QUIESCED		0x100
#define	DI_BUS_DOWN		0x200

/* property types */
#define	DI_PROP_TYPE_BOOLEAN	0
#define	DI_PROP_TYPE_INT	1
#define	DI_PROP_TYPE_STRING	2
#define	DI_PROP_TYPE_BYTE	3
#define	DI_PROP_TYPE_UNKNOWN	4
#define	DI_PROP_TYPE_UNDEF_IT	5
#define	DI_PROP_TYPE_INT64	6

/* private macro for checking if a prop type is valid */
#define	DI_PROP_TYPE_VALID(type) \
	((((type) >= DI_PROP_TYPE_INT) && ((type) <= DI_PROP_TYPE_BYTE)) || \
	    ((type) == DI_PROP_TYPE_INT64))

/* opaque handles */
typedef struct di_node		*di_node_t;		/* node */
typedef struct di_minor		*di_minor_t;		/* minor_node */
typedef struct di_path		*di_path_t;		/* path_node */
typedef struct di_link		*di_link_t;		/* link */
typedef struct di_lnode		*di_lnode_t;		/* endpoint */
typedef struct di_devlink	*di_devlink_t;		/* devlink */
typedef struct di_hp		*di_hp_t;		/* hotplug */

typedef struct di_prop		*di_prop_t;		/* node property */
typedef struct di_path_prop	*di_path_prop_t;	/* path property */
typedef struct di_prom_prop	*di_prom_prop_t;	/* prom property */

typedef struct di_prom_handle	*di_prom_handle_t;	/* prom snapshot */
typedef struct di_devlink_handle *di_devlink_handle_t;	/* devlink snapshot */


/*
 * Null handles to make handles really opaque
 */
#define	DI_NODE_NIL		NULL
#define	DI_MINOR_NIL		NULL
#define	DI_PATH_NIL		NULL
#define	DI_LINK_NIL		NULL
#define	DI_LNODE_NIL		NULL
#define	DI_PROP_NIL		NULL
#define	DI_PROM_PROP_NIL	NULL
#define	DI_PROM_HANDLE_NIL	NULL
#define	DI_HP_NIL		NULL

/*
 * IEEE 1275 properties and other standardized property names
 */
#define	DI_PROP_FIRST_CHAS	"first-in-chassis"
#define	DI_PROP_SLOT_NAMES	"slot-names"
#define	DI_PROP_PHYS_SLOT	"physical-slot#"
#define	DI_PROP_DEV_TYPE	"device_type"
#define	DI_PROP_BUS_RANGE	"bus-range"
#define	DI_PROP_SERID		"serialid#"
#define	DI_PROP_REG		"reg"
#define	DI_PROP_AP_NAMES	"ap-names"

/* Interface Prototypes */

/*
 * Snapshot initialization and cleanup
 */
extern di_node_t	di_init(const char *phys_path, uint_t flag);
extern void		di_fini(di_node_t root);

/*
 * node: traversal, data access, and parameters
 */
extern uint64_t		di_cna_dev(di_node_t root);

extern int		di_walk_node(di_node_t root, uint_t flag, void *arg,
			    int (*node_callback)(di_node_t node, void *arg));

extern di_node_t	di_drv_first_node(const char *drv_name, di_node_t root);
extern di_node_t	di_drv_next_node(di_node_t node);

extern di_node_t	di_parent_node(di_node_t node);
extern di_node_t	di_sibling_node(di_node_t node);
extern di_node_t	di_child_node(di_node_t node);

extern char		*di_node_name(di_node_t node);
extern char		*di_bus_addr(di_node_t node);
extern char		*di_binding_name(di_node_t node);
extern int		di_compatible_names(di_node_t, char **names);
extern int		di_instance(di_node_t node);
extern int		di_nodeid(di_node_t node);
extern int		di_driver_major(di_node_t node);
extern uint_t		di_state(di_node_t node);
extern ddi_node_state_t	di_node_state(di_node_t node);
extern ddi_devid_t	di_devid(di_node_t node);
extern char		*di_driver_name(di_node_t node);
extern uint_t		di_driver_ops(di_node_t node);
extern uint64_t		di_node_cna_dev(di_node_t node);

extern void		di_node_private_set(di_node_t node, void *data);
extern void		*di_node_private_get(di_node_t node);

extern char		*di_devfs_path(di_node_t node);
extern char		*di_devfs_minor_path(di_minor_t minor);
extern void		di_devfs_path_free(char *path_buf);

/*
 * path_node: traversal, data access, and parameters
 */
extern di_path_t	di_path_phci_next_path(di_node_t node, di_path_t);
extern di_path_t	di_path_client_next_path(di_node_t node, di_path_t);

extern di_node_t	di_path_phci_node(di_path_t path);
extern di_node_t	di_path_client_node(di_path_t path);

extern char		*di_path_node_name(di_path_t path);
extern char		*di_path_bus_addr(di_path_t path);
extern int		di_path_instance(di_path_t path);
extern di_path_state_t	di_path_state(di_path_t path);
extern uint_t		di_path_flags(di_path_t path);

extern char		*di_path_devfs_path(di_path_t path);
extern char		*di_path_client_devfs_path(di_path_t path);

extern void		di_path_private_set(di_path_t path, void *data);
extern void		*di_path_private_get(di_path_t path);

extern uint64_t		di_path_cna_dev(di_path_t path);

/*
 * minor_node: traversal, data access, and parameters
 */
extern int		di_walk_minor(di_node_t root, const char *minortype,
			    uint_t flag, void *arg,
			    int (*minor_callback)(di_node_t node,
			    di_minor_t minor, void *arg));
extern di_minor_t	di_minor_next(di_node_t node, di_minor_t minor);

extern di_node_t	di_minor_devinfo(di_minor_t minor);
extern ddi_minor_type	di_minor_type(di_minor_t minor);
extern char		*di_minor_name(di_minor_t minor);
extern dev_t		di_minor_devt(di_minor_t minor);
extern int		di_minor_spectype(di_minor_t minor);
extern char		*di_minor_nodetype(di_minor_t node);

extern void		di_minor_private_set(di_minor_t minor, void *data);
extern void		*di_minor_private_get(di_minor_t minor);

/*
 * node: property access
 */
extern di_prop_t	di_prop_next(di_node_t node, di_prop_t prop);

extern char		*di_prop_name(di_prop_t prop);
extern int		di_prop_type(di_prop_t prop);
extern dev_t		di_prop_devt(di_prop_t prop);

extern int		di_prop_ints(di_prop_t prop, int **prop_data);
extern int		di_prop_int64(di_prop_t prop, int64_t **prop_data);
extern int		di_prop_strings(di_prop_t prop, char **prop_data);
extern int		di_prop_bytes(di_prop_t prop, uchar_t **prop_data);

extern int		di_prop_lookup_bytes(dev_t dev, di_node_t node,
			    const char *prop_name, uchar_t **prop_data);
extern int		di_prop_lookup_ints(dev_t dev, di_node_t node,
			    const char *prop_name, int **prop_data);
extern int		di_prop_lookup_int64(dev_t dev, di_node_t node,
			    const char *prop_name, int64_t **prop_data);
extern int		di_prop_lookup_strings(dev_t dev, di_node_t node,
			    const char *prop_name, char **prop_data);

/*
 * prom_node: property access
 */
extern di_prom_handle_t	di_prom_init(void);
extern void		di_prom_fini(di_prom_handle_t ph);

extern di_prom_prop_t	di_prom_prop_next(di_prom_handle_t ph, di_node_t node,
			    di_prom_prop_t prom_prop);

extern char		*di_prom_prop_name(di_prom_prop_t prom_prop);
extern int		di_prom_prop_data(di_prom_prop_t prop,
			    uchar_t **prom_prop_data);

extern int		di_prom_prop_lookup_ints(di_prom_handle_t prom,
			    di_node_t node, const char *prom_prop_name,
			    int **prom_prop_data);
extern int		di_prom_prop_lookup_strings(di_prom_handle_t prom,
			    di_node_t node, const char *prom_prop_name,
			    char **prom_prop_data);
extern int		di_prom_prop_lookup_bytes(di_prom_handle_t prom,
			    di_node_t node, const char *prom_prop_name,
			    uchar_t **prom_prop_data);

/*
 * path_node: property access
 */
extern di_path_prop_t	di_path_prop_next(di_path_t path, di_path_prop_t prop);

extern char		*di_path_prop_name(di_path_prop_t prop);
extern int		di_path_prop_type(di_path_prop_t prop);
extern int		di_path_prop_len(di_path_prop_t prop);

extern int		di_path_prop_bytes(di_path_prop_t prop,
			    uchar_t **prop_data);
extern int		di_path_prop_ints(di_path_prop_t prop,
			    int **prop_data);
extern int		di_path_prop_int64s(di_path_prop_t prop,
			    int64_t **prop_data);
extern int		di_path_prop_strings(di_path_prop_t prop,
			    char **prop_data);

extern int		di_path_prop_lookup_bytes(di_path_t path,
			    const char *prop_name, uchar_t **prop_data);
extern int		di_path_prop_lookup_ints(di_path_t path,
			    const char *prop_name, int **prop_data);
extern int		di_path_prop_lookup_int64s(di_path_t path,
			    const char *prop_name, int64_t **prop_data);
extern int		di_path_prop_lookup_strings(di_path_t path,
			    const char *prop_name, char **prop_data);

/*
 * layering link/lnode: traversal, data access, and parameters
 */
extern int		di_walk_link(di_node_t root, uint_t flag,
			    uint_t endpoint, void *arg,
			    int (*link_callback)(di_link_t link, void *arg));
extern int		di_walk_lnode(di_node_t root, uint_t flag, void *arg,
			    int (*lnode_callback)(di_lnode_t lnode, void *arg));

extern di_link_t	di_link_next_by_node(di_node_t node,
			    di_link_t link, uint_t endpoint);
extern di_link_t	di_link_next_by_lnode(di_lnode_t lnode,
			    di_link_t link, uint_t endpoint);
extern di_lnode_t	di_lnode_next(di_node_t node, di_lnode_t lnode);
extern char		*di_lnode_name(di_lnode_t lnode);

extern int		di_link_spectype(di_link_t link);
extern di_lnode_t	di_link_to_lnode(di_link_t link, uint_t endpoint);

extern di_node_t	di_lnode_devinfo(di_lnode_t lnode);
extern int		di_lnode_devt(di_lnode_t lnode, dev_t *devt);

extern void		di_link_private_set(di_link_t link, void *data);
extern void		*di_link_private_get(di_link_t link);
extern void		di_lnode_private_set(di_lnode_t lnode, void *data);
extern void		*di_lnode_private_get(di_lnode_t lnode);

/*
 * hp_node: traversal, data access, and parameters
 */
extern int		di_walk_hp(di_node_t node, const char *type,
			    uint_t flag, void *arg,
			    int (*hp_callback)(di_node_t node, di_hp_t hp,
			    void *arg));
extern di_hp_t		di_hp_next(di_node_t node, di_hp_t hp);

extern char		*di_hp_name(di_hp_t hp);
extern int		di_hp_connection(di_hp_t hp);
extern int		di_hp_depends_on(di_hp_t hp);
extern int		di_hp_state(di_hp_t hp);
extern size_t		di_hp_state_priv_size(di_hp_t hp);
extern char		*di_hp_state_priv(di_hp_t hp);
extern int		di_hp_type(di_hp_t hp);
extern char		*di_hp_description(di_hp_t hp);
extern time_t		di_hp_last_change(di_hp_t hp);
extern di_node_t	di_hp_child(di_hp_t hp);

/*
 * Private interfaces
 *
 * The interfaces and structures below are private to this implementation
 * of Solaris and are subject to change at any time without notice.
 *
 * Applications and drivers using these interfaces may fail
 * to run on future releases.
 */
extern di_prop_t di_prop_find(dev_t match_dev, di_node_t node,
    const char *name);
extern int di_devfs_path_match(const char *dp1, const char *dp2);

extern di_node_t	di_vhci_first_node(di_node_t root);
extern di_node_t	di_vhci_next_node(di_node_t node);
extern di_node_t	di_phci_first_node(di_node_t vhci_node);
extern di_node_t	di_phci_next_node(di_node_t node);

/*
 * Interfaces for handling IEEE 1275 and other standardized properties
 */

/* structure for a single slot */
typedef struct di_slot_name {
	int num;	/* corresponding pci device number */
	char *name;
} di_slot_name_t;

extern void di_slot_names_free(int count, di_slot_name_t *slot_names);
extern int di_slot_names_decode(uchar_t *rawdata, int rawlen,
    di_slot_name_t **prop_data);
extern int di_prop_slot_names(di_prop_t prop, di_slot_name_t **prop_data);
extern int di_prom_prop_slot_names(di_prom_prop_t prom_prop,
    di_slot_name_t **prop_data);
extern int di_prop_lookup_slot_names(dev_t dev, di_node_t node,
    di_slot_name_t **prop_data);
extern int di_prom_prop_lookup_slot_names(di_prom_handle_t ph, di_node_t node,
    di_slot_name_t **prop_data);

/*
 * XXX Remove the private di_path_(addr,next,next_phci,next_client) interfaces
 * below after NWS consolidation switches to using di_path_bus_addr,
 * di_path_phci_next_path, and di_path_client_next_path per CR6638521.
 */
extern char *di_path_addr(di_path_t path, char *buf);
extern di_path_t di_path_next(di_node_t node, di_path_t path);
extern di_path_t di_path_next_phci(di_node_t node, di_path_t path);
extern di_path_t di_path_next_client(di_node_t node, di_path_t path);

/*
 * Interfaces for private data
 */
extern di_node_t di_init_driver(const char *drv_name, uint_t flag);
extern di_node_t di_init_impl(const char *phys_path, uint_t flag,
    struct di_priv_data *priv_data);

/*
 * Prtconf needs to know property lists, raw prop_data, and private data
 */
extern di_prop_t di_prop_drv_next(di_node_t node, di_prop_t prop);
extern di_prop_t di_prop_sys_next(di_node_t node, di_prop_t prop);
extern di_prop_t di_prop_global_next(di_node_t node, di_prop_t prop);
extern di_prop_t di_prop_vendor_global_next(di_node_t node, di_prop_t prop);
extern di_prop_t di_prop_admin_global_next(di_node_t node, di_prop_t prop);
extern di_prop_t di_prop_hw_next(di_node_t node, di_prop_t prop);
extern di_prop_t di_prop_vendor_next(di_node_t node, di_prop_t prop);
extern di_prop_t di_prop_admin_next(di_node_t node, di_prop_t prop);

extern int di_prop_rawdata(di_prop_t prop, uchar_t **prop_data);
extern void *di_parent_private_data(di_node_t node);
extern void *di_driver_private_data(di_node_t node);

/*
 * The value of the dip's devi_flags field
 */
uint_t di_flags(di_node_t node);

/*
 * Types of links for devlink lookup
 */
#define	DI_PRIMARY_LINK		0x01
#define	DI_SECONDARY_LINK	0x02
#define	DI_LINK_TYPES		0x03

/*
 * Flag for di_devlink_init()
 */
#define	DI_MAKE_LINK	0x01

/*
 * Flag for di_devlink_close()
 */
#define	DI_LINK_ERROR	0x01

/*
 * For devfsadm synchronous link creation interfaces
 */
#define	DEVFSADM_SYNCH_DOOR	".devfsadm_synch_door"

/*
 * devlink create argument
 */
struct dca_off {
	uint32_t	dca_root;
	uint32_t	dca_minor;
	uint32_t	dca_driver;
	int		dca_error;
	int		dca_flags;
	char		dca_name[PATH_MAX+MAXNAMELEN];
};

extern di_devlink_handle_t di_devlink_init(const char *name, uint_t flags);
extern int di_devlink_walk(di_devlink_handle_t hdl, const char *re,
    const char *minor_path, uint_t flags, void *arg,
    int (*devlink_callback)(di_devlink_t, void *));
extern const char *di_devlink_path(di_devlink_t devlink);
extern const char *di_devlink_content(di_devlink_t devlink);
extern int di_devlink_type(di_devlink_t devlink);
extern di_devlink_t di_devlink_dup(di_devlink_t devlink);
extern int di_devlink_free(di_devlink_t devlink);
extern int di_devlink_fini(di_devlink_handle_t *hdlp);

extern di_devlink_handle_t di_devlink_open(const char *root_dir, uint_t flags);
extern int di_devlink_close(di_devlink_handle_t *pp, int flag);
extern int di_devlink_rm_link(di_devlink_handle_t hdp, const char *link);
extern int di_devlink_add_link(di_devlink_handle_t hdp, const char *link,
    const char *content, int flags);
extern int di_devlink_update(di_devlink_handle_t hdp);
extern di_devlink_handle_t di_devlink_init_root(const char *root,
    const char *name, uint_t flags);
extern int di_devlink_cache_walk(di_devlink_handle_t hdp, const char *re,
    const char *path, uint_t flags, void *arg,
    int (*devlink_callback)(di_devlink_t, void *));

/*
 * Private interfaces for I/O retire
 */
typedef struct di_retire {
	void	*rt_hdl;
	void	(*rt_abort)(void *hdl, const char *format, ...);
	void	(*rt_debug)(void *hdl, const char *format, ...);
} di_retire_t;

extern int di_retire_device(char *path, di_retire_t *dp, int flags);
extern int di_unretire_device(char *path, di_retire_t *dp);
extern uint_t di_retired(di_node_t node);

/*
 * Private interfaces for /etc/logindevperm
 */
extern int di_devperm_login(const char *, uid_t, gid_t, void (*)(char *));
extern int di_devperm_logout(const char *);

/*
 * Private interface for looking up, by path string, a node/path/minor
 * in a snapshot.
 */
extern di_node_t di_lookup_node(di_node_t root, char *path);
extern di_path_t di_lookup_path(di_node_t root, char *path);

/*
 * Private hotplug interfaces to be used between cfgadm pci plugin and
 * devfsadm link generator.
 */
extern char *di_dli_name(char *);
extern int di_dli_openr(char *);
extern int di_dli_openw(char *);
extern void di_dli_close(int);

/*
 * Private interface for parsing path_to_inst binding file
 */
extern int devfs_parse_binding_file(const char *,
	int (*)(void *, const char *, int, const char *), void *);
extern int devfs_walk_minor_nodes(const char *,
	int (*)(void *, const char *), void *);

/*
 * finddev - alternate readdir to discover only /dev persisted device names
 */
typedef struct __finddevhdl *finddevhdl_t;

extern int		device_exists(const char *);
extern int		finddev_readdir(const char *, finddevhdl_t *);
extern int		finddev_emptydir(const char *);
extern void		finddev_close(finddevhdl_t);
extern const char	*finddev_next(finddevhdl_t);


/*
 * Private interfaces for non-global /dev profile
 */
typedef struct __di_prof	*di_prof_t;

extern int	di_prof_init(const char *mountpt, di_prof_t *);
extern void	di_prof_fini(di_prof_t);
extern int	di_prof_commit(di_prof_t);
extern int	di_prof_add_dev(di_prof_t, const char *);
extern int	di_prof_add_devann(di_prof_t, const char *, const char *);
extern int	di_prof_add_exclude(di_prof_t, const char *);
extern int	di_prof_add_symlink(di_prof_t, const char *, const char *);
extern int	di_prof_add_map(di_prof_t, const char *, const char *);

/*
 * Private interfaces for <driver><instance><minor> to path conversion.
 * NOTE: These interfaces do not require or cause attach.  The implementation
 * uses the kernel instance-tree (/etc/path_to_inst) and the di_devlinks
 * database information.
 */
typedef struct __di_dim	*di_dim_t;

extern di_dim_t	di_dim_init(void);
extern void	di_dim_fini(di_dim_t);
extern char	*di_dim_path_devices(di_dim_t,
		    char *drv_name, int instance, char *minor_name);
extern char	*di_dim_path_dev(di_dim_t,
		    char *drv_name, int instance, char *minor_name);

/*
 * Alias related exported interfaces
 */
char *di_alias2curr(di_node_t anynode, char *alias);

/*
 * Private Chassis-Receptacle-Occupant-Link (di_cro) interfaces:
 */
/* di_cro_ opaque handles */
typedef struct di_cro_hdl	*di_cro_hdl_t;
typedef struct di_cro_reca	*di_cro_reca_t;
typedef struct di_cro_rec	*di_cro_rec_t;

/* di_cro_ snapshot/record-array interfaces */
di_cro_hdl_t	di_cro_init(char *cro_db_file, int flags);
void		di_cro_fini(di_cro_hdl_t h);
#define	DI_INIT_HEADERONLY	0x1
uint64_t	di_cro_get_cna(di_cro_hdl_t h);
char		*di_cro_get_fletcher(di_cro_hdl_t h);
char		*di_cro_get_date(di_cro_hdl_t h);
char		*di_cro_get_server_id(di_cro_hdl_t h);
char		*di_cro_get_product_id(di_cro_hdl_t h);
char		*di_cro_get_chassis_id(di_cro_hdl_t h);

di_cro_reca_t	di_cro_reca_create(di_cro_hdl_t h,
		    uint32_t	rec_flag,
		    char	*re_product_id,
		    char	*re_chassis_id,
		    char	*re_alias_id,
		    char	*re_receptacle_name,
		    char	*re_receptacle_type,
		    char	*re_receptacle_fmri,
		    char	*re_occupant_type,
		    char	*re_occupant_instance,
		    char	*re_devchassis_path,
		    char	*re_occupant_devices,
		    char	*re_occupant_paths,
		    char	*re_occupant_compdev,
		    char	*re_occupant_devid,
		    char	*re_occupant_mfg,
		    char	*re_occupant_model,
		    char	*re_occupant_part,
		    char	*re_occupant_serial,
		    char	*re_occupant_firm,
		    char	*re_occupant_misc_1,
		    char	*re_occupant_misc_2,
		    char	*re_occupant_misc_3);
#define	DI_CRO_REC_FLAG_PRIV	0x1	/* private record */
di_cro_reca_t	di_cro_reca_create_query(di_cro_hdl_t h,
		    uint32_t    rec_flag,
		    char	*query);
#define	DI_CRO_Q_PRODUCT_ID		"product-id"
#define	DI_CRO_Q_CHASSIS_ID		"chassis-id"
#define	DI_CRO_Q_ALIAS_ID		"alias-id"
#define	DI_CRO_Q_RECEPTACLE_NAME	"receptacle-name"
#define	DI_CRO_Q_RECEPTACLE_TYPE	"receptacle-type"
#define	DI_CRO_Q_RECEPTACLE_FMRI	"receptacle-fmri"
#define	DI_CRO_Q_OCCUPANT_TYPE		"occupant-type"
#define	DI_CRO_Q_OCCUPANT_INSTANCE	"occupant-instance"
#define	DI_CRO_Q_DEVCHASSIS_PATH	"devchassis-path"
#define	DI_CRO_Q_OCCUPANT_DEVICES	"occupant-devices"
#define	DI_CRO_Q_OCCUPANT_PATHS		"occupant-paths"
#define	DI_CRO_Q_OCCUPANT_COMPDEV	"occupant-compdev"
#define	DI_CRO_Q_OCCUPANT_DEVID		"occupant-devid"
#define	DI_CRO_Q_OCCUPANT_MFG		"occupant-mfg"
#define	DI_CRO_Q_OCCUPANT_MODEL		"occupant-model"
#define	DI_CRO_Q_OCCUPANT_PART		"occupant-part"
#define	DI_CRO_Q_OCCUPANT_SERIAL	"occupant-serial"
#define	DI_CRO_Q_OCCUPANT_FIRM		"occupant-firm"
#define	DI_CRO_Q_OCCUPANT_MISC_1	"occupant-misc-1"
#define	DI_CRO_Q_OCCUPANT_MISC_2	"occupant-misc-2"
#define	DI_CRO_Q_OCCUPANT_MISC_3	"occupant-misc-3"
#define	DI_CRO_QREFMT			":\"%s\""

di_cro_rec_t	di_cro_reca_next(di_cro_reca_t ra, di_cro_rec_t r);
void		di_cro_reca_destroy(di_cro_reca_t ra);

/*
 * di_cro_ record field index interfaces
 *
 * Example:
 *		for (i = 0; more; i++)
 *			xxx = di_cro_rec_fgeti_xxx(r, i, &more, ":");
 *
 *	If you know that there is only one item in the array:
 * 		xxx = di_cro_rec_fgeti_xxx(r, 0, NULL, NULL);
 */
char	*di_cro_rec_fgeti_product_id(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_chassis_id(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_alias_id(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_receptacle_name(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_receptacle_type(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_receptacle_fmri(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_occupant_type(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_occupant_instance(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_devchassis_path(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_occupant_devices(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_occupant_paths(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_occupant_compdev(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_occupant_devid(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_occupant_mfg(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_occupant_model(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_occupant_part(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_occupant_serial(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_occupant_firm(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_occupant_misc_1(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_occupant_misc_2(di_cro_rec_t r, int, int *, char *);
char	*di_cro_rec_fgeti_occupant_misc_3(di_cro_rec_t r, int, int *, char *);

void	di_cro_rec_priv_set(di_cro_rec_t r, void *);
void	*di_cro_rec_priv_get(di_cro_rec_t r);
char	*di_cro_strclean(char *, int, int);

/*
 * Private: The following di_crodc_ interfaces are for devchassisd(1M).
 * Return 0 on success, caller must strfree() non-NULL returned strings.
 *
 * When a chassis is ailased, move <product_id>.<chassis_id> directory into
 * /dev/chassis/.ca so that /dev/chassis only shows one name per chassis: a
 * <alias_id> symlink into '.ca', or a <product_id>.<chassis_id> directory.
 */
int	di_crodc_rec_linkinfo(di_cro_hdl_t h, di_cro_rec_t r, int, int,
	    char **, char **);
#define	DI_CRODC_REC_LINKINFO_STD	0x1	/* use SYS/ALIAS links */
#define	DI_CRODC_REC_LINKINFO_RAW	0x2	/* don't use SYS/ALIAS */
#define	DI_CRODC_REC_LINKINFO_ALIASLINK	0x3	/* return ALIAS setup */
#define	DI_CRODC_REC_LINKINFO_RAWSYS	0x4	/* return RAW SYS */
#define	DI_CRODC_REC_LINKINFO_SYSLINK	0x5	/* return SYS setup */

#define	DI_CRODC_DEVCHASSIS	"/dev/chassis"	/* base of namespace */
#define	DI_CRODC_SYSALIAS	"SYS"
#define	DI_CRODC_SYSALIAS_SS	"/SYS/"
#define	DI_CRODC_ALIASED_DIR	".ca"
#define	DI_CRODC_PC_SEP		"."
#define	DI_CRODC_PC_FMT		"%s" DI_CRODC_PC_SEP "%s"
#define	DI_CRODC_RESERVED_CHARS	"/"

/*
 * Private: The following di_cromk_ interfaces are for fmd(1M) cro construction
 */
typedef struct di_cromk_hdl	*di_cromk_hdl_t;

di_cromk_hdl_t	di_cromk_begin(int flags);
di_cro_rec_t	di_cromk_recadd(di_cromk_hdl_t h,
		    uint32_t rec_flag,
		    char *product_id,
		    char *chassis_id,
		    char *alias_id,
		    char *receptacle_name,
		    char *receptacle_fmri,
		    char *receptacle_type,
		    char *occupant_type,
		    char *occupant_instance,
		    char *devchassis_path,
		    char **occupant_devices,	int n_occupant_devices,
		    char **occupant_paths,	int n_occupant_paths,
		    char **occupant_compdev,	int n_occupant_compdev,
		    char *occupant_devid,
		    char *occupant_mfg,
		    char *occupant_model,
		    char *occupant_part,
		    char *occupant_serial,
		    char *occupant_firm,
		    char **occupant_misc_1,	int n_occupant_misc_1,
		    char **occupant_misc_2,	int n_occupant_misc_2,
		    char **occupant_misc_3,	int n_occupant_misc_3);
void		di_cromk_end(di_cromk_hdl_t h,
		    int		flags,
		    char	*root_server_id,
		    char	*root_product_id,
		    char	*root_chassis_id,
		    uint64_t	cna);
#define	DI_CROMK_END_COMMIT	0x00000001	/* Commit to database */
#define	DI_CROMK_END_ABANDON	0x00000002	/* Throw this db away */
void		di_cromk_cleanup();

#define	DI_CRO_DB		"cro_db"
#define	DI_CRO_DB_FILE		"/etc/dev/" DI_CRO_DB
#define	DI_CRO_DB_FILE_NEW	DI_CRO_DB_FILE ".new"
#define	DI_CRO_DB_FILE_OLD	DI_CRO_DB_FILE ".old"

/*
 * Private: di_pca_ Product-id.Chassis-id Alias-id interfaces used by
 * di_cromk_ (in the future by fmtopo for <alias-id> authority).
 */
typedef	struct	di_pca_hdl	*di_pca_hdl_t;
typedef	struct	di_pca_rec	*di_pca_rec_t;

di_pca_hdl_t	di_pca_init(int flag);
#define	DI_PCA_INIT_FLAG_PRINT			0x1
#define	DI_PCA_INIT_FLAG_ALIASES_FILE_USER	0x2
int		di_pca_sync(di_pca_hdl_t h, int flag);
#define	DI_PCA_SYNC_FLAG_COMMIT		0x1
void		di_pca_fini(di_pca_hdl_t h);

int		di_pca_rec_add(di_pca_hdl_t h,
		    char *product_id, char *chassis_id,
		    char *alias_id, char *comment);
int		di_pca_rec_remove(di_pca_hdl_t h, char *match_str);
void		di_pca_rec_print(FILE *fp, di_pca_rec_t r);

di_pca_rec_t	di_pca_rec_next(di_pca_hdl_t h, di_pca_rec_t r);
di_pca_rec_t	di_pca_rec_lookup(di_pca_hdl_t h, char *match_str);

char		*di_pca_rec_get_product_id(di_pca_rec_t r);
char		*di_pca_rec_get_chassis_id(di_pca_rec_t r);
char		*di_pca_rec_get_alias_id(di_pca_rec_t r);
char		*di_pca_rec_get_comment(di_pca_rec_t r);

int		di_pca_alias_id_used(di_pca_hdl_t h, char *alias_id);

#define	DI_PCA_SUCCESS		0
#define	DI_PCA_FAILURE		-1
#define	DI_PCA_ALIASES		"chassis_aliases"
#define	DI_PCA_ALIASES_FILE	"/etc/dev/." DI_PCA_ALIASES
#define	DI_PCA_ALIASES_FILE_USER "/etc/dev/" DI_PCA_ALIASES
#define	DI_PCA_ALIASES_FILE_TMP	DI_PCA_ALIASES_FILE ".XXXXXX" /* mkstemp(3c) */

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBDEVINFO_H */
