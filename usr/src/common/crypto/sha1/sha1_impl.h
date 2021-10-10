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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SHA1_IMPL_H
#define	_SHA1_IMPL_H


#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>			/* uint*_t */
#include <sys/sha1.h>			/* SHA1_CTX */
#include <fips/fips_post.h>

#ifdef _KERNEL
#define	SHA1_HASH_SIZE		20	/* SHA_1 digest length in bytes */
#define	SHA1_DIGEST_LENGTH	20	/* SHA1 digest length in bytes */
#define	SHA1_HMAC_BLOCK_SIZE	64	/* SHA1-HMAC block size */
#define	SHA1_HMAC_MIN_KEY_LEN	1	/* SHA1-HMAC min key length in bytes */
#define	SHA1_HMAC_MAX_KEY_LEN	INT_MAX /* SHA1-HMAC max key length in bytes */
#define	SHA1_HMAC_INTS_PER_BLOCK	(SHA1_HMAC_BLOCK_SIZE/sizeof (uint32_t))

/*
 * CSPI information (entry points, provider info, etc.)
 */
typedef enum sha1_mech_type {
	SHA1_MECH_INFO_TYPE,		/* SUN_CKM_SHA1 */
	SHA1_HMAC_MECH_INFO_TYPE,	/* SUN_CKM_SHA1_HMAC */
	SHA1_HMAC_GEN_MECH_INFO_TYPE	/* SUN_CKM_SHA1_HMAC_GENERAL */
} sha1_mech_type_t;

/*
 * Context for SHA1 mechanism.
 */
typedef struct sha1_ctx {
	sha1_mech_type_t	sc_mech_type;	/* type of context */
	SHA1_CTX		sc_sha1_ctx;	/* SHA1 context */
} sha1_ctx_t;

/*
 * Context for SHA1-HMAC and SHA1-HMAC-GENERAL mechanisms.
 */
typedef struct sha1_hmac_ctx {
	sha1_mech_type_t	hc_mech_type;	/* type of context */
	uint32_t		hc_digest_len;	/* digest len in bytes */
	SHA1_CTX		hc_icontext;	/* inner SHA1 context */
	SHA1_CTX		hc_ocontext;	/* outer SHA1 context */
} sha1_hmac_ctx_t;

#endif /* _KERNEL */

extern int fips_sha1_post(void);

/* SHA1 functions */
extern SHA1_CTX *fips_sha1_build_context(void);
extern int fips_sha1_hash(SHA1_CTX *sha1_context, uchar_t *in, ulong_t inlen,
    uchar_t *out);

/* SHA1 HMAC functions */
#ifndef _KERNEL
extern soft_hmac_ctx_t *fips_sha1_hmac_build_context(uint8_t *secret_key,
    unsigned int secret_key_length);
extern CK_RV fips_hmac_sha1_hash(unsigned char *hmac_computed,
    uint8_t *secret_key, unsigned int secret_key_length, uint8_t *message,
    unsigned int message_length);

#else
extern sha1_hmac_ctx_t *fips_sha1_hmac_build_context(uint8_t *secret_key,
    unsigned int secret_key_length);

extern void fips_hmac_sha1_hash(sha1_hmac_ctx_t *sha1_hmac_ctx,
    uint8_t *message, uint32_t message_len, uint8_t *hmac_computed);
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _SHA1_IMPL_H */
