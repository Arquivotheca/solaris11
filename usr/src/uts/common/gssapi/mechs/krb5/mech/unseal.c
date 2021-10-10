/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
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
/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/* EXPORT DELETE START */

#include "gssapiP_krb5.h"

/*
 * $Id: unseal.c 23457 2009-12-08 00:04:48Z tlyu $
 */

/*
 * Solaris Kerberos: support for kernel, export builds and keeping
 * krb5_gss_unseal
 */
/*ARGSUSED*/
OM_uint32
krb5_gss_unseal(minor_status, context_handle,
		input_message_buffer, output_message_buffer,
		conf_state, qop_state
#ifdef	 _KERNEL
 		, gssd_ctx_verifier
#endif
		)
     OM_uint32 *minor_status;
     gss_ctx_id_t context_handle;
     gss_buffer_t input_message_buffer;
     gss_buffer_t output_message_buffer;
     int *conf_state;
     int *qop_state;
#ifdef	 _KERNEL
     OM_uint32 gssd_ctx_verifier;
#endif
{
   return(kg_unseal(minor_status, context_handle,
		    input_message_buffer, output_message_buffer,
		    conf_state, (gss_qop_t *) qop_state, KG_TOK_SEAL_MSG));
}

/* V2 interface */
OM_uint32
krb5_gss_unwrap(minor_status, context_handle,
                input_message_buffer, output_message_buffer,
                conf_state, qop_state)
    OM_uint32           *minor_status;
    gss_ctx_id_t        context_handle;
    gss_buffer_t        input_message_buffer;
    gss_buffer_t        output_message_buffer;
    int                 *conf_state;
    gss_qop_t           *qop_state;
{
#ifdef	KRB5_NO_PRIVACY
    return (GSS_S_FAILURE);
#else
    OM_uint32           rstat;

    rstat = kg_unseal(minor_status, context_handle,
                      input_message_buffer, output_message_buffer,
                      conf_state, qop_state, KG_TOK_WRAP_MSG);
    return(rstat);
#endif
}

/* Solaris Kerberos: not supported yet */
#if 0 /************** Begin IFDEF'ed OUT *******************************/
/* AEAD interface */
OM_uint32
krb5_gss_unwrap_iov(OM_uint32 *minor_status,
                    gss_ctx_id_t context_handle,
                    int *conf_state,
                    gss_qop_t *qop_state,
                    gss_iov_buffer_desc *iov,
                    int iov_count)
{
#ifdef	KRB5_NO_PRIVACY
    return (GSS_S_FAILURE);
#else
    OM_uint32 major_status;

    major_status = kg_unseal_iov(minor_status, context_handle,
                                 conf_state, qop_state,
                                 iov, iov_count, KG_TOK_WRAP_MSG);

    return major_status;
#endif
}
#endif /**************** END IFDEF'ed OUT *******************************/
/* EXPORT DELETE END */
