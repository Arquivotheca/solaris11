/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * lib/krb5/krb/set_realm.c
 *
 * Copyright 1997 by the Massachusetts Institute of Technology.
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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "k5-int.h"

krb5_error_code KRB5_CALLCONV
krb5_set_principal_realm(krb5_context context, krb5_principal principal, const char *realm)
{
    size_t  length;
    char    *newrealm;

    if (!realm || !*realm)
        return EINVAL; /* Solaris Kerberos */

    length = strlen(realm);
    newrealm = strdup(realm);
    if (!newrealm)
        return ENOMEM; /* Solaris Kerberos */

    (void) free(krb5_princ_realm(context,principal)->data);

    krb5_princ_realm(context, principal)->length = length;
    krb5_princ_realm(context, principal)->data = newrealm;

    return 0;
}
