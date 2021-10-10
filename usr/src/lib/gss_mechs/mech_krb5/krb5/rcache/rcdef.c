/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */

/*
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * lib/krb5/rcache/rcdef.c
 *
 * Copyright 1990 by the Massachusetts Institute of Technology.
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
 * replay cache default set of operations vectors.
 */

#include "k5-int.h"
#include "rc-int.h"
#include "rc_dfl.h"

/* Solaris Kerberos */
#include "rc_mem.h"

/*
 * Solaris Kerberos
 * MIT just has "dfl" while we now have "FILE" and "MEMORY".
 */
const krb5_rc_ops krb5_rc_dfl_ops = {
    0,
    "FILE",
    krb5_rc_dfl_init,
    krb5_rc_dfl_recover,
    krb5_rc_dfl_recover_or_init,
    krb5_rc_dfl_destroy,
    krb5_rc_dfl_close,
    krb5_rc_dfl_store,
    krb5_rc_dfl_expunge,
    krb5_rc_dfl_get_span,
    krb5_rc_dfl_get_name,
    krb5_rc_dfl_resolve
};

const krb5_rc_ops krb5_rc_mem_ops = {
    0,
    "MEMORY",
    krb5_rc_mem_init,
    krb5_rc_mem_recover,
    krb5_rc_mem_recover_or_init,
    krb5_rc_mem_destroy,
    krb5_rc_mem_close,
    krb5_rc_mem_store,
    /* expunging not used in memory rcache type */
    NULL,
    krb5_rc_mem_get_span,
    krb5_rc_mem_get_name,
    krb5_rc_mem_resolve
};
