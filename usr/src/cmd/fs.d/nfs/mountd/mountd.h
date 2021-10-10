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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_MOUNTD_H
#define	_MOUNTD_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <door.h>
#include <nfs/nfs.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	MAXIPADDRLEN	512

struct nd_hostservlist;
extern void rmtab_load(void);
extern void mntlist_send(SVCXPRT *transp);
extern void mntlist_new(char *host, char *path);
extern void mntlist_delete(char *host, char *path);
extern void mntlist_delete_all(char *host);
extern void netgroup_init(void);
extern int  netgroup_check(struct nd_hostservlist *, char *, int);
extern void export(struct svc_req *);
extern void nfsauth_func(void *, char *, size_t, door_desc_t *, uint_t);
extern char *inet_ntoa_r(struct in_addr, char *);
extern int nfs_getfh(char *, int, int *, char *);

extern void nfsauth_prog(struct svc_req *, SVCXPRT *);

extern struct sh_list *share_list;
extern rwlock_t sharetab_lock;
extern void check_sharetab(void);

extern void log_cant_reply(SVCXPRT *);

extern void *exmalloc(size_t);

extern struct share *findentry(char *);
extern int check_client(struct share *, struct netbuf *,
				struct nd_hostservlist *, int);
extern struct nd_hostservlist *anon_client(char *host);

/*
 * These functions are defined here due to the fact
 * that we can not find the proper header file to
 * include. These functions are, at present, not
 * listed in any other header files.
 */
/*
 * These three functions are hidden functions in the
 * bsm libraries (libbsm).
 */
extern void audit_mountd_setup(void);
extern void audit_mountd_mount(char *, char *, int);
extern void audit_mountd_umount(char *, char *);

/*
 * This is a hidden function in the rpc libraries (libnsl).
 */
extern int __rpc_negotiate_uid(int);

/*
 * This appears to be a hidden function in libc.
 * Private interface to nss_search().
 * Accepts N strings rather than 1.
 */
extern  int __multi_innetgr();

#ifdef	__cplusplus
}
#endif

#endif	/* _MOUNTD_H */
