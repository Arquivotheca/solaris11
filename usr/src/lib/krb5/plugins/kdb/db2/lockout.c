/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * plugins/kdb/db2/lockout.c
 *
 * Copyright (C) 2009 by the Massachusetts Institute of Technology.
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
 *
 *
 *
 */

#include "k5-int.h"
#include "kdb.h"
#include <stdio.h>
#include <errno.h>
#include <kadm5/server_internal.h>
#include "kdb_db2.h"

/*
 * Helper routines for databases that wish to use the default
 * principal lockout functionality.
 */

static krb5_error_code
lookup_lockout_policy(krb5_context context,
                      krb5_db_entry *entry,
                      krb5_kvno *pw_max_fail,
                      krb5_deltat *pw_failcnt_interval,
                      krb5_deltat *pw_lockout_duration)
{
    krb5_tl_data tl_data;
    krb5_error_code code;
    osa_princ_ent_rec adb;
    XDR xdrs;

    *pw_max_fail = 0;
    *pw_failcnt_interval = 0;
    *pw_lockout_duration = 0;

    tl_data.tl_data_type = KRB5_TL_KADM_DATA;

    code = krb5_dbe_lookup_tl_data(context, entry, &tl_data);
    if (code != 0 || tl_data.tl_data_length == 0)
        return code;

    memset(&adb, 0, sizeof(adb));
    xdrmem_create(&xdrs, (char *)tl_data.tl_data_contents,
                  tl_data.tl_data_length, XDR_DECODE);
    if (!xdr_osa_princ_ent_rec(&xdrs, &adb)) {
        xdr_destroy(&xdrs);
        return KADM5_XDR_FAILURE;
    }

    if (adb.policy != NULL) {
        osa_policy_ent_t policy = NULL;
        int count = 0;

        code = krb5_db2_get_policy(context, adb.policy,
                                   &policy, &count);
        if (code == 0 && count == 1) {
            *pw_max_fail = policy->pw_max_fail;
            *pw_failcnt_interval = policy->pw_failcnt_interval;
            *pw_lockout_duration = policy->pw_lockout_duration;
        }
        if (policy != NULL)
            krb5_db2_free_policy(context, policy);
    }

    xdr_destroy(&xdrs);

    xdrmem_create(&xdrs, NULL, 0, XDR_FREE);
    xdr_osa_princ_ent_rec(&xdrs, &adb);
    xdr_destroy(&xdrs);

    return 0;
}

/* draft-behera-ldap-password-policy-10.txt 7.1 */
static krb5_boolean
locked_check_p(krb5_context context,
               krb5_timestamp stamp,
               krb5_kvno max_fail,
               krb5_timestamp lockout_duration,
               krb5_db_entry *entry)
{
    if (max_fail == 0 || entry->fail_auth_count < max_fail)
        return FALSE;

    if (lockout_duration == 0)
        return TRUE; /* principal permanently locked */

    return (stamp < entry->last_failed + lockout_duration);
}

krb5_error_code
krb5_db2_lockout_check_policy(krb5_context context,
                              krb5_db_entry *entry,
                              krb5_timestamp stamp)
{
    krb5_error_code code;
    krb5_kvno max_fail = 0;
    krb5_deltat failcnt_interval = 0;
    krb5_deltat lockout_duration = 0;

    code = lookup_lockout_policy(context, entry, &max_fail,
                                 &failcnt_interval,
                                 &lockout_duration);
    if (code != 0)
        return code;

    if (locked_check_p(context, stamp, max_fail, lockout_duration, entry))
        return KRB5KDC_ERR_CLIENT_REVOKED;

    return 0;
}

krb5_error_code
krb5_db2_lockout_audit(krb5_context context,
                       krb5_db_entry *entry,
                       krb5_timestamp stamp,
                       krb5_error_code status)
{
    krb5_error_code code;
    krb5_kvno max_fail = 0;
    krb5_deltat failcnt_interval = 0;
    krb5_deltat lockout_duration = 0;
    int nentries = 1;

    switch (status) {
    case 0:
    case KRB5KDC_ERR_PREAUTH_FAILED:
    case KRB5KRB_AP_ERR_BAD_INTEGRITY:
        break;
#if 0
    case KRB5KDC_ERR_CLIENT_REVOKED:
        break;
#endif
    default:
        return 0;
    }

    code = lookup_lockout_policy(context, entry, &max_fail,
                                 &failcnt_interval,
                                 &lockout_duration);
    if (code != 0)
        return code;

    assert (!locked_check_p(context, stamp, max_fail, lockout_duration, entry));

    if (status == 0 && (entry->attributes & KRB5_KDB_REQUIRES_PRE_AUTH)) {
        /*
         * Only mark the authentication as successful if the entry
         * required preauthentication, otherwise we have no idea.
         */
        entry->fail_auth_count = 0;
        entry->last_success = stamp;
    } else if (status == KRB5KDC_ERR_PREAUTH_FAILED ||
               status == KRB5KRB_AP_ERR_BAD_INTEGRITY) {
        if (failcnt_interval != 0 &&
            stamp > entry->last_failed + failcnt_interval) {
            /* Reset fail_auth_count after failcnt_interval */
            entry->fail_auth_count = 0;
        }

        entry->last_failed = stamp;
        entry->fail_auth_count++;
    } else
        return 0; /* nothing to do */

    code = krb5_db2_db_put_principal(context, entry,
                                     &nentries, NULL);
    if (code != 0)
        return code;

    return 0;
}
