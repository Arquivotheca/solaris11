/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * lib/krb5/krb/srv_rcache.c
 *
 * Copyright 1991 by the Massachusetts Institute of Technology.
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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Allocate & prepare a default replay cache for a server.
 */

#include "k5-int.h"
#include <ctype.h>
#include <stdio.h>

/* Macro for valid RC name characters*/
#define isvalidrcname(x) ((!ispunct(x))&&isgraph(x))
krb5_error_code KRB5_CALLCONV
krb5_get_server_rcache(krb5_context context, const krb5_data *piece,
                       krb5_rcache *rcptr)
{
    krb5_rcache rcache = 0;
    char *cachename = 0, *cachetype;
    krb5_error_code retval;
    unsigned int i;
    struct k5buf buf;
    char *def_env = 0; /* Solaris Kerberos */
#ifdef HAVE_GETEUID
    unsigned long uid = geteuid();
#endif

    if (piece == NULL)
        return ENOMEM;

    cachetype = krb5_rc_default_type(context);

    krb5int_buf_init_dynamic(&buf);
    /*
     * Solaris Kerberos: Check to see if something other than the default
     * replay cache name will be used.  If so then skip over the construction
     * of said name.
     */
    if ((def_env = krb5_rc_default_name(context)) != 0) {
        /*
         * We expect to have the fully qualified rcache name (<type>:<name>),
         * so we populate the default type here if the type is missing.
         */
        if (strchr(def_env, ':') == NULL)
            krb5int_buf_add_fmt(&buf, "%s:%s", cachetype, def_env);
        else
            krb5int_buf_add(&buf, def_env);
    } else {
        /* Standard MIT 1.8 code */
        krb5int_buf_add(&buf, cachetype);
        krb5int_buf_add(&buf, ":");
        for (i = 0; i < piece->length; i++) {
            if (piece->data[i] == '-')
                krb5int_buf_add(&buf, "--");
            else if (!isvalidrcname((int) piece->data[i]))
                krb5int_buf_add_fmt(&buf, "-%03o", piece->data[i]);
            else
                krb5int_buf_add_len(&buf, &piece->data[i], 1);
        }
#ifdef HAVE_GETEUID
        krb5int_buf_add_fmt(&buf, "_%lu", uid);
#endif
    }

    cachename = krb5int_buf_data(&buf);
    if (cachename == NULL)
        return ENOMEM;

    retval = krb5_rc_resolve_full(context, &rcache, cachename);
    if (retval)
        goto cleanup;

    retval = krb5_rc_recover_or_initialize(context, rcache,
                                           context->clockskew);
    if (retval)
        goto cleanup;

    *rcptr = rcache;
    rcache = 0;
    retval = 0;

cleanup:
    if (rcache)
        krb5_rc_close(context, rcache);
    if (cachename)
        free(cachename);
    return retval;
}
