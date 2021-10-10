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

/*
 * libshare error codes
 */
#ifndef _SYS_SA_ERROR_H
#define	_SYS_SA_ERROR_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * defined error values
 */
#define	SA_OK			0
#define	SA_INTERNAL_ERR		1	/* internal error */
#define	SA_SYSTEM_ERR		2	/* system error, use errno */
#define	SA_NO_MEMORY		3	/* no memory */
#define	SA_SYNTAX_ERR		4	/* syntax error on command line */
#define	SA_NOT_IMPLEMENTED	5	/* operation not implemented */
#define	SA_NOT_SUPPORTED	6	/* operation not supported for proto */
#define	SA_BUSY			7	/* resource is busy */
#define	SA_CONFIG_ERR		8	/* configuration error */
#define	SA_SHARE_NOT_FOUND	9	/* share not found */
#define	SA_DUPLICATE_NAME	10	/* share name exists */
#define	SA_DUPLICATE_PATH	11	/* share with path exists */
#define	SA_DUPLICATE_PROP	12	/* property specified more than once */
#define	SA_DUPLICATE_PROTO	13	/* protocol specified more than once */
#define	SA_NO_SHARE_NAME	14	/* missing share name */
#define	SA_NO_SHARE_PATH	15	/* missing share path */
#define	SA_NO_SHARE_DESC	16	/* missing share description */
#define	SA_NO_SHARE_PROTO	17	/* missing share protocol */
#define	SA_NO_SECTION		18	/* missing section name */
#define	SA_NO_SUCH_PROTO	19	/* no such protocol */
#define	SA_NO_SUCH_PROP		20	/* property not found */
#define	SA_NO_SUCH_SECURITY	21	/* security set not found */
#define	SA_NO_SUCH_SECTION	22	/* section not found */
#define	SA_NO_PERMISSION	23	/* no permission */
#define	SA_INVALID_SHARE	24	/* invalid share */
#define	SA_INVALID_SHARE_NAME	25	/* invalid share name */
#define	SA_INVALID_SHARE_PATH	26	/* invalid share path */
#define	SA_INVALID_SHARE_MNTPNT	27	/* invalid share mntpnt */
#define	SA_INVALID_PROP		28	/* invalid property */
#define	SA_INVALID_SMB_PROP	29	/* invalid smb property */
#define	SA_INVALID_NFS_PROP	30	/* invalid nfs property */
#define	SA_INVALID_PROP_VAL	31	/* invalid property value */
#define	SA_INVALID_PROTO	32	/* invalid protocol */
#define	SA_INVALID_SECURITY	33	/* invalid security mode */
#define	SA_INVALID_UNAME	34	/* invalid username */
#define	SA_INVALID_UID		35	/* invalid uid */
#define	SA_INVALID_FNAME	36	/* invalid filename */
#define	SA_PARTIAL_PUBLISH	37	/* partial dataset publish */
#define	SA_PARTIAL_UNPUBLISH	38	/* partial dataset unpublish */
#define	SA_INVALID_READ_HDL	39	/* invalid read handle */
#define	SA_INVALID_PLUGIN	40	/* invalid plugin */
#define	SA_INVALID_PLUGIN_TYPE	41	/* invalid plugin type */
#define	SA_INVALID_PLUGIN_OPS	42	/* invalid plugin ops */
#define	SA_INVALID_PLUGIN_NAME	43	/* invalid plugin name */
#define	SA_NO_PLUGIN_DIR	44	/* no plugin directory */
#define	SA_NO_SHARE_DIR		45	/* no share directory */
#define	SA_PATH_NOT_FOUND	46	/* share path not found */
#define	SA_MNTPNT_NOT_FOUND	47	/* mountpoint not found */
#define	SA_NOT_SHARED_PROTO	48	/* not shared for protocol */
#define	SA_ANCESTOR_SHARED	49	/* ancestor is already shared */
#define	SA_DESCENDANT_SHARED	50	/* descendant is already shared */
#define	SA_XDR_ENCODE_ERR	51	/* XDR encode error */
#define	SA_XDR_DECODE_ERR	52	/* XDR decode error */
#define	SA_PASSWORD_ENC		53	/* passwords must be encrypted */
#define	SA_SCF_ERROR		54	/* service config facility error */
#define	SA_DOOR_ERROR		55	/* share cache door call failed */
#define	SA_STALE_HANDLE		56	/* stale find handle */
#define	SA_INVALID_ACCLIST_PROP_VAL	57	/* invalid access list prop */
#define	SA_SHARE_OTHERZONE	58	/* shares managed from other zone */
#define	SA_INVALID_ZONE		59	/* not supported in local zone */
#define	SA_PROTO_NOT_INSTALLED	60	/* protocol not installed */
#define	SA_INVALID_FSTYPE	61	/* invalid file system type */
#define	SA_READ_ONLY		62	/* read only file system */
#define	SA_LOCALE_NOT_SUPPORTED	63	/* locale not supported */
#define	SA_ACL_SET_ERROR	64	/* error setting share ACL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_SA_ERROR_H */
