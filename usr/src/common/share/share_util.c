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

#ifdef _KERNEL
#include <sys/sunddi.h>
#else
#include <string.h>
#endif
#include <sys/sa_share.h>

#define	CRC_POLYNOMIAL	0xD8B5D8B5

#define	SA_PROT_FIRST		0
#define	SA_PROT_LAST		1
#define	SA_PROT_END		3
#define	SA_PROT_SHARING_COUNT	2

typedef struct {
	sa_proto_t spi_type;
	char *spi_name;
} sa_proto_info_t;

/*
 * Protocol specific functions and data
 */
sa_proto_info_t sa_proto_info[] = {
	{SA_PROT_NFS, "nfs"},
	{SA_PROT_SMB, "smb"},
	{SA_PROT_AUTOFS, "autofs"},
};

uint32_t
sa_crc_gen(uint8_t *buf, size_t len)
{
	uint32_t crc = CRC_POLYNOMIAL;
	uint8_t *p;
	int i;

	for (p = buf, i = 0; i < len; ++i, ++p) {
		crc = (crc ^ (uint32_t)*p) + (crc << 12);

		if (crc == 0 || crc == 0xFFFFFFFF)
			crc = CRC_POLYNOMIAL;
	}

	return (crc);
}

sa_proto_t
sa_proto_get_type(int i)
{
	return (sa_proto_info[i].spi_type);
}

char *
sa_proto_get_name(int i)
{
	return (sa_proto_info[i].spi_name);
}

/*
 * sa_proto_first
 * as defined in sa_proto_info table.
 *
 * return first protocol type in list.
 */
sa_proto_t
sa_proto_first(void)
{
	return (sa_proto_info[SA_PROT_FIRST].spi_type);
}

/*
 * sa_proto_next
 *
 * return next protocol type in list
 * as defined in sa_proto_info table.
 */
sa_proto_t
sa_proto_next(sa_proto_t p)
{
	int i;

	if (p == SA_PROT_NONE)
		return (sa_proto_info[SA_PROT_FIRST].spi_type);

	for (i = SA_PROT_FIRST; i < SA_PROT_LAST; i++) {
		if (sa_proto_info[i].spi_type == p)
			return (sa_proto_info[i+1].spi_type);
	}

	return (SA_PROT_NONE);
}

/*
 * return number of sharing protocols supported
 */
int
sa_proto_count(void)
{
	return (SA_PROT_SHARING_COUNT);
}

/*
 * sa_val_to_proto
 *
 * return sa_prot_t for specified protocol string
 * as defined in sa_proto_info table.
 */
sa_proto_t
sa_val_to_proto(const char *pval)
{
	int i;

	for (i = SA_PROT_FIRST; i < SA_PROT_END; ++i) {
		if (strcasecmp(pval, sa_proto_info[i].spi_name) == 0)
			return (sa_proto_info[i].spi_type);
	}

	return (SA_PROT_NONE);
}

/*
 * sa_proto_to_val
 *
 * return protocol string from sa_proto_t
 */
char *
sa_proto_to_val(sa_proto_t proto)
{
	int i;

	for (i = SA_PROT_FIRST; i < SA_PROT_END; ++i) {
		if (sa_proto_info[i].spi_type == proto)
			return (sa_proto_info[i].spi_name);
	}

	return ("");
}

/*
 * sa_share_alloc
 *
 * allocate an nvlist_t and populate it with share
 * name and share path if supplied.
 */
nvlist_t *
sa_share_alloc(const char *sh_name, const char *sh_path)
{
	nvlist_t *share;

	if (nvlist_alloc(&share, NV_UNIQUE_NAME, 0) != 0)
		return (NULL);

	if (sh_name != NULL)
		if (sa_share_set_name(share, sh_name) != SA_OK) {
			sa_share_free(share);
			return (NULL);
		}

	if (sh_path != NULL)
		if (sa_share_set_path(share, sh_path) != SA_OK) {
			sa_share_free(share);
			return (NULL);
		}

	return (share);
}

/*
 * sa_share_free
 *
 * free a previously allocated share nvlist_t
 */
void
sa_share_free(nvlist_t *share)
{
	if (share != NULL)
		nvlist_free(share);
}

/*
 * sa_share_get_name
 *
 * Return a pointer to the name property value if it exists.
 * The returned string is valid for life of share, do not free.
 */
char *
sa_share_get_name(nvlist_t *share)
{
	char *sh_name;

	if (share == NULL)
		return (NULL);

	if (nvlist_lookup_string(share, SA_PROP_NAME, &sh_name) != 0)
		return (NULL);

	return (sh_name);
}

/*
 * sa_share_set_name
 *
 * Add name=sh_name to the share
 * If name currently exists, it will be replaced with sh_name
 */
int
sa_share_set_name(nvlist_t *share, const char *sh_name)
{
	if (share == NULL)
		return (SA_INVALID_SHARE);

	if (sh_name == NULL)
		return (SA_INVALID_SHARE_NAME);

	if (nvlist_add_string(share, SA_PROP_NAME, sh_name) != 0)
		return (SA_NO_MEMORY);

	return (SA_OK);
}

/*
 * sa_share_get_path
 *
 * Return a pointer to the path property value if it exists.
 * The returned string is valid for life of share, do not free
 */
char *
sa_share_get_path(nvlist_t *share)
{
	char *shr_path;

	if (share == NULL)
		return (NULL);

	if (nvlist_lookup_string(share, SA_PROP_PATH, &shr_path) != 0)
		return (NULL);

	return (shr_path);
}

/*
 * sa_share_set_path
 *
 * Add path=sh_path to the share.
 * If path currently exists, it will be replaced with sh_path
 */
int
sa_share_set_path(nvlist_t *share, const char *sh_path)
{
	if (share == NULL)
		return (SA_INVALID_SHARE);

	if (sh_path == NULL)
		return (SA_INVALID_SHARE_PATH);

	if (nvlist_add_string(share, SA_PROP_PATH, sh_path) != 0)
		return (SA_NO_MEMORY);

	return (SA_OK);
}

/*
 * sa_share_get_desc
 *
 * returned a pointer to description string if it exists.
 * The string is valid for life of share, do not free
 */
char *
sa_share_get_desc(nvlist_t *share)
{
	char *sh_desc;

	if (share == NULL)
		return (NULL);

	if (nvlist_lookup_string(share, SA_PROP_DESC, &sh_desc) != 0)
		return (NULL);

	return (sh_desc);
}

/*
 * sa_share_set_desc
 *
 * Add the desc=sh_desc pair to the share nvlist.
 * If desc currently exists, it will be replaced.
 *
 */
int
sa_share_set_desc(nvlist_t *share, const char *sh_desc)
{
	if (share == NULL)
		return (SA_INVALID_SHARE);

	if (sh_desc == NULL)
		return (SA_INVALID_PROP_VAL);

	if (nvlist_add_string(share, SA_PROP_DESC, sh_desc) != 0)
		return (SA_NO_MEMORY);

	return (SA_OK);
}

/*
 * sa_share_rem_desc
 *
 * Remove the desc=sh_desc pair from share nvlist.
 *
 */
int
sa_share_rem_desc(nvlist_t *share)
{
	if (share == NULL)
		return (SA_INVALID_SHARE);

	return (sa_share_rem_prop(share, SA_PROP_DESC));
}

/*
 * sa_share_get_mntpnt
 *
 * returned a pointer to mntpnt string if it exists.
 * The string is valid for life of share, do not free
 */
char *
sa_share_get_mntpnt(nvlist_t *share)
{
	char *mntpnt;

	if (share == NULL)
		return (NULL);

	if (nvlist_lookup_string(share, SA_PROP_MNTPNT, &mntpnt) != 0)
		return (NULL);

	return (mntpnt);
}

/*
 * sa_share_set_mntpnt
 *
 * Add the mntpnt=mntpnt pair to the share nvlist.
 * If mntpnt currently exists, it will be replaced.
 *
 */
int
sa_share_set_mntpnt(nvlist_t *share, const char *mntpnt)
{
	if (share == NULL)
		return (SA_INVALID_SHARE);

	if (mntpnt == NULL)
		return (SA_INVALID_SHARE_MNTPNT);

	if (nvlist_add_string(share, SA_PROP_MNTPNT, mntpnt) != 0)
		return (SA_NO_MEMORY);

	return (SA_OK);
}

/*
 * sa_share_is_transient
 *
 * Returns true if the share contains the transient property.
 */
boolean_t
sa_share_is_transient(nvlist_t *share)
{
	char *val;

	if (share == NULL)
		return (B_FALSE);

	if (nvlist_lookup_string(share, SA_PROP_TRANS, &val) != 0)
		return (B_FALSE);

	if (strcmp(val, "true") != 0)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * sa_share_set_transient
 *
 * Add the transient=true pair to the share nvlist.
 * This marks the share as transient.
 *
 */
int
sa_share_set_transient(nvlist_t *share)
{
	if (share == NULL)
		return (SA_INVALID_SHARE);

	if (nvlist_add_string(share, SA_PROP_TRANS, "true") != 0)
		return (SA_NO_MEMORY);

	return (SA_OK);
}

/*
 * sa_share_get_status
 *
 * returns bit mask of currently shared protocols.
 */
sa_proto_t
sa_share_get_status(nvlist_t *share)
{
	sa_proto_t proto;

	if (share == NULL)
		return (NULL);

	if (nvlist_lookup_uint32(share, SA_PROP_STATUS, &proto) != 0)
		return (SA_PROT_NONE);

	return (proto);
}

/*
 * sa_share_set_status
 *
 * Add the protocol bit mask to the share.
 * This should be the list of shared protocols.
 *
 */
int
sa_share_set_status(nvlist_t *share, sa_proto_t proto)
{
	if (share == NULL)
		return (SA_INVALID_SHARE);

	if (nvlist_add_uint32(share, SA_PROP_STATUS, proto) != 0)
		return (SA_NO_MEMORY);

	return (SA_OK);
}

/*
 * sa_share_get_prop
 *
 * Return a pointer to the property value of the specified propname
 * if it exists in the proplist.
 *
 * returned string is valid for life of share, do not free
 */
char *
sa_share_get_prop(nvlist_t *proplist, const char *propname)
{
	char *propval;

	if (proplist == NULL || propname == NULL)
		return (NULL);

	if (nvlist_lookup_string(proplist, propname, &propval) != 0)
		return (NULL);

	return (propval);
}

/*
 * sa_share_set_prop
 *
 * Add the propname=propval pair to the nvlist proplist.
 * If propname currently exists, it will be replaced.
 */
int
sa_share_set_prop(nvlist_t *proplist, const char *propname, const char *propval)
{
	if (proplist == NULL)
		return (SA_INVALID_SHARE);

	if (propname == NULL)
		return (SA_INVALID_PROP);

	if (propval == NULL)
		return (SA_INVALID_PROP_VAL);

	if (nvlist_add_string(proplist, propname, propval) != 0)
		return (SA_NO_MEMORY);

	return (SA_OK);
}

/*
 * sa_share_rem_prop
 *
 * Remove the propname nvpair from the nvlist proplist.
 */
int
sa_share_rem_prop(nvlist_t *proplist, const char *propname)
{
	if (proplist == NULL)
		return (SA_INVALID_SHARE);

	if (propname == NULL)
		return (SA_INVALID_PROP);

	(void) nvlist_remove(proplist, propname, DATA_TYPE_STRING);

	return (SA_OK);
}

/*
 * sa_share_get_proto
 *
 * returns a pointer to the protocol nvlist for the specified proto
 *
 * The returned nvlist is valid for life of share, do not free
 * or hold for long
 */
nvlist_t *
sa_share_get_proto(nvlist_t *share, sa_proto_t proto)
{
	nvlist_t *prot_nvl;

	if (nvlist_lookup_nvlist(share, sa_proto_to_val(proto),
	    &prot_nvl) == 0)
		return (prot_nvl);
	else
		return (NULL);
}

/*
 * sa_share_set_proto
 *
 * set the protocol nvlist to the share. Any existing protocol nvlist
 * of the same protocol type will be replaced.
 */
int
sa_share_set_proto(nvlist_t *share, sa_proto_t proto, nvlist_t *prot_nvl)
{
	if (nvlist_add_nvlist(share, sa_proto_to_val(proto),
	    prot_nvl) != 0)
		return (SA_NO_MEMORY);
	else
		return (SA_OK);
}

int
sa_share_rem_proto(nvlist_t *share, sa_proto_t proto)
{
	if (nvlist_remove(share, sa_proto_to_val(proto), DATA_TYPE_NVLIST) != 0)
		return (SA_NO_SUCH_PROTO);
	else
		return (SA_OK);
}

int
sa_share_proto_count(nvlist_t *share)
{
	int count = 0;
	sa_proto_t p;

	for (p = sa_proto_first(); p != SA_PROT_NONE; p = sa_proto_next(p)) {
		if (sa_share_get_proto(share, p) != NULL)
			count++;
	}

	return (count);
}
