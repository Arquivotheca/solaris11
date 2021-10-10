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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_NFS_SRV_INST_H
#define	_NFS_SRV_INST_H

#include <sys/zone.h>
#include <sys/cladm.h>
#include <rpc/svc.h>

#ifdef	__cplusplus
extern "C" {
#endif


#define	RFS_INST_NAME_DFL	"default"
#define	RFS_INST_HANFS_ID_DFL	NODEID_UNKNOWN

#ifdef	_KERNEL

#define	RFS_UNIQUE_BUFLEN KSTAT_STRLEN

/*
 * forward declarations for structs used by protos in headers
 * included below.
 */
typedef struct rfs_zone rfs_zone_t;
typedef struct rfs_inst rfs_inst_t;
typedef struct rfs4_inst rfs4_inst_t;

void rfs_inst_hold(rfs_inst_t *);
void rfs_inst_rele(rfs_inst_t *);
int rfs_inst_active_tryhold(rfs_inst_t *, int);
void rfs_inst_active_rele(rfs_inst_t *);

int rfs_inst_start(int, int);
void rfs_inst_quiesce(void);
rfs_inst_t *rfs_inst_find(int);
rfs_inst_t *rfs_inst_svcreq_to_rip(struct svc_req *);
rfs_inst_t *rfs_inst_svcxprt_to_rip(SVCXPRT *);

rfs_zone_t *rfs_zone_find(zoneid_t, int);
void rfs_zone_hold(rfs_zone_t *);
void rfs_zone_rele(rfs_zone_t *);
zone_t *rfs_zoneid_to_zone(zoneid_t);
void rfs_zone_uniqstr(rfs_zone_t *, char *, char *, int);
void rfs_inst_uniqstr(rfs_inst_t *, char *, char *, int);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif
#endif	/* _NFS_SRV_INST_H */
