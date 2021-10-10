/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * lib/krb5/krb/gic_keytab.c
 *
 * Copyright (C) 2002, 2003, 2008 by the Massachusetts Institute of Technology.
 * All rights reserved.
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
#ifndef LEAN_CLIENT

#include "k5-int.h"
#include "init_creds_ctx.h"
/* Solaris Kerberos */
#include <libintl.h>
#include <locale.h>

static krb5_error_code
get_as_key_keytab(krb5_context context,
                  krb5_principal client,
                  krb5_enctype etype,
                  krb5_prompter_fct prompter,
                  void *prompter_data,
                  krb5_data *salt,
                  krb5_data *params,
                  krb5_keyblock *as_key,
                  void *gak_data)
{
    krb5_keytab keytab = (krb5_keytab) gak_data;
    krb5_error_code ret;
    krb5_keytab_entry kt_ent;
    krb5_keyblock *kt_key;

    /* if there's already a key of the correct etype, we're done.
       if the etype is wrong, free the existing key, and make
       a new one. */

    if (as_key->length) {
        if (as_key->enctype == etype)
            return(0);

        krb5_free_keyblock_contents(context, as_key);
        as_key->length = 0;
    }

    if (!krb5_c_valid_enctype(etype))
        return(KRB5_PROG_ETYPE_NOSUPP);

    if ((ret = krb5_kt_get_entry(context, keytab, client,
                                 0, /* don't have vno available */
                                 etype, &kt_ent)))
        return(ret);

    ret = krb5_copy_keyblock(context, &kt_ent.key, &kt_key);

    /* again, krb5's memory management is lame... */

    *as_key = *kt_key;
    free(kt_key);

    (void) krb5_kt_free_entry(context, &kt_ent);

    return(ret);
}

krb5_error_code KRB5_CALLCONV
krb5_init_creds_set_keytab(krb5_context context,
                           krb5_init_creds_context ctx,
                           krb5_keytab keytab)
{
    ctx->gak_fct = get_as_key_keytab;
    ctx->gak_data = keytab;

    return 0;
}

krb5_error_code KRB5_CALLCONV
krb5_get_init_creds_keytab(krb5_context context,
                           krb5_creds *creds,
                           krb5_principal client,
                           krb5_keytab arg_keytab,
                           krb5_deltat start_time,
                           char *in_tkt_service,
                           krb5_get_init_creds_opt *options)
{
    krb5_error_code ret, ret2;
    int use_master;
    krb5_keytab keytab;
    /* Solaris Kerberos */
    const char *err_msg = NULL;

    if (arg_keytab == NULL) {
        if ((ret = krb5_kt_default(context, &keytab)))
            return ret;
    } else {
        keytab = arg_keytab;
    }

    /*
    * Solaris Kerberos:
    * If "client" was constructed from krb5_sname_to_princ() it may
    * have a referral realm. This happens when there is no applicable 
    * domain-to-realm mapping in the Kerberos configuration file.
    * If that is the case then the realm of the first principal found
    * in the keytab which matches the client can be used for the client's
    * realm.
    */
    if (krb5_is_referral_realm(&client->realm)) {
        krb5_data realm;
        ret = krb5_kt_find_realm(context, keytab, client, &realm);
        if (ret == 0) {
            krb5_free_data_contents(context, &client->realm);
            client->realm.length = realm.length;
            client->realm.data = realm.data;
        } else {
            /* Try to set a useful error message */
            char *princ = NULL;
            krb5_unparse_name(context, client, &princ);

            krb5_set_error_message(context, ret,
                                   gettext("Failed to find realm for %s in "
                                           "keytab"),
                                   princ ? princ : "<unknown>");
            if (princ)
                krb5_free_unparsed_name(context, princ);
	    goto cleanup;
        }
    }

    use_master = 0;

    /* first try: get the requested tkt from any kdc */

    ret = krb5int_get_init_creds(context, creds, client, NULL, NULL,
                                 start_time, in_tkt_service, options,
                                 get_as_key_keytab, (void *) keytab,
                                 &use_master,NULL);

    /* check for success */

    if (ret == 0)
        goto cleanup;

    /* If all the kdc's are unavailable fail */

    if ((ret == KRB5_KDC_UNREACH) || (ret == KRB5_REALM_CANT_RESOLVE))
        goto cleanup;

    /* if the reply did not come from the master kdc, try again with
       the master kdc */

    if (!use_master) {
        use_master = 1;

        /* Solaris Kerberos - save the original error message string */
        err_msg = krb5_get_error_message(context, ret);

        ret2 = krb5int_get_init_creds(context, creds, client, NULL, NULL,
                                      start_time, in_tkt_service, options,
                                      get_as_key_keytab, (void *) keytab,
                                      &use_master, NULL);

        if (ret2 == 0) {
            ret = 0;
            goto cleanup;
        }

        /* if the master is unreachable, return the error from the
           slave we were able to contact */

        if ((ret2 == KRB5_KDC_UNREACH) ||
            (ret2 == KRB5_REALM_CANT_RESOLVE) ||
            (ret2 == KRB5_REALM_UNKNOWN)) {

            /* Solaris Kerberos - restore the original error message string */
            krb5_set_error_message(context, ret, err_msg);

            goto cleanup;
        }

        ret = ret2;
    }

    /* at this point, we have a response from the master.  Since we don't
       do any prompting or changing for keytabs, that's it. */

cleanup:
    /* Solaris Kerberos */
    if (err_msg)
        krb5_free_error_message(context, err_msg);

    if (arg_keytab == NULL)
        krb5_kt_close(context, keytab);

    return(ret);
}
krb5_error_code KRB5_CALLCONV
krb5_get_in_tkt_with_keytab(krb5_context context, krb5_flags options,
                            krb5_address *const *addrs, krb5_enctype *ktypes,
                            krb5_preauthtype *pre_auth_types,
                            krb5_keytab arg_keytab, krb5_ccache ccache,
                            krb5_creds *creds, krb5_kdc_rep **ret_as_reply)
{
    krb5_error_code retval;
    krb5_get_init_creds_opt *opts;
    char * server = NULL;
    krb5_keytab keytab;
    krb5_principal client_princ, server_princ;
    int use_master = 0;

    retval = krb5int_populate_gic_opt(context, &opts,
                                      options, addrs, ktypes,
                                      pre_auth_types, creds);
    if (retval)
        return retval;

    if (arg_keytab == NULL) {
        retval = krb5_kt_default(context, &keytab);
        if (retval)
            goto cleanup;
    }
    else keytab = arg_keytab;

    retval = krb5_unparse_name( context, creds->server, &server);
    if (retval)
        goto cleanup;
    server_princ = creds->server;
    client_princ = creds->client;
    retval = krb5int_get_init_creds(context, creds, creds->client,
                                    krb5_prompter_posix,  NULL,
                                    0, server, opts,
                                    get_as_key_keytab, (void *)keytab,
                                    &use_master, ret_as_reply);
    krb5_free_unparsed_name( context, server);
    if (retval) {
        goto cleanup;
    }
    krb5_free_principal(context, creds->server);
    krb5_free_principal(context, creds->client);
    creds->client = client_princ;
    creds->server = server_princ;

    /* store it in the ccache! */
    if (ccache)
        if ((retval = krb5_cc_store_cred(context, ccache, creds)))
            goto cleanup;
cleanup:
    krb5_get_init_creds_opt_free(context, opts);
    if (arg_keytab == NULL)
        krb5_kt_close(context, keytab);
    return retval;
}

#endif /* LEAN_CLIENT */
