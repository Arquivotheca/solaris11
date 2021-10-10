/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * lib/crypto/keyblocks.c
 *
 * Copyright (C) 2002, 2005 by the Massachusetts Institute of Technology.
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
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * krb5_init_keyblock- a function to set up
 *  an empty keyblock
 */

/* Solaris Kerberos changes in here for kernel & user space mechs */

#include "k5-int.h"
#ifndef _KERNEL /* Solaris Kerberos */
#include <assert.h>

krb5_error_code
krb5int_c_init_keyblock(krb5_context context, krb5_enctype enctype,
                        size_t length, krb5_keyblock **out)
{
    krb5_keyblock *kb;

    assert(out);
    *out = NULL;

    /* Solaris Kerberos must use MALLOC (kernel/user space compat) */
    kb = malloc(sizeof(krb5_keyblock));
    if (kb == NULL)
        return ENOMEM;
    kb->magic = KV5M_KEYBLOCK;
    kb->enctype = enctype;
    kb->length = length;
    if (length) {
        kb->contents = MALLOC(length);
        if (!kb->contents) {
            free(kb);
            return ENOMEM;
        }
    } else {
        kb->contents = NULL;
    }

    /* Solaris Kerberos begin our keyblock differs */
    kb->dk_list = NULL;
    kb->hKey = CK_INVALID_HANDLE;
    /* Solaris Kerberos end */

    *out = kb;
    return 0;
}
#endif

void
krb5int_c_free_keyblock(krb5_context context, register krb5_keyblock *val)
{
    if (!val)
        return;

    krb5int_c_free_keyblock_contents(context, val);
    FREE(val, sizeof(krb5_keyblock));
}

void
krb5int_c_free_keyblock_contents(krb5_context context, krb5_keyblock *key)
{
    if (key && key->contents) {
        (void) memset(key->contents, 0, key->length);
        zapfree(key->contents, key->length);
        key->length = 0;
        key->contents = NULL;
    }
    /*
     * Solaris Kerberos to support kernel and user space krb mechs and using
     * crypto framework
     */
#ifdef _KERNEL
    if (key->key_tmpl != NULL)
        (void) crypto_destroy_ctx_template(key->key_tmpl);
#else
    if (key->hKey != CK_INVALID_HANDLE) {
        CK_RV rv;
        rv = C_DestroyObject(krb_ctx_hSession(context), key->hKey);
        key->hKey = CK_INVALID_HANDLE;
    }
#endif /* _KERNEL */
    /*
     * If the original key data is freed, we should also free
     * any keys derived from that data.
     * This saves us from making additional calls to "cleanup_dk_list"
     * in all of the many function which have keyblock structures
     * declared on the stack that re-use the keyblock data contents
     * without freeing the entire keyblock record.
     */
    cleanup_dk_list(context, key);
}

krb5_error_code
krb5int_c_copy_keyblock(krb5_context context, const krb5_keyblock *from,
                        krb5_keyblock **to)
{
    krb5_keyblock *new_key;
    krb5_error_code code;

    *to = NULL;
    new_key = MALLOC(sizeof(*new_key));
    if (!new_key)
        return ENOMEM;
    code = krb5int_c_copy_keyblock_contents(context, from, new_key);
    if (code) {
        FREE(new_key, sizeof(*new_key));
        return code;
    }
    *to = new_key;
    return 0;
}

/*
 * Solaris Kerberos:
 * krb5int_copy_solaris_keyblock_data
 *
 * Utility for copying Solaris keyblock data structures safely.  This assumes
 * that the necessary storage areas are already allocated.
 */
krb5_error_code
krb5int_copy_solaris_keyblock_data(krb5_context context,
                                   const krb5_keyblock *from,
                                   krb5_keyblock *to)
{
    krb5_error_code ret = 0;

    /* If nothing to copy, return no error */
    if (from == NULL || to == NULL)
        return (0);

    if ((to->contents == NULL || from->contents == NULL) &&
        from->length > 0)
        return (ENOMEM);

    to->magic       = from->magic;
    to->enctype     = from->enctype;
    to->length      = from->length;
    to->dk_list     = NULL;

    if (from->length > 0)
        (void) memcpy(to->contents, from->contents, from->length);

#ifdef _KERNEL
    to->kef_mt		= from->kef_mt;
    to->kef_key.ck_data	= NULL;
    to->key_tmpl		= NULL;
    if ((ret = init_key_kef(context->kef_cipher_mt, to))) {
        return (ret);
    }
#else
    /*
     * Don't copy or try to initialize crypto framework
     * data.  This data gets initialized the first time it is
     * used.
     */
    to->hKey	= CK_INVALID_HANDLE;
#endif /* _KERNEL */
    return (ret);
}

krb5_error_code
krb5int_c_copy_keyblock_contents(krb5_context context,
                                 const krb5_keyblock *from, krb5_keyblock *to)
{
    /* Solaris Kerberos: begin our keyblock needs different handling */
    if (from->length) {
        to->contents = MALLOC(from->length);
        if (!to->contents)
            return ENOMEM;
    } else 
        to->contents = NULL;
    return krb5int_copy_solaris_keyblock_data(context, from, to);
    /* Solaris Kerberos: end our keyblock needs different handling */
}
