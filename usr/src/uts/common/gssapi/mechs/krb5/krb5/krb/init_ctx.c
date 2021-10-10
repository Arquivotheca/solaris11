/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * lib/krb5/krb/init_ctx.c
 *
 * Copyright 1994,1999,2000, 2002, 2003, 2007, 2008, 2009  by the Massachusetts Institute of Technology.
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
 * Copyright (C) 1998 by the FundsXpress, INC.
 *
 * All rights reserved.
 *
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of FundsXpress. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  FundsXpress makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * krb5_init_contex()
 */

#include "k5-int.h"
#include "int-proto.h"
/*
 * Solaris Kerberos: the code related to EF/pkcs11 and fork safety are mods Sun
 * has made to the MIT code.
 */
#ifndef _KERNEL
#include <ctype.h>

pid_t __krb5_current_pid; /* fork safety: contains the current process ID */
#endif

#include "brand.c"
#ifndef _KERNEL
/* There has to be a better way for windows... */
#if defined(unix) || TARGET_OS_MAC
#include <krb5_libinit.h>
#endif
#endif /* _KERNEL */

/* The des-mdX entries are last for now, because it's easy to
   configure KDCs to issue TGTs with des-mdX keys and then not accept
   them.  This'll be fixed, but for better compatibility, let's prefer
   des-crc for now.  */
/*
 * Solaris Kerberos: Added arcfour-hmac-md5-exp as default enc type.  We don't
 * support ENCTYPE_DES_CBC_MD4 and prefer ENCTYPE_DES_CBC_MD5 over
 * ENCTYPE_DES_CBC_CRC
 */
static krb5_enctype default_enctype_list[] = {
    ENCTYPE_AES256_CTS_HMAC_SHA1_96, ENCTYPE_AES128_CTS_HMAC_SHA1_96,
    ENCTYPE_DES3_CBC_SHA1,
    ENCTYPE_ARCFOUR_HMAC,
    ENCTYPE_ARCFOUR_HMAC_EXP,
    ENCTYPE_DES_CBC_MD5, ENCTYPE_DES_CBC_CRC,
    0
};

#if (defined(_WIN32))
extern krb5_error_code krb5_vercheck();
extern void krb5_win_ccdll_load(krb5_context context);
#endif

static krb5_error_code init_common (krb5_context *, krb5_boolean, krb5_boolean);

krb5_error_code KRB5_CALLCONV
krb5_init_context(krb5_context *context)
{

    return init_common (context, FALSE, FALSE);
}

krb5_error_code KRB5_CALLCONV
krb5_init_secure_context(krb5_context *context)
{

#if 0 /* Solaris Kerberos */
    /* This is to make gcc -Wall happy */
    if(0) krb5_brand[0] = krb5_brand[0];
#endif
    return init_common (context, TRUE, FALSE);
}

#ifndef _KERNEL

krb5_error_code
krb5int_init_context_kdc(krb5_context *context)
{
    return init_common (context, FALSE, TRUE);
}

/* Solaris Kerberos */
krb5_error_code
krb5_open_pkcs11_session(CK_SESSION_HANDLE *hSession)
{
    krb5_error_code retval = 0;
    CK_RV rv; 
    CK_SLOT_ID_PTR slotlist = NULL_PTR; 
    CK_ULONG slotcount; 
    CK_ULONG i;

    /* List of all Slots */
    rv = C_GetSlotList(FALSE, NULL_PTR, &slotcount);
    if (rv != CKR_OK) {
        retval = PKCS_ERR;
        goto cleanup;
    }

    if (slotcount == 0) {
        retval = PKCS_ERR;
        goto cleanup;
    }

    slotlist = (CK_SLOT_ID_PTR)malloc(slotcount * sizeof(CK_SLOT_ID));	
    if (slotlist == NULL) {
        retval = PKCS_ERR;
        goto cleanup;
    }

    rv = C_GetSlotList(FALSE, slotlist, &slotcount);
    if (rv != CKR_OK) {
        retval = PKCS_ERR;
        goto cleanup;
    }
    for (i = 0; i < slotcount; i++) {
        if (slot_supports_krb5(slotlist + i))
            break;
    }
    if (i == slotcount){
        retval = PKCS_ERR;
        goto cleanup;
    }
    rv = C_OpenSession(slotlist[i], CKF_SERIAL_SESSION, NULL_PTR, NULL_PTR,
                       hSession);
    if (rv != CKR_OK) {
        retval = PKCS_ERR;
    }
cleanup:
    if (slotlist != NULL)
        free(slotlist);
    return(retval);
}

/*
 * krb5_reinit_ef_handle()
 *
 * deal with fork safety issue regarding the krb ctx and the pkcs11 hSession
 * field.  This function is called if it is determined that the krb ctx hSession
 * is being accessed in a child process after a fork().  This function
 * re-initilizes the pkcs11 session and returns the session handle.
 */
CK_SESSION_HANDLE
krb5_reinit_ef_handle(krb5_context ctx)
{
    ctx->cryptoki_initialized = FALSE;

    if (krb5_init_ef_handle(ctx) != 0) {
        /*
         * krb5_free_ef_handle() not needed here -- we assume that an equivalent
         * of C_Finalize() was done in the child-side of the fork(), so all EF
         * resources in this context will be invalid.
         */
        return(CK_INVALID_HANDLE);
    }

    /* reset the ctx pid since we're in a new process (child) */
    ctx->pid = __krb5_current_pid; 

    /* If the RC4 handles were initialized, reset them here */
    if (ctx->arcfour_ctx.initialized) {
        krb5_error_code ret;
        ret = krb5_open_pkcs11_session(&ctx->arcfour_ctx.eSession);
        if (ret) {
            ctx->arcfour_ctx.initialized = 0;
            ctx->arcfour_ctx.eSession = CK_INVALID_HANDLE;
            C_CloseSession(ctx->hSession);
            ctx->hSession = CK_INVALID_HANDLE;
        }
        ret = krb5_open_pkcs11_session(&ctx->arcfour_ctx.dSession);
        if (ret) {
            ctx->arcfour_ctx.initialized = 0;
            ctx->arcfour_ctx.eSession = CK_INVALID_HANDLE;
            ctx->arcfour_ctx.dSession = CK_INVALID_HANDLE;
            C_CloseSession(ctx->hSession);
            ctx->hSession = CK_INVALID_HANDLE;
        }
    }

    /* 
     * It is safe for this function to access ctx->hSession directly.  Do
     * NOT use the krb_ctx_hSession() here.
     */
    return(ctx->hSession);
}

/*
 * krb5_pthread_atfork_child_handler() sets a global that indicates the current
 * PID.  This is an optimization to keep getpid() from being called a zillion
 * times.
 */
void
krb5_pthread_atfork_child_handler()
{
    /* 
     * __krb5_current_pid should always be set to current process ID, see the
     * definition of krb_ctx_hSession() for more info 
     */
    __krb5_current_pid = getpid();
}

/*
 * krb5_ld_init() contains code that will be executed at load time (via the
 * ld -zinitarray directive).
 */
void
krb5_ld_init()
{
    /* 
     * fork safety: __krb5_current_pid should always be set to current process
     * ID, see the definition of krb_ctx_hSession() for more info 
     */
    __krb5_current_pid = getpid();
    /* 
     * The child handler below will help reduce the number of times getpid() is
     * called by updating a global PID var. with the current PID whenever a fork
     * occurrs.
     */
    (void) pthread_atfork(NULL, NULL, krb5_pthread_atfork_child_handler);
}
#endif /* !_KERNEL */

krb5_error_code
krb5_init_ef_handle(krb5_context ctx)
{
    krb5_error_code retval = 0;
#ifndef _KERNEL
    CK_RV rv = C_Initialize(NULL_PTR);
    if ((rv != CKR_OK) && (rv != CKR_CRYPTOKI_ALREADY_INITIALIZED)) {
        return (PKCS_ERR);

    }
    /* 
     * It is safe for this function to access ctx->hSession directly.  Do
     * NOT use the krb_ctx_hSession() here.
     */
    retval = krb5_open_pkcs11_session(&ctx->hSession);
    if (retval != 0)
        return (retval);

    ctx->cryptoki_initialized = TRUE;
#else /* ! _KERNEL */
    ctx->kef_cipher_mt = CRYPTO_MECH_INVALID;
    ctx->kef_hash_mt = CRYPTO_MECH_INVALID;
    ctx->kef_cksum_mt = CRYPTO_MECH_INVALID;

    setup_kef_keytypes();
    setup_kef_cksumtypes();

#endif /* ! _KERNEL */
    return(retval);
}

#ifndef _KERNEL
krb5_error_code
krb5_free_ef_handle(krb5_context ctx)
{
    /* 
     * fork safety: Don't free any PKCS state if we've forked since
     * allocating the pkcs handles.
     */
    if (ctx->cryptoki_initialized == TRUE &&
        ctx->pid == __krb5_current_pid) {
        /* 
         * It is safe for this function to access ctx->hSession
         * directly.  Do NOT use the krb_ctx_hSession() here.
         */
        if (ctx->hSession) {
            C_CloseSession(ctx->hSession);
            ctx->hSession = 0;
        }
        if (ctx->arcfour_ctx.dKey) {
            C_DestroyObject(ctx->arcfour_ctx.dSession,
                            ctx->arcfour_ctx.dKey);
            ctx->arcfour_ctx.dKey = 0;
        }
        if (ctx->arcfour_ctx.eKey) {
            C_DestroyObject(ctx->arcfour_ctx.eSession,
                            ctx->arcfour_ctx.eKey);
            ctx->arcfour_ctx.eKey = 0;
        }
        if (ctx->arcfour_ctx.eSession) {
            C_CloseSession(ctx->arcfour_ctx.eSession);
            ctx->arcfour_ctx.eSession = 0;
        }
        if (ctx->arcfour_ctx.dSession) {
            C_CloseSession(ctx->arcfour_ctx.dSession);
            ctx->arcfour_ctx.eSession = 0;
        }
        ctx->arcfour_ctx.initialized = 0;

        ctx->cryptoki_initialized = FALSE;
    }
    return(0);
}
#endif /* !_KERNEL */

static krb5_error_code
init_common (krb5_context *context, krb5_boolean secure, krb5_boolean kdc)
{
    krb5_context ctx = 0;
    krb5_error_code retval;
#ifndef _KERNEL
    struct {
        krb5_int32 now, now_usec;
        long pid;
    } seed_data;
    krb5_data seed;
    int tmp;
#if 0 /* Solaris Kerberos */

    /* Verify some assumptions.  If the assumptions hold and the
       compiler is optimizing, this should result in no code being
       executed.  If we're guessing "unsigned long long" instead
       of using uint64_t, the possibility does exist that we're
       wrong.  */
    {
        krb5_ui_8 i64;
        assert(sizeof(i64) == 8);
        i64 = 0, i64--, i64 >>= 62;
        assert(i64 == 3);
        i64 = 1, i64 <<= 31, i64 <<= 31, i64 <<= 1;
        assert(i64 != 0);
        i64 <<= 1;
        assert(i64 == 0);
    }
#endif

    retval = krb5int_initialize_library();
    if (retval)
        return retval;
#endif

#if (defined(_WIN32))
    /*
     * Load the krbcc32.dll if necessary.  We do this here so that
     * we know to use API: later on during initialization.
     * The context being NULL is ok.
     */
    krb5_win_ccdll_load(ctx);

    /*
     * krb5_vercheck() is defined in win_glue.c, and this is
     * where we handle the timebomb and version server checks.
     */
    retval = krb5_vercheck();
    if (retval)
        return retval;
#endif

    *context = 0;

    ctx = CALLOC(1, sizeof(struct _krb5_context));
    if (!ctx)
        return ENOMEM;
    ctx->magic = KV5M_CONTEXT;

    ctx->profile_secure = secure;

    if ((retval = krb5_os_init_context(ctx, kdc)))
        goto cleanup;

    /*
     * Initialize the EF handle, its needed before doing the random seed.
     */
    if ((retval = krb5_init_ef_handle(ctx)))
        goto cleanup;

#ifndef _KERNEL

    /* fork safety: set pid to current process ID for later checking */
    ctx->pid = __krb5_current_pid;

    retval = profile_get_boolean(ctx->profile, KRB5_CONF_LIBDEFAULTS,
                                 KRB5_CONF_ALLOW_WEAK_CRYPTO, NULL, 0, &tmp);
    if (retval)
        goto cleanup;
    ctx->allow_weak_crypto = tmp;

    /* initialize the prng (not well, but passable) */
#if 0 /* Solaris Kerberos */
    if ((retval = krb5_c_random_os_entropy( ctx, 0, NULL)) !=0)
        goto cleanup;
#endif
    if ((retval = krb5_crypto_us_timeofday(&seed_data.now, &seed_data.now_usec)))
        goto cleanup;
    seed_data.pid = getpid ();
    seed.length = sizeof(seed_data);
    seed.data = (char *) &seed_data;
    /*
     * Solaris Kerberos: we use /dev/urandom, which is automatically seeded, so
     * its OK if this fails.
     */
    (void) krb5_c_random_seed(ctx, &seed);

    ctx->default_realm = 0;
    profile_get_integer(ctx->profile, KRB5_CONF_LIBDEFAULTS, KRB5_CONF_CLOCKSKEW,
                        0, 5 * 60, &tmp);
    ctx->clockskew = tmp;

#if 0
    /* Default ticket lifetime is currently not supported */
    profile_get_integer(ctx->profile, KRB5_CONF_LIBDEFAULTS, "tkt_lifetime",
                        0, 10 * 60 * 60, &tmp);
    ctx->tkt_lifetime = tmp;
#endif

    /* DCE 1.1 and below only support CKSUMTYPE_RSA_MD4 (2)  */
    /* DCE add kdc_req_checksum_type = 2 to krb5.conf */
    profile_get_integer(ctx->profile, KRB5_CONF_LIBDEFAULTS,
                        KRB5_CONF_KDC_REQ_CHECKSUM_TYPE, 0, CKSUMTYPE_RSA_MD5,
                        &tmp);
    ctx->kdc_req_sumtype = tmp;

    profile_get_integer(ctx->profile, KRB5_CONF_LIBDEFAULTS,
                        KRB5_CONF_AP_REQ_CHECKSUM_TYPE, 0, 0,
                        &tmp);
    ctx->default_ap_req_sumtype = tmp;

    profile_get_integer(ctx->profile, KRB5_CONF_LIBDEFAULTS,
                        KRB5_CONF_SAFE_CHECKSUM_TYPE, 0,
                        CKSUMTYPE_RSA_MD5_DES, &tmp);
    ctx->default_safe_sumtype = tmp;

    profile_get_integer(ctx->profile, KRB5_CONF_LIBDEFAULTS,
                        KRB5_CONF_KDC_DEFAULT_OPTIONS, 0,
                        KDC_OPT_RENEWABLE_OK, &tmp);
    ctx->kdc_default_options = tmp;
#define DEFAULT_KDC_TIMESYNC 1
    profile_get_integer(ctx->profile, KRB5_CONF_LIBDEFAULTS,
                        KRB5_CONF_KDC_TIMESYNC, 0, DEFAULT_KDC_TIMESYNC,
                        &tmp);
    ctx->library_options = tmp ? KRB5_LIBOPT_SYNC_KDCTIME : 0;

    /*
     * We use a default file credentials cache of 3.  See
     * lib/krb5/krb/ccache/file/fcc.h for a description of the
     * credentials cache types.
     *
     * Note: DCE 1.0.3a only supports a cache type of 1
     *      DCE 1.1 supports a cache type of 2.
     */
#define DEFAULT_CCACHE_TYPE 4
    profile_get_integer(ctx->profile, KRB5_CONF_LIBDEFAULTS, KRB5_CONF_CCACHE_TYPE,
                        0, DEFAULT_CCACHE_TYPE, &tmp);
    ctx->fcc_default_format = tmp + 0x0500;
    ctx->prompt_types = 0;
    ctx->use_conf_ktypes = 0;

    ctx->udp_pref_limit = -1;
#endif  /* !_KERNEL */
    *context = ctx;
    return 0;

cleanup:
    krb5_free_context(ctx);
    return retval;
}

void KRB5_CALLCONV
krb5_free_context(krb5_context ctx)
{
    if (ctx == NULL)
        return;
#ifndef _KERNEL
    /* Solaris Kerberos kernel/user space changes in here */
    krb5_free_ef_handle(ctx);
    krb5_clear_error_message(ctx); /* Solaris Kerberos moved this up */
#endif

    krb5_os_free_context(ctx);

    /* Solaris Kerberos more kernel/user space changes in here */
#ifdef _KERNEL
    if (ctx->in_tkt_etypes) {
        int i;

        for (i = 0; ctx->in_tkt_etypes[i] != NULL; i++)
        FREE(ctx->in_tkt_etypes, sizeof(krb5_enctype) * (i + 1));
        ctx->in_tkt_etypes = NULL;
    }
#else
    free(ctx->in_tkt_etypes);
    ctx->in_tkt_etypes = NULL;
#endif /* _KERNEL */

#ifdef _KERNEL
    if (ctx->tgs_etypes) {
        int i;

        for (i = 0; ctx->tgs_etypes[i] != NULL; i++)
        FREE(ctx->tgs_etypes, sizeof(krb5_enctype) * (i + 1));
        ctx->tgs_etypes = NULL;
    }
#else
    free(ctx->tgs_etypes);
    ctx->tgs_etypes = NULL;
#endif /* _KERNEL */

#ifdef _KERNEL
    if (ctx->default_realm) {
        FREE(ctx->default_realm, strlen(ctx->default_realm) + 1);
        ctx->default_realm = NULL;
    }
#else
    free(ctx->default_realm);
    ctx->default_realm = NULL;
#endif

    if (ctx->ser_ctx_count && ctx->ser_ctx) {
        FREE(ctx->ser_ctx, sizeof(krb5_ser_entry) * ctx->ser_ctx_count);
        ctx->ser_ctx = NULL;
        ctx->ser_ctx_count = 0;
    }

    ctx->magic = 0;
    FREE(ctx, sizeof(struct _krb5_context));
}

#ifndef _KERNEL
/*
 * Set the desired default ktypes, making sure they are valid.
 */
static krb5_error_code
set_default_etype_var(krb5_context context, const krb5_enctype *etypes,
                      krb5_enctype **var)
{
    krb5_error_code code;
    krb5_enctype *list;
    size_t src, dst;

    if (etypes) {
        /* Empty list passed in. */
        if (etypes[0] == 0)
            return EINVAL;
        code = krb5int_copy_etypes(etypes, &list);
        if (code)
            return code;

        /* Filter list in place to exclude invalid and (optionally) weak
         * enctypes. */
        for (src = dst = 0; list[src]; src++) {
            if (!krb5_c_valid_enctype(list[src]))
                continue;
            if (!context->allow_weak_crypto
                && krb5int_c_weak_enctype(list[src]))
                continue;
            list[dst++] = list[src];
        }
        list[dst] = 0;          /* Zero-terminate. */
        if (dst == 0) {
            free(list);
            return KRB5_CONFIG_ETYPE_NOSUPP;
        }
    } else {
        list = NULL;
    }

    free(*var);
    *var = list;
    return 0;
}

krb5_error_code
krb5_set_default_in_tkt_ktypes(krb5_context context,
                               const krb5_enctype *etypes)
{
    return set_default_etype_var(context, etypes, &context->in_tkt_etypes);
}

krb5_error_code KRB5_CALLCONV
krb5_set_default_tgs_enctypes(krb5_context context, const krb5_enctype *etypes)
{
    return set_default_etype_var(context, etypes, &context->tgs_etypes);
}

/* Old name for above function. */
krb5_error_code
krb5_set_default_tgs_ktypes(krb5_context context, const krb5_enctype *etypes)
{
    return set_default_etype_var(context, etypes, &context->tgs_etypes);
}

/*
 * Add etype to, or remove etype from, the zero-terminated list *list_ptr,
 * reallocating if the list size changes.  Filter out weak enctypes if
 * allow_weak is false.  If memory allocation fails, set *list_ptr to NULL and
 * do nothing for subsequent operations.
 */
static void
mod_list(krb5_enctype etype, krb5_boolean add, krb5_boolean allow_weak,
         krb5_enctype **list_ptr)
{
    size_t i;
    krb5_enctype *list = *list_ptr;

    /* Stop now if a previous allocation failed or the enctype is filtered. */
    if (list == NULL || (!allow_weak && krb5int_c_weak_enctype(etype)))
        return;
    if (add) {
        /* Count entries; do nothing if etype is a duplicate. */
        for (i = 0; list[i] != 0; i++) {
            if (list[i] == etype)
                return;
        }
        /* Make room for the new entry and add it. */
        list = realloc(list, (i + 2) * sizeof(krb5_enctype));
        if (list != NULL) {
            list[i] = etype;
            list[i + 1] = 0;
        }
    } else {
        /* Look for etype in the list. */
        for (i = 0; list[i] != 0; i++) {
            if (list[i] != etype)
                continue;
            /* Perform removal. */
            for (; list[i + 1] != 0; i++)
                list[i] = list[i + 1];
            list[i] = 0;
            list = realloc(list, (i + 1) * sizeof(krb5_enctype));
            break;
        }
    }
    /* Update *list_ptr, freeing the old value if realloc failed. */
    if (list == NULL)
        free(*list_ptr);
    *list_ptr = list;
}

/*
 * Set *result to a zero-terminated list of enctypes resulting from
 * parsing profstr.  profstr may be modified during parsing.
 */
krb5_error_code
krb5int_parse_enctype_list(krb5_context context, char *profstr,
                           krb5_enctype *default_list, krb5_enctype **result)
{
    char *token, *delim = " \t\r\n,", *save = NULL;
    krb5_boolean sel, weak = context->allow_weak_crypto;
    krb5_enctype etype, *list;
    unsigned int i;

    *result = NULL;

    /* Set up an empty list.  Allocation failure is detected at the end. */
    list = malloc(sizeof(krb5_enctype));
    if (list != NULL)
        list[0] = 0;

    /* Walk through the words in profstr. */
    for (token = strtok_r(profstr, delim, &save); token;
         token = strtok_r(NULL, delim, &save)) {
        /* Determine if we are adding or removing enctypes. */
        sel = TRUE;
        if (*token == '+' || *token == '-')
            sel = (*token++ == '+');

        if (strcasecmp(token, "DEFAULT") == 0) {
            /* Set all enctypes in the default list. */
            for (i = 0; default_list[i]; i++)
                mod_list(default_list[i], sel, weak, &list);
        } else if (strcasecmp(token, "des") == 0) {
            mod_list(ENCTYPE_DES_CBC_CRC, sel, weak, &list);
            mod_list(ENCTYPE_DES_CBC_MD5, sel, weak, &list);
            /* Solaris Kerberos we don't support ENCTYPE_DES_CBC_MD4 */
            /* mod_list(ENCTYPE_DES_CBC_MD4, sel, weak, &list); */
        } else if (strcasecmp(token, "des3") == 0) {
            mod_list(ENCTYPE_DES3_CBC_SHA1, sel, weak, &list);
        } else if (strcasecmp(token, "aes") == 0) {
            mod_list(ENCTYPE_AES256_CTS_HMAC_SHA1_96, sel, weak, &list);
            mod_list(ENCTYPE_AES128_CTS_HMAC_SHA1_96, sel, weak, &list);
        } else if (strcasecmp(token, "rc4") == 0) {
            mod_list(ENCTYPE_ARCFOUR_HMAC, sel, weak, &list);
            /* Solaris Kerberos ENCTYPE_ARCFOUR_HMAC_EXP is a default */
            mod_list(ENCTYPE_ARCFOUR_HMAC_EXP, sel, weak, &list);
        } else if (krb5_string_to_enctype(token, &etype) == 0) {
            /* Set a specific enctype. */
            mod_list(etype, sel, weak, &list);
        }
    }

    if (list == NULL)
        return ENOMEM;
    *result = list;
    return 0;
}

/*
 * Set *etypes_ptr to a zero-terminated list of enctypes.  ctx_list
 * (containing application-specified enctypes) is used if non-NULL;
 * otherwise the libdefaults profile string specified by profkey is
 * used.  default_list is the default enctype list to be used while
 * parsing profile strings, and is also used if the profile string is
 * not set.
 */
static krb5_error_code
get_profile_etype_list(krb5_context context, krb5_enctype **etypes_ptr,
                       char *profkey, krb5_enctype *ctx_list,
                       krb5_enctype *default_list)
{
    krb5_enctype *etypes;
    krb5_error_code code;
    char *profstr;

    *etypes_ptr = NULL;

    if (ctx_list) {
        /* Use application defaults. */
        code = krb5int_copy_etypes(ctx_list, &etypes);
        if (code)
            return code;
    } else {
        /* Parse profile setting, or "DEFAULT" if not specified. */
        code = profile_get_string(context->profile, KRB5_CONF_LIBDEFAULTS,
                                  profkey, NULL, "DEFAULT", &profstr);
        if (code)
            return code;
        code = krb5int_parse_enctype_list(context, profstr, default_list,
                                          &etypes);
        profile_release_string(profstr);
        if (code)
            return code;
    }

    if (etypes[0] == 0) {
        free(etypes);
        return KRB5_CONFIG_ETYPE_NOSUPP;
    }

    *etypes_ptr = etypes;
    return 0;
}

krb5_error_code
krb5_get_default_in_tkt_ktypes(krb5_context context, krb5_enctype **ktypes)
{
    return get_profile_etype_list(context, ktypes,
                                  KRB5_CONF_DEFAULT_TKT_ENCTYPES,
                                  context->in_tkt_etypes,
                                  default_enctype_list);
}

void
KRB5_CALLCONV
krb5_free_ktypes (krb5_context context, krb5_enctype *val)
{
    free (val);
}

krb5_error_code
KRB5_CALLCONV
krb5_get_tgs_ktypes(krb5_context context, krb5_const_principal princ, krb5_enctype **ktypes)
{
    if (context->use_conf_ktypes)
        /* This one is set *only* by reading the config file; it's not
           set by the application.  */
        return get_profile_etype_list(context, ktypes,
                                      KRB5_CONF_DEFAULT_TKT_ENCTYPES, NULL,
                                      default_enctype_list);
    else
        return get_profile_etype_list(context, ktypes,
                                      KRB5_CONF_DEFAULT_TGS_ENCTYPES,
                                      context->tgs_etypes,
                                      default_enctype_list);
}

krb5_error_code KRB5_CALLCONV
krb5_get_permitted_enctypes(krb5_context context, krb5_enctype **ktypes)
{
    return get_profile_etype_list(context, ktypes,
                                  KRB5_CONF_PERMITTED_ENCTYPES,
                                  context->tgs_etypes, default_enctype_list);
}

krb5_boolean
krb5_is_permitted_enctype(krb5_context context, krb5_enctype etype)
{
    krb5_enctype *list, *ptr;
    krb5_boolean ret;

    if (krb5_get_permitted_enctypes(context, &list))
        return(0);


    ret = 0;

    for (ptr = list; *ptr; ptr++)
        if (*ptr == etype)
            ret = 1;

    krb5_free_ktypes (context, list);

    return(ret);
}

/* The same as krb5_is_permitted_enctype, but verifies multiple etype's
 * Returns 0 is either the list of the permitted enc types is not available
 * or all requested etypes are not permitted. Otherwise returns 1.
 */

krb5_boolean
krb5_is_permitted_enctype_ext ( krb5_context context,
                                krb5_etypes_permitted *etypes)
{
    krb5_enctype *list, *ptr;
    krb5_boolean ret = 0;
    int i = 0;

    if (krb5_get_permitted_enctypes(context, &list))
        return(0);

    for ( i=0; i< etypes->etype_count; i++ )
    {
        for (ptr = list; *ptr; ptr++)
        {
            if (*ptr == etypes->etype[i])
            {
                etypes->etype_ok[i] =  TRUE;
                ret = 1;
            }
        }
    }
    krb5_free_ktypes (context, list);

    return(ret);
}
#endif /* !KERNEL */
