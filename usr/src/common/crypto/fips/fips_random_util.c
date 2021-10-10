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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/sha1.h>
#include <sys/crypto/common.h>
#include <sys/cmn_err.h>
#ifndef _KERNEL
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <security/cryptoki.h>
#include <cryptoutil.h>
#include "softMAC.h"
#endif
#include <rng/fips_random.h>


int
fips_rng_post(void)
{
	static uint32_t XKeyValue[] = {
		0x80000000, 0x00000000,
		0x00000000, 0x00000000,
		0x00000000
	};

	static uint32_t XSeed[] = {
		0x00000000, 0x00000000,
		0x00000000, 0x00000000,
		0x00000000
	};

	static uint32_t rng_known_GENX[] = {
		0xda39a3ee, 0x5e6b4b0d,
		0x3255bfef, 0x95601890,
		0xafd80709
	};

	uint32_t GENX[SHA1_HASH_SIZE / sizeof (uint32_t)];
	uint32_t XKey[SHA1_HASH_SIZE / sizeof (uint32_t)];

	(void) memcpy(XKey, XKeyValue, SHA1_HASH_SIZE);

	/* Generate X with a known seed. */
	fips_random_inner(XKey, GENX, XSeed);

	/* Verify GENX to perform the RNG integrity check */
	if (memcmp(GENX, rng_known_GENX, SHA1_HASH_SIZE) != 0) {
		return (CKR_DEVICE_ERROR);
	} else {
		return (CKR_OK);
	}
}
