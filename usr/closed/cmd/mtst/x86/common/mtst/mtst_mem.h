/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MTST_MEM_H
#define	_MTST_MEM_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <sys/memtest.h>

#include <mtst_list.h>
#include <mtst_cmd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	MTST_MEM_RSRV_USER	0x1
#define	MTST_MEM_RSRV_KERNEL	0x2

typedef struct mtst_mem_rsrv {
	mtst_list_t mmr_list;		/* list of reservations */
	mtst_cmd_impl_t *mmr_cmd;	/* requestor of reservation */
	int mmr_resnum;			/* reservation number for client */
	int mmr_drvid;			/* reservation id from kernel */
	int mmr_type;			/* reservation type */
	memtest_memreq_t mmr_res;	/* reservation details */
} mtst_mem_rsrv_t;

extern int mtst_mem_rsrv(int, int *, size_t *, uint64_t *, uint64_t *);
extern int mtst_mem_unrsrv(int);

#ifdef __cplusplus
}
#endif

#endif /* _MTST_MEM_H */
