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

#ifndef _SYS_IB_CLIENTS_SDP_QUEUE_H
#define	_SYS_IB_CLIENTS_SDP_QUEUE_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Defines for object types.
 */

typedef enum {
	SDP_GENERIC_TYPE_UNKNOWN = 0x00,
	SDP_GENERIC_TYPE_BUFF = 0x01,
	SDP_GENERIC_TYPE_IOCB = 0x02,
	SDP_GENERIC_TYPE_ADVT = 0x03,
	SDP_GENERIC_TYPE_NONE
} sdp_generic_type_t;

struct sdp_generic_s;
/*
 * Object destruction callback type
 */
typedef int32_t(*sdp_gen_destruct_fn_t) (void  *pool,
    struct sdp_generic_s *element);

typedef int32_t(*sdp_generic_lookup_func_t) (struct sdp_generic_s *element,
    void *arg);

/*
 * SDP generic queue for multiple object types.
 */

typedef struct sdp_generic_s {
	struct sdp_generic_s *next;	/* next structure in table */
	struct sdp_generic_s *prev;	/* previous structure in table */
	uint32_t type;	/* element type. (for generic queue) */
	struct sdp_generic_table_s *table; /* table where this object belong */
	sdp_gen_destruct_fn_t release;	/* release the object */
} sdp_generic_t;

/*
 * Table for holding SDP advertisements.
 */
typedef struct sdp_generic_table_s {
	sdp_generic_t *head; /* double linked list of advertisements */
	int32_t size;	/* current number of advertisements in table */
	uint16_t count[SDP_GENERIC_TYPE_NONE]; /* object specific counter */
	kmutex_t	sgt_mutex;
} sdp_generic_table_t;

/*
 * SDP generic queue inline functions.
 */

extern int32_t sdp_generic_table_size(sdp_generic_table_t *table);
extern int32_t sdp_generic_table_member(sdp_generic_t *element);

#define	generic_table_size(x)   sdp_generic_table_size(x)
#define	generic_table_member(x) sdp_generic_table_member(x)

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_QUEUE_H */
