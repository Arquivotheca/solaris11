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

#ifndef _SMB_SHARE_H
#define	_SMB_SHARE_H

#include <sys/param.h>
#include <sys/nvpair.h>
#include <smbsrv/string.h>
#include <smbsrv/smb_inet.h>
#include <smb/wintypes.h>
#include <smb/lmerr.h>

#ifdef _KERNEL
#include <smbsrv/smb_vops.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define	SMB_CVOL		"/var/smb/cvol"
#define	SMB_SYSROOT		SMB_CVOL "/windows"
#define	SMB_SYSTEM32		SMB_SYSROOT "/system32"
#define	SMB_VSS			SMB_SYSTEM32 "/vss"

/*
 * Share Properties:
 *
 * name			Advertised name of the share
 *
 * ad-container		Active directory container in which the share
 * 			will be published
 *
 * abe			Determines whether Access Based Enumeration is applied
 *			to a share
 *
 * csc			Client-side caching (CSC) options applied to this share
 * 	disabled	The client MUST NOT cache any files
 * 	manual		The client should not automatically cache every file
 * 			that it	opens
 * 	auto		The client may cache every file that it opens
 * 	vdo		The client may cache every file that it opens
 *			and satisfy file requests from its local cache.
 *
 * catia		CATIA character substitution
 *
 * guestok		Determines whether guest access is allowed
 *
 * next three properties use access-list a al NFS
 *
 * ro			list of hosts that will have read-only access
 * rw			list of hosts that will have read/write access
 * none			list of hosts that won't be allowed access
 */
#define	SHOPT_AD_CONTAINER	"ad-container"
#define	SHOPT_ABE		"abe"
#define	SHOPT_NAME		"name"
#define	SHOPT_CSC		"csc"
#define	SHOPT_CATIA		"catia"
#define	SHOPT_GUEST		"guestok"
#define	SHOPT_RO		"ro"
#define	SHOPT_RW		"rw"
#define	SHOPT_NONE		"none"
#define	SHOPT_DFSROOT		"dfsroot"
#define	SHOPT_DESC		"desc"
#define	SHOPT_DESCRIPTION	"description"

/*
 * RAP protocol share related commands only understand
 * share names in OEM format and there is a 13 char size
 * limitation
 */
#define	SMB_SHARE_OEMNAME_MAX		13
#define	SMB_SHARE_NTNAME_MAX		81
#define	SMB_SHARE_CMNT_MAX		(64 * MTS_MB_CHAR_MAX)

/*
 *	struct SHARE_INFO_1 {
 *		char		shi1_netname[13]
 *		char		shi1_pad;
 *		unsigned short	shi1_type
 *		char		*shi1_remark;
 *	}
 */
#define	SHARE_INFO_1_SIZE	(SMB_SHARE_OEMNAME_MAX + 1 + 2 + 4)

/*
 * Share flags:
 *
 * There are two types of flags:
 *
 *   - flags that represent a share property
 *   - other flags set at runtime
 *
 * Property flags:
 *
 * SMB_SHRF_CSC_DISABLED	Client-side caching is disabled for this share
 * SMB_SHRF_CSC_MANUAL	Manual client-side caching is allowed
 * SMB_SHRF_CSC_AUTO	Automatic client-side caching (CSC) is allowed
 * SMB_SHRF_CSC_VDO	Automatic CSC and local cache lookup is allowed
 * SMB_SHRF_ACC_OPEN	No restrictions set
 * SMB_SHRF_ACC_NONE	"none" property set
 * SMB_SHRF_ACC_RO	"ro" (readonly) property set
 * SMB_SHRF_ACC_RW	"rw" (read/write) property set
 * SMB_SHRF_ACC_ALL	All of the access bits
 * SMB_SHRF_CATIA	CATIA character translation on/off
 * SMB_SHRF_GUEST_OK	Guest access on/off
 * SMB_SHRF_ABE		Access Based Enumeration on/off
 * SMB_SHRF_DFSROOT	Share is a standalone DFS root
 * SMB_SHRF_SHORTNAME	Shortname support on/off
 *
 * Runtime flags:
 *
 * SMB_SHRF_TRANS	Transient share
 * SMB_SHRF_PERM	Permanent share
 * SMB_SHRF_AUTOHOME	Autohome share.
 * SMB_SHRF_ADMIN	Admin share
 *
 * All autohome shares are transient but not all transient shares are autohome.
 * IPC$ and drive letter shares (e.g. d$, e$, etc) are transient but
 * not autohome.
 */

/*
 * Property flags
 */
#define	SMB_SHRF_DFSROOT	0x0001
#define	SMB_SHRF_CATIA		0x0002
#define	SMB_SHRF_GUEST_OK	0x0004
#define	SMB_SHRF_ABE		0x0008

#define	SMB_SHRF_CSC_DISABLED	0x0010
#define	SMB_SHRF_CSC_MANUAL	0x0020
#define	SMB_SHRF_CSC_AUTO	0x0040
#define	SMB_SHRF_CSC_VDO	0x0080
#define	SMB_SHRF_CSC_MASK	0x00F0

#define	SMB_SHRF_ACC_OPEN	0x0000
#define	SMB_SHRF_ACC_NONE	0x0100
#define	SMB_SHRF_ACC_RO		0x0200
#define	SMB_SHRF_ACC_RW		0x0400
#define	SMB_SHRF_ACC_ALL	0x0F00

#define	SMB_SHRF_SHORTNAME	0x1000

/*
 * Runtime flags
 */
#define	SMB_SHRF_ADMIN		0x01000000
#define	SMB_SHRF_TRANS		0x10000000
#define	SMB_SHRF_PERM		0x20000000
#define	SMB_SHRF_AUTOHOME	0x40000000

#define	SMB_SHARE_PRINT		"print$"
#define	SMB_SHARE_PRINT_LEN	6

#define	SMB_SHRKEY_NAME		1
#define	SMB_SHRKEY_WINPATH	2

#define	SMB_SHRBUF_SIZE		(8 * 1024)

#define	SMB_SHARE_MAGIC		0x4B534852	/* KSHR */

typedef struct smb_share {
	uint32_t	shr_magic;
	char		*shr_name;
	char		*shr_path;
	char		*shr_cmnt;
	char		*shr_container;
	char		*shr_access_none;
	char		*shr_access_ro;
	char		*shr_access_rw;
	char		*shr_oemname;
	char		*shr_winpath;
	uint32_t	shr_flags;
	uint32_t	shr_type;
	uid_t		shr_uid;		/* autohome only */
	gid_t		shr_gid;		/* autohome only */
	uchar_t		shr_drive;
#ifdef _KERNEL
	uint32_t	shr_refcnt;
	uint32_t	shr_autocnt;
	uint64_t	shr_kid;
	avl_node_t	shr_link;
	kmutex_t	shr_mutex;
	smb_vfs_t	*shr_vfs;
	nvlist_t	*shr_nvdata;
#endif
} smb_share_t;

typedef struct smb_shr_execinfo {
	char		*e_sharename;
	char		*e_sharepath;
	char		*e_winname;
	char		*e_userdom;
	smb_inaddr_t	e_srv_ipaddr;
	smb_inaddr_t	e_cli_ipaddr;
	char		*e_cli_netbiosname;
	uid_t		e_uid;
	int		e_type;
} smb_shr_execinfo_t;

/*
 * Share notify operations
 */
#define	SMB_SHARE_NOP_PUBLISH	1
#define	SMB_SHARE_NOP_UNPUBLISH	2
#define	SMB_SHARE_NOP_REPUBLISH	3
#define	SMB_SHARE_NOP_POPULATE	4

#define	SMB_SHR_NOTIFY_MAGIC		0x4E544659	/* NTFY */
#define	SMB_SHR_NOTIFY_TIMEOUT		1		/* second */

typedef struct smb_shr_notify {
#ifdef _KERNEL
	list_node_t	sn_lnd;
	uint64_t	sn_kid;
	uint32_t	sn_eventid;
	uint32_t	sn_magic;
#endif
	uint32_t	sn_op;
	uint32_t	sn_dfsroot;
	char		*sn_name;
	char		*sn_path;
	char		*sn_container;
	char		*sn_newcontainer;
} smb_shr_notify_t;

uint32_t smb_share_add(const smb_share_t *);
uint32_t smb_share_remove(const char *);
uint32_t smb_share_count(void);
boolean_t smb_share_exists(const char *);
uint32_t smb_share_check(const char *, smb_share_t *);
uint32_t smb_share_lookup(const char *, smb_share_t *);
uint32_t smb_share_lmerr(int);
void smb_share_free(smb_share_t *);
char *smb_share_csc_name(const smb_share_t *);
void smb_share_csc_option(const char *, smb_share_t *);
boolean_t smb_share_csc_valid(const char *);

#ifdef __cplusplus
}
#endif

#endif /* _SMB_SHARE_H */
