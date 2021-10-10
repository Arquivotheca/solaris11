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
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_LDAP_COMMON_H
#define	_LDAP_COMMON_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <ctype.h>
#include <nss_dbdefs.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <lber.h>
#include <ldap.h>
#include <pwd.h>
#include <sys/avl.h>
#include <stddef.h>	/* offsetof */
#include "ns_sldap.h"

#define	_ALIASES		"aliases"
#define	_AUTOMOUNT		"automount"
#define	_AUTHATTR		"auth_attr"
#define	_AUUSER			"audit_user"
#define	_BOOTPARAMS		"bootparams"
#define	_DEFAULT		"default"
#define	_ETHERS			"ethers"
#define	_EXECATTR		"exec_attr"
#define	_GROUP			"group"
#define	_PROJECT		"project"
#define	_HOSTS			"hosts"
#define	_HOSTS6			"hosts"
#define	_NETGROUP		"netgroup"
#define	_NETMASKS		"netmasks"
#define	_NETWORKS		"networks"
#define	_PASSWD			"passwd"
#define	_PRINTERS		"printers"
#define	_PROFATTR		"prof_attr"
#define	_PROTOCOLS		"protocols"
#define	_PUBLICKEY		"publickey"
#define	_RPC			"rpc"
#define	_SERVICES		"services"
#define	_SHADOW			"shadow"
#define	_USERATTR		"user_attr"
#define	_TNRHDB			"tnrhdb"
#define	_TNRHTP			"tnrhtp"

#define	_F_GETPWNAM		"(&(objectClass=posixAccount)(uid=%s))"
#define	_F_GETPWNAM_SSD		"(&(%%s)(uid=%s))"

#define	NSS_STR_PARSE_NO_ADDR	(NSS_STR_PARSE_ERANGE + 100)
#define	NSS_STR_PARSE_NO_RESULT (NSS_STR_PARSE_ERANGE + 101)

#define	DOTTEDSUBDOMAIN(string) \
	((string != NULL) && (strchr(string, '.') != NULL))
#define	SEARCHFILTERLEN		256

#define	_NO_VALUE		""

#define	TEST_AND_ADJUST(len, buffer, buflen, label) \
	    /* Use '>=' to ensure there is at least one byte left for '\0' */ \
	    if (len >= buflen || len < 0) { \
		nss_result = NSS_STR_PARSE_ERANGE; \
		goto label; \
	    } \
	    /* Adjust pointer and available buffer length */ \
	    buffer += len; \
	    buflen -= len;

/*
 * We need to use UID_NOBODY and GID_NOBODY as strings. Therefore we use
 * snprintf to convert [U|G]ID_NOBODY into a string. The target buffer
 * size was chosen as 21 to allow the largest 64-bit number to be stored
 * as string in it. Right now uid_t and gid_t are 32-bit so we don't
 * really need 21 characters but it does allow for future expansion
 * without having to modify this code.
 */
#define	NOBODY_STR_LEN	21

/*
 * nss_ldap may return DN of an ldap entry as optional data.
 * NSS_LDAP_DN_TAG is used to tag such data.
 */
#define	NSS_LDAP_DN_TAG		"#dn:"
#define	NSS_LDAP_DN_TAG_LEN	4

/*
 * A generic list implemented as a AVL tree. Each AVL node has
 * a key and a key length field.
 */
typedef avl_tree_t _nss_ldap_list_t;
typedef struct _nss_ldap_list_entry {
	avl_node_t	avl_link;	/* libavl requirement */
	char		*key;
	size_t		keylen;
} _nss_ldap_list_entry_t;

typedef enum {
	NSS_LDAP_LIST_SUCCESS	= 0,
	NSS_LDAP_LIST_MEMORY	= 1,
	NSS_LDAP_LIST_EXISTED	= 2,
	NSS_LDAP_LIST_NOKEY	= 3,
	NSS_LDAP_LIST_NOLIST	= 4,
	NSS_LDAP_LIST_ERANGE	= 5
} nss_ldap_list_rc_t;

/* nss_ldap's own nscd caches have the following states */
typedef enum {
	NSS_LDAP_CACHE_UNINITED	= 0,
	NSS_LDAP_CACHE_NOTNSCD	= 1,
	NSS_LDAP_CACHE_INITED	= 2,
	NSS_LDAP_CACHE_FAILED	= 3
} nss_ldap_cache_state_t;

/*
 * Superset the nss_backend_t abstract data type. This ADT has
 * been extended to include ldap associated data structures.
 */

typedef struct ldap_backend *ldap_backend_ptr;
typedef nss_status_t (*ldap_backend_op_t)(ldap_backend_ptr, void *);
typedef int (*fnf)(ldap_backend_ptr be, nss_XbyY_args_t *argp);

typedef enum {
	NSS_LDAP_DB_NONE	= 0,
	NSS_LDAP_DB_PUBLICKEY	= 1,
	NSS_LDAP_DB_ETHERS	= 2
} nss_ldap_db_type_t;

struct ldap_backend {
	ldap_backend_op_t	*ops;
	nss_dbop_t		nops;
	char			*tablename;
	void			*enumcookie;
	char			*filter;
	char			*sortattr;
	int			setcalled;
	const char		**attrs;
	ns_ldap_result_t	*result;
	const char		**extra_info_attr;
	ns_ldap_entry_t		*extra_info;
	fnf			ldapobj2str;
	void			*netgroup_cookie;
	void			*services_cookie;
	char			*toglue;
	char			*buffer;
	int			buflen;
	nss_ldap_db_type_t	db_type;
	boolean_t		have_dn;
};

extern nss_status_t	_nss_ldap_destr(ldap_backend_ptr be, void *a);
extern nss_status_t	_nss_ldap_endent(ldap_backend_ptr be, void *a);
extern nss_status_t	_nss_ldap_setent(ldap_backend_ptr be, void *a);
extern nss_status_t	_nss_ldap_getent(ldap_backend_ptr be, void *a);
nss_backend_t		*_nss_ldap_constr(ldap_backend_op_t ops[], int nops,
			char *tablename, const char **attrs, fnf ldapobj2str);
extern nss_status_t	_nss_ldap_nocb_lookup(ldap_backend_ptr be,
			nss_XbyY_args_t *argp, char *database,
			char *searchfilter, char *domain,
			int (*init_filter_cb)(
				const ns_ldap_search_desc_t *desc,
				char **realfilter, const void *userdata),
			const void *userdata);
extern nss_status_t	_nss_ldap_lookup(ldap_backend_ptr be,
			nss_XbyY_args_t *argp, char *database,
			char *searchfilter, char *domain,
			int (*init_filter_cb)(
				const ns_ldap_search_desc_t *desc,
				char **realfilter, const void *userdata),
			const void *userdata);
extern void		_clean_ldap_backend(ldap_backend_ptr be);

extern ns_ldap_attr_t *getattr(ns_ldap_result_t *result, int i);
extern const char *_strip_quotes(char *ipaddress);
extern int propersubdomain(char *domain, char *subdomain);
extern int chophostdomain(char *string, char *host, char *domain);
extern char *_get_domain_name(char *cdn);
extern int _merge_SSD_filter(const ns_ldap_search_desc_t *desc,
	char **realfilter, const void *userdata);
extern int _ldap_filter_name(char *filter_name, const char *name,
	int filter_name_size);

extern void _nss_services_cookie_free(void **cookieP);
extern nss_status_t switch_err(int rc, ns_ldap_error_t *error);

extern nss_ldap_list_rc_t nss_ldap_list_add(_nss_ldap_list_t **list,
    const char *key);
extern void nss_ldap_list_free(_nss_ldap_list_t **list);
extern nss_ldap_list_rc_t nss_ldap_list_dump(_nss_ldap_list_t **list,
    char **buffer, int *buflen);
extern ns_ldap_server_type_t _nss_ldap_get_server_type(ns_ldap_entry_t *entry,
    char **server_type);

#ifdef DEBUG
extern int printresult(ns_ldap_result_t *result);
#endif /* DEBUG */

#ifdef	__cplusplus
}
#endif

#endif	/* _LDAP_COMMON_H */
