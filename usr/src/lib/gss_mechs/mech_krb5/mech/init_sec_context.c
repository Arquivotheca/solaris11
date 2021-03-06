/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Copyright 2000, 2002, 2003, 2007, 2008 by the Massachusetts Institute of Technology.
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
 * Copyright (C) 1998 by the FundsXpress, INC.
 *
 * All rights reserved.
 *
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of FundsXpress. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  FundsXpress makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
/*
 * Copyright (c) 2006-2008, Novell, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *   * The copyright holder's name is not used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/* Solaris Kerberos */
#include <libintl.h>
#include <locale.h>
#include <ctype.h>

#include "k5-int.h"
#include "gssapiP_krb5.h"
#include "mglueP.h" /* Solaris Kerberos */
#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif
#include <stdlib.h>
#include <assert.h>

/* Solaris Kerberos start */
static OM_uint32 get_default_cred(OM_uint32 *, void *, gss_cred_id_t *);
/* Solaris Kerberos end */

/*
 * $Id: init_sec_context.c 23706 2010-02-08 20:55:36Z tlyu $
 */

/* XXX This is for debugging only!!!  Should become a real bitfield
   at some point */
int krb5_gss_dbg_client_expcreds = 0;

/*
 * Common code which fetches the correct krb5 credentials from the
 * ccache.
 */
static krb5_error_code get_credentials(context, cred, server, now,
                                       endtime, out_creds)
    krb5_context context;
    krb5_gss_cred_id_t cred;
    krb5_gss_name_t server;
    krb5_timestamp now;
    krb5_timestamp endtime;
    krb5_creds **out_creds;
{
    krb5_error_code     code;
    krb5_creds          in_creds, evidence_creds;
    krb5_flags          flags = 0;
    krb5_principal      cc_princ = NULL;

    k5_mutex_assert_locked(&cred->lock);
    memset(&in_creds, 0, sizeof(krb5_creds));
    memset(&evidence_creds, 0, sizeof(krb5_creds));
    in_creds.client = in_creds.server = NULL;

    assert(cred->name != NULL);

    if ((code = krb5_cc_get_principal(context, cred->ccache, &cc_princ)))
        goto cleanup;

    /*
     * Do constrained delegation if we have proxy credentials and
     * we're not trying to get a ticket to ourselves (in which case
     * we can just use the S4U2Self or evidence ticket directly).
     */
    if (cred->proxy_cred &&
        !krb5_principal_compare(context, cc_princ, server->princ)) {
        krb5_creds mcreds;

        flags |= KRB5_GC_CANONICALIZE |
            KRB5_GC_NO_STORE |
            KRB5_GC_CONSTRAINED_DELEGATION;

        memset(&mcreds, 0, sizeof(mcreds));

        mcreds.magic = KV5M_CREDS;
        mcreds.times.endtime = cred->tgt_expire;
        mcreds.server = cc_princ;
        mcreds.client = cred->name->princ;

        code = krb5_cc_retrieve_cred(context, cred->ccache,
                                     KRB5_TC_MATCH_TIMES | KRB5_TC_MATCH_AUTHDATA,
                                     &mcreds,
                                     &evidence_creds);
        if (code)
            goto cleanup;

        assert(evidence_creds.ticket_flags & TKT_FLG_FORWARDABLE);

        in_creds.client = cc_princ;
        in_creds.second_ticket = evidence_creds.ticket;
    } else {
        in_creds.client = cred->name->princ;
    }

    in_creds.server = server->princ;
    in_creds.times.endtime = endtime;
    in_creds.authdata = NULL;
    in_creds.keyblock.enctype = 0;

    /*
     * cred->name is immutable, so there is no need to acquire
     * cred->name->lock.
     */
    if (cred->name->ad_context != NULL) {
        code = krb5_authdata_export_authdata(context,
                                             cred->name->ad_context,
                                             AD_USAGE_TGS_REQ,
                                             &in_creds.authdata);
        if (code != 0)
            goto cleanup;
    }

    code = krb5_get_credentials(context, flags, cred->ccache,
                                &in_creds, out_creds);
    if (code)
        goto cleanup;

    if (flags & KRB5_GC_CONSTRAINED_DELEGATION) {
        if (!krb5_principal_compare(context, cred->name->princ,
                                    (*out_creds)->client)) {
            /* server did not support constrained delegation */
            code = KRB5_KDCREP_MODIFIED;
            goto cleanup;
        }
    }

    /*
     * Enforce a stricter limit (without timeskew forgiveness at the
     * boundaries) because accept_sec_context code is also similarly
     * non-forgiving.
     */
    if (!krb5_gss_dbg_client_expcreds && *out_creds != NULL &&
        (*out_creds)->times.endtime < now) {
        code = KRB5KRB_AP_ERR_TKT_EXPIRED;
        goto cleanup;
    }

cleanup:
    krb5_free_authdata(context, in_creds.authdata);
    krb5_free_principal(context, cc_princ);
    krb5_free_cred_contents(context, &evidence_creds);

    return code;
}
struct gss_checksum_data {
    krb5_gss_ctx_id_rec *ctx;
    krb5_gss_cred_id_t cred;
    krb5_checksum md5;
    krb5_data checksum_data;
};

#ifdef CFX_EXERCISE
#include "../../krb5/krb/auth_con.h"
#endif
static krb5_error_code KRB5_CALLCONV
make_gss_checksum (krb5_context context, krb5_auth_context auth_context,
                   void *cksum_data, krb5_data **out)
{
    krb5_error_code code;
    krb5_int32 con_flags;
    unsigned char *ptr;
    struct gss_checksum_data *data = cksum_data;
    krb5_data credmsg;
    unsigned int junk;

    data->checksum_data.data = 0;
    credmsg.data = 0;
    /* build the checksum field */

    if (data->ctx->gss_flags & GSS_C_DELEG_FLAG) {
        /* first get KRB_CRED message, so we know its length */

        /* clear the time check flag that was set in krb5_auth_con_init() */
        krb5_auth_con_getflags(context, auth_context, &con_flags);
        krb5_auth_con_setflags(context, auth_context,
                               con_flags & ~KRB5_AUTH_CONTEXT_DO_TIME);

        assert(data->cred->name != NULL);

        code = krb5_fwd_tgt_creds(context, auth_context, 0,
                                  data->cred->name->princ, data->ctx->there->princ,
                                  data->cred->ccache, 1,
                                  &credmsg);

        /* turn KRB5_AUTH_CONTEXT_DO_TIME back on */
        krb5_auth_con_setflags(context, auth_context, con_flags);

        if (code) {
            /* don't fail here; just don't accept/do the delegation
               request */
            data->ctx->gss_flags &= ~(GSS_C_DELEG_FLAG |
                                      GSS_C_DELEG_POLICY_FLAG);

            data->checksum_data.length = 24;
        } else {
            if (credmsg.length+28 > KRB5_INT16_MAX) {
                krb5_free_data_contents(context, &credmsg);
                return(KRB5KRB_ERR_FIELD_TOOLONG);
            }

            data->checksum_data.length = 28+credmsg.length;
        }
    } else {
        data->checksum_data.length = 24;
    }
#ifdef CFX_EXERCISE
    if (data->ctx->auth_context->keyblock != NULL
        && data->ctx->auth_context->keyblock->enctype == 18) {
        srand(time(0) ^ getpid());
        /* Our ftp client code stupidly assumes a base64-encoded
           version of the token will fit in 10K, so don't make this
           too big.  */
        junk = rand() & 0xff;
    } else
        junk = 0;
#else
    junk = 0;
#endif

    data->checksum_data.length += junk;

    /* now allocate a buffer to hold the checksum data and
       (maybe) KRB_CRED msg */

    if ((data->checksum_data.data =
         (char *) xmalloc(data->checksum_data.length)) == NULL) {
        if (credmsg.data)
            krb5_free_data_contents(context, &credmsg);
        return(ENOMEM);
    }

    ptr = (unsigned char *)data->checksum_data.data;

    TWRITE_INT(ptr, data->md5.length, 0);
    TWRITE_STR(ptr, data->md5.contents, data->md5.length);
    TWRITE_INT(ptr, data->ctx->gss_flags, 0);

    /* done with this, free it */
    xfree(data->md5.contents);

    if (credmsg.data) {
        TWRITE_INT16(ptr, KRB5_GSS_FOR_CREDS_OPTION, 0);
        TWRITE_INT16(ptr, credmsg.length, 0);
        TWRITE_STR(ptr, credmsg.data, credmsg.length);

        /* free credmsg data */
        krb5_free_data_contents(context, &credmsg);
    }
    if (junk)
        memset(ptr, 'i', junk);
    *out = &data->checksum_data;
    return 0;
}

static krb5_error_code
make_ap_req_v1(context, ctx, cred, k_cred, ad_context,
               chan_bindings, mech_type, token)
    krb5_context context;
    krb5_gss_ctx_id_rec *ctx;
    krb5_gss_cred_id_t cred;
    krb5_creds *k_cred;
    krb5_authdata_context ad_context;
    gss_channel_bindings_t chan_bindings;
    gss_OID mech_type;
    gss_buffer_t token;
{
    krb5_flags mk_req_flags = 0;
    krb5_error_code code;
    struct gss_checksum_data cksum_struct;
    krb5_checksum md5;
    krb5_data ap_req;
    krb5_data *checksum_data = NULL;
    unsigned char *ptr;
    unsigned char *t;
    unsigned int tlen;

    k5_mutex_assert_locked(&cred->lock);
    ap_req.data = 0;

    /* compute the hash of the channel bindings */

    if ((code = kg_checksum_channel_bindings(context, chan_bindings, &md5, 0)))
        return(code);

    krb5_auth_con_set_req_cksumtype(context, ctx->auth_context,
                                    CKSUMTYPE_KG_CB);
    cksum_struct.md5 = md5;
    cksum_struct.ctx = ctx;
    cksum_struct.cred = cred;
    cksum_struct.checksum_data.data = NULL;
    switch (k_cred->keyblock.enctype) {
    case ENCTYPE_DES_CBC_CRC:
    case ENCTYPE_DES_CBC_MD4:
    case ENCTYPE_DES_CBC_MD5:
    case ENCTYPE_DES3_CBC_SHA1:
        code = make_gss_checksum(context, ctx->auth_context, &cksum_struct,
                                 &checksum_data);
        if (code)
            goto cleanup;
        break;
    default:
        krb5_auth_con_set_checksum_func(context, ctx->auth_context,
                                        make_gss_checksum, &cksum_struct);
        break;
    }


    /* call mk_req.  subkey and ap_req need to be used or destroyed */

    mk_req_flags = AP_OPTS_USE_SUBKEY;

    if (ctx->gss_flags & GSS_C_MUTUAL_FLAG)
        mk_req_flags |= AP_OPTS_MUTUAL_REQUIRED | AP_OPTS_ETYPE_NEGOTIATION;

    krb5_auth_con_set_authdata_context(context, ctx->auth_context, ad_context);
    code = krb5_mk_req_extended(context, &ctx->auth_context, mk_req_flags,
                                checksum_data, k_cred, &ap_req);
    krb5_auth_con_set_authdata_context(context, ctx->auth_context, NULL);
    krb5_free_data_contents(context, &cksum_struct.checksum_data);
    if (code)
        goto cleanup;

    /* store the interesting stuff from creds and authent */
    ctx->krb_times = k_cred->times;
    ctx->krb_flags = k_cred->ticket_flags;

    /* build up the token */
    if (ctx->gss_flags & GSS_C_DCE_STYLE) {
        /*
         * For DCE RPC, do not encapsulate the AP-REQ in the
         * typical GSS wrapping.
         */
        token->length = ap_req.length;
        token->value = ap_req.data;

        ap_req.data = NULL; /* don't double free */
    } else {
        /* allocate space for the token */
        tlen = g_token_size((gss_OID) mech_type, ap_req.length);

        if ((t = (unsigned char *) xmalloc(tlen)) == NULL) {
            code = ENOMEM;
            goto cleanup;
        }

        /* fill in the buffer */
        ptr = t;

        g_make_token_header(mech_type, ap_req.length,
                            &ptr, KG_TOK_CTX_AP_REQ);

        TWRITE_STR(ptr, ap_req.data, ap_req.length);

        /* pass it back */

        token->length = tlen;
        token->value = (void *) t;
    }

    code = 0;

cleanup:
    if (checksum_data && checksum_data->data)
        krb5_free_data_contents(context, checksum_data);
    if (ap_req.data)
        krb5_free_data_contents(context, &ap_req);

    return (code);
}

/*
 * new_connection
 *
 * Do the grunt work of setting up a new context.
 */
OM_uint32
kg_new_connection(
    OM_uint32 *minor_status,
    krb5_gss_cred_id_t cred,
    gss_ctx_id_t *context_handle,
    gss_name_t target_name,
    gss_OID mech_type,
    OM_uint32 req_flags,
    OM_uint32 time_req,
    gss_channel_bindings_t input_chan_bindings,
    gss_buffer_t input_token,
    gss_OID *actual_mech_type,
    gss_buffer_t output_token,
    OM_uint32 *ret_flags,
    OM_uint32 *time_rec,
    krb5_context context,
    int default_mech)
{
    OM_uint32 major_status;
    krb5_error_code code;
    krb5_creds *k_cred;
    krb5_gss_ctx_id_rec *ctx, *ctx_free;
    krb5_timestamp now;
    gss_buffer_desc token;
    krb5_keyblock *keyblock;

    k5_mutex_assert_locked(&cred->lock);
    major_status = GSS_S_FAILURE;
    token.length = 0;
    token.value = NULL;

    /* make sure the cred is usable for init */

    if ((cred->usage != GSS_C_INITIATE) &&
        (cred->usage != GSS_C_BOTH)) {
        *minor_status = 0;
        return(GSS_S_NO_CRED);
    }

    /* complain if the input token is non-null */

    if (input_token != GSS_C_NO_BUFFER && input_token->length != 0) {
        *minor_status = 0;
        return(GSS_S_DEFECTIVE_TOKEN);
    }

    /* create the ctx */

    if ((ctx = (krb5_gss_ctx_id_rec *) xmalloc(sizeof(krb5_gss_ctx_id_rec)))
        == NULL) {
        *minor_status = ENOMEM;
        return(GSS_S_FAILURE);
    }

    /* fill in the ctx */
    memset(ctx, 0, sizeof(krb5_gss_ctx_id_rec));
    ctx_free = ctx;
    if ((code = krb5_auth_con_init(context, &ctx->auth_context)))
        goto fail;
    krb5_auth_con_setflags(context, ctx->auth_context,
                           KRB5_AUTH_CONTEXT_DO_SEQUENCE);

    /* limit the encryption types negotiated (if requested) */
    if (cred->req_enctypes) {
        if ((code = krb5_set_default_tgs_enctypes(context,
                                                  cred->req_enctypes))) {
            goto fail;
        }
    }

    ctx->initiate = 1;
    ctx->gss_flags = (GSS_C_INTEG_FLAG | GSS_C_CONF_FLAG |
                      GSS_C_TRANS_FLAG |
                      ((req_flags) & (GSS_C_MUTUAL_FLAG | GSS_C_REPLAY_FLAG |
                                      GSS_C_SEQUENCE_FLAG | GSS_C_DELEG_FLAG |
                                      GSS_C_DCE_STYLE | GSS_C_IDENTIFY_FLAG |
                                      GSS_C_EXTENDED_ERROR_FLAG)));
    ctx->seed_init = 0;
    ctx->big_endian = 0;  /* all initiators do little-endian, as per spec */
    ctx->seqstate = 0;

    if (req_flags & GSS_C_DCE_STYLE)
        ctx->gss_flags |= GSS_C_MUTUAL_FLAG;

    if ((code = krb5_timeofday(context, &now)))
        goto fail;

    if (time_req == 0 || time_req == GSS_C_INDEFINITE) {
        ctx->krb_times.endtime = 0;
    } else {
        ctx->krb_times.endtime = now + time_req;
    }

    if ((code = kg_duplicate_name(context, cred->name, 0, &ctx->here)))
        goto fail;

    if ((code = kg_duplicate_name(context, (krb5_gss_name_t)target_name, 0, &ctx->there)))
        goto fail;

    code = get_credentials(context, cred, ctx->there, now,
                           ctx->krb_times.endtime, &k_cred);
    if (code)
        goto fail;

    ctx->krb_times = k_cred->times;

    /*
     * GSS_C_DELEG_POLICY_FLAG means to delegate only if the
     * ok-as-delegate ticket flag is set.
     */
    if ((req_flags & GSS_C_DELEG_POLICY_FLAG)
        && (k_cred->ticket_flags & TKT_FLG_OK_AS_DELEGATE))
        ctx->gss_flags |= GSS_C_DELEG_FLAG | GSS_C_DELEG_POLICY_FLAG;

    if (default_mech) {
        mech_type = (gss_OID) gss_mech_krb5;
    }

    if (generic_gss_copy_oid(minor_status, mech_type, &ctx->mech_used)
        != GSS_S_COMPLETE) {
        code = *minor_status;
        goto fail;
    }
    /*
     * Now try to make it static if at all possible....
     */
    ctx->mech_used = krb5_gss_convert_static_mech_oid(ctx->mech_used);

    {
        /* gsskrb5 v1 */
        krb5_int32 seq_temp;
        if ((code = make_ap_req_v1(context, ctx,
                                   cred, k_cred, ctx->here->ad_context,
                                   input_chan_bindings,
                                   mech_type, &token))) {
            if ((code == KRB5_FCC_NOFILE) || (code == KRB5_CC_NOTFOUND) ||
                (code == KG_EMPTY_CCACHE))
                major_status = GSS_S_NO_CRED;
            if (code == KRB5KRB_AP_ERR_TKT_EXPIRED)
                major_status = GSS_S_CREDENTIALS_EXPIRED;
            goto fail;
        }

        krb5_auth_con_getlocalseqnumber(context, ctx->auth_context, &seq_temp);
        ctx->seq_send = seq_temp;
        code = krb5_auth_con_getsendsubkey(context, ctx->auth_context,
                                           &keyblock);
        if (code != 0)
            goto fail;
        code = krb5_k_create_key(context, keyblock, &ctx->subkey);
        krb5_free_keyblock(context, keyblock);
        if (code != 0)
            goto fail;
    }

    krb5_free_creds(context, k_cred);
    k_cred = NULL;
    ctx->enc = NULL;
    ctx->seq = NULL;
    ctx->have_acceptor_subkey = 0;
    code = kg_setup_keys(context, ctx, ctx->subkey, &ctx->cksumtype);
    if (code != 0)
        goto fail;

    /* at this point, the context is constructed and valid,
       hence, releaseable */

    /* intern the context handle */

    if (! kg_save_ctx_id((gss_ctx_id_t) ctx)) {
        code = G_VALIDATE_FAILED;
        goto fail;
    }
    *context_handle = (gss_ctx_id_t) ctx;
    ctx_free = 0;

    /* compute time_rec */
    if (time_rec) {
        if ((code = krb5_timeofday(context, &now)))
            goto fail;
        *time_rec = ctx->krb_times.endtime - now;
    }

    /* set the other returns */
    *output_token = token;

    if (ret_flags)
        *ret_flags = ctx->gss_flags;

    if (actual_mech_type)
        *actual_mech_type = mech_type;

    /* return successfully */

    *minor_status = 0;
    if (ctx->gss_flags & GSS_C_MUTUAL_FLAG) {
        ctx->established = 0;
        return(GSS_S_CONTINUE_NEEDED);
    } else {
        ctx->seq_recv = ctx->seq_send;
        g_order_init(&(ctx->seqstate), ctx->seq_recv,
                     (ctx->gss_flags & GSS_C_REPLAY_FLAG) != 0,
                     (ctx->gss_flags & GSS_C_SEQUENCE_FLAG) != 0, ctx->proto);
        ctx->gss_flags |= GSS_C_PROT_READY_FLAG;
        ctx->established = 1;
        return(GSS_S_COMPLETE);
    }

fail:
    if (ctx_free) {
        if (ctx_free->auth_context)
            krb5_auth_con_free(context, ctx_free->auth_context);
        if (ctx_free->here)
            kg_release_name(context, 0, &ctx_free->here);
        if (ctx_free->there)
            kg_release_name(context, 0, &ctx_free->there);
        if (ctx_free->subkey)
            krb5_k_free_key(context, ctx_free->subkey);
        xfree(ctx_free);
    } else
        (void)krb5_gss_delete_sec_context(minor_status, context_handle, NULL);

    *minor_status = code;
    return (major_status);
}

/*
 * mutual_auth
 *
 * Handle the reply from the acceptor, if we're doing mutual auth.
 */
static OM_uint32
mutual_auth(
    OM_uint32 *minor_status,
    gss_ctx_id_t *context_handle,
    gss_name_t target_name,
    gss_OID mech_type,
    OM_uint32 req_flags,
    OM_uint32 time_req,
    gss_channel_bindings_t input_chan_bindings,
    gss_buffer_t input_token,
    gss_OID *actual_mech_type,
    gss_buffer_t output_token,
    OM_uint32 *ret_flags,
    OM_uint32 *time_rec,
    krb5_context context)
{
    OM_uint32 major_status;
    unsigned char *ptr;
    char *sptr;
    krb5_data ap_rep;
    krb5_ap_rep_enc_part *ap_rep_data;
    krb5_timestamp now;
    krb5_gss_ctx_id_rec *ctx;
    krb5_error *krb_error;
    krb5_error_code code;
    krb5int_access kaccess;

    major_status = GSS_S_FAILURE;

    code = krb5int_accessor (&kaccess, KRB5INT_ACCESS_VERSION);
    if (code)
        goto fail;

    /* validate the context handle */
    /*SUPPRESS 29*/
    if (! kg_validate_ctx_id(*context_handle)) {
        *minor_status = (OM_uint32) G_VALIDATE_FAILED;
        return(GSS_S_NO_CONTEXT);
    }

    ctx = (krb5_gss_ctx_id_t) *context_handle;

    /* make sure the context is non-established, and that certain
       arguments are unchanged */

    if ((ctx->established) ||
        ((ctx->gss_flags & GSS_C_MUTUAL_FLAG) == 0)) {
        code = KG_CONTEXT_ESTABLISHED;
        goto fail;
    }

    if (! kg_compare_name(context, ctx->there, (krb5_gss_name_t)target_name)) {
        /* Solaris Kerberos: spruce-up the err msg */
        krb5_principal tname = (krb5_principal) target_name;
        char *s_name = NULL, *s_princ= NULL;
        int kret = krb5_unparse_name(context, tname, &s_name);
        int kret1 = krb5_unparse_name(context, ctx->there->princ, &s_princ);
        code = KRB5_PRINC_NOMATCH;
        if (kret == 0 && kret1 == 0) {
            krb5_set_error_message(context, code,
                                   dgettext(TEXT_DOMAIN,
                                            "Target name principal '%s' does "
                                            "not match '%s'"),
                                   s_name, s_princ);
            save_error_info(code, context);
        }
        if (s_name)
            krb5_free_unparsed_name(context, s_name);
        if (s_princ)
            krb5_free_unparsed_name(context, s_princ);

        (void)krb5_gss_delete_sec_context(minor_status,
                                          context_handle, NULL);
        code = 0;
        major_status = GSS_S_BAD_NAME;
        goto fail;
    }

    /* verify the token and leave the AP_REP message in ap_rep */

    if (input_token == GSS_C_NO_BUFFER) {
        (void)krb5_gss_delete_sec_context(minor_status,
                                          context_handle, NULL);
        code = 0;
        major_status = GSS_S_DEFECTIVE_TOKEN;
        goto fail;
    }

    ptr = (unsigned char *) input_token->value;

    if (ctx->gss_flags & GSS_C_DCE_STYLE) {
        /* Raw AP-REP */
        ap_rep.length = input_token->length;
        ap_rep.data = (char *)input_token->value;
    } else if (g_verify_token_header(ctx->mech_used,
                                     &(ap_rep.length),
                                     &ptr, KG_TOK_CTX_AP_REP,
                                     input_token->length, 1)) {
        if (g_verify_token_header((gss_OID) ctx->mech_used,
                                  &(ap_rep.length),
                                  &ptr, KG_TOK_CTX_ERROR,
                                  input_token->length, 1) == 0) {

            /* Handle a KRB_ERROR message from the server */

            sptr = (char *) ptr;           /* PC compiler bug */
            TREAD_STR(sptr, ap_rep.data, ap_rep.length);

            code = krb5_rd_error(context, &ap_rep, &krb_error);
            if (code)
                goto fail;
            if (krb_error->error)
                code = (krb5_error_code)krb_error->error + ERROR_TABLE_BASE_krb5;
            else
                code = 0;
            krb5_free_error(context, krb_error);
            goto fail;
        } else {
            *minor_status = 0;
            return(GSS_S_DEFECTIVE_TOKEN);
        }
    }

    sptr = (char *) ptr;                      /* PC compiler bug */
    TREAD_STR(sptr, ap_rep.data, ap_rep.length);

    /* decode the ap_rep */
    if ((code = krb5_rd_rep(context, ctx->auth_context, &ap_rep,
                            &ap_rep_data))) {
        /*
         * XXX A hack for backwards compatiblity.
         * To be removed in 1999 -- proven
         */
        krb5_auth_con_setuseruserkey(context, ctx->auth_context,
                                     &ctx->subkey->keyblock);
        if ((krb5_rd_rep(context, ctx->auth_context, &ap_rep,
                         &ap_rep_data)))
            goto fail;
    }

    /* store away the sequence number */
    ctx->seq_recv = ap_rep_data->seq_number;
    g_order_init(&(ctx->seqstate), ctx->seq_recv,
                 (ctx->gss_flags & GSS_C_REPLAY_FLAG) != 0,
                 (ctx->gss_flags & GSS_C_SEQUENCE_FLAG) !=0, ctx->proto);

    if (ap_rep_data->subkey != NULL &&
        (ctx->proto == 1 || (ctx->gss_flags & GSS_C_DCE_STYLE) ||
         ap_rep_data->subkey->enctype != ctx->subkey->keyblock.enctype)) {
        /* Keep acceptor's subkey.  */
        ctx->have_acceptor_subkey = 1;
        code = krb5_k_create_key(context, ap_rep_data->subkey,
                                 &ctx->acceptor_subkey);
        if (code) {
            krb5_free_ap_rep_enc_part(context, ap_rep_data);
            goto fail;
        }
        code = kg_setup_keys(context, ctx, ctx->acceptor_subkey,
                             &ctx->acceptor_subkey_cksumtype);
        if (code) {
            krb5_free_ap_rep_enc_part(context, ap_rep_data);
            goto fail;
        }
    }
    /* free the ap_rep_data */
    krb5_free_ap_rep_enc_part(context, ap_rep_data);

    if (ctx->gss_flags & GSS_C_DCE_STYLE) {
        krb5_data outbuf;

        code = krb5_mk_rep_dce(context, ctx->auth_context, &outbuf);
        if (code)
            goto fail;

        output_token->value = outbuf.data;
        output_token->length = outbuf.length;
    }

    /* set established */
    ctx->established = 1;

    /* set returns */

    if (time_rec) {
        if ((code = krb5_timeofday(context, &now)))
            goto fail;
        *time_rec = ctx->krb_times.endtime - now;
    }

    if (ret_flags)
        *ret_flags = ctx->gss_flags;

    if (actual_mech_type)
        *actual_mech_type = mech_type;

    /* success */

    *minor_status = 0;
    return GSS_S_COMPLETE;

fail:
    (void)krb5_gss_delete_sec_context(minor_status, context_handle, NULL);

    *minor_status = code;
    return (major_status);
}

OM_uint32
krb5_gss_init_sec_context(minor_status, claimant_cred_handle,
                          context_handle, target_name, mech_type,
                          req_flags, time_req, input_chan_bindings,
                          input_token, actual_mech_type, output_token,
                          ret_flags, time_rec)
    OM_uint32 *minor_status;
    gss_cred_id_t claimant_cred_handle;
    gss_ctx_id_t *context_handle;
    gss_name_t target_name;
    gss_OID mech_type;
    OM_uint32 req_flags;
    OM_uint32 time_req;
    gss_channel_bindings_t input_chan_bindings;
    gss_buffer_t input_token;
    gss_OID *actual_mech_type;
    gss_buffer_t output_token;
    OM_uint32 *ret_flags;
    OM_uint32 *time_rec;
{
    krb5_context context;
    krb5_gss_cred_id_t cred;
    int err;
    krb5_error_code kerr;
    int default_mech = 0;
    OM_uint32 major_status;
    OM_uint32 tmp_min_stat;

    if (*context_handle == GSS_C_NO_CONTEXT) {
        kerr = krb5_gss_init_context(&context);
        if (kerr) {
            *minor_status = kerr;
            return GSS_S_FAILURE;
        }
        if (GSS_ERROR(kg_sync_ccache_name(context, minor_status))) {
            save_error_info(*minor_status, context);
            krb5_free_context(context);
            return GSS_S_FAILURE;
        }
    } else {
        context = ((krb5_gss_ctx_id_rec *)*context_handle)->k5_context;
    }

    /* set up return values so they can be "freed" successfully */

    major_status = GSS_S_FAILURE; /* Default major code */
    output_token->length = 0;
    output_token->value = NULL;
    if (actual_mech_type)
        *actual_mech_type = NULL;

    /* verify that the target_name is valid and usable */

    if (! kg_validate_name(target_name)) {
        /* Solaris Kerberos: spruce-up the err msg */
        krb5_principal princ = (krb5_principal) target_name;
        char *s_name = NULL;
        int kret = krb5_unparse_name(context, princ, &s_name);
        *minor_status = (OM_uint32) G_VALIDATE_FAILED;
        if (kret == 0) {
            krb5_set_error_message(context, *minor_status,
                                   dgettext(TEXT_DOMAIN,
                                            "Target name principal '%s' is invalid"),
                                   s_name);
            krb5_free_unparsed_name(context, s_name);
            save_error_info(*minor_status, context);
        }

        if (*context_handle == GSS_C_NO_CONTEXT)
            krb5_free_context(context);
        return(GSS_S_CALL_BAD_STRUCTURE|GSS_S_BAD_NAME);
    }

    /* verify the credential, or use the default */
    /*SUPPRESS 29*/
    if (claimant_cred_handle == GSS_C_NO_CREDENTIAL) {
        /*
         * Solaris Kerberos: here we are using the Solaris specific
         * function get_default_cred() to handle the special case of a
         * root principal
         */
        major_status = get_default_cred(minor_status, context,
                                        (gss_cred_id_t *)&cred);

        if (major_status && GSS_ERROR(major_status)) {
            save_error_info(*minor_status, context); /* Solaris Kerberos */
            if (*context_handle == GSS_C_NO_CONTEXT)
                krb5_free_context(context);
            return(major_status);
        }
    } else {
        major_status = krb5_gss_validate_cred(minor_status, claimant_cred_handle);
        if (GSS_ERROR(major_status)) {
            save_error_info(*minor_status, context);
            if (*context_handle == GSS_C_NO_CONTEXT)
                krb5_free_context(context);
            return(major_status);
        }
        cred = (krb5_gss_cred_id_t) claimant_cred_handle;
    }
    kerr = k5_mutex_lock(&cred->lock);
    if (kerr) {
        if (claimant_cred_handle == GSS_C_NO_CREDENTIAL)
            krb5_gss_release_cred(minor_status, (gss_cred_id_t *)&cred);
        krb5_free_context(context);
        *minor_status = kerr;
        return GSS_S_FAILURE;
    }

    /* verify the mech_type */

    err = 0;
    if (mech_type == GSS_C_NULL_OID) {
        default_mech = 1;
        if (cred->rfc_mech) {
            mech_type = (gss_OID) gss_mech_krb5;
        } else if (cred->prerfc_mech) {
            mech_type = (gss_OID) gss_mech_krb5_old;
        } else {
            err = 1;
        }
    } else if (g_OID_equal(mech_type, gss_mech_krb5)) {
        mech_type = (gss_OID) gss_mech_krb5;
        if (!cred->rfc_mech)
            err = 1;
    } else if (g_OID_equal(mech_type, gss_mech_krb5_old)) {
        mech_type = (gss_OID) gss_mech_krb5_old;
        if (!cred->prerfc_mech)
            err = 1;
    } else if (g_OID_equal(mech_type, gss_mech_krb5_wrong)) {
        mech_type = (gss_OID) gss_mech_krb5_wrong;
        if (!cred->rfc_mech)
            err = 1;
    } else {
        err = 1;
    }

    if (err) {
        k5_mutex_unlock(&cred->lock);
        if (claimant_cred_handle == GSS_C_NO_CREDENTIAL)
            krb5_gss_release_cred(minor_status, (gss_cred_id_t *)&cred);
        *minor_status = 0;
        if (*context_handle == GSS_C_NO_CONTEXT)
            krb5_free_context(context);
        return(GSS_S_BAD_MECH);
    }

    /* is this a new connection or not? */

    /*SUPPRESS 29*/
    if (*context_handle == GSS_C_NO_CONTEXT) {
        major_status = kg_new_connection(minor_status, cred, context_handle,
                                         target_name, mech_type, req_flags,
                                         time_req, input_chan_bindings,
                                         input_token, actual_mech_type,
                                         output_token, ret_flags, time_rec,
                                         context, default_mech);
        k5_mutex_unlock(&cred->lock);
        if (*context_handle == GSS_C_NO_CONTEXT) {
            save_error_info (*minor_status, context);
            krb5_free_context(context);
        } else
            ((krb5_gss_ctx_id_rec *) *context_handle)->k5_context = context;
    } else {
        /* mutual_auth doesn't care about the credentials */
        k5_mutex_unlock(&cred->lock);
        major_status = mutual_auth(minor_status, context_handle,
                                   target_name, mech_type, req_flags,
                                   time_req, input_chan_bindings,
                                   input_token, actual_mech_type,
                                   output_token, ret_flags, time_rec,
                                   context);
        /* If context_handle is now NO_CONTEXT, mutual_auth called
           delete_sec_context, which would've zapped the krb5 context
           too.  */
    }

    if (claimant_cred_handle == GSS_C_NO_CREDENTIAL)
        krb5_gss_release_cred(&tmp_min_stat, (gss_cred_id_t *)&cred);

    return(major_status);
}

#ifndef _WIN32
k5_mutex_t kg_kdc_flag_mutex = K5_MUTEX_PARTIAL_INITIALIZER;
static int kdc_flag = 0;
#endif

krb5_error_code
krb5_gss_init_context (krb5_context *ctxp)
{
    krb5_error_code err;
#ifndef _WIN32
    int is_kdc;
#endif

    err = gss_krb5int_initialize_library();
    if (err)
        return err;
#ifndef _WIN32
    err = k5_mutex_lock(&kg_kdc_flag_mutex);
    if (err)
        return err;
    is_kdc = kdc_flag;
    k5_mutex_unlock(&kg_kdc_flag_mutex);

    if (is_kdc)
        return krb5int_init_context_kdc(ctxp);
#endif

    return krb5_init_context(ctxp);
}

#ifndef _WIN32
OM_uint32
krb5int_gss_use_kdc_context(OM_uint32 *minor_status,
                            const gss_OID desired_mech,
                            const gss_OID desired_object,
                            gss_buffer_t value)
{
    OM_uint32 err;

    *minor_status = 0;

    err = gss_krb5int_initialize_library();
    if (err)
        return err;
    *minor_status = k5_mutex_lock(&kg_kdc_flag_mutex);
    if (*minor_status) {
        return GSS_S_FAILURE;
    }
    kdc_flag = 1;
    k5_mutex_unlock(&kg_kdc_flag_mutex);
    return GSS_S_COMPLETE;
}
#endif

/* Solaris Kerberos specific routines start */

#define ROOT_UID 0
#define KRB5_DEFAULT_LIFE 60*60*10
#define CACHE_FILENAME_LEN 35

extern int
safechown(const char *src, uid_t uid, gid_t gid, int mode);

/* Solaris Kerberos */
static OM_uint32
load_root_cred_using_keytab(OM_uint32 *minor_status,
                            krb5_context context,
                            const char *name,
                            krb5_int32 type)
{
    krb5_creds my_creds;
    krb5_principal me = NULL;
    krb5_principal server = NULL;
    krb5_error_code code;
    krb5_ccache ccache = NULL;
    krb5_keytab keytab = NULL;
    krb5_timestamp now;
    krb5_deltat lifetime = KRB5_DEFAULT_LIFE;   /* -l option */
    krb5_get_init_creds_opt opt;
    krb5_data tgtname = {
        0,
        KRB5_TGS_NAME_SIZE,
        KRB5_TGS_NAME
    };
    char *svcname = NULL;

    if (!name)
        return (GSS_S_FAILURE);

    memset((char *)&my_creds, 0, sizeof(my_creds));

    if (code = krb5_kt_default(context, &keytab)) {
        *minor_status = code;
        return (GSS_S_FAILURE);
    }

    if (type == KRB5_NT_SRV_HST) 
    	code = krb5_sname_to_principal(context, NULL, name, type, &me);
    else
	code = krb5_parse_name(context, name, &me);	

    if (code == 0 && krb5_is_referral_realm(&me->realm)) {
        krb5_data realm;
        code = krb5_kt_find_realm(context, keytab, me, &realm);
        if (code == 0) {
            krb5_free_data_contents(context, &me->realm);
            me->realm.length = realm.length;
            me->realm.data = realm.data;
        } else {
            /* Try to set a useful error message */
            char *princ = NULL;
            krb5_error_code ret;
            ret = krb5_unparse_name(context, me, &princ);

            krb5_set_error_message(context, code,
                                   dgettext(TEXT_DOMAIN,
                                            "Failed to find realm for %s in keytab"),
                                   ret == 0 ? princ : "unknown");
            if (princ)
                krb5_free_unparsed_name(context, princ);
        }
    }

    if (code) {
        (void) krb5_kt_close(context, keytab);
        krb5_free_principal(context, me);
        *minor_status = code;
        return (GSS_S_FAILURE);
    }

    my_creds.client = me;

    if ((code = krb5_build_principal_ext(context, &server,
                                        krb5_princ_realm(context, me)->length,
                                        krb5_princ_realm(context, me)->data,
                                        tgtname.length, tgtname.data,
                                        krb5_princ_realm(context, me)->length,
                                        krb5_princ_realm(context, me)->data,
                                        0))) {
        *minor_status = code;
        krb5_free_cred_contents(context, &my_creds);
        (void) krb5_kt_close(context, keytab);
        return (GSS_S_FAILURE);
    }

    my_creds.server = server;
    my_creds.times.starttime = 0;     /* start timer
                                       * when request
                                       * gets to KDC
                                       */
    if ((code = krb5_timeofday(context, &now))) {
        *minor_status = code;
        krb5_free_cred_contents(context, &my_creds);
        (void) krb5_kt_close(context, keytab);
        return (GSS_S_FAILURE);
    }
    my_creds.times.endtime = now + lifetime;
    my_creds.times.renew_till = 0;

    memset(&opt, 0, sizeof (opt));
    krb5_get_init_creds_opt_init(&opt);
    krb5_get_init_creds_opt_set_tkt_life(&opt, lifetime);

    code = krb5_unparse_name(context, server, &svcname);
    if (code != 0) {
        *minor_status = code;
        krb5_free_cred_contents(context, &my_creds);
        (void) krb5_kt_close(context, keytab);
        return (GSS_S_FAILURE);
    }
    /*
     * Evidently (sigh), on success, krb5_get_init_creds_keytab
     * changes the my_creds princ ptrs so we need to free those
     * princs (me&server) as well as freeing all of my_creds contents.
     */
    code = krb5_get_init_creds_keytab(context,
                                      &my_creds, me, keytab,
                                      0, svcname, &opt);

    (void) krb5_kt_close(context, keytab);

    if (svcname != NULL) {
        free(svcname);
        svcname = NULL;
    }
    if (code) {
        *minor_status = code;
        krb5_free_cred_contents(context, &my_creds);
        return (GSS_S_FAILURE);
    }

    krb5_free_principal(context, server);
    server = NULL;

    code = krb5_cc_resolve (context,
                            krb5_cc_default_name(context),
                            &ccache);
    if (code != 0) {
        *minor_status = code;
        krb5_free_cred_contents(context, &my_creds);
        return (GSS_S_FAILURE);
    }

    code = krb5_cc_initialize (context, ccache, me);
    krb5_free_principal(context, me);
    me = NULL;
    if (code != 0) {
        *minor_status = code;
        krb5_free_cred_contents(context, &my_creds);
        (void) krb5_cc_close(context, ccache);
        return (GSS_S_FAILURE);
    }

    code = krb5_cc_store_cred(context, ccache,
                              &my_creds);
    krb5_free_cred_contents(context, &my_creds);
    (void) krb5_cc_close(context, ccache);

    if (code) {
        *minor_status = code;
        return (GSS_S_FAILURE);
    }

    return (GSS_S_COMPLETE);
}

static OM_uint32
renew_ccache(OM_uint32 *minor_status, krb5_context context, uid_t uid)
{
    krb5_principal me;
    krb5_principal server;
    krb5_creds	creds;
    krb5_creds	tmpcreds;
    krb5_creds	*out_creds;
    krb5_error_code code;
    krb5_ccache ccache = NULL;
    static char ccache_name_buf[CACHE_FILENAME_LEN];
    int options = 0;
    krb5_data tgtname = {
        0,
        KRB5_TGS_NAME_SIZE,
        KRB5_TGS_NAME
    };
    gid_t gid = getgid();

    memset((char *)&creds, 0, sizeof(creds));
    memset((char *)&tmpcreds, 0, sizeof(creds));

    if ((code = krb5_cc_default(context, &ccache))) {
        *minor_status = code;
        (void) krb5_cc_close(context, ccache);
        return (GSS_S_FAILURE);
    }

    if ((code = krb5_cc_get_principal(context, ccache, &me)) != 0) {
        *minor_status = code;
        (void) krb5_cc_close(context, ccache);
        return (GSS_S_FAILURE);
    }

    creds.client = me;

    if((code = krb5_build_principal_ext(context, &server,
                                        krb5_princ_realm(context, me)->length,
                                        krb5_princ_realm(context, me)->data,
                                        tgtname.length, tgtname.data,
                                        krb5_princ_realm(context, me)->length,
                                        krb5_princ_realm(context, me)->data,
                                        0))) {
        krb5_free_principal(context, me);
        (void) krb5_cc_close(context, ccache);
        *minor_status = code;
        return (GSS_S_FAILURE);
    }

    creds.server = server;
    creds.ticket_flags = TKT_FLG_RENEWABLE;

    if ((krb5_cc_retrieve_cred(context, ccache, KRB5_TC_MATCH_FLAGS,
                               &creds, &tmpcreds))) {
        (void) krb5_cc_close(context, ccache);
        return (KDC_ERR_BADOPTION);
    }

    creds.ticket_flags = 0;
    code = krb5_get_credentials_renew(context, options, ccache,
                                      &creds, &out_creds);
    krb5_free_cred_contents(context, &creds);
    krb5_free_cred_contents(context, &tmpcreds);

    if (code) {
        *minor_status = code;
        return (GSS_S_FAILURE);
    }

    krb5_free_creds(context, out_creds);
    snprintf(ccache_name_buf, CACHE_FILENAME_LEN, "/tmp/krb5cc_%d",
             uid);
    code = safechown(ccache_name_buf, uid, gid, -1);

    if (code == -1) {
        (void) krb5_cc_destroy(context, ccache);
        *minor_status = code;
        return (GSS_S_FAILURE);
    }

    (void) krb5_cc_close(context, ccache);

    return (GSS_S_COMPLETE);
}

/* Solaris Kerberos */
#define	SAM_ACCOUNT_LEN 17 /* 15:hostname + 1:$ + 1:\0 */
krb5_error_code
get_sam_account_name(char **name)
{
    char *p, localname[SAM_ACCOUNT_LEN];

    if (*name == NULL)
	return (EINVAL);

    if (gethostname(localname, SAM_ACCOUNT_LEN) != 0)
	return (errno);

    localname[SAM_ACCOUNT_LEN - 2] = '\0';

    if ((p = strchr(localname, '.')) != NULL)
	*p = '\0';

    for (p = localname; *p; p++)
	*p = toupper(*p);

    (void) strlcat(localname, "$", SAM_ACCOUNT_LEN);

    *name = strdup(localname);
    if (*name == NULL)
	return (ENOMEM);

    return (0);
}

/*
 * Solaris Kerberos:
 * We enforce a minimum refresh time on the root cred. This avoids problems for
 * the higher level communication protocol for having valid creds and
 * setting up a valid context, only to have it expire before or while
 * it is being used. For non root users we don't care since we do not refresh
 * there creds, they get what they can get.
 */
#define MIN_REFRESH_TIME 300
#define MIN_RENEW_TIME 1500

/* get_default_cred() must be called with the krb5_mutex lock held */
static OM_uint32
get_default_cred(OM_uint32 *minor_status, void *ct, gss_cred_id_t *cred_handle)
{
    krb5_timestamp now;
    krb5_gss_cred_id_t cred;
    OM_uint32 major;
    OM_uint32 mntmp;
    /*
     * Solaris Kerberos
     * Use krb5_getuid() to select the mechanism to obtain the uid.
     */
    uid_t uid = krb5_getuid();
    krb5_context context = (krb5_context)ct;

    /* Get the default cred for user */
    if (((major = kg_get_defcred(minor_status, cred_handle)) != NULL) &&
        GSS_ERROR(major)) {

        /* If we're not root we're done */
        if (uid != ROOT_UID)
            return (major);

        /*
         * Try and get root's cred in the cache using keytab.
         *
         * First try "root", then try "host", and finally try the Security
	 * Account Manager (SAM) for MS AD interop.
         */
        major = load_root_cred_using_keytab(minor_status,
                                            context, "root", KRB5_NT_SRV_HST);

        if (major != GSS_S_COMPLETE)
            major = load_root_cred_using_keytab(minor_status,
                                                context, "host",
						KRB5_NT_SRV_HST);

	if (major != GSS_S_COMPLETE) {
	    char *name;
	    krb5_error_code code;

	    code = get_sam_account_name(&name);
	    if (code == 0) {
		major = load_root_cred_using_keytab(minor_status,
						    context, name,
						    KRB5_NT_PRINCIPAL);
		free(name);
	    }
	}

        if (major != GSS_S_COMPLETE)
            return (major);

        /* We should have valid tgt now in the cache, so get it. */
        major = kg_get_defcred(minor_status, cred_handle);

        return (major);
    }

    /* We've got a gss cred handle that is a kerberos cred handle. */
    cred = (krb5_gss_cred_id_t)*cred_handle;

    /* If we can't get the time, assume the worst. */
    if (krb5_timeofday(context, &now)) {
        (void) krb5_gss_release_cred(&mntmp, cred_handle);
        return (GSS_S_CREDENTIALS_EXPIRED);
    }

    /* If root's cred has expired re-get it */
    if (cred->tgt_expire < now + MIN_REFRESH_TIME && uid == ROOT_UID) {
        (void) krb5_gss_release_cred(&mntmp, cred_handle);

        major = load_root_cred_using_keytab(minor_status,
                                            context, "root", KRB5_NT_SRV_HST);

        if (major != GSS_S_COMPLETE)
            major = load_root_cred_using_keytab(minor_status,
                                                context, "host",
						KRB5_NT_SRV_HST);

        if (major != GSS_S_COMPLETE)
            return (major);

        major = kg_get_defcred(minor_status, cred_handle);
        if (major != GSS_S_COMPLETE)
            return (major);

        /* Any body else is SOL unless we can renew their credential cache */
    } else if ((cred->tgt_expire < now + MIN_RENEW_TIME) &&
               (cred->tgt_expire > now)) {
        (void) krb5_gss_release_cred(&mntmp, cred_handle);

        major = renew_ccache(minor_status, context, uid);
        if ((major != GSS_S_COMPLETE) &&
            (major != KDC_ERR_BADOPTION))
            return (major);

        major = kg_get_defcred(minor_status, cred_handle);
        if (major != GSS_S_COMPLETE)
            return (major);

    }

    /* Otherwise we got non expired creds */
    return (GSS_S_COMPLETE);
}

/* Solaris Kerberos specific routines end */
