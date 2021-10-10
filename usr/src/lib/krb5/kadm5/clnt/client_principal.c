/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright 1993 OpenVision Technologies, Inc., All Rights Reserved
 *
 * $Header$
 */

/*
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 *
 *	Openvision retains the copyright to derivative works of
 *	this source code.  Do *NOT* create a derivative of this
 *	source code before consulting with your legal department.
 *	Do *NOT* integrate *ANY* of this source code into another
 *	product before consulting with your legal department.
 *
 *	For further information, read the top-level Openvision
 *	copyright which is contained in the top-level MIT Kerberos
 *	copyright.
 *
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 *
 */

#if !defined(lint) && !defined(__CODECENTER__)
static char *rcsid = "$Header$";
#endif

#include    <rpc/rpc.h>
#include    <kadm5/admin.h>
#include    <kadm5/kadm_rpc.h>
#ifdef HAVE_MEMORY_H
#include    <memory.h>
#endif
#include    <string.h>
#include    <errno.h>
#include    "client_internal.h"

#ifdef DEBUG
#define eret() do { clnt_perror(handle->clnt, "null ret"); return KADM5_RPC_ERROR; } while (0)
#else
#define eret() do { return KADM5_RPC_ERROR; } while (0)
#endif

kadm5_ret_t
kadm5_create_principal(void *server_handle,
                       kadm5_principal_ent_t princ, long mask,
                       char *pw)
{
    generic_ret         *r;
    cprinc_arg          arg;
    kadm5_server_handle_t handle = server_handle;

    CHECK_HANDLE(server_handle);

    memset(&arg, 0, sizeof(arg));
    arg.mask = mask;
    arg.passwd = pw;
    arg.api_version = handle->api_version;

    if(princ == NULL)
        return EINVAL;

    memcpy(&arg.rec, princ, sizeof(kadm5_principal_ent_rec));
    arg.rec.mod_name = NULL;

    if(!(mask & KADM5_POLICY))
        arg.rec.policy = NULL;
    if (! (mask & KADM5_KEY_DATA)) {
        arg.rec.n_key_data = 0;
        arg.rec.key_data = NULL;
    }
    if (! (mask & KADM5_TL_DATA)) {
        arg.rec.n_tl_data = 0;
        arg.rec.tl_data = NULL;
    }

    r = create_principal_2(&arg, handle->clnt);

    if(r == NULL)
        eret();
    return r->code;
}

kadm5_ret_t
kadm5_create_principal_3(void *server_handle,
                         kadm5_principal_ent_t princ, long mask,
                         int n_ks_tuple,
                         krb5_key_salt_tuple *ks_tuple,
                         char *pw)
{
    generic_ret         *r;
    cprinc3_arg         arg;
    kadm5_server_handle_t handle = server_handle;

    CHECK_HANDLE(server_handle);

    memset(&arg, 0, sizeof(arg));
    arg.mask = mask;
    arg.passwd = pw;
    arg.api_version = handle->api_version;
    arg.n_ks_tuple = n_ks_tuple;
    arg.ks_tuple = ks_tuple;

    if(princ == NULL)
        return EINVAL;

    memcpy(&arg.rec, princ, sizeof(kadm5_principal_ent_rec));
    arg.rec.mod_name = NULL;

    if(!(mask & KADM5_POLICY))
        arg.rec.policy = NULL;
    if (! (mask & KADM5_KEY_DATA)) {
        arg.rec.n_key_data = 0;
        arg.rec.key_data = NULL;
    }
    if (! (mask & KADM5_TL_DATA)) {
        arg.rec.n_tl_data = 0;
        arg.rec.tl_data = NULL;
    }

    r = create_principal3_2(&arg, handle->clnt);

    if(r == NULL)
        eret();
    return r->code;
}

kadm5_ret_t
kadm5_delete_principal(void *server_handle, krb5_principal principal)
{
    dprinc_arg          arg;
    generic_ret         *r;
    kadm5_server_handle_t handle = server_handle;

    CHECK_HANDLE(server_handle);

    if(principal == NULL)
        return EINVAL;
    arg.princ = principal;
    arg.api_version = handle->api_version;
    r = delete_principal_2(&arg, handle->clnt);
    if(r == NULL)
        eret();
    return r->code;
}

kadm5_ret_t
kadm5_modify_principal(void *server_handle,
                       kadm5_principal_ent_t princ, long mask)
{
    mprinc_arg          arg;
    generic_ret         *r;
    kadm5_server_handle_t handle = server_handle;

    CHECK_HANDLE(server_handle);

    memset(&arg, 0, sizeof(arg));
    arg.mask = mask;
    arg.api_version = handle->api_version;
    if(princ == NULL)
        return EINVAL;
    memcpy(&arg.rec, princ, sizeof(kadm5_principal_ent_rec));
    if(!(mask & KADM5_POLICY))
        arg.rec.policy = NULL;
    if (! (mask & KADM5_KEY_DATA)) {
        arg.rec.n_key_data = 0;
        arg.rec.key_data = NULL;
    }
    if (! (mask & KADM5_TL_DATA)) {
        arg.rec.n_tl_data = 0;
        arg.rec.tl_data = NULL;
    }

    arg.rec.mod_name = NULL;

    r = modify_principal_2(&arg, handle->clnt);

    if(r == NULL)
        eret();
    return r->code;
}

kadm5_ret_t
kadm5_get_principal(void *server_handle,
                    krb5_principal princ, kadm5_principal_ent_t ent,
                    long mask)
{
    gprinc_arg  arg;
    gprinc_ret  *r;
    kadm5_server_handle_t handle = server_handle;

    CHECK_HANDLE(server_handle);

    if(princ == NULL)
        return EINVAL;
    arg.princ = princ;
    arg.mask = mask;
    arg.api_version = handle->api_version;
    r = get_principal_2(&arg, handle->clnt);
    if(r == NULL)
        eret();
    if (r->code == 0)
        memcpy(ent, &r->rec, sizeof(r->rec));

    return r->code;
}

kadm5_ret_t
kadm5_get_principals(void *server_handle,
                     char *exp, char ***princs, int *count)
{
    gprincs_arg arg;
    gprincs_ret *r;
    kadm5_server_handle_t handle = server_handle;

    CHECK_HANDLE(server_handle);

    if(princs == NULL || count == NULL)
        return EINVAL;
    arg.exp = exp;
    arg.api_version = handle->api_version;
    r = get_princs_2(&arg, handle->clnt);
    if(r == NULL)
        eret();
    if(r->code == 0) {
        *count = r->count;
        *princs = r->princs;
    } else {
        *count = 0;
        *princs = NULL;
    }

    return r->code;
}

kadm5_ret_t
kadm5_rename_principal(void *server_handle,
                       krb5_principal source, krb5_principal dest)
{
    rprinc_arg          arg;
    generic_ret         *r;
    kadm5_server_handle_t handle = server_handle;

    CHECK_HANDLE(server_handle);

    arg.src = source;
    arg.dest = dest;
    arg.api_version = handle->api_version;
    if (source == NULL || dest == NULL)
        return EINVAL;
    r = rename_principal_2(&arg, handle->clnt);
    if(r == NULL)
        eret();
    return r->code;
}

kadm5_ret_t
kadm5_chpass_principal(void *server_handle,
                       krb5_principal princ, char *password)
{
    chpass_arg          arg;
    generic_ret         *r;
    kadm5_server_handle_t handle = server_handle;

    CHECK_HANDLE(server_handle);

    arg.princ = princ;
    arg.pass = password;
    arg.api_version = handle->api_version;

    if(princ == NULL)
        return EINVAL;
    r = chpass_principal_2(&arg, handle->clnt);
    if(r == NULL)
        eret();
    return r->code;
}

kadm5_ret_t
kadm5_chpass_principal_3(void *server_handle,
                         krb5_principal princ, krb5_boolean keepold,
                         int n_ks_tuple, krb5_key_salt_tuple *ks_tuple,
                         char *password)
{
    chpass3_arg         arg;
    generic_ret         *r;
    kadm5_server_handle_t handle = server_handle;

    CHECK_HANDLE(server_handle);

    arg.princ = princ;
    arg.pass = password;
    arg.api_version = handle->api_version;
    arg.keepold = keepold;
    arg.n_ks_tuple = n_ks_tuple;
    arg.ks_tuple = ks_tuple;

    if(princ == NULL)
        return EINVAL;
    r = chpass_principal3_2(&arg, handle->clnt);
    if(r == NULL)
        eret();
    return r->code;
}

kadm5_ret_t
kadm5_setv4key_principal(void *server_handle,
                         krb5_principal princ,
                         krb5_keyblock *keyblock)
{
    setv4key_arg        arg;
    generic_ret         *r;
    kadm5_server_handle_t handle = server_handle;

    CHECK_HANDLE(server_handle);

    arg.princ = princ;
    arg.keyblock = keyblock;
    arg.api_version = handle->api_version;

    if(princ == NULL || keyblock == NULL)
        return EINVAL;
    r = setv4key_principal_2(&arg, handle->clnt);
    if(r == NULL)
        eret();
    return r->code;
}

kadm5_ret_t
kadm5_setkey_principal(void *server_handle,
                       krb5_principal princ,
                       krb5_keyblock *keyblocks,
                       int n_keys)
{
    setkey_arg          arg;
    generic_ret         *r;
    kadm5_server_handle_t handle = server_handle;

    CHECK_HANDLE(server_handle);

    arg.princ = princ;
    arg.keyblocks = keyblocks;
    arg.n_keys = n_keys;
    arg.api_version = handle->api_version;

    if(princ == NULL || keyblocks == NULL)
        return EINVAL;
    r = setkey_principal_2(&arg, handle->clnt);
    if(r == NULL)
        eret();
    return r->code;
}

kadm5_ret_t
kadm5_setkey_principal_3(void *server_handle,
                         krb5_principal princ,
                         krb5_boolean keepold, int n_ks_tuple,
                         krb5_key_salt_tuple *ks_tuple,
                         krb5_keyblock *keyblocks,
                         int n_keys)
{
    setkey3_arg         arg;
    generic_ret         *r;
    kadm5_server_handle_t handle = server_handle;

    CHECK_HANDLE(server_handle);

    arg.princ = princ;
    arg.keyblocks = keyblocks;
    arg.n_keys = n_keys;
    arg.api_version = handle->api_version;
    arg.keepold = keepold;
    arg.n_ks_tuple = n_ks_tuple;
    arg.ks_tuple = ks_tuple;

    if(princ == NULL || keyblocks == NULL)
        return EINVAL;
    r = setkey_principal3_2(&arg, handle->clnt);
    if(r == NULL)
        eret();
    return r->code;
}

/*
 * Solaris Kerberos:
 * This routine implements just the "old" randkey_principal code.
 * The code in the kadmin client sometimes needs to call this
 * directly when the kadm5_randkey_principal_3 call fails.  
 *
 * The kadmin client utility uses a specific set of key/salt tuples,
 * so the standard fallback in kadm5_randkey_principal (see below)
 * will not work because it would result in kadm5_randkey_principal_3
 * being called twice - once with the specific key/salts specified by
 * kadmin and once with the NULL set (used to indicate that the server
 * should use the full set of supported enctypes).  Making this
 * routine separate makes the code simpler and avoids making the
 * kadm5_randkey_principal_3 twice from kadmin.
 */
kadm5_ret_t
kadm5_randkey_principal_old(void *server_handle,
                            krb5_principal princ,
                            krb5_keyblock **key,
                            int *n_keys)
{
    chrand_arg		arg;
    chrand_ret		*r;
    kadm5_server_handle_t handle = server_handle;
    int		i, ret;

    /* For safety */
    if (n_keys)
        *n_keys = 0;
    if (key)
        *key = NULL;
    CHECK_HANDLE(server_handle);

    arg.princ = princ;
    arg.api_version = handle->api_version;

    if(princ == NULL)
        return EINVAL;
    r = chrand_principal_2(&arg, handle->clnt);
    if (r == NULL)
        return KADM5_RPC_ERROR;
    if (key && (r->n_keys > 0)) {
        *key = (krb5_keyblock *) malloc(r->n_keys*sizeof(krb5_keyblock));
        if (*key == NULL)
            return ENOMEM;
        for (i = 0; i < r->n_keys; i++) {
            ret = krb5_copy_keyblock_contents(handle->context,
                                              &r->keys[i],
                                              &(*key)[i]);
            if (ret) {
                free(*key);
                *key = NULL;
                return ENOMEM;
            }
        }
        if (n_keys)
            *n_keys = r->n_keys;
    }
    return (r->code);
}

kadm5_ret_t
kadm5_randkey_principal_3(void *server_handle,
                          krb5_principal princ,
                          krb5_boolean keepold, int n_ks_tuple,
                          krb5_key_salt_tuple *ks_tuple,
                          krb5_keyblock **key, int *n_keys)
{
    chrand3_arg         arg;
    chrand_ret          *r;
    kadm5_server_handle_t handle = server_handle;
    int                 i, ret;

    /* Solaris Kerberos - For safety */
    if (n_keys)
	*n_keys = 0;
    if (key)
	*key = NULL;

    CHECK_HANDLE(server_handle);

    arg.princ = princ;
    arg.api_version = handle->api_version;
    arg.keepold = keepold;
    arg.n_ks_tuple = n_ks_tuple;
    arg.ks_tuple = ks_tuple;

    if(princ == NULL)
        return EINVAL;
    r = chrand_principal3_2(&arg, handle->clnt);
    if(r == NULL)
        eret();
    if (n_keys)
        *n_keys = r->n_keys;
    if (key) {
        if(r->n_keys) {
            *key = malloc(r->n_keys * sizeof(krb5_keyblock));
            if (*key == NULL)
                return ENOMEM;
            for (i = 0; i < r->n_keys; i++) {
                ret = krb5_copy_keyblock_contents(handle->context, &r->keys[i],
                                                  &(*key)[i]);
                if (ret) {
                    free(*key);
                    return ENOMEM;
                }
            }
        } else
            *key = NULL;
    }

    return r->code;
}

kadm5_ret_t
kadm5_randkey_principal(void *server_handle,
                        krb5_principal princ,
                        krb5_keyblock **key, int *n_keys)
{
    /* Solaris Kerberos */
    kadm5_ret_t kret;

    /*
     * Default to trying the newest API to insure that the full
     * set of enctypes is created.
     */
    kret = kadm5_randkey_principal_3(server_handle, princ, FALSE,
                                     0, NULL, key, n_keys);

    /*
     * We will get an RPC error if the RPC call failed which
     * will normally indicate that the remote procedure did not
     * exist on the server, so try the older API.
     */
    if (kret == KADM5_RPC_ERROR) {
        kret = kadm5_randkey_principal_old(server_handle, princ,
                                           key, n_keys);
    }
    return (kret);
}

/* not supported on client side */
kadm5_ret_t kadm5_decrypt_key(void *server_handle,
                              kadm5_principal_ent_t entry, krb5_int32
                              ktype, krb5_int32 stype, krb5_int32
                              kvno, krb5_keyblock *keyblock,
                              krb5_keysalt *keysalt, int *kvnop)
{
    return EINVAL;
}
