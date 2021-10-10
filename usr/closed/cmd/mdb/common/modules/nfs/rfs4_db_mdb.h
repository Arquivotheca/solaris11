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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_RFS4_DB_MDB_H
#define	_RFS4_DB_MDB_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <nfs/nfs4_db_impl.h>

int rfs4_db_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_db_tbl_walk_init(mdb_walk_state_t *w);
int rfs4_db_tbl_walk_step(mdb_walk_state_t *w);

int rfs4_tbl_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);

int rfs4_idx_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_db_idx_walk_init(mdb_walk_state_t *w);
int rfs4_db_idx_walk_step(mdb_walk_state_t *w);

int rfs4_bkt_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_db_bkt_walk_init(mdb_walk_state_t *w);
int rfs4_db_bkt_walk_step(mdb_walk_state_t *w);
void rfs4_db_bkt_walk_fini(mdb_walk_state_t *w);

int rfs4_clnt_kc_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_delegState_kc_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_file_kc_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_loSid_kc_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_lo_kc_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_oo_kc_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
int rfs4_osid_kc_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);

int nfs4_setopt_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _RFS4_DB_MDB_H */
