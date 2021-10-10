/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * lib/krb5/krb/gc_via_tgt.c
 *
 * Copyright 1990,1991,2007-2009 by the Massachusetts Institute of Technology.
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
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Given a tkt, and a target cred, get it.
 * Assumes that the kdc_rep has been decrypted.
 */

#include "k5-int.h"
#include "int-proto.h"
/* Solaris Kerberos */
#include <locale.h>
#include <ctype.h>

static krb5_error_code
kdcrep2creds(krb5_context context, krb5_kdc_rep *pkdcrep, krb5_address *const *address,
             krb5_data *psectkt, krb5_creds **ppcreds)
{
    krb5_error_code retval;
    krb5_data *pdata;

    if ((*ppcreds = (krb5_creds *)calloc(1,sizeof(krb5_creds))) == NULL) {
        return ENOMEM;
    }

    if ((retval = krb5_copy_principal(context, pkdcrep->client,
                                      &(*ppcreds)->client)))
        goto cleanup;

    if ((retval = krb5_copy_principal(context, pkdcrep->enc_part2->server,
                                      &(*ppcreds)->server)))
        goto cleanup;

    if ((retval = krb5_copy_keyblock_contents(context,
                                              pkdcrep->enc_part2->session,
                                              &(*ppcreds)->keyblock)))
        goto cleanup;

    if ((retval = krb5_copy_data(context, psectkt, &pdata)))
        goto cleanup_keyblock;
    (*ppcreds)->second_ticket = *pdata;
    free(pdata);

    (*ppcreds)->ticket_flags = pkdcrep->enc_part2->flags;
    (*ppcreds)->times = pkdcrep->enc_part2->times;
    (*ppcreds)->magic = KV5M_CREDS;

    (*ppcreds)->authdata = NULL;                        /* not used */
    (*ppcreds)->is_skey = psectkt->length != 0;

    if (pkdcrep->enc_part2->caddrs) {
        if ((retval = krb5_copy_addresses(context, pkdcrep->enc_part2->caddrs,
                                          &(*ppcreds)->addresses)))
            goto cleanup_keyblock;
    } else {
        /* no addresses in the list means we got what we had */
        if ((retval = krb5_copy_addresses(context, address,
                                          &(*ppcreds)->addresses)))
            goto cleanup_keyblock;
    }

    if ((retval = encode_krb5_ticket(pkdcrep->ticket, &pdata)))
        goto cleanup_keyblock;

    (*ppcreds)->ticket = *pdata;
    free(pdata);
    return 0;

cleanup_keyblock:
    krb5_free_keyblock_contents(context, &(*ppcreds)->keyblock);

cleanup:
    free (*ppcreds);
    *ppcreds = NULL;
    return retval;
}

static krb5_error_code
check_reply_server(krb5_context context, krb5_flags kdcoptions,
                   krb5_creds *in_cred, krb5_kdc_rep *dec_rep)
{

    if (!krb5_principal_compare(context, dec_rep->ticket->server,
                                dec_rep->enc_part2->server))
        return KRB5_KDCREP_MODIFIED;

    /* Reply is self-consistent. */

    if (krb5_principal_compare(context, dec_rep->ticket->server,
                               in_cred->server))
        return 0;

    /* Server in reply differs from what we requested. */

    if (kdcoptions & KDC_OPT_CANONICALIZE) {
        /* in_cred server differs from ticket returned, but ticket
           returned is consistent and we requested canonicalization. */
#if 0
#ifdef DEBUG_REFERRALS
        printf("gc_via_tkt: in_cred and encoding don't match but referrals requested\n");
        krb5int_dbgref_dump_principal("gc_via_tkt: in_cred",in_cred->server);
        krb5int_dbgref_dump_principal("gc_via_tkt: encoded server",dec_rep->enc_part2->server);
#endif
#endif
        return 0;
    }

    /* We didn't request canonicalization. */

    if (!IS_TGS_PRINC(context, in_cred->server) ||
        !IS_TGS_PRINC(context, dec_rep->ticket->server)) {
        /* Canonicalization not requested, and not a TGS referral. */
        return KRB5_KDCREP_MODIFIED;
    }
#if 0
    /*
     * Is this check needed?  find_nxt_kdc() in gc_frm_kdc.c already
     * effectively checks this.
     */
    if (krb5_realm_compare(context, in_cred->client, in_cred->server) &&
        data_eq(*in_cred->server->data[1], *in_cred->client->realm)) {
        /* Attempted to rewrite local TGS. */
        return KRB5_KDCREP_MODIFIED;
    }
#endif
    return 0;
}

/* Return true if a TGS credential is for the client's local realm. */
static inline int
tgt_is_local_realm(krb5_creds *tgt)
{
    return (tgt->server->length == 2
            && data_eq_string(tgt->server->data[0], KRB5_TGS_NAME)
            && data_eq(tgt->server->data[1], tgt->client->realm)
            && data_eq(tgt->server->realm, tgt->client->realm));
}

krb5_error_code
krb5_get_cred_via_tkt(krb5_context context, krb5_creds *tkt,
                      krb5_flags kdcoptions, krb5_address *const *address,
                      krb5_creds *in_cred, krb5_creds **out_cred)
{
    return krb5_get_cred_via_tkt_ext (context, tkt,
                                      kdcoptions, address,
                                      NULL, in_cred, NULL, NULL,
                                      NULL, NULL, out_cred, NULL);
}

/*
 * Solaris Kerberos 
 * Modified to return the krb5_kdc_req associated with request_data
 * for use by the DTrace probes.
 */
krb5_error_code
krb5int_make_tgs_request(krb5_context context,
                         krb5_creds *tkt,
                         krb5_flags kdcoptions,
                         krb5_address *const *address,
                         krb5_pa_data **in_padata,
                         krb5_creds *in_cred,
                         krb5_error_code (*pacb_fct)(krb5_context,
                                                     krb5_keyblock *,
                                                     krb5_kdc_req *,
                                                     void *),
                         void *pacb_data,
                         krb5_data *request_data,
                         krb5_timestamp *timestamp,
                         krb5_int32 *nonce,
                         krb5_keyblock **subkey,
                         krb5_kdc_req **retreq)
{
    krb5_error_code retval;
    krb5_enctype *enctypes = NULL;
    krb5_boolean second_tkt;

    request_data->data = NULL;
    *timestamp = 0;
    *subkey = NULL;

    /* tkt->client must be equal to in_cred->client */
    if (!krb5_principal_compare(context, tkt->client, in_cred->client)) {
        /* Solaris Kerberos */
        char *r_name = NULL;
        char *t_name = NULL;
        krb5_error_code r_err, t_err;
        t_err = krb5_unparse_name(context, tkt->client, &t_name);
        r_err = krb5_unparse_name(context, in_cred->client, &r_name);
        krb5_set_error_message(context, KRB5_PRINC_NOMATCH,
                               dgettext(TEXT_DOMAIN,
                                        "Requested principal and ticket don't"
                                        " match:  Requested principal is '%s'"
                                        " and ticket is '%s'"),
                               r_err ? "unknown" : r_name,
                               t_err ? "unknown" : t_name);
        if (r_name)
            krb5_free_unparsed_name(context, r_name);
        if (t_name)
            krb5_free_unparsed_name(context, t_name);
        return KRB5_PRINC_NOMATCH;
    }

    if (!tkt->ticket.length)
        return KRB5_NO_TKT_SUPPLIED;

    second_tkt = ((kdcoptions & (KDC_OPT_ENC_TKT_IN_SKEY |
                                 KDC_OPT_CNAME_IN_ADDL_TKT)) != 0);
    if (second_tkt && !in_cred->second_ticket.length)
        return KRB5_NO_2ND_TKT;

    if (in_cred->keyblock.enctype) {
        enctypes = (krb5_enctype *)malloc(sizeof(krb5_enctype)*2);
        if (enctypes == NULL)
            return ENOMEM;
        enctypes[0] = in_cred->keyblock.enctype;
        enctypes[1] = 0;
    }

    /* Solaris Kerberos */
    retval = krb5int_make_tgs_request_ext(context, kdcoptions, &in_cred->times,
                                          enctypes, in_cred->server, address,
                                          in_cred->authdata, in_padata,
                                          second_tkt ?
                                          &in_cred->second_ticket : 0,
                                          tkt, pacb_fct, pacb_data,
                                          request_data,
                                          timestamp, nonce, subkey, retreq);
    if (enctypes != NULL)
        free(enctypes);

    return retval;
}

krb5_error_code
krb5int_process_tgs_reply(krb5_context context,
                          krb5_data *response_data,
                          krb5_creds *tkt,
                          krb5_flags kdcoptions,
                          krb5_address *const *address,
                          krb5_pa_data **in_padata,
                          krb5_creds *in_cred,
                          krb5_timestamp timestamp,
                          krb5_int32 nonce,
                          krb5_keyblock *subkey,
                          krb5_pa_data ***out_padata,
                          krb5_pa_data ***out_enc_padata,
                          krb5_creds **out_cred,
                          const char *hostname_used)
{
    krb5_error_code retval;
    krb5_kdc_rep *dec_rep = NULL;
    krb5_error *err_reply = NULL;
    krb5_boolean s4u2self;

    s4u2self = krb5int_find_pa_data(context, in_padata,
                                    KRB5_PADATA_S4U_X509_USER) ||
        krb5int_find_pa_data(context, in_padata,
                             KRB5_PADATA_FOR_USER);

    if (krb5_is_krb_error(response_data)) {
        retval = decode_krb5_error(response_data, &err_reply);
        if (retval != 0)
            goto cleanup;
        retval = (krb5_error_code) err_reply->error + ERROR_TABLE_BASE_krb5;
        if (err_reply->text.length > 0) {
            switch (err_reply->error) {
            case KRB_ERR_GENERIC:
                krb5_set_error_message(context, retval,
                                       /* Solaris Kerberos - added dgettext */
                                       dgettext(TEXT_DOMAIN,
                                       "KDC returned error string: %.*s"),
                                       err_reply->text.length,
                                       err_reply->text.data);
                break;
            case KDC_ERR_S_PRINCIPAL_UNKNOWN:
            {
                char *s_name;
                if (err_reply->server &&
                    krb5_unparse_name(context, err_reply->server, &s_name) == 0) {
                    krb5_set_error_message(context, retval,
                                           /*
                                            * Solaris Kerberos - added dgettext
                                            */
                                           dgettext(TEXT_DOMAIN,
                                           "Server %s not found in Kerberos "
                                           "database"),
                                           s_name);
                    krb5_free_unparsed_name(context, s_name);
                } else
                    /* In case there's a stale S_PRINCIPAL_UNKNOWN
                       report already noted.  */
                    krb5_clear_error_message(context);
            }
            break;
	    case KRB_AP_ERR_SKEW:
		/* Solaris Kerberos */
                {
                    char *s_name = NULL;
                    char *c_name = NULL;
		    char stimestring[17];
		    char ctimestring[17];
		    char fill = ' ';
		    int st_err, ct_err, serr, cerr;

		    st_err = krb5_timestamp_to_sfstring(err_reply->stime,
							stimestring,
							sizeof (stimestring),
							&fill);
		    ct_err = krb5_timestamp_to_sfstring(err_reply->ctime,
							ctimestring,
							sizeof (ctimestring),
							&fill);
                    serr = krb5_unparse_name(context, in_cred->server, &s_name);
                    cerr = krb5_unparse_name(context, in_cred->client, &c_name);
		    krb5_set_error_message(context, retval,
					dgettext(TEXT_DOMAIN,
						"Clock skew too great: '%s' "
                                                "requesting ticket '%s' from "
                                                "KDC '%s' (%s). Skew is %dm."),
					cerr == 0 ? c_name : "unknown",
					serr == 0 ? s_name : "unknown",
					hostname_used ?
					  hostname_used : "host unknown",
					st_err == 0 ? stimestring : "unknown",
					(ct_err||st_err) ? 0 :
					    abs(err_reply->stime -
					        err_reply->ctime) / 60);

		    if (s_name)
			    krb5_free_unparsed_name(context, s_name);
		    if (c_name)
			    krb5_free_unparsed_name(context, c_name);
                }
	        break;
	    case KRB_AP_ERR_TKT_NYV:
		/* Solaris Kerberos */
                {
                    char *s_name = NULL;
                    char *c_name = NULL;
		    char timestring[17];
		    char stimestring[17];
		    char fill = ' ';
		    krb5_error_code t_err, st_err, cerr, serr;

		    t_err = krb5_timestamp_to_sfstring(tkt->times.starttime,
						    timestring,
						    sizeof (timestring),
						    &fill);
		    st_err = krb5_timestamp_to_sfstring(err_reply->stime,
							stimestring,
							sizeof (stimestring),
							&fill);
                    serr = krb5_unparse_name(context, in_cred->server, &s_name);
                    cerr = krb5_unparse_name(context, in_cred->client, &c_name);
		    krb5_set_error_message(context, retval,
					dgettext(TEXT_DOMAIN,
						"Ticket not yet valid: '%s' "
                                                "requesting ticket '%s' from "
                                                "'%s' (%s). TGT start time is %s"),
					cerr ? "unknown" : c_name,
					serr ? "unknown" : s_name,
					hostname_used ? hostname_used : "host unknown",
					st_err ? "unknown" : stimestring,
					t_err ? "unknown" : timestring);
		    if (s_name)
		        krb5_free_unparsed_name(context, s_name);
		    if (c_name)
		        krb5_free_unparsed_name(context, c_name);
                }
	        break;
            }
        }
        krb5_free_error(context, err_reply);
        goto cleanup;
    } else if (!krb5_is_tgs_rep(response_data)) {
        retval = KRB5KRB_AP_ERR_MSG_TYPE;
        goto cleanup;
    }

    /* Unfortunately, Heimdal at least up through 1.2  encrypts using
       the session key not the subsession key.  So we try both. */
    retval = krb5int_decode_tgs_rep(context, response_data,
                                    subkey,
                                    KRB5_KEYUSAGE_TGS_REP_ENCPART_SUBKEY,
                                    &dec_rep);
    if (retval) {
        if ((krb5int_decode_tgs_rep(context, response_data,
                                    &tkt->keyblock,
                                    KRB5_KEYUSAGE_TGS_REP_ENCPART_SESSKEY, &dec_rep)) == 0)
            retval = 0;
        else
            goto cleanup;
    }

    if (dec_rep->msg_type != KRB5_TGS_REP) {
        retval = KRB5KRB_AP_ERR_MSG_TYPE;
        goto cleanup;
    }

    /*
     * Don't trust the ok-as-delegate flag from foreign KDCs unless the
     * cross-realm TGT also had the ok-as-delegate flag set.
     */
    if (!tgt_is_local_realm(tkt)
        && !(tkt->ticket_flags & TKT_FLG_OK_AS_DELEGATE))
        dec_rep->enc_part2->flags &= ~TKT_FLG_OK_AS_DELEGATE;

    /* make sure the response hasn't been tampered with..... */
    retval = 0;

    if (s4u2self && !IS_TGS_PRINC(context, dec_rep->ticket->server)) {
        /* Final hop, check whether KDC supports S4U2Self */
        if (krb5_principal_compare(context, dec_rep->client, in_cred->server))
            retval = KRB5KDC_ERR_PADATA_TYPE_NOSUPP;
    } else if ((kdcoptions & KDC_OPT_CNAME_IN_ADDL_TKT) == 0) {
        /* XXX for constrained delegation this check must be performed by caller
         * as we don't have access to the key to decrypt the evidence ticket.
         */
        if (!krb5_principal_compare(context, dec_rep->client, tkt->client))
            retval = KRB5_KDCREP_MODIFIED;
    }

    if (retval == 0)
        retval = check_reply_server(context, kdcoptions, in_cred, dec_rep);

    if (dec_rep->enc_part2->nonce != nonce)
        retval = KRB5_KDCREP_MODIFIED;

    if ((kdcoptions & KDC_OPT_POSTDATED) &&
        (in_cred->times.starttime != 0) &&
        (in_cred->times.starttime != dec_rep->enc_part2->times.starttime))
        retval = KRB5_KDCREP_MODIFIED;

    if ((in_cred->times.endtime != 0) &&
        (dec_rep->enc_part2->times.endtime > in_cred->times.endtime))
        retval = KRB5_KDCREP_MODIFIED;

    if ((kdcoptions & KDC_OPT_RENEWABLE) &&
        (in_cred->times.renew_till != 0) &&
        (dec_rep->enc_part2->times.renew_till > in_cred->times.renew_till))
        retval = KRB5_KDCREP_MODIFIED;

    if ((kdcoptions & KDC_OPT_RENEWABLE_OK) &&
        (dec_rep->enc_part2->flags & KDC_OPT_RENEWABLE) &&
        (in_cred->times.endtime != 0) &&
        (dec_rep->enc_part2->times.renew_till > in_cred->times.endtime))
        retval = KRB5_KDCREP_MODIFIED;

    if (retval != 0)
        goto cleanup;

    if (!in_cred->times.starttime &&
        !in_clock_skew(dec_rep->enc_part2->times.starttime,
                       timestamp)) {
        retval = KRB5_KDCREP_SKEW;
        goto cleanup;
    }

    if (out_padata != NULL) {
        *out_padata = dec_rep->padata;
        dec_rep->padata = NULL;
    }
    if (out_enc_padata != NULL) {
        *out_enc_padata = dec_rep->enc_part2->enc_padata;
        dec_rep->enc_part2->enc_padata = NULL;
    }

    retval = kdcrep2creds(context, dec_rep, address,
                          &in_cred->second_ticket, out_cred);
    if (retval != 0)
        goto cleanup;

cleanup:
    if (dec_rep != NULL) {
        memset(dec_rep->enc_part2->session->contents, 0,
               dec_rep->enc_part2->session->length);
        krb5_free_kdc_rep(context, dec_rep);
    }

    return retval;
}

krb5_error_code
krb5_get_cred_via_tkt_ext(krb5_context context, krb5_creds *tkt,
                          krb5_flags kdcoptions, krb5_address *const *address,
                          krb5_pa_data **in_padata,
                          krb5_creds *in_cred,
                          krb5_error_code (*pacb_fct)(krb5_context,
                                                      krb5_keyblock *,
                                                      krb5_kdc_req *,
                                                      void *),
                          void *pacb_data,
                          krb5_pa_data ***out_padata,
                          krb5_pa_data ***out_enc_padata,
                          krb5_creds **out_cred,
                          krb5_keyblock **out_subkey)
{
    krb5_error_code retval;
    krb5_data request_data;
    krb5_data response_data;
    krb5_timestamp timestamp;
    krb5_int32 nonce;
    krb5_keyblock *subkey = NULL;
    int tcp_only = 0, use_master = 0;
    char *hostname_used = NULL;

    request_data.data = NULL;
    request_data.length = 0;
    response_data.data = NULL;
    response_data.length = 0;

    /* Solaris Kerberos */
    krb5_kdc_req *retreq = NULL;

#ifdef DEBUG_REFERRALS
    printf("krb5_get_cred_via_tkt starting; referral flag is %s\n", kdcoptions&KDC_OPT_CANONICALIZE?"on":"off");
    krb5int_dbgref_dump_principal("krb5_get_cred_via_tkt requested ticket", in_cred->server);
    krb5int_dbgref_dump_principal("krb5_get_cred_via_tkt TGT in use", tkt->server);
#endif

    retval = krb5int_make_tgs_request(context, tkt, kdcoptions,
                                      address, in_padata, in_cred,
                                      pacb_fct, pacb_data,
                                      &request_data, &timestamp, &nonce,
                                      &subkey, &retreq);
    if (retval != 0)
        goto cleanup;

send_again:
    use_master = 0;
   /*
    * Solaris Kerberos:
    * want hostname_used for better errors and retreq for DTrace
    */
    retval = krb5_sendto_kdc(context, &request_data, retreq,
                             krb5_princ_realm(context, in_cred->server),
                             &response_data, &use_master, tcp_only,
                             &hostname_used);
    /* Solaris Kerberos */
    if (retreq != NULL) {
        krb5_free_kdc_req(context, retreq);
        retreq = NULL;
    }

    if (retval == 0) {
        if (krb5_is_krb_error(&response_data)) {
            if (!tcp_only) {
                krb5_error *err_reply;
                retval = decode_krb5_error(&response_data, &err_reply);
                if (retval != 0)
                    goto cleanup;
                if (err_reply->error == KRB_ERR_RESPONSE_TOO_BIG) {
                    tcp_only = 1;
                    krb5_free_error(context, err_reply);
                    krb5_free_data_contents(context, &response_data);
                    goto send_again;
                }
                krb5_free_error(context, err_reply);
            }
        }
    } else
        goto cleanup;

    /* Solaris Kerberos: want hostname_used for better errors */
    retval = krb5int_process_tgs_reply(context, &response_data,
                                       tkt, kdcoptions, address,
                                       in_padata, in_cred,
                                       timestamp, nonce, subkey,
                                       out_padata,
                                       out_enc_padata, out_cred,
                                       hostname_used);
    if (retval != 0)
        goto cleanup;

cleanup:
#ifdef DEBUG_REFERRALS
    printf("krb5_get_cred_via_tkt ending; %s\n", retval?error_message(retval):"no error");
#endif

    krb5_free_data_contents(context, &request_data);
    krb5_free_data_contents(context, &response_data);

    if (subkey != NULL) {
        if (retval == 0 && out_subkey != NULL)
            *out_subkey = subkey;
        else
            krb5_free_keyblock(context, subkey);
    }

    /* Solaris Kerberos */
    if (hostname_used != NULL)
        free(hostname_used);

    return retval;
}
