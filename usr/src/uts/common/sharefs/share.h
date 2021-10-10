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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SHAREFS_SHARE_H
#define	_SHAREFS_SHARE_H

#include <sys/types.h>
#include <sys/nvpair.h>
#include <sys/param.h>
#include <sys/ioccom.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * struct share defines the format of an exported filesystem.
 *
 * It is also the interface between the userland tools and
 * the kernel.
 */
typedef struct share {
	char		*sh_path;
	char		*sh_res;
	char		*sh_fstype;
	char		*sh_opts;
	char		*sh_descr;
	size_t		sh_size;
	struct share	*sh_next;
} share_t;

#ifdef _SYSCALL32
typedef struct share32 {
	caddr32_t	sh_path;
	caddr32_t	sh_res;
	caddr32_t	sh_fstype;
	caddr32_t	sh_opts;
	caddr32_t	sh_descr;
	size32_t	sh_size;
	caddr32_t	sh_next;
} share32_t;
#endif /* _SYSCALL32 */

#define	SHARETAB	"/etc/dfs/sharetab"
#define	MAXBUFSIZE	65536

/*
 * Flavors of the system call.
 */

typedef enum sharefs_proto {
	SHAREFS_NFS,
	SHAREFS_SMB
} sharefs_proto_t;

typedef enum sharefs_op {
	SHAREFS_PUBLISH,
	SHAREFS_UNPUBLISH
} sharefs_op_t;

/*
 * sharefs ioctl interface
 */
#define	SHAREFS_IOC_VERSION	0x53465331 /* SFS1 */

#define	SHAREFS_IOC_BASE	(('S' << 16) | ('H' << 8))
#define	SHAREFS_IOC_LOOKUP	_IOR(SHAREFS_IOC_BASE, 1, int)
#define	SHAREFS_IOC_FIND_INIT	_IOR(SHAREFS_IOC_BASE, 2, int)
#define	SHAREFS_IOC_FIND_NEXT	_IOR(SHAREFS_IOC_BASE, 3, int)
#define	SHAREFS_IOC_FIND_FINI	_IOR(SHAREFS_IOC_BASE, 4, int)

typedef struct sharefs_ioc_hdr {
	uint32_t	version;
	uint32_t	crc;
	uint32_t	len;
	uint32_t	cmd;
} sharefs_ioc_hdr_t;

#define	SHAREFS_SH_NAME_MAX	128

typedef struct sharefs_ioc_lookup {
	sharefs_ioc_hdr_t	hdr;
	char			sh_name[SHAREFS_SH_NAME_MAX];
	char			sh_path[MAXPATHLEN];
	uint32_t		proto;
	uint32_t		shrlen;
	char			share[1];
} sharefs_ioc_lookup_t;

typedef struct sharefs_find_hdl {
	uint32_t id;
	uint32_t proto;
	uint32_t all_shares;
	uint32_t mpn_id;
	uint32_t scn_id;
} sharefs_find_hdl_t;

typedef struct sharefs_ioc_find_init {
	sharefs_ioc_hdr_t	hdr;
	char			mntpnt[MAXPATHLEN];
	uint32_t		proto;
	sharefs_find_hdl_t	hdl;
} sharefs_ioc_find_init_t;

typedef struct sharefs_ioc_find_next {
	sharefs_ioc_hdr_t	hdr;
	sharefs_find_hdl_t	hdl;
	uint32_t		shrlen;
	char			share[1];
} sharefs_ioc_find_next_t;

typedef struct sharefs_ioc_find_fini {
	sharefs_ioc_hdr_t	hdr;
	sharefs_find_hdl_t	hdl;
} sharefs_ioc_find_fini_t;

typedef union sharefs_ioc {
	sharefs_ioc_hdr_t	ioc_hdr;
	sharefs_ioc_lookup_t	ioc_lookup;
	sharefs_ioc_find_init_t	ioc_find_init;
	sharefs_ioc_find_next_t	ioc_find_next;
	sharefs_ioc_find_fini_t	ioc_find_fini;
} sharefs_ioc_t;

#ifdef _KERNEL

typedef int (*sharefs_sop_t)(sharefs_op_t, void *, size_t);
typedef int (*sharefs_secpolicy_op_t)(char *, cred_t *);

extern int sharefs(sharefs_proto_t, sharefs_op_t, void *, size_t);
extern int sharefs_register(sharefs_proto_t, sharefs_sop_t);
extern int sharetab_add(nvlist_t *, char *, char *);
extern int sharetab_remove(char *, char *);
extern void sharefs_secpolicy_register(int, sharefs_secpolicy_op_t);
extern int sharefs_secpolicy_share(char *);

#else

extern int sharefs(sharefs_proto_t, sharefs_op_t, void *, size_t);

#endif

#ifdef __cplusplus
}
#endif

#endif /* !_SHAREFS_SHARE_H */
