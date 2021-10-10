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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*LINTLIBRARY*/

#if defined(lint) || defined(__lint)

	
#include <sys/types.h>

/*ARGSUSED*/
void yf_aes_expand128(uint64_t *rk, const uint32_t *key)
{ return; }

/*ARGSUSED*/
void yf_aes_expand192(uint64_t *rk, const uint32_t *key)
{ return; }

/*ARGSUSED*/
void yf_aes_expand256(uint64_t *rk, const uint32_t *key)
{ return; }

/*ARGSUSED*/
void yf_aes_encrypt128(const uint64_t *rk, const uint32_t *pt, uint32_t *ct)
{ return; }

/*ARGSUSED*/
void yf_aes_encrypt192(const uint64_t *rk, const uint32_t *pt, uint32_t *ct)
{ return; }

/*ARGSUSED*/
void yf_aes_encrypt256(const uint64_t *rk, const uint32_t *pt, uint32_t *ct)
{ return; }

/*ARGSUSED*/
void yf_aes_decrypt128(const uint64_t *rk, const uint32_t *ct, uint32_t *pt)
{ return; }

/*ARGSUSED*/
void yf_aes_decrypt192(const uint64_t *rk, const uint32_t *ct, uint32_t *pt)
{ return; }

/*ARGSUSED*/
void yf_aes_decrypt256(const uint64_t *rk, const uint32_t *ct, uint32_t *pt)
{ return; }

void yf_aes128_load_keys_for_encrypt(uint64_t *ks)
{ return; }

/*ARGSUSED*/
void yf_aes192_load_keys_for_encrypt(uint64_t *ks)
{ return; }

/*ARGSUSED*/
void yf_aes256_load_keys_for_encrypt(uint64_t *ks)
{ return; }

/*ARGSUSED*/
void yf_aes128_ecb_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes192_ecb_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes256_ecb_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes128_cbc_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes192_cbc_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes256_cbc_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes128_cbc_mac(uint64_t *ks, uint64_t *asm_in, uint64_t *mac,
    size_t num_blocks)
{ return; }

/*ARGSUSED*/
void yf_aes192_cbc_mac(uint64_t *ks, uint64_t *asm_in, uint64_t *mac,
    size_t num_blocks)
{ return; }

/*ARGSUSED*/
void yf_aes256_cbc_mac(uint64_t *ks, uint64_t *asm_in, uint64_t *mac,
    size_t num_blocks)
{ return; }

/*ARGSUSED*/
void yf_aes128_ctr_crypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes192_ctr_crypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes256_ctr_crypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t *asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes128_cfb128_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes192_cfb128_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes256_cfb128_encrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

void yf_aes128_load_keys_for_decrypt(uint64_t *ks)
{ return; }

/*ARGSUSED*/
void yf_aes192_load_keys_for_decrypt(uint64_t *ks)
{ return; }

/*ARGSUSED*/
void yf_aes256_load_keys_for_decrypt(uint64_t *ks)
{ return; }

/*ARGSUSED*/
void yf_aes128_ecb_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes192_ecb_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes256_ecb_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes128_cbc_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes192_cbc_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes256_cbc_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes128_cfb128_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes192_cfb128_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

/*ARGSUSED*/
void yf_aes256_cfb128_decrypt(uint64_t *ks, uint64_t *asm_in,
    uint64_t * asm_out, size_t amount_to_encrypt, uint64_t *iv)
{ return; }

#else	/* lint || __lint */

#include<sys/asm_linkage.h>


	ENTRY(yf_aes_expand128)

!load key
	ld	[%o1], %f0
	ld	[%o1 + 0x4], %f1
	ld	[%o1 + 0x8], %f2
	ld	[%o1 + 0xc], %f3

!expand the key
	aes_kexpand1 %f0, %f2, 0x0, %f4
	aes_kexpand2 %f2, %f4, %f6
	aes_kexpand1 %f4, %f6, 0x1, %f8
	aes_kexpand2 %f6, %f8, %f10
	aes_kexpand1 %f8, %f10, 0x2, %f12
	aes_kexpand2 %f10, %f12, %f14
	aes_kexpand1 %f12, %f14, 0x3, %f16
	aes_kexpand2 %f14, %f16, %f18
	aes_kexpand1 %f16, %f18, 0x4, %f20
	aes_kexpand2 %f18, %f20, %f22
	aes_kexpand1 %f20, %f22, 0x5, %f24
	aes_kexpand2 %f22, %f24, %f26
	aes_kexpand1 %f24, %f26, 0x6, %f28
	aes_kexpand2 %f26, %f28, %f30
	aes_kexpand1 %f28, %f30, 0x7, %f32
	aes_kexpand2 %f30, %f32, %f34
	aes_kexpand1 %f32, %f34, 0x8, %f36
	aes_kexpand2 %f34, %f36, %f38
	aes_kexpand1 %f36, %f38, 0x9, %f40
	aes_kexpand2 %f38, %f40, %f42

!copy expanded key back into array
	std	%f4, [%o0]
	std	%f6, [%o0 + 0x8]
	std	%f8, [%o0 + 0x10]
	std	%f10, [%o0 + 0x18]
	std	%f12, [%o0 + 0x20]
	std	%f14, [%o0 + 0x28]
	std	%f16, [%o0 + 0x30]
	std	%f18, [%o0 + 0x38]
	std	%f20, [%o0 + 0x40]
	std	%f22, [%o0 + 0x48]
	std	%f24, [%o0 + 0x50]
	std	%f26, [%o0 + 0x58]
	std	%f28, [%o0 + 0x60]
	std	%f30, [%o0 + 0x68]
	std	%f32, [%o0 + 0x70]
	std	%f34, [%o0 + 0x78]
	std	%f36, [%o0 + 0x80]
	std	%f38, [%o0 + 0x88]
	std	%f40, [%o0 + 0x90]
	retl
	std	%f42, [%o0 + 0x98]

	SET_SIZE(yf_aes_expand128)


	ENTRY(yf_aes_encrypt128)

!load input
	ld	[%o1], %f30		!using %f30 as tmp
	ld	[%o1 + 0x4], %f31	!using %f31 as tmp
	fmovd	%f30, %f52
	ld	[%o1 + 0x8], %f30	!using %f30 as tmp
	ld	[%o1 + 0xc], %f31	!using %f31 as tmp
	fmovd	%f30, %f54

!load expanded key
	ldd	[%o0], %f0		!original key
	ldd	[%o0 + 0x8], %f2	!original key
	ldd	[%o0 + 0x10], %f4
	ldd	[%o0 + 0x18], %f6
	ldd	[%o0 + 0x20], %f8
	ldd	[%o0 + 0x28], %f10
	ldd	[%o0 + 0x30], %f12
	ldd	[%o0 + 0x38], %f14
	ldd	[%o0 + 0x40], %f16
	ldd	[%o0 + 0x48], %f18
	ldd	[%o0 + 0x50], %f20
	ldd	[%o0 + 0x58], %f22
	ldd	[%o0 + 0x60], %f24
	ldd	[%o0 + 0x68], %f26
	ldd	[%o0 + 0x70], %f28
	ldd	[%o0 + 0x78], %f30
	ldd	[%o0 + 0x80], %f32
	ldd	[%o0 + 0x88], %f34
	ldd	[%o0 + 0x90], %f36
	ldd	[%o0 + 0x98], %f38
	ldd	[%o0 + 0xa0], %f40
	ldd	[%o0 + 0xa8], %f42

!perform the cipher transformation
	fxor		%f00, %f52,      %f52	!initial ARK
	fxor		%f02, %f54,      %f54
	aes_eround01	%f04, %f52, %f54, %f56	!Round 1
	aes_eround23	%f06, %f52, %f54, %f58
	aes_eround01	%f08, %f56, %f58, %f52	!Round 2
	aes_eround23	%f10, %f56, %f58, %f54
	aes_eround01	%f12, %f52, %f54, %f56	!Round 3
	aes_eround23	%f14, %f52, %f54, %f58
	aes_eround01	%f16, %f56, %f58, %f52	!Round 4
	aes_eround23	%f18, %f56, %f58, %f54
	aes_eround01	%f20, %f52, %f54, %f56	!Round 5
	aes_eround23	%f22, %f52, %f54, %f58
	aes_eround01	%f24, %f56, %f58, %f52	!Round 6
	aes_eround23	%f26, %f56, %f58, %f54
	aes_eround01	%f28, %f52, %f54, %f56	!Round 7
	aes_eround23	%f30, %f52, %f54, %f58
	aes_eround01	%f32, %f56, %f58, %f52	!Round 8
	aes_eround23	%f34, %f56, %f58, %f54
	aes_eround01	%f36, %f52, %f54, %f56	!Round 9
	aes_eround23	%f38, %f52, %f54, %f58
	aes_eround01_l	%f40, %f56, %f58, %f60	!Round 10
	aes_eround23_l	%f42, %f56, %f58, %f62

!copy output back to array
	fmovd	%f60, %f0	!using %f0 as tmp
	st	%f0, [%o2]
	st	%f1, [%o2 + 0x4]
	fmovd	%f62, %f0	!using %f0 as tmp
	st	%f0, [%o2 + 0x8]
	retl
	st	%f1, [%o2 + 0xc]

	SET_SIZE(yf_aes_encrypt128)


	ENTRY(yf_aes_decrypt128)
	
!load input
	ld	[%o1], %f30		!using %f30 as tmp
	ld	[%o1 + 0x4], %f31	!using %f31 as tmp
	fmovd	%f30, %f52
	ld	[%o1 + 0x8], %f30	!using %f30 as tmp
	ld	[%o1 + 0xc], %f31	!using %f31 as tmp
	fmovd	%f30, %f54

!load expanded key
	ldd	[%o0], %f0		!original key
	ldd	[%o0 + 0x8], %f2	!original key
	ldd	[%o0 + 0x10], %f4
	ldd	[%o0 + 0x18], %f6
	ldd	[%o0 + 0x20], %f8
	ldd	[%o0 + 0x28], %f10
	ldd	[%o0 + 0x30], %f12
	ldd	[%o0 + 0x38], %f14
	ldd	[%o0 + 0x40], %f16
	ldd	[%o0 + 0x48], %f18
	ldd	[%o0 + 0x50], %f20
	ldd	[%o0 + 0x58], %f22
	ldd	[%o0 + 0x60], %f24
	ldd	[%o0 + 0x68], %f26
	ldd	[%o0 + 0x70], %f28
	ldd	[%o0 + 0x78], %f30
	ldd	[%o0 + 0x80], %f32
	ldd	[%o0 + 0x88], %f34
	ldd	[%o0 + 0x90], %f36
	ldd	[%o0 + 0x98], %f38
	ldd	[%o0 + 0xa0], %f40
	ldd	[%o0 + 0xa8], %f42

!perform the cipher transformation
        fxor            %f42, %f54,      %f54   !initial ARK
        fxor            %f40, %f52,      %f52
	aes_dround23	%f38, %f52, %f54, %f58	!# Round 1
	aes_dround01	%f36, %f52, %f54, %f56
	aes_dround23	%f34, %f56, %f58, %f54	!# Round 2
	aes_dround01	%f32, %f56, %f58, %f52
	aes_dround23	%f30, %f52, %f54, %f58	!# Round 3
	aes_dround01	%f28, %f52, %f54, %f56
	aes_dround23	%f26, %f56, %f58, %f54	!# Round 4
	aes_dround01	%f24, %f56, %f58, %f52
	aes_dround23	%f22, %f52, %f54, %f58	!# Round 5
	aes_dround01	%f20, %f52, %f54, %f56
	aes_dround23	%f18, %f56, %f58, %f54	!# Round 6
	aes_dround01	%f16, %f56, %f58, %f52
	aes_dround23	%f14, %f52, %f54, %f58	!# Round 7
	aes_dround01	%f12, %f52, %f54, %f56
	aes_dround23	%f10, %f56, %f58, %f54	!# Round 8
	aes_dround01	%f8 , %f56, %f58, %f52
	aes_dround23	%f6 , %f52, %f54, %f58	!# Round 9
	aes_dround01	%f4 , %f52, %f54, %f56
	aes_dround23_l	%f2 , %f56, %f58, %f54	!# Round 10
	aes_dround01_l	%f0 , %f56, %f58, %f52

!copy output back to array
	fmovd	%f52, %f0	!using %f0 as tmp
	st	%f0, [%o2]
	st	%f1, [%o2 + 0x4]
	fmovd	%f54, %f0	!using %f0 as tmp
	st	%f0, [%o2 + 0x8]
	retl
	st	%f1, [%o2 + 0xc]

	SET_SIZE(yf_aes_decrypt128)


	ENTRY(yf_aes_expand192)

!load key
	ld	[%o1], %f0
	ld	[%o1 + 0x4], %f1
	ld	[%o1 + 0x8], %f2
	ld	[%o1 + 0xc], %f3
	ld	[%o1 + 0x10], %f4
	ld	[%o1 + 0x14], %f5

!expand the key
	aes_kexpand1 %f0, %f4, 0x0, %f6
	aes_kexpand2 %f2, %f6, %f8
	aes_kexpand2 %f4, %f8, %f10

	aes_kexpand1 %f6, %f10, 0x1, %f12
	aes_kexpand2 %f8, %f12, %f14
	aes_kexpand2 %f10, %f14, %f16

	aes_kexpand1 %f12, %f16, 0x2, %f18
	aes_kexpand2 %f14, %f18, %f20
	aes_kexpand2 %f16, %f20, %f22

	aes_kexpand1 %f18, %f22, 0x3, %f24
	aes_kexpand2 %f20, %f24, %f26
	aes_kexpand2 %f22, %f26, %f28

	aes_kexpand1 %f24, %f28, 0x4, %f30
	aes_kexpand2 %f26, %f30, %f32
	aes_kexpand2 %f28, %f32, %f34

	aes_kexpand1 %f30, %f34, 0x5, %f36
	aes_kexpand2 %f32, %f36, %f38
	aes_kexpand2 %f34, %f38, %f40

	aes_kexpand1 %f36, %f40, 0x6, %f42
	aes_kexpand2 %f38, %f42, %f44
	aes_kexpand2 %f40, %f44, %f46

	aes_kexpand1 %f42, %f46, 0x7, %f48
	aes_kexpand2 %f44, %f48, %f50

!copy expanded key back into array
	std	%f6, [%o0]
	std	%f8, [%o0 + 0x8]
	std	%f10, [%o0 + 0x10]
	std	%f12, [%o0 + 0x18]
	std	%f14, [%o0 + 0x20]
	std	%f16, [%o0 + 0x28]
	std	%f18, [%o0 + 0x30]
	std	%f20, [%o0 + 0x38]
	std	%f22, [%o0 + 0x40]
	std	%f24, [%o0 + 0x48]
	std	%f26, [%o0 + 0x50]
	std	%f28, [%o0 + 0x58]
	std	%f30, [%o0 + 0x60]
	std	%f32, [%o0 + 0x68]
	std	%f34, [%o0 + 0x70]
	std	%f36, [%o0 + 0x78]
	std	%f38, [%o0 + 0x80]
	std	%f40, [%o0 + 0x88]
	std	%f42, [%o0 + 0x90]
	std	%f44, [%o0 + 0x98]
	std	%f46, [%o0 + 0xa0]
	std	%f48, [%o0 + 0xa8]
	retl
	std	%f50, [%o0 + 0xb0]

	SET_SIZE(yf_aes_expand192)


	ENTRY(yf_aes_encrypt192)

!load input
	ld	[%o1], %f30		!using %f30 as tmp
	ld	[%o1 + 0x4], %f31	!using %f31 as tmp
	fmovd	%f30, %f54
	ld	[%o1 + 0x8], %f30	!using %f30 as tmp
	ld	[%o1 + 0xc], %f31	!using %f31 as tmp
	fmovd	%f30, %f56

!load expanded key
	ldd	[%o0], %f0		!original key
	ldd	[%o0 + 0x8], %f2	!original key
	ldd	[%o0 + 0x10], %f4	!original key
	ldd	[%o0 + 0x18], %f6
	ldd	[%o0 + 0x20], %f8
	ldd	[%o0 + 0x28], %f10
	ldd	[%o0 + 0x30], %f12
	ldd	[%o0 + 0x38], %f14
	ldd	[%o0 + 0x40], %f16
	ldd	[%o0 + 0x48], %f18
	ldd	[%o0 + 0x50], %f20
	ldd	[%o0 + 0x58], %f22
	ldd	[%o0 + 0x60], %f24
	ldd	[%o0 + 0x68], %f26
	ldd	[%o0 + 0x70], %f28
	ldd	[%o0 + 0x78], %f30
	ldd	[%o0 + 0x80], %f32
	ldd	[%o0 + 0x88], %f34
	ldd	[%o0 + 0x90], %f36
	ldd	[%o0 + 0x98], %f38
	ldd	[%o0 + 0xa0], %f40
	ldd	[%o0 + 0xa8], %f42
	ldd	[%o0 + 0xb0], %f44
	ldd	[%o0 + 0xb8], %f46
	ldd	[%o0 + 0xc0], %f48
	ldd	[%o0 + 0xc8], %f50

!perform the cipher transformation
!%f58 and %f60 are scratch registers
	fxor		%f00, %f54,      %f54	!initial ARK
	fxor		%f02, %f56,      %f56
	aes_eround01	%f04, %f54, %f56, %f58	!Round 1
	aes_eround23	%f06, %f54, %f56, %f60
	aes_eround01	%f08, %f58, %f60, %f54	!Round 2
	aes_eround23	%f10, %f58, %f60, %f56
	aes_eround01	%f12, %f54, %f56, %f58	!Round 3
	aes_eround23	%f14, %f54, %f56, %f60
	aes_eround01	%f16, %f58, %f60, %f54	!Round 4
	aes_eround23	%f18, %f58, %f60, %f56
	aes_eround01	%f20, %f54, %f56, %f58	!Round 5
	aes_eround23	%f22, %f54, %f56, %f60
	aes_eround01	%f24, %f58, %f60, %f54	!Round 6
	aes_eround23	%f26, %f58, %f60, %f56
	aes_eround01	%f28, %f54, %f56, %f58	!Round 7
	aes_eround23	%f30, %f54, %f56, %f60
	aes_eround01	%f32, %f58, %f60, %f54	!Round 8
	aes_eround23	%f34, %f58, %f60, %f56
	aes_eround01	%f36, %f54, %f56, %f58	!Round 9
	aes_eround23	%f38, %f54, %f56, %f60
	aes_eround01	%f40, %f58, %f60, %f54	!Round 10
	aes_eround23	%f42, %f58, %f60, %f56
	aes_eround01	%f44, %f54, %f56, %f58	!Round 11
	aes_eround23	%f46, %f54, %f56, %f60
	aes_eround01_l	%f48, %f58, %f60, %f54	!Round 12
	aes_eround23_l	%f50, %f58, %f60, %f56

!copy output back to array
	fmovd	%f54, %f0	!using %f0 as tmp
	st	%f0, [%o2]
	st	%f1, [%o2 + 0x4]
	fmovd	%f56, %f0	!using %f0 as tmp
	st	%f0, [%o2 + 0x8]
	retl
	st	%f1, [%o2 + 0xc]

	SET_SIZE(yf_aes_encrypt192)


	ENTRY(yf_aes_decrypt192)

!load input
	ld	[%o1], %f30		!using %f30 as tmp
	ld	[%o1 + 0x4], %f31	!using %f31 as tmp
	fmovd	%f30, %f52
	ld	[%o1 + 0x8], %f30	!using %f30 as tmp
	ld	[%o1 + 0xc], %f31	!using %f31 as tmp
	fmovd	%f30, %f54

!load expanded key
	ldd	[%o0], %f0		!original key
	ldd	[%o0 + 0x8], %f2	!original key
	ldd	[%o0 + 0x10], %f4	!original key
	ldd	[%o0 + 0x18], %f6
	ldd	[%o0 + 0x20], %f8
	ldd	[%o0 + 0x28], %f10
	ldd	[%o0 + 0x30], %f12
	ldd	[%o0 + 0x38], %f14
	ldd	[%o0 + 0x40], %f16
	ldd	[%o0 + 0x48], %f18
	ldd	[%o0 + 0x50], %f20
	ldd	[%o0 + 0x58], %f22
	ldd	[%o0 + 0x60], %f24
	ldd	[%o0 + 0x68], %f26
	ldd	[%o0 + 0x70], %f28
	ldd	[%o0 + 0x78], %f30
	ldd	[%o0 + 0x80], %f32
	ldd	[%o0 + 0x88], %f34
	ldd	[%o0 + 0x90], %f36
	ldd	[%o0 + 0x98], %f38
	ldd	[%o0 + 0xa0], %f40
	ldd	[%o0 + 0xa8], %f42
	ldd	[%o0 + 0xb0], %f44
	ldd	[%o0 + 0xb8], %f46
	ldd	[%o0 + 0xc0], %f48
	ldd	[%o0 + 0xc8], %f50

!perform the cipher transformation
        fxor            %f50, %f54,      %f54   !initial ARK
        fxor            %f48, %f52,      %f52
	aes_dround23	%f46, %f52, %f54, %f58	!# Round 1
	aes_dround01	%f44, %f52, %f54, %f56
	aes_dround23	%f42, %f56, %f58, %f54	!# Round 2
	aes_dround01	%f40, %f56, %f58, %f52
	aes_dround23	%f38, %f52, %f54, %f58	!# Round 3
	aes_dround01	%f36, %f52, %f54, %f56
	aes_dround23	%f34, %f56, %f58, %f54	!# Round 4
	aes_dround01	%f32, %f56, %f58, %f52
	aes_dround23	%f30, %f52, %f54, %f58	!# Round 5
	aes_dround01	%f28, %f52, %f54, %f56
	aes_dround23	%f26, %f56, %f58, %f54	!# Round 6
	aes_dround01	%f24, %f56, %f58, %f52
	aes_dround23	%f22, %f52, %f54, %f58	!# Round 7
	aes_dround01	%f20, %f52, %f54, %f56
	aes_dround23	%f18, %f56, %f58, %f54	!# Round 8
	aes_dround01	%f16, %f56, %f58, %f52
	aes_dround23	%f14, %f52, %f54, %f58	!# Round 9
	aes_dround01	%f12, %f52, %f54, %f56
	aes_dround23   	%f10, %f56, %f58, %f54	!# Round 10
	aes_dround01   	%f8, %f56, %f58, %f52
	aes_dround23   	%f6, %f52, %f54, %f58	!# Round 11
	aes_dround01   	%f4, %f52, %f54, %f56
	aes_dround23_l	%f2, %f56, %f58, %f54	!# Round 12
	aes_dround01_l	%f0, %f56, %f58, %f52

!copy output back to array
	fmovd	%f52, %f0	!using %f0 as tmp
	st	%f0, [%o2]
	st	%f1, [%o2 + 0x4]
	fmovd	%f54, %f0	!using %f0 as tmp
	st	%f0, [%o2 + 0x8]
	retl
	st	%f1, [%o2 + 0xc]

	SET_SIZE(yf_aes_decrypt192)


	ENTRY(yf_aes_expand256)

!load key
	ld	[%o1], %f0
	ld	[%o1 + 0x4], %f1
	ld	[%o1 + 0x8], %f2
	ld	[%o1 + 0xc], %f3
	ld	[%o1 + 0x10], %f4
	ld	[%o1 + 0x14], %f5
	ld	[%o1 + 0x18], %f6
	ld	[%o1 + 0x1c], %f7

!expand the key
	aes_kexpand1 %f0, %f6, 0x0, %f8
	aes_kexpand2 %f2, %f8, %f10
	aes_kexpand0 %f4, %f10, %f12
	aes_kexpand2 %f6, %f12, %f14

	aes_kexpand1 %f8, %f14, 0x1, %f16
	aes_kexpand2 %f10, %f16, %f18
	aes_kexpand0 %f12, %f18, %f20
	aes_kexpand2 %f14, %f20, %f22

	aes_kexpand1 %f16, %f22, 0x2, %f24
	aes_kexpand2 %f18, %f24, %f26
	aes_kexpand0 %f20, %f26, %f28
	aes_kexpand2 %f22, %f28, %f30

	aes_kexpand1 %f24, %f30, 0x3, %f32
	aes_kexpand2 %f26, %f32, %f34
	aes_kexpand0 %f28, %f34, %f36
	aes_kexpand2 %f30, %f36, %f38

	aes_kexpand1 %f32, %f38, 0x4, %f40
	aes_kexpand2 %f34, %f40, %f42
	aes_kexpand0 %f36, %f42, %f44
	aes_kexpand2 %f38, %f44, %f46

	aes_kexpand1 %f40, %f46, 0x5, %f48
	aes_kexpand2 %f42, %f48, %f50
	aes_kexpand0 %f44, %f50, %f52
	aes_kexpand2 %f46, %f52, %f54

	aes_kexpand1 %f48, %f54, 0x6, %f56
	aes_kexpand2 %f50, %f56, %f58

!copy expanded key back into array
	std	%f8, [%o0]
	std	%f10, [%o0 + 0x8]
	std	%f12, [%o0 + 0x10]
	std	%f14, [%o0 + 0x18]
	std	%f16, [%o0 + 0x20]
	std	%f18, [%o0 + 0x28]
	std	%f20, [%o0 + 0x30]
	std	%f22, [%o0 + 0x38]
	std	%f24, [%o0 + 0x40]
	std	%f26, [%o0 + 0x48]
	std	%f28, [%o0 + 0x50]
	std	%f30, [%o0 + 0x58]
	std	%f32, [%o0 + 0x60]
	std	%f34, [%o0 + 0x68]
	std	%f36, [%o0 + 0x70]
	std	%f38, [%o0 + 0x78]
	std	%f40, [%o0 + 0x80]
	std	%f42, [%o0 + 0x88]
	std	%f44, [%o0 + 0x90]
	std	%f46, [%o0 + 0x98]
	std	%f48, [%o0 + 0xa0]
	std	%f50, [%o0 + 0xa8]
	std	%f52, [%o0 + 0xb0]
	std	%f54, [%o0 + 0xb8]
	std	%f56, [%o0 + 0xc0]
	retl
	std	%f58, [%o0 + 0xc8]

	SET_SIZE(yf_aes_expand256)


	ENTRY(yf_aes_encrypt256)

!load input
	ld	[%o1], %f30		!using %f30 as tmp
	ld	[%o1 + 0x4], %f31	!using %f31 as tmp
	fmovd	%f30, %f54
	ld	[%o1 + 0x8], %f30	!using %f30 as tmp
	ld	[%o1 + 0xc], %f31	!using %f31 as tmp
	fmovd	%f30, %f56

!load expanded key
	ldd	[%o0], %f0		!original key
	ldd	[%o0 + 0x8], %f2	!original key
	ldd	[%o0 + 0x10], %f4	!original key
	ldd	[%o0 + 0x18], %f6	!original key
	ldd	[%o0 + 0x20], %f8
	ldd	[%o0 + 0x28], %f10
	ldd	[%o0 + 0x30], %f12
	ldd	[%o0 + 0x38], %f14
	ldd	[%o0 + 0x40], %f16
	ldd	[%o0 + 0x48], %f18
	ldd	[%o0 + 0x50], %f20
	ldd	[%o0 + 0x58], %f22
	ldd	[%o0 + 0x60], %f24
	ldd	[%o0 + 0x68], %f26
	ldd	[%o0 + 0x70], %f28
	ldd	[%o0 + 0x78], %f30
	ldd	[%o0 + 0x80], %f32
	ldd	[%o0 + 0x88], %f34
	ldd	[%o0 + 0x90], %f36
	ldd	[%o0 + 0x98], %f38
	ldd	[%o0 + 0xa0], %f40
	ldd	[%o0 + 0xa8], %f42
	ldd	[%o0 + 0xb0], %f44
	ldd	[%o0 + 0xb8], %f46
	ldd	[%o0 + 0xc0], %f48
	ldd	[%o0 + 0xc8], %f50
	ldd	[%o0 + 0xd0], %f52

!perform the cipher transformation
!%f58 and %f60 are scratch registers
	fxor		%f00, %f54,      %f54	!initial ARK
	fxor		%f02, %f56,      %f56
	aes_eround01	%f04, %f54, %f56, %f58	!Round 1
	aes_eround23	%f06, %f54, %f56, %f60
	aes_eround01	%f08, %f58, %f60, %f54	!Round 2
	aes_eround23	%f10, %f58, %f60, %f56
	aes_eround01	%f12, %f54, %f56, %f58	!Round 3
	aes_eround23	%f14, %f54, %f56, %f60
	aes_eround01	%f16, %f58, %f60, %f54	!Round 4
	aes_eround23	%f18, %f58, %f60, %f56
	aes_eround01	%f20, %f54, %f56, %f58	!Round 5
	aes_eround23	%f22, %f54, %f56, %f60
	aes_eround01	%f24, %f58, %f60, %f54	!Round 6
	aes_eround23	%f26, %f58, %f60, %f56
	aes_eround01	%f28, %f54, %f56, %f58	!Round 7
	aes_eround23	%f30, %f54, %f56, %f60
	aes_eround01	%f32, %f58, %f60, %f54	!Round 8
	aes_eround23	%f34, %f58, %f60, %f56
	aes_eround01	%f36, %f54, %f56, %f58	!Round 9
	aes_eround23	%f38, %f54, %f56, %f60
	aes_eround01	%f40, %f58, %f60, %f54	!Round 10
	aes_eround23	%f42, %f58, %f60, %f56
	aes_eround01	%f44, %f54, %f56, %f58	!Round 11
	aes_eround23	%f46, %f54, %f56, %f60
	aes_eround01	%f48, %f58, %f60, %f54	!Round 12
	aes_eround23	%f50, %f58, %f60, %f56

	ldd		[%o0 + 0xd8], %f0	!using %f0 as tmp
	ldd		[%o0 + 0xe0], %f2	!using %f2 as tmp
	ldd		[%o0 + 0xe8], %f4	!using %f4 as tmp
	aes_eround01	%f52, %f54, %f56, %f58	!Round 13
	aes_eround23	%f0, %f54, %f56, %f60
	aes_eround01_l	%f2, %f58, %f60, %f54	!Round 14
	aes_eround23_l	%f4, %f58, %f60, %f56

!copy output back to array
	fmovd	%f54, %f0			!using %f0 as tmp
	st	%f0, [%o2]
	st	%f1, [%o2 + 0x4]
	fmovd	%f56, %f0			!using %f0 as tmp
	st	%f0, [%o2 + 0x8]
	retl
	st	%f1, [%o2 + 0xc]

	SET_SIZE(yf_aes_encrypt256)


	ENTRY(yf_aes_decrypt256)

!load input
	ld	[%o1], %f30		!using %f30 as tmp
	ld	[%o1 + 0x4], %f31	!using %f31 as tmp
	fmovd	%f30, %f52
	ld	[%o1 + 0x8], %f30	!using %f30 as tmp
	ld	[%o1 + 0xc], %f31	!using %f31 as tmp
	fmovd	%f30, %f54

!load expanded key
	ldd	[%o0 + 0x20], %f8
	ldd	[%o0 + 0x28], %f10
	ldd	[%o0 + 0x30], %f12
	ldd	[%o0 + 0x38], %f14
	ldd	[%o0 + 0x40], %f16
	ldd	[%o0 + 0x48], %f18
	ldd	[%o0 + 0x50], %f20
	ldd	[%o0 + 0x58], %f22
	ldd	[%o0 + 0x60], %f24
	ldd	[%o0 + 0x68], %f26
	ldd	[%o0 + 0x70], %f28
	ldd	[%o0 + 0x78], %f30
	ldd	[%o0 + 0x80], %f32
	ldd	[%o0 + 0x88], %f34
	ldd	[%o0 + 0x90], %f36
	ldd	[%o0 + 0x98], %f38
	ldd	[%o0 + 0xa0], %f40
	ldd	[%o0 + 0xa8], %f42
	ldd	[%o0 + 0xb0], %f44
	ldd	[%o0 + 0xb8], %f46
	ldd	[%o0 + 0xc0], %f48
	ldd	[%o0 + 0xc8], %f50

!perform the cipher transformation
	ldd	[%o0 + 0xe8], %f0	!using %f0 as tmp
	ldd	[%o0 + 0xe0], %f2	!using %f2 as tmp
	ldd	[%o0 + 0xd8], %f4	!using %f4 as tmp
	ldd	[%o0 + 0xd0], %f6	!using %f6 as tmp
	fxor	%f0, %f54, %f54		!initial ARK
	fxor	%f2, %f52, %f52
	aes_dround23	%f4, %f52, %f54, %f58		!# Round 1
	aes_dround01	%f6, %f52, %f54, %f56
	aes_dround23	%f50, %f56, %f58, %f54		!# Round 2
	aes_dround01	%f48, %f56, %f58, %f52
	aes_dround23	%f46, %f52, %f54, %f58		!# Round 3
	aes_dround01	%f44, %f52, %f54, %f56
	aes_dround23	%f42, %f56, %f58, %f54		!# Round 4
	aes_dround01	%f40, %f56, %f58, %f52
	aes_dround23	%f38, %f52, %f54, %f58		!# Round 5
	aes_dround01	%f36, %f52, %f54, %f56
	aes_dround23	%f34, %f56, %f58, %f54		!# Round 6
	aes_dround01	%f32, %f56, %f58, %f52
	aes_dround23	%f30, %f52, %f54, %f58		!# Round 7
	aes_dround01	%f28, %f52, %f54, %f56
	aes_dround23	%f26, %f56, %f58, %f54		!# Round 8
	aes_dround01	%f24, %f56, %f58, %f52
	aes_dround23	%f22, %f52, %f54, %f58		!# Round 9
	aes_dround01	%f20, %f52, %f54, %f56
	aes_dround23	%f18, %f56, %f58, %f54		!# Round 10
	aes_dround01	%f16, %f56, %f58, %f52
	aes_dround23	%f14, %f52, %f54, %f58		!# Round 11
	aes_dround01	%f12, %f52, %f54, %f56
	aes_dround23   	%f10, %f56, %f58, %f54		!# Round 12
	aes_dround01   	%f8, %f56, %f58, %f52

	ldd	[%o0 + 0x18], %f6	!original key
	ldd	[%o0 + 0x10], %f4	!original key
	ldd	[%o0 + 0x8], %f2	!original key
	ldd	[%o0], %f0		!original key
	aes_dround23	%f6, %f52, %f54, %f58		!# Round 13
	aes_dround01	%f4, %f52, %f54, %f56
	aes_dround23_l	%f2, %f56, %f58, %f54		!# Round 14
	aes_dround01_l	%f0, %f56, %f58, %f52

!copy output back to array
	fmovd	%f52, %f0	!using %f0 as tmp
	st	%f0, [%o2]
	st	%f1, [%o2 + 0x4]
	fmovd	%f54, %f0	!using %f0 as tmp
	st	%f0, [%o2 + 0x8]
	retl
	st	%f1, [%o2 + 0xc]

	SET_SIZE(yf_aes_decrypt256)




#define	FIRST_TWO_EROUNDS \
	aes_eround01	%f0, %f60, %f62, %f56 ; \
	aes_eround23	%f2, %f60, %f62, %f58 ; \
	aes_eround01	%f4, %f56, %f58, %f60 ; \
	aes_eround23	%f6, %f56, %f58, %f62

#define	MID_TWO_EROUNDS \
	aes_eround01	%f8, %f60, %f62, %f56 ; \
	aes_eround23	%f10, %f60, %f62, %f58 ; \
	aes_eround01	%f12, %f56, %f58, %f60 ; \
	aes_eround23	%f14, %f56, %f58, %f62

#define	MID_TWO_EROUNDS_2 \
	aes_eround01	%f8, %f0, %f2, %f6 ; \
	aes_eround23	%f10, %f0, %f2, %f4 ; \
	aes_eround01	%f8, %f60, %f62, %f56 ; \
	aes_eround23	%f10, %f60, %f62, %f58 ; \
	aes_eround01	%f12, %f6, %f4, %f0 ; \
	aes_eround23	%f14, %f6, %f4, %f2 ; \
	aes_eround01	%f12, %f56, %f58, %f60 ; \
	aes_eround23	%f14, %f56, %f58, %f62

#define	TEN_EROUNDS \
	aes_eround01	%f16, %f60, %f62, %f56 ; \
	aes_eround23	%f18, %f60, %f62, %f58 ; \
	aes_eround01	%f20, %f56, %f58, %f60 ; \
	aes_eround23	%f22, %f56, %f58, %f62 ; \
	aes_eround01	%f24, %f60, %f62, %f56 ; \
	aes_eround23	%f26, %f60, %f62, %f58 ; \
	aes_eround01	%f28, %f56, %f58, %f60 ; \
	aes_eround23	%f30, %f56, %f58, %f62 ; \
	aes_eround01	%f32, %f60, %f62, %f56 ; \
	aes_eround23	%f34, %f60, %f62, %f58 ; \
	aes_eround01	%f36, %f56, %f58, %f60 ; \
	aes_eround23	%f38, %f56, %f58, %f62 ; \
	aes_eround01	%f40, %f60, %f62, %f56 ; \
	aes_eround23	%f42, %f60, %f62, %f58 ; \
	aes_eround01	%f44, %f56, %f58, %f60 ; \
	aes_eround23	%f46, %f56, %f58, %f62 ; \
	aes_eround01	%f48, %f60, %f62, %f56 ; \
	aes_eround23	%f50, %f60, %f62, %f58 ; \
	aes_eround01_l	%f52, %f56, %f58, %f60 ; \
	aes_eround23_l	%f54, %f56, %f58, %f62

#define	TEN_EROUNDS_2 \
	aes_eround01	%f16, %f0, %f2, %f6 ; \
	aes_eround23	%f18, %f0, %f2, %f4 ; \
	aes_eround01	%f16, %f60, %f62, %f56 ; \
	aes_eround23	%f18, %f60, %f62, %f58 ; \
	aes_eround01	%f20, %f6, %f4, %f0 ; \
	aes_eround23	%f22, %f6, %f4, %f2 ; \
	aes_eround01	%f20, %f56, %f58, %f60 ; \
	aes_eround23	%f22, %f56, %f58, %f62 ; \
	aes_eround01	%f24, %f0, %f2, %f6 ; \
	aes_eround23	%f26, %f0, %f2, %f4 ; \
	aes_eround01	%f24, %f60, %f62, %f56 ; \
	aes_eround23	%f26, %f60, %f62, %f58 ; \
	aes_eround01	%f28, %f6, %f4, %f0 ; \
	aes_eround23	%f30, %f6, %f4, %f2 ; \
	aes_eround01	%f28, %f56, %f58, %f60 ; \
	aes_eround23	%f30, %f56, %f58, %f62 ; \
	aes_eround01	%f32, %f0, %f2, %f6 ; \
	aes_eround23	%f34, %f0, %f2, %f4 ; \
	aes_eround01	%f32, %f60, %f62, %f56 ; \
	aes_eround23	%f34, %f60, %f62, %f58 ; \
	aes_eround01	%f36, %f6, %f4, %f0 ; \
	aes_eround23	%f38, %f6, %f4, %f2 ; \
	aes_eround01	%f36, %f56, %f58, %f60 ; \
	aes_eround23	%f38, %f56, %f58, %f62 ; \
	aes_eround01	%f40, %f0, %f2, %f6 ; \
	aes_eround23	%f42, %f0, %f2, %f4 ; \
	aes_eround01	%f40, %f60, %f62, %f56 ; \
	aes_eround23	%f42, %f60, %f62, %f58 ; \
	aes_eround01	%f44, %f6, %f4, %f0 ; \
	aes_eround23	%f46, %f6, %f4, %f2 ; \
	aes_eround01	%f44, %f56, %f58, %f60 ; \
	aes_eround23	%f46, %f56, %f58, %f62 ; \
	aes_eround01	%f48, %f0, %f2, %f6 ; \
	aes_eround23	%f50, %f0, %f2, %f4 ; \
	aes_eround01	%f48, %f60, %f62, %f56 ; \
	aes_eround23	%f50, %f60, %f62, %f58 ; \
	aes_eround01_l	%f52, %f6, %f4, %f0 ; \
	aes_eround23_l	%f54, %f6, %f4, %f2 ; \
	aes_eround01_l	%f52, %f56, %f58, %f60 ; \
	aes_eround23_l	%f54, %f56, %f58, %f62

#define	TWELVE_EROUNDS \
	MID_TWO_EROUNDS	; \
	TEN_EROUNDS

#define	TWELVE_EROUNDS_2 \
	MID_TWO_EROUNDS_2	; \
	TEN_EROUNDS_2

#define	 FOURTEEN_EROUNDS \
	FIRST_TWO_EROUNDS ; \
	TWELVE_EROUNDS

#define	FOURTEEN_EROUNDS_2 \
	aes_eround01	%f0, %f20, %f22, %f24 ; \
	aes_eround23	%f2, %f20, %f22, %f22 ; \
	ldd	[%o0 + 0x60], %f20 ; \
	aes_eround01	%f0, %f60, %f62, %f56 ; \
	aes_eround23	%f2, %f60, %f62, %f58 ; \
	aes_eround01	%f4, %f24, %f22, %f0 ; \
	aes_eround23	%f6, %f24, %f22, %f2 ; \
	ldd	[%o0 + 0x68], %f22 ; \
	aes_eround01	%f4, %f56, %f58, %f60 ; \
	ldd	[%o0 + 0x70], %f24 ; \
	aes_eround23	%f6, %f56, %f58, %f62 ; \
	aes_eround01	%f8, %f0, %f2, %f6 ; \
	aes_eround23	%f10, %f0, %f2, %f4 ; \
	aes_eround01	%f8, %f60, %f62, %f56 ; \
	aes_eround23	%f10, %f60, %f62, %f58 ; \
	aes_eround01	%f12, %f6, %f4, %f0 ; \
	aes_eround23	%f14, %f6, %f4, %f2 ; \
	aes_eround01	%f12, %f56, %f58, %f60 ; \
	aes_eround23	%f14, %f56, %f58, %f62 ; \
	aes_eround01	%f16, %f0, %f2, %f6 ; \
	aes_eround23	%f18, %f0, %f2, %f4 ; \
	aes_eround01	%f16, %f60, %f62, %f56 ; \
	aes_eround23	%f18, %f60, %f62, %f58 ; \
	aes_eround01	%f20, %f6, %f4, %f0 ; \
	aes_eround23	%f22, %f6, %f4, %f2 ; \
	aes_eround01	%f20, %f56, %f58, %f60 ; \
	aes_eround23	%f22, %f56, %f58, %f62 ; \
	aes_eround01	%f24, %f0, %f2, %f6 ; \
	aes_eround23	%f26, %f0, %f2, %f4 ; \
	aes_eround01	%f24, %f60, %f62, %f56 ; \
	aes_eround23	%f26, %f60, %f62, %f58 ; \
	aes_eround01	%f28, %f6, %f4, %f0 ; \
	aes_eround23	%f30, %f6, %f4, %f2 ; \
	aes_eround01	%f28, %f56, %f58, %f60 ; \
	aes_eround23	%f30, %f56, %f58, %f62 ; \
	aes_eround01	%f32, %f0, %f2, %f6 ; \
	aes_eround23	%f34, %f0, %f2, %f4 ; \
	aes_eround01	%f32, %f60, %f62, %f56 ; \
	aes_eround23	%f34, %f60, %f62, %f58 ; \
	aes_eround01	%f36, %f6, %f4, %f0 ; \
	aes_eround23	%f38, %f6, %f4, %f2 ; \
	aes_eround01	%f36, %f56, %f58, %f60 ; \
	aes_eround23	%f38, %f56, %f58, %f62 ; \
	aes_eround01	%f40, %f0, %f2, %f6 ; \
	aes_eround23	%f42, %f0, %f2, %f4 ; \
	aes_eround01	%f40, %f60, %f62, %f56 ; \
	aes_eround23	%f42, %f60, %f62, %f58 ; \
	aes_eround01	%f44, %f6, %f4, %f0 ; \
	aes_eround23	%f46, %f6, %f4, %f2 ; \
	aes_eround01	%f44, %f56, %f58, %f60 ; \
	aes_eround23	%f46, %f56, %f58, %f62 ; \
	aes_eround01	%f48, %f0, %f2, %f6 ; \
	aes_eround23	%f50, %f0, %f2, %f4 ; \
	ldd	[%o0 + 0x10], %f0 ; \
	aes_eround01	%f48, %f60, %f62, %f56 ; \
	ldd	[%o0 + 0x18], %f2 ; \
	aes_eround23	%f50, %f60, %f62, %f58 ; \
	aes_eround01_l	%f52, %f6, %f4, %f20 ; \
	aes_eround23_l	%f54, %f6, %f4, %f22 ; \
	ldd	[%o0 + 0x20], %f4 ; \
	aes_eround01_l	%f52, %f56, %f58, %f60 ; \
	ldd	[%o0 + 0x28], %f6 ; \
	aes_eround23_l	%f54, %f56, %f58, %f62

#define	FIRST_TWO_DROUNDS \
	aes_dround01	%f0, %f60, %f62, %f56 ; \
	aes_dround23	%f2, %f60, %f62, %f58 ; \
	aes_dround01	%f4, %f56, %f58, %f60 ; \
	aes_dround23	%f6, %f56, %f58, %f62

#define	MID_TWO_DROUNDS \
	aes_dround01	%f8, %f60, %f62, %f56 ; \
	aes_dround23	%f10, %f60, %f62, %f58 ; \
	aes_dround01	%f12, %f56, %f58, %f60 ; \
	aes_dround23	%f14, %f56, %f58, %f62

#define	MID_TWO_DROUNDS_2 \
	aes_dround01	%f8, %f0, %f2, %f6 ; \
	aes_dround23	%f10, %f0, %f2, %f4 ; \
	aes_dround01	%f8, %f60, %f62, %f56 ; \
	aes_dround23	%f10, %f60, %f62, %f58 ; \
	aes_dround01	%f12, %f6, %f4, %f0 ; \
	aes_dround23	%f14, %f6, %f4, %f2 ; \
	aes_dround01	%f12, %f56, %f58, %f60 ; \
	aes_dround23	%f14, %f56, %f58, %f62

#define	TEN_DROUNDS \
	aes_dround01	%f16, %f60, %f62, %f56 ; \
	aes_dround23	%f18, %f60, %f62, %f58 ; \
	aes_dround01	%f20, %f56, %f58, %f60 ; \
	aes_dround23	%f22, %f56, %f58, %f62 ; \
	aes_dround01	%f24, %f60, %f62, %f56 ; \
	aes_dround23	%f26, %f60, %f62, %f58 ; \
	aes_dround01	%f28, %f56, %f58, %f60 ; \
	aes_dround23	%f30, %f56, %f58, %f62 ; \
	aes_dround01	%f32, %f60, %f62, %f56 ; \
	aes_dround23	%f34, %f60, %f62, %f58 ; \
	aes_dround01	%f36, %f56, %f58, %f60 ; \
	aes_dround23	%f38, %f56, %f58, %f62 ; \
	aes_dround01	%f40, %f60, %f62, %f56 ; \
	aes_dround23	%f42, %f60, %f62, %f58 ; \
	aes_dround01	%f44, %f56, %f58, %f60 ; \
	aes_dround23	%f46, %f56, %f58, %f62 ; \
	aes_dround01	%f48, %f60, %f62, %f56 ; \
	aes_dround23	%f50, %f60, %f62, %f58 ; \
	aes_dround01_l	%f52, %f56, %f58, %f60 ; \
	aes_dround23_l	%f54, %f56, %f58, %f62

#define	TEN_DROUNDS_2 \
	aes_dround01	%f16, %f0, %f2, %f6 ; \
	aes_dround23	%f18, %f0, %f2, %f4 ; \
	aes_dround01	%f16, %f60, %f62, %f56 ; \
	aes_dround23	%f18, %f60, %f62, %f58 ; \
	aes_dround01	%f20, %f6, %f4, %f0 ; \
	aes_dround23	%f22, %f6, %f4, %f2 ; \
	aes_dround01	%f20, %f56, %f58, %f60 ; \
	aes_dround23	%f22, %f56, %f58, %f62 ; \
	aes_dround01	%f24, %f0, %f2, %f6 ; \
	aes_dround23	%f26, %f0, %f2, %f4 ; \
	aes_dround01	%f24, %f60, %f62, %f56 ; \
	aes_dround23	%f26, %f60, %f62, %f58 ; \
	aes_dround01	%f28, %f6, %f4, %f0 ; \
	aes_dround23	%f30, %f6, %f4, %f2 ; \
	aes_dround01	%f28, %f56, %f58, %f60 ; \
	aes_dround23	%f30, %f56, %f58, %f62 ; \
	aes_dround01	%f32, %f0, %f2, %f6 ; \
	aes_dround23	%f34, %f0, %f2, %f4 ; \
	aes_dround01	%f32, %f60, %f62, %f56 ; \
	aes_dround23	%f34, %f60, %f62, %f58 ; \
	aes_dround01	%f36, %f6, %f4, %f0 ; \
	aes_dround23	%f38, %f6, %f4, %f2 ; \
	aes_dround01	%f36, %f56, %f58, %f60 ; \
	aes_dround23	%f38, %f56, %f58, %f62 ; \
	aes_dround01	%f40, %f0, %f2, %f6 ; \
	aes_dround23	%f42, %f0, %f2, %f4 ; \
	aes_dround01	%f40, %f60, %f62, %f56 ; \
	aes_dround23	%f42, %f60, %f62, %f58 ; \
	aes_dround01	%f44, %f6, %f4, %f0 ; \
	aes_dround23	%f46, %f6, %f4, %f2 ; \
	aes_dround01	%f44, %f56, %f58, %f60 ; \
	aes_dround23	%f46, %f56, %f58, %f62 ; \
	aes_dround01	%f48, %f0, %f2, %f6 ; \
	aes_dround23	%f50, %f0, %f2, %f4 ; \
	aes_dround01	%f48, %f60, %f62, %f56 ; \
	aes_dround23	%f50, %f60, %f62, %f58 ; \
	aes_dround01_l	%f52, %f6, %f4, %f0 ; \
	aes_dround23_l	%f54, %f6, %f4, %f2 ; \
	aes_dround01_l	%f52, %f56, %f58, %f60 ; \
	aes_dround23_l	%f54, %f56, %f58, %f62

#define	TWELVE_DROUNDS \
	MID_TWO_DROUNDS	; \
	TEN_DROUNDS

#define	TWELVE_DROUNDS_2 \
	MID_TWO_DROUNDS_2	; \
	TEN_DROUNDS_2

#define	FOURTEEN_DROUNDS \
	FIRST_TWO_DROUNDS ; \
	TWELVE_DROUNDS	

#define	FOURTEEN_DROUNDS_2 \
	aes_dround01	%f0, %f20, %f22, %f24 ; \
	aes_dround23	%f2, %f20, %f22, %f22 ; \
	ldd	[%o0 + 0x80], %f20 ; \
	aes_dround01	%f0, %f60, %f62, %f56 ; \
	aes_dround23	%f2, %f60, %f62, %f58 ; \
	aes_dround01	%f4, %f24, %f22, %f0 ; \
	aes_dround23	%f6, %f24, %f22, %f2 ; \
	ldd	[%o0 + 0x88], %f22 ; \
	aes_dround01	%f4, %f56, %f58, %f60 ; \
	ldd	[%o0 + 0x70], %f24 ; \
	aes_dround23	%f6, %f56, %f58, %f62 ; \
	aes_dround01	%f8, %f0, %f2, %f6 ; \
	aes_dround23	%f10, %f0, %f2, %f4 ; \
	aes_dround01	%f8, %f60, %f62, %f56 ; \
	aes_dround23	%f10, %f60, %f62, %f58 ; \
	aes_dround01	%f12, %f6, %f4, %f0 ; \
	aes_dround23	%f14, %f6, %f4, %f2 ; \
	aes_dround01	%f12, %f56, %f58, %f60 ; \
	aes_dround23	%f14, %f56, %f58, %f62 ; \
	aes_dround01	%f16, %f0, %f2, %f6 ; \
	aes_dround23	%f18, %f0, %f2, %f4 ; \
	aes_dround01	%f16, %f60, %f62, %f56 ; \
	aes_dround23	%f18, %f60, %f62, %f58 ; \
	aes_dround01	%f20, %f6, %f4, %f0 ; \
	aes_dround23	%f22, %f6, %f4, %f2 ; \
	aes_dround01	%f20, %f56, %f58, %f60 ; \
	aes_dround23	%f22, %f56, %f58, %f62 ; \
	aes_dround01	%f24, %f0, %f2, %f6 ; \
	aes_dround23	%f26, %f0, %f2, %f4 ; \
	aes_dround01	%f24, %f60, %f62, %f56 ; \
	aes_dround23	%f26, %f60, %f62, %f58 ; \
	aes_dround01	%f28, %f6, %f4, %f0 ; \
	aes_dround23	%f30, %f6, %f4, %f2 ; \
	aes_dround01	%f28, %f56, %f58, %f60 ; \
	aes_dround23	%f30, %f56, %f58, %f62 ; \
	aes_dround01	%f32, %f0, %f2, %f6 ; \
	aes_dround23	%f34, %f0, %f2, %f4 ; \
	aes_dround01	%f32, %f60, %f62, %f56 ; \
	aes_dround23	%f34, %f60, %f62, %f58 ; \
	aes_dround01	%f36, %f6, %f4, %f0 ; \
	aes_dround23	%f38, %f6, %f4, %f2 ; \
	aes_dround01	%f36, %f56, %f58, %f60 ; \
	aes_dround23	%f38, %f56, %f58, %f62 ; \
	aes_dround01	%f40, %f0, %f2, %f6 ; \
	aes_dround23	%f42, %f0, %f2, %f4 ; \
	aes_dround01	%f40, %f60, %f62, %f56 ; \
	aes_dround23	%f42, %f60, %f62, %f58 ; \
	aes_dround01	%f44, %f6, %f4, %f0 ; \
	aes_dround23	%f46, %f6, %f4, %f2 ; \
	aes_dround01	%f44, %f56, %f58, %f60 ; \
	aes_dround23	%f46, %f56, %f58, %f62 ; \
	aes_dround01	%f48, %f0, %f2, %f6 ; \
	aes_dround23	%f50, %f0, %f2, %f4 ; \
	ldd	[%o0 + 0xd0], %f0 ; \
	aes_dround01	%f48, %f60, %f62, %f56 ; \
	ldd	[%o0 + 0xd8], %f2 ; \
	aes_dround23	%f50, %f60, %f62, %f58 ; \
	aes_dround01_l	%f52, %f6, %f4, %f20 ; \
	aes_dround23_l	%f54, %f6, %f4, %f22 ; \
	ldd	[%o0 + 0xc0], %f4 ; \
	aes_dround01_l	%f52, %f56, %f58, %f60 ; \
	ldd	[%o0 + 0xc8], %f6 ; \
	aes_dround23_l	%f54, %f56, %f58, %f62


	ENTRY(yf_aes128_load_keys_for_encrypt)

	ldd	[%o0 + 0x10], %f16
	ldd	[%o0 + 0x18], %f18
	ldd	[%o0 + 0x20], %f20
	ldd	[%o0 + 0x28], %f22
	ldd	[%o0 + 0x30], %f24
	ldd	[%o0 + 0x38], %f26
	ldd	[%o0 + 0x40], %f28
	ldd	[%o0 + 0x48], %f30
	ldd	[%o0 + 0x50], %f32
	ldd	[%o0 + 0x58], %f34
	ldd	[%o0 + 0x60], %f36
	ldd	[%o0 + 0x68], %f38
	ldd	[%o0 + 0x70], %f40
	ldd	[%o0 + 0x78], %f42
	ldd	[%o0 + 0x80], %f44
	ldd	[%o0 + 0x88], %f46
	ldd	[%o0 + 0x90], %f48
	ldd	[%o0 + 0x98], %f50
	ldd	[%o0 + 0xa0], %f52
	retl
	ldd	[%o0 + 0xa8], %f54

	SET_SIZE(yf_aes128_load_keys_for_encrypt)


	ENTRY(yf_aes192_load_keys_for_encrypt)

	ldd	[%o0 + 0x10], %f8
	ldd	[%o0 + 0x18], %f10
	ldd	[%o0 + 0x20], %f12
	ldd	[%o0 + 0x28], %f14
	ldd	[%o0 + 0x30], %f16
	ldd	[%o0 + 0x38], %f18
	ldd	[%o0 + 0x40], %f20
	ldd	[%o0 + 0x48], %f22
	ldd	[%o0 + 0x50], %f24
	ldd	[%o0 + 0x58], %f26
	ldd	[%o0 + 0x60], %f28
	ldd	[%o0 + 0x68], %f30
	ldd	[%o0 + 0x70], %f32
	ldd	[%o0 + 0x78], %f34
	ldd	[%o0 + 0x80], %f36
	ldd	[%o0 + 0x88], %f38
	ldd	[%o0 + 0x90], %f40
	ldd	[%o0 + 0x98], %f42
	ldd	[%o0 + 0xa0], %f44
	ldd	[%o0 + 0xa8], %f46
	ldd	[%o0 + 0xb0], %f48
	ldd	[%o0 + 0xb8], %f50
	ldd	[%o0 + 0xc0], %f52
	retl
	ldd	[%o0 + 0xc8], %f54

	SET_SIZE(yf_aes192_load_keys_for_encrypt)

	
	ENTRY(yf_aes256_load_keys_for_encrypt)

	ldd	[%o0 + 0x10], %f0
	ldd	[%o0 + 0x18], %f2
	ldd	[%o0 + 0x20], %f4
	ldd	[%o0 + 0x28], %f6
	ldd	[%o0 + 0x30], %f8
	ldd	[%o0 + 0x38], %f10
	ldd	[%o0 + 0x40], %f12
	ldd	[%o0 + 0x48], %f14
	ldd	[%o0 + 0x50], %f16
	ldd	[%o0 + 0x58], %f18
	ldd	[%o0 + 0x60], %f20
	ldd	[%o0 + 0x68], %f22
	ldd	[%o0 + 0x70], %f24
	ldd	[%o0 + 0x78], %f26
	ldd	[%o0 + 0x80], %f28
	ldd	[%o0 + 0x88], %f30
	ldd	[%o0 + 0x90], %f32
	ldd	[%o0 + 0x98], %f34
	ldd	[%o0 + 0xa0], %f36
	ldd	[%o0 + 0xa8], %f38
	ldd	[%o0 + 0xb0], %f40
	ldd	[%o0 + 0xb8], %f42
	ldd	[%o0 + 0xc0], %f44
	ldd	[%o0 + 0xc8], %f46
	ldd	[%o0 + 0xd0], %f48
	ldd	[%o0 + 0xd8], %f50
	ldd	[%o0 + 0xe0], %f52
	retl
	ldd	[%o0 + 0xe8], %f54

	SET_SIZE(yf_aes256_load_keys_for_encrypt)


#define	TEST_PARALLEL_ECB_ENCRYPT
#ifdef  TEST_PARALLEL_ECB_ENCRYPT
	ENTRY(yf_aes128_ecb_encrypt)

	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]
	and	%o3, 16, %o4
	brz	%o4, ecbenc128_loop
	nop

	ldx	[%o1], %g3	!input
	ldx	[%o1 + 8], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f60
	movxtod	%g4, %f62

	TEN_EROUNDS

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	be	ecbenc128_loop_end
	add	%o2, 16, %o2
	
ecbenc128_loop:
	ldx	[%o1], %g3	!input
	ldx	[%o1 + 8], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f0
	movxtod	%g4, %f2
	ldx	[%o1 + 16], %g3	!input
	ldx	[%o1 + 24], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f60
	movxtod	%g4, %f62

	TEN_EROUNDS_2

	std	%f0, [%o2]
	std	%f2, [%o2 + 8]

	std	%f60, [%o2 + 16]
	std	%f62, [%o2 + 24]

	add	%o1, 32, %o1
	subcc	%o3, 32, %o3
	bne	ecbenc128_loop
	add	%o2, 32, %o2
ecbenc128_loop_end:
	retl
	nop
	
	SET_SIZE(yf_aes128_ecb_encrypt)


	ENTRY(yf_aes192_ecb_encrypt)

	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]
	and	%o3, 16, %o4
	brz	%o4, ecbenc192_loop
	nop

	ldx	[%o1], %g3	!input
	ldx	[%o1 + 8], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f60
	movxtod	%g4, %f62

	TWELVE_EROUNDS

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	be	ecbenc192_loop_end
	add	%o2, 16, %o2
	
ecbenc192_loop:
	ldx	[%o1], %g3	!input
	ldx	[%o1 + 8], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f0
	movxtod	%g4, %f2
	ldx	[%o1 + 16], %g3	!input
	ldx	[%o1 + 24], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f60
	movxtod	%g4, %f62

	TWELVE_EROUNDS_2

	std	%f0, [%o2]
	std	%f2, [%o2 + 8]

	std	%f60, [%o2 + 16]
	std	%f62, [%o2 + 24]

	add	%o1, 32, %o1
	subcc	%o3, 32, %o3
	bne	ecbenc192_loop
	add	%o2, 32, %o2
ecbenc192_loop_end:
	retl
	nop
	
	SET_SIZE(yf_aes192_ecb_encrypt)
	

	ENTRY(yf_aes256_ecb_encrypt)

	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]
	and	%o3, 16, %o4
	brz	%o4, ecbenc256_loop
	nop

	ldx	[%o1], %g3	!input
	ldx	[%o1 + 8], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f60
	movxtod	%g4, %f62

	FOURTEEN_EROUNDS

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	be	ecbenc256_loop_end
	add	%o2, 16, %o2
	
ecbenc256_loop:
	ldx	[%o1], %g3	!input
	ldx	[%o1 + 8], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f20
	movxtod	%g4, %f22
	ldx	[%o1 + 16], %g3	!input
	ldx	[%o1 + 24], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f60
	movxtod	%g4, %f62

	FOURTEEN_EROUNDS_2

	std	%f20, [%o2]
	std	%f22, [%o2 + 8]

	std	%f60, [%o2 + 16]
	std	%f62, [%o2 + 24]

	add	%o1, 32, %o1
	subcc	%o3, 32, %o3
	bne	ecbenc256_loop
	add	%o2, 32, %o2

	ldd	[%o0 + 0x60], %f20
	ldd	[%o0 + 0x68], %f22

ecbenc256_loop_end:
	retl
	nop
	
	SET_SIZE(yf_aes256_ecb_encrypt)

#else

	ENTRY(yf_aes128_ecb_encrypt)

	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

ecbenc128_loop:
	ldx	[%o1], %g3	!input
	ldx	[%o1 + 8], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f60
	movxtod	%g4, %f62

	TEN_EROUNDS

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	ecbenc128_loop
	add	%o2, 16, %o2

	retl
	nop
	
	SET_SIZE(yf_aes128_ecb_encrypt)


	ENTRY(yf_aes192_ecb_encrypt)

	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

ecbenc192_loop:
	ldx	[%o1], %g3	!input
	ldx	[%o1 + 8], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f60
	movxtod	%g4, %f62
	
	TWELVE_EROUNDS

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	ecbenc192_loop
	add	%o2, 16, %o2

	retl
	nop

	SET_SIZE(yf_aes192_ecb_encrypt)

	
	ENTRY(yf_aes256_ecb_encrypt)

	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

ecbenc256_loop:
	ldx	[%o1], %g3	!input
	ldx	[%o1 + 8], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f60
	movxtod	%g4, %f62
	
	FOURTEEN_EROUNDS

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	ecbenc256_loop
	add	%o2, 16, %o2

	retl
	nop

	SET_SIZE(yf_aes256_ecb_encrypt)
#endif


	ENTRY(yf_aes128_cbc_encrypt)

	ldd	[%o4], %f60	! IV
	ldd	[%o4 +8], %f62	! IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

cbcenc128_loop:
	ldx	[%o1], %g3	!input
	ldx	[%o1 + 8], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f56
	movxtod	%g4, %f58
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	TEN_EROUNDS

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	cbcenc128_loop
	add	%o2, 16, %o2

	std	%f60, [%o4]
	retl
	std	%f62, [%o4 + 8]
	
	SET_SIZE(yf_aes128_cbc_encrypt)


	ENTRY(yf_aes192_cbc_encrypt)

	ldd	[%o4], %f60	! IV
	ldd	[%o4 + 8], %f62	! IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

cbcenc192_loop:
	ldx	[%o1], %g3	!input
	ldx	[%o1 + 8], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f56
	movxtod	%g4, %f58
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62
	
	TWELVE_EROUNDS

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	cbcenc192_loop
	add	%o2, 16, %o2

	std	%f60, [%o4]
	retl
	std	%f62, [%o4 + 8]

	SET_SIZE(yf_aes192_cbc_encrypt)

	
	ENTRY(yf_aes256_cbc_encrypt)

	ldd	[%o4], %f60	! IV
	ldd	[%o4 + 8], %f62	! IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

cbcenc256_loop:
	ldx	[%o1], %g3	!input
	ldx	[%o1 + 8], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f56
	movxtod	%g4, %f58
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62
	
	FOURTEEN_EROUNDS

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	cbcenc256_loop
	add	%o2, 16, %o2

	std	%f60, [%o4]
	retl
	std	%f62, [%o4 + 8]

	SET_SIZE(yf_aes256_cbc_encrypt)



	ENTRY(yf_aes128_cbc_mac)

	ldd	[%o2], %f60	! IV
	ldd	[%o2 +8], %f62	! IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

cbcmac128_loop:
	ldx	[%o1], %g3	!input
	ldx	[%o1 + 8], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f56
	movxtod	%g4, %f58
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	TEN_EROUNDS

	subcc	%o3, 1, %o3
	bne	cbcmac128_loop
	add	%o1, 16, %o1

	std	%f60, [%o2]
	retl
	std	%f62, [%o2 + 8]
	
	SET_SIZE(yf_aes128_cbc_mac)


	ENTRY(yf_aes192_cbc_mac)

	ldd	[%o2], %f60	! IV
	ldd	[%o2 + 8], %f62	! IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

cbcmac192_loop:
	ldx	[%o1], %g3	!input
	ldx	[%o1 + 8], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f56
	movxtod	%g4, %f58
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62
	
	TWELVE_EROUNDS

	subcc	%o3, 1, %o3
	bne	cbcmac192_loop
	add	%o1, 16, %o1

	std	%f60, [%o2]
	retl
	std	%f62, [%o2 + 8]

	SET_SIZE(yf_aes192_cbc_mac)

	
	ENTRY(yf_aes256_cbc_mac)

	ldd	[%o2], %f60	! IV
	ldd	[%o2 + 8], %f62	! IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

cbcmac256_loop:
	ldx	[%o1], %g3	!input
	ldx	[%o1 + 8], %g4	!input
	xor	%g1, %g3, %g3	!input ^ ks[0-1]
	xor	%g2, %g4, %g4	!input ^ ks[0-1]
	movxtod	%g3, %f56
	movxtod	%g4, %f58
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62
	
	FOURTEEN_EROUNDS

	subcc	%o3, 1, %o3
	bne	cbcmac256_loop
	add	%o1, 16, %o1

	std	%f60, [%o2]
	retl
	std	%f62, [%o2 + 8]

	SET_SIZE(yf_aes256_cbc_mac)


#define	TEST_PARALLEL_CTR_CRYPT
#ifdef  TEST_PARALLEL_CTR_CRYPT
	ENTRY(yf_aes128_ctr_crypt)

	ldx	[%o4], %g3	! IV
	ldx	[%o4 +8], %g4	! IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]
	and	%o3, 16, %g5
	brz, %g5, ctr128_loop

	xor	%g1, %g3, %g5
	movxtod	%g5, %f60
	xor	%g2, %g4, %g5
	movxtod	%g5, %f62
	inc	%g4

	TEN_EROUNDS

	ldd	[%o1], %f56	!input
	ldd	[%o1 + 8], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62
	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	be	ctr128_loop_end
	add	%o2, 16, %o2

ctr128_loop:
	xor	%g1, %g3, %g5
	movxtod	%g5, %f0
	xor	%g2, %g4, %g5
	movxtod	%g5, %f2
	inc	%g4

	xor	%g1, %g3, %g5
	movxtod	%g5, %f60
	xor	%g2, %g4, %g5
	movxtod	%g5, %f62
	inc	%g4
	
	TEN_EROUNDS_2

	ldd	[%o1], %f6		!input
	ldd	[%o1 + 8], %f4		!input
	ldd	[%o1 + 16], %f56	!input
	ldd	[%o1 + 24], %f58	!input
	fxor	%f0, %f6, %f0
	fxor	%f2, %f4, %f2
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62
	std	%f0, [%o2]
	std	%f2, [%o2 + 8]
	std	%f60, [%o2 + 16]
	std	%f62, [%o2 + 24]

	add	%o1, 32, %o1
	subcc	%o3, 32, %o3
	bne	ctr128_loop
	add	%o2, 32, %o2

ctr128_loop_end:
	stx	%g3, [%o4]
	retl
	stx	%g4, [%o4 + 8]
	
	SET_SIZE(yf_aes128_ctr_crypt)


	ENTRY(yf_aes192_ctr_crypt)

	ldx	[%o4], %g3	! IV
	ldx	[%o4 +8], %g4	! IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]
	and	%o3, 16, %g5
	brz, %g5, ctr192_loop

	xor	%g1, %g3, %g5
	movxtod	%g5, %f60
	xor	%g2, %g4, %g5
	movxtod	%g5, %f62
	inc	%g4

	TWELVE_EROUNDS

	ldd	[%o1], %f56	!input
	ldd	[%o1 + 8], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62
	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	be	ctr192_loop_end
	add	%o2, 16, %o2

ctr192_loop:
	xor	%g1, %g3, %g5
	movxtod	%g5, %f0
	xor	%g2, %g4, %g5
	movxtod	%g5, %f2
	inc	%g4

	xor	%g1, %g3, %g5
	movxtod	%g5, %f60
	xor	%g2, %g4, %g5
	movxtod	%g5, %f62
	inc	%g4
	
	TWELVE_EROUNDS_2

	ldd	[%o1], %f6		!input
	ldd	[%o1 + 8], %f4		!input
	ldd	[%o1 + 16], %f56	!input
	ldd	[%o1 + 24], %f58	!input
	fxor	%f0, %f6, %f0
	fxor	%f2, %f4, %f2
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62
	std	%f0, [%o2]
	std	%f2, [%o2 + 8]
	std	%f60, [%o2 + 16]
	std	%f62, [%o2 + 24]

	add	%o1, 32, %o1
	subcc	%o3, 32, %o3
	bne	ctr192_loop
	add	%o2, 32, %o2

ctr192_loop_end:
	stx	%g3, [%o4]
	retl
	stx	%g4, [%o4 + 8]
	
	SET_SIZE(yf_aes192_ctr_crypt)


	ENTRY(yf_aes256_ctr_crypt)

	ldx	[%o4], %g3	! IV
	ldx	[%o4 +8], %g4	! IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]
	and	%o3, 16, %g5
	brz,	%g5, ctr256_loop

	xor	%g1, %g3, %g5
	movxtod	%g5, %f60
	xor	%g2, %g4, %g5
	movxtod	%g5, %f62
	inc	%g4

	FOURTEEN_EROUNDS

	ldd	[%o1], %f56	!input
	ldd	[%o1 + 8], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62
	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	be	ctr256_loop_end
	add	%o2, 16, %o2

ctr256_loop:
	xor	%g1, %g3, %g5
	movxtod	%g5, %f20
	xor	%g2, %g4, %g5
	movxtod	%g5, %f22
	inc	%g4

	xor	%g1, %g3, %g5
	movxtod	%g5, %f60
	xor	%g2, %g4, %g5
	movxtod	%g5, %f62
	inc	%g4
	
	FOURTEEN_EROUNDS_2

	ldd	[%o1], %f56		!input
	ldd	[%o1 + 8], %f58		!input
	fxor	%f20, %f56, %f20
	fxor	%f22, %f58, %f22
	ldd	[%o1 + 16], %f56	!input
	ldd	[%o1 + 24], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62
	std	%f20, [%o2]
	std	%f22, [%o2 + 8]
	std	%f60, [%o2 + 16]
	std	%f62, [%o2 + 24]

	add	%o1, 32, %o1
	subcc	%o3, 32, %o3
	bne	ctr256_loop
	add	%o2, 32, %o2

	ldd	[%o0 + 0x60], %f20
	ldd	[%o0 + 0x68], %f22

ctr256_loop_end:
	stx	%g3, [%o4]
	retl
	stx	%g4, [%o4 + 8]
	
	SET_SIZE(yf_aes256_ctr_crypt)

#else

	ENTRY(yf_aes128_ctr_crypt)

	ldx	[%o4], %g3	! IV
	ldx	[%o4 +8], %g4	! IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

ctr128_loop:
	xor	%g1, %g3, %g5
	movxtod	%g5, %f60
	xor	%g2, %g4, %g5
	movxtod	%g5, %f62
	inc	%g4

	TEN_EROUNDS

	ldd	[%o1], %f56	!input
	ldd	[%o1 + 8], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62
	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	ctr128_loop
	add	%o2, 16, %o2

	stx	%g3, [%o4]
	retl
	stx	%g4, [%o4 + 8]
	
	SET_SIZE(yf_aes128_ctr_crypt)

	ENTRY(yf_aes192_ctr_crypt)

	ldx	[%o4], %g3	! IV
	ldx	[%o4 +8], %g4	! IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

ctr192_loop:
	xor	%g1, %g3, %g5
	movxtod	%g5, %f60
	xor	%g2, %g4, %g5
	movxtod	%g5, %f62
	inc	%g4

	TWELVE_EROUNDS

	ldd	[%o1], %f56	!input
	ldd	[%o1 + 8], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62
	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	ctr192_loop
	add	%o2, 16, %o2

	stx	%g3, [%o4]
	retl
	stx	%g4, [%o4 + 8]
	
	SET_SIZE(yf_aes192_ctr_crypt)


	ENTRY(yf_aes256_ctr_crypt)

	ldx	[%o4], %g3	! IV
	ldx	[%o4 +8], %g4	! IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

ctr256_loop:
	xor	%g1, %g3, %g5
	movxtod	%g5, %f60
	xor	%g2, %g4, %g5
	movxtod	%g5, %f62
	inc	%g4

	FOURTEEN_EROUNDS

	ldd	[%o1], %f56	!input
	ldd	[%o1 + 8], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62
	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	ctr256_loop
	add	%o2, 16, %o2

	stx	%g3, [%o4]
	retl
	stx	%g4, [%o4 + 8]
	
	SET_SIZE(yf_aes256_ctr_crypt)

#endif

	ENTRY(yf_aes128_cfb128_encrypt)

	ldd	[%o4], %f60	! IV
	ldd	[%o4 +8], %f62	! IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

cfb128_128_loop:
	movxtod	%g1, %f56
	movxtod	%g2, %f58
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	TEN_EROUNDS

	ldd	[%o1], %f56	!input
	ldd	[%o1 + 8], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	cfb128_128_loop
	add	%o2, 16, %o2

	std	%f60, [%o4]
	retl
	std	%f62, [%o4 + 8]
	
	SET_SIZE(yf_aes128_cfb128_encrypt)


	ENTRY(yf_aes192_cfb128_encrypt)

	ldd	[%o4], %f60	! IV
	ldd	[%o4 +8], %f62	! IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

cfb128_192_loop:
	movxtod	%g1, %f56
	movxtod	%g2, %f58
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	TWELVE_EROUNDS

	ldd	[%o1], %f56	!input
	ldd	[%o1 + 8], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	cfb128_192_loop
	add	%o2, 16, %o2

	std	%f60, [%o4]
	retl
	std	%f62, [%o4 + 8]
	
	SET_SIZE(yf_aes192_cfb128_encrypt)


	ENTRY(yf_aes256_cfb128_encrypt)

	ldd	[%o4], %f60	! IV
	ldd	[%o4 +8], %f62	! IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

cfb128_256_loop:
	movxtod	%g1, %f56
	movxtod	%g2, %f58
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	FOURTEEN_EROUNDS

	ldd	[%o1], %f56	!input
	ldd	[%o1 + 8], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	cfb128_256_loop
	add	%o2, 16, %o2

	std	%f60, [%o4]
	retl
	std	%f62, [%o4 + 8]
	
	SET_SIZE(yf_aes256_cfb128_encrypt)


	ENTRY(yf_aes128_load_keys_for_decrypt)

	ldd	[%o0], %f52
	ldd	[%o0 + 0x8], %f54
	ldd	[%o0 + 0x10], %f48
	ldd	[%o0 + 0x18], %f50
	ldd	[%o0 + 0x20], %f44
	ldd	[%o0 + 0x28], %f46
	ldd	[%o0 + 0x30], %f40
	ldd	[%o0 + 0x38], %f42
	ldd	[%o0 + 0x40], %f36
	ldd	[%o0 + 0x48], %f38
	ldd	[%o0 + 0x50], %f32
	ldd	[%o0 + 0x58], %f34
	ldd	[%o0 + 0x60], %f28
	ldd	[%o0 + 0x68], %f30
	ldd	[%o0 + 0x70], %f24
	ldd	[%o0 + 0x78], %f26
	ldd	[%o0 + 0x80], %f20
	ldd	[%o0 + 0x88], %f22
	ldd	[%o0 + 0x90], %f16
	retl
	ldd	[%o0 + 0x98], %f18

	SET_SIZE(yf_aes128_load_keys_for_decrypt)

	
	ENTRY(yf_aes192_load_keys_for_decrypt)

	ldd	[%o0], %f52
	ldd	[%o0 + 0x8], %f54
	ldd	[%o0 + 0x10], %f48
	ldd	[%o0 + 0x18], %f50
	ldd	[%o0 + 0x20], %f44
	ldd	[%o0 + 0x28], %f46
	ldd	[%o0 + 0x30], %f40
	ldd	[%o0 + 0x38], %f42
	ldd	[%o0 + 0x40], %f36
	ldd	[%o0 + 0x48], %f38
	ldd	[%o0 + 0x50], %f32
	ldd	[%o0 + 0x58], %f34
	ldd	[%o0 + 0x60], %f28
	ldd	[%o0 + 0x68], %f30
	ldd	[%o0 + 0x70], %f24
	ldd	[%o0 + 0x78], %f26
	ldd	[%o0 + 0x80], %f20
	ldd	[%o0 + 0x88], %f22
	ldd	[%o0 + 0x90], %f16
	ldd	[%o0 + 0x98], %f18
	ldd	[%o0 + 0xa0], %f12
	ldd	[%o0 + 0xa8], %f14
	ldd	[%o0 + 0xb0], %f8
	retl
	ldd	[%o0 + 0xb8], %f10

	SET_SIZE(yf_aes192_load_keys_for_decrypt)

	
	ENTRY(yf_aes256_load_keys_for_decrypt)


	ldd	[%o0], %f52
	ldd	[%o0 + 0x8], %f54
	ldd	[%o0 + 0x10], %f48
	ldd	[%o0 + 0x18], %f50
	ldd	[%o0 + 0x20], %f44
	ldd	[%o0 + 0x28], %f46
	ldd	[%o0 + 0x30], %f40
	ldd	[%o0 + 0x38], %f42
	ldd	[%o0 + 0x40], %f36
	ldd	[%o0 + 0x48], %f38
	ldd	[%o0 + 0x50], %f32
	ldd	[%o0 + 0x58], %f34
	ldd	[%o0 + 0x60], %f28
	ldd	[%o0 + 0x68], %f30
	ldd	[%o0 + 0x70], %f24
	ldd	[%o0 + 0x78], %f26
	ldd	[%o0 + 0x80], %f20
	ldd	[%o0 + 0x88], %f22
	ldd	[%o0 + 0x90], %f16
	ldd	[%o0 + 0x98], %f18
	ldd	[%o0 + 0xa0], %f12
	ldd	[%o0 + 0xa8], %f14
	ldd	[%o0 + 0xb0], %f8
	ldd	[%o0 + 0xb8], %f10
	ldd	[%o0 + 0xc0], %f4
	ldd	[%o0 + 0xc8], %f6
	ldd	[%o0 + 0xd0], %f0
	retl
	ldd	[%o0 + 0xd8], %f2

	SET_SIZE(yf_aes256_load_keys_for_decrypt)


#define	TEST_PARALLEL_ECB_DECRYPT
#ifdef  TEST_PARALLEL_ECB_DECRYPT
	ENTRY(yf_aes128_ecb_decrypt)
	
	ldx	[%o0 + 0xa0], %g1	!ks[last-1]
	ldx	[%o0 + 0xa8], %g2	!ks[last]
	and	%o3, 16, %o4
	brz	%o4, ecbdec128_loop
	nop

	ldx	[%o1], %o4
	ldx	[%o1 + 8], %o5
	xor	%g1, %o4, %g3	!initial ARK
	movxtod	%g3, %f60
	xor	%g2, %o5, %g3	!initial ARK
	movxtod	%g3, %f62

	TEN_DROUNDS

	std	%f60, [%o2]
	std	%f62, [%o2 + 0x8]
	
	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	be	ecbdec128_loop_end
	add	%o2, 16, %o2
	
ecbdec128_loop:
	ldx	[%o1], %o4
	ldx	[%o1 + 8], %o5
	xor	%g1, %o4, %g3	!initial ARK
	movxtod	%g3, %f0
	xor	%g2, %o5, %g3	!initial ARK
	movxtod	%g3, %f2
	ldx	[%o1 + 16], %o4
	ldx	[%o1 + 24], %o5
	xor	%g1, %o4, %g3	!initial ARK
	movxtod	%g3, %f60
	xor	%g2, %o5, %g3	!initial ARK
	movxtod	%g3, %f62

	TEN_DROUNDS_2

	std	%f0, [%o2]
	std	%f2, [%o2 + 8]
	std	%f60, [%o2 + 16]
	std	%f62, [%o2 + 24]
	
	add	%o1, 32, %o1
	subcc	%o3, 32, %o3
	bne	ecbdec128_loop
	add	%o2, 32, %o2
ecbdec128_loop_end:

	retl
	nop

	SET_SIZE(yf_aes128_ecb_decrypt)
	
	ENTRY(yf_aes192_ecb_decrypt)
	
	ldx	[%o0 + 0xc0], %g1	!ks[last-1]
	ldx	[%o0 + 0xc8], %g2	!ks[last]
	and	%o3, 16, %o4
	brz	%o4, ecbdec192_loop
	nop

	ldx	[%o1], %o4
	ldx	[%o1 + 8], %o5
	xor	%g1, %o4, %g3	!initial ARK
	movxtod	%g3, %f60
	xor	%g2, %o5, %g3	!initial ARK
	movxtod	%g3, %f62

	TWELVE_DROUNDS

	std	%f60, [%o2]
	std	%f62, [%o2 + 0x8]
	
	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	be	ecbdec192_loop_end
	add	%o2, 16, %o2
	
ecbdec192_loop:
	ldx	[%o1], %o4
	ldx	[%o1 + 8], %o5
	xor	%g1, %o4, %g3	!initial ARK
	movxtod	%g3, %f0
	xor	%g2, %o5, %g3	!initial ARK
	movxtod	%g3, %f2
	ldx	[%o1 + 16], %o4
	ldx	[%o1 + 24], %o5
	xor	%g1, %o4, %g3	!initial ARK
	movxtod	%g3, %f60
	xor	%g2, %o5, %g3	!initial ARK
	movxtod	%g3, %f62

	TWELVE_DROUNDS_2

	std	%f0, [%o2]
	std	%f2, [%o2 + 8]
	std	%f60, [%o2 + 16]
	std	%f62, [%o2 + 24]
	
	add	%o1, 32, %o1
	subcc	%o3, 32, %o3
	bne	ecbdec192_loop
	add	%o2, 32, %o2
ecbdec192_loop_end:

	retl
	nop

	SET_SIZE(yf_aes192_ecb_decrypt)

	
	ENTRY(yf_aes256_ecb_decrypt)
	
	ldx	[%o0 + 0xe0], %g1	!ks[last-1]
	ldx	[%o0 + 0xe8], %g2	!ks[last]
	and	%o3, 16, %o4
	brz	%o4, ecbdec256_loop
	nop

	ldx	[%o1], %o4
	ldx	[%o1 + 8], %o5
	xor	%g1, %o4, %g3	!initial ARK
	movxtod	%g3, %f60
	xor	%g2, %o5, %g3	!initial ARK
	movxtod	%g3, %f62

	FOURTEEN_DROUNDS

	std	%f60, [%o2]
	std	%f62, [%o2 + 0x8]
	
	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	be	ecbdec256_loop_end
	add	%o2, 16, %o2
	
ecbdec256_loop:
	ldx	[%o1], %o4
	ldx	[%o1 + 8], %o5
	xor	%g1, %o4, %g3	!initial ARK
	movxtod	%g3, %f20
	xor	%g2, %o5, %g3	!initial ARK
	movxtod	%g3, %f22
	ldx	[%o1 + 16], %o4
	ldx	[%o1 + 24], %o5
	xor	%g1, %o4, %g3	!initial ARK
	movxtod	%g3, %f60
	xor	%g2, %o5, %g3	!initial ARK
	movxtod	%g3, %f62

	FOURTEEN_DROUNDS_2

	std	%f20, [%o2]
	std	%f22, [%o2 + 8]
	std	%f60, [%o2 + 16]
	std	%f62, [%o2 + 24]
	
	add	%o1, 32, %o1
	subcc	%o3, 32, %o3
	bne	ecbdec256_loop
	add	%o2, 32, %o2

	ldd	[%o0 + 0x80], %f20
	ldd	[%o0 + 0x88], %f22

ecbdec256_loop_end:

	retl
	nop

	SET_SIZE(yf_aes256_ecb_decrypt)
	
#else

	ENTRY(yf_aes128_ecb_decrypt)
	
	ldx	[%o0 + 0xa0], %g1	!ks[last-1]
	ldx	[%o0 + 0xa8], %g2	!ks[last]

ecbdec128_loop:
	ldx	[%o1], %o4
	ldx	[%o1 + 8], %o5
	xor	%g1, %o4, %g3	!initial ARK
	movxtod	%g3, %f60
	xor	%g2, %o5, %g3	!initial ARK
	movxtod	%g3, %f62

	TEN_DROUNDS

	std	%f60, [%o2]
	std	%f62, [%o2 + 0x8]
	
	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	ecbdec128_loop
	add	%o2, 16, %o2

	retl
	nop

	SET_SIZE(yf_aes128_ecb_decrypt)

	
	ENTRY(yf_aes192_ecb_decrypt)
	
	ldx	[%o0 + 0xc0], %g1	!ks[last-1]
	ldx	[%o0 + 0xc8], %g2	!ks[last]

ecbdec192_loop:
	ldx	[%o1], %o4
	ldx	[%o1 + 8], %o5
	xor	%g1, %o4, %g3	!initial ARK
	movxtod	%g3, %f60
	xor	%g2, %o5, %g3	!initial ARK
	movxtod	%g3, %f62

	TWELVE_DROUNDS

	std	%f60, [%o2]
	std	%f62, [%o2 + 0x8]
	
	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	ecbdec192_loop
	add	%o2, 16, %o2

	retl
	nop

	SET_SIZE(yf_aes192_ecb_decrypt)


	ENTRY(yf_aes256_ecb_decrypt)

	ldx	[%o0 + 0xe0], %g1	!ks[last-1]
	ldx	[%o0 + 0xe8], %g2	!ks[last]

ecbdec256_loop:
	ldx	[%o1], %o4
	ldx	[%o1 + 8], %o5
	xor	%g1, %o4, %g3	!initial ARK
	movxtod	%g3, %f60
	xor	%g2, %o5, %g3	!initial ARK
	movxtod	%g3, %f62

	FOURTEEN_DROUNDS

	std	%f60, [%o2]
	std	%f62, [%o2 + 0x8]
	
	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	ecbdec256_loop
	add	%o2, 16, %o2

	retl
	nop

	SET_SIZE(yf_aes256_ecb_decrypt)

#endif

#define	TEST_PARALLEL_CBC_DECRYPT
#ifdef	TEST_PARALLEL_CBC_DECRYPT
		ENTRY(yf_aes128_cbc_decrypt)
	
	save    %sp, -SA(MINFRAME), %sp
	ldx	[%i4], %o0		!IV
	ldx	[%i4 + 8], %o1		!IV
	ldx	[%i0 + 0xa0], %o2	!ks[last-1]
	ldx	[%i0 + 0xa8], %o3	!ks[last]
	and	%i3, 16, %o4
	brz	%o4, cbcdec128_loop
	nop

	ldx	[%i1], %o4
	ldx	[%i1 + 8], %o5
	xor	%o2, %o4, %g1	!initial ARK
	movxtod	%g1, %f60
	xor	%o3, %o5, %g1	!initial ARK
	movxtod	%g1, %f62

	TEN_DROUNDS

	movxtod	%o0, %f56
	movxtod	%o1, %f58
	mov	%o4, %o0	!save last block as next IV
	mov	%o5, %o1
	fxor	%f56, %f60, %f60	!add in previous IV
	fxor	%f58, %f62, %f62
	
	std	%f60, [%i2]
	std	%f62, [%i2 + 0x8]
	
	add	%i1, 16, %i1
	subcc	%i3, 16, %i3
	be	cbcdec128_loop_end
	add	%i2, 16, %i2
	

cbcdec128_loop:
	ldx	[%i1], %g4
	ldx	[%i1 + 8], %g5
	xor	%o2, %g4, %g1	!initial ARK
	movxtod	%g1, %f0
	xor	%o3, %g5, %g1	!initial ARK
	movxtod	%g1, %f2
	
	ldx	[%i1 + 16], %o4
	ldx	[%i1 + 24], %o5
	xor	%o2, %o4, %g1	!initial ARK
	movxtod	%g1, %f60
	xor	%o3, %o5, %g1	!initial ARK
	movxtod	%g1, %f62

	TEN_DROUNDS_2

	movxtod	%o0, %f6
	movxtod	%o1, %f4
	fxor	%f6, %f0, %f0	!add in previous IV
	fxor	%f4, %f2, %f2
	
	std	%f0, [%i2]
	std	%f2, [%i2 + 8]

	movxtod	%g4, %f56
	movxtod	%g5, %f58
	mov	%o4, %o0	!save last block as next IV
	mov	%o5, %o1
	fxor	%f56, %f60, %f60	!add in previous IV
	fxor	%f58, %f62, %f62
	
	std	%f60, [%i2 + 16]
	std	%f62, [%i2 + 24]
	
	add	%i1, 32, %i1
	subcc	%i3, 32, %i3
	bne	cbcdec128_loop
	add	%i2, 32, %i2
	
cbcdec128_loop_end:
	stx	%o0, [%i4]
	stx	%o1, [%i4 + 8]
	ret
	restore

	SET_SIZE(yf_aes128_cbc_decrypt)


	ENTRY(yf_aes192_cbc_decrypt)
	
	save    %sp, -SA(MINFRAME), %sp
	ldx	[%i4], %o0		!IV
	ldx	[%i4 + 8], %o1		!IV
	ldx	[%i0 + 0xc0], %o2	!ks[last-1]
	ldx	[%i0 + 0xc8], %o3	!ks[last]
	and	%i3, 16, %o4
	brz	%o4, cbcdec192_loop
	nop

	ldx	[%i1], %o4
	ldx	[%i1 + 8], %o5
	xor	%o2, %o4, %g1	!initial ARK
	movxtod	%g1, %f60
	xor	%o3, %o5, %g1	!initial ARK
	movxtod	%g1, %f62

	TWELVE_DROUNDS

	movxtod	%o0, %f56
	movxtod	%o1, %f58
	mov	%o4, %o0	!save last block as next IV
	mov	%o5, %o1
	fxor	%f56, %f60, %f60	!add in previous IV
	fxor	%f58, %f62, %f62
	
	std	%f60, [%i2]
	std	%f62, [%i2 + 0x8]
	
	add	%i1, 16, %i1
	subcc	%i3, 16, %i3
	be	cbcdec192_loop_end
	add	%i2, 16, %i2
	

cbcdec192_loop:
	ldx	[%i1], %g4
	ldx	[%i1 + 8], %g5
	xor	%o2, %g4, %g1	!initial ARK
	movxtod	%g1, %f0
	xor	%o3, %g5, %g1	!initial ARK
	movxtod	%g1, %f2
	
	ldx	[%i1 + 16], %o4
	ldx	[%i1 + 24], %o5
	xor	%o2, %o4, %g1	!initial ARK
	movxtod	%g1, %f60
	xor	%o3, %o5, %g1	!initial ARK
	movxtod	%g1, %f62

	TWELVE_DROUNDS_2

	movxtod	%o0, %f6
	movxtod	%o1, %f4
	fxor	%f6, %f0, %f0	!add in previous IV
	fxor	%f4, %f2, %f2
	
	std	%f0, [%i2]
	std	%f2, [%i2 + 8]

	movxtod	%g4, %f56
	movxtod	%g5, %f58
	mov	%o4, %o0	!save last block as next IV
	mov	%o5, %o1
	fxor	%f56, %f60, %f60	!add in previous IV
	fxor	%f58, %f62, %f62
	
	std	%f60, [%i2 + 16]
	std	%f62, [%i2 + 24]
	
	add	%i1, 32, %i1
	subcc	%i3, 32, %i3
	bne	cbcdec192_loop
	add	%i2, 32, %i2
	
cbcdec192_loop_end:
	stx	%o0, [%i4]
	stx	%o1, [%i4 + 8]
	ret
	restore

	SET_SIZE(yf_aes192_cbc_decrypt)


	ENTRY(yf_aes256_cbc_decrypt)
	
	save    %sp, -SA(MINFRAME), %sp
	mov	%i0, %o0		!FOURTEEN_DROUNDS uses %o0
	ldx	[%i4], %g2		!IV
	ldx	[%i4 + 8], %o1		!IV
	ldx	[%o0 + 0xe0], %o2	!ks[last-1]
	ldx	[%o0 + 0xe8], %o3	!ks[last]
	and	%i3, 16, %o4
	brz	%o4, cbcdec256_loop
	nop

	ldx	[%i1], %o4
	ldx	[%i1 + 8], %o5
	xor	%o2, %o4, %g1	!initial ARK
	movxtod	%g1, %f60
	xor	%o3, %o5, %g1	!initial ARK
	movxtod	%g1, %f62

	FOURTEEN_DROUNDS

	movxtod	%g2, %f56
	movxtod	%o1, %f58
	mov	%o4, %g2	!save last block as next IV
	mov	%o5, %o1
	fxor	%f56, %f60, %f60	!add in previous IV
	fxor	%f58, %f62, %f62
	
	std	%f60, [%i2]
	std	%f62, [%i2 + 0x8]
	
	add	%i1, 16, %i1
	subcc	%i3, 16, %i3
	be	cbcdec256_loop_end
	add	%i2, 16, %i2
	

cbcdec256_loop:
	ldx	[%i1], %g4
	ldx	[%i1 + 8], %g5
	xor	%o2, %g4, %g1	!initial ARK
	movxtod	%g1, %f20
	xor	%o3, %g5, %g1	!initial ARK
	movxtod	%g1, %f22
	
	ldx	[%i1 + 16], %o4
	ldx	[%i1 + 24], %o5
	xor	%o2, %o4, %g1	!initial ARK
	movxtod	%g1, %f60
	xor	%o3, %o5, %g1	!initial ARK
	movxtod	%g1, %f62

	FOURTEEN_DROUNDS_2

	movxtod	%g2, %f56
	movxtod	%o1, %f58
	fxor	%f56, %f20, %f20	!add in previous IV
	fxor	%f58, %f22, %f22
	
	std	%f20, [%i2]
	std	%f22, [%i2 + 8]

	movxtod	%g4, %f56
	movxtod	%g5, %f58
	mov	%o4, %g2	!save last block as next IV
	mov	%o5, %o1
	fxor	%f56, %f60, %f60	!add in previous IV
	fxor	%f58, %f62, %f62
	
	std	%f60, [%i2 + 16]
	std	%f62, [%i2 + 24]
	
	add	%i1, 32, %i1
	subcc	%i3, 32, %i3
	bne	cbcdec256_loop
	add	%i2, 32, %i2

	ldd	[%o0 + 0x80], %f20
	ldd	[%o0 + 0x88], %f22
	
cbcdec256_loop_end:
	stx	%g2, [%i4]
	stx	%o1, [%i4 + 8]
	ret
	restore

	SET_SIZE(yf_aes256_cbc_decrypt)

#else

	ENTRY(yf_aes128_cbc_decrypt)
	
	save    %sp, -SA(MINFRAME), %sp
	ldx	[%i4], %o0		!IV
	ldx	[%i4 + 8], %o1		!IV
	ldx	[%i0 + 0xa0], %o2	!ks[last-1]
	ldx	[%i0 + 0xa8], %o3	!ks[last]

cbcdec128_loop:
	ldx	[%i1], %o4
	ldx	[%i1 + 8], %o5
	xor	%o2, %o4, %g1	!initial ARK
	movxtod	%g1, %f60
	xor	%o3, %o5, %g1	!initial ARK
	movxtod	%g1, %f62

	TEN_DROUNDS

	movxtod	%o0, %f56
	movxtod	%o1, %f58
	mov	%o4, %o0	!save last block as next IV
	mov	%o5, %o1
	fxor	%f56, %f60, %f60	!add in previous IV
	fxor	%f58, %f62, %f62
	
	std	%f60, [%i2]
	std	%f62, [%i2 + 0x8]
	
	add	%i1, 16, %i1
	subcc	%i3, 16, %i3
	bne	cbcdec128_loop
	add	%i2, 16, %i2

	stx	%o0, [%i4]
	stx	%o1, [%i4 + 8]
	ret
	restore

	SET_SIZE(yf_aes128_cbc_decrypt)

	
	ENTRY(yf_aes192_cbc_decrypt)
	
	save    %sp, -SA(MINFRAME), %sp
	ldx	[%i4], %o0		!IV
	ldx	[%i4 + 8], %o1		!IV
	ldx	[%i0 + 0xc0], %o2	!ks[last-1]
	ldx	[%i0 + 0xc8], %o3	!ks[last]

cbcdec192_loop:
	ldx	[%i1], %o4
	ldx	[%i1 + 8], %o5
	xor	%o2, %o4, %g1	!initial ARK
	movxtod	%g1, %f60
	xor	%o3, %o5, %g1	!initial ARK
	movxtod	%g1, %f62

	TWELVE_DROUNDS

	movxtod	%o0, %f56
	movxtod	%o1, %f58
	mov	%o4, %o0	!save last block as next IV
	mov	%o5, %o1
	fxor	%f56, %f60, %f60	!add in previous IV
	fxor	%f58, %f62, %f62
	
	std	%f60, [%i2]
	std	%f62, [%i2 + 0x8]
	
	add	%i1, 16, %i1
	subcc	%i3, 16, %i3
	bne	cbcdec192_loop
	add	%i2, 16, %i2

	stx	%o0, [%i4]
	stx	%o1, [%i4 + 8]
	ret
	restore

	SET_SIZE(yf_aes192_cbc_decrypt)


	ENTRY(yf_aes256_cbc_decrypt)

	save    %sp, -SA(MINFRAME), %sp
	ldx	[%i4], %o0		!IV
	ldx	[%i4 + 8], %o1		!IV
	ldx	[%i0 + 0xe0], %o2	!ks[last-1]
	ldx	[%i0 + 0xe8], %o3	!ks[last]

cbcdec256_loop:
	ldx	[%i1], %o4
	ldx	[%i1 + 8], %o5
	xor	%o2, %o4, %g1	!initial ARK
	movxtod	%g1, %f60
	xor	%o3, %o5, %g1	!initial ARK
	movxtod	%g1, %f62

	FOURTEEN_DROUNDS

	movxtod	%o0, %f56
	movxtod	%o1, %f58
	mov	%o4, %o0	!save last block as next IV
	mov	%o5, %o1
	fxor	%f56, %f60, %f60	!add in previous IV
	fxor	%f58, %f62, %f62
	
	std	%f60, [%i2]
	std	%f62, [%i2 + 0x8]
	
	add	%i1, 16, %i1
	subcc	%i3, 16, %i3
	bne	cbcdec256_loop
	add	%i2, 16, %i2

	stx	%o0, [%i4]
	stx	%o1, [%i4 + 8]
	ret
	restore

	SET_SIZE(yf_aes256_cbc_decrypt)

#endif

#define	TEST_PARALLEL_CFB128_DECRYPT
#ifdef	TEST_PARALLEL_CFB128_DECRYPT

	ENTRY(yf_aes128_cfb128_decrypt)

	ldd	[%o4], %f56	!IV
	ldd	[%o4 + 8], %f58	!IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]
	and	%o3, 16, %o5
	brz	%o5, cfb128dec_128_loop

	movxtod	%g1, %f60
	movxtod	%g2, %f62
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	/* CFB mode uses encryption for the decrypt operation */
	TEN_EROUNDS

	ldd	[%o1], %f56	!input
	ldd	[%o1 + 8], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	be	cfb128dec_128_loop_end
	add	%o2, 16, %o2

cfb128dec_128_loop:
	ldd	[%o1], %f6	!input
	ldd	[%o1 + 8], %f4	!input
	movxtod	%g1, %f60
	movxtod	%g2, %f62
	fxor	%f60, %f6, %f0
	fxor	%f62, %f4, %f2
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	/* CFB mode uses encryption for the decrypt operation */
	TEN_EROUNDS_2

	ldd	[%o1], %f6	!input
	ldd	[%o1 + 8], %f4	!input
	ldd	[%o1 + 16], %f56	!input
	ldd	[%o1 + 24], %f58	!input
	
	fxor	%f60, %f6, %f6
	fxor	%f62, %f4, %f4
	fxor	%f0, %f56, %f60
	fxor	%f2, %f58, %f62

	std	%f6, [%o2]
	std	%f4, [%o2 + 8]
	std	%f60, [%o2 + 16]
	std	%f62, [%o2 + 24]

	add	%o1, 32, %o1
	subcc	%o3, 32, %o3
	bne	cfb128dec_128_loop
	add	%o2, 32, %o2

cfb128dec_128_loop_end:
	std	%f56, [%o4]
	retl
	std	%f58, [%o4 + 8]

	SET_SIZE(yf_aes128_cfb128_decrypt)


	ENTRY(yf_aes192_cfb128_decrypt)

	ldd	[%o4], %f56	!IV
	ldd	[%o4 + 8], %f58	!IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]
	and	%o3, 16, %o5
	brz	%o5, cfb128dec_192_loop

	movxtod	%g1, %f60
	movxtod	%g2, %f62
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	/* CFB mode uses encryption for the decrypt operation */
	TWELVE_EROUNDS

	ldd	[%o1], %f56	!input
	ldd	[%o1 + 8], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	be	cfb128dec_192_loop_end
	add	%o2, 16, %o2

cfb128dec_192_loop:
	ldd	[%o1], %f6	!input
	ldd	[%o1 + 8], %f4	!input
	movxtod	%g1, %f60
	movxtod	%g2, %f62
	fxor	%f60, %f6, %f0
	fxor	%f62, %f4, %f2
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	/* CFB mode uses encryption for the decrypt operation */
	TWELVE_EROUNDS_2

	ldd	[%o1], %f6	!input
	ldd	[%o1 + 8], %f4	!input
	ldd	[%o1 + 16], %f56	!input
	ldd	[%o1 + 24], %f58	!input
	
	fxor	%f60, %f6, %f6
	fxor	%f62, %f4, %f4
	fxor	%f0, %f56, %f60
	fxor	%f2, %f58, %f62

	std	%f6, [%o2]
	std	%f4, [%o2 + 8]
	std	%f60, [%o2 + 16]
	std	%f62, [%o2 + 24]

	add	%o1, 32, %o1
	subcc	%o3, 32, %o3
	bne	cfb128dec_192_loop
	add	%o2, 32, %o2

cfb128dec_192_loop_end:
	std	%f56, [%o4]
	retl
	std	%f58, [%o4 + 8]

	SET_SIZE(yf_aes192_cfb128_decrypt)


	ENTRY(yf_aes256_cfb128_decrypt)

	ldd	[%o4], %f56	!IV
	ldd	[%o4 + 8], %f58	!IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]
	and	%o3, 16, %o5
	brz	%o5, cfb128dec_256_loop

	movxtod	%g1, %f60
	movxtod	%g2, %f62
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	/* CFB mode uses encryption for the decrypt operation */
	FOURTEEN_EROUNDS

	ldd	[%o1], %f56	!input
	ldd	[%o1 + 8], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	be	cfb128dec_256_loop_end
	add	%o2, 16, %o2

cfb128dec_256_loop:
	ldd	[%o1], %f20	!input
	ldd	[%o1 + 8], %f22	!input
	movxtod	%g1, %f60
	movxtod	%g2, %f62
	fxor	%f60, %f20, %f20
	fxor	%f62, %f22, %f22
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	/* CFB mode uses encryption for the decrypt operation */
	FOURTEEN_EROUNDS_2

	ldd	[%o1 + 16], %f56	!input
	ldd	[%o1 + 24], %f58	!input
	fxor	%f20, %f56, %f20
	fxor	%f22, %f58, %f22
	std	%f20, [%o2 + 16]
	std	%f22, [%o2 + 24]

	ldd	[%o1], %f20	!input
	ldd	[%o1 + 8], %f22	!input
	
	fxor	%f60, %f20, %f20
	fxor	%f62, %f22, %f22

	std	%f20, [%o2]
	std	%f22, [%o2 + 8]

	add	%o1, 32, %o1
	subcc	%o3, 32, %o3
	bne	cfb128dec_256_loop
	add	%o2, 32, %o2

	ldd	[%o0 + 0x60], %f20
	ldd	[%o0 + 0x68], %f22

cfb128dec_256_loop_end:
	std	%f56, [%o4]
	retl
	std	%f58, [%o4 + 8]

	SET_SIZE(yf_aes256_cfb128_decrypt)

#else
	ENTRY(yf_aes128_cfb128_decrypt)

	ldd	[%o4], %f56	!IV
	ldd	[%o4 + 8], %f58	!IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

cfb128dec_128_loop:
	movxtod	%g1, %f60
	movxtod	%g2, %f62
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	/* CFB mode uses encryption for the decrypt operation */
	TEN_EROUNDS

	ldd	[%o1], %f56	!input
	ldd	[%o1 + 8], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	cfb128dec_128_loop
	add	%o2, 16, %o2

	std	%f56, [%o4]
	retl
	std	%f58, [%o4 + 8]

	SET_SIZE(yf_aes128_cfb128_decrypt)


	ENTRY(yf_aes192_cfb128_decrypt)

	ldd	[%o4], %f56	!IV
	ldd	[%o4 + 8], %f58	!IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

cfb128dec_192_loop:
	movxtod	%g1, %f60
	movxtod	%g2, %f62
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	/* CFB mode uses encryption for the decrypt operation */
	TWELVE_EROUNDS

	ldd	[%o1], %f56	!input
	ldd	[%o1 + 8], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	cfb128dec_192_loop
	add	%o2, 16, %o2

	std	%f56, [%o4]
	retl
	std	%f58, [%o4 + 8]

	SET_SIZE(yf_aes192_cfb128_decrypt)


	ENTRY(yf_aes256_cfb128_decrypt)

	ldd	[%o4], %f56	!IV
	ldd	[%o4 + 8], %f58	!IV
	ldx	[%o0], %g1	! ks[0]
	ldx	[%o0 + 8], %g2	! ks[1]

cfb128dec_256_loop:
	movxtod	%g1, %f60
	movxtod	%g2, %f62
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	/* CFB mode uses encryption for the decrypt operation */
	FOURTEEN_EROUNDS

	ldd	[%o1], %f56	!input
	ldd	[%o1 + 8], %f58	!input
	fxor	%f60, %f56, %f60
	fxor	%f62, %f58, %f62

	std	%f60, [%o2]
	std	%f62, [%o2 + 8]

	add	%o1, 16, %o1
	subcc	%o3, 16, %o3
	bne	cfb128dec_256_loop
	add	%o2, 16, %o2

	std	%f56, [%o4]
	retl
	std	%f58, [%o4 + 8]

	SET_SIZE(yf_aes256_cfb128_decrypt)

#endif

#endif  /* lint || __lint */
