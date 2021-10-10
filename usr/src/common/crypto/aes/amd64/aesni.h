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
 * Copyright (c) 2009 Intel Corporation
 * All Rights Reserved.
 */

#ifndef _AESNI_H
#define	_AESNI_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/inttypes.h>

/*
 * These are C interfaces for assembly functions.
 * Define length as uint64_t, not size_t, as it is always 64-bit,
 * including when compiled for 32-bit libraries.
 */

/* AES CBC mode functions */
void aes128_cbc_encrypt_asm(unsigned char *ks, unsigned char *p,
	unsigned char *c, uint64_t length, unsigned char *IV);
void aes192_cbc_encrypt_asm(unsigned char *ks, unsigned char *p,
	unsigned char *c, uint64_t length, unsigned char *IV);
void aes256_cbc_encrypt_asm(unsigned char *ks, unsigned char *p,
	unsigned char *c, uint64_t length, unsigned char *IV);
void aes128_cbc_decrypt_asm(unsigned char *ks, unsigned char *p,
	unsigned char *c, uint64_t length, unsigned char *IV,
	unsigned char *feedback);
void aes192_cbc_decrypt_asm(unsigned char *ks, unsigned char *p,
	unsigned char *c, uint64_t length, unsigned char *IV,
	unsigned char *feedback);
void aes256_cbc_decrypt_asm(unsigned char *ks, unsigned char *p,
	unsigned char *c, uint64_t length, unsigned char *IV,
	unsigned char *feedback);

/* AES CTR mode functions */
void aes128_ctr_asm(unsigned char *ks, unsigned char *p, unsigned char *c,
	uint64_t length, unsigned char *IV, unsigned char *feedback);
void aes192_ctr_asm(unsigned char *ks, unsigned char *p, unsigned char *c,
	uint64_t length, unsigned char *IV, unsigned char *feedback);
void aes256_ctr_asm(unsigned char *ks, unsigned char *p, unsigned char *c,
	uint64_t length, unsigned char *IV, unsigned char *feedback);

/* AES ECB mode functions */
void aes128_ecb_encrypt_asm(unsigned char *ks, unsigned char *p,
	unsigned char *c, uint64_t length);
void aes192_ecb_encrypt_asm(unsigned char *ks, unsigned char *p,
	unsigned char *c, uint64_t length);
void aes256_ecb_encrypt_asm(unsigned char *ks, unsigned char *p,
	unsigned char *c, uint64_t length);
void aes128_ecb_decrypt_asm(unsigned char *ks, unsigned char *p,
	unsigned char *c, uint64_t length);
void aes192_ecb_decrypt_asm(unsigned char *ks, unsigned char *p,
	unsigned char *c, uint64_t length);
void aes256_ecb_decrypt_asm(unsigned char *ks, unsigned char *p,
	unsigned char *c, uint64_t length);

#ifdef	__cplusplus
}
#endif

#endif	/* _AESNI_H */
