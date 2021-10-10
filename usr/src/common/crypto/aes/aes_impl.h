/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_AES_IMPL_H
#define	_AES_IMPL_H

/*
 * Common definitions used by AES.
 */

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>

/* Similar to sysmacros.h IS_P2ALIGNED, but checks two pointers: */
#define	IS_P2ALIGNED2(v, w, a) \
	((((uintptr_t)(v) | (uintptr_t)(w)) & ((uintptr_t)(a) - 1)) == 0)

#define	AES_BLOCK_LEN	16	/* bytes */
/* Round constant length, in number of 32-bit elements: */
#define	RC_LENGTH	(5 * ((AES_BLOCK_LEN) / 4 - 2))

/* AES key size definitions */
#define	AES_MINBITS		128
#define	AES_MINBYTES		((AES_MINBITS) >> 3)
#define	AES_MAXBITS		256
#define	AES_MAXBYTES		((AES_MAXBITS) >> 3)

#define	AES_MIN_KEY_BYTES	((AES_MINBITS) >> 3)
#define	AES_MAX_KEY_BYTES	((AES_MAXBITS) >> 3)
#define	AES_192_KEY_BYTES	24
#define	AES_IV_LEN		16	/* bytes */

/*
 * The AES encryption or decryption key schedule has 11, 13, or 15 elements.
 * The AES key schedule may be implemented with 32- or 64-bit elements.
 */
#define	AES_32BIT_KS		32
#define	AES_64BIT_KS		64

#define	MAX_AES_NR		14 /* Maximum number of rounds */
#define	MAX_AES_NB		4  /* Number of columns comprising a state */

typedef union {
#if defined(sun4u) || defined(sun4v)
	uint64_t	ks64[((MAX_AES_NR) + 1) * (MAX_AES_NB)];
#endif
	uint32_t	ks32[((MAX_AES_NR) + 1) * (MAX_AES_NB)];
} aes_ks_t;

typedef struct aes_key aes_key_t;
struct aes_key {
	aes_ks_t	encr_ks;  /* encryption key schedule */
	aes_ks_t	decr_ks;  /* decryption key schedule */
	int		nr;	  /* number of rounds (10, 12, or 14) */
	int		type;	  /* key schedule size (32 or 64 bits) */
#ifdef __amd64			  /* 128-bit alignment for Intel AES-NI. */
	long double	align128; /* MUST be last field to force alignment */
#endif	/* __amd64 */
};

/*
 * Core AES functions.
 * ks and keysched are pointers to aes_key_t.
 * They are declared void* as they are intended to be opaque types.
 * Use function aes_alloc_keysched() to allocate memory for ks and keysched.
 */
extern void *aes_alloc_keysched(size_t *size, int kmflag);
extern void aes_free_keysched(void *ptr, size_t size);
extern void aes_init_keysched(const uint8_t *cipherKey, uint_t keyBits,
	void *keysched);
extern int aes_encrypt_block(const void *ks, const uint8_t *pt, uint8_t *ct);
extern int aes_decrypt_block(const void *ks, const uint8_t *ct, uint8_t *pt);

/*
 * AES mode utility functions.
 * The first 2 functions operate on 16-byte AES blocks.
 */
extern void aes_copy_block(uint8_t *in, uint8_t *out);
extern void aes_xor_block(uint8_t *data, uint8_t *dst);

/* Note: ctx is a pointer to aes_ctx_t defined in modes.h */
extern int aes_encrypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out);
extern int aes_decrypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out);
extern void aes_cbcmac_multiblock(void *ctx, uint64_t *input, size_t len);

#ifdef _KERNEL

typedef enum aes_mech_type {
	CRYPTO_AES_ECB,		/* SUN_CKM_AES_ECB */
	CRYPTO_AES_CBC,		/* SUN_CKM_AES_CBC */
	CRYPTO_AES_CTR,		/* SUN_CKM_AES_CTR */
	CRYPTO_AES_CCM,		/* SUN_CKM_AES_CCM */
	CRYPTO_AES_GCM,		/* SUN_CKM_AES_GCM */
	CRYPTO_AES_GMAC,	/* SUN_CKM_AES_GMAC */
	CRYPTO_AES_CFB128	/* SUN_CKM_AES_CFB128 */
} aes_mech_type_t;

#endif /* _KERNEL */

#ifdef sun4v
#ifdef _KERNEL
extern boolean_t yf_aes_instructions_present(void);
#endif
/* SPARC assembly language functions */
extern void yf_aes_expand128(uint64_t *rk, const uint32_t *key);
extern void yf_aes_expand192(uint64_t *rk, const uint32_t *key);
extern void yf_aes_expand256(uint64_t *rk, const uint32_t *key);
extern void yf_aes_encrypt128(const uint64_t *rk, const uint32_t *pt,
    uint32_t *ct);
extern void yf_aes_encrypt192(const uint64_t *rk, const uint32_t *pt,
    uint32_t *ct);
extern void yf_aes_encrypt256(const uint64_t *rk, const uint32_t *pt,
    uint32_t *ct);
extern void yf_aes_decrypt128(const uint64_t *rk, const uint32_t *ct,
    uint32_t *pt);
extern void yf_aes_decrypt192(const uint64_t *rk, const uint32_t *ct,
    uint32_t *pt);
extern void yf_aes_decrypt256(const uint64_t *rk, const uint32_t *ct,
    uint32_t *pt);
extern void yf_aes128_load_keys_for_encrypt(uint64_t *ks);
extern void yf_aes192_load_keys_for_encrypt(uint64_t *ks);
extern void yf_aes256_load_keys_for_encrypt(uint64_t *ks);
extern void yf_aes128_ecb_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *dummy);
extern void yf_aes192_ecb_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *dummy);
extern void yf_aes256_ecb_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *dummy);
extern void yf_aes128_cbc_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *iv);
extern void yf_aes192_cbc_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *iv);
extern void yf_aes256_cbc_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *iv);
extern void yf_aes128_cbc_mac(uint64_t *ks, uint64_t *asm_in,
    uint64_t *mac, size_t blocks_to_digest);
extern void yf_aes192_cbc_mac(uint64_t *ks, uint64_t *asm_in,
    uint64_t *mac, size_t blocks_to_digest);
extern void yf_aes256_cbc_mac(uint64_t *ks, uint64_t *asm_in,
    uint64_t *mac, size_t blocks_to_digest);
extern void yf_aes128_ctr_crypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *iv);
extern void yf_aes192_ctr_crypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *iv);
extern void yf_aes256_ctr_crypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *iv);
extern void yf_aes128_cfb128_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *iv);
extern void yf_aes192_cfb128_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *iv);
extern void yf_aes256_cfb128_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *iv);
extern void yf_aes128_load_keys_for_decrypt(uint64_t *ks);
extern void yf_aes192_load_keys_for_decrypt(uint64_t *ks);
extern void yf_aes256_load_keys_for_decrypt(uint64_t *ks);
extern void yf_aes128_ecb_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_decrypt, uint64_t *dummy);
extern void yf_aes192_ecb_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_decrypt, uint64_t *dummy);
extern void yf_aes256_ecb_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_decrypt, uint64_t *dummy);
extern void yf_aes128_cbc_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_decrypt, uint64_t *iv);
extern void yf_aes192_cbc_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_decrypt, uint64_t *iv);
extern void yf_aes256_cbc_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_decrypt, uint64_t *iv);
extern void yf_aes128_cfb128_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *iv);
extern void yf_aes192_cfb128_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *iv);
extern void yf_aes256_cfb128_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *iv);
#endif

/*
 * The following definitions and declarations are only used by AES FIPS POST
 */
#ifdef _AES_FIPS_POST

#include <fips/fips_post.h>

/*
 * FIPS preprocessor directives for AES-ECB and AES-CBC.
 */
#define	FIPS_AES_BLOCK_SIZE		16  /* 128-bits */
#define	FIPS_AES_ENCRYPT_LENGTH		16  /* 128-bits */
#define	FIPS_AES_DECRYPT_LENGTH		16  /* 128-bits */
#define	FIPS_AES_128_KEY_SIZE		16  /* 128-bits */
#define	FIPS_AES_192_KEY_SIZE		24  /* 192-bits */
#define	FIPS_AES_256_KEY_SIZE		32  /* 256-bits */

#ifdef _KERNEL

#undef	CKM_AES_ECB
#undef	CKM_AES_CBC
#undef	CKM_AES_CTR

#define	CKM_AES_ECB		CRYPTO_AES_ECB
#define	CKM_AES_CBC		CRYPTO_AES_CBC
#define	CKM_AES_CTR		CRYPTO_AES_CTR

#define	CK_RV			int
#define	CK_ULONG		ulong_t
#define	CK_BYTE_PTR		uchar_t *
#define	CK_ULONG_PTR		ulong_t *
#define	CK_MECHANISM_TYPE	aes_mech_type_t


typedef struct soft_aes_ctx {
	void *key_sched;		/* pointer to key schedule */
	size_t keysched_len;		/* Length of the key schedule */
	uint8_t ivec[AES_BLOCK_LEN];	/* initialization vector */
	uint8_t data[AES_BLOCK_LEN];	/* for use by update */
	size_t remain_len;		/* for use by update */
	void *mode_ctx;			/* context for various modes */
} soft_aes_ctx_t;
#endif /* _KERNEL */

/* AES FIPS functions */
extern int fips_aes_post(int aes_key_size);

#ifdef _AES_IMPL
#ifndef _KERNEL
extern soft_aes_ctx_t *fips_aes_build_context(uint8_t *key, int key_len,
    uint8_t *iv, CK_MECHANISM_TYPE mechanism);
#else
extern soft_aes_ctx_t *fips_aes_build_context(uint8_t *key, int key_len,
    uint8_t *iv, CK_MECHANISM_TYPE mechanism, boolean_t is_encrypt_init);
#endif /* _KERNEL */
extern void fips_aes_free_context(soft_aes_ctx_t *soft_aes_ctx);
extern CK_RV fips_aes_encrypt(soft_aes_ctx_t *soft_aes_ctx, CK_BYTE_PTR in_buf,
    CK_ULONG ulDataLen, CK_BYTE_PTR out_buf, CK_ULONG_PTR pulEncryptedLen,
    CK_MECHANISM_TYPE mechanism);
extern CK_RV fips_aes_decrypt(soft_aes_ctx_t *soft_aes_ctx, CK_BYTE_PTR in_buf,
    CK_ULONG ulEncryptedLen, CK_BYTE_PTR out_buf, CK_ULONG_PTR pulDataLen,
    CK_MECHANISM_TYPE mechanism);
#endif /* _AES_IMPL */
#endif /* _AES_FIPS_POST */

/* High-level AES functions */
extern int aes_encrypt_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t tmpl,
    crypto_req_handle_t req);
extern int aes_decrypt_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t tmpl,
    crypto_req_handle_t req);
extern int aes_common_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t tmpl,
    crypto_req_handle_t req, boolean_t is_encrypt_init);
extern int aes_encrypt_update(crypto_ctx_t *ctx, crypto_data_t *plaintext,
    crypto_data_t *ciphertext, crypto_req_handle_t req);
extern int aes_decrypt_update(crypto_ctx_t *ctx, crypto_data_t *ciphertext,
    crypto_data_t *plaintext, crypto_req_handle_t req);
extern int aes_encrypt_final(crypto_ctx_t *ctx, crypto_data_t *data,
    crypto_req_handle_t req);
extern int aes_decrypt_final(crypto_ctx_t *ctx, crypto_data_t *data,
    crypto_req_handle_t req);
extern int aes_encrypt_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *plaintext, crypto_data_t *ciphertext,
    crypto_spi_ctx_template_t tmpl, crypto_req_handle_t req);
extern int aes_decrypt_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *ciphertext, crypto_data_t *plaintext,
    crypto_spi_ctx_template_t tmpl, crypto_req_handle_t req);
extern int aes_create_ctx_template(crypto_provider_handle_t provider,
    crypto_mechanism_t *mechanism, crypto_key_t *key,
    crypto_spi_ctx_template_t *tmpl, size_t *tmpl_size,
    crypto_req_handle_t req);
extern int aes_mac_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *mac,
    crypto_spi_ctx_template_t tmpl, crypto_req_handle_t req);
extern int aes_mac_verify_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *mac,
    crypto_spi_ctx_template_t tmpl, crypto_req_handle_t req);
extern int aes_free_context(crypto_ctx_t *ctx);

#ifdef	__cplusplus
}
#endif

#endif	/* _AES_IMPL_H */
