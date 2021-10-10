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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_NFS4_MDB_H
#define	_NFS4_MDB_H

#ifdef	__cplusplus
extern "C" {
#endif



#include <nfs/nfs4.h>
#include <nfs/nfs4_clnt.h>
#include <nfs/rnode4.h>

extern int nfs4_print_verifier4(verifier4, int);
extern void nfs4_clientid4_print(clientid4 *, int *);
extern void nfs4_client_id4_print(nfs_client_id4 *);
extern int nfs4_print_stateid4(stateid4, int);
extern int nfs4_print_nfs_client_id4(nfs_client_id4, int);
extern int nfs4_print_open_owner4(open_owner4, int);
extern int nfs4_print_lock_owner4(lock_owner4, int);
extern int nfs4_print_cb_client4(cb_client4, int);
extern int nfs4_changeid4_print(changeid4);
extern uintptr_t nfs4_get_mimsg(uintptr_t);

#ifdef	__cplusplus
}
#endif

#endif /* _NFS4_MDB_H */
