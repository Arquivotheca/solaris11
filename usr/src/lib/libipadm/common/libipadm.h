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
#ifndef _LIBIPADM_H
#define	_LIBIPADM_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <libnvpair.h>
#include <netinet/tcp.h>
#include <sys/stropts.h>
#include <sys/list.h>

#define	IPADM_AOBJ_USTRSIZ	32
#define	IPADM_AOBJSIZ		(LIFNAMSIZ + IPADM_AOBJ_USTRSIZ)
#define	MAXPROPVALLEN		512
#define	LOOPBACK_IF		"lo0"

/* special timeout values for dhcp operations */
#define	IPADM_DHCP_WAIT_DEFAULT	(-1)
#define	IPADM_DHCP_WAIT_FOREVER	(-2)
/*
 * Specifies that the string passed to ipadm_str2nvlist() is a string of comma
 * separated names and that each name does not have values associated with it.
 */
#define	IPADM_NORVAL		0x00000001

/* error codes */
typedef enum {
	IPADM_SUCCESS,		/* No error occurred */
	IPADM_FAILURE,		/* Generic failure */
	IPADM_INSUFF_AUTH,	/* Insufficient user authorizations */
	IPADM_PERM_DENIED,	/* Permission denied */
	IPADM_NO_BUFS,		/* No Buffer space available */
	IPADM_NO_MEMORY,	/* Insufficient memory */
	IPADM_BAD_ADDR,		/* Invalid address */
	IPADM_INVALID_IFID,	/* Invalid interface id */
	IPADM_BAD_PROTOCOL,	/* Wrong protocol family for operation */
	IPADM_DAD_FOUND,	/* Duplicate address detected */
	IPADM_OBJ_EXISTS,	/* Object already exists */
	IPADM_IF_EXISTS,	/* Interface already exists */
	IPADM_ADDROBJ_EXISTS,	/* Address object already exists */
	IPADM_ADDRCONF_EXISTS,	/* Addrconf already in progress */
	IPADM_NOSUCH_IF,	/* Interface does not exist */
	IPADM_GRP_NOTEMPTY,	/* IPMP group non-empty on unplumb */
	IPADM_INVALID_ARG,	/* Invalid argument */
	IPADM_INVALID_IFNAME,	/* Invalid interface name */
	IPADM_DLPI_FAILURE,	/* Could not open DLPI link */
	IPADM_DLPI_NOLINK,	/* Datalink does not exist */
	IPADM_DLADM_FAILURE,	/* DLADM error encountered */
	IPADM_PROP_UNKNOWN,	/* Unknown property */
	IPADM_VAL_OUT_OF_RANGE,	/* Value is outside the allowed range */
	IPADM_VAL_NOTEXISTS,	/* Value does not exist */
	IPADM_VAL_OVERFLOW,	/* Number of values exceed the allowed limit */
	IPADM_OBJ_NOTFOUND,	/* Object not found */
	IPADM_IF_INUSE,		/* Interface already in use */
	IPADM_ADDR_INUSE,	/* Address alrelady in use */
	IPADM_HOSTNAME_TO_MULTADDR, /* hostname maps to multiple IP addresses */
	IPADM_CANNOT_ASSIGN_ADDR,   /* Can't assign requested address */
	IPADM_NDPD_NOT_RUNNING,	/* in.ndpd not running */
	IPADM_NDPD_TIMEOUT,	/* Communication with in.ndpd timed out */
	IPADM_NDPD_IO_FAILURE,	/* I/O error with in.ndpd */
	IPADM_CANNOT_START_DHCP, /* Cannot start dhcpagent */
	IPADM_DHCP_IPC_FAILURE,	/* Communication with dhcpagent failed */
	IPADM_DHCP_INVALID_IF,	/* DHCP client could not run on the interface */
	IPADM_DHCP_IPC_TIMEOUT,	/* Communication with dhcpagent timed out */
	IPADM_TEMPORARY_OBJ,	/* Permanent operation on temporary object */
	IPADM_DAEMON_IPC_FAILURE, /* Cannot communicate with daemon */
	IPADM_OP_DISABLE_OBJ,	/* Operation on disable object */
	IPADM_OP_NOTSUP,	/* Operation not supported */
	IPADM_INVALID_EXCH,	/* Invalid data exchange with ipmgmtd */
	IPADM_NOT_IPMPIF,	/* Not an IPMP interface */
	IPADM_NOSUCH_IPMPIF,	/* No such IPMP interface exists */
	IPADM_ALREADY_IN_GRP,	/* Already in an IPMP group */
	IPADM_NOSUCH_UNDERIF,	/* No such underlying interface exists */
	IPADM_NOT_IN_GROUP,	/* Interface not in given IPMP group */
	IPADM_IPMPIF_NOT_ENABLED, /* IPMP interface is not enabled */
	IPADM_IPMPIF_DHCP_NOT_ENABLED, /* DHCP data address is not enabled */
	IPADM_IF_NOT_FULLY_ENABLED, /* Interface could not be fully enabled */
	/*
	 * IPADM_UNDERIF_APP_ADDRS: Underlying interface has addresses managed
	 * by external applications
	 */
	IPADM_UNDERIF_APP_ADDRS,
	/*
	 * IPADM_UNDERIF_DHCP_MANAGED: Underlying interface has addresses
	 * managed by dhcpagent(1M)
	 */
	IPADM_UNDERIF_DHCP_MANAGED,
	/*
	 * IPADM_UNDERIF_NDPD_MANAGED: Underlying interface has addresses
	 * managed by in.ndpd(1M)
	 */
	IPADM_UNDERIF_NDPD_MANAGED,
	/*
	 * IPADM_UNDERIF_UP_ADDRS: Underlying interface has addresses marked up
	 */
	IPADM_UNDERIF_UP_ADDRS,
	/*
	 * IPADM_IPMPIF_MISSING_AF: IPMP interface missing address families
	 * configured on underlying interface
	 */
	IPADM_IPMPIF_MISSING_AF,
	/*
	 * IPADM_ADDROBJ_NOT_CREATED : For one or more of the migrated data
	 * addresses in the legacy case, an address object could not be created.
	 */
	IPADM_ADDROBJ_NOT_CREATED
} ipadm_status_t;

/*
 * option flags taken by the libipadm functions
 *
 *  - IPADM_OPT_PERSIST:
 *	For all the create/delete/up/down/set/get functions,
 * 	requests to persist the configuration so that it can be
 *	re-enabled or reapplied on boot.
 *
 *  - IPADM_OPT_ACTIVE:
 *	Requests to apply configuration without persisting it and
 *	used by show-* subcommands to retrieve current values.
 *
 *  - IPADM_OPT_DEFAULT:
 *	retrieves the default value for a given property
 *
 *  - IPADM_OPT_PERM
 *	retrieves the permission for a given property
 *
 *  - IPADM_OPT_POSSIBLE
 *	retrieves the range of values for a given property
 *
 *  - IPADM_OPT_APPEND
 *	for multi-valued properties, appends a new value.
 *
 *  - IPADM_OPT_REMOVE
 *	for multi-valued properties, removes the specified value
 *
 *  - IPADM_OPT_GENPPA
 *	Used in ipadm_create_ipmp() to generate a ppa for the given IPMP
 *	interface.
 *
 *  - IPADM_OPT_ZEROADDR
 *	return :: or INADDR_ANY
 *
 *  - IPADM_OPT_RELEASE
 *	Used to release the lease on a dhcp address object
 *
 *  - IPADM_OPT_INFORM
 *	Used to perform DHCP_INFORM on a specified static address object
 *
 *  - IPADM_OPT_UP
 *	Used to bring up a static address on creation
 *
 *  - IPADM_OPT_FORCE
 *	Used in ipadm_delete_ipmp() to forcefully delete an IPMP interface
 *	even when it has underlying interfaces.
 *
 *  - IPADM_OPT_MODIFY
 *      Used to modify only the active configuration even when the
 *	configuration is persistent. Used to update an addrconf interface id.
 *
 *  - IPADM_OPT_NWAM_OVERRIDE
 *	Used by nwamd to override checking of disabled objects when creating
 *	interfaces and addresses.
 */
#define	IPADM_OPT_PERSIST	0x00000001
#define	IPADM_OPT_ACTIVE	0x00000002
#define	IPADM_OPT_DEFAULT	0x00000004
#define	IPADM_OPT_PERM		0x00000008
#define	IPADM_OPT_POSSIBLE	0x00000010
#define	IPADM_OPT_APPEND	0x00000020
#define	IPADM_OPT_REMOVE	0x00000040
#define	IPADM_OPT_GENPPA	0x00000080
#define	IPADM_OPT_ZEROADDR	0x00000100
#define	IPADM_OPT_RELEASE	0x00000200
#define	IPADM_OPT_INFORM	0x00000400
#define	IPADM_OPT_UP		0x00000800
#define	IPADM_OPT_FORCE		0x00001000
#define	IPADM_OPT_MODIFY	0x00002000
#define	IPADM_OPT_NWAM_OVERRIDE	0x00004000

/* IPADM property class */
#define	IPADMPROP_CLASS_MODULE	0x00000001	/* on 'protocol' only */
#define	IPADMPROP_CLASS_IF	0x00000002	/* on 'IP interface' only */
#define	IPADMPROP_CLASS_ADDR	0x00000004	/* on 'IP address' only */
/* protocol property that can be applied on interface too */
#define	IPADMPROP_CLASS_MODIF	(IPADMPROP_CLASS_MODULE | IPADMPROP_CLASS_IF)

/* opaque ipadm handle to libipadm functions */
struct ipadm_handle;
typedef struct ipadm_handle	*ipadm_handle_t;

/* ipadm_handle flags */
#define	IH_VRRP			0x00000001	/* Caller is VRRP */
#define	IH_LEGACY		0x00000002	/* Caller is legacy app */
#define	IH_IPMGMTD		0x00000004	/* Caller is ipmgmtd itself */
/*
 * Indicates that the operation being invoked is in 'init' context. This is
 * a library private flag.
 */
#define	IH_INIT			0x10000000

/*
 * Interface classes. These constants are committed to disk, as part of storing
 * the persistent configuration. These should not be changed without upgrading
 * the persistent configuration to match the enum values.
 */
typedef enum {
	IPADMIF_CLASS_IP,
	IPADMIF_CLASS_IPMP,
	IPADMIF_CLASS_LOOPBACK,
	IPADMIF_CLASS_VNI
} ipadm_if_class_t;

/* opaque address object structure */
typedef struct ipadm_addrobj_s	*ipadm_addrobj_t;

/* ipadm_if_info_t states */
typedef enum {
	IPADM_IFS_DOWN,		/* Interface has no UP addresses */
	IPADM_IFS_OK,		/* Interface is usable */
	IPADM_IFS_FAILED,	/* Interface has failed. */
	IPADM_IFS_OFFLINE,	/* Interface has been offlined */
	IPADM_IFS_DISABLED	/* Interface has been disabled. */
} ipadm_if_state_t;

typedef struct ipadm_iflist_s {
	list_node_t		ifn_link;
	char			ifn_name[LIFNAMSIZ];
} ipadm_ifname_t;

typedef struct ipadm_if_info_s {
	list_node_t		ifi_link;
	char			ifi_name[LIFNAMSIZ];	/* interface name */
	ipadm_if_class_t	ifi_class;		/* interface class */
	boolean_t		ifi_active;		/* active or not */
	ipadm_if_state_t	ifi_state;		/* see above */
	uint_t			ifi_cflags;		/* current flags */
	uint_t			ifi_pflags;		/* persistent flags */
	/*
	 * ifi_unders: list of underlying interfaces in the
	 * running configuration for the IPMP interface,
	 * each of type `ipadm_ifname_t'.
	 */
	list_t			ifi_unders;
	/*
	 * ifi_punders: list of underlying interfaces in the
	 * persistent configuration for the IPMP interface,
	 * each of type `ipadm_ifname_t'.
	 */
	list_t			ifi_punders;
} ipadm_if_info_t;

/* ipadm_if_info_t flags */
#define	IPADM_IFF_BROADCAST	0x00000001
#define	IPADM_IFF_MULTICAST	0x00000002
#define	IPADM_IFF_POINTOPOINT	0x00000004
#define	IPADM_IFF_VIRTUAL	0x00000008
#define	IPADM_IFF_STANDBY	0x00000010
#define	IPADM_IFF_INACTIVE	0x00000020
#define	IPADM_IFF_VRRP		0x00000040
#define	IPADM_IFF_NOACCEPT	0x00000080
#define	IPADM_IFF_IPV4		0x00000100
#define	IPADM_IFF_IPV6		0x00000200
#define	IPADM_IFF_L3PROTECT	0x00000400
#define	IPADM_IFF_UNDERIF	0x00000800

/* ipadm_addr_info_t state */
typedef enum {
	IPADM_ADDRS_DISABLED,	/* Address not in active configuration. */
	IPADM_ADDRS_DUPLICATE,	/* DAD failed. */
	IPADM_ADDRS_DOWN,	/* Address is not IFF_UP */
	IPADM_ADDRS_TENTATIVE,	/* DAD verification initiated */
	IPADM_ADDRS_OK,		/* Address is usable */
	IPADM_ADDRS_INACCESSIBLE	/* Interface has failed */
} ipadm_addr_state_t;

/* possible address types */
typedef enum  {
	IPADM_ADDR_NONE,
	IPADM_ADDR_STATIC,
	IPADM_ADDR_IPV6_ADDRCONF,
	IPADM_ADDR_DHCP
} ipadm_addr_type_t;

/* possible Client ID types */
typedef enum  {
	IPADM_CID_DEFAULT = -1,
	IPADM_CID_OTHER,
	IPADM_CID_DUID_LLT,
	IPADM_CID_DUID_LL,
	IPADM_CID_DUID_EN
} ipadm_cid_type_t;

typedef struct ipadm_addr_info_s {
	struct ifaddrs		ia_ifa;		/* list of addresses */
	char			ia_sname[NI_MAXHOST];	/* local hostname */
	char			ia_dname[NI_MAXHOST];	/* remote hostname */
	char			ia_aobjname[IPADM_AOBJSIZ];
	uint_t			ia_cflags;	/* active flags */
	uint_t			ia_pflags;	/* persistent flags */
	ipadm_addr_type_t	ia_atype;	/* see above */
	ipadm_addr_state_t	ia_state;	/* see above */
	time_t			ia_lease_begin;
	time_t			ia_lease_expire;
	time_t			ia_lease_renew;
	ipadm_cid_type_t	ia_clientid_type;
	char			*ia_clientid;
} ipadm_addr_info_t;
#define	IA_NEXT(ia)		((ipadm_addr_info_t *)(ia->ia_ifa.ifa_next))

/* ipadm_addr_info_t flags */
#define	IPADM_ADDRF_UP		0x00000001
#define	IPADM_ADDRF_UNNUMBERED	0x00000002
#define	IPADM_ADDRF_PRIVATE	0x00000004
#define	IPADM_ADDRF_TEMPORARY	0x00000008
#define	IPADM_ADDRF_DEPRECATED	0x00000010

/* open/close libipadm handle */
extern ipadm_status_t	ipadm_open(ipadm_handle_t *, uint32_t);
extern void		ipadm_close(ipadm_handle_t);

/* Check authorization for network configuration */
extern boolean_t	ipadm_check_auth(void);
/*
 * Interface mangement functions
 */
extern ipadm_status_t	ipadm_create_ip(ipadm_handle_t, const char *,
			    sa_family_t, uint32_t);
extern ipadm_status_t	ipadm_delete_ip(ipadm_handle_t, const char *,
			    sa_family_t, uint32_t);
extern ipadm_status_t	ipadm_create_vni(ipadm_handle_t, const char *,
			    sa_family_t, uint32_t);
extern ipadm_status_t	ipadm_delete_vni(ipadm_handle_t, const char *,
			    sa_family_t, uint32_t);
extern ipadm_status_t	ipadm_create_loopback(ipadm_handle_t, const char *,
			    sa_family_t, uint32_t);
extern ipadm_status_t	ipadm_delete_loopback(ipadm_handle_t, const char *,
			    sa_family_t, uint32_t);
extern ipadm_status_t	ipadm_create_ipmp(ipadm_handle_t, char *, sa_family_t,
			    uint32_t);
extern ipadm_status_t	ipadm_delete_ipmp(ipadm_handle_t, const char *,
			    sa_family_t, uint32_t);
extern ipadm_status_t	ipadm_add_ipmp(ipadm_handle_t, const char *,
			    const char *, uint32_t);
extern ipadm_status_t	ipadm_remove_ipmp(ipadm_handle_t, const char *,
			    const char *, uint32_t);
extern ipadm_status_t	ipadm_disable_if(ipadm_handle_t, const char *,
			    uint32_t);
extern ipadm_status_t	ipadm_enable_if(ipadm_handle_t, const char *, uint32_t);
extern ipadm_status_t	ipadm_if_info(ipadm_handle_t, const char *, list_t *,
			    uint32_t, int64_t);
extern void		ipadm_free_if_info(list_t *);

/*
 * Helper functions
 */
extern ipadm_status_t	ipadm_create_ipmp_implicit(ipadm_handle_t,
			    const char *);
extern ipadm_status_t	ipadm_mark_testaddrs(ipadm_handle_t, const char *);
extern ipadm_status_t	ipadm_clear_testaddrs(ipadm_handle_t, const char *);
extern ipadm_status_t	ipadm_up_addrs(ipadm_handle_t, ipadm_addr_info_t *);
extern ipadm_status_t	ipadm_down_addrs(ipadm_handle_t, const char *,
			    ipadm_addr_info_t **);
extern ipadm_status_t	ipadm_wait_app_addrs(ipadm_handle_t, const char *);
extern ipadm_status_t	ipadm_migrate_dataaddrs(ipadm_handle_t,
			    const char *, ipadm_addr_info_t *);

/*
 * Address management functions
 */
extern ipadm_status_t	ipadm_create_addr(ipadm_handle_t, ipadm_addrobj_t,
			    uint32_t);
extern ipadm_status_t	ipadm_delete_addr(ipadm_handle_t, const char *,
			    uint32_t);
extern ipadm_status_t	ipadm_disable_addr(ipadm_handle_t, const char *,
			    uint32_t);
extern ipadm_status_t	ipadm_enable_addr(ipadm_handle_t, const char *,
			    uint32_t);
extern ipadm_status_t	ipadm_up_addr(ipadm_handle_t, const char *,
			    uint32_t);
extern ipadm_status_t	ipadm_down_addr(ipadm_handle_t, const char *,
			    uint32_t);
extern ipadm_status_t	ipadm_refresh_addr(ipadm_handle_t, const char *,
			    uint32_t);
extern ipadm_status_t	ipadm_addr_info(ipadm_handle_t, const char *,
			    ipadm_addr_info_t **, uint32_t, int64_t);
extern void		ipadm_free_addr_info(ipadm_addr_info_t *);

/* Functions related to creating/deleting/modifying opaque address object */
extern ipadm_status_t	ipadm_create_addrobj(ipadm_addr_type_t, const char *,
			    ipadm_addrobj_t *);
extern void		ipadm_destroy_addrobj(ipadm_addrobj_t);
extern ipadm_status_t   ipadm_get_aobjname(ipadm_addrobj_t, char *,
			    size_t);
extern ipadm_status_t	ipadm_get_interface_id(ipadm_handle_t, const char *,
			    struct sockaddr_in6 *, int *, uint32_t);

extern ipadm_status_t	ipadm_update_interface_id(ipadm_handle_t,
			    ipadm_addrobj_t, uint32_t);

/* Functions to set fields in addrobj for static addresses */
extern ipadm_status_t	ipadm_set_addr(ipadm_addrobj_t, const char *,
			    sa_family_t);
extern ipadm_status_t	ipadm_set_dst_addr(ipadm_addrobj_t, const char *,
			    sa_family_t);
extern ipadm_status_t   ipadm_get_addr(const ipadm_addrobj_t,
			    struct sockaddr_storage *);
extern ipadm_status_t	ipadm_update_addr(ipadm_handle_t, ipadm_addrobj_t,
			    uint32_t);

/* Functions to set fields in addrobj for IPv6 addrconf */
extern ipadm_status_t	ipadm_set_interface_id(ipadm_addrobj_t, const char *);
extern ipadm_status_t	ipadm_set_dst_interface_id(ipadm_addrobj_t,
			    const char *);
extern ipadm_status_t	ipadm_set_stateless(ipadm_addrobj_t, boolean_t);
extern ipadm_status_t	ipadm_set_stateful(ipadm_addrobj_t, boolean_t);

/* Functions to set fields in addrobj for DHCP */
extern ipadm_status_t	ipadm_set_primary(ipadm_addrobj_t, boolean_t);
extern ipadm_status_t	ipadm_set_wait_time(ipadm_addrobj_t, int32_t);
extern ipadm_status_t	ipadm_set_reqhost(ipadm_addrobj_t, const char *);

/*
 * Property management functions
 */
/* call back function for the property walker */
typedef boolean_t	ipadm_prop_wfunc_t(void *, const char *, uint_t);
extern ipadm_status_t	ipadm_walk_proptbl(uint_t, uint_t, ipadm_prop_wfunc_t *,
			    void *);
extern ipadm_status_t	ipadm_walk_prop(const char *, uint_t, uint_t,
			    ipadm_prop_wfunc_t *, void *);

/* Interface property management - set, reset and get */
extern ipadm_status_t	ipadm_set_ifprop(ipadm_handle_t, const char *,
			    const char *, const char *, uint_t, uint_t);
extern ipadm_status_t	ipadm_get_ifprop(ipadm_handle_t, const char *,
			    const char *, char *, uint_t *, uint_t, uint_t);

/* Address property management - set, reset and get */
extern ipadm_status_t	ipadm_set_addrprop(ipadm_handle_t, const char *,
			    const char *, const char *, uint_t);
extern ipadm_status_t	ipadm_get_addrprop(ipadm_handle_t, const char *, char *,
			    uint_t *, const char *, uint_t);

/* Protoocl property management - set, reset and get */
extern ipadm_status_t	ipadm_set_prop(ipadm_handle_t, const char *,
			    const char *, uint_t, uint_t);
extern ipadm_status_t	ipadm_get_prop(ipadm_handle_t, const char *, char *,
			    uint_t *, uint_t, uint_t);

/*
 * miscellaneous helper functions.
 */
extern const char 	*ipadm_status2str(ipadm_status_t);
extern int		ipadm_str2nvlist(const char *, nvlist_t **, uint_t);
extern size_t		ipadm_nvlist2str(nvlist_t *, char *, size_t);
extern char		*ipadm_proto2str(uint_t);
extern uint_t		ipadm_str2proto(const char *);
extern ipadm_status_t	ipadm_open_arp_on_udp(const char *, int *);
extern int		ipadm_legacy2new_propname(const char *, char *,
			    uint_t, uint_t *);
extern int		ipadm_new2legacy_propname(const char *, char *,
			    uint_t, uint_t);
extern boolean_t	ipadm_is_vni(const char *);
extern boolean_t	ipadm_is_loopback(const char *);
extern boolean_t	ipadm_is_ip(ipadm_handle_t, const char *);
extern boolean_t	ipadm_is_ipmp(ipadm_handle_t, const char *);

/*
 * Congestion control
 */
typedef boolean_t cong_db_func_t(void *, nvlist_t *, const char *, const char *,
			    const char *);

extern ipadm_status_t	ipadm_cong_walk_db(ipadm_handle_t, nvlist_t *,
			    cong_db_func_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBIPADM_H */
