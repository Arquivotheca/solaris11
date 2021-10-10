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
 * This file contains the structure definitions in support of the the mdb
 * commands and walkers which interact with the NFSv4 file system hash (fsh)
 * table.
 */

#ifndef	_RFS4_FSH_H
#define	_RFS4_FSH_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <mdb/mdb_modapi.h>
#include <nfs/nfs4.h>

typedef struct rfs4_fsh_addr_walk_data {
	uintptr_t rfawd_bkt_addr;	/* address of the current bucket */
	int rfawd_num_buckets;		/* number of buckets in fsh_table */
	int rfawd_idx;			/* current bucket we are on */
} rfs4_fsh_addr_walk_data_t;

typedef struct rfs4_fsh_val_walk_data {
	fsh_bucket_t *rfvwd_table;	/* copy of the fsh_table */
	int rfvwd_size;			/* size of fsh_table */
	int rfvwd_num_buckets;		/* number of buckets in fsh_table */
	int rfvwd_idx;			/* current bucket we are on */
} rfs4_fsh_val_walk_data_t;

#ifdef  __cplusplus
}
#endif

#endif /* _RFS4_FSH_H */
