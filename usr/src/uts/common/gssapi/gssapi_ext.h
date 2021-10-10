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
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright 2008 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 */
/*
 * Private extensions and utilities to the GSS-API.
 * These are not part of the GSS-API specification
 * but may be useful to GSS-API users.
 */

#ifndef _GSSAPI_EXT_H
#define	_GSSAPI_EXT_H

#include <gssapi/gssapi.h>
#ifdef	_KERNEL
#include <sys/systm.h>
#else
#include <strings.h>
#endif


#ifdef	__cplusplus
extern "C" {
#endif

/* MACRO for comparison of gss_OID's */
#define	g_OID_equal(o1, o2) \
	(((o1)->length == (o2)->length) && \
	(memcmp((o1)->elements, (o2)->elements, (int)(o1)->length) == 0))


/*
 * MACRO for copying of OIDs - memory must already be allocated
 * o2 is copied to o1
 */
#define	g_OID_copy(o1, o2) \
	bcopy((o2)->elements, (o1)->elements, (o2)->length);\
	(o1)->length = (o2)->length;


/* MACRO to check if input buffer is valid */
#define	GSS_EMPTY_BUFFER(buf)	((buf) == NULL ||\
	(buf)->value == NULL || (buf)->length == 0)


/*
 * GSSAPI Extension functions -- these functions aren't
 * in the GSSAPI specification, but are provided in our
 * GSS library.
 */

#ifndef	_KERNEL

/*
 * qop configuration file handling.
 */
#define	MAX_QOP_NUM_PAIRS	128
#define	MAX_QOPS_PER_MECH	128

typedef struct _qop_num {
	char *qop;
	OM_uint32 num;
	char *mech;
} qop_num;

OM_uint32
__gss_qop_to_num(
	char		*qop,		/* input qop string */
	char		*mech,		/* input mech string */
	OM_uint32	*num		/* output qop num */
);

OM_uint32
__gss_num_to_qop(
	char		*mech,		/* input mech string */
	OM_uint32	num,		/* input qop num */
	char		**qop		/* output qop name */
);

OM_uint32
__gss_get_mech_info(
	char		*mech,		/* input mech string */
	char		**qops		/* buffer for return qops */
);

OM_uint32
__gss_mech_qops(
	char *mech,			/* input mech */
	qop_num *mech_qops,		/* mech qops buffer */
	int *numqops			/* buffer to return numqops */
);

OM_uint32
__gss_mech_to_oid(
	const char *mech,		/* mechanism string name */
	gss_OID *oid			/* mechanism oid */
);

const char *
__gss_oid_to_mech(
	const gss_OID oid		/* mechanism oid */
);

OM_uint32
__gss_get_mechanisms(
	char *mechArray[],		/* array to populate with mechs */
	int arrayLen			/* length of passed in array */
);

OM_uint32
__gss_get_mech_type(
	gss_OID oid,			/* mechanism oid */
	const gss_buffer_t token	/* token */
);

OM_uint32
__gss_userok(
	OM_uint32 *,		/* minor status */
	const gss_name_t,	/* remote user principal name */
	const char *,		/* local unix user name */
	int *);			/* remote principal ok to login w/out pw? */

OM_uint32
gsscred_expname_to_unix_cred(
	const gss_buffer_t,	/* export name */
	uid_t *,		/* uid out */
	gid_t *,		/* gid out */
	gid_t *[],		/* gid array out */
	int *);			/* gid array length */

OM_uint32
gsscred_name_to_unix_cred(
	const gss_name_t,	/* gss name */
	const gss_OID,		/* mechanim type */
	uid_t *,		/* uid out */
	gid_t *,		/* gid out */
	gid_t *[],		/* gid array out */
	int *);			/* gid array length */


/*
 * The following function will be used to resolve group
 * ids from a UNIX uid.
 */
OM_uint32
gss_get_group_info(
	const uid_t,		/* entity UNIX uid */
	gid_t *,		/* gid out */
	gid_t *[],		/* gid array */
	int *);			/* length of the gid array */



OM_uint32
gss_acquire_cred_with_password(
	OM_uint32 *		minor_status,
	const gss_name_t	desired_name,
	const gss_buffer_t	password,
	OM_uint32		time_req,
	const gss_OID_set	desired_mechs,
	int			cred_usage,
	gss_cred_id_t 		*output_cred_handle,
	gss_OID_set *		actual_mechs,
	OM_uint32 *		time_rec);

OM_uint32
gss_add_cred_with_password(
	OM_uint32		*minor_status,
	const gss_cred_id_t	input_cred_handle,
	const gss_name_t	desired_name,
	const gss_OID		desired_mech,
	const gss_buffer_t	password,
	gss_cred_usage_t	cred_usage,
	OM_uint32		initiator_time_req,
	OM_uint32		acceptor_time_req,
	gss_cred_id_t		*output_cred_handle,
	gss_OID_set		*actual_mechs,
	OM_uint32		*initiator_time_rec,
	OM_uint32		*acceptor_time_rec);

/* Solaris Kerberos: not supported yet */
#if 0 /************** Begin IFDEF'ed OUT *******************************/
/*
 * AEAD extensions
 */

OM_uint32 KRB5_CALLCONV gss_wrap_aead
	(OM_uint32 * /*minor_status*/,
	 gss_ctx_id_t /*context_handle*/,
	 int /*conf_req_flag*/,
	 gss_qop_t /*qop_req*/,
	 gss_buffer_t /*input_assoc_buffer*/,
	 gss_buffer_t /*input_payload_buffer*/,
	 int * /*conf_state*/,
	 gss_buffer_t /*output_message_buffer*/);

OM_uint32 KRB5_CALLCONV gss_unwrap_aead
	(OM_uint32 * /*minor_status*/,
	 gss_ctx_id_t /*context_handle*/,
	 gss_buffer_t /*input_message_buffer*/,
	 gss_buffer_t /*input_assoc_buffer*/,
	 gss_buffer_t /*output_payload_buffer*/,
	 int * /*conf_state*/,
	 gss_qop_t * /*qop_state*/);

#endif /**************** END IFDEF'ed OUT *******************************/
/*
 * SSPI extensions
 */
#define GSS_C_DCE_STYLE			0x1000
#define GSS_C_IDENTIFY_FLAG		0x2000
#define GSS_C_EXTENDED_ERROR_FLAG	0x4000

/*
 * Returns a buffer set with the first member containing the
 * session key for SSPI compatibility. The optional second
 * member contains an OID identifying the session key type.
 */
extern const gss_OID GSS_C_INQ_SSPI_SESSION_KEY;

typedef struct gss_iov_buffer_desc_struct {
    OM_uint32 type;
    gss_buffer_desc buffer;
} gss_iov_buffer_desc, *gss_iov_buffer_t;

#define GSS_C_NO_IOV_BUFFER		    ((gss_iov_buffer_t)0)

#define GSS_IOV_BUFFER_TYPE_EMPTY	    0
#define GSS_IOV_BUFFER_TYPE_DATA	    1	/* Packet data */
#define GSS_IOV_BUFFER_TYPE_HEADER	    2	/* Mechanism header */
#define GSS_IOV_BUFFER_TYPE_MECH_PARAMS	    3	/* Mechanism specific parameters */
#define GSS_IOV_BUFFER_TYPE_TRAILER	    7	/* Mechanism trailer */
#define GSS_IOV_BUFFER_TYPE_PADDING	    9	/* Padding */
#define GSS_IOV_BUFFER_TYPE_STREAM	    10	/* Complete wrap token */
#define GSS_IOV_BUFFER_TYPE_SIGN_ONLY	    11	/* Sign only packet data */

#define GSS_IOV_BUFFER_FLAG_MASK	    0xFFFF0000
#define GSS_IOV_BUFFER_FLAG_ALLOCATE	    0x00010000	/* indicates GSS should allocate */
#define GSS_IOV_BUFFER_FLAG_ALLOCATED	    0x00020000	/* indicates caller should free */

#define GSS_IOV_BUFFER_TYPE(_type)	    ((_type) & ~(GSS_IOV_BUFFER_FLAG_MASK))
#define GSS_IOV_BUFFER_FLAGS(_type)	    ((_type) & GSS_IOV_BUFFER_FLAG_MASK)

typedef struct gss_any *gss_any_t;

#else	/*	_KERNEL	*/

OM_uint32
kgsscred_expname_to_unix_cred(
	const gss_buffer_t expName,
	uid_t *uidOut,
	gid_t *gidOut,
	gid_t *gids[],
	int *gidsLen,
	uid_t uid);

OM_uint32
kgsscred_name_to_unix_cred(
	const gss_name_t intName,
	const gss_OID mechType,
	uid_t *uidOut,
	gid_t *gidOut,
	gid_t *gids[],
	int *gidsLen,
	uid_t uid);

OM_uint32
kgss_get_group_info(
	const uid_t puid,
	gid_t *gidOut,
	gid_t *gids[],
	int *gidsLen,
	uid_t uid);
#endif

/*
 * GGF extensions
 */
typedef struct gss_buffer_set_desc_struct {
    size_t count;
    gss_buffer_desc *elements;
} gss_buffer_set_desc, *gss_buffer_set_t;

#define	GSS_C_NO_BUFFER_SET ((gss_buffer_set_t)0)

OM_uint32 gss_create_empty_buffer_set
	(OM_uint32 *, /* minor_status */
	gss_buffer_set_t *); /* buffer_set */

OM_uint32 gss_add_buffer_set_member
	(OM_uint32 *, /* minor_status */
	const gss_buffer_t, /* member_buffer */
	gss_buffer_set_t *); /* buffer_set */

OM_uint32  gss_release_buffer_set
	(OM_uint32 *, /* minor_status */
	gss_buffer_set_t *); /* buffer_set */

#ifndef _KERNEL
OM_uint32 gss_inquire_sec_context_by_oid
	(OM_uint32 *, /* minor_status */
	const gss_ctx_id_t, /* context_handle */
	const gss_OID, /* desired_object */
	gss_buffer_set_t *); /* data_set */

/* XXX do these really belong in this header? */
/* OM_uint32 KRB5_CALLCONV gssspi_set_cred_option */
OM_uint32 gssspi_set_cred_option
	(OM_uint32 * /*minor_status*/,
	 gss_cred_id_t /*cred*/,
	 const gss_OID /*desired_object*/,
	 const gss_buffer_t /*value*/);

/* OM_uint32 KRB5_CALLCONV gssspi_mech_invoke */
OM_uint32 gssspi_mech_invoke
	(OM_uint32 * /*minor_status*/,
	 const gss_OID /*desired_mech*/,
	 const gss_OID /*desired_object*/,
	 gss_buffer_t /*value*/);
#endif /* #ifndef _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _GSSAPI_EXT_H */
