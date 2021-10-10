/* #ident  "@(#)mglueP.h 1.2     96/01/18 SMI" */

/*
 * This header contains the private mechglue definitions.
 */

/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _GSS_MECHGLUEP_H
#define _GSS_MECHGLUEP_H

/* Solaris Kerberos: disable for sake of non-krb5 mechs */
/* #include "autoconf.h" */ 

/* Solaris Kerberos: */
#ifndef GSS_DLLIMP
#define GSS_DLLIMP
#endif

/*
 * Solaris Kerberos: since this header is included by non-krb mechs it's best to
 * not use gssapiP_generic.h and just include gssapi_ext.h.
 */
/* #include "gssapiP_generic.h" */
#include <gssapi/gssapi_ext.h>

#ifdef _KERNEL
#include <rpc/rpc.h>
#endif

/* Solaris Kerberos */
#ifndef g_OID_copy 

#define	g_OID_copy(o1, o2)					\
do {								\
	memcpy((o1)->elements, (o2)->elements, (o2)->length);	\
	(o1)->length = (o2)->length;				\
} while (0)

#endif /* g_OID_copy */

/*
 * Array of context IDs typed by mechanism OID
 */
typedef struct gss_ctx_id_struct {
	struct gss_ctx_id_struct *loopback;
	gss_OID			mech_type;
	gss_ctx_id_t		internal_ctx_id;
} gss_union_ctx_id_desc, *gss_union_ctx_id_t;

/*
 * Generic GSSAPI names.  A name can either be a generic name, or a
 * mechanism specific name....
 */
typedef struct gss_name_struct {
	struct gss_name_struct *loopback;
	gss_OID			name_type;
	gss_buffer_t		external_name;
	/*
	 * These last two fields are only filled in for mechanism
	 * names.
	 */
	gss_OID			mech_type;
	gss_name_t		mech_name;
} gss_union_name_desc, *gss_union_name_t;

/*
 * Structure for holding list of mechanism-specific name types
 */
typedef struct gss_mech_spec_name_t {
    gss_OID	name_type;
    gss_OID	mech;
    struct gss_mech_spec_name_t	*next, *prev;
} gss_mech_spec_name_desc, *gss_mech_spec_name;

/*
 * Credential auxiliary info, used in the credential structure
 */
typedef struct gss_union_cred_auxinfo {
	gss_buffer_desc		name;
	gss_OID			name_type;
	OM_uint32		creation_time;
	OM_uint32		time_rec;
	int			cred_usage;
} gss_union_cred_auxinfo;

/*
 * Set of Credentials typed on mechanism OID
 */
typedef struct gss_cred_id_struct {
	struct gss_cred_id_struct *loopback;
	int			count;
	gss_OID			mechs_array;
	gss_cred_id_t		*cred_array;
	gss_union_cred_auxinfo	auxinfo;
} gss_union_cred_desc, *gss_union_cred_t;

typedef	OM_uint32	    (*gss_acquire_cred_with_password_sfct)(
		    OM_uint32 *,	/* minor_status */
		    const gss_name_t,	/* desired_name */
		    const gss_buffer_t, /* password */
		    OM_uint32,		/* time_req */
		    const gss_OID_set,	/* desired_mechs */
		    int,		/* cred_usage */
		    gss_cred_id_t *,	/* output_cred_handle */
		    gss_OID_set *,	/* actual_mechs */
		    OM_uint32 *		/* time_rec */
	/* */);

/*
 * Rudimentary pointer validation macro to check whether the
 * "loopback" field of an opaque struct points back to itself.  This
 * field also catches some programming errors where an opaque pointer
 * is passed to a function expecting the address of the opaque
 * pointer.
 */
#define GSSINT_CHK_LOOP(p) (!((p) != NULL && (p)->loopback == (p)))

/********************************************************/
/* The Mechanism Dispatch Table -- a mechanism needs to */
/* define one of these and provide a function to return */
/* it to initialize the GSSAPI library		  */
int gssint_mechglue_initialize_library(void);

OM_uint32 gssint_get_mech_type_oid(gss_OID OID, gss_buffer_t token);

/*
 * This is the definition of the mechs_array struct, which is used to
 * define the mechs array table. This table is used to indirectly
 * access mechanism specific versions of the gssapi routines through
 * the routines in the glue module (gssd_mech_glue.c)
 *
 * This contants all of the functions defined in gssapi.h except for
 * gss_release_buffer() and gss_release_oid_set(), which I am
 * assuming, for now, to be equal across mechanisms.
 */

typedef struct gss_config {
    gss_OID_desc    mech_type;
    void *	    context;
#ifdef	_KERNEL
    struct gss_config *next;
    bool_t	    uses_kmod;
#endif
#ifndef	_KERNEL
    OM_uint32       (*gss_acquire_cred)
	(
		    OM_uint32*,		/* minor_status */
		    gss_name_t,		/* desired_name */
		    OM_uint32,		/* time_req */
		    gss_OID_set,	/* desired_mechs */
		    int,		/* cred_usage */
		    gss_cred_id_t*,	/* output_cred_handle */
		    gss_OID_set*,	/* actual_mechs */
		    OM_uint32*		/* time_rec */
		    );
    OM_uint32       (*gss_release_cred)
	(
		    OM_uint32*,		/* minor_status */
		    gss_cred_id_t*	/* cred_handle */
		    );
    OM_uint32       (*gss_init_sec_context)
	(
		    OM_uint32*,			/* minor_status */
		    gss_cred_id_t,		/* claimant_cred_handle */
		    gss_ctx_id_t*,		/* context_handle */
		    gss_name_t,			/* target_name */
		    gss_OID,			/* mech_type */
		    OM_uint32,			/* req_flags */
		    OM_uint32,			/* time_req */
		    gss_channel_bindings_t,	/* input_chan_bindings */
		    gss_buffer_t,		/* input_token */
		    gss_OID*,			/* actual_mech_type */
		    gss_buffer_t,		/* output_token */
		    OM_uint32*,			/* ret_flags */
		    OM_uint32*			/* time_rec */
		    );
    OM_uint32       (*gss_accept_sec_context)
	(
		    OM_uint32*,			/* minor_status */
		    gss_ctx_id_t*,		/* context_handle */
		    gss_cred_id_t,		/* verifier_cred_handle */
		    gss_buffer_t,		/* input_token_buffer */
		    gss_channel_bindings_t,	/* input_chan_bindings */
		    gss_name_t*,		/* src_name */
		    gss_OID*,			/* mech_type */
		    gss_buffer_t,		/* output_token */
		    OM_uint32*,			/* ret_flags */
		    OM_uint32*,			/* time_rec */
		    gss_cred_id_t*		/* delegated_cred_handle */
		    );
    OM_uint32       (*gss_process_context_token)
	(
		    OM_uint32*,		/* minor_status */
		    gss_ctx_id_t,	/* context_handle */
		    gss_buffer_t	/* token_buffer */
		    );
#endif	/* ifndef _KERNEL */
#ifdef _KERNEL
    OM_uint32       (*gss_delete_sec_context)
	(
		    OM_uint32*,		/* minor_status */
		    gss_ctx_id_t*,	/* context_handle */
		    gss_buffer_t,	/* output_token */
		    OM_uint32 		/* context verifier */
		    );
#else
    OM_uint32       (*gss_delete_sec_context)
	(
		    OM_uint32*,		/* minor_status */
		    gss_ctx_id_t*,	/* context_handle */
		    gss_buffer_t	/* output_token */
		    );
#endif
#ifndef _KERNEL
    OM_uint32       (*gss_context_time)
	(
		    OM_uint32*,		/* minor_status */
		    gss_ctx_id_t,	/* context_handle */
		    OM_uint32*		/* time_rec */
		    );
    OM_uint32       (*gss_get_mic)
	(
		    OM_uint32*,		/* minor_status */
		    gss_ctx_id_t,	/* context_handle */
		    gss_qop_t,		/* qop_req */
		    gss_buffer_t,	/* message_buffer */
		    gss_buffer_t	/* message_token */
		    );
    OM_uint32       (*gss_verify_mic)
	(
		    OM_uint32*,		/* minor_status */
		    gss_ctx_id_t,	/* context_handle */
		    gss_buffer_t,	/* message_buffer */
		    gss_buffer_t,	/* token_buffer */
		    gss_qop_t*		/* qop_state */
		    );
#endif
/* EXPORT DELETE START */ /* CRYPT DELETE START */
#ifdef _KERNEL
    /* Solaris Kerberos: we still use gss_seal instead of gss_wrap here */
    OM_uint32	    (*gss_seal)
	(
		    OM_uint32 *,	/* minor_status */
		    const gss_ctx_id_t,	/* context_handle */
		    int,		/* conf_req_flag */
		    int,		/* qop_req */
		    const gss_buffer_t,	/* input_message_buffer */
		    int *,		/* conf_state */
		    gss_buffer_t,	/* output_message_buffer */
		    OM_uint32	        /* context verifier */
		    );
#else
    OM_uint32	    (*gss_seal)
	(
		    OM_uint32 *,	/* minor_status */
		    const gss_ctx_id_t,	/* context_handle */
		    int,		/* conf_req_flag */
		    int,		/* qop_req */
		    const gss_buffer_t,	/* input_message_buffer */
		    int *,		/* conf_state */
		    gss_buffer_t 	/* output_message_buffer */
		    );
#endif
#ifdef _KERNEL
    /* Solaris Kerberos: we still use gss_unseal instead of gss_unwrap here */
    OM_uint32	    (*gss_unseal)
	(
		    OM_uint32 *,	/* minor_status */
		    const gss_ctx_id_t,	/* context_handle */
		    const gss_buffer_t,	/* input_message_buffer */
		    gss_buffer_t,	/* output_message_buffer */
		    int *,		/* conf_state */
		    int *,		/* qop_state */
		    OM_uint32	        /* context verifier */
		    );
#else
    OM_uint32	    (*gss_unseal)
	(
		    OM_uint32 *,	/* minor_status */
		    const gss_ctx_id_t,	/* context_handle */
		    const gss_buffer_t,	/* input_message_buffer */
		    gss_buffer_t,	/* output_message_buffer */
		    int *,		/* conf_state */
		    int *		/* qop_state */
		    );
#endif
/* EXPORT DELETE END */ /* CRYPT DELETE END */
#ifndef	_KERNEL
    OM_uint32       (*gss_display_status)
	(
		    OM_uint32*,		/* minor_status */
		    OM_uint32,		/* status_value */
		    int,		/* status_type */
		    gss_OID,		/* mech_type */
		    OM_uint32*,		/* message_context */
		    gss_buffer_t	/* status_string */
		    );
    OM_uint32       (*gss_indicate_mechs)
	(
		    OM_uint32*,		/* minor_status */
		    gss_OID_set*	/* mech_set */
		    );
    OM_uint32       (*gss_compare_name)
	(
		    OM_uint32*,		/* minor_status */
		    gss_name_t,		/* name1 */
		    gss_name_t,		/* name2 */
		    int*		/* name_equal */
		    );
    OM_uint32       (*gss_display_name)
	(
		    OM_uint32*,		/* minor_status */
		    gss_name_t,		/* input_name */
		    gss_buffer_t,	/* output_name_buffer */
		    gss_OID*		/* output_name_type */
		    );
    OM_uint32       (*gss_import_name)
	(
		    OM_uint32*,		/* minor_status */
		    gss_buffer_t,	/* input_name_buffer */
		    gss_OID,		/* input_name_type */
		    gss_name_t*		/* output_name */
		    );
    OM_uint32       (*gss_release_name)
	(
		    OM_uint32*,		/* minor_status */
		    gss_name_t*		/* input_name */
		    );
    OM_uint32       (*gss_inquire_cred)
	(
		    OM_uint32 *,		/* minor_status */
		    gss_cred_id_t,		/* cred_handle */
		    gss_name_t *,		/* name */
		    OM_uint32 *,		/* lifetime */
		    int *,			/* cred_usage */
		    gss_OID_set *		/* mechanisms */
		    );
    OM_uint32	    (*gss_add_cred)
	(
		    OM_uint32 *,	/* minor_status */
		    gss_cred_id_t,	/* input_cred_handle */
		    gss_name_t,		/* desired_name */
		    gss_OID,		/* desired_mech */
		    gss_cred_usage_t,	/* cred_usage */
		    OM_uint32,		/* initiator_time_req */
		    OM_uint32,		/* acceptor_time_req */
		    gss_cred_id_t *,	/* output_cred_handle */
		    gss_OID_set *,	/* actual_mechs */
		    OM_uint32 *,	/* initiator_time_rec */
		    OM_uint32 *		/* acceptor_time_rec */
		    );
    OM_uint32	    (*gss_export_sec_context)
	(
		    OM_uint32 *,	/* minor_status */
		    gss_ctx_id_t *,	/* context_handle */
		    gss_buffer_t	/* interprocess_token */
		    );
#endif /* ifndef _KERNEL */
    OM_uint32	    (*gss_import_sec_context)
	(
		    OM_uint32 *,	/* minor_status */
		    gss_buffer_t,	/* interprocess_token */
		    gss_ctx_id_t *	/* context_handle */
		    );
#ifndef _KERNEL
    OM_uint32 	    (*gss_inquire_cred_by_mech)
	(
		    OM_uint32 *,	/* minor_status */
		    gss_cred_id_t,	/* cred_handle */
		    gss_OID,		/* mech_type */
		    gss_name_t *,	/* name */
		    OM_uint32 *,	/* initiator_lifetime */
		    OM_uint32 *,	/* acceptor_lifetime */
		    gss_cred_usage_t *	/* cred_usage */
		    );
    OM_uint32	    (*gss_inquire_names_for_mech)
	(
		    OM_uint32 *,	/* minor_status */
		    gss_OID,		/* mechanism */
		    gss_OID_set *	/* name_types */
		    );
    OM_uint32	(*gss_inquire_context)
	(
		    OM_uint32 *,	/* minor_status */
		    gss_ctx_id_t,	/* context_handle */
		    gss_name_t *,	/* src_name */
		    gss_name_t *,	/* targ_name */
		    OM_uint32 *,	/* lifetime_rec */
		    gss_OID *,		/* mech_type */
		    OM_uint32 *,	/* ctx_flags */
		    int *,	   	/* locally_initiated */
		    int *		/* open */
		    );
    OM_uint32	    (*gss_internal_release_oid)
	(
		    OM_uint32 *,	/* minor_status */
		    gss_OID *		/* OID */
	 );
    OM_uint32	     (*gss_wrap_size_limit)
	(
		    OM_uint32 *,	/* minor_status */
		    gss_ctx_id_t,	/* context_handle */
		    int,		/* conf_req_flag */
		    gss_qop_t,		/* qop_req */
		    OM_uint32,		/* req_output_size */
		    OM_uint32 *		/* max_input_size */
	 );

/*
 * Solaris Kerberos: we're using these functions some of which have different
 * function signatures
 */
    OM_uint32	(*pname_to_uid)
	(
		    OM_uint32 *,	/* minor_status */
		    const gss_name_t,	/* pname */
		    uid_t *		/* uid */
		    );
    OM_uint32	(*gssint_userok)
	(
		    OM_uint32 *,	/* minor_status */
		    const gss_name_t,	/* pname */
		    const char *,	/* local user */
		    int *		/* user ok? */
	/* */);

    OM_uint32	(*gss_export_name)
	(
		OM_uint32 *,		/* minor_status */
		const gss_name_t,	/* input_name */
		gss_buffer_t		/* exported_name */
	/* */);
#endif /* ifndef _KERNEL */

/* Solaris Kerberos: more pain */
/* EXPORT DELETE START */
/* CRYPT DELETE START */
/*
 * This block comment is Sun Proprietary: Need-To-Know.
 * What we are doing is leaving the seal and unseal entry points
 * in an obvious place before sign and unsign for the Domestic customer
 * of the Solaris Source Product. The Domestic customer of the Solaris Source
 * Product will have to deal with the problem of creating exportable libgss
 * binaries.
 * In the binary product that Sun builds, these entry points are elsewhere,
 * and bracketed with special comments so that the CRYPT_SRC and EXPORT_SRC
 * targets delete them.
 */
#if 0
/* CRYPT DELETE END */
	OM_uint32	    (*gss_seal)
	(
		    OM_uint32 *,	/* minor_status */
		    const gss_ctx_id_t,	/* context_handle */
		    int,		/* conf_req_flag */
		    int,		/* qop_req */
		    const gss_buffer_t,	/* input_message_buffer */
		    int *,		/* conf_state */
		    gss_buffer_t	/* output_message_buffer */
#ifdef	 _KERNEL
	/* */, OM_uint32
#endif
	/* */);
	OM_uint32	    (*gss_unseal)
	(
		    OM_uint32 *,	/* minor_status */
		    const gss_ctx_id_t,	/* context_handle */
		    const gss_buffer_t,	/* input_message_buffer */
		    gss_buffer_t,	/* output_message_buffer */
		    int *,		/* conf_state */
		    int *		/* qop_state */
#ifdef	 _KERNEL
	/* */, OM_uint32
#endif
	/* */);
/* CRYPT DELETE START */
#endif /* 0 */
/* CRYPT DELETE END */
/* EXPORT DELETE END */
	OM_uint32	(*gss_sign)
	(
		    OM_uint32 *,	/* minor_status */
		    const gss_ctx_id_t,	/* context_handle */
		    int,		/* qop_req */
		    const gss_buffer_t,	/* message_buffer */
		    gss_buffer_t	/* message_token */
#ifdef	 _KERNEL
	/* */, OM_uint32
#endif
	/* */);
	OM_uint32	(*gss_verify)
	(
		OM_uint32 *,		/* minor_status */
		const gss_ctx_id_t,	/* context_handle */
		const gss_buffer_t,	/* message_buffer */
		const gss_buffer_t,	/* token_buffer */
		int *			/* qop_state */
#ifdef	 _KERNEL
	/* */, OM_uint32
#endif
	/* */);

#ifndef	 _KERNEL
	OM_uint32	(*gss_store_cred)
	(
		OM_uint32 *,		/* minor_status */
		const gss_cred_id_t,	/* input_cred */
		gss_cred_usage_t,	/* cred_usage */
		const gss_OID,		/* desired_mech */
		OM_uint32,		/* overwrite_cred */
		OM_uint32,		/* default_cred */
		gss_OID_set *,		/* elements_stored */
		gss_cred_usage_t *	/* cred_usage_stored */
	/* */);


	/* GGF extensions */

	OM_uint32       (*gss_inquire_sec_context_by_oid)
    	(
    		    OM_uint32 *,	/* minor_status */
    		    const gss_ctx_id_t, /* context_handle */
    		    const gss_OID,      /* OID */
    		    gss_buffer_set_t *  /* data_set */
    		    );
	OM_uint32       (*gss_inquire_cred_by_oid)
    	(
    		    OM_uint32 *,	/* minor_status */
    		    const gss_cred_id_t, /* cred_handle */
    		    const gss_OID,      /* OID */
    		    gss_buffer_set_t *  /* data_set */
    		    );
	OM_uint32       (*gss_set_sec_context_option)
    	(
    		    OM_uint32 *,	/* minor_status */
    		    gss_ctx_id_t *,     /* context_handle */
    		    const gss_OID,      /* OID */
    		    const gss_buffer_t  /* value */
    		    );
	OM_uint32       (*gssspi_set_cred_option)
    	(
    		    OM_uint32 *,	/* minor_status */
    		    gss_cred_id_t,      /* cred_handle */
    		    const gss_OID,      /* OID */
    		    const gss_buffer_t	/* value */
    		    );
	OM_uint32       (*gssspi_mech_invoke)
    	(
    		    OM_uint32*,		/* minor_status */
    		    const gss_OID, 	/* mech OID */
    		    const gss_OID,      /* OID */
    		    gss_buffer_t 	/* value */
    		    );

/* EXPORT DELETE START */ /* CRYPT DELETE START */
	/* AEAD extensions */
	OM_uint32	(*gss_wrap_aead)
	(
	    OM_uint32 *,		/* minor_status */
	    gss_ctx_id_t,		/* context_handle */
	    int,			/* conf_req_flag */
	    gss_qop_t,			/* qop_req */
	    gss_buffer_t,		/* input_assoc_buffer */
	    gss_buffer_t,		/* input_payload_buffer */
	    int *,			/* conf_state */
	    gss_buffer_t		/* output_message_buffer */
	/* */);

	OM_uint32	(*gss_unwrap_aead)
	(
	    OM_uint32 *,		/* minor_status */
	    gss_ctx_id_t,		/* context_handle */
	    gss_buffer_t,		/* input_message_buffer */
	    gss_buffer_t,		/* input_assoc_buffer */
	    gss_buffer_t,		/* output_payload_buffer */
	    int *,			/* conf_state */
	    gss_qop_t *			/* qop_state */
	/* */);

	/* SSPI extensions */
	OM_uint32	(*gss_wrap_iov)
	(
	    OM_uint32 *,		/* minor_status */
	    gss_ctx_id_t,		/* context_handle */
	    int,			/* conf_req_flag */
	    gss_qop_t,			/* qop_req */
	    int *,			/* conf_state */
	    gss_iov_buffer_desc *,	/* iov */
	    int				/* iov_count */
	/* */);

	OM_uint32	(*gss_unwrap_iov)
	(
	    OM_uint32 *,		/* minor_status */
	    gss_ctx_id_t,		/* context_handle */
	    int *,			/* conf_state */
	    gss_qop_t *,		/* qop_state */
	    gss_iov_buffer_desc *,	/* iov */
	    int				/* iov_count */
	/* */);

	OM_uint32	(*gss_wrap_iov_length)
	(
	    OM_uint32 *,		/* minor_status */
	    gss_ctx_id_t,		/* context_handle */
	    int,			/* conf_req_flag*/
	    gss_qop_t, 			/* qop_req */
	    int *, 			/* conf_state */
	    gss_iov_buffer_desc *,	/* iov */
	    int				/* iov_count */
	/* */);
/* EXPORT DELETE END */ /* CRYPT DELETE END */

	OM_uint32       (*gss_complete_auth_token)
    	(
    		    OM_uint32*,		/* minor_status */
    		    const gss_ctx_id_t,	/* context_handle */
    		    gss_buffer_t	/* input_message_buffer */
    		    );

	/* New for 1.8 */

	OM_uint32	(*gss_acquire_cred_impersonate_name)
	(
	    OM_uint32 *,		/* minor_status */
	    const gss_cred_id_t,	/* impersonator_cred_handle */
	    const gss_name_t,		/* desired_name */
	    OM_uint32,			/* time_req */
	    const gss_OID_set,		/* desired_mechs */
	    gss_cred_usage_t,		/* cred_usage */
	    gss_cred_id_t *,		/* output_cred_handle */
	    gss_OID_set *,		/* actual_mechs */
	    OM_uint32 *			/* time_rec */
	/* */);

	OM_uint32	(*gss_add_cred_impersonate_name)
	(
	    OM_uint32 *,		/* minor_status */
	    gss_cred_id_t,		/* input_cred_handle */
	    const gss_cred_id_t,	/* impersonator_cred_handle */
	    const gss_name_t,		/* desired_name */
	    const gss_OID,		/* desired_mech */
	    gss_cred_usage_t,		/* cred_usage */
	    OM_uint32,			/* initiator_time_req */
	    OM_uint32,			/* acceptor_time_req */
	    gss_cred_id_t *,		/* output_cred_handle */
	    gss_OID_set *,		/* actual_mechs */
	    OM_uint32 *,		/* initiator_time_rec */
	    OM_uint32 *			/* acceptor_time_rec */
	/* */);

	OM_uint32	(*gss_display_name_ext)
	(
	    OM_uint32 *,		/* minor_status */
	    gss_name_t,			/* name */
	    gss_OID,			/* display_as_name_type */
	    gss_buffer_t		/* display_name */
	/* */);

	OM_uint32	(*gss_inquire_name)
	(
	    OM_uint32 *,		/* minor_status */
	    gss_name_t,			/* name */
	    int *,			/* name_is_MN */
	    gss_OID *,			/* MN_mech */
	    gss_buffer_set_t *		/* attrs */
	/* */);

	OM_uint32	(*gss_get_name_attribute)
	(
	    OM_uint32 *,		/* minor_status */
	    gss_name_t,			/* name */
	    gss_buffer_t,		/* attr */
	    int *,			/* authenticated */
	    int *,			/* complete */
	    gss_buffer_t,		/* value */
	    gss_buffer_t,		/* display_value */
	    int *			/* more */
	/* */);

	OM_uint32	(*gss_set_name_attribute)
	(
	    OM_uint32 *,		/* minor_status */
	    gss_name_t,			/* name */
	    int,			/* complete */
	    gss_buffer_t,		/* attr */
	    gss_buffer_t		/* value */
	/* */);

	OM_uint32	(*gss_delete_name_attribute)
	(
	    OM_uint32 *,		/* minor_status */
	    gss_name_t,			/* name */
	    gss_buffer_t		/* attr */
	/* */);

	OM_uint32	(*gss_export_name_composite)
	(
	    OM_uint32 *,		/* minor_status */
	    gss_name_t,			/* name */
	    gss_buffer_t		/* exp_composite_name */
	/* */);

	OM_uint32	(*gss_map_name_to_any)
	(
	    OM_uint32 *,		/* minor_status */
	    gss_name_t,			/* name */
	    int,			/* authenticated */
	    gss_buffer_t,		/* type_id */
	    gss_any_t *			/* output */
	/* */);

	OM_uint32	(*gss_release_any_name_mapping)
	(
	    OM_uint32 *,		/* minor_status */
	    gss_name_t,			/* name */
	    gss_buffer_t,		/* type_id */
	    gss_any_t *			/* input */
	/* */);

        OM_uint32       (*gss_pseudo_random)
        (
            OM_uint32 *,                /* minor_status */
            gss_ctx_id_t,               /* context */
            int,                        /* prf_key */
            const gss_buffer_t,         /* prf_in */
            ssize_t,                    /* desired_output_len */
            gss_buffer_t                /* prf_out */
        /* */);

	OM_uint32	(*gss_set_neg_mechs)
	(
	    OM_uint32 *,		/* minor_status */
	    gss_cred_id_t,		/* cred_handle */
	    const gss_OID_set		/* mech_set */
	/* */);
#endif /* #ifndef _KERNEL */
} *gss_mechanism;

#ifndef _KERNEL
/* This structure MUST NOT be used by any code outside libgss */
typedef struct gss_config_ext {
    gss_acquire_cred_with_password_sfct	gss_acquire_cred_with_password;
} *gss_mechanism_ext;
#endif /* ifndef _KERNEL */

/*
 * In the user space we use a wrapper structure to encompass the
 * mechanism entry points.  The wrapper contain the mechanism
 * entry points and other data which is only relevant to the gss-api
 * layer.  In the kernel we use only the gss_config strucutre because
 * the kernal does not cantain any of the extra gss-api specific data.
 */
typedef struct gss_mech_config {
	char *kmodName;			/* kernel module name */
	char *uLibName;			/* user library name */
	char *mechNameStr;		/* mechanism string name */
	char *optionStr;		/* optional mech parameters */
	void *dl_handle;		/* RTLD object handle for the mech */
	gss_OID mech_type;		/* mechanism oid */
	gss_mechanism mech;		/* mechanism initialization struct */
#ifndef _KERNEL
 	gss_mechanism_ext mech_ext;	/* extensions */
#endif
 	int priority;			/* mechanism preference order */
	int freeMech;			/* free mech table */
	struct gss_mech_config *next;	/* next element in the list */
} *gss_mech_info;

/********************************************************/
/* Internal mechglue routines */


/* Solaris Kerberos: we use this */
int gssint_mechglue_init(void);
void gssint_mechglue_fini(void);

gss_mechanism gssint_get_mechanism (gss_OID);
#ifndef _KERNEL
gss_mechanism_ext gssint_get_mechanism_ext(const gss_OID);
#endif
OM_uint32 gssint_get_mech_type (gss_OID, gss_buffer_t);
char *gssint_get_kmodName(const gss_OID);
char *gssint_get_modOptions(const gss_OID);
OM_uint32 gssint_import_internal_name (OM_uint32 *, gss_OID, gss_union_name_t,
				      gss_name_t *);
OM_uint32 gssint_export_internal_name(OM_uint32 *, const gss_OID,
	const gss_name_t, gss_buffer_t);
OM_uint32 gssint_display_internal_name (OM_uint32 *, gss_OID, gss_name_t,
				       gss_buffer_t, gss_OID *);
OM_uint32 gssint_release_internal_name (OM_uint32 *, gss_OID, gss_name_t *);
OM_uint32 gssint_delete_internal_sec_context (OM_uint32 *, gss_OID,
					      gss_ctx_id_t *, gss_buffer_t);
#ifdef _GSS_STATIC_LINK
int gssint_register_mechinfo(gss_mech_info template);
#endif

OM_uint32 gssint_convert_name_to_union_name
	  (OM_uint32 *,		/* minor_status */
	   gss_mechanism,	/* mech */
	   gss_name_t,		/* internal_name */
	   gss_name_t *		/* external_name */
	   );
gss_cred_id_t gssint_get_mechanism_cred
	  (gss_union_cred_t,	/* union_cred */
	   gss_OID		/* mech_type */
	   );

OM_uint32 gssint_create_copy_buffer(
	const gss_buffer_t,	/* src buffer */
	gss_buffer_t *,		/* destination buffer */
	int			/* NULL terminate buffer ? */
);

OM_uint32 gssint_copy_oid_set(
	OM_uint32 *,			/* minor_status */
	const gss_OID_set_desc * const,	/* oid set */
	gss_OID_set *			/* new oid set */
);

gss_OID gss_find_mechanism_from_name_type (gss_OID); /* name_type */

OM_uint32 gss_add_mech_name_type
	   (OM_uint32 *,	/* minor_status */
	    gss_OID,		/* name_type */
	    gss_OID		/* mech */
	       );

/*
 * Sun extensions to GSS-API v2
 */

OM_uint32
gssint_mech_to_oid(
	const char *mech,		/* mechanism string name */
	gss_OID *oid			/* mechanism oid */
);

const char *
gssint_oid_to_mech(
	const gss_OID oid		/* mechanism oid */
);

OM_uint32
gssint_get_mechanisms(
	char *mechArray[],		/* array to populate with mechs */
	int arrayLen			/* length of passed in array */
);

OM_uint32
gssint_userok(
	OM_uint32 *,		/* minor */
	const gss_name_t,	/* name */
	const char *,		/* user */
	int *			/* user_ok */
);

OM_uint32
gss_store_cred(
	OM_uint32 *,		/* minor_status */
	const gss_cred_id_t,	/* input_cred_handle */
	gss_cred_usage_t,	/* cred_usage */
	const gss_OID,		/* desired_mech */
	OM_uint32,		/* overwrite_cred */
	OM_uint32,		/* default_cred */
	gss_OID_set *,		/* elements_stored */
	gss_cred_usage_t *	/* cred_usage_stored */
);

int
gssint_get_der_length(
	unsigned char **,	/* buf */
	unsigned int,		/* buf_len */
	unsigned int *		/* bytes */
);

unsigned int
gssint_der_length_size(unsigned int /* len */);

int
gssint_put_der_length(
	unsigned int,		/* length */
	unsigned char **,	/* buf */
	unsigned int		/* max_len */
);

OM_uint32
gssint_wrap_aead (gss_mechanism,	/* mech */
		  OM_uint32 *,		/* minor_status */
		  gss_union_ctx_id_t,	/* ctx */
		  int,			/* conf_req_flag */
		  gss_qop_t,		/* qop_req_flag */
		  gss_buffer_t,		/* input_assoc_buffer */
		  gss_buffer_t,		/* input_payload_buffer */
		  int *,		/* conf_state */
		  gss_buffer_t);	/* output_message_buffer */
OM_uint32
gssint_unwrap_aead (gss_mechanism,	/* mech */
		    OM_uint32 *,	/* minor_status */
		    gss_union_ctx_id_t,	/* ctx */
		    gss_buffer_t,	/* input_message_buffer */
		    gss_buffer_t,	/* input_assoc_buffer */
		    gss_buffer_t,	/* output_payload_buffer */
		    int *,		/* conf_state */
		    gss_qop_t *);	/* qop_state */

/*
 * Solaris Kerberos
 * New map error API in MIT 1.7, at build time generates code for errors.
 * Solaris does not gen the errors at build time so we just stub these
 * for now, need to revisit.
 * See mglueP.h and util_errmap.c in MIT 1.7.
*/
#ifdef _KERNEL

#define map_error(MINORP, MECH)
#define map_errcode(MINORP)

#else  /* _KERNEL */

/* Use this to map an error code that was returned from a mech
   operation; the mech will be asked to produce the associated error
   messages.

   Remember that if the minor status code cannot be returned to the
   caller (e.g., if it's stuffed in an automatic variable and then
   ignored), then we don't care about producing a mapping.  */
#define map_error(MINORP, MECH) \
    (*(MINORP) = gssint_mecherrmap_map(*(MINORP), &(MECH)->mech_type))
#define map_error_oid(MINORP, MECHOID) \
    (*(MINORP) = gssint_mecherrmap_map(*(MINORP), (MECHOID)))

/* Use this to map an errno value or com_err error code being
   generated within the mechglue code (e.g., by calling generic oid
   ops).  Any errno or com_err values produced by mech operations
   should be processed with map_error.  This means they'll be stored
   separately even if the mech uses com_err, because we can't assume
   that it will use com_err.  */
#define map_errcode(MINORP) \
    (*(MINORP) = gssint_mecherrmap_map_errcode(*(MINORP)))

#endif /* _KERNEL */

/* Solaris Kerberos: kernel and gssd support */

/*
 * derived types for passing context and credential handles
 * between gssd and kernel
 */
typedef unsigned int gssd_ctx_id_t;
typedef unsigned int gssd_cred_id_t;

/* SUNW15resync - Solaris versions - replace w/mit ones? */
gss_mechanism __gss_get_mechanism(const gss_OID);
#ifndef _KERNEL
gss_mechanism_ext __gss_get_mechanism_ext(const gss_OID);
#endif /* _KERNEL */
char *__gss_get_kmodName(const gss_OID);
char *__gss_get_modOptions(const gss_OID);
OM_uint32 __gss_import_internal_name(OM_uint32 *, const gss_OID,
 	gss_union_name_t, gss_name_t *);
OM_uint32 __gss_export_internal_name(OM_uint32 *, const gss_OID,
	const gss_name_t, gss_buffer_t);
OM_uint32 __gss_display_internal_name(OM_uint32 *, const gss_OID,
	const gss_name_t, gss_buffer_t, gss_OID *);
OM_uint32 __gss_release_internal_name(OM_uint32 *, const gss_OID,
	gss_name_t *);
OM_uint32 gssint_delete_internal_sec_context (OM_uint32 *, gss_OID,
	gss_ctx_id_t *, gss_buffer_t);
OM_uint32 __gss_convert_name_to_union_name(
	OM_uint32 *,		/* minor_status */
	gss_mechanism,	/* mech */
	gss_name_t,		/* internal_name */
	gss_name_t *		/* external_name */
);

 gss_cred_id_t __gss_get_mechanism_cred(
	const gss_union_cred_t,	/* union_cred */
	const gss_OID		/* mech_type */
);

#ifdef	_KERNEL

#ifndef	_KRB5_H
/* These macros are defined for Kerberos in krb5.h, and have priority */
#define	MALLOC(n) kmem_alloc((n), KM_SLEEP)
#define	FREE(x, n) kmem_free((x), (n))
#endif	/* _KRB5_H */

gss_mechanism __kgss_get_mechanism(gss_OID);
void __kgss_add_mechanism(gss_mechanism);
#endif /* _KERNEL */

struct	kgss_cred {
	gssd_cred_id_t	gssd_cred;
	OM_uint32	gssd_cred_verifier;
};

#define	KCRED_TO_KGSS_CRED(cred)	((struct kgss_cred *)(cred))
#define	KCRED_TO_CRED(cred)	(KCRED_TO_KGSS_CRED(cred)->gssd_cred)
#define	KCRED_TO_CREDV(cred)    (KCRED_TO_KGSS_CRED(cred)->gssd_cred_verifier)

struct	kgss_ctx {
	gssd_ctx_id_t	gssd_ctx;
#ifdef _KERNEL
	gss_ctx_id_t	gssd_i_ctx;
	bool_t		ctx_imported;
	gss_mechanism	mech;
#endif /* _KERNEL */
	OM_uint32	gssd_ctx_verifier;
};

#define	KCTX_TO_KGSS_CTX(ctx)	((struct kgss_ctx *)(ctx))
#define	KCTX_TO_CTX_IMPORTED(ctx)	(KCTX_TO_KGSS_CTX(ctx)->ctx_imported)
#define	KCTX_TO_GSSD_CTX(ctx)	(KCTX_TO_KGSS_CTX(ctx)->gssd_ctx)
#define	KCTX_TO_CTXV(ctx)	(KCTX_TO_KGSS_CTX(ctx)->gssd_ctx_verifier)
#define	KCTX_TO_MECH(ctx)	(KCTX_TO_KGSS_CTX(ctx)->mech)
#define	KGSS_CTX_TO_GSSD_CTX(ctx)	\
	(((ctx) == GSS_C_NO_CONTEXT) ? (gssd_ctx_id_t)(uintptr_t)(ctx) : \
	KCTX_TO_GSSD_CTX(ctx))
#define	KGSS_CTX_TO_GSSD_CTXV(ctx)	\
	(((ctx) == GSS_C_NO_CONTEXT) ? (NULL) : KCTX_TO_CTXV(ctx))

#ifdef _KERNEL
#define	KCTX_TO_I_CTX(ctx)	(KCTX_TO_KGSS_CTX(ctx)->gssd_i_ctx)
#define	KCTX_TO_CTX(ctx) \
((KCTX_TO_CTX_IMPORTED(ctx) == FALSE) ? (ctx) : \
	KCTX_TO_I_CTX(ctx))
#define	KGSS_CRED_ALLOC()	kmem_zalloc(sizeof (struct kgss_cred), \
	KM_SLEEP)
#define	KGSS_CRED_FREE(cred)	kmem_free(cred, sizeof (struct kgss_cred))

#define	KGSS_ALLOC()	kmem_zalloc(sizeof (struct kgss_ctx), KM_SLEEP)
#define	KGSS_FREE(ctx)	kmem_free(ctx, sizeof (struct kgss_ctx))

#define	KGSS_SIGN(minor_st, ctx, qop, msg, tkn)	\
	(*(KCTX_TO_MECH(ctx)->gss_sign))(minor_st, \
		KCTX_TO_CTX(ctx), qop, msg, tkn, KCTX_TO_CTXV(ctx))

#define	KGSS_VERIFY(minor_st, ctx, msg, tkn, qop)	\
	(*(KCTX_TO_MECH(ctx)->gss_verify))(minor_st,\
		KCTX_TO_CTX(ctx), msg, tkn, qop,  KCTX_TO_CTXV(ctx))

#define	KGSS_DELETE_SEC_CONTEXT(minor_st, ctx, int_ctx_id,  tkn)	\
	(*(KCTX_TO_MECH(ctx)->gss_delete_sec_context))(\
		minor_st, int_ctx_id, tkn, KCTX_TO_CTXV(ctx))

#define	KGSS_IMPORT_SEC_CONTEXT(minor_st, tkn, ctx, int_ctx_id)	\
	(*(KCTX_TO_MECH(ctx)->gss_import_sec_context))(\
		minor_st, tkn, int_ctx_id)

/* EXPORT DELETE START */
#define	KGSS_SEAL(minor_st, ctx, conf_req, qop, msg, conf_state, tkn) \
	(*(KCTX_TO_MECH(ctx)->gss_seal))(minor_st, \
		KCTX_TO_CTX(ctx), conf_req, qop, msg, conf_state, tkn,\
		KCTX_TO_CTXV(ctx))

#define	KGSS_UNSEAL(minor_st, ctx, msg, tkn, conf, qop)	\
	(*(KCTX_TO_MECH(ctx)->gss_unseal))(minor_st,\
		KCTX_TO_CTX(ctx), msg, tkn, conf, qop, \
		KCTX_TO_CTXV(ctx))

/* EXPORT DELETE END */

#define KGSS_INIT_CONTEXT(ctx) krb5_init_context(ctx)
#define KGSS_RELEASE_OID(minor_st, oid) krb5_gss_release_oid(minor_st, oid)
extern OM_uint32 kgss_release_oid(OM_uint32 *, gss_OID *);

#else /* !_KERNEL */

#define KGSS_INIT_CONTEXT(ctx) krb5_gss_init_context(ctx)
#define KGSS_RELEASE_OID(minor_st, oid) gss_release_oid(minor_st, oid)

#define	KCTX_TO_CTX(ctx)  (KCTX_TO_KGSS_CTX(ctx)->gssd_ctx)
#define	MALLOC(n) malloc(n)
#define	FREE(x, n) free(x)
#define	KGSS_CRED_ALLOC()	(struct kgss_cred *) \
		MALLOC(sizeof (struct kgss_cred))
#define	KGSS_CRED_FREE(cred)	free(cred)
#define	KGSS_ALLOC()	(struct kgss_ctx *)MALLOC(sizeof (struct kgss_ctx))
#define	KGSS_FREE(ctx)	free(ctx)

#define	KGSS_SIGN(minor_st, ctx, qop, msg, tkn)	\
	kgss_sign_wrapped(minor_st, \
		KCTX_TO_CTX(ctx), qop, msg, tkn, KCTX_TO_CTXV(ctx))

#define	KGSS_VERIFY(minor_st, ctx, msg, tkn, qop)	\
	kgss_verify_wrapped(minor_st,\
		KCTX_TO_CTX(ctx), msg, tkn, qop, KCTX_TO_CTXV(ctx))

#define	KGSS_SEAL(minor_st, ctx, conf_req, qop, msg, conf_state, tkn) \
	kgss_seal_wrapped(minor_st, \
		KCTX_TO_CTX(ctx), conf_req, qop, msg, conf_state, tkn, \
		KCTX_TO_CTXV(ctx))

#define	KGSS_UNSEAL(minor_st, ctx, msg, tkn, conf, qop)	\
	kgss_unseal_wrapped(minor_st,\
		KCTX_TO_CTX(ctx), msg, tkn, conf, qop,  \
		KCTX_TO_CTXV(ctx))
#endif /* _KERNEL */
#endif /* _GSS_MECHGLUEP_H */
