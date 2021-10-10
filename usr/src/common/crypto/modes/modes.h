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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_COMMON_CRYPTO_MODES_H
#define	_COMMON_CRYPTO_MODES_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/strsun.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/rwlock.h>
#include <sys/kmem.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>

#ifdef _KERNEL
#include <sys/debug.h>			/* ASSERT */
#else
#include <assert.h>
#define	ASSERT3U(x, op, z)	assert((x) op /* */ (z))
#define	ASSERT3P(x, op, z)	assert((x) op /* */ (z))
#define	ASSERT			assert
#endif	/* _KERNEL */

#define	ECB_MODE			0x00000002
#define	CBC_MODE			0x00000004
#define	CTR_MODE			0x00000008
#define	CCM_MODE			0x00000010
#define	GCM_MODE			0x00000020
#define	GMAC_MODE			0x00000040
#define	CFB128_MODE			0x00000080
#define	MODE_MASK (ECB_MODE | CBC_MODE | CTR_MODE | CCM_MODE | \
		GCM_MODE | GMAC_MODE | CFB128_MODE)

/* leaving room for future modes flags */
#define	NO_CTR_FINAL			0x00001000

#ifdef	__amd64
/*
 * These constants are used to align memory allocated by kmem_zalloc().  Some
 * Intel SSE instructions require 0 mod 16 alignment of data.  kmem_zalloc()
 * only performs alignment on 0 mod 64-sized chunks (otherwise is aligned
 * 0 mod 8).  Userland calloc() aligns on 0 mod 16 addresses.
 */
#define	CTX_ALIGN16		16ULL
#define	CTX_ALIGN16_MASK	(~(CTX_ALIGN16 - 1ULL))
#define	CTX_ALIGN64		64ULL
#define	CTX_ALIGN64_MASK	(~(CTX_ALIGN64 - 1ULL))
#endif

/*
 * cc_keysched:		Pointer to key schedule.
 *
 * cc_keysched_len:	Length of the key schedule.
 *
 * cc_remainder:	This is for residual data, i.e. data that can't
 *			be processed because there are too few bytes.
 *			Must wait until more data arrives.
 *
 * cc_remainder_len:	Number of bytes in cc_remainder.
 *
 * cc_iv:		Scratch buffer that sometimes contains the IV.
 *
 * cc_lastp:		Pointer to previous block of ciphertext.
 *
 * cc_copy_to:		Pointer to where encrypted residual data needs
 *			to be copied.
 *
 * cc_flags:		PROVIDER_OWNS_KEY_SCHEDULE
 *			When a context is freed, it is necessary
 *			to know whether the key schedule was allocated
 *			by the caller, or internally, e.g. an init routine.
 *			If allocated by the latter, then it needs to be freed.
 *
 * cc_flags & MODE_MASK:
 *			ECB_MODE, CBC_MODE, CTR_MODE, CCM_MODE,
 *			GCM_MODE, GMAC_MODE (uses struct gcm_ctx), or
 *			CFB128_MODE.
 *			If no bit in MODE_MASK is set, assume ECB_MODE.
 * cc_flags:		NO_CTR_FINAL
 *			Used in the kernel only to indicate to the
 *			ctr_mode_final() function that it has to do nothing
 *			as its job has already been done.
 */
struct common_ctx {
	void		*cc_keysched;
	size_t		cc_keysched_len;
	uint64_t	cc_iv[2];		/* align 0 mod 16 for amd64 */
	uint64_t	cc_remainder[2];	/* align 0 mod 16 for amd64 */
	size_t		cc_remainder_len;
	uint8_t		*cc_lastp;
	uint8_t		*cc_copy_to;
	uint32_t	cc_flags;
#ifdef __amd64
	long double	align128;		/* align 0 mod 16 for amd64 */
#endif  /* __amd64 */
};

typedef struct common_ctx common_ctx_t;

typedef struct ecb_ctx {
	struct common_ctx ecb_common;
	uint64_t ecb_lastblock[2];
#if defined(__amd64) && defined(_KERNEL)
	/* Pad struct size to 0 mod 64 to force at least 0 mod 16 alignment: */
	long double	align128[1];
#endif  /* __amd64 && _KERNEL */
} ecb_ctx_t;

#define	ecb_keysched		ecb_common.cc_keysched
#define	ecb_keysched_len	ecb_common.cc_keysched_len
#define	ecb_iv			ecb_common.cc_iv
#define	ecb_remainder		ecb_common.cc_remainder
#define	ecb_remainder_len	ecb_common.cc_remainder_len
#define	ecb_lastp		ecb_common.cc_lastp
#define	ecb_copy_to		ecb_common.cc_copy_to
#define	ecb_flags		ecb_common.cc_flags

typedef struct cbc_ctx {
	struct common_ctx cbc_common;
	uint64_t cbc_lastblock[2]; /* feedback to XOR with next block */
#if defined(__amd64) && defined(_KERNEL)
	/* Pad struct size to 0 mod 64 to force at least 0 mod 16 alignment: */
	long double	align128[1];
#endif  /* __amd64 && _KERNEL */
} cbc_ctx_t;

#define	cbc_keysched		cbc_common.cc_keysched
#define	cbc_keysched_len	cbc_common.cc_keysched_len
#define	cbc_iv			cbc_common.cc_iv
#define	cbc_remainder		cbc_common.cc_remainder
#define	cbc_remainder_len	cbc_common.cc_remainder_len
#define	cbc_lastp		cbc_common.cc_lastp
#define	cbc_copy_to		cbc_common.cc_copy_to
#define	cbc_flags		cbc_common.cc_flags

typedef struct cfb_ctx {
	struct common_ctx cfb_common;
} cfb_ctx_t;

#define	cfb_keysched		cfb_common.cc_keysched
#define	cfb_keysched_len	cfb_common.cc_keysched_len
#define	cfb_iv			cfb_common.cc_iv
#define	cfb_unused_bytes	cfb_common.cc_remainder_len
#define	cfb_lastp		cfb_common.cc_lastp
#define	cfb_flags		cfb_common.cc_flags

/*
 * Mask for counter, 1 - 128 bits in length:
 * ctr_lower_mask		Bit-mask for lower 8 bytes of counter block.
 * ctr_upper_mask		Bit-mask for upper 8 bytes of counter block.
 * Note: ctr_*_mask is in network byte order (big-endian).
 */
typedef struct ctr_ctx {
	struct common_ctx ctr_common;
	uint64_t ctr_lower_mask;
	uint64_t ctr_upper_mask;
	uint32_t ctr_tmp[4];	/* contains block encryption result */
#if defined(__amd64) && defined(_KERNEL)
	/*
	 * For ctr_ctx_t, size is already 0 mod 64. No need to pad.
	 */
#endif  /* __amd64 && _KERNEL */
} ctr_ctx_t;

/*
 * ctr_cb			Counter block.
 */
#define	ctr_keysched		ctr_common.cc_keysched
#define	ctr_keysched_len	ctr_common.cc_keysched_len
#define	ctr_cb			ctr_common.cc_iv /* always big-endian */
#define	ctr_remainder		ctr_common.cc_remainder
#define	ctr_remainder_len	ctr_common.cc_remainder_len
#define	ctr_lastp		ctr_common.cc_lastp
#define	ctr_copy_to		ctr_common.cc_copy_to
#define	ctr_flags		ctr_common.cc_flags

/*
 *
 * ccm_mac_len:		Stores length of the MAC in CCM mode.
 * ccm_mac_buf:		Stores the intermediate value for MAC in CCM encrypt.
 *			In CCM decrypt, stores the input MAC value.
 * ccm_data_len:	Length of the plaintext for CCM mode encrypt, or
 *			length of the ciphertext for CCM mode decrypt.
 * ccm_processed_data_len:
 *			Length of processed plaintext in CCM mode encrypt,
 *			or length of processed ciphertext for CCM mode decrypt.
 * ccm_processed_mac_len:
 *			Length of MAC data accumulated in CCM mode decrypt.
 *
 * ccm_pt_buf:		Only used in CCM mode decrypt.  It stores the
 *			decrypted plaintext to be returned when
 *			MAC verification succeeds in decrypt_final.
 *			Memory for this should be allocated in the AES module.
 *
 */
typedef struct ccm_ctx {
	struct ctr_ctx ccm_ctr;
	uint64_t ccm_tmp[2];
	size_t ccm_mac_len;
	uint64_t ccm_mac_buf[2];
	size_t ccm_data_len;
	size_t ccm_processed_data_len;
	size_t ccm_processed_mac_len;
	uint64_t *ccm_pt_buf;
	uint64_t ccm_mac_input_buf[2];
	uint64_t ccm_counter_mask;
} ccm_ctx_t;

#define	ccm_keysched		ccm_ctr.ctr_common.cc_keysched
#define	ccm_keysched_len	ccm_ctr.ctr_common.cc_keysched_len
#define	ccm_cb			ccm_ctr.ctr_common.cc_iv
#define	ccm_remainder		ccm_ctr.ctr_common.cc_remainder
#define	ccm_remainder_len	ccm_ctr.ctr_common.cc_remainder_len
#define	ccm_lastp		ccm_ctr.ctr_common.cc_lastp
#define	ccm_copy_to		ccm_ctr.ctr_common.cc_copy_to
#define	ccm_flags		ccm_ctr.ctr_common.cc_flags

/*
 * gcm_tag_len:		Length of authentication tag.
 *
 * gcm_ghash:		Stores output from the GHASH function.
 *
 * gcm_processed_data_len:
 *			Length of processed plaintext (encrypt) or
 *			length of processed ciphertext (decrypt).
 *
 * gcm_pt_buf:		Stores the decrypted plaintext returned by
 *			decrypt_final when the computed authentication
 *			tag matches the	user supplied tag.
 *
 * gcm_pt_buf_len:	Length of the plaintext buffer.
 *
 * gcm_H:		Subkey.
 *
 * gcm_J0:		Pre-counter block generated from the IV.
 *
 * gcm_len_a_len_c:	64-bit representations of the bit lengths of
 *			AAD and ciphertext.
 *
 * gcm_kmflag:		Current value of kmflag. Used only for allocating
 *			the plaintext buffer during decryption.
 */
typedef struct gcm_ctx {
	struct ctr_ctx gcm_ctr;
	size_t gcm_tag_len;
	size_t gcm_processed_data_len;
	size_t gcm_pt_buf_len;
	uint64_t gcm_tmp[2];
	uint64_t gcm_ghash[2];
	uint64_t gcm_H[2];
	uint64_t gcm_J0[2];
	uint64_t gcm_len_a_len_c[2];
	uint64_t *gcm_pt_buf;
	int gcm_kmflag;
} gcm_ctx_t;

#define	gcm_keysched		gcm_ctr.ctr_common.cc_keysched
#define	gcm_keysched_len	gcm_ctr.ctr_common.cc_keysched_len
#define	gcm_cb			gcm_ctr.ctr_common.cc_iv
#define	gcm_remainder		gcm_ctr.ctr_common.cc_remainder
#define	gcm_remainder_len	gcm_ctr.ctr_common.cc_remainder_len
#define	gcm_lastp		gcm_ctr.ctr_common.cc_lastp
#define	gcm_copy_to		gcm_ctr.ctr_common.cc_copy_to
#define	gcm_flags		gcm_ctr.ctr_common.cc_flags

#define	AES_GMAC_IV_LEN		12
#define	AES_GMAC_TAG_BITS	128

typedef struct aes_ctx {
	union {
		ecb_ctx_t acu_ecb;
		cbc_ctx_t acu_cbc;
		ctr_ctx_t acu_ctr;
		ccm_ctx_t acu_ccm;
		gcm_ctx_t acu_gcm;
	} acu;
} aes_ctx_t;

#define	ac_flags		acu.acu_ecb.ecb_common.cc_flags
#define	ac_remainder_len	acu.acu_ecb.ecb_common.cc_remainder_len
#define	ac_keysched		acu.acu_ecb.ecb_common.cc_keysched
#define	ac_keysched_len		acu.acu_ecb.ecb_common.cc_keysched_len
#define	ac_iv			acu.acu_ecb.ecb_common.cc_iv
#define	ac_lastp		acu.acu_ecb.ecb_common.cc_lastp
#define	ac_pt_buf		acu.acu_ccm.ccm_pt_buf
#define	ac_mac_len		acu.acu_ccm.ccm_mac_len
#define	ac_data_len		acu.acu_ccm.ccm_data_len
#define	ac_processed_mac_len	acu.acu_ccm.ccm_processed_mac_len
#define	ac_processed_data_len	acu.acu_ccm.ccm_processed_data_len
#define	ac_tag_len		acu.acu_gcm.gcm_tag_len

typedef struct blowfish_ctx {
	union {
		ecb_ctx_t bcu_ecb;
		cbc_ctx_t bcu_cbc;
	} bcu;
} blowfish_ctx_t;

#define	bc_flags		bcu.bcu_ecb.ecb_common.cc_flags
#define	bc_remainder_len	bcu.bcu_ecb.ecb_common.cc_remainder_len
#define	bc_keysched		bcu.bcu_ecb.ecb_common.cc_keysched
#define	bc_keysched_len		bcu.bcu_ecb.ecb_common.cc_keysched_len
#define	bc_iv			bcu.bcu_ecb.ecb_common.cc_iv
#define	bc_lastp		bcu.bcu_ecb.ecb_common.cc_lastp

typedef struct des_ctx {
	union {
		ecb_ctx_t dcu_ecb;
		cbc_ctx_t dcu_cbc;
	} dcu;
} des_ctx_t;

#define	dc_flags		dcu.dcu_ecb.ecb_common.cc_flags
#define	dc_remainder_len	dcu.dcu_ecb.ecb_common.cc_remainder_len
#define	dc_keysched		dcu.dcu_ecb.ecb_common.cc_keysched
#define	dc_keysched_len		dcu.dcu_ecb.ecb_common.cc_keysched_len
#define	dc_iv			dcu.dcu_ecb.ecb_common.cc_iv
#define	dc_lastp		dcu.dcu_ecb.ecb_common.cc_lastp


#ifdef sun4v

typedef struct yf_functions yf_functions_t;

typedef int yf_process_ipb_t(void *ctx, uint8_t **in, iovec_t *out_bufs,
    uint_t *outbufind, offset_t *offset, size_t *length, yf_functions_t *funcs,
    size_t *nr_bytes_written, boolean_t *need_loadkey);

typedef int yf_process_cwb_t(void *ctx, uint8_t *in, uint8_t *out,
    size_t length, yf_functions_t *funcs, boolean_t *need_loadkey);

typedef int yf_process_lpb_t(void *ctx, uint8_t *in, uint8_t *out,
    size_t length, yf_functions_t *funcs, size_t *nr_bytes_written,
    boolean_t *need_loadkey);

typedef void yf_load_keys_fun_t(uint64_t *key);

typedef void yf_contig_blocks_fun_t(uint64_t *key, uint64_t *in, uint64_t *out,
    size_t amount_to_encrypt, uint64_t *iv);

typedef void yf_mac_fun_t(uint64_t *key, uint64_t *in, uint64_t *mac,
    size_t blocks_to_digest);

struct yf_functions {
	yf_process_ipb_t	*process_initial_partial_block;
	yf_process_cwb_t 	*process_contiguous_whole_blocks;
	yf_process_lpb_t	*process_last_partial_block;
	yf_load_keys_fun_t	*load_keys;
	yf_contig_blocks_fun_t	*contig_blocks;
	yf_mac_fun_t		*mac;
};

#endif

extern int ecb_cipher_contiguous_blocks(ecb_ctx_t *ctx, char *data,
    size_t length, crypto_data_t *out, size_t block_size,
    int (*cipher)(const void *, const uint8_t *, uint8_t *));

extern int cbc_encrypt_contiguous_blocks(cbc_ctx_t *ctx, char *data,
    size_t length, crypto_data_t *out, size_t block_size,
    int (*encrypt)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

extern int cbc_decrypt_contiguous_blocks(cbc_ctx_t *ctx, char *data,
    size_t length, crypto_data_t *out, size_t block_size,
    int (*decrypt)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

extern int cfb_mode_contiguous_blocks(cfb_ctx_t *ctx, char *data,
    size_t length, crypto_data_t *out, size_t block_size,
    int (*cipher)(const void *ks, const uint8_t *pt, uint8_t *ct),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *),
    boolean_t is_encrypt);

extern int ctr_mode_contiguous_blocks(ctr_ctx_t *ctx, char *data,
    size_t length, crypto_data_t *out, size_t block_size,
    int (*cipher)(const void *, const uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

extern int ccm_mode_encrypt_contiguous_blocks(ccm_ctx_t *ctx, char *data,
    size_t length, crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

extern int ccm_mode_decrypt_contiguous_blocks(ccm_ctx_t *ctx, char *data,
    size_t length, crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

extern int gcm_mode_encrypt_contiguous_blocks(gcm_ctx_t *ctx, char *data,
    size_t length, crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

extern int gcm_mode_decrypt_contiguous_blocks(gcm_ctx_t *ctx, char *data,
    size_t length, crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

int ccm_encrypt_final(ccm_ctx_t *ctx, crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

int gcm_encrypt_final(gcm_ctx_t *ctx, crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

extern int ccm_decrypt_final(ccm_ctx_t *ctx, crypto_data_t *out,
    size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    int (*decrypt_contiguous_blocks)(void *ctx, char *data, size_t length,
    crypto_data_t *out),
    void (*cbcmac_multiblock)(void *ctx, uint64_t *data, size_t length));

extern int gcm_decrypt_final(
    gcm_ctx_t *ctx, crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *),
    int (*decrypt_contiguous_blocks)(void *ctx, char *data, size_t length,
    crypto_data_t *out));

extern int cbc_init_ctx(cbc_ctx_t *ctx, char *param, size_t param_len,
    size_t block_size,
    void (*copy_block)(uint8_t *, uint64_t *));

extern int ctr_init_ctx(ctr_ctx_t *ctr_ctx, ulong_t count, uint8_t *cb,
    void (*copy_block)(uint8_t *, uint8_t *));

extern int ccm_init_ctx(ccm_ctx_t *ccm_ctx, char *param, int kmflag,
    boolean_t is_encrypt_init, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *), boolean_t);

extern int gcm_init_ctx(gcm_ctx_t *gcm_ctx, char *param, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *), boolean_t);

extern int gmac_init_ctx(gcm_ctx_t *gcm_ctx, char *param, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *), boolean_t);

extern int cfb_init_ctx(cfb_ctx_t *cfb_ctx, char *param, size_t param_len,
    size_t block_size,
    void (*copy_block)(uint8_t *, uint64_t *));

extern void calculate_ccm_mac(ccm_ctx_t *ctx, uint8_t *ccm_mac,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *));

extern void gcm_mul(uint64_t *x_in, uint64_t *y, uint64_t *res);

extern void ghash_multiblock(gcm_ctx_t *ctx, uint64_t *datap, int len);

extern void crypto_init_ptrs(crypto_data_t *out, void **iov_or_mp,
    offset_t *current_offset);
extern void crypto_get_ptrs(crypto_data_t *out, void **iov_or_mp,
    offset_t *current_offset, uint8_t **out_data_1, size_t *out_data_1_len,
    uint8_t **out_data_2, size_t amt);

extern int crypto_init_outbufs(crypto_data_t *out, uint_t *outbufnum,
    iovec_t **out_bufs, iovec_t *outvecs, int outvecs_size,
    offset_t *out_offset, boolean_t *out_bufs_allocated);

extern void *ecb_alloc_ctx(int kmflag);
extern void *cbc_alloc_ctx(int kmflag);
extern void *cfb_alloc_ctx(int kmflag);
extern void *ctr_alloc_ctx(int kmflag);
extern void *ccm_alloc_ctx(int kmflag);
extern void *gcm_alloc_ctx(int kmflag);
extern void *gmac_alloc_ctx(int kmflag);
extern void crypto_free_mode_ctx(void *ctx);
extern void gcm_set_kmflag(gcm_ctx_t *ctx, int kmflag);

extern int crypto_put_output_data(uchar_t *buf, crypto_data_t *output,
    int len);
extern int crypto_update_iov(void *ctx, crypto_data_t *input,
    crypto_data_t *output,
    int (*cipher)(void *, caddr_t, size_t, crypto_data_t *),
    void (*copy_block)(uint8_t *, uint64_t *));

#define	CRYPTO_DEFAULT_NUMBER_OF_IOVECS	20



#ifdef DEBUG_DUMP
extern void modes_printdump(char *head, void *ptr, int len);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _COMMON_CRYPTO_MODES_H */
