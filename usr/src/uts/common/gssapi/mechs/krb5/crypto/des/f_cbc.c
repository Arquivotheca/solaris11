/*
 * Copyright (c) 2000, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 1990 Dennis Ferguson.  All rights reserved.
 *
 * Commercial use is permitted only if products which are derived from
 * or include this software are made available for purchase and/or use
 * in Canada.  Otherwise, redistribution and use in source and binary
 * forms are permitted.
 */

/*
 * des_cbc_encrypt.c - an implementation of the DES cipher function in cbc mode
 */
#include "des_int.h"

/*
 * des_cbc_encrypt - {en,de}crypt a stream in CBC mode
 */

/* SUNW14resync - sparcv9 cc complained about lack of object init */
/* = all zero */
const mit_des_cblock mit_des_zeroblock = {0, 0, 0, 0, 0, 0, 0, 0};

#undef mit_des_cbc_encrypt

#ifndef _KERNEL
int
mit_des_cbc_encrypt(context, in, out, length, key, ivec, encrypt)
	krb5_context context;
	const mit_des_cblock  *in;
	mit_des_cblock  *out;
	long length;
	krb5_keyblock *key;
	mit_des_cblock ivec;
	int encrypt;
{
    krb5_error_code ret = KRB5_PROG_ETYPE_NOSUPP;
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
    mechanism.pParameter = ivec;
    if (ivec != NULL)
    	mechanism.ulParameterLen = MIT_DES_BLOCK_LENGTH;
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

/*
 * This routine performs DES cipher-block-chaining operation, either
 * encrypting from cleartext to ciphertext, if encrypt != 0 or
 * decrypting from ciphertext to cleartext, if encrypt == 0.
 *
 * The key schedule is passed as an arg, as well as the cleartext or
 * ciphertext.  The cleartext and ciphertext should be in host order.
 *
 * NOTE-- the output is ALWAYS an multiple of 8 bytes long.  If not
 * enough space was provided, your program will get trashed.
 *
 * For encryption, the cleartext string is null padded, at the end, to
 * an integral multiple of eight bytes.
 *
 * For decryption, the ciphertext will be used in integral multiples
 * of 8 bytes, but only the first "length" bytes returned into the
 * cleartext.
 */

/* ARGSUSED */
int 
mit_des_cbc_encrypt(krb5_context context,
	const mit_des_cblock *in,
	mit_des_cblock *out,
	long length, krb5_keyblock *key,
	mit_des_cblock ivec, int encrypt)
{
	int ret = KRB5_PROG_ETYPE_NOSUPP;
/* EXPORT DELETE START */
	krb5_data ivdata;
	ret = 0;

	ivdata.data = (char *)ivec;
	ivdata.length = sizeof(mit_des_cblock);

	ret = k5_ef_crypto((const char *)in,
			(char *)out, length, key, &ivdata, encrypt);

/* EXPORT DELETE END */
	return(ret);
}
#endif /* !_KERNEL */
