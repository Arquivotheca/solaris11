/*
 * Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright 1995 by Richard P. Basch.  All Rights Reserved.
 * Copyright 1995 by Lehman Brothers, Inc.  All Rights Reserved.
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
 * the name of Richard P. Basch, Lehman Brothers and M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  Richard P. Basch,
 * Lehman Brothers and M.I.T. make no representations about the suitability
 * of this software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#include "des_int.h"

/*
 * Triple-DES CBC encryption mode.
 */
#ifndef _KERNEL
int
mit_des3_cbc_encrypt(krb5_context context, const mit_des_cblock *in, mit_des_cblock *out,
		     unsigned long length, krb5_keyblock *key,
		     const mit_des_cblock ivec, int encrypt)
{
    int ret = KRB5_PROG_ETYPE_NOSUPP;
/* EXPORT DELETE START */
    KRB5_MECH_TO_PKCS algos;
    CK_MECHANISM mechanism;
    CK_RV rv;
    /* For the Key Object */
    ret = 0;

    if ((rv = get_algo(key->enctype, &algos)) != CKR_OK) {
        ret = PKCS_ERR;
        goto cleanup;
    }

    rv = init_key_uef(krb_ctx_hSession(context), key);
    if (rv != CKR_OK) {
        ret = PKCS_ERR;
        goto cleanup;
    }

    mechanism.mechanism = algos.enc_algo;
    mechanism.pParameter = (void*)ivec;
    if (ivec != NULL)
    	mechanism.ulParameterLen = sizeof(mit_des_cblock);
    else
	mechanism.ulParameterLen = 0;

    if (encrypt)
        rv = C_EncryptInit(krb_ctx_hSession(context), &mechanism, key->hKey);
    else
        rv = C_DecryptInit(krb_ctx_hSession(context), &mechanism, key->hKey);

    if (rv != CKR_OK) {
        ret = PKCS_ERR;
        goto cleanup;
    }

    if (encrypt)
        rv = C_Encrypt(krb_ctx_hSession(context), (CK_BYTE_PTR)in,
            (CK_ULONG)length, (CK_BYTE_PTR)out,
            (CK_ULONG_PTR)&length);
    else
        rv = C_Decrypt(krb_ctx_hSession(context), (CK_BYTE_PTR)in,
            (CK_ULONG)length, (CK_BYTE_PTR)out,
            (CK_ULONG_PTR)&length);

    if (rv != CKR_OK) {
            ret = PKCS_ERR;
    }
cleanup:

final_cleanup:
    if (ret)
        (void) memset(out, 0, length);

/* EXPORT DELETE END */
    return(ret);
}

#else
#include <sys/crypto/api.h>

/* ARGSUSED */
int
mit_des3_cbc_encrypt(krb5_context context,
	const mit_des_cblock *in,
	mit_des_cblock *out,
        unsigned long length, krb5_keyblock *key,
        const mit_des_cblock ivec, int encrypt)
{
	int ret = KRB5_PROG_ETYPE_NOSUPP;
/* EXPORT DELETE START */
	krb5_data ivdata;

	ivdata.data = (char *)ivec;
	ivdata.length = sizeof(mit_des_cblock);

        ret = k5_ef_crypto((const char *)in, (char *)out,
			length, key, &ivdata, encrypt);

/* EXPORT DELETE END */
        return(ret);
}
#endif /* !_KERNEL */
