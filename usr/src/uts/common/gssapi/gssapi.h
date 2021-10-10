/*
 * Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright 1993 by OpenVision Technologies, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of OpenVision not be used
 * in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission. OpenVision makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * OPENVISION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OPENVISION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_GSSAPI_H_
#define	_GSSAPI_H_

#ifdef	__cplusplus
extern "C" {
#endif


/*
 * First, include sys/types.h to get size_t defined.
 */
#include <sys/types.h>

/*
 * If the platform supports the xom.h header file, it should be
 * included here.
 */
#ifdef HAVE_XOM_H
#include <xom.h>
#endif

/*
 * Now define the three implementation-dependent types.
 */
struct gss_ctx_id;
struct gss_cred_id;
struct gss_name;

typedef struct gss_ctx_id  *gss_ctx_id_t;
typedef struct gss_cred_id *gss_cred_id_t;
typedef struct gss_name *gss_name_t;

/*
 * The following type must be defined as the smallest natural
 * unsigned integer supported by the platform that has at least
 * 32 bits of precision.
 */
typedef unsigned int gss_uint32;
typedef int gss_int32;


#ifdef OM_STRING
/*
 * We have included the xom.h header file.  Verify that OM_uint32
 * is defined correctly.
 */

#if sizeof (gss_uint32) != sizeof (OM_uint32)
#error Incompatible definition of OM_uint32 from xom.h
#endif

typedef OM_object_identifier gss_OID_desc, *gss_OID;

#else



/*
 * We can't use X/Open definitions, so roll our own.
 */

typedef gss_uint32 OM_uint32;

typedef struct gss_OID_desc_struct {
	OM_uint32 length;
	void*elements;
} gss_OID_desc, *gss_OID;

#endif

typedef struct gss_OID_set_desc_struct	{
	size_t  count;
	gss_OID elements;
} gss_OID_set_desc, *gss_OID_set;

#ifdef	_SYSCALL32
typedef struct gss_OID_desc_struct32 {
	OM_uint32 length;
	caddr32_t elements;
} gss_OID_desc32, *gss_OID32;
#endif	/* _SYSCALL32 */

typedef struct gss_buffer_desc_struct {
	size_t length;
	void *value;
} gss_buffer_desc, *gss_buffer_t;

typedef struct gss_channel_bindings_struct {
	OM_uint32 initiator_addrtype;
	gss_buffer_desc initiator_address;
	OM_uint32 acceptor_addrtype;
	gss_buffer_desc acceptor_address;
	gss_buffer_desc application_data;
} *gss_channel_bindings_t;

/*
 * For now, define a QOP-type as an OM_uint32
 */
typedef	OM_uint32 gss_qop_t;
typedef	int gss_cred_usage_t;

/*
 * Flag bits for context-level services.
 */
#define	GSS_C_DELEG_FLAG 1
#define	GSS_C_MUTUAL_FLAG 2
#define	GSS_C_REPLAY_FLAG 4
#define	GSS_C_SEQUENCE_FLAG 8
#define	GSS_C_CONF_FLAG 16
#define	GSS_C_INTEG_FLAG 32
#define	GSS_C_ANON_FLAG 64
#define	GSS_C_PROT_READY_FLAG 128
#define	GSS_C_TRANS_FLAG 256
#define	GSS_C_DELEG_POLICY_FLAG 32768

/*
 * Credential usage options
 */
#define	GSS_C_BOTH 0
#define	GSS_C_INITIATE 1
#define	GSS_C_ACCEPT 2

/*
 * Status code types for gss_display_status
 */
#define	GSS_C_GSS_CODE 1
#define	GSS_C_MECH_CODE 2

/*
 * The constant definitions for channel-bindings address families
 */
#define	GSS_C_AF_UNSPEC		0
#define	GSS_C_AF_LOCAL		1
#define	GSS_C_AF_INET		2
#define	GSS_C_AF_IMPLINK	3
#define	GSS_C_AF_PUP		4
#define	GSS_C_AF_CHAOS		5
#define	GSS_C_AF_NS		6
#define	GSS_C_AF_NBS		7
#define	GSS_C_AF_ECMA		8
#define	GSS_C_AF_DATAKIT	9
#define	GSS_C_AF_CCITT		10
#define	GSS_C_AF_SNA		11
#define	GSS_C_AF_DECnet		12
#define	GSS_C_AF_DLI		13
#define	GSS_C_AF_LAT		14
#define	GSS_C_AF_HYLINK		15
#define	GSS_C_AF_APPLETALK	16
#define	GSS_C_AF_BSC		17
#define	GSS_C_AF_DSS		18
#define	GSS_C_AF_OSI		19
#define	GSS_C_AF_X25		21

#define	GSS_C_AF_NULLADDR	255

/*
 * Various Null values
 */
#define	GSS_C_NO_NAME ((gss_name_t)0)
#define	GSS_C_NO_BUFFER ((gss_buffer_t)0)
#define	GSS_C_NO_OID ((gss_OID)0)
#define	GSS_C_NO_OID_SET ((gss_OID_set)0)
#define	GSS_C_NO_CONTEXT ((gss_ctx_id_t)0)
#define	GSS_C_NO_CREDENTIAL ((gss_cred_id_t)0)
#define	GSS_C_NO_CHANNEL_BINDINGS ((gss_channel_bindings_t)0)
#define	GSS_C_EMPTY_BUFFER {0, NULL}

/*
 * Some alternate names for a couple of the above
 * values.  These are defined for V1 compatibility.
 */
#define	GSS_C_NULL_OID		GSS_C_NO_OID
#define	GSS_C_NULL_OID_SET	GSS_C_NO_OID_SET

/*
 * Define the default Quality of Protection for per-message
 * services.  Note that an implementation that offers multiple
 * levels of QOP may define GSS_C_QOP_DEFAULT to be either zero
 * (as done here) to mean "default protection", or to a specific
 * explicit QOP value.  However, a value of 0 should always be
 * interpreted by a GSSAPI implementation as a request for the
 * default protection level.
 */
#define	GSS_C_QOP_DEFAULT 0

/*
 * Expiration time of 2^32-1 seconds means infinite lifetime for a
 * credential or security context
 */
#define	GSS_C_INDEFINITE ((OM_uint32) 0xfffffffful)

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {10, (void *)"\x2a\x86\x48\x86\xf7\x12"
 *  "\x01\x02\x01\x01"},
 * corresponding to an object-identifier value of
 * {iso(1) member-body(2) United States(840) mit(113554)
 *  infosys(1) gssapi(2) generic(1) user_name(1)}.  The constant
 * GSS_C_NT_USER_NAME should be initialized to point
 * to that gss_OID_desc.
 */
extern const gss_OID GSS_C_NT_USER_NAME;

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {10, (void *)"\x2a\x86\x48\x86\xf7\x12"
 *  "\x01\x02\x01\x02"},
 * corresponding to an object-identifier value of
 * {iso(1) member-body(2) United States(840) mit(113554)
 *  infosys(1) gssapi(2) generic(1) machine_uid_name(2)}.
 * The constant GSS_C_NT_MACHINE_UID_NAME should be
 * initialized to point to that gss_OID_desc.
 */
extern const gss_OID GSS_C_NT_MACHINE_UID_NAME;

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {10, (void *)"\x2a\x86\x48\x86\xf7\x12"
 *  "\x01\x02\x01\x03"},
 * corresponding to an object-identifier value of
 * {iso(1) member-body(2) United States(840) mit(113554)
 *  infosys(1) gssapi(2) generic(1) string_uid_name(3)}.
 * The constant GSS_C_NT_STRING_UID_NAME should be
 * initialized to point to that gss_OID_desc.
 */
extern const gss_OID GSS_C_NT_STRING_UID_NAME;

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {6, (void *)"\x2b\x06\x01\x05\x06\x02"},
 * corresponding to an object-identifier value of
 * {1(iso), 3(org), 6(dod), 1(internet), 5(security),
 * 6(nametypes), 2(gss-host-based-services)}.  The constant
 * GSS_C_NT_HOSTBASED_SERVICE should be initialized to point
 * to that gss_OID_desc.
 */
extern const gss_OID GSS_C_NT_HOSTBASED_SERVICE;

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {6, (void *)"\x2b\x06\01\x05\x06\x03"},
 * corresponding to an object identifier value of
 * {1(iso), 3(org), 6(dod), 1(internet), 5(security),
 * 6(nametypes), 3(gss-anonymous-name)}.  The constant
 * and GSS_C_NT_ANONYMOUS should be initialized to point
 * to that gss_OID_desc.
 */
extern const gss_OID GSS_C_NT_ANONYMOUS;

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {6, (void *)"\x2b\x06\x01\x05\x06\x04"},
 * corresponding to an object-identifier value of
 * {1(iso), 3(org), 6(dod), 1(internet), 5(security),
 * 6(nametypes), 4(gss-api-exported-name)}.  The constant
 * GSS_C_NT_EXPORT_NAME should be initialized to point
 * to that gss_OID_desc.
 */
extern const gss_OID GSS_C_NT_EXPORT_NAME;


/* Major status codes */

#define	GSS_S_COMPLETE 0

/*
 * Some "helper" definitions to make the status code macros obvious.
 */
#define	GSS_C_CALLING_ERROR_OFFSET 24
#define	GSS_C_ROUTINE_ERROR_OFFSET 16
#define	GSS_C_SUPPLEMENTARY_OFFSET 0
#define	GSS_C_CALLING_ERROR_MASK ((OM_uint32) 0377ul)
#define	GSS_C_ROUTINE_ERROR_MASK ((OM_uint32) 0377ul)
#define	GSS_C_SUPPLEMENTARY_MASK ((OM_uint32) 0177777ul)

/*
 * The macros that test status codes for error conditions.
 * Note that the GSS_ERROR() macro has changed slightly from
 * the V1 GSSAPI so that it now evaluates its argument
 * only once.
 */
#define	GSS_CALLING_ERROR(x) \
	((x) & (GSS_C_CALLING_ERROR_MASK << GSS_C_CALLING_ERROR_OFFSET))
#define	GSS_ROUTINE_ERROR(x) \
	((x) & (GSS_C_ROUTINE_ERROR_MASK << GSS_C_ROUTINE_ERROR_OFFSET))
#define	GSS_SUPPLEMENTARY_INFO(x) \
	((x) & (GSS_C_SUPPLEMENTARY_MASK << GSS_C_SUPPLEMENTARY_OFFSET))
#define	GSS_ERROR(x) \
	((x) & ((GSS_C_CALLING_ERROR_MASK << GSS_C_CALLING_ERROR_OFFSET) | \
	(GSS_C_ROUTINE_ERROR_MASK << GSS_C_ROUTINE_ERROR_OFFSET)))

/*
 * Now the actual status code definitions
 */

/*
 * Calling errors:
 */
#define	GSS_S_CALL_INACCESSIBLE_READ \
	(((OM_uint32) 1ul) << GSS_C_CALLING_ERROR_OFFSET)
#define	GSS_S_CALL_INACCESSIBLE_WRITE \
	(((OM_uint32) 2ul) << GSS_C_CALLING_ERROR_OFFSET)
#define	GSS_S_CALL_BAD_STRUCTURE \
	(((OM_uint32) 3ul) << GSS_C_CALLING_ERROR_OFFSET)

/*
 * Routine errors:
 */
#define	GSS_S_BAD_MECH (((OM_uint32) 1ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_BAD_NAME (((OM_uint32) 2ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_BAD_NAMETYPE (((OM_uint32) 3ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_BAD_BINDINGS (((OM_uint32) 4ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_BAD_STATUS (((OM_uint32) 5ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_BAD_SIG (((OM_uint32) 6ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_BAD_MIC GSS_S_BAD_SIG
#define	GSS_S_NO_CRED (((OM_uint32) 7ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_NO_CONTEXT (((OM_uint32) 8ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_DEFECTIVE_TOKEN (((OM_uint32) 9ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_DEFECTIVE_CREDENTIAL \
	(((OM_uint32) 10ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_CREDENTIALS_EXPIRED \
	(((OM_uint32) 11ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_CONTEXT_EXPIRED \
	(((OM_uint32) 12ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_FAILURE (((OM_uint32) 13ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_BAD_QOP (((OM_uint32) 14ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_UNAUTHORIZED (((OM_uint32) 15ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_UNAVAILABLE (((OM_uint32) 16ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_DUPLICATE_ELEMENT \
	(((OM_uint32) 17ul) << GSS_C_ROUTINE_ERROR_OFFSET)
#define	GSS_S_NAME_NOT_MN (((OM_uint32) 18ul) << GSS_C_ROUTINE_ERROR_OFFSET)

/*
 * Supplementary info bits:
 */
#define	GSS_S_CONTINUE_NEEDED (1 << (GSS_C_SUPPLEMENTARY_OFFSET + 0))
#define	GSS_S_DUPLICATE_TOKEN (1 << (GSS_C_SUPPLEMENTARY_OFFSET + 1))
#define	GSS_S_OLD_TOKEN (1 << (GSS_C_SUPPLEMENTARY_OFFSET + 2))
#define	GSS_S_UNSEQ_TOKEN (1 << (GSS_C_SUPPLEMENTARY_OFFSET + 3))
#define	GSS_S_GAP_TOKEN (1 << (GSS_C_SUPPLEMENTARY_OFFSET + 4))


/*
 * Finally, function prototypes for the GSS-API routines.
 */

OM_uint32 gss_acquire_cred(
	OM_uint32 *,		/* minor_status */
	const gss_name_t,	/* desired_name */
	OM_uint32,		/* time_req */
	const gss_OID_set,	/* desired_mechs */
	gss_cred_usage_t,	/* cred_usage */
	gss_cred_id_t *,	/* output_cred_handle */
	gss_OID_set *,		/* actual_mechs */
	/* CSTYLED */
	OM_uint32 *		/* time_rec */
);

OM_uint32 gss_release_cred(
	OM_uint32 *,		/* minor_status */
	/* CSTYLED */
	gss_cred_id_t *		/* cred_handle */
);

OM_uint32 gss_init_sec_context(
	OM_uint32 *,		/* minor_status */
	const gss_cred_id_t,	/* initiator_cred_handle */
	gss_ctx_id_t *,		/* context_handle */
	const gss_name_t,	/* target_name */
	const gss_OID,		/* mech_type */
	OM_uint32,		/* req_flags */
	OM_uint32,		/* time_req */
	gss_channel_bindings_t,	/* input_chan_bindings */
	const gss_buffer_t,	/* input_token */
	gss_OID *,		/* actual_mech_type */
	gss_buffer_t,		/* output_token */
	OM_uint32 *,		/* ret_flags */
	/* CSTYLED */
	OM_uint32 *		/* time_rec */
);

OM_uint32 gss_accept_sec_context(
	OM_uint32 *,		/* minor_status */
	gss_ctx_id_t *,		/* context_handle */
	const gss_cred_id_t,	/* acceptor_cred_handle */
	const gss_buffer_t,	/* input_token_buffer */
	const gss_channel_bindings_t,	/* input_chan_bindings */
	gss_name_t *,		/* src_name */
	gss_OID *,		/* mech_type */
	gss_buffer_t,		/* output_token */
	OM_uint32 *,		/* ret_flags */
	OM_uint32 *,		/* time_rec */
	/* CSTYLED */
	gss_cred_id_t *		/* delegated_cred_handle */
);

OM_uint32 gss_process_context_token(
	OM_uint32 *,		/* minor_status */
	const gss_ctx_id_t,	/* context_handle */
	const gss_buffer_t	/* token_buffer */
);

OM_uint32 gss_delete_sec_context(
	OM_uint32 *,		/* minor_status */
	gss_ctx_id_t *,		/* context_handle */
	gss_buffer_t		/* output_token */
);

OM_uint32 gss_context_time(
	OM_uint32 *,		/* minor_status */
	const gss_ctx_id_t,	/* context_handle */
	/* CSTYLED */
	OM_uint32 *		/* time_rec */
);

OM_uint32 gss_get_mic(
	OM_uint32 *,		/* minor_status */
	const gss_ctx_id_t,	/* context_handle */
	gss_qop_t,		/* qop_req */
	const gss_buffer_t,	/* message_buffer */
	gss_buffer_t		/* message_token */
);

OM_uint32 gss_verify_mic(
	OM_uint32 *,		/* minor_status */
	const gss_ctx_id_t,	/* context_handle */
	const gss_buffer_t,	/* message_buffer */
	const gss_buffer_t,	/* token_buffer */
	/* CSTYLED */
	gss_qop_t *		/* qop_state */
);

OM_uint32 gss_wrap(
	OM_uint32 *,		/* minor_status */
	const gss_ctx_id_t,	/* context_handle */
	int,			/* conf_req_flag */
	gss_qop_t,		/* qop_req */
	const gss_buffer_t,	/* input_message_buffer */
	int *,			/* conf_state */
	gss_buffer_t		/* output_message_buffer */
);

OM_uint32 gss_unwrap(
	OM_uint32 *,		/* minor_status */
	const gss_ctx_id_t,	/* context_handle */
	const gss_buffer_t,	/* input_message_buffer */
	gss_buffer_t,		/* output_message_buffer */
	int *,			/* conf_state */
	/* CSTYLED */
	gss_qop_t *		/* qop_state */
);

OM_uint32 gss_display_status(
	OM_uint32 *,		/* minor_status */
	OM_uint32,		/* status_value */
	int,			/* status_type */
	const gss_OID,		/* mech_type */
	OM_uint32 *,		/* message_context */
	gss_buffer_t		/* status_string */
);

OM_uint32 gss_indicate_mechs(
	OM_uint32 *,		/* minor_status */
	/* CSTYLED */
	gss_OID_set *		/* mech_set */
);

OM_uint32 gss_compare_name(
	OM_uint32 *,		/* minor_status */
	const gss_name_t,	/* name1 */
	const gss_name_t,	/* name2 */
	/* CSTYLED */
	int *			/* name_equal */
);

OM_uint32 gss_display_name(
	OM_uint32 *,		/* minor_status */
	const gss_name_t,	/* input_name */
	gss_buffer_t,		/* output_name_buffer */
	/* CSTYLED */
	gss_OID *		/* output_name_type */
);

OM_uint32 gss_import_name(
	OM_uint32 *,		/* minor_status */
	const gss_buffer_t,	/* input_name_buffer */
	const gss_OID,		/* input_name_type */
	/* CSTYLED */
	gss_name_t *		/* output_name */
);

OM_uint32 gss_export_name(
	OM_uint32 *,		/* minor_status */
	const gss_name_t,  	/* input_name */
	gss_buffer_t 		/* exported_name */
);

OM_uint32 gss_release_name(
	OM_uint32 *,		/* minor_status */
	/* CSTYLED */
	gss_name_t *		/* input_name */
);

OM_uint32 gss_release_buffer(
	OM_uint32 *,		/* minor_status */
	gss_buffer_t		/* buffer */
);

OM_uint32 gss_release_oid_set(
	OM_uint32 *,		/* minor_status */
	/* CSTYLED */
	gss_OID_set *		/* set */
);

OM_uint32 gss_inquire_cred(
	OM_uint32 *,		/* minor_status */
	const gss_cred_id_t,	/* cred_handle */
	gss_name_t *,		/* name */
	OM_uint32 *,		/* lifetime */
	gss_cred_usage_t *,	/* cred_usage */
	/* CSTYLED */
	gss_OID_set *		/* mechanisms */
);

OM_uint32 gss_inquire_context(
	OM_uint32 *,		/* minor_status */
	const gss_ctx_id_t,	/* context_handle */
	gss_name_t *,		/* src_name */
	gss_name_t *,		/* targ_name */
	OM_uint32 *,		/* lifetime_rec */
	gss_OID *,		/* mech_type */
	OM_uint32 *,		/* ctx_flags */
	int *,			/* locally_initiated */
	/* CSTYLED */
	int *			/* open */
);

OM_uint32 gss_wrap_size_limit(
	OM_uint32 *,		/* minor_status */
	const gss_ctx_id_t,	/* context_handle */
	int,			/* conf_req_flag */
	gss_qop_t,		/* qop_req */
	OM_uint32,		/* req_output_size */
	/* CSTYLED */
	OM_uint32 *		/* max_input_size */
);

OM_uint32 gss_add_cred(
	OM_uint32 *,		/* minor_status */
	const gss_cred_id_t,	/* input_cred_handle */
	const gss_name_t,	/* desired_name */
	const gss_OID,		/* desired_mech */
	gss_cred_usage_t,	/* cred_usage */
	OM_uint32,		/* initiator_time_req */
	OM_uint32,		/* acceptor_time_req */
	gss_cred_id_t *,	/* output_cred_handle */
	gss_OID_set *,		/* actual_mechs */
	OM_uint32 *,		/* initiator_time_rec */
	/* CSTYLED */
	OM_uint32 *		/* acceptor_time_rec */
);

OM_uint32 gss_store_cred(
	OM_uint32 *,		/* minor_status */
	const gss_cred_id_t,	/* input_cred */
	gss_cred_usage_t,	/* cred_usage */
	const gss_OID,		/* desired_mech */
	OM_uint32,		/* overwrite_cred */
	OM_uint32,		/* default_cred */
	gss_OID_set *,		/* elements_stored */
	/* CSTYLED */
	gss_cred_usage_t *	/* cred_usage_stored */
);

OM_uint32 gss_inquire_cred_by_mech(
	OM_uint32  *,		/* minor_status */
	const gss_cred_id_t,	/* cred_handle */
	const gss_OID,		/* mech_type */
	gss_name_t *,		/* name */
	OM_uint32 *,		/* initiator_lifetime */
	OM_uint32 *,		/* acceptor_lifetime */
	/* CSTYLED */
	gss_cred_usage_t *	/* cred_usage */
);

OM_uint32 gss_export_sec_context(
	OM_uint32 *,		/* minor_status */
	gss_ctx_id_t *,		/* context_handle */
	gss_buffer_t		/* interprocess_token */
);

OM_uint32 gss_import_sec_context(
	OM_uint32 *,		/* minor_status */
	const gss_buffer_t,	/* interprocess_token */
	/* CSTYLED */
	gss_ctx_id_t *		/* context_handle */
);

OM_uint32 gss_create_empty_oid_set(
	OM_uint32 *, 		/* minor_status */
	/* CSTYLED */
	gss_OID_set *		/* oid_set */
);

OM_uint32 gss_add_oid_set_member(
	OM_uint32 *, 		/* minor_status */
	const gss_OID,  	/* member_oid */
	/* CSTYLED */
	gss_OID_set *		/* oid_set */
);

OM_uint32 gss_test_oid_set_member(
	OM_uint32 *, 		/* minor_status */
	const gss_OID,  	/* member */
	const gss_OID_set, 	/* set */
	/* CSTYLED */
	int * 			/* present */
);

OM_uint32 gss_inquire_names_for_mech(
	OM_uint32 *, 		/* minor_status */
	const gss_OID,  	/* mechanism */
	gss_OID_set *		/* name_types */
);

OM_uint32 gss_inquire_mechs_for_name(
	OM_uint32 *, 		/* minor_status */
	const gss_name_t,  	/* input_name */
	gss_OID_set *		/* mech_types */
);

OM_uint32 gss_canonicalize_name(
	OM_uint32 *, 		/* minor_status */
	const gss_name_t,  	/* input_name */
	const gss_OID,  	/* mech_type */
	/* CSTYLED */
	gss_name_t * 		/* output_name */
);

OM_uint32 gss_duplicate_name(
	OM_uint32 *, 		/* minor_status */
	const gss_name_t,  	/* src_name */
	/* CSTYLED */
	gss_name_t * 		/* dest_name */
);


OM_uint32 gss_release_oid(
	OM_uint32 *,		/* minor_status */
	/* CSTYLED */
	gss_OID *		/* oid */
);

OM_uint32 gss_str_to_oid(
	OM_uint32 *,		/* minor_status */
	const gss_buffer_t,	/* oid_str */
	/* CSTYLED */
	gss_OID *		/* oid */
);

OM_uint32 gss_oid_to_str(
	OM_uint32 *,		/* minor_status */
	const gss_OID,		/* oid */
	gss_buffer_t		/* oid_str */
);


/*
 * The following routines are obsolete variants of gss_get_mic,
 * gss_verify_mic, gss_wrap and gss_unwrap.  They should be
 * provided by GSSAPI V2 implementations for backwards
 * compatibility with V1 applications.  Distinct entrypoints
 * (as opposed to #defines) should be provided, both to allow
 * GSSAPI V1 applications to link against GSSAPI V2 implementations,
 * and to retain the slight parameter type differences between the
 * obsolete versions of these routines and their current forms.
 */

OM_uint32 gss_sign(
	OM_uint32 *,		/* minor_status */
	gss_ctx_id_t,		/* context_handle */
	int,			/* qop_req */
	gss_buffer_t,		/* message_buffer */
	gss_buffer_t		/* message_token */
);

OM_uint32 gss_verify(
	OM_uint32 *,		/* minor_status */
	gss_ctx_id_t,		/* context_handle */
	gss_buffer_t,		/* message_buffer */
	gss_buffer_t,		/* token_buffer */
	/* CSTYLED */
	int *			/* qop_state */
);

OM_uint32 gss_seal(
	OM_uint32 *,		/* minor_status */
	gss_ctx_id_t,		/* context_handle */
	int,			/* conf_req_flag */
	int,			/* qop_req */
	gss_buffer_t,		/* input_message_buffer */
	int *,			/* conf_state */
	gss_buffer_t		/* output_message_buffer */
);

OM_uint32 gss_unseal(
	OM_uint32 *,		/* minor_status */
	gss_ctx_id_t,		/* context_handle */
	gss_buffer_t,		/* input_message_buffer */
	gss_buffer_t,		/* output_message_buffer */
	int *,			/* conf_state */
	/* CSTYLED */
	int *			/* qop_state */
);


#ifdef _KERNEL /* For kernel */

#include <rpc/types.h>

void kgss_free_oid(gss_OID oid);

OM_uint32 kgss_acquire_cred(
	OM_uint32 *,
	const gss_name_t,
	OM_uint32,
	const gss_OID_set,
	int,
	gss_cred_id_t *,
	gss_OID_set *,
	OM_uint32 *,
	uid_t);

OM_uint32 kgss_add_cred(
	OM_uint32 *,
	gss_cred_id_t,
	gss_name_t,
	gss_OID,
	int,
	int,
	int,
	gss_OID_set *,
	OM_uint32 *,
	OM_uint32 *,
	uid_t);

OM_uint32 kgss_release_cred(
	OM_uint32 *,
	gss_cred_id_t *,
	uid_t);

OM_uint32 kgss_init_sec_context(
	OM_uint32 *,
	const gss_cred_id_t,
	gss_ctx_id_t *,
	const gss_name_t,
	const gss_OID,
	int,
	OM_uint32,
	const gss_channel_bindings_t,
	const gss_buffer_t,
	gss_OID *,
	gss_buffer_t,
	int *,
	OM_uint32 *,
	uid_t);

OM_uint32 kgss_accept_sec_context(
	OM_uint32 *,
	gss_ctx_id_t *,
	const gss_cred_id_t,
	const gss_buffer_t,
	const gss_channel_bindings_t,
	const gss_buffer_t,
	gss_OID *,
	gss_buffer_t,
	int *,
	OM_uint32 *,
	gss_cred_id_t *,
	uid_t);

OM_uint32 kgss_process_context_token(
	OM_uint32 *,
	const gss_ctx_id_t,
	const gss_buffer_t,
	uid_t);

OM_uint32 kgss_delete_sec_context(
	OM_uint32 *,
	gss_ctx_id_t *,
	gss_buffer_t);

OM_uint32 kgss_export_sec_context(
	OM_uint32 *,
	const gss_ctx_id_t,
	gss_buffer_t);

OM_uint32 kgss_import_sec_context(
	OM_uint32  *,
	const gss_buffer_t,
	gss_ctx_id_t);

OM_uint32 kgss_context_time(
	OM_uint32 *,
	const gss_ctx_id_t,
	OM_uint32 *,
	uid_t);

OM_uint32 kgss_sign(
	OM_uint32 *,
	const gss_ctx_id_t,
	int,
	const gss_buffer_t,
	gss_buffer_t);


OM_uint32 kgss_verify(
	OM_uint32 *,
	const gss_ctx_id_t,
	const gss_buffer_t,
	const gss_buffer_t,
	int *);

OM_uint32 kgss_seal(
	OM_uint32 *,
	const gss_ctx_id_t,
	int,
	int,
	const gss_buffer_t,
	int *,
	gss_buffer_t);

OM_uint32 kgss_unseal(
	OM_uint32 *,
	const gss_ctx_id_t,
	const gss_buffer_t,
	gss_buffer_t,
	int *,
	int *);

OM_uint32 kgss_display_status(
	OM_uint32 *,
	OM_uint32,
	int,
	const gss_OID,
	int *,
	gss_buffer_t,
	uid_t);

OM_uint32 kgss_indicate_mechs(
	OM_uint32 *,
	gss_OID_set *,
	uid_t);

OM_uint32 kgss_inquire_cred(
	OM_uint32 *,
	const gss_cred_id_t,
	gss_name_t *,
	OM_uint32 *,
	int *,
	gss_OID_set *,
	uid_t);

OM_uint32 kgss_inquire_cred_by_mech(
	OM_uint32 *,
	gss_cred_id_t,
	gss_OID,
	uid_t);


#endif /* if _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _GSSAPI_H_ */
