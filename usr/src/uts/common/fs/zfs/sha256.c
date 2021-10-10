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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */
#include <sys/zfs_context.h>
#include <sys/zio.h>
#ifdef _KERNEL
#include <sys/crypto/api.h>
#endif /* _KERNEL */
#include <sys/sha2.h>

inline static void
zio_checksum_SHA256_common(const void *buf, uint64_t size, zio_cksum_t *zcp,
    boolean_t truncate_digest)
{
	zio_cksum_t tmp;
#ifdef _KERNEL
	crypto_mechanism_t mech = { 0 };
	crypto_data_t ddata, digest;

	mech.cm_type = crypto_mech2id(SUN_CKM_SHA256);
	CRYPTO_SET_RAW_DATA(ddata, buf, size);
	CRYPTO_SET_RAW_DATA(digest, &tmp, sizeof (tmp));

	/*
	 * Try using KCF underlying providers. It may fail if FIPS140 mode
	 * verification fails.
	 */
	if (crypto_digest(&mech, &ddata, &digest, NULL) != CRYPTO_SUCCESS) {
		SHA2_CTX ctx;

		SHA2Init(SHA256, &ctx);
		SHA2Update(&ctx, buf, size);
		SHA2Final(&tmp, &ctx);
	}
#else
	SHA2_CTX ctx;

	SHA2Init(SHA256, &ctx);
	SHA2Update(&ctx, buf, size);
	SHA2Final(&tmp, &ctx);
#endif

	/*
	 * A prior implementation of this function that had a
	 * private SHA256 implementation always wrote things out in
	 * Big Endian and there wasn't a byteswap variant of it.
	 * To preseve on disk compatibility we need to force that
	 * behaviour.
	 */
	zcp->zc_word[0] = BE_64(tmp.zc_word[0]);
	zcp->zc_word[1] = BE_64(tmp.zc_word[1]);
	if (truncate_digest) {
		BF64_SET(zcp->zc_word[2], 0, 32,
		    BF64_GET(BE_64(tmp.zc_word[2]), 0, 32));
	} else {
		zcp->zc_word[2] = BE_64(tmp.zc_word[2]);
		zcp->zc_word[3] = BE_64(tmp.zc_word[3]);
	}
}

void
zio_checksum_SHA256(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	zio_checksum_SHA256_common(buf, size, zcp, FALSE);
}

/*
 * SHA256 truncated at 128 and stored in the the first two words
 * of the checksum.  The last two words store the MAC.
 */
void
zio_checksum_SHAMAC(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	zio_checksum_SHA256_common(buf, size, zcp, TRUE);
}
