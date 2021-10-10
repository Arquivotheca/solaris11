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

#ifndef _LIBIPADM_IMPL_H
#define	_LIBIPADM_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/socket.h>
#include <net/if.h>
#include <libipadm.h>
#include <libdladm.h>
#include <ipadm_ipmgmt.h>
#include <inet/tunables.h>
#include <netinet/in.h>
#include <pthread.h>
#include <libinetutil.h>
#include <libsocket_priv.h>
#include <libuutil.h>

#define	IPADM_STRSIZE		256
#define	IPADM_ONSTR		"on"
#define	IPADM_OFFSTR		"off"
#define	IPADM_LOGICAL_SEP	':'

#define	IPADM_OTHER_AF(af)	((af) == AF_INET ? AF_INET6 : AF_INET)
#define	IPADM_SOCK(iph, af)	((af) == AF_INET ? \
				    (iph)->ih_sock : (iph)->ih_sock6)
#define	IPADM_IS_V4(flags)	(((flags) & IFF_IPV4) ? B_TRUE : B_FALSE)
#define	IPADM_IS_V6(flags)	(((flags) & IFF_IPV6) ? B_TRUE : B_FALSE)
#define	IPADM_IFF2AF(flags)	(IPADM_IS_V4((flags)) ? AF_INET : \
				    (IPADM_IS_V6((flags)) ? AF_INET6 : \
				    AF_UNSPEC))

/* mask for flags accepted by libipadm functions */
#define	IPADM_COMMON_OPT_MASK	(IPADM_OPT_ACTIVE | IPADM_OPT_PERSIST | \
				    IPADM_OPT_FORCE)

/* Opaque library handle */
struct ipadm_handle {
	int		ih_sock;	/* socket to interface */
	int		ih_sock6;	/* socket to interface */
	int		ih_door_fd;	/* door descriptor to ipmgmtd */
	int		ih_rtsock;	/* routing socket */
	dladm_handle_t	ih_dh;		/* handle to libdladm library */
	uint32_t	ih_flags;	/* internal flags */
	pthread_mutex_t	ih_lock;	/* lock to set door_fd */
	zoneid_t	ih_zoneid;	/* zoneid where handle was opened */
};

struct ipadm_addrobj_s {
	char 			ipadm_ifname[LIFNAMSIZ];
	int32_t			ipadm_lifnum;
	char			ipadm_aobjname[IPADM_AOBJSIZ];
	ipadm_addr_type_t	ipadm_atype;
	uint32_t		ipadm_flags;
	sa_family_t		ipadm_af;
	union {
		struct {
			char			ipadm_ahname[MAXNAMELEN];
			struct sockaddr_storage	ipadm_addr;
			uint32_t		ipadm_prefixlen;
			char			ipadm_dhname[MAXNAMELEN];
			struct sockaddr_storage ipadm_dstaddr;
		} ipadm_static_addr_s;
		struct {
			struct sockaddr_in6	ipadm_intfid;
			uint32_t		ipadm_intfidlen;
			struct sockaddr_in6	ipadm_dintfid;
			boolean_t		ipadm_stateless;
			boolean_t		ipadm_stateful;
		} ipadm_ipv6_intfid_s;
		struct {
			boolean_t		ipadm_primary;
			int32_t			ipadm_wait;
			char			ipadm_reqhost[MAXNAMELEN];
		} ipadm_dhcp_s;
	} ipadm_addr_u;
};

#define	ipadm_static_addr	ipadm_addr_u.ipadm_static_addr_s.ipadm_addr
#define	ipadm_static_aname	ipadm_addr_u.ipadm_static_addr_s.ipadm_ahname
#define	ipadm_static_prefixlen	ipadm_addr_u.ipadm_static_addr_s.ipadm_prefixlen
#define	ipadm_static_dst_addr	ipadm_addr_u.ipadm_static_addr_s.ipadm_dstaddr
#define	ipadm_static_dname	ipadm_addr_u.ipadm_static_addr_s.ipadm_dhname
#define	ipadm_intfid		ipadm_addr_u.ipadm_ipv6_intfid_s.ipadm_intfid
#define	ipadm_intfidlen		ipadm_addr_u.ipadm_ipv6_intfid_s.ipadm_intfidlen
#define	ipadm_dintfid		ipadm_addr_u.ipadm_ipv6_intfid_s.ipadm_dintfid
#define	ipadm_stateless		ipadm_addr_u.ipadm_ipv6_intfid_s.ipadm_stateless
#define	ipadm_stateful		ipadm_addr_u.ipadm_ipv6_intfid_s.ipadm_stateful
#define	ipadm_primary		ipadm_addr_u.ipadm_dhcp_s.ipadm_primary
#define	ipadm_wait		ipadm_addr_u.ipadm_dhcp_s.ipadm_wait
#define	ipadm_reqhost		ipadm_addr_u.ipadm_dhcp_s.ipadm_reqhost

/*
 * Structure used when moving data address objects from underlying interface
 * to an IPMP interface.
 */
typedef struct ipadm_migrate_addr_s {
	/*
	 * im_link : link to adjacent objects in the list
	 */
	list_node_t	im_link;
	/*
	 * im_aobjname : Address object name on underif to be removed.
	 */
	char		im_aobjname[IPADM_AOBJSIZ];
	/*
	 * im_addr : Data address that was migrated from underif to ipmpif.
	 */
	struct sockaddr_storage	im_addr;
	/*
	 * im_lifnum : Logical interface number on the underif from which
	 * the data address was moved.
	 */
	uint32_t	im_lifnum;
} ipadm_migrate_addr_t;

/*
 * Structure used while waiting for addresses that are externally managed
 * by dhcpagent(1M) and in.ndpd(1M) to be removed from the underlying
 * interfaces.
 */
typedef struct {
	const char	*ia_app;
	uint64_t	ia_flag;
	ipadm_status_t	ia_status;
	uint_t		ia_tries;
} ipadm_appflags_t;

/*
 * Operations on an IPMP interface object.
 */
typedef enum {
	IPADM_IPMPOP_ADD,
	IPADM_IPMPOP_REMOVE
} ipadm_ipmpop_t;

/*
 * Data structures and callback functions related to property management
 */
struct ipadm_prop_desc;
typedef struct ipadm_prop_desc ipadm_prop_desc_t;

/* property set() callback */
typedef ipadm_status_t	ipadm_pd_setf_t(ipadm_handle_t, const void *,
    ipadm_prop_desc_t *, const void *, uint_t, uint_t);

/* property get() callback */
typedef ipadm_status_t	ipadm_pd_getf_t(ipadm_handle_t, const void *,
    ipadm_prop_desc_t *, char *, uint_t *, uint_t, uint_t);

struct ipadm_prop_desc {
	char		*ipd_name;	/* property name */
	uint_t		ipd_class; 	/* prop. class - global/perif/both */
	uint_t		ipd_proto;	/* protocol to which property belongs */
	uint_t		ipd_flags;	/* see below */
	ipadm_pd_setf_t	*ipd_set;	/* set callback function */
	ipadm_pd_getf_t	*ipd_get_range;	/* get range callback function */
	ipadm_pd_getf_t	*ipd_get;	/* get value callback function */
};

/* ipd_flags values */
#define	IPADMPROP_MULVAL	0x00000001	/* property multi-valued */
/*
 * IPADMPROP_GETPERSIST: property has a separate function callback to get
 *			 persistent value.
 */
#define	IPADMPROP_GETPERSIST	0x00000002

/* property is persisted via SMF, rather than ipadm database */
#define	IPADMPROP_SMFPERSIST	0x00000004

/* Internal flags for use in ipd_get callback function */
#define	MOD_PROP_PERSIST	0x80000000	/* get persistent value */

extern ipadm_prop_desc_t	ipadm_addrprop_table[];
extern ipadm_pd_getf_t		i_ipadm_get_onoff;

/* libipadm.c */
extern ipadm_status_t	i_ipadm_get_flags(ipadm_handle_t, const char *,
			    sa_family_t, uint64_t *);
extern ipadm_status_t	i_ipadm_set_flags(ipadm_handle_t, const char *,
			    sa_family_t, uint64_t, uint64_t);
extern ipadm_status_t	i_ipadm_enable_if(ipadm_handle_t, const char *,
			    nvlist_t *);
extern ipadm_status_t	i_ipadm_init_ifobj(ipadm_handle_t, const char *,
			    const char *, nvlist_t *);
extern ipadm_status_t	i_ipadm_init_addrobj(ipadm_handle_t, nvlist_t *);
extern ipadm_status_t	i_ipadm_addr_persist(ipadm_handle_t,
			    const ipadm_addrobj_t, boolean_t, uint32_t);
extern ipadm_status_t	i_ipadm_delete_addr(ipadm_handle_t, ipadm_addrobj_t);
extern int		i_ipadm_strioctl(int, int, char *, int);
extern ipadm_status_t	i_ipadm_get_groupname(ipadm_handle_t, const char *,
			    char *, size_t);
extern boolean_t	i_ipadm_is_under_ipmp(ipadm_handle_t, const char *);
extern boolean_t	i_ipadm_is_6to4(ipadm_handle_t, const char *);
extern boolean_t	i_ipadm_validate_ifname(ipadm_handle_t, const char *);
extern ipadm_status_t	ipadm_errno2status(int);
extern int		ipadm_door_call(ipadm_handle_t, void *, size_t, void *,
			    size_t);
extern int		ipadm_door_dyncall(ipadm_handle_t, void *, size_t,
			    void **, size_t);
extern boolean_t 	ipadm_if_enabled(ipadm_handle_t, const char *,
			    sa_family_t);
extern boolean_t	i_ipadm_is_legacy(ipadm_handle_t);
extern sa_family_t	i_ipadm_get_active_af(ipadm_handle_t, const char *);
extern uint_t		i_ipadm_get_pflags(ipadm_handle_t, const char *);
extern ipadm_status_t	i_ipadm_validate_ifcreate(ipadm_handle_t, const char *,
			    sa_family_t, uint32_t);
extern ipadm_status_t	i_ipadm_get_groupname(ipadm_handle_t, const char *,
			    char *, size_t);

/* ipadm_ndpd.c */
extern ipadm_status_t	i_ipadm_create_linklocal(ipadm_handle_t,
			    ipadm_addrobj_t);
extern ipadm_status_t	i_ipadm_create_ipv6addrs(ipadm_handle_t,
			    ipadm_addrobj_t, uint32_t);
extern ipadm_status_t	i_ipadm_delete_ipv6addrs(ipadm_handle_t,
			    ipadm_addrobj_t);
extern ipadm_status_t	i_ipadm_set_linklocal(ipadm_handle_t, ipadm_addrobj_t);
extern ipadm_status_t	i_ipadm_disable_autoconf(const char *);
extern ipadm_status_t	i_ipadm_enable_autoconf(const char *);
extern ipadm_status_t	i_ipadm_send_ndpd_cmd(const char *,
			    const struct ipadm_addrobj_s *, int);

/* ipadm_persist.c */
extern ipadm_status_t	i_ipadm_add_ipaddr2nvl(nvlist_t *, ipadm_addrobj_t);
extern ipadm_status_t	i_ipadm_add_ip6addr2nvl(nvlist_t *, ipadm_addrobj_t);
extern ipadm_status_t	i_ipadm_add_intfid2nvl(nvlist_t *, ipadm_addrobj_t);
extern ipadm_status_t	i_ipadm_add_dhcp2nvl(nvlist_t *, boolean_t, int32_t);

/* ipadm_prop.c */
extern ipadm_status_t	i_ipadm_persist_propval(ipadm_handle_t,
			    ipadm_prop_desc_t *, const char *, const void *,
			    uint_t);
extern ipadm_status_t	i_ipadm_get_persist_propval(ipadm_handle_t,
			    ipadm_prop_desc_t *, char *, uint_t *,
			    const void *);

extern ipadm_status_t	i_ipadm_getprop_common(ipadm_handle_t, const char *,
			    const char *, char *, uint_t *, uint_t, uint_t);

/* ipadm_addr.c */
extern void		i_ipadm_init_addr(ipadm_addrobj_t, const char *,
			    const char *, ipadm_addr_type_t);
extern ipadm_status_t	i_ipadm_merge_prefixlen_from_nvl(nvlist_t *, nvlist_t *,
			    const char *);
extern ipadm_status_t	i_ipadm_get_addrobj(ipadm_handle_t, ipadm_addrobj_t);
extern ipadm_status_t	i_ipadm_enable_static(ipadm_handle_t, const char *,
			    nvlist_t *, sa_family_t);
extern ipadm_status_t	i_ipadm_enable_dhcp(ipadm_handle_t, const char *,
			    nvlist_t *);
extern ipadm_status_t	i_ipadm_enable_addrconf(ipadm_handle_t, const char *,
			    nvlist_t *);
extern void		i_ipadm_addrobj2lifname(ipadm_addrobj_t, char *, int);
extern ipadm_status_t	i_ipadm_nvl2in6_addr(nvlist_t *, char *,
			    in6_addr_t *);
extern ipadm_status_t	i_ipadm_get_lif2addrobj(ipadm_handle_t, const char *,
			    sa_family_t, ipadm_addrobj_t);
extern ipadm_status_t	i_ipadm_lookupadd_addrobj(ipadm_handle_t,
			    ipadm_addrobj_t);
extern ipadm_status_t	i_ipadm_setlifnum_addrobj(ipadm_handle_t,
			    ipadm_addrobj_t);
extern ipadm_status_t	i_ipadm_do_addif(ipadm_handle_t, ipadm_addrobj_t);
extern ipadm_status_t	i_ipadm_delete_addrobj(ipadm_handle_t,
			    const ipadm_addrobj_t, uint32_t);
extern boolean_t	i_ipadm_name2atype(const char *, sa_family_t *,
			    ipadm_addr_type_t *);
extern ipadm_status_t	i_ipadm_resolve_addr(const char *, sa_family_t,
			    struct sockaddr_storage *);
extern ipadm_status_t	i_ipadm_pd2permstr(ipadm_prop_desc_t *, char *,
			    uint_t *);
extern ipadm_status_t	i_ipadm_create_ipv6_on_underif(ipadm_handle_t,
			    const char *);
extern ipadm_status_t	i_ipadm_migrate_addrs(ipadm_handle_t, const char *,
			    list_t *, const char *);

/* ipadm_if.c */
extern ipadm_status_t	i_ipadm_create_loopback(ipadm_handle_t, const char *,
			    sa_family_t, uint32_t);
extern ipadm_status_t	i_ipadm_create_if(ipadm_handle_t, char *, sa_family_t,
			    ipadm_if_class_t, uint32_t);
extern ipadm_status_t	i_ipadm_delete_if(ipadm_handle_t, const char *,
			    sa_family_t af, ipadm_if_class_t, uint32_t);
extern ipadm_status_t	i_ipadm_disable_if(ipadm_handle_t, const char *,
			    uint32_t);
extern ipadm_status_t	i_ipadm_plumb_if(ipadm_handle_t, char *, sa_family_t,
			    ipadm_if_class_t, uint32_t);
extern ipadm_status_t	i_ipadm_plumb_lif(ipadm_handle_t, const char *,
			    sa_family_t, ipadm_if_class_t);
extern ipadm_status_t	i_ipadm_unplumb_if(ipadm_handle_t, const char *,
			    sa_family_t, uint32_t);
extern ipadm_status_t	i_ipadm_unplumb_lif(ipadm_handle_t, const char *,
			    sa_family_t, uint32_t);
extern ipadm_status_t	i_ipadm_get_db_if(ipadm_handle_t, const char *,
			    nvlist_t **);
extern ipadm_status_t	i_ipadm_persist_if_info(ipadm_handle_t, const char *,
			    list_t *);
extern boolean_t	i_ipadm_if_pexists(ipadm_handle_t, const char *,
			    sa_family_t);
extern ipadm_status_t	i_ipadm_delete_ifobj(ipadm_handle_t, const char *,
			    sa_family_t, boolean_t);
extern int		i_ipadm_get_lnum(const char *);
extern void		i_ipadm_get_ifname(const char *, char *, size_t);
extern boolean_t	i_ipadm_is_if_down(char *, struct ifaddrs *);
extern boolean_t	i_ipadm_ipmp_pexists(ipadm_handle_t, const char *);
extern ipadm_status_t	i_ipadm_get_persist_ipmpif(ipadm_handle_t, const char *,
			    char *, size_t);
extern ipadm_status_t	i_ipadm_add_ipmp(ipadm_handle_t, const char *,
			    const char *, const char *, uint32_t);
extern ipadm_status_t	i_ipadm_add_ipmp_bringup_underif(ipadm_handle_t,
			    const char *, sa_family_t);

/* ipadm_cong.c */
extern ipadm_status_t	ipadm_cong_get_algs(char *, uint_t *, uint_t,
			    boolean_t);
extern boolean_t	ipadm_cong_is_privprop(const char *, char **);
extern ipadm_status_t	ipadm_cong_get_persist_propval(const char *,
			    const char *, char *, uint_t *, const char *);
extern ipadm_status_t	ipadm_cong_persist_propval(const char *,
			    const char *, const char *, const char *, uint_t);
extern ipadm_status_t	ipadm_cong_smf_set_state(const char *, uint_t,
			    uint_t);
extern void		ipadm_cong_smf_disable_nondef(const char *, uint_t,
			    uint_t);
extern void		ipadm_cong_list_sort(char *);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBIPADM_IMPL_H */
