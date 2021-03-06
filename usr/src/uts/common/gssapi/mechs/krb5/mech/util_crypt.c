/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Copyright 2001, 2008 by the Massachusetts Institute of Technology.
 * Copyright 1993 by OpenVision Technologies, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of OpenVision not be used
 * in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission. OpenVision makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * OPENVISION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OPENVISION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
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
 * Solaris Kerberos: order is important here.  include gssapiP_krb5.h before
 * all others, otherwise we get a LINT error from MALLOC macro being redefined
 * in mechglueP.h.  Also note there are changes throughout to support the
 * kernel.
 */
#include "gssapiP_krb5.h"
#include "k5-int.h"
#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif

static const char kg_arcfour_l40[] = "fortybits";

static krb5_error_code
kg_copy_keys(krb5_context context, krb5_gss_ctx_id_rec *ctx, krb5_key subkey)
{
    krb5_error_code code;

    krb5_k_free_key(context, ctx->enc);
    ctx->enc = NULL;
    code = krb5_k_create_key(context, &subkey->keyblock, &ctx->enc);
    if (code != 0)
        return code;

    krb5_k_free_key(context, ctx->seq);
    ctx->seq = NULL;
    code = krb5_k_create_key(context, &subkey->keyblock, &ctx->seq);
    if (code != 0)
        return code;

    return 0;
}

static krb5_error_code
kg_derive_des_enc_key(krb5_context context, krb5_key subkey, krb5_key *out)
{
    krb5_error_code code;
    krb5_keyblock *keyblock;
    unsigned int i;

    *out = NULL;

    code = krb5_k_key_keyblock(context, subkey, &keyblock);
    if (code != 0)
        return code;

    for (i = 0; i < keyblock->length; i++)
        keyblock->contents[i] ^= 0xF0;

    code = krb5_k_create_key(context, keyblock, out);
    krb5_free_keyblock(context, keyblock);
    return code;
}

krb5_error_code
kg_setup_keys(krb5_context context, krb5_gss_ctx_id_rec *ctx, krb5_key subkey,
              krb5_cksumtype *cksumtype)
{
    krb5_error_code code;
#ifndef _KERNEL
    krb5int_access kaccess;
#endif

    assert(ctx != NULL);
    assert(subkey != NULL);

    *cksumtype = 0;
    ctx->proto = 0;

    if (ctx->enc == NULL) {
        ctx->signalg = -1;
        ctx->sealalg = -1;
    }

#ifndef _KERNEL
    code = krb5int_accessor(&kaccess, KRB5INT_ACCESS_VERSION);
    if (code != 0)
        return code;

    code = (*kaccess.mandatory_cksumtype)(context, subkey->keyblock.enctype,
                                          cksumtype);
#else
    code = krb5int_c_mandatory_cksumtype(context, subkey->keyblock.enctype,
					 cksumtype);
#endif
    if (code != 0)
        return code;

    switch (subkey->keyblock.enctype) {
    case ENCTYPE_DES_CBC_MD5:
    case ENCTYPE_DES_CBC_MD4:
    case ENCTYPE_DES_CBC_CRC:
        krb5_k_free_key(context, ctx->seq);
        code = krb5_k_create_key(context, &subkey->keyblock, &ctx->seq);
        if (code != 0)
            return code;

        krb5_k_free_key(context, ctx->enc);
        code = kg_derive_des_enc_key(context, subkey, &ctx->enc);
        if (code != 0)
            return code;

        ctx->enc->keyblock.enctype = ENCTYPE_DES_CBC_RAW;
        ctx->seq->keyblock.enctype = ENCTYPE_DES_CBC_RAW;
        ctx->signalg = SGN_ALG_DES_MAC_MD5;
        ctx->cksum_size = 8;
        ctx->sealalg = SEAL_ALG_DES;

        break;
    case ENCTYPE_DES3_CBC_SHA1:
        code = kg_copy_keys(context, ctx, subkey);
        if (code != 0)
            return code;

        ctx->enc->keyblock.enctype = ENCTYPE_DES3_CBC_RAW;
        ctx->seq->keyblock.enctype = ENCTYPE_DES3_CBC_RAW;
        ctx->signalg = SGN_ALG_HMAC_SHA1_DES3_KD;
        ctx->cksum_size = 20;
        ctx->sealalg = SEAL_ALG_DES3KD;
        break;
    case ENCTYPE_ARCFOUR_HMAC:
    case ENCTYPE_ARCFOUR_HMAC_EXP:
        code = kg_copy_keys(context, ctx, subkey);
        if (code != 0)
            return code;

        ctx->signalg = SGN_ALG_HMAC_MD5;
        ctx->cksum_size = 8;
        ctx->sealalg = SEAL_ALG_MICROSOFT_RC4;
        break;
    default:
        ctx->proto = 1;
        break;
    }

    return 0;
}

int
kg_confounder_size(krb5_context context, krb5_enctype enctype)
{
    krb5_error_code code;
    size_t blocksize;
    /* We special case rc4*/
    if (enctype == ENCTYPE_ARCFOUR_HMAC || enctype == ENCTYPE_ARCFOUR_HMAC_EXP)
        return 8;
    code = krb5_c_block_size(context, enctype, &blocksize);
    if (code)
        return(-1); /* XXX */

    return(blocksize);
}

krb5_error_code
kg_make_confounder(krb5_context context, krb5_enctype enctype,
                   unsigned char *buf)
{
    int confsize;
    krb5_data lrandom;

    confsize = kg_confounder_size(context, enctype);
    if (confsize < 0)
        return KRB5_BAD_MSIZE;

    lrandom.length = confsize;
    lrandom.data = (char *)buf;

    return(krb5_c_random_make_octets(context, &lrandom));
}

krb5_error_code
kg_encrypt(krb5_context context, krb5_key key, int usage, krb5_pointer iv,
           krb5_const_pointer in, krb5_pointer out, unsigned int length)
{
    krb5_error_code code;
    size_t blocksize;
    krb5_data ivd, *pivd, inputd;
    krb5_enc_data outputd;

    if (iv) {
        code = krb5_c_block_size(context, key->keyblock.enctype, &blocksize);
        if (code)
            return(code);

        ivd.length = blocksize;
        ivd.data = MALLOC(ivd.length);
        if (ivd.data == NULL)
            return ENOMEM;
        (void) memcpy(ivd.data, iv, ivd.length);
        pivd = &ivd;
    } else {
        pivd = NULL;
    }

    inputd.length = length;
    inputd.data = (char *)in;

    outputd.ciphertext.length = length;
    outputd.ciphertext.data = out;

    code = krb5_k_encrypt(context, key, usage, pivd, &inputd, &outputd);
    if (pivd != NULL)
        krb5_free_data_contents(context, pivd); /* Solaris Kerberos */
    return code;
}

/* length is the length of the cleartext. */

krb5_error_code
kg_decrypt(krb5_context context, krb5_key key, int usage, krb5_pointer iv,
           krb5_const_pointer in, krb5_pointer out, unsigned int length)
{
    krb5_error_code code;
    size_t blocksize;
    krb5_data ivd, *pivd, outputd;
    krb5_enc_data inputd;

    if (iv) {
        code = krb5_c_block_size(context, key->keyblock.enctype, &blocksize);
        if (code)
            return(code);

        ivd.length = blocksize;
        ivd.data = MALLOC(ivd.length);
        if (ivd.data == NULL)
            return ENOMEM;
        (void) memcpy(ivd.data, iv, ivd.length);
        pivd = &ivd;
    } else {
        pivd = NULL;
    }

    inputd.enctype = ENCTYPE_UNKNOWN;
    inputd.ciphertext.length = length;
    inputd.ciphertext.data = (char *)in;

    outputd.length = length;
    outputd.data = out;

    code = krb5_k_decrypt(context, key, usage, pivd, &inputd, &outputd);
    if (pivd != NULL)
        krb5_free_data_contents(context, pivd); /* Solaris Kerberos */
    return code;
}

/* Solaris Kerberos: using our version of kg_arcfour_docrypt */
krb5_error_code
kg_arcfour_docrypt (krb5_context context,
		const krb5_keyblock *longterm_key , int ms_usage,
		const unsigned char *kd_data, size_t kd_data_len,
		const unsigned char *input_buf, size_t input_len,
		unsigned char *output_buf)
{
  krb5_error_code code;
  krb5_data input, output;
  krb5_keyblock seq_enc_key, usage_key;
  unsigned char t[4];

  bzero(&usage_key, sizeof(krb5_keyblock));
  bzero(&seq_enc_key, sizeof(krb5_keyblock));

  usage_key.length = longterm_key->length;
  usage_key.contents = MALLOC(usage_key.length);
  usage_key.enctype = longterm_key->enctype;
  usage_key.dk_list = NULL;
#ifdef _KERNEL

  usage_key.kef_mt  = longterm_key->kef_mt;
  code = init_key_kef(longterm_key->kef_mt, &usage_key);
  if (code)
	return (code);
#endif /* _KERNEL */
  if (usage_key.contents == NULL)
    return (ENOMEM);
  seq_enc_key.length = longterm_key->length;
  seq_enc_key.contents = MALLOC(seq_enc_key.length);
  seq_enc_key.enctype = longterm_key->enctype;
  seq_enc_key.dk_list = NULL;
#ifdef _KERNEL
  seq_enc_key.kef_mt  = longterm_key->kef_mt;
  code = init_key_kef(longterm_key->kef_mt, &seq_enc_key);
  if (code)
	return (code);
#endif /* _KERNEL */
  if (seq_enc_key.contents == NULL) {
    FREE ((void *) usage_key.contents, usage_key.length);
    return (ENOMEM);
  }

  t[0] = ms_usage &0xff;
  t[1] = (ms_usage>>8) & 0xff;
  t[2] = (ms_usage>>16) & 0xff;
  t[3] = (ms_usage>>24) & 0xff;
  input.data = (void *) &t;
  input.length = 4;
  output.data = (void *) usage_key.contents;
  output.length = usage_key.length;
#ifdef _KERNEL
  code = krb5_hmac(context, longterm_key, &input, &output);
#else
  code = krb5_hmac(context, &krb5int_hash_md5,
		longterm_key, 1, &input, &output);
#endif /* _KERNEL */
  if (code)
    goto cleanup_arcfour;
	  
  input.data = ( void *) kd_data;
  input.length = kd_data_len;
  output.data = (void *) seq_enc_key.contents;
#ifdef _KERNEL
  code = krb5_hmac(context, &usage_key, &input, &output);
#else
  code = krb5_hmac(context, &krb5int_hash_md5,
		&usage_key, 1, &input, &output);
#endif /* _KERNEL */

  if (code)
    goto cleanup_arcfour;
  input.data = ( void * ) input_buf;
  input.length = input_len;
  output.data = (void * ) output_buf;
  output.length = input_len;

  /*
   * Call the arcfour encryption method directly here, we cannot
   * use the standard "krb5_c_encrypt" interface because we just
   * want the arcfour algorithm applied and not the additional MD5-HMAC
   * which are applied when using the standard interface.
   */
  code = krb5int_enc_arcfour.encrypt(context, &seq_enc_key, 0, &input, &output);

 cleanup_arcfour:
  bzero ((void *) seq_enc_key.contents, seq_enc_key.length);
  bzero ((void *) usage_key.contents, usage_key.length);
  FREE ((void *) usage_key.contents, usage_key.length);
  FREE ((void *) seq_enc_key.contents, seq_enc_key.length);

  return (code);
}

/* Solaris Kerberos: not supported yet */
#if 0 /************** Begin IFDEF'ed OUT *******************************/
#ifndef _KERNEL
/* AEAD */
static krb5_error_code
kg_translate_iov_v1(krb5_context context, krb5_enctype enctype,
                    gss_iov_buffer_desc *iov, int iov_count,
                    krb5_crypto_iov **pkiov, size_t *pkiov_count)
{
    gss_iov_buffer_desc *header;
    gss_iov_buffer_desc *trailer;
    int i = 0, j;
    size_t kiov_count;
    krb5_crypto_iov *kiov;
    size_t conf_len;

    *pkiov = NULL;
    *pkiov_count = 0;

    conf_len = kg_confounder_size(context, enctype);

    header = kg_locate_iov(iov, iov_count, GSS_IOV_BUFFER_TYPE_HEADER);
    assert(header != NULL);

    if (header->buffer.length < conf_len)
        return KRB5_BAD_MSIZE;

    trailer = kg_locate_iov(iov, iov_count, GSS_IOV_BUFFER_TYPE_TRAILER);
    assert(trailer == NULL || trailer->buffer.length == 0);

    kiov_count = 3 + iov_count;
    kiov = (krb5_crypto_iov *)MALLOC(kiov_count * sizeof(krb5_crypto_iov));
    if (kiov == NULL)
        return ENOMEM;

    /* For pre-CFX (raw enctypes) there is no krb5 header */
    kiov[i].flags = KRB5_CRYPTO_TYPE_HEADER;
    kiov[i].data.length = 0;
    kiov[i].data.data = NULL;
    i++;

    /* For pre-CFX, the confounder is at the end of the GSS header */
    kiov[i].flags = KRB5_CRYPTO_TYPE_DATA;
    kiov[i].data.length = conf_len;
    kiov[i].data.data = (char *)header->buffer.value + header->buffer.length - conf_len;
    i++;

    for (j = 0; j < iov_count; j++) {
        kiov[i].flags = kg_translate_flag_iov(iov[j].type);
        if (kiov[i].flags == KRB5_CRYPTO_TYPE_EMPTY)
            continue;

        kiov[i].data.length = iov[j].buffer.length;
        kiov[i].data.data = (char *)iov[j].buffer.value;
        i++;
    }

    kiov[i].flags = KRB5_CRYPTO_TYPE_TRAILER;
    kiov[i].data.length = 0;
    kiov[i].data.data = NULL;
    i++;

    *pkiov = kiov;
    *pkiov_count = i;

    return 0;
}

/*
 * DCE_STYLE indicates actual RRC is EC + RRC
 * EC is extra rotate count for DCE_STYLE, pad length otherwise
 * RRC is rotate count.
 */
static krb5_error_code
kg_translate_iov_v3(krb5_context context, int dce_style, size_t ec, size_t rrc,
                    krb5_enctype enctype, gss_iov_buffer_desc *iov,
                    int iov_count, krb5_crypto_iov **pkiov,
                    size_t *pkiov_count)
{
    gss_iov_buffer_t header;
    gss_iov_buffer_t trailer;
    int i = 0, j;
    size_t kiov_count;
    krb5_crypto_iov *kiov;
    unsigned int k5_headerlen = 0, k5_trailerlen = 0;
    size_t gss_headerlen, gss_trailerlen;
    krb5_error_code code;

    *pkiov = NULL;
    *pkiov_count = 0;

    header = kg_locate_iov(iov, iov_count, GSS_IOV_BUFFER_TYPE_HEADER);
    assert(header != NULL);

    trailer = kg_locate_iov(iov, iov_count, GSS_IOV_BUFFER_TYPE_TRAILER);
    assert(trailer == NULL || rrc == 0);

    code = krb5_c_crypto_length(context, enctype, KRB5_CRYPTO_TYPE_HEADER,
                                &k5_headerlen);
    if (code != 0)
        return code;

    code = krb5_c_crypto_length(context, enctype, KRB5_CRYPTO_TYPE_TRAILER,
                                &k5_trailerlen);
    if (code != 0)
        return code;

    /* Check header and trailer sizes */
    gss_headerlen = 16 /* GSS-Header */ + k5_headerlen; /* Kerb-Header */
    gss_trailerlen = ec + 16 /* E(GSS-Header) */ + k5_trailerlen; /* Kerb-Trailer */

    /* If we're caller without a trailer, we must rotate by trailer length */
    if (trailer == NULL) {
        size_t actual_rrc = rrc;

        if (dce_style)
            actual_rrc += ec; /* compensate for Windows bug */

        if (actual_rrc != gss_trailerlen)
            return KRB5_BAD_MSIZE;

        gss_headerlen += gss_trailerlen;
        gss_trailerlen = 0;
    } else {
        if (trailer->buffer.length != gss_trailerlen)
            return KRB5_BAD_MSIZE;
    }

    if (header->buffer.length != gss_headerlen)
        return KRB5_BAD_MSIZE;

    kiov_count = 3 + iov_count;
    kiov = (krb5_crypto_iov *)MALLOC(kiov_count * sizeof(krb5_crypto_iov));
    if (kiov == NULL)
        return ENOMEM;

    /*
     * The krb5 header is located at the end of the GSS header.
     */
    kiov[i].flags = KRB5_CRYPTO_TYPE_HEADER;
    kiov[i].data.length = k5_headerlen;
    kiov[i].data.data = (char *)header->buffer.value + header->buffer.length - k5_headerlen;
    i++;

    for (j = 0; j < iov_count; j++) {
        kiov[i].flags = kg_translate_flag_iov(iov[j].type);
        if (kiov[i].flags == KRB5_CRYPTO_TYPE_EMPTY)
            continue;

        kiov[i].data.length = iov[j].buffer.length;
        kiov[i].data.data = (char *)iov[j].buffer.value;
        i++;
    }

    /*
     * The EC and encrypted GSS header are placed in the trailer, which may
     * be rotated directly after the plaintext header if no trailer buffer
     * is provided.
     */
    kiov[i].flags = KRB5_CRYPTO_TYPE_DATA;
    kiov[i].data.length = ec + 16; /* E(Header) */
    if (trailer == NULL)
        kiov[i].data.data = (char *)header->buffer.value + 16;
    else
        kiov[i].data.data = (char *)trailer->buffer.value;
    i++;

    /*
     * The krb5 trailer is placed after the encrypted copy of the
     * krb5 header (which may be in the GSS header or trailer).
     */
    kiov[i].flags = KRB5_CRYPTO_TYPE_TRAILER;
    kiov[i].data.length = k5_trailerlen;
    kiov[i].data.data = kiov[i - 1].data.data + ec + 16; /* E(Header) */
    i++;

    *pkiov = kiov;
    *pkiov_count = i;

    return 0;
}

/* PROTO is 1 if CFX, 0 if pre-CFX */
static krb5_error_code
kg_translate_iov(krb5_context context, int proto, int dce_style, size_t ec,
                 size_t rrc, krb5_enctype enctype, gss_iov_buffer_desc *iov,
                 int iov_count, krb5_crypto_iov **pkiov, size_t *pkiov_count)
{
    return proto ?
        kg_translate_iov_v3(context, dce_style, ec, rrc, enctype,
                            iov, iov_count, pkiov, pkiov_count) :
        kg_translate_iov_v1(context, enctype, iov, iov_count,
                            pkiov, pkiov_count);
}

krb5_error_code
kg_encrypt_iov(krb5_context context, int proto, int dce_style, size_t ec,
               size_t rrc, krb5_key key, int usage, krb5_pointer iv,
               gss_iov_buffer_desc *iov, int iov_count)
{
    krb5_error_code code;
    size_t blocksize;
    krb5_data ivd, *pivd;
    size_t kiov_count;
    krb5_crypto_iov *kiov;

    if (iv) {
        code = krb5_c_block_size(context, key->keyblock.enctype, &blocksize);
        if (code)
            return(code);

        ivd.length = blocksize;
        ivd.data = MALLOC(ivd.length);
        if (ivd.data == NULL)
            return ENOMEM;
        (void) memcpy(ivd.data, iv, ivd.length);
        pivd = &ivd;
    } else {
        pivd = NULL;
    }

    code = kg_translate_iov(context, proto, dce_style, ec, rrc,
                            key->keyblock.enctype, iov, iov_count,
                            &kiov, &kiov_count);
    if (code == 0) {
        code = krb5_k_encrypt_iov(context, key, usage, pivd, kiov, kiov_count);
        free(kiov);
    }

    if (pivd != NULL)
        free(pivd->data);

    return code;
}

/* length is the length of the cleartext. */

krb5_error_code
kg_decrypt_iov(krb5_context context, int proto, int dce_style, size_t ec,
               size_t rrc, krb5_key key, int usage, krb5_pointer iv,
               gss_iov_buffer_desc *iov, int iov_count)
{
    krb5_error_code code;
    size_t blocksize;
    krb5_data ivd, *pivd;
    size_t kiov_count;
    krb5_crypto_iov *kiov;

    if (iv) {
        code = krb5_c_block_size(context, key->keyblock.enctype, &blocksize);
        if (code)
            return(code);

        ivd.length = blocksize;
        ivd.data = MALLOC(ivd.length);
        if (ivd.data == NULL)
            return ENOMEM;
        (void) memcpy(ivd.data, iv, ivd.length);
        pivd = &ivd;
    } else {
        pivd = NULL;
    }

    code = kg_translate_iov(context, proto, dce_style, ec, rrc,
                            key->keyblock.enctype, iov, iov_count,
                            &kiov, &kiov_count);
    if (code == 0) {
        code = krb5_k_decrypt_iov(context, key, usage, pivd, kiov, kiov_count);
        free(kiov);
    }

    if (pivd != NULL)
        free(pivd->data);

    return code;
}

krb5_error_code
kg_arcfour_docrypt_iov(krb5_context context, const krb5_keyblock *keyblock,
                       int usage, const unsigned char *kd_data,
                       size_t kd_data_len, gss_iov_buffer_desc *iov,
                       int iov_count)
{
    krb5_error_code code;
    krb5_data kd = make_data((char *) kd_data, kd_data_len);
    krb5int_access kaccess;
    krb5_crypto_iov *kiov = NULL;
    size_t kiov_count = 0;

    code = krb5int_accessor (&kaccess, KRB5INT_ACCESS_VERSION);
    if (code)
        return code;
    code = kg_translate_iov(context, 0 /* proto */, 0 /* dce_style */,
                            0 /* ec */, 0 /* rrc */, keyblock->enctype,
                            iov, iov_count, &kiov, &kiov_count);
    if (code)
        return code;
    code = (*kaccess.arcfour_gsscrypt)(keyblock, usage, &kd, kiov, kiov_count);
    free(kiov);
    return code;
}

krb5_cryptotype
kg_translate_flag_iov(OM_uint32 type)
{
    krb5_cryptotype ktype;

    switch (GSS_IOV_BUFFER_TYPE(type)) {
    case GSS_IOV_BUFFER_TYPE_DATA:
    case GSS_IOV_BUFFER_TYPE_PADDING:
        ktype = KRB5_CRYPTO_TYPE_DATA;
        break;
    case GSS_IOV_BUFFER_TYPE_SIGN_ONLY:
        ktype = KRB5_CRYPTO_TYPE_SIGN_ONLY;
        break;
    default:
        ktype = KRB5_CRYPTO_TYPE_EMPTY;
        break;
    }

    return ktype;
}

gss_iov_buffer_t
kg_locate_iov(gss_iov_buffer_desc *iov, int iov_count, OM_uint32 type)
{
    int i;
    gss_iov_buffer_t p = GSS_C_NO_IOV_BUFFER;

    if (iov == GSS_C_NO_IOV_BUFFER)
        return GSS_C_NO_IOV_BUFFER;

    for (i = iov_count - 1; i >= 0; i--) {
        if (GSS_IOV_BUFFER_TYPE(iov[i].type) == type) {
            if (p == GSS_C_NO_IOV_BUFFER)
                p = &iov[i];
            else
                return GSS_C_NO_IOV_BUFFER;
        }
    }

    return p;
}

void
kg_iov_msglen(gss_iov_buffer_desc *iov, int iov_count, size_t *data_length_p,
              size_t *assoc_data_length_p)
{
    int i;
    size_t data_length = 0, assoc_data_length = 0;

    assert(iov != GSS_C_NO_IOV_BUFFER);

    *data_length_p = *assoc_data_length_p = 0;

    for (i = 0; i < iov_count; i++) {
        OM_uint32 type = GSS_IOV_BUFFER_TYPE(iov[i].type);

        if (type == GSS_IOV_BUFFER_TYPE_SIGN_ONLY)
            assoc_data_length += iov[i].buffer.length;

        if (type == GSS_IOV_BUFFER_TYPE_DATA ||
            type == GSS_IOV_BUFFER_TYPE_SIGN_ONLY)
            data_length += iov[i].buffer.length;
    }

    *data_length_p = data_length;
    *assoc_data_length_p = assoc_data_length;
}

void
kg_release_iov(gss_iov_buffer_desc *iov, int iov_count)
{
    int i;
    OM_uint32 min_stat;

    assert(iov != GSS_C_NO_IOV_BUFFER);

    for (i = 0; i < iov_count; i++) {
        if (iov[i].type & GSS_IOV_BUFFER_FLAG_ALLOCATED) {
            gss_release_buffer(&min_stat, &iov[i].buffer);
            iov[i].type &= ~(GSS_IOV_BUFFER_FLAG_ALLOCATED);
        }
    }
}

OM_uint32
kg_fixup_padding_iov(OM_uint32 *minor_status, gss_iov_buffer_desc *iov,
                     int iov_count)
{
    gss_iov_buffer_t padding = NULL;
    gss_iov_buffer_t data = NULL;
    size_t padlength, relative_padlength;
    unsigned char *p;
    OM_uint32 minor;

    data = kg_locate_iov(iov, iov_count, GSS_IOV_BUFFER_TYPE_DATA);
    padding = kg_locate_iov(iov, iov_count, GSS_IOV_BUFFER_TYPE_PADDING);

    if (data == NULL) {
        *minor_status = 0;
        return GSS_S_COMPLETE;
    }

    if (padding == NULL || padding->buffer.length == 0) {
        *minor_status = EINVAL;
        return GSS_S_FAILURE;
    }

    p = (unsigned char *)padding->buffer.value;
    padlength = p[padding->buffer.length - 1];

    if (data->buffer.length + padding->buffer.length < padlength ||
        padlength == 0) {
        *minor_status = (OM_uint32)KRB5_BAD_MSIZE;
        return GSS_S_DEFECTIVE_TOKEN;
    }

    /*
     * kg_unseal_stream_iov() will place one byte of padding in the
     * padding buffer; its true value is unknown until after decryption.
     *
     * relative_padlength contains the number of bytes to compensate the
     * padding and data buffers by; it will be zero if the caller manages
     * the padding length.
     *
     * If the caller manages the padding length, then relative_padlength
     * wil be zero.
     *
     * eg. if the buffers are structured as follows:
     *
     *      +---DATA---+-PAD-+
     *      | ABCDE444 | 4   |
     *      +----------+-----+
     *
     * after compensation they would look like:
     *
     *      +-DATA--+-PAD--+
     *      | ABCDE | NULL |
     *      +-------+------+
     */
    relative_padlength = padlength - padding->buffer.length;

    assert(data->buffer.length >= relative_padlength);

    data->buffer.length -= relative_padlength;

    if (padding->type & GSS_IOV_BUFFER_FLAG_ALLOCATED) {
        gss_release_buffer(&minor, &padding->buffer);
        padding->type &= ~(GSS_IOV_BUFFER_FLAG_ALLOCATED);
    }

    padding->buffer.length = 0;
    padding->buffer.value = NULL;

    return GSS_S_COMPLETE;
}

int
kg_map_toktype(int proto, int toktype)
{
    int toktype2;

    if (proto)
        switch (toktype) {
        case KG_TOK_SIGN_MSG:
            toktype2 = KG2_TOK_MIC_MSG;
            break;
        case KG_TOK_WRAP_MSG:
            toktype2 = KG2_TOK_WRAP_MSG;
            break;
        case KG_TOK_DEL_CTX:
            toktype2 = KG2_TOK_DEL_CTX;
            break;
        default:
            toktype2 = toktype;
            break;
        }
    else
        toktype2 = toktype;

    return toktype2;
}

krb5_boolean
kg_integ_only_iov(gss_iov_buffer_desc *iov, int iov_count)
{
    int i;
    krb5_boolean has_conf_data = FALSE;

    assert(iov != GSS_C_NO_IOV_BUFFER);

    for (i = 0; i < iov_count; i++) {
        if (GSS_IOV_BUFFER_TYPE(iov[i].type) == GSS_IOV_BUFFER_TYPE_DATA) {
            has_conf_data = TRUE;
            break;
        }
    }

    return (has_conf_data == FALSE);
}

krb5_error_code
kg_allocate_iov(gss_iov_buffer_t iov, size_t size)
{
    assert(iov != GSS_C_NO_IOV_BUFFER);
    assert(iov->type & GSS_IOV_BUFFER_FLAG_ALLOCATE);

    iov->buffer.length = size;
    iov->buffer.value = xmalloc(size);
    if (iov->buffer.value == NULL) {
        iov->buffer.length = 0;
        return ENOMEM;
    }

    iov->type |= GSS_IOV_BUFFER_FLAG_ALLOCATED;

    return 0;
}
#endif /* #ifndef _KERNEL */
#endif /**************** END IFDEF'ed OUT *******************************/
