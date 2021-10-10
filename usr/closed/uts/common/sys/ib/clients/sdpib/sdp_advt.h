/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * LEGAL NOTICE
 *
 * This file contains source code that implements the Sockets Direct
 * Protocol (SDP) as defined by the InfiniBand Architecture Specification,
 * Volume 1, Annex A4, Version 1.1.  Due to restrictions in the SDP license,
 * source code contained in this file may not be distributed outside of
 * Sun Microsystems without further legal review to ensure compliance with
 * the license terms.
 *
 * Sun employees and contactors are cautioned not to extract source code
 * from this file and use it for other purposes.  The SDP implementation
 * code in this and other files must be kept separate from all other source
 * code.
 *
 * As required by the license, the following notice is added to the source
 * code:
 *
 * This source code may incorporate intellectual property owned by
 * Microsoft Corporation.  Our provision of this source code does not
 * include any licenses or any other rights to you under any Microsoft
 * intellectual property.  If you would like a license from Microsoft
 * (e.g., to rebrand, redistribute), you need to contact Microsoft
 * directly.
 */

#ifndef	_SYS_IB_CLIENTS_SDP_ADVT_H
#define	_SYS_IB_CLIENTS_SDP_ADVT_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/ib/clients/sdpib/sdp_queue.h>

/* ADVT has an active read operation */
#define	SDP_ADVT_F_READ	0x00000001

struct sdp_advt_table_s;
/*
 * SDP read/write advertisements
 */
typedef struct sdp_advt_s {
	struct sdp_advt_s *next;	/* next structure in table */
	struct sdp_advt_s *prev;	/* previous structure in table */
	uint32_t type;	/* element type. (for generic queue) */
	struct sdp_advt_table_s *table; /* table to which this object belongs */
	sdp_gen_destruct_fn_t *release;	/* release the object */
	/*
	 * advertisement specific
	 */
	int32_t rkey;	/* advertised buffer remote key */
	intptr_t size;	/* advertised buffer size */
	int32_t post;	/* running total of data moved for this advert. */
	uint32_t wrid;	/* work request completing this advertisement */
	uint32_t flag;	/* advertisement flags. */
	uint64_t addr;	/* advertised buffer virtual address */
} sdp_advt_t;

/*
 * Table for holding SDP advertisements.
 */
typedef struct sdp_advt_table_s {
	sdp_advt_t *head;	/* double linked list of advertisements */
	int32_t size;	/* current number of advertisements in table */
	kmutex_t	sat_mutex;
} sdp_advt_table_t;

#define	conn_advt_table_size(table) ((table)->size)

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_ADVT_H */
