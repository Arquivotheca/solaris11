/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * lib/krb5/krb/mk_safe.c
 *
 * Copyright 1990,1991 by the Massachusetts Institute of Technology.
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
 */
/*
 * Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * krb5_mk_safe()
 */

#include "k5-int.h"
#include "cleanup.h"
#include "auth_con.h"

/* Solaris Kerberos */
#include "kerberos_dtrace.h"

/*
  Formats a KRB_SAFE message into outbuf.

  userdata is formatted as the user data in the message.
  sumtype specifies the encryption type; key specifies the key which
  might be used to seed the checksum; sender_addr and recv_addr specify
  the full addresses (host and port) of the sender and receiver.
  The host portion of sender_addr is used to form the addresses used in the
  KRB_SAFE message.

  The outbuf buffer storage is allocated, and should be freed by the
  caller when finished.

  returns system errors
*/
static krb5_error_code
krb5_mk_safe_basic(krb5_context context, const krb5_data *userdata,
                   krb5_key key, krb5_replay_data *replaydata,
                   krb5_address *local_addr, krb5_address *remote_addr,
                   krb5_cksumtype sumtype, krb5_data *outbuf)
{
    krb5_error_code retval;
    krb5_safe safemsg;
    krb5_octet zero_octet = 0;
    krb5_checksum safe_checksum;
    krb5_data *scratch1, *scratch2;

    if (!krb5_c_valid_cksumtype(sumtype))
        return KRB5_PROG_SUMTYPE_NOSUPP;
    if (!krb5_c_is_coll_proof_cksum(sumtype)
        || !krb5_c_is_keyed_cksum(sumtype))
        return KRB5KRB_AP_ERR_INAPP_CKSUM;

    safemsg.user_data = *userdata;
    safemsg.s_address = (krb5_address *) local_addr;
    safemsg.r_address = (krb5_address *) remote_addr;

    /* We should check too make sure one exists. */
    safemsg.timestamp  = replaydata->timestamp;
    safemsg.usec       = replaydata->usec;
    safemsg.seq_number = replaydata->seq;

    /*
     * To do the checksum stuff, we need to encode the message with a
     * zero-length zero-type checksum, then checksum the encoding, then
     * re-encode with the checksum.
     */

    safe_checksum.length = 0;
    safe_checksum.checksum_type = 0;
    safe_checksum.contents = &zero_octet;

    safemsg.checksum = &safe_checksum;

    if ((retval = encode_krb5_safe(&safemsg, &scratch1)))
        return retval;

    if ((retval = krb5_k_make_checksum(context, sumtype, key,
                                       KRB5_KEYUSAGE_KRB_SAFE_CKSUM,
                                       scratch1, &safe_checksum)))
        goto cleanup_checksum;

    safemsg.checksum = &safe_checksum;
    if ((retval = encode_krb5_safe(&safemsg, &scratch2))) {
        goto cleanup_checksum;
    }

    /* Solaris Kerberos */
    KERBEROS_PROBE_KRB_SAFE(MAKE, scratch2, &safemsg);

    *outbuf = *scratch2;
    free(scratch2);
    retval = 0;

cleanup_checksum:
    free(safe_checksum.contents);

    memset(scratch1->data, 0, scratch1->length);
    krb5_free_data(context, scratch1);
    return retval;
}

krb5_error_code KRB5_CALLCONV
krb5_mk_safe(krb5_context context, krb5_auth_context auth_context,
             const krb5_data *userdata, krb5_data *outbuf,
             krb5_replay_data *outdata)
{
    krb5_error_code       retval;
    krb5_key              key;
    krb5_replay_data      replaydata;

    /* Clear replaydata block */
    memset(&replaydata, 0, sizeof(krb5_replay_data));

    /* Get key */
    if ((key = auth_context->send_subkey) == NULL)
        key = auth_context->key;

    /* Get replay info */
    if ((auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_DO_TIME) &&
        (auth_context->rcache == NULL))
        return KRB5_RC_REQUIRED;

    if (((auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_RET_TIME) ||
         (auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_RET_SEQUENCE)) &&
        (outdata == NULL))
        /* Need a better error */
        return KRB5_RC_REQUIRED;

    if (!auth_context->local_addr)
        return KRB5_LOCAL_ADDR_REQUIRED;

    if ((auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_DO_TIME) ||
        (auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_RET_TIME)) {
        if ((retval = krb5_us_timeofday(context, &replaydata.timestamp,
                                        &replaydata.usec)))
            return retval;
        if (auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_RET_TIME) {
            outdata->timestamp = replaydata.timestamp;
            outdata->usec = replaydata.usec;
        }
    }
    if ((auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_DO_SEQUENCE) ||
        (auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_RET_SEQUENCE)) {
        replaydata.seq = auth_context->local_seq_number++;
        if (auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_RET_SEQUENCE)
            outdata->seq = replaydata.seq;
    }

    {
        krb5_address * premote_fulladdr = NULL;
        krb5_address * plocal_fulladdr;
        krb5_address remote_fulladdr;
        krb5_address local_fulladdr;
        krb5_cksumtype sumtype;

        CLEANUP_INIT(2);

        if (auth_context->local_port) {
            if (!(retval = krb5_make_fulladdr(context, auth_context->local_addr,
                                              auth_context->local_port,
                                              &local_fulladdr))){
                CLEANUP_PUSH(local_fulladdr.contents, free);
                plocal_fulladdr = &local_fulladdr;
            } else {
                goto error;
            }
        } else {
            plocal_fulladdr = auth_context->local_addr;
        }

        if (auth_context->remote_addr) {
            if (auth_context->remote_port) {
                if (!(retval = krb5_make_fulladdr(context,auth_context->remote_addr,
                                                  auth_context->remote_port,
                                                  &remote_fulladdr))){
                    CLEANUP_PUSH(remote_fulladdr.contents, free);
                    premote_fulladdr = &remote_fulladdr;
                } else {
                    CLEANUP_DONE();
                    goto error;
                }
            } else {
                premote_fulladdr = auth_context->remote_addr;
            }
        }

        {
            krb5_enctype enctype = krb5_k_key_enctype(context, key);
            unsigned int nsumtypes;
            unsigned int i;
            krb5_cksumtype *sumtypes;
            retval = krb5_c_keyed_checksum_types (context, enctype,
                                                  &nsumtypes, &sumtypes);
            if (retval) {
                CLEANUP_DONE ();
                goto error;
            }
            if (nsumtypes == 0) {
                retval = KRB5_BAD_ENCTYPE;
                krb5_free_cksumtypes (context, sumtypes);
                CLEANUP_DONE ();
                goto error;
            }
            /*
             * Solaris Kerberos
             *
             * Iterate thru sumtypes looking for match of safe_cksumtype.
             * If found, set sumtype to safe_cksumtype.
             * If not found, and our enctype is DES_CBC, set sumtype to the
             * corresponding cksumtype, or call krb5int_c_mandatory_cksumtype()
             * to determine and set sumtype.
             */

            for (i = 0; i < nsumtypes; i++)
                if (auth_context->safe_cksumtype == sumtypes[i])
                    break;
            krb5_free_cksumtypes (context, sumtypes);
            if (i < nsumtypes)
                sumtype = auth_context->safe_cksumtype;
            else {
                switch (enctype) {
                case ENCTYPE_DES_CBC_MD4:
                    sumtype = CKSUMTYPE_RSA_MD4_DES;
                    break;
                case ENCTYPE_DES_CBC_MD5:
                case ENCTYPE_DES_CBC_CRC:
                    sumtype = CKSUMTYPE_RSA_MD5_DES;
                    break;
                default:
                    retval = krb5int_c_mandatory_cksumtype(context,
                                                           enctype,
                                                           &sumtype);
                    if (retval) {
                        CLEANUP_DONE();
                        goto error;
                    }
                    break;
                }
            }
        }
        if ((retval = krb5_mk_safe_basic(context, userdata, key, &replaydata,
                                         plocal_fulladdr, premote_fulladdr,
                                         sumtype, outbuf))) {
            CLEANUP_DONE();
            goto error;
        }

        CLEANUP_DONE();
    }

    if (auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_DO_TIME) {
        krb5_donot_replay replay;

        if ((retval = krb5_gen_replay_name(context, auth_context->local_addr,
                                           "_safe", &replay.client))) {
            free(outbuf);
            goto error;
        }

        replay.server = "";             /* XXX */
        replay.msghash = NULL;
        replay.cusec = replaydata.usec;
        replay.ctime = replaydata.timestamp;
        if ((retval = krb5_rc_store(context, auth_context->rcache, &replay))) {
            /* should we really error out here? XXX */
            free(outbuf);
            free(replay.client); /* Solaris Kerberos: fix memleak */
            goto error;
        }
        free(replay.client);
    }

    return 0;

error:
    if ((auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_DO_SEQUENCE) ||
        (auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_RET_SEQUENCE))
        auth_context->local_seq_number--;

    return retval;
}
