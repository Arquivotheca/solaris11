/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
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

#ifndef ETYPES_H
#define ETYPES_H

#include "k5-int.h"

struct krb5_keytypes;

typedef void (*krb5_encrypt_length_func) (const struct krb5_enc_provider *enc,
  const struct krb5_hash_provider *hash,
  size_t inputlen, size_t *length);

typedef krb5_error_code (*krb5_crypt_func) (
  krb5_context context,
  krb5_const struct krb5_enc_provider *enc,
  krb5_const struct krb5_hash_provider *hash,
  krb5_const krb5_keyblock *key, krb5_keyusage usage,
  krb5_const krb5_data *ivec,
  krb5_const krb5_data *input, krb5_data *output);

#ifndef	_KERNEL
typedef krb5_error_code (*krb5_str2key_func) (
  krb5_context context,
  krb5_const struct krb5_enc_provider *enc, krb5_const krb5_data *string,
  krb5_const krb5_data *salt, krb5_const krb5_data *params,
  krb5_keyblock *key);
#endif	/* _KERNEL */

typedef krb5_error_code (*krb5_prf_func)(
					 const struct krb5_enc_provider *enc,
					 const struct krb5_hash_provider *hash,
					 const krb5_keyblock *key,
					 const krb5_data *in, krb5_data *out);

typedef krb5_error_code (*prf_func)(const struct krb5_keytypes *ktp,
                                    krb5_key key,
                                    const krb5_data *in, krb5_data *out);

struct krb5_keytypes {
    krb5_enctype etype;
    char *in_string;
    char *out_string;
    const struct krb5_enc_provider *enc;
    const struct krb5_hash_provider *hash;
    krb5_encrypt_length_func encrypt_len;
    krb5_crypt_func encrypt;
    krb5_crypt_func decrypt;
    krb5_cksumtype required_ctype;
#ifndef	_KERNEL
    /* Solaris Kerberos: not done in the kernel */
    char *name;
    char *aliases[2];
    krb5_str2key_func str2key;
    size_t prf_length;
    prf_func prf;
    krb5_flags flags;
#else	/* _KERNEL */
    char *mt_e_name;
    char *mt_h_name;
    crypto_mech_type_t kef_cipher_mt;
    crypto_mech_type_t kef_hash_mt;
#endif	/* _KERNEL */
};

#define ETYPE_WEAK 1

extern struct krb5_keytypes krb5_enctypes_list[];
extern const int krb5_enctypes_length;

#define	krb5int_enctypes_list	krb5_enctypes_list
#define	krb5int_enctypes_length	krb5_enctypes_length

/*
 * Solaris Kerberos
 * New (183) MIT code expects const but Solaris code is still using non-const
 * (some of Kerb pkcs11/EF requires it).
 */
static inline const struct krb5_keytypes *
find_enctype(krb5_enctype enctype)
{
    int i;

    /* Solaris Kerberos - later when doable s/krb5_/krb5int_ */
    for (i = 0; i < krb5_enctypes_length; i++) {
        if (krb5_enctypes_list[i].etype == enctype)
            break;
    }

    if (i == krb5_enctypes_length)
        return NULL;
    /* Solaris Kerberos - const cast */
    return (const struct krb5_keytypes *)&krb5_enctypes_list[i];
}

#endif
