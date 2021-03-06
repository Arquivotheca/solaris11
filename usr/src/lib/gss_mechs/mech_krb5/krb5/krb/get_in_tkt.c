/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * lib/krb5/krb/get_in_tkt.c
 *
 * Copyright 1990,1991, 2003, 2008 by the Massachusetts Institute of Technology.
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
 *
 * krb5_get_in_tkt()
 */

#include <string.h>

#include "k5-int.h"
#include "int-proto.h"
#include "os-proto.h"
#include "fast.h"
#include "init_creds_ctx.h"
/* Solaris Kerberos begin */
#include <ctype.h>
#include <locale.h>
#include "kerberos_dtrace.h"
/* Solaris Kerberos end */

#if APPLE_PKINIT
#define     IN_TKT_DEBUG    0
#if         IN_TKT_DEBUG
#define     inTktDebug(args...)       printf(args)
#else
#define     inTktDebug(args...)
#endif
#endif /* APPLE_PKINIT */

/*
  All-purpose initial ticket routine, usually called via
  krb5_get_in_tkt_with_password or krb5_get_in_tkt_with_skey.

  Attempts to get an initial ticket for creds->client to use server
  creds->server, (realm is taken from creds->client), with options
  options, and using creds->times.starttime, creds->times.endtime,
  creds->times.renew_till as from, till, and rtime.
  creds->times.renew_till is ignored unless the RENEWABLE option is requested.

  key_proc is called to fill in the key to be used for decryption.
  keyseed is passed on to key_proc.

  decrypt_proc is called to perform the decryption of the response (the
  encrypted part is in dec_rep->enc_part; the decrypted part should be
  allocated and filled into dec_rep->enc_part2
  arg is passed on to decrypt_proc.

  If addrs is non-NULL, it is used for the addresses requested.  If it is
  null, the system standard addresses are used.

  A succesful call will place the ticket in the credentials cache ccache
  and fill in creds with the ticket information used/returned..

  returns system errors, encryption errors

*/

/* Solaris Kerberos */
#define	max(a, b) ((a) > (b) ? (a) : (b))

/* some typedef's for the function args to make things look a bit cleaner */

typedef krb5_error_code (*git_key_proc) (krb5_context,
                                         krb5_enctype,
                                         krb5_data *,
                                         krb5_const_pointer,
                                         krb5_keyblock **);

typedef krb5_error_code (*git_decrypt_proc) (krb5_context,
                                             const krb5_keyblock *,
                                             krb5_const_pointer,
                                             krb5_kdc_rep * );

static krb5_error_code make_preauth_list (krb5_context,
                                          krb5_preauthtype *,
                                          int, krb5_pa_data ***);
static krb5_error_code sort_krb5_padata_sequence(krb5_context context,
                                                 krb5_data *realm,
                                                 krb5_pa_data **padata);

/*
 * This function performs 32 bit bounded addition so we can generate
 * lifetimes without overflowing krb5_int32
 */
static krb5_int32
krb5int_addint32 (krb5_int32 x, krb5_int32 y)
{
    if ((x > 0) && (y > (KRB5_INT32_MAX - x))) {
        /* sum will be be greater than KRB5_INT32_MAX */
        return KRB5_INT32_MAX;
    } else if ((x < 0) && (y < (KRB5_INT32_MIN - x))) {
        /* sum will be less than KRB5_INT32_MIN */
        return KRB5_INT32_MIN;
    }

    return x + y;
}

#if APPLE_PKINIT
/*
 * Common code to generate krb5_kdc_req.nonce. Like the original MIT code this
 * just uses krb5_timeofday(); it should use a PRNG. Even more unfortunately this
 * value is used interchangeably with an explicit now_time throughout this module...
 */
static krb5_error_code
gen_nonce(krb5_context  context,
          krb5_int32    *nonce)
{
    krb5_int32 time_now;
    krb5_error_code retval = krb5_timeofday(context, &time_now);
    if(retval) {
        return retval;
    }
    *nonce = time_now;
    return 0;
}
#endif /* APPLE_PKINIT */

/*
 * This function sends a request to the KDC, and gets back a response;
 * the response is parsed into ret_err_reply or ret_as_reply if the
 * reponse is a KRB_ERROR or a KRB_AS_REP packet.  If it is some other
 * unexpected response, an error is returned.
 */

/*
 * Solaris Kerberos:
 * Modified to return the raw reply ("rep") so it can be used for the Kerberos
 * DTrace probes. "rep->data" must be freed by the caller.
 */
static krb5_error_code
send_as_request(krb5_context            context,
                krb5_data               *packet, 
                krb5_kdc_req            *request,
                const krb5_data         *realm,
                krb5_error **           ret_err_reply,
                krb5_kdc_rep **         ret_as_reply,
                int                     *use_master)
{
    krb5_kdc_rep *as_reply = 0;
    krb5_error_code retval;
    krb5_data reply;
    char k4_version;            /* same type as *(krb5_data::data) */
    int tcp_only = 0;

    reply.data = 0;

    /* set the nonce if the caller expects us to do it */

    k4_version = packet->data[0];
send_again:
    /*
     * Solaris Kerberos
     * krb5_sendto_kdc takes an extra parameter - "request" which is the
     * krb5_kdc_req encoded in "packet".
     */
    retval = krb5_sendto_kdc(context, packet, request,
                             realm,
                             &reply, use_master, tcp_only, NULL);
#if APPLE_PKINIT
    inTktDebug("krb5_sendto_kdc returned %d\n", (int)retval);
#endif /* APPLE_PKINIT */

    if (retval)
        goto cleanup;

    /* now decode the reply...could be error or as_rep */
    if (krb5_is_krb_error(&reply)) {
        krb5_error *err_reply;

        if ((retval = decode_krb5_error(&reply, &err_reply)))
            /* some other error code--??? */
            goto cleanup;

        if (ret_err_reply) {
            if (err_reply->error == KRB_ERR_RESPONSE_TOO_BIG
                && tcp_only == 0) {
                tcp_only = 1;
                krb5_free_error(context, err_reply);
                free(reply.data);
                reply.data = 0;
                /* Solaris Kerberos */
                reply.length = 0;
                goto send_again;
            }
            *ret_err_reply = err_reply;
        } else {
            krb5_free_error(context, err_reply);
            err_reply = NULL; /* Solaris Kerberos */
        }
        goto cleanup;
    }

    /*
     * Check to make sure it isn't a V4 reply.
     */
    if (!krb5_is_as_rep(&reply)) {
/* these are in <kerberosIV/prot.h> as well but it isn't worth including. */
#define V4_KRB_PROT_VERSION     4
#define V4_AUTH_MSG_ERR_REPLY   (5<<1)
        /* check here for V4 reply */
        unsigned int t_switch;

        /* From v4 g_in_tkt.c: This used to be
           switch (pkt_msg_type(rpkt) & ~1) {
           but SCO 3.2v4 cc compiled that incorrectly.  */
        t_switch = reply.data[1];
        t_switch &= ~1;

        if (t_switch == V4_AUTH_MSG_ERR_REPLY
            && (reply.data[0] == V4_KRB_PROT_VERSION
                || reply.data[0] == k4_version)) {
            retval = KRB5KRB_AP_ERR_V4_REPLY;
        } else {
            retval = KRB5KRB_AP_ERR_MSG_TYPE;
        }
        goto cleanup;
    }

    /* It must be a KRB_AS_REP message, or an bad returned packet */
    if ((retval = decode_krb5_as_rep(&reply, &as_reply)))
        /* some other error code ??? */
        goto cleanup;

    if (as_reply->msg_type != KRB5_AS_REP) {
        retval = KRB5KRB_AP_ERR_MSG_TYPE;
        krb5_free_kdc_rep(context, as_reply);
        goto cleanup;
    }

    if (ret_as_reply)
        *ret_as_reply = as_reply;
    else
        krb5_free_kdc_rep(context, as_reply);

cleanup:
    if (reply.data) {
        free(reply.data);
        /* Solaris Kerberos just to be safe */
        reply.data = NULL;
        reply.length = 0;
    }
out: /* Solaris Kerberos for dtrace */
    return retval;
}

static krb5_error_code
decrypt_as_reply(krb5_context           context,
                 krb5_kdc_req           *request,
                 krb5_kdc_rep           *as_reply,
                 git_key_proc           key_proc,
                 krb5_const_pointer     keyseed,
                 krb5_keyblock *        key,
                 git_decrypt_proc       decrypt_proc,
                 krb5_const_pointer     decryptarg)
{
    krb5_error_code             retval;
    krb5_keyblock *             decrypt_key = 0;
    krb5_data                   salt;

    if (as_reply->enc_part2)
        return 0;

    if (key)
        decrypt_key = key;
    else {
        /*
         * Use salt corresponding to the client principal supplied by
         * the KDC, which may differ from the requested principal if
         * canonicalization is in effect.  We will check
         * as_reply->client later in verify_as_reply.
         */
        if ((retval = krb5_principal2salt(context, as_reply->client, &salt)))
            return(retval);

        retval = (*key_proc)(context, as_reply->enc_part.enctype,
                             &salt, keyseed, &decrypt_key);
        free(salt.data);
        if (retval)
            goto cleanup;
    }

    if ((retval = (*decrypt_proc)(context, decrypt_key, decryptarg, as_reply)))
        goto cleanup;

cleanup:
    if (!key && decrypt_key)
        krb5_free_keyblock(context, decrypt_key);
    return (retval);
}

/**
 * Fully anonymous replies include a pa_pkinit_kx padata type including the KDC
 * contribution key.  This routine confirms that the session key is of the
 * right form for fully anonymous requests.  It is here rather than in the
 * preauth code because the session key cannot be verified until the AS reply
 * is decrypted and the preauth code all runs before the AS reply is decrypted.
 */
static krb5_error_code
verify_anonymous( krb5_context context, krb5_kdc_req *request,
                  krb5_kdc_rep *reply, krb5_keyblock *as_key)
{
    krb5_error_code ret = 0;
    krb5_pa_data *pa;
    krb5_data scratch;
    krb5_keyblock *kdc_key = NULL, *expected = NULL;
    krb5_enc_data *enc = NULL;
    krb5_keyblock *session = reply->enc_part2->session;

    if (!krb5_principal_compare_any_realm(context, request->client,
                                          krb5_anonymous_principal()))
        return 0; /* Only applies to fully anonymous */
    pa = krb5int_find_pa_data(context, reply->padata, KRB5_PADATA_PKINIT_KX);
    if (pa == NULL)
        goto verification_error;
    scratch.length = pa->length;
    scratch.data = (char *) pa->contents;
    ret = decode_krb5_enc_data( &scratch, &enc);
    if (ret)
        goto cleanup;
    scratch.data = k5alloc(enc->ciphertext.length, &ret);
    if (ret)
        goto cleanup;
    scratch.length = enc->ciphertext.length;
    ret = krb5_c_decrypt(context, as_key, KRB5_KEYUSAGE_PA_PKINIT_KX,
                         NULL /*cipherstate*/, enc, &scratch);
    if (ret) {
        free(scratch.data);
        goto cleanup;
    }
    ret = decode_krb5_encryption_key( &scratch, &kdc_key);
    zap(scratch.data, scratch.length);
    free(scratch.data);
    if (ret)
        goto cleanup;
    ret = krb5_c_fx_cf2_simple(context, kdc_key, "PKINIT",
                               as_key, "KEYEXCHANGE", &expected);
    if (ret)
        goto cleanup;
    if ((expected->enctype != session->enctype) ||
        (expected->length != session->length) ||
        (memcmp(expected->contents, session->contents, expected->length) != 0))
        goto verification_error;
cleanup:
    if (kdc_key)
        krb5_free_keyblock(context, kdc_key);
    if (expected)
        krb5_free_keyblock(context, expected);
    if (enc)
        krb5_free_enc_data(context, enc);
    return ret;
verification_error:
    ret = KRB5_KDCREP_MODIFIED;
    krb5_set_error_message(context, ret, "Reply has wrong form of session key "
                           "for anonymous request");
    goto cleanup;
}

static krb5_error_code
verify_as_reply(krb5_context            context,
                krb5_timestamp          time_now,
                krb5_kdc_req            *request,
                krb5_kdc_rep            *as_reply)
{
    krb5_error_code             retval;
    int                         canon_req;
    int                         canon_ok;

    /* check the contents for sanity: */
    if (!as_reply->enc_part2->times.starttime)
        as_reply->enc_part2->times.starttime =
            as_reply->enc_part2->times.authtime;

    /*
     * We only allow the AS-REP server name to be changed if the
     * caller set the canonicalize flag (or requested an enterprise
     * principal) and we requested (and received) a TGT.
     */
    canon_req = ((request->kdc_options & KDC_OPT_CANONICALIZE) != 0) ||
        (krb5_princ_type(context, request->client) ==
         KRB5_NT_ENTERPRISE_PRINCIPAL) ||
        (request->kdc_options & KDC_OPT_REQUEST_ANONYMOUS);
    if (canon_req) {
        canon_ok = IS_TGS_PRINC(context, request->server) &&
            IS_TGS_PRINC(context, as_reply->enc_part2->server);
        if (!canon_ok && (request->kdc_options & KDC_OPT_REQUEST_ANONYMOUS)) {
            canon_ok = krb5_principal_compare_any_realm(context,
                                                        as_reply->client,
                                                        krb5_anonymous_principal());
        }
    } else
        canon_ok = 0;

    if ((!canon_ok &&
         (!krb5_principal_compare(context, as_reply->client, request->client) ||
          !krb5_principal_compare(context, as_reply->enc_part2->server, request->server)))
        || !krb5_principal_compare(context, as_reply->enc_part2->server, as_reply->ticket->server)
        || (request->nonce != as_reply->enc_part2->nonce)
        /* XXX check for extraneous flags */
        /* XXX || (!krb5_addresses_compare(context, addrs, as_reply->enc_part2->caddrs)) */
        || ((request->kdc_options & KDC_OPT_POSTDATED) &&
            (request->from != 0) &&
            (request->from != as_reply->enc_part2->times.starttime))
        || ((request->till != 0) &&
            (as_reply->enc_part2->times.endtime > request->till))
        || ((request->kdc_options & KDC_OPT_RENEWABLE) &&
            /*
             * Solaris Kerberos: Here we error only if renewable_ok was not set.
             */
            !(request->kdc_options & KDC_OPT_RENEWABLE_OK) &&
            (as_reply->enc_part2->flags & KDC_OPT_RENEWABLE) &&
            (request->rtime != 0) &&
            (as_reply->enc_part2->times.renew_till > request->rtime))
        || ((request->kdc_options & KDC_OPT_RENEWABLE_OK) &&
            !(request->kdc_options & KDC_OPT_RENEWABLE) &&
            (as_reply->enc_part2->flags & KDC_OPT_RENEWABLE) &&
            (request->till != 0) &&
            (as_reply->enc_part2->times.renew_till > request->till))
            /*
             * Solaris Kerberos: renew_till should never be greater than till or
             * rtime.
             */
        || ((request->kdc_options & KDC_OPT_RENEWABLE_OK) &&
            (as_reply->enc_part2->flags & KDC_OPT_RENEWABLE) &&
            (request->till != 0) &&
            (request->rtime != 0) &&
            (as_reply->enc_part2->times.renew_till > max(request->till,
            request->rtime)))
    ) {
#if APPLE_PKINIT
        inTktDebug("verify_as_reply: KDCREP_MODIFIED\n");
#if IN_TKT_DEBUG
        if(request->client->realm.length && request->client->data->length)
            inTktDebug("request: name %s realm %s\n",
                       request->client->realm.data, request->client->data->data);
        if(as_reply->client->realm.length && as_reply->client->data->length)
            inTktDebug("reply  : name %s realm %s\n",
                       as_reply->client->realm.data, as_reply->client->data->data);
#endif
#endif /* APPLE_PKINIT */
        return KRB5_KDCREP_MODIFIED;
    }

    if (context->library_options & KRB5_LIBOPT_SYNC_KDCTIME) {
        retval = krb5_set_real_time(context,
                                    as_reply->enc_part2->times.authtime, -1);
        if (retval)
            return retval;
    } else {
        if ((request->from == 0) &&
            (labs(as_reply->enc_part2->times.starttime - time_now)
             > context->clockskew))
            return (KRB5_KDCREP_SKEW);
    }
    return 0;
}

static krb5_error_code
stash_as_reply(krb5_context             context,
               krb5_timestamp           time_now,
               krb5_kdc_req             *request,
               krb5_kdc_rep             *as_reply,
               krb5_creds *             creds,
               krb5_ccache              ccache)
{
    krb5_error_code             retval;
    krb5_data *                 packet;
    krb5_principal              client;
    krb5_principal              server;

    client = NULL;
    server = NULL;

    if (!creds->client)
        if ((retval = krb5_copy_principal(context, as_reply->client, &client)))
            goto cleanup;

    if (!creds->server)
        if ((retval = krb5_copy_principal(context, as_reply->enc_part2->server,
                                          &server)))
            goto cleanup;

    /* fill in the credentials */
    if ((retval = krb5_copy_keyblock_contents(context,
                                              as_reply->enc_part2->session,
                                              &creds->keyblock)))
        goto cleanup;

    creds->times = as_reply->enc_part2->times;
    creds->is_skey = FALSE;             /* this is an AS_REQ, so cannot
                                           be encrypted in skey */
    creds->ticket_flags = as_reply->enc_part2->flags;
    if ((retval = krb5_copy_addresses(context, as_reply->enc_part2->caddrs,
                                      &creds->addresses)))
        goto cleanup;

    creds->second_ticket.length = 0;
    creds->second_ticket.data = 0;

    if ((retval = encode_krb5_ticket(as_reply->ticket, &packet)))
        goto cleanup;

    creds->ticket = *packet;
    free(packet);

    /* store it in the ccache! */
    if (ccache)
        if ((retval = krb5_cc_store_cred(context, ccache, creds)))
            goto cleanup;

    if (!creds->client)
        creds->client = client;
    if (!creds->server)
        creds->server = server;

cleanup:
    if (retval) {
        if (client)
            krb5_free_principal(context, client);
        if (server)
            krb5_free_principal(context, server);
        if (creds->keyblock.contents) {
            memset(creds->keyblock.contents, 0,
                   creds->keyblock.length);
            free(creds->keyblock.contents);
            creds->keyblock.contents = 0;
            creds->keyblock.length = 0;
        }
        if (creds->ticket.data) {
            free(creds->ticket.data);
            creds->ticket.data = 0;
        }
        if (creds->addresses) {
            krb5_free_addresses(context, creds->addresses);
            creds->addresses = 0;
        }
    }
    return (retval);
}

static krb5_error_code
make_preauth_list(krb5_context  context,
                  krb5_preauthtype *    ptypes,
                  int                   nptypes,
                  krb5_pa_data ***      ret_list)
{
    krb5_preauthtype *          ptypep;
    krb5_pa_data **             preauthp;
    int                         i;

    if (nptypes < 0) {
        for (nptypes=0, ptypep = ptypes; *ptypep; ptypep++, nptypes++)
            ;
    }

    /* allocate space for a NULL to terminate the list */

    if ((preauthp =
         (krb5_pa_data **) malloc((nptypes+1)*sizeof(krb5_pa_data *))) == NULL)
        return(ENOMEM);

    for (i=0; i<nptypes; i++) {
        if ((preauthp[i] =
             (krb5_pa_data *) malloc(sizeof(krb5_pa_data))) == NULL) {
            for (; i>=0; i--)
                free(preauthp[i]);
            free(preauthp);
            return (ENOMEM);
        }
        preauthp[i]->magic = KV5M_PA_DATA;
        preauthp[i]->pa_type = ptypes[i];
        preauthp[i]->length = 0;
        preauthp[i]->contents = 0;
    }

    /* fill in the terminating NULL */

    preauthp[nptypes] = NULL;

    *ret_list = preauthp;
    return 0;
}

#define MAX_IN_TKT_LOOPS 16
static const krb5_enctype get_in_tkt_enctypes[] = {
    ENCTYPE_DES3_CBC_SHA1,
    ENCTYPE_ARCFOUR_HMAC,
    ENCTYPE_DES_CBC_MD5,
    ENCTYPE_DES_CBC_MD4,
    ENCTYPE_DES_CBC_CRC,
    0
};

static krb5_error_code
rewrite_server_realm(krb5_context context,
                     krb5_const_principal old_server,
                     const krb5_data *realm,
                     krb5_boolean tgs,
                     krb5_principal *server)
{
    krb5_error_code retval;

    assert(*server == NULL);

    retval = krb5_copy_principal(context, old_server, server);
    if (retval)
        return retval;

    krb5_free_data_contents(context, &(*server)->realm);
    (*server)->realm.data = NULL;

    retval = krb5int_copy_data_contents(context, realm, &(*server)->realm);
    if (retval)
        goto cleanup;

    if (tgs) {
        krb5_free_data_contents(context, &(*server)->data[1]);
        (*server)->data[1].data = NULL;

        retval = krb5int_copy_data_contents(context, realm, &(*server)->data[1]);
        if (retval)
            goto cleanup;
    }

cleanup:
    if (retval) {
        krb5_free_principal(context, *server);
        *server = NULL;
    }

    return retval;
}

static inline int
tgt_is_local_realm(krb5_creds *tgt)
{
    return (tgt->server->length == 2
            && data_eq_string(tgt->server->data[0], KRB5_TGS_NAME)
            && data_eq(tgt->server->data[1], tgt->client->realm)
            && data_eq(tgt->server->realm, tgt->client->realm));
}

static krb5_error_code
request_enc_pa_rep(krb5_pa_data ***padptr)
{
    size_t size = 0;
    krb5_pa_data **pad = *padptr;
    krb5_pa_data *pa= NULL;
    if (pad)
        for (size=0; pad[size]; size++);
    pad = realloc(pad, sizeof(*pad)*(size+2));

    if (pad == NULL)
        return ENOMEM;
    pad[size+1] = NULL;
    pa = malloc(sizeof(krb5_pa_data));
    if (pa == NULL)
        return ENOMEM;
    pa->contents = NULL;
    pa->length = 0;
    pa->pa_type = KRB5_ENCPADATA_REQ_ENC_PA_REP;
    pad[size] = pa;
    *padptr = pad;
    return 0;
}

krb5_error_code KRB5_CALLCONV
krb5_get_in_tkt(krb5_context context,
                krb5_flags options,
                krb5_address * const * addrs,
                krb5_enctype * ktypes,
                krb5_preauthtype * ptypes,
                git_key_proc key_proc,
                krb5_const_pointer keyseed,
                git_decrypt_proc decrypt_proc,
                krb5_const_pointer decryptarg,
                krb5_creds * creds,
                krb5_ccache ccache,
                krb5_kdc_rep ** ret_as_reply)
{
    krb5_error_code     retval;
    krb5_timestamp      time_now;
    krb5_keyblock *     decrypt_key = 0;
    krb5_kdc_req        request;
    krb5_data *encoded_request;
    krb5_error *        err_reply;
    krb5_kdc_rep *      as_reply = 0;
    krb5_pa_data  **    preauth_to_use = 0;
    int                 loopcount = 0;
    krb5_int32          do_more = 0;
    int                 canon_flag;
    int             use_master = 0;
    int                 referral_count = 0;
    krb5_principal_data referred_client;
    krb5_principal      referred_server = NULL;
    krb5_boolean        is_tgt_req;

#if APPLE_PKINIT
    inTktDebug("krb5_get_in_tkt top\n");
#endif /* APPLE_PKINIT */

    if (! krb5_realm_compare(context, creds->client, creds->server)) {
        /* Solaris Kerberos better errors */
        char *s_name = NULL;
        char *c_name = NULL;
        krb5_error_code serr, cerr;
        serr = krb5_unparse_name(context, creds->server, &s_name);
        cerr = krb5_unparse_name(context, creds->client, &c_name);
        krb5_set_error_message(context, KRB5_IN_TKT_REALM_MISMATCH,
                               dgettext(TEXT_DOMAIN,
                                        "Client/server realm mismatch in "
                                        "initial ticket request: '%s' "
                                        "requesting ticket '%s'"),
                               cerr ? "unknown" : c_name,
                               serr ? "unknown" : s_name);
        if (s_name)
            krb5_free_unparsed_name(context, s_name);
        if (c_name)
            krb5_free_unparsed_name(context, c_name);
        return KRB5_IN_TKT_REALM_MISMATCH;
    }

    if (ret_as_reply)
        *ret_as_reply = 0;

    referred_client = *(creds->client);
    referred_client.realm.data = NULL;
    referred_client.realm.length = 0;

    /* per referrals draft, enterprise principals imply canonicalization */
    canon_flag = ((options & KDC_OPT_CANONICALIZE) != 0) ||
        creds->client->type == KRB5_NT_ENTERPRISE_PRINCIPAL;

    /*
     * Set up the basic request structure
     */
    request.magic = KV5M_KDC_REQ;
    request.msg_type = KRB5_AS_REQ;
    request.addresses = 0;
    request.ktype = 0;
    request.padata = 0;
    if (addrs)
        request.addresses = (krb5_address **) addrs;
    else
        if ((retval = krb5_os_localaddr(context, &request.addresses)))
            goto cleanup;
    request.kdc_options = options;
    request.client = creds->client;
    request.server = creds->server;
    request.nonce = 0;
    request.from = creds->times.starttime;
    request.till = creds->times.endtime;
    request.rtime = creds->times.renew_till;
#if APPLE_PKINIT
    retval = gen_nonce(context, (krb5_int32 *)&time_now);
    if(retval) {
        goto cleanup;
    }
    request.nonce = time_now;
#endif /* APPLE_PKINIT */

    retval = krb5int_copy_etypes(get_in_tkt_enctypes, &request.ktype);
    request.nktypes = krb5int_count_etypes(request.ktype);
    if (ktypes) {
        int i, req, next = 0;
        for (req = 0; ktypes[req]; req++) {
            if (ktypes[req] == request.ktype[next]) {
                next++;
                continue;
            }
            for (i = next + 1; i < request.nktypes; i++)
                if (ktypes[req] == request.ktype[i]) {
                    /* Found the enctype we want, but not in the
                       position we want.  Move it, but keep the old
                       one from the desired slot around in case it's
                       later in our requested-ktypes list.  */
                    krb5_enctype t;
                    t = request.ktype[next];
                    request.ktype[next] = request.ktype[i];
                    request.ktype[i] = t;
                    next++;
                    break;
                }
            /* If we didn't find it, don't do anything special, just
               drop it.  */
        }
        request.ktype[next] = 0;
        request.nktypes = next;
    }
    request.authorization_data.ciphertext.length = 0;
    request.authorization_data.ciphertext.data = 0;
    request.unenc_authdata = 0;
    request.second_ticket = 0;

    /*
     * If a list of preauth types are passed in, convert it to a
     * preauth_to_use list.
     */
    if (ptypes) {
        retval = make_preauth_list(context, ptypes, -1, &preauth_to_use);
        if (retval)
            goto cleanup;
    }

    is_tgt_req = tgt_is_local_realm(creds);

    while (1) {
        if (loopcount++ > MAX_IN_TKT_LOOPS) {
            retval = KRB5_GET_IN_TKT_LOOP;
            /* Solaris Kerberos better errors */
            {
                char *s_name = NULL;
                char *c_name = NULL;
                krb5_error_code serr, cerr;
                serr = krb5_unparse_name(context, creds->server, &s_name);
                cerr = krb5_unparse_name(context, creds->client, &c_name);
                krb5_set_error_message(context, retval,
                                       dgettext(TEXT_DOMAIN,
                                                "Looping detected getting "
                                                "ticket: '%s' requesting "
                                                "ticket '%s'. Max loops is "
                                                "%d.  Make sure a KDC is "
                                                "available"),
                                       cerr ? "unknown" : c_name,
                                       serr ? "unknown" : s_name,
                                       MAX_IN_TKT_LOOPS);
                if (s_name)
                    krb5_free_unparsed_name(context, s_name);
                if (c_name)
                    krb5_free_unparsed_name(context, c_name);
            }
            goto cleanup;
        }

#if APPLE_PKINIT
        inTktDebug("krb5_get_in_tkt calling krb5_obtain_padata\n");
#endif /* APPLE_PKINIT */
        if ((retval = krb5_obtain_padata(context, preauth_to_use, key_proc,
                                         keyseed, creds, &request)) != 0)
            goto cleanup;
        if (preauth_to_use)
            krb5_free_pa_data(context, preauth_to_use);
        preauth_to_use = 0;

        err_reply = 0;
        as_reply = 0;

        if ((retval = krb5_timeofday(context, &time_now)))
            goto cleanup;

        /*
         * XXX we know they are the same size... and we should do
         * something better than just the current time
         */
        request.nonce = (krb5_int32) time_now;

        if ((retval = encode_krb5_as_req(&request, &encoded_request)) != 0)
            goto cleanup;
        retval = send_as_request(context, encoded_request, &request,
                                 krb5_princ_realm(context, request.client), &err_reply,
                                 &as_reply, &use_master);
        krb5_free_data(context, encoded_request);
        if (retval != 0)
            goto cleanup;

        if (err_reply) {
            if (err_reply->error == KDC_ERR_PREAUTH_REQUIRED &&
                err_reply->e_data.length > 0) {
                retval = decode_krb5_padata_sequence(&err_reply->e_data,
                                                     &preauth_to_use);
                krb5_free_error(context, err_reply);
                if (retval)
                    goto cleanup;
                retval = sort_krb5_padata_sequence(context,
                                                   &request.server->realm,
                                                   preauth_to_use);
                if (retval)
                    goto cleanup;
                continue;
            } else if (canon_flag && err_reply->error == KDC_ERR_WRONG_REALM) {
                if (++referral_count > KRB5_REFERRAL_MAXHOPS ||
                    err_reply->client == NULL ||
                    err_reply->client->realm.length == 0) {
                    retval = KRB5KDC_ERR_WRONG_REALM;
                    krb5_free_error(context, err_reply);
                    goto cleanup;
                }
                /* Rewrite request.client with realm from error reply */
                if (referred_client.realm.data) {
                    krb5_free_data_contents(context, &referred_client.realm);
                    referred_client.realm.data = NULL;
                }
                retval = krb5int_copy_data_contents(context,
                                                    &err_reply->client->realm,
                                                    &referred_client.realm);
                krb5_free_error(context, err_reply);
                if (retval)
                    goto cleanup;
                request.client = &referred_client;

                if (referred_server != NULL) {
                    krb5_free_principal(context, referred_server);
                    referred_server = NULL;
                }

                retval = rewrite_server_realm(context,
                                              creds->server,
                                              &referred_client.realm,
                                              is_tgt_req,
                                              &referred_server);
                if (retval)
                    goto cleanup;
                request.server = referred_server;

                continue;
            } else {
                retval = (krb5_error_code) err_reply->error
                    + ERROR_TABLE_BASE_krb5;
                krb5_free_error(context, err_reply);
                goto cleanup;
            }
        } else if (!as_reply) {
            retval = KRB5KRB_AP_ERR_MSG_TYPE;
            goto cleanup;
        }
        if ((retval = krb5_process_padata(context, &request, as_reply,
                                          key_proc, keyseed, decrypt_proc,
                                          &decrypt_key, creds,
                                          &do_more)) != 0)
            goto cleanup;

        if (!do_more)
            break;
    }

    if ((retval = decrypt_as_reply(context, &request, as_reply, key_proc,
                                   keyseed, decrypt_key, decrypt_proc,
                                   decryptarg)))
        goto cleanup;

    if ((retval = verify_as_reply(context, time_now, &request, as_reply)))
        goto cleanup;

    if ((retval = stash_as_reply(context, time_now, &request, as_reply,
                                 creds, ccache)))
        goto cleanup;

cleanup:
    if (request.ktype)
        free(request.ktype);
    if (!addrs && request.addresses)
        krb5_free_addresses(context, request.addresses);
    if (request.padata)
        krb5_free_pa_data(context, request.padata);
    if (preauth_to_use)
        krb5_free_pa_data(context, preauth_to_use);
    if (decrypt_key)
        krb5_free_keyblock(context, decrypt_key);
    if (as_reply) {
        if (ret_as_reply)
            *ret_as_reply = as_reply;
        else
            krb5_free_kdc_rep(context, as_reply);
    }
    if (referred_client.realm.data)
        krb5_free_data_contents(context, &referred_client.realm);
    if (referred_server)
        krb5_free_principal(context, referred_server);
    return (retval);
}

/* Sort a pa_data sequence so that types named in the "preferred_preauth_types"
 * libdefaults entry are listed before any others. */
static krb5_error_code
sort_krb5_padata_sequence(krb5_context context, krb5_data *realm,
                          krb5_pa_data **padata)
{
    int i, j, base;
    krb5_error_code ret;
    const char *p;
    long l;
    char *q, *preauth_types = NULL;
    krb5_pa_data *tmp;
    int need_free_string = 1;

    if ((padata == NULL) || (padata[0] == NULL)) {
        return 0;
    }

    ret = krb5int_libdefault_string(context, realm, KRB5_CONF_PREFERRED_PREAUTH_TYPES,
                                    &preauth_types);
    if ((ret != 0) || (preauth_types == NULL)) {
        /* Try to use PKINIT first. */
        preauth_types = "17, 16, 15, 14";
        need_free_string = 0;
    }

#ifdef DEBUG
    fprintf (stderr, "preauth data types before sorting:");
    for (i = 0; padata[i]; i++) {
        fprintf (stderr, " %d", padata[i]->pa_type);
    }
    fprintf (stderr, "\n");
#endif

    base = 0;
    for (p = preauth_types; *p != '\0';) {
        /* skip whitespace to find an entry */
        p += strspn(p, ", ");
        if (*p != '\0') {
            /* see if we can extract a number */
            l = strtol(p, &q, 10);
            if ((q != NULL) && (q > p)) {
                /* got a valid number; search for a matchin entry */
                for (i = base; padata[i] != NULL; i++) {
                    /* bubble the matching entry to the front of the list */
                    if (padata[i]->pa_type == l) {
                        tmp = padata[i];
                        for (j = i; j > base; j--)
                            padata[j] = padata[j - 1];
                        padata[base] = tmp;
                        base++;
                        break;
                    }
                }
                p = q;
            } else {
                break;
            }
        }
    }
    if (need_free_string)
        free(preauth_types);

#ifdef DEBUG
    fprintf (stderr, "preauth data types after sorting:");
    for (i = 0; padata[i]; i++)
        fprintf (stderr, " %d", padata[i]->pa_type);
    fprintf (stderr, "\n");
#endif

    return 0;
}

static krb5_error_code
build_in_tkt_name(krb5_context context,
                  char *in_tkt_service,
                  krb5_const_principal client,
                  krb5_principal *server)
{
    krb5_error_code ret;

    *server = NULL;

    if (in_tkt_service) {
        /* this is ugly, because so are the data structures involved.  I'm
           in the library, so I'm going to manipulate the data structures
           directly, otherwise, it will be worse. */

        if ((ret = krb5_parse_name(context, in_tkt_service, server)))
            return ret;

        /* stuff the client realm into the server principal.
           realloc if necessary */
        if ((*server)->realm.length < client->realm.length) {
            char *p = realloc((*server)->realm.data,
                              client->realm.length);
            if (p == NULL) {
                krb5_free_principal(context, *server);
                *server = NULL;
                return ENOMEM;
            }
            (*server)->realm.data = p;
        }

        (*server)->realm.length = client->realm.length;
        memcpy((*server)->realm.data, client->realm.data, client->realm.length);
    } else {
        ret = krb5_build_principal_ext(context, server,
                                       client->realm.length,
                                       client->realm.data,
                                       KRB5_TGS_NAME_SIZE,
                                       KRB5_TGS_NAME,
                                       client->realm.length,
                                       client->realm.data,
                                       0);
    }
    return ret;
}

void KRB5_CALLCONV
krb5_init_creds_free(krb5_context context,
                     krb5_init_creds_context ctx)
{
    if (ctx == NULL)
        return;

    if (ctx->opte != NULL && krb5_gic_opt_is_shadowed(ctx->opte)) {
        krb5_get_init_creds_opt_free(context,
                                     (krb5_get_init_creds_opt *)ctx->opte);
    }
    free(ctx->in_tkt_service);
    zap(ctx->password.data, ctx->password.length);
    krb5_free_data_contents(context, &ctx->password);
    krb5_free_error(context, ctx->err_reply);
    krb5_free_cred_contents(context, &ctx->cred);
    krb5_free_kdc_req(context, ctx->request);
    krb5_free_kdc_rep(context, ctx->reply);
    krb5_free_data(context, ctx->encoded_request_body);
    krb5_free_data(context, ctx->encoded_previous_request);
    krb5int_fast_free_state(context, ctx->fast_state);
    krb5_free_pa_data(context, ctx->preauth_to_use);
    krb5_free_data_contents(context, &ctx->salt);
    krb5_free_data_contents(context, &ctx->s2kparams);
    krb5_free_keyblock_contents(context, &ctx->as_key);
    free(ctx);
}

static krb5_error_code
init_creds_get(krb5_context context,
               krb5_init_creds_context ctx,
               int *use_master)
{
    krb5_error_code code;
    krb5_data request;
    krb5_data reply;
    krb5_data realm;
    unsigned int flags = 0;
    int tcp_only = 0;
    char *hostname_used = NULL; /* Solaris Kerberos better errors */

    request.length = 0;
    request.data = NULL;
    reply.length = 0;
    reply.data = NULL;
    realm.length = 0;
    realm.data = NULL;

    for (;;) {
        code = krb5_init_creds_step(context,
                                    ctx,
                                    &reply,
                                    &request,
                                    &realm,
                                    &flags,
				    hostname_used);
        if (code == KRB5KRB_ERR_RESPONSE_TOO_BIG && !tcp_only)
            tcp_only = 1;
        else if (code != 0 || (flags & 1) == 0)
            break;

        krb5_free_data_contents(context, &reply);

        /* Solaris Kerberos */
        code = krb5_sendto_kdc(context, &request, ctx->request, &realm,
                               &reply, use_master, tcp_only,
			       &hostname_used);
        if (code != 0)
            break;

        /* Solaris Kerberos: mem leak fix */
        if (ctx->request->padata) {
            krb5_free_pa_data(context, ctx->request->padata);
            ctx->request->padata = NULL;
        }

        krb5_free_data_contents(context, &request);
        krb5_free_data_contents(context, &realm);
        /* Solaris Kerberos */
        free(hostname_used);
        hostname_used = NULL;
    }

    krb5_free_data_contents(context, &request);
    krb5_free_data_contents(context, &reply);
    krb5_free_data_contents(context, &realm);
    /* Solaris Kerberos */
    free(hostname_used);

    return code;
}

/* Heimdal API */
krb5_error_code KRB5_CALLCONV
krb5_init_creds_get(krb5_context context,
                    krb5_init_creds_context ctx)
{
    int use_master = 0;

    return init_creds_get(context, ctx, &use_master);
}

krb5_error_code KRB5_CALLCONV
krb5_init_creds_get_creds(krb5_context context,
                          krb5_init_creds_context ctx,
                          krb5_creds *creds)
{
    if ((ctx->flags & KRB5_INIT_CREDS_STEP_FLAG_COMPLETE) == 0)
        return KRB5_NO_TKT_SUPPLIED;

    return krb5int_copy_creds_contents(context, &ctx->cred, creds);
}

krb5_error_code KRB5_CALLCONV
krb5_init_creds_get_times(krb5_context context,
                          krb5_init_creds_context ctx,
                          krb5_ticket_times *times)
{
    if ((ctx->flags & KRB5_INIT_CREDS_STEP_FLAG_COMPLETE) == 0)
        return KRB5_NO_TKT_SUPPLIED;

    *times = ctx->cred.times;

    return 0;
}

krb5_error_code KRB5_CALLCONV
krb5_init_creds_get_error(krb5_context context,
                          krb5_init_creds_context ctx,
                          krb5_error **error)
{
    krb5_error_code code;
    krb5_error *ret = NULL;

    *error = NULL;

    if (ctx->err_reply == NULL)
        return 0;

    ret = k5alloc(sizeof(*ret), &code);
    if (code != 0)
        goto cleanup;

    ret->magic = KV5M_ERROR;
    ret->ctime = ctx->err_reply->ctime;
    ret->cusec = ctx->err_reply->cusec;
    ret->susec = ctx->err_reply->susec;
    ret->stime = ctx->err_reply->stime;
    ret->error = ctx->err_reply->error;

    if (ctx->err_reply->client != NULL) {
        code = krb5_copy_principal(context, ctx->err_reply->client,
                                   &ret->client);
        if (code != 0)
            goto cleanup;
    }

    code = krb5_copy_principal(context, ctx->err_reply->server, &ret->server);
    if (code != 0)
        goto cleanup;

    code = krb5int_copy_data_contents(context, &ctx->err_reply->text,
                                      &ret->text);
    if (code != 0)
        goto cleanup;

    code = krb5int_copy_data_contents(context, &ctx->err_reply->e_data,
                                      &ret->e_data);
    if (code != 0)
        goto cleanup;

    *error = ret;

cleanup:
    if (code != 0)
        krb5_free_error(context, ret);

    return code;
}

/**
 * Throw away any state related to specific realm either at the beginning of a
 * request, or when a realm changes, or when we start to use FAST after
 * assuming we would not do so.
 *
 * @param padata padata from an error if an error from the realm we now expect
 * to talk to caused the restart.  Used to infer negotiation characteristics
 * such as whether FAST is used.
 */
static krb5_error_code
restart_init_creds_loop(krb5_context context, krb5_init_creds_context ctx,
                        krb5_pa_data **padata)
{
    krb5_error_code code = 0;
    unsigned char random_buf[4];
    krb5_data random_data;
    if (ctx->preauth_to_use) {
        krb5_free_pa_data(context, ctx->preauth_to_use);
        ctx->preauth_to_use = NULL;
    }

    if (ctx->fast_state) {
        krb5int_fast_free_state(context, ctx->fast_state);
        ctx->fast_state = NULL;
    }
    code = krb5int_fast_make_state(context, &ctx->fast_state);
    if (code != 0)
        goto cleanup;
    ctx->get_data_rock.fast_state = ctx->fast_state;
    krb5_preauth_request_context_init(context);
    if (ctx->encoded_request_body) {
        krb5_free_data(context, ctx->encoded_request_body);
        ctx->encoded_request_body = NULL;
    }
    if (ctx->opte &&
        (ctx->opte->flags & KRB5_GET_INIT_CREDS_OPT_PREAUTH_LIST)) {
        if ((code = make_preauth_list(context, ctx->opte->preauth_list,
                                      ctx->opte->preauth_list_length,
                                      &ctx->preauth_to_use)))
            goto cleanup;
    }

    /* Set the request nonce. */
    random_data.length = 4;
    random_data.data = (char *)random_buf;
    code = krb5_c_random_make_octets(context, &random_data);
    if (code !=0)
        goto cleanup;
    /*
     * See RT ticket 3196 at MIT.  If we set the high bit, we may have
     * compatibility problems with Heimdal, because we (incorrectly) encode
     * this value as signed.
     */
    ctx->request->nonce = 0x7fffffff & load_32_n(random_buf);
    krb5_free_principal(context, ctx->request->server);
    ctx->request->server = NULL;

    code = build_in_tkt_name(context, ctx->in_tkt_service,
                             ctx->request->client,
                             &ctx->request->server);
    if (code != 0)
        goto cleanup;

    code = krb5_timeofday(context, &ctx->request_time);
    if (code != 0)
        goto cleanup;

    code = krb5int_fast_as_armor(context, ctx->fast_state,
                                 ctx->opte, ctx->request);
    if (code != 0)
        goto cleanup;
    if (krb5int_upgrade_to_fast_p(context, ctx->fast_state, padata)) {
        code = krb5int_fast_as_armor(context, ctx->fast_state,
                                     ctx->opte, ctx->request);
        if (code != 0)
            goto cleanup;
    }
    /* give the preauth plugins a chance to prep the request body */
    krb5_preauth_prepare_request(context, ctx->opte, ctx->request);

    ctx->request->from = krb5int_addint32(ctx->request_time,
                                          ctx->start_time);
    ctx->request->till = krb5int_addint32(ctx->request->from,
                                          ctx->tkt_life);

    if (ctx->renew_life > 0) {
        ctx->request->rtime =
            krb5int_addint32(ctx->request->from, ctx->renew_life);
        if (ctx->request->rtime < ctx->request->till) {
            /* don't ask for a smaller renewable time than the lifetime */
            ctx->request->rtime = ctx->request->till;
        }
        ctx->request->kdc_options &= ~(KDC_OPT_RENEWABLE_OK);
    } else
        ctx->request->rtime = 0;
    code = krb5int_fast_prep_req_body(context, ctx->fast_state,
                                      ctx->request,
                                      &ctx->encoded_request_body);
    if (code != 0)
        goto cleanup;
cleanup:
    return code;
}

krb5_error_code KRB5_CALLCONV
krb5_init_creds_init(krb5_context context,
                     krb5_principal client,
                     krb5_prompter_fct prompter,
                     void *data,
                     krb5_deltat start_time,
                     krb5_get_init_creds_opt *options,
                     krb5_init_creds_context *pctx)
{
    krb5_error_code code;
    krb5_init_creds_context ctx;
    int tmp;
    char *str = NULL;
    krb5_gic_opt_ext *opte;

    ctx = k5alloc(sizeof(*ctx), &code);
    if (code != 0)
        goto cleanup;

    ctx->request = k5alloc(sizeof(krb5_kdc_req), &code);
    if (code != 0)
        goto cleanup;
    ctx->enc_pa_rep_permitted = 1;
    code = krb5_copy_principal(context, client, &ctx->request->client);
    if (code != 0)
        goto cleanup;

    ctx->prompter = prompter;
    ctx->prompter_data = data;
    ctx->gak_fct = krb5_get_as_key_password;
    ctx->gak_data = &ctx->password;

    ctx->request_time = 0; /* filled in later */
    ctx->start_time = start_time;

    if (options == NULL) {
        code = krb5_get_init_creds_opt_alloc(context, &options);
        if (code != 0)
            goto cleanup;
    }

    code = krb5int_gic_opt_to_opte(context, options,
                                       &ctx->opte, 1, "krb5_init_creds_init");
    if (code != 0)
        goto cleanup;

    opte = ctx->opte;

    ctx->get_data_rock.magic = CLIENT_ROCK_MAGIC;
    ctx->get_data_rock.etype = &ctx->etype;

    /* Initialise request parameters as per krb5_get_init_creds() */
    ctx->request->kdc_options = context->kdc_default_options;

    /* forwaradble */
    if (opte->flags & KRB5_GET_INIT_CREDS_OPT_FORWARDABLE)
        tmp = opte->forwardable;
    else if (krb5int_libdefault_boolean(context, &ctx->request->client->realm,
                                        KRB5_CONF_FORWARDABLE, &tmp) == 0)
        ;
    else
        tmp = 0;
    if (tmp)
        ctx->request->kdc_options |= KDC_OPT_FORWARDABLE;

    /* proxiable */
    if (opte->flags & KRB5_GET_INIT_CREDS_OPT_PROXIABLE)
        tmp = opte->proxiable;
    else if (krb5int_libdefault_boolean(context, &ctx->request->client->realm,
                                        KRB5_CONF_PROXIABLE, &tmp) == 0)
        ;
    else
        tmp = 0;
    if (tmp)
        ctx->request->kdc_options |= KDC_OPT_PROXIABLE;

    /* canonicalize */
    if (opte->flags & KRB5_GET_INIT_CREDS_OPT_CANONICALIZE)
        tmp = 1;
    else if (krb5int_libdefault_boolean(context, &ctx->request->client->realm,
                                        KRB5_CONF_CANONICALIZE, &tmp) == 0)
        ;
    else
        tmp = 0;
    if (tmp)
        ctx->request->kdc_options |= KDC_OPT_CANONICALIZE;

    /* allow_postdate */
    if (ctx->start_time > 0)
        ctx->request->kdc_options |= KDC_OPT_ALLOW_POSTDATE | KDC_OPT_POSTDATED;

    /* ticket lifetime */
    if (opte->flags & KRB5_GET_INIT_CREDS_OPT_TKT_LIFE)
        ctx->tkt_life = options->tkt_life;
    else if (krb5int_libdefault_string(context, &ctx->request->client->realm,
                                       KRB5_CONF_TICKET_LIFETIME, &str) == 0) {
        code = krb5_string_to_deltat(str, &ctx->tkt_life);
        if (code != 0)
            goto cleanup;
        free(str);
        str = NULL;
    } else
        ctx->tkt_life = 24 * 60 * 60; /* previously hardcoded in kinit */

    /* renewable lifetime */
    if (opte->flags & KRB5_GET_INIT_CREDS_OPT_RENEW_LIFE)
        ctx->renew_life = options->renew_life;
    else if (krb5int_libdefault_string(context, &ctx->request->client->realm,
                                       KRB5_CONF_RENEW_LIFETIME, &str) == 0) {
        code = krb5_string_to_deltat(str, &ctx->renew_life);
        if (code != 0)
            goto cleanup;
        free(str);
        str = NULL;
    } else
        ctx->renew_life = 0;

    if (ctx->renew_life > 0)
        ctx->request->kdc_options |= KDC_OPT_RENEWABLE;

    /* enctypes */
    if (opte->flags & KRB5_GET_INIT_CREDS_OPT_ETYPE_LIST) {
        ctx->request->ktype =
            k5alloc((opte->etype_list_length * sizeof(krb5_enctype)),
                    &code);
        if (code != 0)
            goto cleanup;
        ctx->request->nktypes = opte->etype_list_length;
        memcpy(ctx->request->ktype, opte->etype_list,
               ctx->request->nktypes * sizeof(krb5_enctype));
    } else if (krb5_get_default_in_tkt_ktypes(context,
                                              &ctx->request->ktype) == 0) {
        ctx->request->nktypes = krb5int_count_etypes(ctx->request->ktype);
    } else {
        /* there isn't any useful default here. */
        code = KRB5_CONFIG_ETYPE_NOSUPP;
        goto cleanup;
    }

    /* addresess */
    if (opte->flags & KRB5_GET_INIT_CREDS_OPT_ADDRESS_LIST) {
        code = krb5_copy_addresses(context, opte->address_list,
                                   &ctx->request->addresses);
        if (code != 0)
            goto cleanup;
    } else if (krb5int_libdefault_boolean(context, &ctx->request->client->realm,
                                          KRB5_CONF_NOADDRESSES, &tmp) != 0
               || tmp) {
        ctx->request->addresses = NULL;
    } else {
        code = krb5_os_localaddr(context, &ctx->request->addresses);
        if (code != 0)
            goto cleanup;
    }

    if (opte->flags & KRB5_GET_INIT_CREDS_OPT_SALT) {
        code = krb5int_copy_data_contents(context, opte->salt, &ctx->salt);
        if (code != 0)
            goto cleanup;
    } else {
        ctx->salt.length = SALT_TYPE_AFS_LENGTH;
        ctx->salt.data = NULL;
    }

    /* Anonymous. */
    if(opte->flags & KRB5_GET_INIT_CREDS_OPT_ANONYMOUS) {
        ctx->request->kdc_options |= KDC_OPT_REQUEST_ANONYMOUS;
        /* Remap @REALM to WELLKNOWN/ANONYMOUS@REALM. */
        if (client->length == 1 && client->data[0].length ==0) {
            krb5_principal new_client;
            code = krb5_build_principal_ext(context, &new_client,
                                            client->realm.length,
                                            client->realm.data,
                                            strlen(KRB5_WELLKNOWN_NAMESTR),
                                            KRB5_WELLKNOWN_NAMESTR,
                                            strlen(KRB5_ANONYMOUS_PRINCSTR),
                                            KRB5_ANONYMOUS_PRINCSTR,
                                            0);
            if (code)
                goto cleanup;
            krb5_free_principal(context, ctx->request->client);
            ctx->request->client = new_client;
            krb5_princ_type(context, ctx->request->client) = KRB5_NT_WELLKNOWN;
        }
    }
    /* We will also handle anonymous if the input principal is the anonymous
     * principal. */
    if (krb5_principal_compare_any_realm(context, ctx->request->client,
                                         krb5_anonymous_principal())) {
        ctx->request->kdc_options |= KDC_OPT_REQUEST_ANONYMOUS;
        krb5_princ_type(context, ctx->request->client) = KRB5_NT_WELLKNOWN;
    }
    code = restart_init_creds_loop(context, ctx, NULL);
    if (code) {
        /* Solaris Kerberos */
        krb5_preauth_request_context_fini(context);
        goto cleanup;
    }

    *pctx = ctx;
    ctx = NULL;

cleanup:
    krb5_init_creds_free(context, ctx);
    free(str);

    return code;
}

krb5_error_code KRB5_CALLCONV
krb5_init_creds_set_service(krb5_context context,
                            krb5_init_creds_context ctx,
                            const char *service)
{
    char *s;

    s = strdup(service);
    if (s == NULL)
        return ENOMEM;

    free(ctx->in_tkt_service);
    ctx->in_tkt_service = s;

    krb5_preauth_request_context_fini(context);
    return restart_init_creds_loop(context, ctx, NULL);
}

static krb5_error_code
init_creds_validate_reply(krb5_context context,
                          krb5_init_creds_context ctx,
                          krb5_data *reply)
{
    krb5_error_code code;
    krb5_error *error = NULL;
    krb5_kdc_rep *as_reply = NULL;

    krb5_free_error(context, ctx->err_reply);
    ctx->err_reply = NULL;

    krb5_free_kdc_rep(context, ctx->reply);
    ctx->reply = NULL;

    if (krb5_is_krb_error(reply)) {
        code = decode_krb5_error(reply, &error);
        if (code != 0)
            return code;

        assert(error != NULL);

        if (error->error == KRB_ERR_RESPONSE_TOO_BIG) {
            krb5_free_error(context, error);
            return KRB5KRB_ERR_RESPONSE_TOO_BIG;
        } else {
            ctx->err_reply = error;
            return 0;
        }
    }

    /*
     * Check to make sure it isn't a V4 reply.
     */
    if (reply->length != 0 && !krb5_is_as_rep(reply)) {
/* these are in <kerberosIV/prot.h> as well but it isn't worth including. */
#define V4_KRB_PROT_VERSION     4
#define V4_AUTH_MSG_ERR_REPLY   (5<<1)
        /* check here for V4 reply */
        unsigned int t_switch;

        /* From v4 g_in_tkt.c: This used to be
           switch (pkt_msg_type(rpkt) & ~1) {
           but SCO 3.2v4 cc compiled that incorrectly.  */
        t_switch = reply->data[1];
        t_switch &= ~1;

        if (t_switch == V4_AUTH_MSG_ERR_REPLY
            && reply->data[0] == V4_KRB_PROT_VERSION) {
            code = KRB5KRB_AP_ERR_V4_REPLY;
        } else {
            code = KRB5KRB_AP_ERR_MSG_TYPE;
        }
        return code;
    }

    /* It must be a KRB_AS_REP message, or an bad returned packet */
    code = decode_krb5_as_rep(reply, &as_reply);
    if (code != 0)
        return code;

    if (as_reply->msg_type != KRB5_AS_REP) {
        krb5_free_kdc_rep(context, as_reply);
        return KRB5KRB_AP_ERR_MSG_TYPE;
    }

    ctx->reply = as_reply;

    return 0;
}

static krb5_error_code
init_creds_step_request(krb5_context context,
                        krb5_init_creds_context ctx,
                        krb5_data *out)
{
    krb5_error_code code;

    if (ctx->loopcount >= MAX_IN_TKT_LOOPS) {
        code = KRB5_GET_IN_TKT_LOOP;
        /* Solaris Kerberos */
        {
            char *s_name = NULL;
            char *c_name = NULL;
            krb5_error_code serr, cerr;
            serr = krb5_unparse_name(context, ctx->cred.server, &s_name);
            cerr = krb5_unparse_name(context, ctx->cred.client, &c_name);
            krb5_set_error_message(context, code,
                                   dgettext(TEXT_DOMAIN,
                                            "Looping detected getting initial "
                                            "creds: '%s' requesting ticket "
                                            "'%s'. Max loops is %d.  Make sure "
                                            "a KDC is available"),
                                   cerr ? "unknown" : c_name,
                                   serr ? "unknown" : s_name,
                                   MAX_IN_TKT_LOOPS);
            if (s_name)
                krb5_free_unparsed_name(context, s_name);
            if (c_name)
                krb5_free_unparsed_name(context, c_name);
        }
        goto cleanup;
    }

    if (ctx->err_reply == NULL) {
        /* either our first attempt, or retrying after PREAUTH_NEEDED */
        code = krb5_do_preauth(context,
                               ctx->request,
                               ctx->encoded_request_body,
                               ctx->encoded_previous_request,
                               ctx->preauth_to_use,
                               &ctx->request->padata,
                               &ctx->salt,
                               &ctx->s2kparams,
                               &ctx->etype,
                               &ctx->as_key,
                               ctx->prompter,
                               ctx->prompter_data,
                               ctx->gak_fct,
                               ctx->gak_data,
                               &ctx->get_data_rock,
                               ctx->opte);
        if (code != 0)
            goto cleanup;
    } else {
        if (ctx->preauth_to_use != NULL) {
            /*
             * Retry after an error other than PREAUTH_NEEDED,
             * using e-data to figure out what to change.
             */
            code = krb5_do_preauth_tryagain(context,
                                            ctx->request,
                                            ctx->encoded_request_body,
                                            ctx->encoded_previous_request,
                                            ctx->preauth_to_use,
                                            &ctx->request->padata,
                                            ctx->err_reply,
                                            &ctx->salt,
                                            &ctx->s2kparams,
                                            &ctx->etype,
                                            &ctx->as_key,
                                            ctx->prompter,
                                            ctx->prompter_data,
                                            ctx->gak_fct,
                                            ctx->gak_data,
                                            &ctx->get_data_rock,
                                            ctx->opte);
        } else {
            /* No preauth supplied, so can't query the plugins. */
            code = KRB5KRB_ERR_GENERIC;
        }
        if (code != 0) {
            /* couldn't come up with anything better */
            code = ctx->err_reply->error + ERROR_TABLE_BASE_krb5;
            goto cleanup;
        }
    }

    if (ctx->encoded_previous_request != NULL) {
        krb5_free_data(context, ctx->encoded_previous_request);
        ctx->encoded_previous_request = NULL;
    }
    if (ctx->request->padata)
        ctx->sent_nontrivial_preauth = 1;
    if (ctx->enc_pa_rep_permitted) {
        code = request_enc_pa_rep(&ctx->request->padata);
    }
    if (code)
        goto cleanup;
    code = krb5int_fast_prep_req(context, ctx->fast_state,
                                 ctx->request, ctx->encoded_request_body,
                                 encode_krb5_as_req,
                                 &ctx->encoded_previous_request);
    if (code != 0)
        goto cleanup;

    code = krb5int_copy_data_contents(context,
                                      ctx->encoded_previous_request,
                                      out);
    if (code != 0)
        goto cleanup;

cleanup:
    return code;
}

/*
 * The control flow is complicated.  In order to switch from non-FAST mode to
 * FAST mode, we need to reset our pre-authentication state.  FAST negotiation
 * attempts to make sure we rarely have to do this.  When FAST negotiation is
 * working, we record whether FAST is available when we obtain an armor ticket;
 * if so, we start out with FAST enabled .  There are two complicated
 * situations.
 *
 * First, if we get a PREAUTH_REQUIRED error including PADATA_FX_FAST back from
 * a KDC in a case where we were not expecting to use FAST, and we have an
 * armor ticket available, then we want to use FAST.  That involves clearing
 * out the pre-auth state, reinitializing the plugins and trying again with an
 * armor key.
 *
 * Secondly, using the negotiation can cause problems with some older KDCs.
 * Negotiation involves including a special padata item.  Some KDCs, including
 * MIT prior to 1.7, will return PREAUTH_FAILED rather than PREAUTH_REQUIRED in
 * pre-authentication is required and unknown padata are included in the
 * request.  To make matters worse, these KDCs typically do not include a list
 * of padata in PREAUTH_FAILED errors.  So, if we get PREAUTH_FAILED and we
 * generated no pre-authentication other than the negotiation then we want to
 * retry without negotiation.  In this case it is probably also desirable to
 * retry with the preauth plugin state cleared.
 *
 * In all these cases we should not start over more than once.  Control flow is
 * managed by several variables.
 *
 *   sent_nontrivial_preauth: if true, we sent preauth other than negotiation;
 *   no restart on PREAUTH_FAILED
 *
 *   KRB5INT_FAST_ARMOR_AVAIL: fast_state_flag if desired we could generate
 *   armor; if not set, then we can't use FAST even if the KDC wants to.
 *
 *   have_restarted: true if we've already restarted
 */
static krb5_boolean
negotiation_requests_restart(krb5_context context, krb5_init_creds_context ctx,
                             krb5_pa_data **padata)
{
    if (!ctx->have_restarted &&
        (krb5int_upgrade_to_fast_p(context, ctx->fast_state, padata) ||
         (ctx->err_reply->error == KDC_ERR_PREAUTH_FAILED &&
          !ctx->sent_nontrivial_preauth)))
        return 1;
    return 0;
}

/* Ensure that the reply enctype was among the requested enctypes. */
static krb5_error_code
check_reply_enctype(krb5_init_creds_context ctx)
{
    int i;

    for (i = 0; i < ctx->request->nktypes; i++) {
        if (ctx->request->ktype[i] == ctx->reply->enc_part.enctype)
            return 0;
    }
    return KRB5_CONFIG_ETYPE_NOSUPP;
}

/*
 * Solaris Kerberos
 * Return 1 if any char in string is lower-case.
 */
static int
is_lower_case(char *s)
{
    if (!s)
        return 0;

    while (*s) {
        if (islower((int)*s))
            return 1;
        s++;
    }
    return 0;
}

static krb5_error_code
init_creds_step_reply(krb5_context context,
                      krb5_init_creds_context ctx,
                      krb5_data *in,
		      char *hostname_used)
{
    krb5_error_code code;
    krb5_pa_data **padata = NULL;
    krb5_pa_data **kdc_padata = NULL;
    krb5_boolean retry = FALSE;
    int canon_flag = 0;
    krb5_keyblock *strengthen_key = NULL;
    krb5_keyblock encrypting_key;
    krb5_boolean fast_avail;

#if 0 /* ************ Begin IFDEF'ed OUT ***************************** */
    encrypting_key.length = 0;
    encrypting_key.contents = NULL;
#else
    /* Solaris Kerberos: our keyblock has more fields */
    (void) memset(&encrypting_key, 0, sizeof (encrypting_key));
#endif /* ************** END IFDEF'ed OUT ***************************** */

    /* process previous KDC response */
    code = init_creds_validate_reply(context, ctx, in);
    if (code != 0)
        goto cleanup;

    /* per referrals draft, enterprise principals imply canonicalization */
    canon_flag = ((ctx->request->kdc_options & KDC_OPT_CANONICALIZE) != 0) ||
        ctx->request->client->type == KRB5_NT_ENTERPRISE_PRINCIPAL;

    if (ctx->err_reply != NULL) {
        code = krb5int_fast_process_error(context, ctx->fast_state,
                                          &ctx->err_reply, &padata, &retry);
        if (code != 0)
            goto cleanup;
        if (negotiation_requests_restart(context, ctx, padata)) {
            ctx->have_restarted = 1;
            krb5_preauth_request_context_fini(context);
            if ((ctx->fast_state->fast_state_flags & KRB5INT_FAST_DO_FAST) ==0)
                ctx->enc_pa_rep_permitted = 0;
            code = restart_init_creds_loop(context, ctx, padata);
            krb5_free_error(context, ctx->err_reply);
            ctx->err_reply = NULL;
        } else if (ctx->err_reply->error == KDC_ERR_PREAUTH_REQUIRED &&
                   retry) {
            /* reset the list of preauth types to try */
            krb5_free_pa_data(context, ctx->preauth_to_use);
            ctx->preauth_to_use = padata;
            padata = NULL;
            /* this will trigger a new call to krb5_do_preauth() */
            krb5_free_error(context, ctx->err_reply);
            ctx->err_reply = NULL;
            code = sort_krb5_padata_sequence(context,
                                             &ctx->request->client->realm,
                                             ctx->preauth_to_use);

        } else if (canon_flag && ctx->err_reply->error == KDC_ERR_WRONG_REALM) {
            if (ctx->err_reply->client == NULL ||
                !krb5_princ_realm(context, ctx->err_reply->client)->length) {
                code = KRB5KDC_ERR_WRONG_REALM;
                goto cleanup;
            }
            /* Rewrite request.client with realm from error reply */
            krb5_free_data_contents(context, &ctx->request->client->realm);
            code = krb5int_copy_data_contents(context,
                                              &ctx->err_reply->client->realm,
                                              &ctx->request->client->realm);
            /* this will trigger a new call to krb5_do_preauth() */
            krb5_free_error(context, ctx->err_reply);
            ctx->err_reply = NULL;
            krb5_preauth_request_context_fini(context);
            /* Permit another negotiation based restart. */
            ctx->have_restarted = 0;
            ctx->sent_nontrivial_preauth = 0;
            code = restart_init_creds_loop(context, ctx, NULL);
            if (code != 0)
                goto cleanup;
        } else {
            if (retry) {
                code = 0;
            } else {
                /* error + no hints = give up */
                code = (krb5_error_code)ctx->err_reply->error +
                    ERROR_TABLE_BASE_krb5;
            }
        }

        /* Return error code, or continue with next iteration */
        goto cleanup;
    }

    /* We have a response. Process it. */
    assert(ctx->reply != NULL);

    /* Check for replies (likely forged) with unasked-for enctypes. */
    code = check_reply_enctype(ctx);
    if (code != 0)
        goto cleanup;

    /* process any preauth data in the as_reply */
    krb5_clear_preauth_context_use_counts(context);
    code = krb5int_fast_process_response(context, ctx->fast_state,
                                         ctx->reply, &strengthen_key);
    if (code != 0)
        goto cleanup;

    code = sort_krb5_padata_sequence(context, &ctx->request->client->realm,
                                     ctx->reply->padata);
    if (code != 0)
        goto cleanup;

    ctx->etype = ctx->reply->enc_part.enctype;

    code = krb5_do_preauth(context,
                           ctx->request,
                           ctx->encoded_request_body,
                           ctx->encoded_previous_request,
                           ctx->reply->padata,
                           &kdc_padata,
                           &ctx->salt,
                           &ctx->s2kparams,
                           &ctx->etype,
                           &ctx->as_key,
                           ctx->prompter,
                           ctx->prompter_data,
                           ctx->gak_fct,
                           ctx->gak_data,
                           &ctx->get_data_rock,
                           ctx->opte);
    if (code != 0)
        goto cleanup;

    /*
     * If we haven't gotten a salt from another source yet, set up one
     * corresponding to the client principal returned by the KDC.  We
     * could get the same effect by passing local_as_reply->client to
     * gak_fct below, but that would put the canonicalized client name
     * in the prompt, which raises issues of needing to sanitize
     * unprintable characters.  So for now we just let it affect the
     * salt.  local_as_reply->client will be checked later on in
     * verify_as_reply.
     */
    if (ctx->salt.length == SALT_TYPE_AFS_LENGTH && ctx->salt.data == NULL) {
        code = krb5_principal2salt(context, ctx->reply->client, &ctx->salt);
        if (code != 0)
            goto cleanup;
    }

    /* XXX For 1.1.1 and prior KDC's, when SAM is used w/ USE_SAD_AS_KEY,
       the AS_REP comes back encrypted in the user's longterm key
       instead of in the SAD. If there was a SAM preauth, there
       will be an as_key here which will be the SAD. If that fails,
       use the gak_fct to get the password, and try again. */

    /* XXX because etypes are handled poorly (particularly wrt SAM,
       where the etype is fixed by the kdc), we may want to try
       decrypt_as_reply twice.  If there's an as_key available, try
       it.  If decrypting the as_rep fails, or if there isn't an
       as_key at all yet, then use the gak_fct to get one, and try
       again.  */
    if (ctx->as_key.length) {
        code = krb5int_fast_reply_key(context, strengthen_key, &ctx->as_key,
                                      &encrypting_key);
        if (code != 0)
            goto cleanup;
        code = decrypt_as_reply(context, NULL, ctx->reply, NULL, NULL,
                                &encrypting_key, krb5_kdc_rep_decrypt_proc,
                                NULL);
    } else
        code = -1;

    if (code != 0) {
        /* if we haven't get gotten a key, get it now */
        code = (*ctx->gak_fct)(context, ctx->request->client,
                               ctx->reply->enc_part.enctype,
                               ctx->prompter, ctx->prompter_data,
                               &ctx->salt, &ctx->s2kparams,
                               &ctx->as_key, ctx->gak_data);
        if (code != 0)
            goto cleanup;

        code = krb5int_fast_reply_key(context, strengthen_key, &ctx->as_key,
                                      &encrypting_key);
        if (code != 0)
            goto cleanup;

        code = decrypt_as_reply(context, NULL, ctx->reply, NULL, NULL,
                                &encrypting_key, krb5_kdc_rep_decrypt_proc,
                                NULL);
        if (code != 0)
            goto cleanup;
    }

    code = krb5int_fast_verify_nego(context, ctx->fast_state,
                                    ctx->reply, ctx->encoded_previous_request,
                                    &encrypting_key, &fast_avail);
    if (code)
        goto cleanup;
    code = verify_as_reply(context, ctx->request_time,
                           ctx->request, ctx->reply);
    if (code != 0)
        goto cleanup;
    code = verify_anonymous(context, ctx->request, ctx->reply,
                            &encrypting_key);
    if (code)
        goto cleanup;

    code = stash_as_reply(context, ctx->request_time, ctx->request,
                          ctx->reply, &ctx->cred, NULL);
    if (code != 0)
        goto cleanup;
    if (ctx->opte && ctx->opte->opt_private->out_ccache) {
        krb5_ccache out_ccache = ctx->opte->opt_private->out_ccache;
        krb5_data config_data;
        code = krb5_cc_initialize(context, out_ccache, ctx->cred.client);
        if (code != 0)
            goto cc_cleanup;
        code = krb5_cc_store_cred(context, out_ccache, &ctx->cred);
        if (code != 0)
            goto cc_cleanup;
        if (fast_avail) {
            config_data.data = "yes";
            config_data.length = strlen(config_data.data);
            code = krb5_cc_set_config(context, out_ccache, ctx->cred.server,
                                      KRB5_CONF_FAST_AVAIL, &config_data);
        }
    cc_cleanup:
        if (code !=0) {
            const char *msg;
            msg = krb5_get_error_message(context, code);
            krb5_set_error_message(context, code,
                                   "%s while storing credentials", msg);
            krb5_free_error_message(context, msg);
	    /* Solaris Kerberos */
	    goto cleanup;
        }
    }

    krb5_preauth_request_context_fini(context);

    /* success */
    code = 0;
    ctx->flags |= KRB5_INIT_CREDS_STEP_FLAG_COMPLETE;

cleanup:
    /* Solaris Kerberos dtrace */
    if (krb5_is_as_rep(in)) {
        k5_trace_kdc_rep_read(in, code == 0 ? ctx->reply : NULL);
    }

    /* Solaris Kerberos: begin spruce-up the err msg */
    if (code != 0) {
        char *client_name = NULL;
        /* See if we can produce a more detailed error message.  */
        switch (code) {
        case KRB5KDC_ERR_C_PRINCIPAL_UNKNOWN:
            if (krb5_unparse_name(context, ctx->request->client, &client_name) == 0) {
                krb5_set_error_message(context, code,
                                       dgettext(TEXT_DOMAIN,
                                                "Client '%s' not found in Kerberos database"),
                                       client_name);
                krb5_free_unparsed_name(context, client_name);
            }
            break;
            /* Solaris Kerberos: spruce-up the err msg */
        case KRB5_PREAUTH_FAILED:
        case KRB5KDC_ERR_PREAUTH_FAILED:
            if (krb5_unparse_name(context, ctx->request->client, &client_name) == 0) {
                krb5_set_error_message(context, code,
                                       dgettext(TEXT_DOMAIN,
                                                "Client '%s' pre-authentication failed"),
                                       client_name);
                krb5_free_unparsed_name(context, client_name);
            }
            break;
            /* Solaris Kerberos: spruce-up the err msg */
        case KRB5KRB_AP_ERR_SKEW: /* KRB_AP_ERR_SKEW + ERROR_TABLE_BASE_krb5 */
            {
                char *s_name = NULL;
                char *c_name = NULL;
                char stimestring[17];
                char fill = ' ';
                krb5_error_code c_err, s_err, s_time = 0;
                krb5_timestamp time_now;

                s_err = krb5_unparse_name(context,
                                          ctx->request->server, &s_name);
                if (ctx->err_reply) {
                    s_time = krb5_timestamp_to_sfstring(ctx->err_reply->stime,
                                                        stimestring,
                                                        sizeof (stimestring),
                                                        &fill);
                }
                (void) krb5_timeofday(context, &time_now);
                c_err = krb5_unparse_name(context, ctx->request->client, &c_name);
                krb5_set_error_message(context, code,
                                       dgettext(TEXT_DOMAIN,
                                                "Clock skew too great: '%s' requesting ticket '%s' from KDC '%s' (%s). Skew is %dm"),
                                       c_err == 0 ? c_name : "unknown",
                                       s_err == 0 ? s_name : "unknown",
                                       hostname_used ? hostname_used : "unknown",
                                       s_time == 0 ? stimestring : "unknown",
                                       (s_time != 0) ? 0 :
                                       (abs(ctx->err_reply->stime - time_now) / 60));
                if (s_name)
                    krb5_free_unparsed_name(context, s_name);
                if (c_name)
                    krb5_free_unparsed_name(context, c_name);
            }
            break;
        case KRB5_KDCREP_MODIFIED:
            if (krb5_unparse_name(context, ctx->request->client, &client_name) == 0) {
                /*
                 * Solaris Kerberos
                 * Extra err msg for common(?) case of 
                 * 'kinit user@lower-case-def-realm'.
                 * DNS SRV recs will match (case insensitive) and trigger sendto
                 * KDC and result in this error (at least w/MSFT AD KDC).
                 */
                char *realm = strpbrk(client_name, "@");
                int set = 0;
                if (realm++) {
                    if (realm && realm[0] && is_lower_case(realm)) {
                        krb5_set_error_message(context, code,
                                               dgettext(TEXT_DOMAIN,
                                                        "KDC reply did not match expectations for client '%s': lower-case detected in realm '%s'"),
                                               client_name, realm);
                        set = 1;
                    }
                }
                if (!set)
                    krb5_set_error_message(context, code,
                                           dgettext(TEXT_DOMAIN,
                                                    "KDC reply did not match expectations for client '%s'"),                                 
                                           client_name);
                krb5_free_unparsed_name(context, client_name);
            }
            break;
        default:
            break;
        }
    }
    /* Solaris Kerberos: end spruce-up the err msg */
    krb5_free_pa_data(context, padata);
    krb5_free_pa_data(context, kdc_padata);
    krb5_free_keyblock(context, strengthen_key);
    krb5_free_keyblock_contents(context, &encrypting_key);

    return code;
}

/*
 * Do next step of credentials acquisition.
 *
 * On success returns 0 or KRB5KRB_ERR_RESPONSE_TOO_BIG if the request
 * should be sent with TCP.
 */
krb5_error_code KRB5_CALLCONV
krb5_init_creds_step(krb5_context context,
                     krb5_init_creds_context ctx,
                     krb5_data *in,
                     krb5_data *out,
                     krb5_data *realm,
                     unsigned int *flags,
		     char *hostname_used) /* Solaris Kerberos better errors */
{
    krb5_error_code code = 0, code2;

    *flags = 0;

    out->data = NULL;
    out->length = 0;

    realm->data = NULL;
    realm->length = 0;

    if (ctx->flags & KRB5_INIT_CREDS_STEP_FLAG_COMPLETE)
        goto cleanup;

    if (in->length != 0) {
        code = init_creds_step_reply(context, ctx, in, hostname_used);
        if (code == KRB5KRB_ERR_RESPONSE_TOO_BIG) {
            code2 = krb5int_copy_data_contents(context,
                                               ctx->encoded_previous_request,
                                               out);
            if (code2 != 0) {
                code = code2;
                goto cleanup;
            }
            goto copy_realm;
        }
        if (code != 0 || (ctx->flags & KRB5_INIT_CREDS_STEP_FLAG_COMPLETE))
            goto cleanup;
    }

    code = init_creds_step_request(context, ctx, out);
    if (code != 0)
        goto cleanup;

    /* Only a new request increments the loop count, not a TCP retry */
    ctx->loopcount++;

copy_realm:
    assert(ctx->request->server != NULL);

    code2 = krb5int_copy_data_contents(context,
                                       &ctx->request->server->realm,
                                       realm);
    if (code2 != 0) {
        code = code2;
        goto cleanup;
    }

cleanup:
    *flags = (ctx->flags & KRB5_INIT_CREDS_STEP_FLAG_COMPLETE) ? 0 : 1;
    return code;
}

krb5_error_code KRB5_CALLCONV
krb5int_get_init_creds(krb5_context context,
                       krb5_creds *creds,
                       krb5_principal client,
                       krb5_prompter_fct prompter,
                       void *prompter_data,
                       krb5_deltat start_time,
                       char *in_tkt_service,
                       krb5_get_init_creds_opt *options,
                       krb5_gic_get_as_key_fct gak_fct,
                       void *gak_data,
                       int  *use_master,
                       krb5_kdc_rep **as_reply)
{
    krb5_error_code code;
    krb5_init_creds_context ctx = NULL;

    code = krb5_init_creds_init(context,
                                client,
                                prompter,
                                prompter_data,
                                start_time,
                                options,
                                &ctx);
    if (code != 0)
        goto cleanup;

    ctx->gak_fct = gak_fct;
    ctx->gak_data = gak_data;

    if (in_tkt_service) {
        code = krb5_init_creds_set_service(context, ctx, in_tkt_service);
        if (code != 0)
            goto cleanup;
    }

    code = init_creds_get(context, ctx, use_master);
    if (code != 0)
        goto cleanup;

    code = krb5_init_creds_get_creds(context, ctx, creds);
    if (code != 0)
        goto cleanup;

    if (as_reply != NULL) {
        *as_reply = ctx->reply;
        ctx->reply = NULL;
    }

cleanup:
    krb5_init_creds_free(context, ctx);

    return code;
}

krb5_error_code
krb5int_populate_gic_opt(krb5_context context, krb5_get_init_creds_opt **out,
                         krb5_flags options, krb5_address *const *addrs,
                         krb5_enctype *ktypes,
                         krb5_preauthtype *pre_auth_types, krb5_creds *creds)
{
    int i;
    krb5_int32 starttime;
    krb5_get_init_creds_opt *opt;
    krb5_error_code retval;

    *out = NULL;
    retval = krb5_get_init_creds_opt_alloc(context, &opt);
    if (retval)
        return(retval);

    if (addrs)
        krb5_get_init_creds_opt_set_address_list(opt, (krb5_address **) addrs);
    if (ktypes) {
        i = krb5int_count_etypes(ktypes);
        if (i)
            krb5_get_init_creds_opt_set_etype_list(opt, ktypes, i);
    }
    if (pre_auth_types) {
        for (i=0; pre_auth_types[i]; i++);
        if (i)
            krb5_get_init_creds_opt_set_preauth_list(opt, pre_auth_types, i);
    }
    if (options&KDC_OPT_FORWARDABLE)
        krb5_get_init_creds_opt_set_forwardable(opt, 1);
    else krb5_get_init_creds_opt_set_forwardable(opt, 0);
    if (options&KDC_OPT_PROXIABLE)
        krb5_get_init_creds_opt_set_proxiable(opt, 1);
    else krb5_get_init_creds_opt_set_proxiable(opt, 0);
    if (creds && creds->times.endtime) {
        retval = krb5_timeofday(context, &starttime);
        if (retval)
            goto cleanup;
        if (creds->times.starttime) starttime = creds->times.starttime;
        krb5_get_init_creds_opt_set_tkt_life(opt, creds->times.endtime - starttime);
    }
    *out = opt;
    return 0;

cleanup:
    krb5_get_init_creds_opt_free(context, opt);
    return retval;
}
