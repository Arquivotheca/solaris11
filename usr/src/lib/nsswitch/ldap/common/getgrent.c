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
 * Copyright (c) 1999, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdlib.h>
#include <grp.h>
#include <note.h>
#include "ldap_common.h"
#include <alloca.h> /* alloca */

/* String which may need to be removed from beginning of group password */
#define	_CRYPT		"{CRYPT}"
#define	_NO_PASSWD_VAL	""

/* AD-specific, used in search filter to resolve group memberships. */
#define	LDAP_MATCHING_RULE_IN_CHAIN_OID	"1.2.840.113556.1.4.1941"

/* Group attributes filters */
#define	_G_NAME		"cn"
#define	_G_UID		"uid"
#define	_G_GIDNUMBER	"gidNumber"
#define	_G_PASSWD	"userPassword"
#define	_G_MEMBERUID	"memberUid"
#define	_G_MEMBER	"member"
#define	_G_UNIQUEMEMBER	"uniqueMember"
#define	_G_MEMBEROF	"memberOf"
#define	_G_MEMBEROF_ODSEE "isMemberOf"
#define	_G_OBJECTCLASS	"objectClass"

#define	_OC_ACCOUNT	"posixAccount"
#define	_OC_GROUP	"posixGroup"

#define	_F_GETGRNAM	"(&(objectClass=posixGroup)(cn=%s))"
#define	_F_GETGRNAM_SSD	"(&(%%s)(cn=%s))"
#define	_F_GETGRGID	"(&(objectClass=posixGroup)(gidNumber=%u))"
#define	_F_GETGRGID_SSD	"(&(%%s)(gidNumber=%u))"
#define	_F_GETGRMEM	"(&(objectClass=posixGroup)(memberUid=%s))"
#define	_F_GETGRMEM_SSD	"(&(%%s)(memberUid=%s))"

/*
 * These filters are used to get all the containing groups (immediate parents)
 * of a user by searching with memberUid (user name) and member/uniqueMember
 * DNs (user's full DN) in just one ldap search request. The reason why
 * the uniqueMember part is repeated is to cover different servers' behavior.
 * Some allow a wild card search to match DNs with optional unique identifier
 * and some would only match DN exactly (i.e., optional unique ID not allowed).
 */
#define	_UID_MBR_UMBR	"(member=%s)(uniqueMember=%s)(uniqueMember=%s*)"
#define	_MUID_MBRS	"(|(memberUid=%s)" _UID_MBR_UMBR ")"
#define	_F_GETGRUSR	"(&(objectClass=posixGroup)" _MUID_MBRS ")"
#define	_F_GETGRUSR_SSD	"(&(%%s)" _MUID_MBRS ")"

/*
 * These filters are used to get all the containing groups (immediate parents)
 * of a group by searching with member and uniqueMember DNs (user's full DN)
 * in just one ldap search request. The reason why the uniqueMember part is
 * repeated is the same as the above filters.
 */
#define	_CN_MBR_UMBR	"(|(member=%s)(uniqueMember=%s)(uniqueMember=%s*))"
#define	_F_GETGRGRP	"(&(objectClass=posixGroup)" _CN_MBR_UMBR ")"
#define	_F_GETGRGRP_SSD "(&(%%s)" _CN_MBR_UMBR ")"

/*
 * The next two filters are used to retrieve entries from AD. Both cause
 * server side group expansion. The first one asks for all group member
 * entries to be returned in just one search. The second one requests
 * all parent groups (including nested ones) to be returned in one search.
 */
#define	_F_GETGRMEM_BL	"(memberOf:" LDAP_MATCHING_RULE_IN_CHAIN_OID ":=%s)"
#define	_F_GETGRMOF_BL	"(member:" LDAP_MATCHING_RULE_IN_CHAIN_OID ":=%s)"

/* Each reallocation adds _PSEARCH_BUF_BLK more bytes to a packed buffer */
#define	_PSEARCH_BUF_BLK	(16 * 1024)

/*
 * Max. number of recursive calls to go down to find child groups
 * or to go up to find all the parent groups. This limit only
 * affects the users who use the ODSEE or OpenLDAP servers.
 * AD is not affected due to the LDAP_MATCHING_RULE_IN_CHAIN
 * server-side group expansion.
 */
#define	DN2UID_MAX_RECURSIVE_CALL	20
#define	MEMBEROF_MAX_RECURSIVE_CALL	20

#define	_OC_ACCOUNT_T	1
#define	_OC_GROUP_T	2

/*
 * The values of the member and uniqueMember attributes are in DN
 * syntax, and specify either user members or group members. If group,
 * then they are nested groups. To evaluate group membership that is
 * defined using the member and/or uniqueMember attributes, nss_ldap
 * needs to do dn2uid searches (by calling __ns_ldap_read_dn) with the
 * member DNs to obtain the names of the user members or the DNs of
 * the nested groups. For nested groups, the dn2uid searches will be
 * done recursively until all members have been resolved to user names
 * or until the maximum level of recursion has been reached. To
 * improve performance, the dn2uid search results need to be cached.
 *
 * nss_ldap will resolve the group membership specified by any or all
 * of the memberUid, member, and uniqueMember attributes. The result
 * will be that a group has membership that is the union of all three
 * with duplicates removed.
 *
 * A dn2uid search with a user DN would return the name of the user.
 * A dn2uid search with a group DN may return a series of user names
 * a series of user DNs (user members of the group), and/or a series
 * of group DNs (group members of the group).
 *
 * The result of dn2uid search is placed in the data section of the
 * packed buffer. See below where the nscd dn2uid cache is described
 * about how the nscd is tapped into to cache the dn2uid search results.
 *
 * The packed result of the dn2uid search has up to four sections:
 *  - a structure, _dn2uid_res_t
 *  - a series of user names
 *  - a base DN
 *  - a series of (short or full user/group) DNs
 *
 *  The _dn2uid_res_t structure has the following elements:
 *    -- ocType
 *       indicates a user or group entry
 *    -- users
 *       number of user names that follows this structure
 *    -- dns
 *       Number of DNs that follows the base DN. These DNs
 *       are either the DN of a user or that of a nested group.
 *       If a DN has the same base DN as that stored in this
 *       structure, it will be truncated to have only the non-base
 *       DN part, with a ',' added to the end to indicate that it's
 *       a short version.
 *    -- basedn_len
 *       length of the base DN
 *    -- dnlen_max
 *       max. length of member DN. This is used to allocate one shared
 *       space big enough to hold the member DNs one at a time.
 *    -- basedn_offset
 *       Where in the buffer the base DN starts. The first
 *       byte of this structure is offset 0.
 *    -- next_byte_offset
 *       Where in the buffer to add the next data item. The first
 *       byte of this structure is offset 0.
 *    -- dbuf_size
 *       Current size of the data area in the packed buffer. This
 *       size includes the size of the _dn2uid_res_t structure.
 *
 * The user names, base DN, and user/group DNs are all null
 * terminated strings. If the member/uniqueMember attributes
 * are not used, the user name list is created entirely from the
 * values of the memberuid attribute returned in the group
 * search. Otherwise, it could also be from the user name (uid
 * attribute) of the dn2uid search of a user DN. The base DN
 * and the user/group DNs will only be set if there are member
 * or uniqueMember attributes in the search results.
 *
 * So the data layout in the data section of the packed buffer is
 * as follows:
 *
 * struct [ "username1" [ "username2 " ...]] ["baseDN"]
 * [ "user or group DN 1" [ "user or group DN 2" ...]]
 *
 */
typedef struct {
	int	ocType;
	int	users;
	int	dns;
	int	basedn_len;
	int	dnlen_max;
	size_t	basedn_offset;
	size_t	next_byte_offset;
	size_t	dbuf_size;
} _dn2uid_res_t;

/*
 * When processing a getGroupsbyMember request, nss_ldap first
 * translates the given member (a user name) into the DN of the
 * user entry. It then does a group search with a filter
 * (|(memberuid=<user name>)((member=<user DN>)(uniqueMember=<user DN>))
 * and asks for the gidNumber, memberUid, member, uniqueMember, and
 * memberOf attributes. The returned groups are all the groups having
 * this user as a member. The gidNumber of the groups will be added
 * to the gid number list to be returned as the output. The present of
 * the memberOf attribute in a group entry is used as an indication that
 * the group is a member of a nested group, so the DN of the group
 * will be used to do further memberof searches. This will be done
 * recursively until all parent groups are found or the maximum level
 * of recursion is reached.
 *
 * The result of a memberof search is placed in the data section
 * of the packed buffer. The packed result of the search has up to
 * three sections:
 *  - a structure, _memberof_res_t
 *  - a base DN
 *  - a series of gid number and group DN pairs
 *
 *  The _memberof_res_t structure has the following elements:
 *    -- parents
 *       number of the containing groups
 *    -- group_dns
 *       number of group DNs that need a further group search
 *    -- basedn_len
 *       length of the base DN
 *    -- dnlen_max
 *       max. length of group DN. This is used to allocate one shared
 *       space big enough to hold the group DNs one at a time.
 *    -- no_sort_search
 *       indicate a no sorting search should be done next
 *    -- basedn_offset
 *       Where in the buffer the base DN starts. The first
 *       byte of this structure is offset 0.
 *    -- next_byte_offset
 *       Where in the buffer to add the next data item. The first
 *       byte of this structure is offset 0.
 *    -- dbuf_size
 *       Current size of the data area in the packed buffer. This
 *       size includes the size of the _memberof_res_t structure.
 *
 * In the gidNumber/groupDN pair section, there is one pair for each
 * containing group found. The gid umber will always be set but the DN
 * of the group may be an empty string, which means no further group
 * search is necessary. The gid numbers and group DNs are null-terminated
 * strings. The DNs are also truncated if they have the same base DNs
 * as that stored in the _memberof_res_t structure.
 *
 * So the layout of the search result in the data section of the
 * packed buffer is as follows:
 *
 *  struct ["baseDN"] [ "gidnumber1" "groupDN1" [ "gidnumber1" "groupDN2" ...]]
 */
typedef struct {
	int	parents;
	int	group_dns;
	int	basedn_len;
	int	dnlen_max;
	boolean_t no_sort_search;
	size_t	basedn_offset;
	size_t	next_byte_offset;
	size_t	dbuf_size;
} _memberof_res_t;

/*
 * gr_attrs lists the attributes needed for generating a group(4) entry.
 */
static const char *gr_attrs[] = {
	_G_NAME,
	_G_GIDNUMBER,
	_G_PASSWD,
	_G_MEMBERUID,
	_G_MEMBER,
	_G_UNIQUEMEMBER,
	_G_OBJECTCLASS,
	(char *)NULL
};

/* gr_attrs2 lists the group attributes that may need schema mapping. */
static const char *gr_attrs2[] = {
	_G_UID,
	_G_MEMBERUID,
	_G_MEMBER,
	_G_UNIQUEMEMBER,
	_G_OBJECTCLASS,
	(char *)NULL
};

/*
 * gr_attrs3 is for processing the member DN list from a dn2uid lookup.
 * Do not change the order, _G_UNIQUEMEMBER should always be the second one.
 */
static const char *gr_attrs3[] = {
	_G_MEMBER,
	_G_UNIQUEMEMBER,
	(char *)NULL
};

/*
 * gr_attrs4 is used for schema mapping a group entry returned
 * from a dn2uid lookup.
 */
static const char *gr_attrs4[] = {
	_G_MEMBERUID,
	_G_MEMBER,
	_G_UNIQUEMEMBER,
	(char *)NULL
};

/*
 * gr_attrs5 is used when searching groups with both memberuid and DN.
 * The presence of any memberOf attributes indicates whether a group
 * is a nested one.
 */
static const char *gr_attrs5[] = {
	_G_GIDNUMBER,
	_G_MEMBERUID,
	_G_MEMBER,
	_G_UNIQUEMEMBER,
	_G_MEMBEROF,
	_G_MEMBEROF_ODSEE,
	(char *)NULL
};

/*
 * gr_attrs6 is used when searching groups with a member DN.
 * The presence of any memberOf attributes indicates whether
 * a group is a nested one.
 */
static const char *gr_attrs6[] = {
	_G_GIDNUMBER,
	_G_MEMBEROF,
	_G_MEMBEROF_ODSEE,
	(char *)NULL
};

/* pw_attrs is used to translate a user's uid to DN */
static const char *pw_attrs[] = {
	"uid",
	(char *)NULL
};

/* Use this attribute to ask for server type from libsldap. */
static const char *gr_extra_info_attr[] = {
	"__ns_ldap_op_attr_server_type",
	(char *)NULL
};

typedef	nss_status_t (*getkey_func)(void *, size_t, char **, int *,
    nss_XbyY_args_t *);
typedef void (*psearch_func)(void **, size_t);

#pragma weak	_nss_cache_create
extern nss_status_t _nss_cache_create(char *, int, int, int);

#pragma weak	_nss_cache_get
extern nss_status_t _nss_cache_get(void **, size_t, getkey_func, psearch_func,
    int, char *);

nss_ldap_cache_state_t dn2uid_cache_state = NSS_LDAP_CACHE_UNINITED;
static mutex_t	dn2uid_init_lock = DEFAULTMUTEX;
nss_ldap_cache_state_t memberof_cache_state = NSS_LDAP_CACHE_UNINITED;
static mutex_t	memberof_init_lock = DEFAULTMUTEX;
static nss_status_t (*cache_get)(void **, size_t, getkey_func, psearch_func,
    int, char *);

/* nss_ldap's dn2uid cache */
#define	DN2UID_POS_TTL		600
#define	LDAP_DN2UID_CACHE_NM	":ldap:dn2uid"
#define	LDAP_DN2UID_CACHE_LEN	(sizeof (LDAP_DN2UID_CACHE_NM))

/* nss_ldap's memberof cache */
#define	MEMBEROF_POS_TTL	600
#define	LDAP_MEMBEROF_CACHE_NM	":ldap:memberof"
#define	LDAP_MEMBEROF_CACHE_LEN (sizeof (LDAP_MEMBEROF_CACHE_NM))

/*
 * For memberof cache search, the UID tag indicates a search with a uid
 * as the key is being performed. The GROUP_DN tag indicates a group DN
 * as the key. GROUP_DN_NS indicates a group DN key but with no result
 * sorting on the server side.
 */
#define	PSEARCH_TAG_LEN		2
#define	PSEARCH_TAG_UID		"u:"
#define	PSEARCH_TAG_GROUP_DN	"d:"
#define	PSEARCH_TAG_GROUP_DN_NS	"n:"

/*
 * userdata consumed by the TLE_group_member_cb callback function
 */
typedef struct {
	_nss_ldap_list_t **listpp;
	int	cnt;
} _member_tl_cb_t;

/*
 * userdata consumed by the TLE_memberof_cb callback function
 */
typedef struct {
	void		**bufpp;
	_memberof_res_t	**grespp;
	nss_pheader_t	**pbufpp;
} _memberof_tl_cb_t;

/*
 * attribute list used by an AD member Transitive Link
 * Expansion search
 */
static const char *gr_attrs_mbr_tl[] = {
	_G_UID,
	_G_OBJECTCLASS,
	(char *)NULL
};

/*
 * attribute list used by an AD memberof Transitive Link
 * Expansion search
 */
static const char *gr_attrs_mof_tl[] = {
	_G_GIDNUMBER,
	(char *)NULL
};

/* Test to see if string 'a' begins with string 'b'. */
static boolean_t
strbw(const char *a, const char *b)
{
	return (strncmp(a, b, strlen(b)) == 0);
}

/*
 * See if string 'a' ends with string 'b', and is longer.
 * Return the pointer to where in 'a' string 'b' begins.
 */
static char *
strew(const char *a, const char *b)
{
	char *p;

	p = strstr(a, b);
	if (p != NULL && p[strlen(b)] == '\0' && (p > a))
		return (p);
	return (NULL);
}

/* Test to see if a group is a nested group. */
static boolean_t
is_nested_group(const ns_ldap_entry_t *group_entry)
{
	ns_ldap_attr_t		*members;

	/*
	 * Use the memberOf attribute (or for DSEE, isMemberOf) to
	 * determine if this group is a nested group. If present,
	 * then yes.
	 */
	members = __ns_ldap_getAttrStruct(group_entry, _G_MEMBEROF);
	if (members != NULL && members->attrvalue != NULL)
		return (B_TRUE);
	members = __ns_ldap_getAttrStruct(group_entry, _G_MEMBEROF_ODSEE);
	if (members != NULL && members->attrvalue != NULL)
		return (B_TRUE);
	return (B_FALSE);
}

void
free2dArray(char ***inppp)
{
	char	**temptr;

	if (inppp == NULL || *inppp == NULL)
		return;
	for (temptr = *inppp; *temptr != NULL; temptr++) {
		free(*temptr);
	}
	free(*inppp);
	*inppp = NULL;
}

/* Loop and compare to see if an item is in a counted list. */
static boolean_t
is_in_counted_list(char **list, int count, char *item)
{
	int i;

	for (i = 0; i < count; i++) {
		if (strcasecmp(list[i], item) == 0)
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * The following code enables and uses/fills the dn2uid cache.
 * The dn2uid cache uses the caching features of nscd if nscd
 * is enabled.
 *
 * How it works:
 *   The nss backend taps into nscd cache using 2 APIs:
 *	_nss_cache_create - to init a backend cache
 *	_nss_cache_get -  to search the cache. This API requires 2 callbacks
 *		getkey - performs the nss_packed_getkey operation
 *		psearch- performs the nss_psearch operation
 *   The expectation is that get receives a properly formed packed buffer
 *   and processes the specific search request, packing the results into
 *   the buffer.  psearch performs the actual lookup, while getkey returns
 *   the necessary key information for psearch and for cache test operations.
 *
 * So that the system does not fail if nscd is disabled, the real APIs
 * are provided as weak symbols to APIs that return error on create
 * and process the lookup using getkey/psearch (aka an uncached lookup).
 *
 * The format of the result packing is project private and subject to
 * change. See the comments on the structures, _dn2uid_res_t and
 * _memberof_res_t, above for the layout of the search results.
 *
 */
/*
 * Local _nss_cache_get performs the actual lookup if not running in the
 * nscd process.
 */
/*ARGSUSED0*/
static nss_status_t
_nss_cache_get_local(void **bufpp, size_t length, getkey_func getkey,
    psearch_func psearch, int nscd_cache, char *from_backend)
{
	nss_pheader_t   *pbuf = (nss_pheader_t *)*bufpp;

	if (bufpp == NULL || *bufpp == NULL || length == 0 ||
	    getkey == NULL || psearch == NULL)
		return (NSS_ERROR);

	/* fake _nss_cache_get just calls psearch (no caching) */
	(*psearch)(bufpp, length);
	pbuf = (nss_pheader_t *)*bufpp;
	return (pbuf->p_status);
}

/*
 * Setup the dn2uid cache if this is the first time call.
 */
static void
_nss_ldap_dn2uid_init()
{
	/* Return if the dn2uid cache setup has been done of tried. */
	if (dn2uid_cache_state != NSS_LDAP_CACHE_UNINITED)
		return;

	(void) mutex_lock(&dn2uid_init_lock);
	/* Return if just done of tried. */
	if (dn2uid_cache_state != NSS_LDAP_CACHE_UNINITED) {
		(void) mutex_unlock(&dn2uid_init_lock);
		return;
	}

	/* Not running in nscd ? */
	if (_nss_cache_create == NULL) {
		dn2uid_cache_state = NSS_LDAP_CACHE_NOTNSCD;
		cache_get = _nss_cache_get_local;
	} else {
		if (_nss_cache_create(LDAP_DN2UID_CACHE_NM,
		    0, 1, DN2UID_POS_TTL) == NSS_SUCCESS) {
			dn2uid_cache_state = NSS_LDAP_CACHE_INITED;
			cache_get = _nss_cache_get;
		} else
			dn2uid_cache_state = NSS_LDAP_CACHE_FAILED;
	}
	(void) mutex_unlock(&dn2uid_init_lock);
}

/*
 * Setup the memberof cache if this is the first time call.
 */
static void
_nss_ldap_memberof_init()
{
	/* Return if the memberof cache setup has been done of tried. */
	if (memberof_cache_state != NSS_LDAP_CACHE_UNINITED)
		return;

	(void) mutex_lock(&memberof_init_lock);
	/* Return if just done of tried. */
	if (memberof_cache_state != NSS_LDAP_CACHE_UNINITED) {
		(void) mutex_unlock(&memberof_init_lock);
		return;
	}

	/* Not running in nscd ? */
	if (_nss_cache_create == NULL) {
		memberof_cache_state = NSS_LDAP_CACHE_NOTNSCD;
		cache_get = _nss_cache_get_local;
	} else {
		if (_nss_cache_create(LDAP_MEMBEROF_CACHE_NM,
		    0, 1, MEMBEROF_POS_TTL) == NSS_SUCCESS) {
			memberof_cache_state = NSS_LDAP_CACHE_INITED;
			cache_get = _nss_cache_get;
		} else
			memberof_cache_state = NSS_LDAP_CACHE_FAILED;
	}
	(void) mutex_unlock(&memberof_init_lock);
}

/* Find the search key from the packed buffer header. */
static nss_status_t
_nss_ldap_grgetkey(void *buffer, size_t buflen,
	char **dbnamepp, int *dbop, nss_XbyY_args_t *arg)
{
	nss_pheader_t	*pbuf = (nss_pheader_t *)buffer;
	nss_dbd_t	*pdbd;
	nssuint_t	off;

	if (buffer == NULL || buflen == 0 || dbop == NULL ||
	    arg == NULL || dbnamepp == NULL)
		return (NSS_ERROR);
	*dbop = pbuf->nss_dbop;
	off = pbuf->dbd_off;
	pdbd = (nss_dbd_t *)((char *)buffer + off);
	*dbnamepp = (char *)buffer + off + pdbd->o_name;
	arg->key.name = (char *)buffer + pbuf->key_off;
	return (NSS_SUCCESS);
}

/* Add a member uid to the member list. */
static int
_mlist_add(_nss_ldap_list_t **mlpp, char *member)
{
	nss_ldap_list_rc_t rc;

	/* Add member to the list */
	rc = nss_ldap_list_add(mlpp, member);
	if (rc == NSS_LDAP_LIST_EXISTED)
		/* It is fine if member is already in the list */
		return (1);
	else if (rc != NSS_LDAP_LIST_SUCCESS)
		return (-1);

	return (1);
}

/*
 * Add a dn2uid data string 'str' to the data area in the packed buffer,
 * '*bufpp', expand the packed buffer if necessary.
 */
static nss_status_t
dn2uid_add_string(void **bufpp, size_t *length, _dn2uid_res_t **respp,
    nss_pheader_t **pbufpp, const char *str)
{
	_dn2uid_res_t	*pres = *respp;
	char		*bptr;
	size_t		left;
	int		slen;

	bptr = (char *)pres + pres->next_byte_offset;
	left = pres->dbuf_size - pres->next_byte_offset;
	slen = strlen(str) + 1;

	/* expand the buffer if not enough room to add the string */
	while (slen > left) {
		size_t		res_off, bufsize;
		void		*tmp;

		bufsize = (*pbufpp)->pbufsiz + _PSEARCH_BUF_BLK;
		tmp = realloc(*bufpp, bufsize);
		if (tmp == NULL)
			return (NSS_ERROR);

		/* adjust pointers */
		res_off = (char *)(*respp) - (char *)(*bufpp);
		/* Cast to void * eliminates lint warning */
		*respp = (_dn2uid_res_t *)(void *)((char *)tmp + res_off);
		pres = *respp;
		*bufpp = tmp;
		*pbufpp = (nss_pheader_t *)tmp;

		/* adjust buffer length */
		*length = bufsize;
		(*pbufpp)->pbufsiz = bufsize;
		pres->dbuf_size += _PSEARCH_BUF_BLK;

		left += _PSEARCH_BUF_BLK;
		bptr = (char *)pres + pres->next_byte_offset;
	}

	(void) strlcpy(bptr, str, left);
	pres->next_byte_offset += slen;

	return (NSS_SUCCESS);
}

/* Generate an attributed mapped attr list to be used by a dn2uid lookup. */
static char **
attr_map_dn2uid_group_attrs()
{
	int	i;
	char	**app;
	char	**amap;

	for (i = 0; gr_attrs2[i] != NULL; i++)
		continue;
	app = (char **)calloc(i + 1, sizeof (char *));
	if (app == NULL)
		return (NULL);

	for (i = 0; gr_attrs2[i] != NULL; i++) {
		amap = __ns_ldap_getMappedAttributes(_GROUP, gr_attrs2[i]);
		if (amap != NULL && amap[0] != NULL)
			app[i] = strdup(amap[0]);
		else
			app[i] = strdup(gr_attrs2[i]);
		if (app[i] == NULL) {
			free2dArray(&app);
			return (NULL);
		}
		if (amap != NULL)
			free2dArray(&amap);
	}
	return (app);
}

/* Perform schema mapping on a group entry returned from a dn2uid lookup. */
static nss_status_t
schema_map_dn2uid_group(ns_ldap_entry_t *entry, ns_ldap_attr_t *goc,
    char **in_attrs)
{
	int		i, j, cnt;
	char		**attrs, *group_oc, *mapped_attr;
	char		**omap;
	char		*attrname;
	nss_status_t	rc = NSS_SUCCESS;

	cnt = goc->value_count;
	attrs = goc->attrvalue;

	/* Make sure it's a group entry. */
	omap = __ns_ldap_getMappedObjectClass(_GROUP, _OC_GROUP);
	if (omap != NULL && omap[0] != NULL)
		group_oc = omap[0];
	else
		group_oc = _OC_GROUP;

	if (!is_in_counted_list(attrs, cnt, group_oc)) {
		/* not a posixGroup entry, return NSS_NOTFOUND */
		rc = NSS_NOTFOUND;
		goto cleanup;
	}

	/*
	 * Find the memberUid, member, and uniqueMember attributes,
	 * that could be schema mapped to use a different name, to
	 * change the name back. gr_attrs4[] contains "memberUid",
	 * "member", and "uniqueMember". gr_attrs2[] contains the
	 * name of the attributes that may be schema mapped.
	 * in_attrs[] contains the corresponding mapped gr_attrs2[]
	 * names that may be seen in the result group entry.
	 */
	for (i = 0; gr_attrs4[i] != NULL; i++) {
		mapped_attr = NULL;
		for (j = 0; gr_attrs2[j] != NULL; j++) {
			if (strcasecmp(gr_attrs4[i], gr_attrs2[j]) != 0)
				continue;
			if (strcasecmp(in_attrs[j], gr_attrs2[j]) == 0)
				continue;
			mapped_attr = in_attrs[j];
			break;
		}
		if (mapped_attr == NULL)
			continue;

		for (j = 0; j < entry->attr_count; j++) {
			attrname = entry->attr_pair[j]->attrname;
			if (strcasecmp(attrname, mapped_attr) == 0) {
				if (strlen(attrname) < strlen(gr_attrs4[i])) {
					free(attrname);
					entry->attr_pair[j]->attrname = NULL;
					attrname = strdup(gr_attrs4[i]);
					if (attrname == NULL) {
						rc = NSS_ERROR;
						goto cleanup;
					}
					entry->attr_pair[j]->attrname =
					    attrname;
				} else
					(void) strcpy(attrname, gr_attrs4[i]);
				break;
			}
		}
	}

cleanup:
	if (omap != NULL)
		free2dArray(&omap);

	return (rc);
}

/*
 * A value for the uniqueMember attribute is a DN followed
 * by an optional hash (#) and uniqueIdentifier.
 * For example, uniqueMember: cn=aaa,dc=bbb,dc=ccc #1234
 * This function remove the optional # and uniqueIdentifier.
 */
static void
remove_uniqueID(char *dn)
{
	char *hash, *eq;

	if ((hash = strrchr(dn, '#')) == NULL)
		return;
	/* invalid DN if no '=' and '#' should be after all RDNs */
	if ((eq = strrchr(dn, '=')) == NULL || eq > hash)
		return;
	/* Remove '# ID' */
	*hash = '\0';
}

/*
 * Perform a dn2uid lookup using the input packed buffer.
 * A dn2uid lookup does an LDAP search with the input
 * user or group DN and returns the dn2uid result data
 * in the data area of the packed buffer. See the comments
 * above about the specific of a dn2uid lookup and the layout
 * of the dn2uid result data.
 */
static void
_nss_ldap_dn2uid_psearch(void **bufpp, size_t length)
{
	nss_pheader_t   	*pbuf;
	nss_XbyY_args_t		arg;
	nss_status_t		ret;
	ns_ldap_result_t	*result = NULL;
	ns_ldap_error_t		*error = NULL, *ep;
	ns_ldap_attr_t		*members, *oc, *username;
	_dn2uid_res_t		*pres;
	const char		*dn;
	char			*dbname, *user;
	char			**classes;
	char			**mapping;
	char			**search_attrs = NULL;
	int			dbop = 0, rc, i, cnt;
	char			*bptr;
	size_t			slen;
	int			mi;
	int			oc_type;
	char			**param;

	pbuf = (nss_pheader_t *)*bufpp;

	ret = _nss_ldap_grgetkey(*bufpp, length, &dbname, &dbop, &arg);
	if (ret != NSS_SUCCESS) {
		pbuf->p_status = ret;
		return;
	}
	dn = arg.key.name;
	if (dn == NULL || (nssuint_t)(strlen(dn) + 1) != pbuf->key_len) {
		/* malformed/improperly sized  key/dn */
		pbuf->p_status = NSS_ERROR;
		return;
	}

	/*
	 * Get the DN entry from the server. Specify 'passwd' as the service
	 * first, in case this DN is that of a posixAccount (user) entry, so
	 * that schema mapping for the passwd service will be done by libsldap.
	 * Also need to map the requested group attributes here, in case this DN
	 * points to a posixGroup (group) entry, in which case, libsldap won't
	 * do the schema mapping because the service specified is not 'group'.
	 * We do this extra schema mapping because we want to try to get
	 * the user or group entry in just one call to __ns_ldap_read_dn().
	 */
	mapping = __ns_ldap_getOrigAttribute(_GROUP,
	    NS_HASH_SCHEMA_MAPPING_EXISTED);
	if (mapping != NULL) {
		/*
		 * There is schema mapping for the group database, so map the
		 * group attributes so that the ldap server knows which attrs
		 * to return.
		 */
		search_attrs = attr_map_dn2uid_group_attrs();
		if (search_attrs == NULL) {
			pbuf->p_status = NSS_ERROR;
			goto cleanup;
		}
	}
	rc = __ns_ldap_read_dn(dn, _PASSWD, NULL,
	    (const char **)search_attrs,
	    NULL, 0, &result, &error, NULL, NULL, NULL, NULL);
	if (rc != NS_LDAP_SUCCESS) {
		if (rc == NS_LDAP_NOTFOUND)
			ret = NSS_NOTFOUND;
		else {
			ret = NSS_ERROR;
			pbuf->p_errno = errno;
		}
		goto cleanup;
	}

	/* posixAccount or posixGroup dn ? */
	oc = __ns_ldap_getAttrStruct(result->entry, _G_OBJECTCLASS);
	if (oc == NULL) {
		ret = NSS_NOTFOUND;
		goto cleanup;
	}
	cnt = oc->value_count;
	classes = oc->attrvalue;
	if (is_in_counted_list(classes, cnt, _OC_ACCOUNT)) {
		oc_type = _OC_ACCOUNT_T;
	} else {
		/*
		 * Not a posixAccount entry, should be a posixGroup one.
		 * Perform schema mapping as it's not done by libsldap.
		 */
		if (mapping != NULL) {
			rc = schema_map_dn2uid_group(result->entry, oc,
			    search_attrs);
			free2dArray(&mapping);
			if (rc != NSS_SUCCESS) {
				ret = rc;
				goto cleanup;
			}
		} else {
			if (!is_in_counted_list(classes, cnt, _OC_GROUP)) {
				ret = NSS_NOTFOUND;
				goto cleanup;
			}
		}
		oc_type = _OC_GROUP_T;
	}

	/* pack up and return results */
	bptr = (char *)*bufpp + pbuf->data_off;
	(void) memset(bptr, 0, sizeof (_dn2uid_res_t));
	pres = (_dn2uid_res_t *)bptr;
	pres->ocType = oc_type;
	pres->next_byte_offset = sizeof (_dn2uid_res_t);
	pres->dbuf_size = pbuf->data_len;

	/* found a posixAccount (user) dn, pack up the user name */
	if (pres->ocType == _OC_ACCOUNT_T) {
		username = __ns_ldap_getAttrStruct(result->entry, _G_UID);
		if (username != NULL && username->attrvalue != NULL) {
			user = username->attrvalue[0];
			if ((ret = dn2uid_add_string(bufpp, &length, &pres,
			    &pbuf, user)) == NSS_SUCCESS) {
				pres->users = 1;
				pbuf->data_len = pres->next_byte_offset;
			}
		} else
			ret = NSS_NOTFOUND;
		goto cleanup;
	}

	/* found a posixGroup dn, pack up the users and dns */
	members = __ns_ldap_getAttrStruct(result->entry, _G_MEMBERUID);
	if (members != NULL && members->attrvalue != NULL) {
		cnt = members->value_count;
		for (i = 0; i < cnt; i++) {
			user = members->attrvalue[i];
			if ((ret = dn2uid_add_string(bufpp, &length, &pres,
			    &pbuf, user)) == NSS_SUCCESS) {
				pres->users++;
			} else
				goto cleanup;
		}
	}

	/* for attrs: _G_MEMBER & _G_UNIQUEMEMBER */
	for (mi = 0; gr_attrs3[mi] != NULL; mi++) {
		members = __ns_ldap_getAttrStruct(result->entry, gr_attrs3[mi]);
		if (members == NULL || members->attrvalue == NULL)
			continue;

		/* Process member (DN) */
		cnt = members->value_count;

		/*
		 * get the configured base DN so that it's not repeated
		 * in every member DN saved
		 */
		if (pres->basedn_len == 0) {
			rc = __ns_ldap_getParam(NS_LDAP_SEARCH_BASEDN_P,
			    (void ***)&param, &ep);
			if (rc == NS_LDAP_SUCCESS) {
				slen = strlen(param[0]) + 1;
				if ((ret = dn2uid_add_string(bufpp, &length,
				    &pres, &pbuf, param[0])) == NSS_SUCCESS) {
					/* point to start of base DN */
					pres->basedn_offset =
					    pres->next_byte_offset - slen;
					pres->basedn_len = slen - 1;
					(void) __ns_ldap_freeParam(
					    (void ***)&param);
				} else
					goto cleanup;
			} else
				(void) __ns_ldap_freeError(&ep);
		}

		for (i = 0; i < cnt; i++) {
			/* use non-basedn part if possible */
			dn = members->attrvalue[i];
			/*
			 * If uniqueMember, drop the optional uniqueIdentifier.
			 */
			if (mi == 1)
				remove_uniqueID((char *)dn);
			if (pres->basedn_len != 0) {
				int dnlen;
				char *p;
				char *basedn = (char *)pres +
				    pres->basedn_offset;

				/*
				 * remember max. DN length seen so far
				 */
				dnlen = strlen(dn) + 1;
				if (dnlen > pres->dnlen_max)
					pres->dnlen_max = dnlen;

				/* if dn ends with basedn */
				if ((p = strew(dn, basedn)) != NULL) {
					/*
					 * This last ',' denotes basedn.
					 */
					*p = ',';
					*(p + 1) = '\0';
				}
			}
			if ((ret = dn2uid_add_string(bufpp, &length, &pres,
			    &pbuf, dn)) == NSS_SUCCESS) {
				pres->dns++;
			} else
				goto cleanup;
		}
	}

	/* Return NSS_NOTFOUND if no new data added. */
	if (pres->next_byte_offset != sizeof (_dn2uid_res_t)) {
		pbuf->data_len = pres->next_byte_offset;
		ret = NSS_SUCCESS;
	} else {
		/*
		 * No group member found.  Returning NSS_NOTFOUND
		 * makes it a negative nscd cache if running in
		 * nscd.
		 */
		ret = NSS_NOTFOUND;
	}

cleanup:
	if (mapping != NULL)
		free2dArray(&mapping);
	if (search_attrs != NULL)
		free2dArray(&search_attrs);
	(void) __ns_ldap_freeResult(&result);
	pbuf->p_status = ret;
}

/* Set up a packed buffer and perform a cache search. */
static nss_status_t
do_lookup(char *cache_name, const char *tag, const char *key, int dbop,
    int start_size, int nscd_cache, char **outputp,
    getkey_func getkey, psearch_func psearch)
{
	nss_status_t		res;
	char			*buffer, *bptr;
	size_t			buflen, off, len;
	size_t			taglen = 0;
	size_t			keylen;
	size_t			keylen_round_up;
	size_t			cache_name_len;
	size_t			cache_name_len_round_up;
	nss_pheader_t		*pbuf;
	nss_dbd_t		*pdbd;

	*outputp = NULL;

	/* space needed for cache name: cache_name_len_round_up */
	cache_name_len = strlen(cache_name) + 1;
	cache_name_len_round_up =
	    ROUND_UP(cache_name_len, sizeof (nssuint_t));

	/* space needed for tag and key: keylen_round_up */
	keylen = strlen(key) + 1;
	if (tag != NULL) {
		taglen = strlen(tag);
		keylen += taglen;
	}
	keylen_round_up = ROUND_UP(keylen, sizeof (nssuint_t));

	/*
	 * Space needed for the packed buffer header:
	 * sizeof (nss_pheader_t) + sizeof (nss_dbd_t) +
	 * cache_name_len_round_up + keylen_round_up.
	 *
	 * Total space needed: packed buffer header size +
	 * requested data size (start_size). See code below
	 * for the consturction of the packed buffer header.
	 */
	buflen = sizeof (nss_pheader_t) + sizeof (nss_dbd_t) +
	    cache_name_len_round_up + keylen_round_up + start_size;

	if ((buffer = calloc(buflen, 1)) == NULL)
		return (NSS_ERROR);

	pbuf = (nss_pheader_t *)buffer;
	pbuf->pbufsiz = buflen;

	/* Populate the packed buffer with a request */
	/* packed header */
	pbuf->nss_dbop = dbop;
	off = sizeof (nss_pheader_t);
	bptr = buffer + off;

	/* nss_dbd_t */
	pdbd = (nss_dbd_t *)bptr;
	pbuf->dbd_off = off;
	len = sizeof (nss_dbd_t);
	bptr += len;
	off += len;

	/* cache name */
	pbuf->dbd_len = sizeof (nss_dbd_t) + cache_name_len;
	pdbd->o_name = sizeof (nss_dbd_t);
	(void) strcpy(bptr, cache_name);
	bptr += cache_name_len_round_up;
	off += cache_name_len_round_up;

	/* tag and key */
	pbuf->key_off = off;
	if (tag != NULL) {
		(void) strcpy(bptr, tag);
		(void) strcpy(bptr + PSEARCH_TAG_LEN, key);
	} else
		(void) strcpy(bptr, key);
	pbuf->key_len = keylen;
	off += keylen_round_up;

	pbuf->data_off = off;
	pbuf->data_len = buflen - off;

	/* perform the cache search */
	res = (*cache_get)((void **)&buffer, buflen, getkey, psearch,
	    nscd_cache, "ldap");
	if (res == NSS_SUCCESS)
		*outputp = buffer;
	else
		(void) free(buffer);

	return (res);
}

/*
 * Given a group or member (user) DN, search the nscd group or passwd cache
 * to find a group or passwd entry with a matching DN.
 */
static nss_status_t
lookup_nscd_cache(char *dn, _nss_ldap_list_t **mlpp)
{
	nss_status_t	res;
	nss_pheader_t	*pbuf;
	char		key[256];
	char		*buffer, *cp, *mp, *ep;
	char		*sptr, *optr, *dptr;
	int		rc;
	boolean_t	dn_matched;

	/*
	 * Check to see if the member or group is already in nscd cache.
	 * Search key is the rdn value.
	 */
	*key = '\0';
	/* skip rdn attr type and use the attr value as the search key */
	cp = strchr(dn, '=');
	if (cp != NULL) {
		ep = strchr(cp + 1, ',');
		if (ep != NULL) {
			*ep = '\0';
			(void) strlcpy(key, cp + 1, sizeof (key));
			*ep = ',';
		}
	}

	if (*key == '\0')
		return (NSS_NOTFOUND);

	/* try search user first */
	res = do_lookup(NSS_DBNAM_PASSWD, NULL, key,
	    NSS_DBOP_PASSWD_BYNAME, NSS_LINELEN_PASSWD, 1,
	    &buffer, NULL, NULL);

	/*
	 * If user found in nscd cache and with the same DN,
	 * add it to the member list.
	 */
	dn_matched = B_FALSE;
	if (res == NSS_SUCCESS) {
		pbuf = (nss_pheader_t *)buffer;
		sptr = buffer + pbuf->data_off;
		/*
		 * The optional DN string is right after the regular database
		 * result string. pbuf->data_len indicates the sum of these
		 * two strings. If it's not larger than the length of the
		 * first string, then there is no optional DN.
		 */
		if (pbuf->data_len > strlen(sptr)) {
			optr = sptr + strlen(sptr) + 1;
			if (strbw(optr, NSS_LDAP_DN_TAG)) {
				dptr = optr + NSS_LDAP_DN_TAG_LEN;
				/* skip server type */
				dptr = strchr(dptr, ':');
				if (dptr != NULL &&
				    strcmp(dptr + 1, dn) == 0)
					dn_matched = B_TRUE;
			}
		}

		free(buffer);

		if (dn_matched) {
			rc = _mlist_add(mlpp, key);
			if (rc < 0)
				return (NSS_ERROR);
		} else
			return (NSS_NOTFOUND);

		return (NSS_SUCCESS);
	}

	/* next try searching group */
	res = do_lookup(NSS_DBNAM_GROUP, NULL, key,
	    NSS_DBOP_GROUP_BYNAME, NSS_LINELEN_GROUP, 1,
	    &buffer, NULL, NULL);

	/*
	 * If group found in nscd cache and with the same DN,
	 * add all members of the group to the member list.
	 */
	dn_matched = B_FALSE;
	if (res == NSS_SUCCESS) {
		pbuf = (nss_pheader_t *)buffer;
		sptr = buffer + pbuf->data_off;
		/*
		 * The optional DN string is right after the regular database
		 * result string. pbuf->data_len indicates the sum of these
		 * two strings. If it's not larger than the length of the
		 * first string, then there is no optional DN.
		 */
		if (pbuf->data_len > strlen(sptr)) {
			optr = sptr + strlen(sptr) + 1;
			if (strbw(optr, NSS_LDAP_DN_TAG)) {
				dptr = optr + NSS_LDAP_DN_TAG_LEN;
				/* skip server type */
				dptr = strchr(dptr, ':');
				if (dptr != NULL &&
				    strcmp(dptr + 1, dn) == 0)
					dn_matched = B_TRUE;
			}
		}

		if (dn_matched) {
			res = NSS_SUCCESS;
			for (mp = strrchr(sptr, ':'); mp != NULL; mp = ep) {
				mp++;
				ep = strchr(mp, ',');
				if (ep != NULL)
					*ep = '\0';
				if (*mp == '\0')
					continue;
				if (_mlist_add(mlpp, mp) < 0) {
					res = NSS_ERROR;
					break;
				}
			}
			free(buffer);
			return (res);
		}
		free(buffer);
	}

	return (NSS_NOTFOUND);
}

/*
 * Given a member DN, find the corresponding user, or if the DN is that
 * of a group, find all the members recursively until the maximum level
 * of recursion is reached. The users or groups could be from the nscd
 * cache or from the ldap servers.
 */
static nss_status_t
_nss_ldap_proc_memberDN(char *dn, ns_ldap_server_type_t serverType,
    _nss_ldap_list_t **dlpp, _nss_ldap_list_t **mlpp, int *rec_callp)
{
	nss_status_t		res;
	nss_pheader_t		*pbuf;
	_dn2uid_res_t		*pres;
	char			*buffer = NULL;
	char			*sptr, *osptr, *pp, *mdn;
	char			*basedn;
	char			*wbdn = NULL;
	int			i, mdn_len;

	/*
	 * If the depth of recursion reaches DN2UID_MAX_RECURSIVE_CALL
	 * then stop and return NSS_SUCCESS.
	 */
	*rec_callp += 1;
	if (*rec_callp == DN2UID_MAX_RECURSIVE_CALL) {
		res = NSS_SUCCESS;
		goto out;
	}

	/* create the dn2uid nscd backend cache if not done yet */
	if (dn2uid_cache_state == NSS_LDAP_CACHE_UNINITED)
		_nss_ldap_dn2uid_init();

	/*
	 * For loop detection, check to see if dn has already been
	 * processed by adding it to the dnlist. It not able to add,
	 * then it has been processed or error occurred, ignore it.
	 */
	if (nss_ldap_list_add(dlpp, dn) != NSS_LDAP_LIST_SUCCESS) {
		res = NSS_SUCCESS;
		goto out;
	}

	/*
	 * If running in nscd, check to see if member or
	 * group is already in nscd cache.
	 */
	if (_nss_cache_create != NULL) {
		res = lookup_nscd_cache(dn, mlpp);
		if (res != NSS_NOTFOUND)
			goto out;
	}

	/*
	 * Check to see if dn has already been searched and in nscd's
	 * dn2uid cache. If not running in nscd, then the local
	 * _nss_cache_get will be called to search the DN directly.
	 */
	res = do_lookup(LDAP_DN2UID_CACHE_NM, NULL, dn, 0,
	    _PSEARCH_BUF_BLK, 0, &buffer, _nss_ldap_grgetkey,
	    _nss_ldap_dn2uid_psearch);
	if (res != NSS_SUCCESS)
		goto out;

	pbuf = (nss_pheader_t *)buffer;
	pp = buffer + pbuf->data_off;
	pres = (_dn2uid_res_t *)pp;
	osptr = sptr = (char *)&pres[1];
	if (pres->ocType == _OC_ACCOUNT_T) {
		/* a single user id; add it to the member list */
		if (_mlist_add(mlpp, sptr) < 0) {
			res = NSS_ERROR;
			goto out;
		}
	} else { /* a group entry */
		/* add all users to the member list */
		for (i = pres->users; --i >= 0; ) {
			if (_mlist_add(mlpp, sptr) < 0) {
				res = NSS_ERROR;
				goto out;
			}
			sptr += strlen(sptr) + 1;
		}

		/*
		 * If base DN is present, set pointer to it and
		 * go pass it to process the next set of data,
		 * group DNs.
		 */
		if (pres->basedn_len > 0) {
			basedn = (char *)pres + pres->basedn_offset;
			sptr += pres->basedn_len + 1;
		}

		/*
		 * If there are group DNs, get all members recursively
		 * by calling _nss_ldap_proc_memberDN again.
		 */
		for (i = pres->dns; --i >= 0; ) {
			/*
			 * Restore member DN if it was truncated to save
			 * space.
			 */
			mdn = sptr;
			mdn_len = strlen(sptr);
			sptr += mdn_len + 1;
			if (pres->basedn_len > 0 && mdn[mdn_len - 1] == ',') {
				/*
				 * Get a work buffer from the stack
				 * that can be reused.
				 */
				if (wbdn == NULL)
					wbdn = (char *)alloca(pres->dnlen_max);

				/* drop the last ',' */
				(void) strlcpy(wbdn, mdn, mdn_len);
				(void) strlcat(wbdn, basedn, pres->dnlen_max);
				mdn = wbdn;
			}

			res = _nss_ldap_proc_memberDN(mdn, serverType, dlpp,
			    mlpp, rec_callp);
			if (res != NSS_SUCCESS && res != NSS_NOTFOUND)
				goto out;
		}
	}

	/* If no new data added, return NSS_NOTFOUND. */
	if (osptr == sptr)
		res = NSS_NOTFOUND;
	else
		res = NSS_SUCCESS;

out:
	if (buffer != NULL)
		(void) free(buffer);
	*rec_callp -= 1;
	return (res);
}

/*
 * TLE_group_member_cb is a callback function callable by libsldap
 * to process each one of the member entres returned by an AD
 * Transitive Link Expansion search that request all members of
 * a group.
 */
int
TLE_group_member_cb(const ns_ldap_entry_t *entry, const void *userdata)
{
	int		cnt;
	char		**username;
	char		**classes;
	ns_ldap_attr_t	*oc;
	_member_tl_cb_t	*tc = (_member_tl_cb_t *)userdata;

	/* posixAccount or posixGroup dn ? */
	oc = __ns_ldap_getAttrStruct(entry, _G_OBJECTCLASS);
	if (oc == NULL)
		return (NS_LDAP_NOTFOUND);

	cnt = oc->value_count;
	classes = oc->attrvalue;
	if (!is_in_counted_list(classes, cnt, _OC_ACCOUNT)) {
		/* Not an posixAccount entry, skip. */
		return (NS_LDAP_CB_NEXT);

	}

	username = __ns_ldap_getAttr(entry, _G_UID);
	if (username == NULL || username[0] == NULL) {
		/* No user name, just ignore the entry. */
		return (NS_LDAP_CB_NEXT);
	}

	if (_mlist_add(tc->listpp, username[0]) < 0) {
		return (NS_LDAP_OP_FAILED);
	}

	tc->cnt++;

	return (NS_LDAP_CB_NEXT);
}

/*
 * Perform an AD Transitive Link Expansion search to get all members
 * of a group entry. All member entries including those of the child
 * nested groups will be returned in one search. The callback function
 * TLE_group_member_cb is used to process each one of the entries.
 */
static nss_status_t
get_TLE_group_member(ns_ldap_entry_t *entry, _member_tl_cb_t *member_tc)
{
	char	**dn;
	int	rc;
	int	ocnt;
	char	searchfilter[SEARCHFILTERLEN];
	char	searchDN[SEARCHFILTERLEN * 4];
	ns_ldap_result_t	*result;
	ns_ldap_error_t		*error;

	dn = __ns_ldap_getAttr(entry, "dn");
	if (dn == NULL || dn[0] == NULL)
		return (NSS_ERROR);
	/* Escape any special characters in the dn first. */
	if (_ldap_filter_name(searchDN, dn[0], sizeof (searchDN)) != 0)
		return (NSS_NOTFOUND);

	ocnt = member_tc->cnt;
	rc = snprintf(searchfilter, sizeof (searchfilter), _F_GETGRMEM_BL,
	    searchDN);
	if (rc >= sizeof (searchfilter) || rc < 0)
		return (NSS_NOTFOUND);

	rc = __ns_ldap_list_sort(_PASSWD, searchfilter, _G_UID,
	    _merge_SSD_filter, gr_attrs_mbr_tl, NULL, NS_LDAP_PAGE_CTRL,
	    &result, &error, TLE_group_member_cb, member_tc);

	if (result != NULL)
		(void) __ns_ldap_freeResult(&result);
	if (error != NULL)
		(void) __ns_ldap_freeError(&error);

	/*
	 * libsldap returns NS_LDAP_NOTFOUND if
	 * all result entries were consumed by
	 * the callback function.
	 */
	if (rc == NS_LDAP_NOTFOUND && ocnt != member_tc->cnt)
		return (NSS_SUCCESS);
	else
		return (NSS_ERROR);

}

/*
 * _nss_ldap_memberlist collects all members of a group
 * (recursively if necessary) processing all:
 *     _G_MEMBERUID, G_MEMBER, G_UNIQUEMEMBER
 * starting with the current results.  Follow on
 * dn2uid results may be cached.
 */
static nss_status_t
_nss_ldap_memberlist(ns_ldap_result_t *result, ns_ldap_entry_t *extra_info,
    _nss_ldap_list_t **dlpp, _nss_ldap_list_t **mlpp, int *rec_callp)
{
	ns_ldap_attr_t	*members;
	ns_ldap_server_type_t serverType;
	char		*dn, **users;
	nss_status_t	res;
	int 		i, cnt;
	int		mi;
	_member_tl_cb_t	member_tc;

	/* process member uid, attr: _G_MEMBERUID */
	members = __ns_ldap_getAttrStruct(result->entry, _G_MEMBERUID);
	if (members != NULL && members->attrvalue != NULL) {
		cnt = members->value_count;
		users = members->attrvalue;
		for (i = 0; i < cnt; i++) {
			if (_mlist_add(mlpp, users[i]) < 0) {
				return (NSS_ERROR);
			}
		}
	}

	/* Determine which type of server the entry is from. */
	serverType = _nss_ldap_get_server_type(extra_info, NULL);

	/*
	 * If from AD, do a Transitive Link Expansion search
	 * to get all member entries with just one search.
	 */
	if (serverType == NS_LDAP_SERVERTYPE_AD) {
		member_tc.listpp = mlpp;
		member_tc.cnt = 0;
		res = get_TLE_group_member(result->entry, &member_tc);
		if (res == NSS_SUCCESS)
			return (NSS_SUCCESS);
	}

	/* process member (or group) DN, attrs: _G_MEMBER & _G_UNIQUEMEMBER */
	for (mi = 0; gr_attrs3[mi] != NULL; mi++) {
		members = __ns_ldap_getAttrStruct(result->entry, gr_attrs3[mi]);
		if (members != NULL && members->attrvalue != NULL) {
			/* Process member (DN) */
			cnt = members->value_count;
			for (i = 0; i < cnt; i++) {
				dn = members->attrvalue[i];
				/*
				 * If uniqueMember, drop the optional
				 * uniqueIdentifier.
				 */
				if (mi == 1)
					remove_uniqueID((char *)dn);
				res = _nss_ldap_proc_memberDN(dn, serverType,
				    dlpp, mlpp, rec_callp);
				/*
				 * No group member is not an error,
				 * as groups having no users are allowed.
				 */
				if (res == NSS_NOTFOUND)
					continue;
				if (res != NSS_SUCCESS)
					return (res);
			}
		}
	}
	return (NSS_SUCCESS);
}


/*
 * _nss_ldap_group2str is the data marshaling method for the group getXbyY
 * (e.g., getgrnam(), getgrgid(), getgrent()) backend processes. This method
 * is called after a successful ldap search has been performed. This method
 * will parse the ldap search values into the file format.
 * e.g.
 *
 * adm::4:root,adm,daemon
 *
 */

static int
_nss_ldap_group2str(ldap_backend_ptr be, nss_XbyY_args_t *argp)
{
	int		nss_result;
	int		buflen = 0, len;
	char		*buffer = NULL;
	ns_ldap_result_t	*result = be->result;
	ns_ldap_entry_t	*extra_info = be->extra_info;
	char		**gname, **passwd, **gid, *password, *end;
	char		gid_nobody[NOBODY_STR_LEN];
	char		*gid_nobody_v[1];
	char		**dn_v;
	char		*server_type;
	int		rec_call = -1;
	nss_status_t	ret;
	_nss_ldap_list_t *memlist = NULL;
	_nss_ldap_list_t *dnlist = NULL;
	nss_ldap_list_rc_t list_rc;

	(void) snprintf(gid_nobody, sizeof (gid_nobody), "%u", GID_NOBODY);
	gid_nobody_v[0] = gid_nobody;

	if (result == NULL)
		return (NSS_STR_PARSE_PARSE);
	buflen = argp->buf.buflen;

	if (argp->buf.result != NULL) {
		if ((be->buffer = calloc(1, buflen)) == NULL) {
			nss_result = NSS_STR_PARSE_PARSE;
			goto result_grp2str;
		}
		buffer = be->buffer;
	} else
		buffer = argp->buf.buffer;

	nss_result = NSS_STR_PARSE_SUCCESS;
	(void) memset(buffer, 0, buflen);

	gname = __ns_ldap_getAttr(result->entry, _G_NAME);
	if (gname == NULL || gname[0] == NULL || (strlen(gname[0]) < 1)) {
		nss_result = NSS_STR_PARSE_PARSE;
		goto result_grp2str;
	}
	passwd = __ns_ldap_getAttr(result->entry, _G_PASSWD);
	if (passwd == NULL || passwd[0] == NULL || (strlen(passwd[0]) == 0)) {
		/* group password could be NULL, replace it with "" */
		password = _NO_PASSWD_VAL;
	} else {
		/*
		 * Preen "{crypt}" if necessary.
		 * If the password does not include the {crypt} prefix
		 * then the password may be plain text.  And thus
		 * perhaps crypt(3c) should be used to encrypt it.
		 * Currently the password is copied verbatim.
		 */
		if (strncasecmp(passwd[0], _CRYPT, strlen(_CRYPT)) == 0)
			password = passwd[0] + strlen(_CRYPT);
		else
			password = passwd[0];
	}
	gid = __ns_ldap_getAttr(result->entry, _G_GIDNUMBER);
	if (gid == NULL || gid[0] == NULL || (strlen(gid[0]) < 1)) {
		nss_result = NSS_STR_PARSE_PARSE;
		goto result_grp2str;
	}
	/* Validate GID */
	if (strtoul(gid[0], &end, 10) > MAXUID)
		gid = gid_nobody_v;
	len = snprintf(buffer, buflen, "%s:%s:%s:", gname[0], password, gid[0]);
	TEST_AND_ADJUST(len, buffer, buflen, result_grp2str);

	/* Rescursive member list processing */
	ret = _nss_ldap_memberlist(result, extra_info, &dnlist, &memlist,
	    &rec_call);
	if (ret != NSS_SUCCESS) {
		nss_result = NSS_STR_PARSE_PARSE;
		goto result_grp2str;
	}

	/*
	 * Copy all the members to the output buffer.
	 * If no members, nss_ldap_list_dump will do
	 * nothing.
	 */
	list_rc = nss_ldap_list_dump(&memlist, &buffer, &buflen);
	if (list_rc != NSS_LDAP_LIST_SUCCESS &&
	    list_rc != NSS_LDAP_LIST_NOLIST) {
		if (list_rc == NSS_LDAP_LIST_ERANGE)
			nss_result = NSS_STR_PARSE_ERANGE;
		else
			nss_result = NSS_STR_PARSE_PARSE;
		goto result_grp2str;
	}

nomember:
	/* The front end marshaller doesn't need the trailing nulls */
	if (argp->buf.result != NULL) {
		be->buflen = strlen(be->buffer);
	} else {
		/*
		 * Save the entry DN as an optional data, if
		 * dn to uid caching is being performed and
		 * not processing enumeration results and
		 * there's enough room in the buffer.
		 * Format is: (group data followed by optional DN data)
		 * <group data> + '\0' + '#dn:<server_type>:' + <DN> + '\0'
		 */

		/* Terminate the primary result. */
		*buffer++ = '\0';
		buflen--;

		be->have_dn = B_FALSE;
		if (dn2uid_cache_state == NSS_LDAP_CACHE_INITED &&
		    be->enumcookie == NULL) {
			dn_v = __ns_ldap_getAttr(result->entry, "dn");
			(void) _nss_ldap_get_server_type(be->extra_info,
			    &server_type);
			len = snprintf(buffer, buflen, "%s%s:%s",
			    NSS_LDAP_DN_TAG, server_type, dn_v[0]);
			if (len < buflen && len >= 0) {
				len++;	/* Include the \0 */
				buffer += len;
				buflen -= len;
				be->have_dn = B_TRUE;
			}
		}
	}

result_grp2str:
	nss_ldap_list_free(&memlist);
	nss_ldap_list_free(&dnlist);
	if (be->extra_info != NULL) {
		__ns_ldap_freeEntry(be->extra_info);
		be->extra_info = NULL;
	}
	(void) __ns_ldap_freeResult(&be->result);
	return (nss_result);
}

/*
 * getbynam gets a group entry by name. This function constructs an ldap
 * search filter using the name invocation parameter and the getgrnam search
 * filter defined. Once the filter is constructed, we searche for a matching
 * entry and marshal the data results into struct group for the frontend
 * process. The function _nss_ldap_group2ent performs the data marshaling.
 */

static nss_status_t
getbynam(ldap_backend_ptr be, void *a)
{
	nss_XbyY_args_t	*argp = (nss_XbyY_args_t *)a;
	char		searchfilter[SEARCHFILTERLEN];
	char		userdata[SEARCHFILTERLEN];
	char		groupname[SEARCHFILTERLEN];
	int		ret;

	if (_ldap_filter_name(groupname, argp->key.name, sizeof (groupname)) !=
	    0)
		return ((nss_status_t)NSS_NOTFOUND);

	ret = snprintf(searchfilter, sizeof (searchfilter),
	    _F_GETGRNAM, groupname);
	if (ret >= sizeof (searchfilter) || ret < 0)
		return ((nss_status_t)NSS_NOTFOUND);

	ret = snprintf(userdata, sizeof (userdata), _F_GETGRNAM_SSD, groupname);
	if (ret >= sizeof (userdata) || ret < 0)
		return ((nss_status_t)NSS_NOTFOUND);

	be->extra_info_attr = gr_extra_info_attr;
	return (_nss_ldap_lookup(be, argp, _GROUP, searchfilter, NULL,
	    _merge_SSD_filter, userdata));
}


/*
 * getbygid gets a group entry by number. This function constructs an ldap
 * search filter using the name invocation parameter and the getgrgid search
 * filter defined. Once the filter is constructed, we searche for a matching
 * entry and marshal the data results into struct group for the frontend
 * process. The function _nss_ldap_group2ent performs the data marshaling.
 */

static nss_status_t
getbygid(ldap_backend_ptr be, void *a)
{
	nss_XbyY_args_t	*argp = (nss_XbyY_args_t *)a;
	char searchfilter[SEARCHFILTERLEN];
	char userdata[SEARCHFILTERLEN];
	int ret;

	if (argp->key.uid > MAXUID)
		return ((nss_status_t)NSS_NOTFOUND);

	ret = snprintf(searchfilter, sizeof (searchfilter),
	    _F_GETGRGID, argp->key.uid);
	if (ret >= sizeof (searchfilter) || ret < 0)
		return ((nss_status_t)NSS_NOTFOUND);

	ret = snprintf(userdata, sizeof (userdata),
	    _F_GETGRGID_SSD, argp->key.uid);
	if (ret >= sizeof (userdata) || ret < 0)
		return ((nss_status_t)NSS_NOTFOUND);

	be->extra_info_attr = gr_extra_info_attr;
	return (_nss_ldap_lookup(be, argp, _GROUP, searchfilter, NULL,
	    _merge_SSD_filter, userdata));

}

/*
 * setup_buf_memberof_res sets up a control block for doing
 * memberof searches. The control block locates at the
 * beginning of the result data area in the packed buffer.
 */
static void
setup_buf_memberof_res(void *buffer, _memberof_res_t **grespp)
{
	char		*bptr;
	size_t		glen;
	nss_pheader_t	*pbuf = (nss_pheader_t *)buffer;
	_memberof_res_t	*gres;
	ns_ldap_error_t	*errorp;
	void		**param;
	int		rc, len;

	bptr = (char *)buffer + pbuf->data_off;
	glen = sizeof (_memberof_res_t);
	*grespp = (_memberof_res_t *)bptr;
	gres = *grespp;
	gres->parents = 0;
	gres->group_dns = 0;
	gres->dnlen_max = 0;
	gres->next_byte_offset = glen;
	gres->dbuf_size = pbuf->data_len;
	gres->no_sort_search = B_FALSE;

	/* get the configured baseDN */
	rc = __ns_ldap_getParam(NS_LDAP_SEARCH_BASEDN_P, &param, &errorp);
	if (rc == NS_LDAP_SUCCESS) {
		len = strlen(param[0]);
		(void) strlcpy(((char *)gres + gres->next_byte_offset),
		    param[0], gres->dbuf_size - gres->next_byte_offset);
		gres->basedn_len = len;
		gres->basedn_offset = glen;
		gres->next_byte_offset += len + 1;
		(void) __ns_ldap_freeParam(&param);
	} else {
		gres->basedn_len = 0;
		gres->basedn_offset = 0;
		(void) __ns_ldap_freeError(&errorp);
	}
}

/*
 * Realloc the packed buffer for a memberof search if not
 * enough space left.
 */
static void *
memberof_realloc_buf(void **bufpp, _memberof_res_t **grespp,
    nss_pheader_t **pbufpp, char **bptrpp, size_t *bufsizepp)
{
	void		*tmp;
	size_t		res_off, bufsize;
	nss_pheader_t  	*pbuf = (nss_pheader_t *)*bufpp;
	_memberof_res_t	*gres = *grespp;

	bufsize = pbuf->pbufsiz + _PSEARCH_BUF_BLK;
	tmp = realloc(*bufpp,  bufsize);
	if (tmp == NULL)
		return (NULL);

	/*
	 * Buffer pointer may be changed, make sure all related
	 * data in the packed buffer are updated.
	 */
	res_off = (char *)gres - (char *)pbuf;
	*grespp = (_memberof_res_t *)((char *)tmp + res_off);
	(*grespp)->dbuf_size = (*grespp)->dbuf_size + _PSEARCH_BUF_BLK;
	pbuf = (nss_pheader_t *)tmp;
	*pbufpp = tmp;
	pbuf->pbufsiz = bufsize;
	*bufpp = tmp;
	*bufsizepp = (*grespp)->dbuf_size;
	*bptrpp = (char *)(*grespp) + (*grespp)->next_byte_offset;

	return (tmp);
}

/*
 * pack_group_gid_dn adds the gid and DN of a group to the
 * result area. If not a nested group, its DN will be stored
 * as an empty string, as an indication that parent group
 * searching needs not be done.
 */
static int
pack_group_gid_dn(void **bufpp, const ns_ldap_entry_t *entry,
    _memberof_res_t **grespp, nss_pheader_t **pbufpp,
    boolean_t nested_group)
{
	char	**gidvalue;
	char	*gid, *bptr;
	char	**dn, *entry_dn;
	size_t	gid_dn_slen, dlen, dbuf_size;
	_memberof_res_t	*gres =  *grespp;

	bptr = (char *)gres + gres->next_byte_offset;
	dlen = gres->next_byte_offset;
	dbuf_size = gres->dbuf_size;

	entry_dn = "";
	if (nested_group) {
		dn = __ns_ldap_getAttr(entry, "dn");
		if (dn != NULL && dn[0] != NULL)
			entry_dn = dn[0];
		/* store only non-basedn part if possible */
		if (gres->basedn_len != 0) {
			int dnlen;
			char *p;
			char *basedn = (char *)gres +
			    gres->basedn_offset;

			/*
			 * remember max. DN length seen so far
			 */
			dnlen = strlen(entry_dn) + 1;
			if (dnlen > gres->dnlen_max)
				gres->dnlen_max = dnlen;

			/* if entry_dn ends with basedn */
			if ((p = strew(entry_dn, basedn)) != NULL) {
				/* This last ',' denotes basedn. */
				*p = ',';
				*(p + 1) = '\0';
			}
		}
	}

	/* get group gid */
	gidvalue = __ns_ldap_getAttr(entry, "gidnumber");
	if (gidvalue == NULL)
		return (NSS_STR_PARSE_NO_RESULT);
	gid = gidvalue[0];
	/* length including 2 '\0' */
	gid_dn_slen = strlen(gid) + strlen(entry_dn) + 2;
	while (gid_dn_slen + dlen > dbuf_size) {
		if (memberof_realloc_buf(bufpp, grespp, pbufpp,
		    &bptr, &dbuf_size) == NULL)
			return (NSS_STR_PARSE_PARSE);
		else
			gres = *grespp;
	}

	(void) snprintf(bptr, dbuf_size - dlen, "%s%c%s%c", gid, '\0',
	    entry_dn, '\0');
	dlen += gid_dn_slen;
	gres->next_byte_offset = dlen;
	gres->parents++;
	if (*entry_dn != '\0')
		gres->group_dns++;
	return (NSS_STR_PARSE_SUCCESS);
}

/*
 * TLE_memberof_cb is a callback function callable by libsldap
 * to process each one of the group entries returned by an AD
 * memberof Transitive Link Expansion search.
 */
int
TLE_memberof_cb(const ns_ldap_entry_t *entry, const void *userdata)
{
	_memberof_tl_cb_t *bc = (_memberof_tl_cb_t *)userdata;
	int rc;

	rc = pack_group_gid_dn(bc->bufpp, entry,
	    bc->grespp, bc->pbufpp, 0);
	if (rc == NSS_STR_PARSE_SUCCESS)
		return (NS_LDAP_CB_NEXT);
	else
		return (NS_LDAP_OP_FAILED);
}

/*
 * key_groupDN_psearch is a memberof psearch function that finds all
 * the groups that the group identified by dn is a member of. This
 * is used to resolve nested group membership.
 */
static void
key_groupDN_psearch(const char *tag, const char *dn, void **bufpp)
{
	int			i, ret;
	int			no_sort_search = B_FALSE;
	char			searchfilter[SEARCHFILTERLEN * 4];
	char			userdata[SEARCHFILTERLEN * 4];
	char			searchDN[SEARCHFILTERLEN * 4];
	ns_ldap_result_t	*result = NULL;
	ns_ldap_error_t		*error = NULL;
	ns_ldap_entry_t		*curEntry;
	_memberof_res_t		*gres;
	nss_pheader_t   	*pbuf = (nss_pheader_t *)*bufpp;
	size_t			old_dlen;

	/* Escape any special characters in the DN first. */
	if (_ldap_filter_name(searchDN, dn, sizeof (searchDN)) != 0) {
		ret = NSS_NOTFOUND;
		goto cleanup;
	}

	/*
	 * Do a search to get all the group entries which have the
	 * group DN as one of the values of its member or uniqueMember
	 * attribute.
	 */

	ret = snprintf(searchfilter, sizeof (searchfilter), _F_GETGRGRP,
	    searchDN, searchDN, searchDN);
	if (ret >= sizeof (searchfilter) || ret < 0) {
		ret = NSS_NOTFOUND;
		goto cleanup;
	}
	ret = snprintf(userdata, sizeof (userdata), _F_GETGRGRP_SSD,
	    searchDN, searchDN, searchDN);
	if (ret >= sizeof (userdata) || ret < 0) {
		ret = NSS_NOTFOUND;
		goto cleanup;
	}

	/*
	 * Use __ns_ldap_list() if not able to get VLV results from
	 * the server.
	 */
	if (strbw(tag, PSEARCH_TAG_GROUP_DN_NS)) {
		no_sort_search = B_TRUE;
		ret = __ns_ldap_list(_GROUP, searchfilter,
		    _merge_SSD_filter, gr_attrs6, NULL, 0,
		    &result, &error, NULL, userdata);
	} else {
		ret = __ns_ldap_list_sort(_GROUP, searchfilter, _G_NAME,
		    _merge_SSD_filter, gr_attrs6, NULL, NS_LDAP_PAGE_CTRL,
		    &result, &error, NULL, userdata);
	}
	if (ret == NS_LDAP_NOTFOUND) {
		ret = NSS_NOTFOUND;
		goto cleanup;
	} else if (ret != NS_LDAP_SUCCESS) {
		ret = NSS_ERROR;
		goto cleanup;
	}

	/*
	 * Set up the group result control block which is at the start
	 * of result area.
	 */
	setup_buf_memberof_res(*bufpp, &gres);
	pbuf = (nss_pheader_t *)*bufpp;
	old_dlen = gres->next_byte_offset;
	gres->no_sort_search = no_sort_search;


	/*
	 * Store gidnumber and entry DN from each result entry in the
	 * result area.
	 */
	for (curEntry = result->entry, i = 0; i < result->entries_count;
	    i++, curEntry = curEntry->next) {
		ret = pack_group_gid_dn(bufpp, curEntry, &gres,
		    &pbuf, is_nested_group(curEntry));
	}

	/* Return NSS_NOTFOUND if no new data added. */
	if (old_dlen == gres->next_byte_offset) {
		ret = NSS_NOTFOUND;
	} else {
		ret = NSS_SUCCESS;
		pbuf->data_len = gres->next_byte_offset;
	}

cleanup:
	if (result != NULL)
		(void) __ns_ldap_freeResult(&result);
	if (error != NULL)
		(void) __ns_ldap_freeError(&error);
	pbuf->p_status = ret;
}

/*
 * key_uid_psearch is a memberof psearch function that finds all
 * the groups that the user identified by key is a member of.
 */
static void
key_uid_psearch(const char *key, void **bufpp)
{
	int			i, k, ret;
	char			**membervalue;
	char			searchfilter[SEARCHFILTERLEN * 8];
	char			userdata[SEARCHFILTERLEN * 8];
	char			searchDN[SEARCHFILTERLEN * 4];
	char			**dn;
	char			*user_dn = NULL;
	char			*buffer = NULL;
	char			*sptr, *optr, *dptr;
	size_t			old_dlen;
	ns_ldap_result_t	*result = NULL;
	ns_ldap_result_t	*result1 = NULL;
	ns_ldap_entry_t		*curEntry;
	ns_ldap_attr_t		*members;
	ns_ldap_error_t		*error = NULL;
	ns_ldap_entry_t		*extra_info = NULL;
	nss_pheader_t   	*pbuf;
	_memberof_res_t		*gres;
	_memberof_tl_cb_t	memberof_tc;
	ns_ldap_server_type_t	serverType = NS_LDAP_SERVERTYPE_UNKNOWN;

	pbuf = (nss_pheader_t *)*bufpp;
	if (key == NULL || *key == '\0') {
		pbuf->p_status = NSS_ERROR;
		return;
	}

	/*
	 * Try searching the user in nscd's passwd cache, in case
	 * user DN is available there.
	 */
	ret = do_lookup(NSS_DBNAM_PASSWD, NULL, key,
	    NSS_DBOP_PASSWD_BYNAME, NSS_LINELEN_PASSWD, 1,
	    &buffer, NULL, NULL);

	/*
	 * If found in nscd cache, get the user's DN and server type
	 * if available.
	 */
	if (ret == NSS_SUCCESS) {
		int slen;
		pbuf = (nss_pheader_t *)buffer;
		sptr = buffer + pbuf->data_off;
		slen = strlen(sptr);
		if (pbuf->data_len != slen) {
			optr = sptr + slen + 1;
			/* check for optional data's DN tag */
			if (strbw(optr, NSS_LDAP_DN_TAG)) { /* user DN */
				/* first get server type string */
				optr = optr + NSS_LDAP_DN_TAG_LEN;
				dptr = strchr(optr, ':');
				*dptr = '\0';

				/* then set server type */
				if (strcmp(optr,
				    NS_LDAP_ATTR_VAL_SERVER_AD) == 0)
					serverType = NS_LDAP_SERVERTYPE_AD;
				else if (strcmp(optr,
				    NS_LDAP_ATTR_VAL_SERVER_OPENLDAP) == 0)
					serverType =
					    NS_LDAP_SERVERTYPE_OPENLDAP;

				/* user DN follows server type */
				user_dn = dptr + 1;
			}
		}
	}

	/*
	 * If no user DN found in cache, call __ns_ldap_list_ext to get it.
	 */
	if (user_dn == NULL) {
		ret = snprintf(searchfilter, sizeof (searchfilter),
		    _F_GETPWNAM, key);
		if (ret >= sizeof (searchfilter) || ret < 0) {
			ret = NSS_NOTFOUND;
			goto cleanup;
		}
		ret = snprintf(userdata, sizeof (userdata), _F_GETPWNAM_SSD,
		    key);
		if (ret >= sizeof (userdata) || ret < 0) {
			ret = NSS_NOTFOUND;
			goto cleanup;
		}

		ret = __ns_ldap_list_ext(_PASSWD, searchfilter,
		    _merge_SSD_filter, pw_attrs, NULL, NS_LDAP_NOT_CVT_DN,
		    &result1, &error, NULL, userdata, gr_extra_info_attr,
		    &extra_info);
		if (ret == NS_LDAP_NOTFOUND) {
			ret = NSS_NOTFOUND;
			goto cleanup;
		} else if (ret != NS_LDAP_SUCCESS) {
			pbuf->p_errno = errno;
			ret = NSS_ERROR;
			goto cleanup;
		}

		dn = __ns_ldap_getAttr(result1->entry, "dn");
		if (dn == NULL || dn[0] == NULL) {
			ret = NSS_ERROR;
			goto cleanup;
		}
		user_dn = dn[0];

		/* Determine which type of server the entry is from. */
		if (extra_info != NULL) {
			serverType = _nss_ldap_get_server_type(extra_info,
			    NULL);
			__ns_ldap_freeEntry(extra_info);
			extra_info = NULL;
		}
	}

	/*
	 * Set up the group result control block which is at the start
	 * of result area.
	 */
	setup_buf_memberof_res(*bufpp, &gres);
	pbuf = (nss_pheader_t *)*bufpp;
	old_dlen = gres->next_byte_offset;

	/* Escape any special characters in the user_dn first. */
	if (_ldap_filter_name(searchDN, user_dn, sizeof (searchDN)) != 0) {
		ret = NSS_NOTFOUND;
		goto cleanup;
	}

	/*
	 * If the ldap server is an AD, do a Transitive Link Expansion
	 * search to get all parent groups in one search.
	 */
	if (serverType == NS_LDAP_SERVERTYPE_AD) {
		ret = snprintf(searchfilter, sizeof (searchfilter),
		    _F_GETGRMOF_BL, searchDN);
		if (ret >= sizeof (searchfilter) || ret < 0) {
			ret = NSS_NOTFOUND;
			goto cleanup;
		}

		/*
		 * Use a callback function to process each result entry.
		 * userdata is the callback control block.
		 */
		memberof_tc.bufpp = bufpp;
		memberof_tc.grespp = &gres;
		memberof_tc.pbufpp = &pbuf;

		ret = __ns_ldap_list_sort(_GROUP, searchfilter,
		    _G_NAME, NULL, gr_attrs_mof_tl, NULL,
		    NS_LDAP_PAGE_CTRL, &result, &error,
		    TLE_memberof_cb, &memberof_tc);
		if (result != NULL)
			(void) __ns_ldap_freeResult(&result);
		if (error != NULL)
			(void) __ns_ldap_freeError(&error);

		/*
		 * Done if no error and have result data.
		 * libsldap returns NS_LDAP_NOTFOUND if
		 * all result entries were consumed by
		 * the callback function.
		 */
		if (ret == NS_LDAP_NOTFOUND &&
		    old_dlen != gres->next_byte_offset) {
			pbuf->data_len = gres->next_byte_offset;
			ret = NSS_SUCCESS;
			goto cleanup;
		}

		/*
		 * Otherwise clean up incomplete result data in the buffer
		 * by resetting the result control block.
		 */
		if (old_dlen != gres->next_byte_offset) {
			setup_buf_memberof_res(*bufpp, &gres);
			pbuf = (nss_pheader_t *)*bufpp;
			old_dlen = gres->next_byte_offset;
		}
	}

	/*
	 * Not AD, or the Transitive Link Expansion search didn't work,
	 * do a search to get all the group entries which have the
	 * key as a memberuid or user_dn as one of the values of its
	 * member or uniqueMember attribute.
	 */

	ret = snprintf(searchfilter, sizeof (searchfilter), _F_GETGRUSR,
	    key, searchDN, searchDN, searchDN);
	if (ret >= sizeof (searchfilter) || ret < 0) {
		ret = NSS_NOTFOUND;
		goto cleanup;
	}
	ret = snprintf(userdata, sizeof (userdata), _F_GETGRUSR_SSD,
	    key, searchDN, searchDN, searchDN);
	if (ret >= sizeof (userdata) || ret < 0) {
		ret = NSS_NOTFOUND;
		goto cleanup;
	}

	/*
	 * If the ldap server is an openLDAP, don't sort the results
	 * as there's no order rule for most attributes.
	 */
	if (serverType == NS_LDAP_SERVERTYPE_OPENLDAP) {
		gres->no_sort_search = B_TRUE;
		ret = __ns_ldap_list(_GROUP, searchfilter,
		    _merge_SSD_filter, gr_attrs5, NULL, 0,
		    &result, &error, NULL, userdata);
	} else {
		ret = __ns_ldap_list_sort(_GROUP, searchfilter, _G_NAME,
		    _merge_SSD_filter, gr_attrs5, NULL, NS_LDAP_PAGE_CTRL,
		    &result, &error, NULL, userdata);
	}

	if (ret == NS_LDAP_NOTFOUND) {
		ret = NSS_NOTFOUND;
		goto cleanup;
	} else if (ret != NS_LDAP_SUCCESS) {
		ret = NSS_ERROR;
		goto cleanup;
	}

	/*
	 * Store gidnumber and entry DN from each result entry in the
	 * result area.
	 */
	for (curEntry = result->entry, i = 0; i < result->entries_count;
	    i++, curEntry = curEntry->next) {
		boolean_t username_matched = B_FALSE;
		boolean_t memberDN_matched = B_FALSE;

		/*
		 * Check to see if this group is found by a matched username.
		 */
		members = __ns_ldap_getAttrStruct(curEntry, _G_MEMBERUID);
		if (members != NULL && members->attrvalue != NULL) {
			membervalue = members->attrvalue;
			for (k = 0; k < members->value_count; k++) {
				if (strcmp(membervalue[k], key) == 0) {
					username_matched = B_TRUE;
					break;
				}
			}
		}

		/* Otherwise, found by a matched member/uniqueMember DN ? */
		if (!username_matched) {
			members = __ns_ldap_getAttrStruct(curEntry, _G_MEMBER);
			if (members != NULL && members->attrvalue != NULL)
				memberDN_matched = B_TRUE;
			else {
				members = __ns_ldap_getAttrStruct(curEntry,
				    _G_UNIQUEMEMBER);
				if (members != NULL &&
				    members->attrvalue != NULL)
					memberDN_matched = B_TRUE;
			}
		}

		/*
		 * Not matched ? Most likely, unamename not listed as
		 * one of the memberUid attribute values, skip.
		 */
		if (!username_matched && !memberDN_matched)
			continue;

		ret = pack_group_gid_dn(bufpp, curEntry, &gres,
		    &pbuf, is_nested_group(curEntry));
	}

	/* Return NSS_NOTFOUND if no new data added. */
	if (old_dlen == gres->next_byte_offset) {
		ret = NSS_NOTFOUND;
	} else {
		ret = NSS_SUCCESS;
		pbuf->data_len = gres->next_byte_offset;
	}

cleanup:
	if (buffer != NULL)
		free(buffer);
	if (result != NULL)
		(void) __ns_ldap_freeResult(&result);
	if (result1 != NULL)
		(void) __ns_ldap_freeResult(&result1);
	if (error != NULL)
		(void) __ns_ldap_freeError(&error);
	pbuf->p_status = ret;
}

/*
 * _nss_ldap_memberof_psearch is the top level psearch function for
 * memberof searches. The search key stored in the packed buffer
 * header is tagged to indicate whether the search key is a member uid
 * or group DN. If uid, key_uid_psearch will be called. Otherwise,
 * key_groupDN_psearch will be called.
 */
static void
_nss_ldap_memberof_psearch(void **bufpp, size_t length)
{
	nss_pheader_t   	*pbuf;
	nss_XbyY_args_t		arg;
	nss_status_t		ret;
	const char		*key;
	char			*dbname;
	int			dbop = 0;

	pbuf = (nss_pheader_t *)*bufpp;

	ret = _nss_ldap_grgetkey(*bufpp, length, &dbname, &dbop, &arg);
	if (ret != NSS_SUCCESS) {
		pbuf->p_status = ret;
		return;
	}
	key = arg.key.name;

	/* Searching with a member uid or group DN ? */
	if (strbw(key, PSEARCH_TAG_UID)) {
		/* member uid */
		key_uid_psearch(key + PSEARCH_TAG_LEN, bufpp);
	} else if (strbw(key, PSEARCH_TAG_GROUP_DN) ||
	    strbw(key, PSEARCH_TAG_GROUP_DN_NS)) {
		/* group DN */
		key_groupDN_psearch(key, key + PSEARCH_TAG_LEN, bufpp);
	} else {
		pbuf->p_status = NSS_NOTFOUND;
		return;
	}
}

/*
 * get_parent_gids find all groups, including nested ones, that a user
 * is a member of and write the gid number of these groups in the output
 * gid array. It may call itself recursively when dealing with nested
 * group. The nest level is limited by MEMBEROF_MAX_RECURSIVE_CALL.
 */
static nss_status_t
get_parent_gids(struct nss_groupsbymem *argp, const char *tag,
    const char *key, _nss_ldap_list_t **gl, int *rec_callp)
{
	nss_pheader_t	*pbuf;
	nss_status_t	res;
	_memberof_res_t	*gres;
	char		*sptr, *sptr_s, *gp;
	char		*buffer = NULL;
	int		i, k;
	gid_t		gid;
	char		*new_tag;
	char		*gdn;
	int		gdn_len;
	char		*wbdn = NULL;

	/*
	 * If the depth of recursion reaches MEMBEROF_MAX_RECURSIVE_CALL
	 * then stop and return NSS_SUCCESS.
	 */
	*rec_callp += 1;
	if (*rec_callp == MEMBEROF_MAX_RECURSIVE_CALL) {
		res = NSS_SUCCESS;
		goto out;
	}

	/* create the memberof nscd backend cache if not done yet */
	if (memberof_cache_state == NSS_LDAP_CACHE_UNINITED)
		_nss_ldap_memberof_init();

	/*
	 * For loop detection, check to see if key has already been
	 * processed by adding it to the grlist. If not able to add,
	 * then it has been processed or error occurred, ignore it.
	 */
	if (nss_ldap_list_add(gl, key) != NSS_LDAP_LIST_SUCCESS) {
		res = NSS_SUCCESS;
		goto out;
	}

	/*
	 * Check to see if memberof result available in nscd's memberof cache.
	 * If not, get it from the LDAP server.
	 */
	res = do_lookup(LDAP_MEMBEROF_CACHE_NM, tag, key,
	    0, _PSEARCH_BUF_BLK, 0, &buffer,
	    _nss_ldap_grgetkey, _nss_ldap_memberof_psearch);
	if (res != NSS_SUCCESS)
		goto out;

	/* add gids to the output gid array */
	pbuf = (nss_pheader_t *)buffer;
	gp = buffer + pbuf->data_off;
	gres = (void *)gp;
	sptr = (char *)gres + sizeof (_memberof_res_t);
	if (gres->basedn_len != 0)
		sptr += gres->basedn_len + 1;
	sptr_s = sptr;
	for (i = 0; i < gres->parents; i++) {
		gid = (gid_t)strtol(sptr, (char **)NULL, 10);
		/* move pass gid */
		sptr += strlen(sptr) + 1;
		/* Skip DN of the group entry, it will be processed later. */
		sptr += strlen(sptr) + 1;
		/* no need to add if already in the output */
		for (k = 0; k < argp->numgids; k++) {
			if (argp->gid_array[k] == gid) {
				/* already exists */
				break;
			}
		}
		/* ignore duplicate gid */
		if (k  < argp->numgids)
			continue;

		argp->gid_array[argp->numgids++] = gid;
		if (argp->numgids >= argp->maxgids)
			break;
	}

	/*
	 * Done if no group DNs to process, i.e., no nested groups.
	 */
	if (gres->group_dns == 0) {
		res = NSS_SUCCESS;
		goto out;
	}

	sptr = sptr_s;
	for (i = gres->parents; --i >= 0; ) {
		if (argp->numgids >= argp->maxgids)
			break;
		/* group DN always follows the gid string, so move pass gid */
		gdn = sptr += strlen(sptr) + 1;
		/* if NULL group DN, not a nested group, nothing to do */
		if (*gdn == '\0') {
			sptr++;
			continue;
		}

		/* Restore group DN if it was truncated to save space. */
		if (gres->basedn_len > 0) {
			gdn_len = strlen(gdn);
			if (gdn[gdn_len - 1] == ',') {
				/*
				 * Get a work buffer from the stack
				 * that can be reused.
				 */
				if (wbdn == NULL)
					wbdn = (char *)alloca(gres->dnlen_max);

				/* drop the last ',' */
				(void) strlcpy(wbdn, gdn, gdn_len);
				/*
				 * basedn is the first string after the
				 * control struct
				 */
				(void) strlcat(wbdn, (char *)gres +
				    gres->basedn_offset, gres->dnlen_max);
				gdn = wbdn;
			}
		}

		/*
		 * If server is an openLDAP, do a no result sorting
		 * search.
		 */
		if (gres->no_sort_search)
			new_tag = PSEARCH_TAG_GROUP_DN_NS;
		else
			new_tag = PSEARCH_TAG_GROUP_DN;
		res = get_parent_gids(argp, new_tag, gdn, gl, rec_callp);
		if (res != NSS_SUCCESS && res != NSS_NOTFOUND)
			goto out;

		/* move pass group DN */
		sptr += strlen(sptr) + 1;
	}

	res = NSS_SUCCESS;

out:
	if (buffer != NULL)
		free(buffer);
	*rec_callp -= 1;
	return (res);
}

/*
 * getbymember returns all groups a user is defined in. This function
 * uses different architectural procedures than the other group backend
 * system calls because it's a private interface. This function constructs
 * an ldap search filter using the name invocation parameter. Once the
 * filter is constructed, we search for all matching groups counting
 * and storing each group name, gid, etc. Data marshaling is used for
 * group processing. The function _nss_ldap_group2ent() performs the
 * data marshaling.
 *
 * (const char *)argp->username;	(size_t)strlen(argp->username);
 * (gid_t)argp->gid_array;		(int)argp->maxgids;
 * (int)argp->numgids;
 */

static nss_status_t
getbymember(ldap_backend_ptr be, void *a)
{
	NOTE(ARGUNUSED(be))

	int			gcnt = (int)0;
	struct nss_groupsbymem	*argp = (struct nss_groupsbymem *)a;
	char			name[SEARCHFILTERLEN];
	int			rec_call = -1;
	_nss_ldap_list_t	*grlist = NULL;
	nss_status_t		res;

	if (strcmp(argp->username, "") == 0 ||
	    strcmp(argp->username, "root") == 0)
		return ((nss_status_t)NSS_NOTFOUND);

	if (_ldap_filter_name(name, argp->username, sizeof (name)) != 0)
		return ((nss_status_t)NSS_NOTFOUND);

	gcnt = (int)argp->numgids;

	res = get_parent_gids(argp, PSEARCH_TAG_UID, name, &grlist, &rec_call);
	if (grlist != NULL)
		nss_ldap_list_free(&grlist);
	if (res == NSS_ERROR)
		return (res);

	if (gcnt == argp->numgids)
		return ((nss_status_t)NSS_NOTFOUND);

	/*
	 * Return NSS_SUCCESS only if array is full.
	 * Explained in <nss_dbdefs.h>.
	 */
	return ((nss_status_t)((argp->numgids == argp->maxgids)
	    ? NSS_SUCCESS
	    : NSS_NOTFOUND));
}

static ldap_backend_op_t gr_ops[] = {
	_nss_ldap_destr,
	_nss_ldap_endent,
	_nss_ldap_setent,
	_nss_ldap_getent,
	getbynam,
	getbygid,
	getbymember
};


/*ARGSUSED0*/
nss_backend_t *
_nss_ldap_group_constr(const char *dummy1, const char *dummy2,
			const char *dummy3)
{

	return ((nss_backend_t *)_nss_ldap_constr(gr_ops,
	    sizeof (gr_ops)/sizeof (gr_ops[0]), _GROUP, gr_attrs,
	    _nss_ldap_group2str));
}
