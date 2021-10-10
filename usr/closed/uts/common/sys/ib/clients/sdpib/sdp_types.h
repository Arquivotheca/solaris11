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

#ifndef _SYS_IB_CLIENTS_SDP_TYPES_H
#define	_SYS_IB_CLIENTS_SDP_TYPES_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _DEBUG
#define	SDP_EXPECT(expr)                                      \
{                                                                 \
	if (!(expr)) {                                                  \
		SDP_TRACE(T_TERSE, TRACE_FLOW_WARN,                       \
		    "EXPECT: internal error check <%s> failed.", #expr); \
	} /* if */                                                      \
}   /* SDP_EXPECT */

#define	SDP_CHECK_NULL(value, result) \
	ASSERT(value != NULL);

#define	SDP_CHECK_LT(value, bound, result) \
	if ((bound) > (value)) \
		return (result);
#define	SDP_CHECK_GT(value, bound, result) \
	if ((bound) < (value)) \
		return (result);
#define	SDP_CHECK_EQ(value, test, result) \
	if ((test) == (value)) \
		return (result);
#define	SDP_CHECK_EXPR(expr, result) \
	if (!(expr)) \
		return (result);
#else
#define	SDP_EXPECT(expr)
#define	SDP_CHECK_NULL(value, result)
#define	SDP_CHECK_LT(value, bound, result)
#define	SDP_CHECK_GT(value, bound, result)
#define	SDP_CHECK_EQ(value, test, result)
#define	SDP_CHECK_EXPR(expr, result)
#endif /* DEBUG */

#define	PTRSUM(a, b)	((uintptr_t)(((uintptr_t)(a)) + ((uintptr_t)(b))))
#define	PTRDIFF(a, b)	((uintptr_t)(((uintptr_t)(a)) - ((uintptr_t)(b))))
#define	PTR_GREATER_THAN(a, b) (((unsigned char *)(a)) > ((unsigned char *)(b)))

#define	TS_IB_HANDLE_INVALID (-1)

/*
 * Types
 */
typedef struct sdp_conn_struct_t sdp_conn_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_TYPES_H */
