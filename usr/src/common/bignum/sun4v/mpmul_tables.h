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
 *  Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MPMUL_TABLES_H
#define	_MPMUL_TABLES_H

#ifdef	__cplusplus
extern "C" {
#endif
	.section	".text", #alloc, #execinstr
	.align	8

mpmul_64:
	.word	0x81b02900	! mpmul 0
	ba	mpmul_check_errors
	nop
mpmul_128:
	.word	0x81b02901	! mpmul 1
	ba	mpmul_check_errors
	nop
mpmul_192:
	.word	0x81b02902	! mpmul 2
	ba	mpmul_check_errors
	nop
mpmul_256:
	.word	0x81b02903	! mpmul 3
	ba	mpmul_check_errors
	nop
mpmul_320:
	.word	0x81b02904	! mpmul 4
	ba	mpmul_check_errors
	nop
mpmul_384:
	.word	0x81b02905	! mpmul 5
	ba	mpmul_check_errors
	nop
mpmul_448:
	.word	0x81b02906	! mpmul 6
	ba	mpmul_check_errors
	nop
mpmul_512:
	.word	0x81b02907	! mpmul 7
	ba	mpmul_check_errors
	nop
mpmul_576:
	.word	0x81b02908	! mpmul 8
	ba	mpmul_check_errors
	nop
mpmul_640:
	.word	0x81b02909	! mpmul 9
	ba	mpmul_check_errors
	nop
mpmul_704:
	.word	0x81b0290a	! mpmul 10
	ba	mpmul_check_errors
	nop
mpmul_768:
	.word	0x81b0290b	! mpmul 11
	ba	mpmul_check_errors
	nop
mpmul_832:
	.word	0x81b0290c	! mpmul 12
	ba	mpmul_check_errors
	nop
mpmul_896:
	.word	0x81b0290d	! mpmul 13
	ba	mpmul_check_errors
	nop
mpmul_960:
	.word	0x81b0290e	! mpmul 14
	ba	mpmul_check_errors
	nop
mpmul_1024:
	.word	0x81b0290f	! mpmul 15
	ba	mpmul_check_errors
	nop
mpmul_1088:
	.word	0x81b02910	! mpmul 16
	ba	mpmul_check_errors
	nop
mpmul_1152:
	.word	0x81b02911	! mpmul 17
	ba	mpmul_check_errors
	nop
mpmul_1216:
	.word	0x81b02912	! mpmul 18
	ba	mpmul_check_errors
	nop
mpmul_1280:
	.word	0x81b02913	! mpmul 19
	ba	mpmul_check_errors
	nop
mpmul_1344:
	.word	0x81b02914	! mpmul 20
	ba	mpmul_check_errors
	nop
mpmul_1408:
	.word	0x81b02915	! mpmul 21
	ba	mpmul_check_errors
	nop
mpmul_1472:
	.word	0x81b02916	! mpmul 22
	ba	mpmul_check_errors
	nop
mpmul_1536:
	.word	0x81b02917	! mpmul 23
	ba	mpmul_check_errors
	nop
mpmul_1600:
	.word	0x81b02918	! mpmul 24
	ba	mpmul_check_errors
	nop
mpmul_1664:
	.word	0x81b02919	! mpmul 25
	ba	mpmul_check_errors
	nop
mpmul_1728:
	.word	0x81b0291a	! mpmul 26
	ba	mpmul_check_errors
	nop
mpmul_1792:
	.word	0x81b0291b	! mpmul 27
	ba	mpmul_check_errors
	nop
mpmul_1856:
	.word	0x81b0291c	! mpmul 28
	ba	mpmul_check_errors
	nop
mpmul_1920:
	.word	0x81b0291d	! mpmul 29
	ba	mpmul_check_errors
	nop
mpmul_1984:
	.word	0x81b0291e	! mpmul 30
	ba	mpmul_check_errors
	nop
mpmul_2048:
	.word	0x81b0291f	! mpmul 31
	ba	mpmul_check_errors
	nop

		.section	".data", #alloc, #write
	.align	8
	.global	mpm_yf_functions_table

mpm_yf_functions_table:
	.type	load_m1_64, #function
	.nword	load_m1_64
	.type	load_m2_64, #function
	.nword	load_m2_64
	.type	store_res_64, #function
	.nword	store_res_64
	.type	mpmul_64, #function
	.nword	mpmul_64

	.type	load_m1_128, #function
	.nword	load_m1_128
	.type	load_m2_128, #function
	.nword	load_m2_128
	.type	store_res_128, #function
	.nword	store_res_128
	.type	mpmul_128, #function
	.nword	mpmul_128

	.type	load_m1_192, #function
	.nword	load_m1_192
	.type	load_m2_192, #function
	.nword	load_m2_192
	.type	store_res_192, #function
	.nword	store_res_192
	.type	mpmul_192, #function
	.nword	mpmul_192

	.type	load_m1_256, #function
	.nword	load_m1_256
	.type	load_m2_256, #function
	.nword	load_m2_256
	.type	store_res_256, #function
	.nword	store_res_256
	.type	mpmul_256, #function
	.nword	mpmul_256

	.type	load_m1_320, #function
	.nword	load_m1_320
	.type	load_m2_320, #function
	.nword	load_m2_320
	.type	store_res_320, #function
	.nword	store_res_320
	.type	mpmul_320, #function
	.nword	mpmul_320

	.type	load_m1_384, #function
	.nword	load_m1_384
	.type	load_m2_384, #function
	.nword	load_m2_384
	.type	store_res_384, #function
	.nword	store_res_384
	.type	mpmul_384, #function
	.nword	mpmul_384

	.type	load_m1_448, #function
	.nword	load_m1_448
	.type	load_m2_448, #function
	.nword	load_m2_448
	.type	store_res_448, #function
	.nword	store_res_448
	.type	mpmul_448, #function
	.nword	mpmul_448

	.type	load_m1_512, #function
	.nword	load_m1_512
	.type	load_m2_512, #function
	.nword	load_m2_512
	.type	store_res_512, #function
	.nword	store_res_512
	.type	mpmul_512, #function
	.nword	mpmul_512

	.type	load_m1_576, #function
	.nword	load_m1_576
	.type	load_m2_576, #function
	.nword	load_m2_576
	.type	store_res_576, #function
	.nword	store_res_576
	.type	mpmul_576, #function
	.nword	mpmul_576

	.type	load_m1_640, #function
	.nword	load_m1_640
	.type	load_m2_640, #function
	.nword	load_m2_640
	.type	store_res_640, #function
	.nword	store_res_640
	.type	mpmul_640, #function
	.nword	mpmul_640

	.type	load_m1_704, #function
	.nword	load_m1_704
	.type	load_m2_704, #function
	.nword	load_m2_704
	.type	store_res_704, #function
	.nword	store_res_704
	.type	mpmul_704, #function
	.nword	mpmul_704

	.type	load_m1_768, #function
	.nword	load_m1_768
	.type	load_m2_768, #function
	.nword	load_m2_768
	.type	store_res_768, #function
	.nword	store_res_768
	.type	mpmul_768, #function
	.nword	mpmul_768

	.type	load_m1_832, #function
	.nword	load_m1_832
	.type	load_m2_832, #function
	.nword	load_m2_832
	.type	store_res_832, #function
	.nword	store_res_832
	.type	mpmul_832, #function
	.nword	mpmul_832

	.type	load_m1_896, #function
	.nword	load_m1_896
	.type	load_m2_896, #function
	.nword	load_m2_896
	.type	store_res_896, #function
	.nword	store_res_896
	.type	mpmul_896, #function
	.nword	mpmul_896

	.type	load_m1_960, #function
	.nword	load_m1_960
	.type	load_m2_960, #function
	.nword	load_m2_960
	.type	store_res_960, #function
	.nword	store_res_960
	.type	mpmul_960, #function
	.nword	mpmul_960

	.type	load_m1_1024, #function
	.nword	load_m1_1024
	.type	load_m2_1024, #function
	.nword	load_m2_1024
	.type	store_res_1024, #function
	.nword	store_res_1024
	.type	mpmul_1024, #function
	.nword	mpmul_1024

	.type	load_m1_1088, #function
	.nword	load_m1_1088
	.type	load_m2_1088, #function
	.nword	load_m2_1088
	.type	store_res_1088, #function
	.nword	store_res_1088
	.type	mpmul_1088, #function
	.nword	mpmul_1088

	.type	load_m1_1152, #function
	.nword	load_m1_1152
	.type	load_m2_1152, #function
	.nword	load_m2_1152
	.type	store_res_1152, #function
	.nword	store_res_1152
	.type	mpmul_1152, #function
	.nword	mpmul_1152

	.type	load_m1_1216, #function
	.nword	load_m1_1216
	.type	load_m2_1216, #function
	.nword	load_m2_1216
	.type	store_res_1216, #function
	.nword	store_res_1216
	.type	mpmul_1216, #function
	.nword	mpmul_1216

	.type	load_m1_1280, #function
	.nword	load_m1_1280
	.type	load_m2_1280, #function
	.nword	load_m2_1280
	.type	store_res_1280, #function
	.nword	store_res_1280
	.type	mpmul_1280, #function
	.nword	mpmul_1280

	.type	load_m1_1344, #function
	.nword	load_m1_1344
	.type	load_m2_1344, #function
	.nword	load_m2_1344
	.type	store_res_1344, #function
	.nword	store_res_1344
	.type	mpmul_1344, #function
	.nword	mpmul_1344

	.type	load_m1_1408, #function
	.nword	load_m1_1408
	.type	load_m2_1408, #function
	.nword	load_m2_1408
	.type	store_res_1408, #function
	.nword	store_res_1408
	.type	mpmul_1408, #function
	.nword	mpmul_1408

	.type	load_m1_1472, #function
	.nword	load_m1_1472
	.type	load_m2_1472, #function
	.nword	load_m2_1472
	.type	store_res_1472, #function
	.nword	store_res_1472
	.type	mpmul_1472, #function
	.nword	mpmul_1472

	.type	load_m1_1536, #function
	.nword	load_m1_1536
	.type	load_m2_1536, #function
	.nword	load_m2_1536
	.type	store_res_1536, #function
	.nword	store_res_1536
	.type	mpmul_1536, #function
	.nword	mpmul_1536

	.type	load_m1_1600, #function
	.nword	load_m1_1600
	.type	load_m2_1600, #function
	.nword	load_m2_1600
	.type	store_res_1600, #function
	.nword	store_res_1600
	.type	mpmul_1600, #function
	.nword	mpmul_1600

	.type	load_m1_1664, #function
	.nword	load_m1_1664
	.type	load_m2_1664, #function
	.nword	load_m2_1664
	.type	store_res_1664, #function
	.nword	store_res_1664
	.type	mpmul_1664, #function
	.nword	mpmul_1664

	.type	load_m1_1728, #function
	.nword	load_m1_1728
	.type	load_m2_1728, #function
	.nword	load_m2_1728
	.type	store_res_1728, #function
	.nword	store_res_1728
	.type	mpmul_1728, #function
	.nword	mpmul_1728

	.type	load_m1_1792, #function
	.nword	load_m1_1792
	.type	load_m2_1792, #function
	.nword	load_m2_1792
	.type	store_res_1792, #function
	.nword	store_res_1792
	.type	mpmul_1792, #function
	.nword	mpmul_1792

	.type	load_m1_1856, #function
	.nword	load_m1_1856
	.type	load_m2_1856, #function
	.nword	load_m2_1856
	.type	store_res_1856, #function
	.nword	store_res_1856
	.type	mpmul_1856, #function
	.nword	mpmul_1856

	.type	load_m1_1920, #function
	.nword	load_m1_1920
	.type	load_m2_1920, #function
	.nword	load_m2_1920
	.type	store_res_1920, #function
	.nword	store_res_1920
	.type	mpmul_1920, #function
	.nword	mpmul_1920

	.type	load_m1_1984, #function
	.nword	load_m1_1984
	.type	load_m2_1984, #function
	.nword	load_m2_1984
	.type	store_res_1984, #function
	.nword	store_res_1984
	.type	mpmul_1984, #function
	.nword	mpmul_1984

	.type	load_m1_2048, #function
	.nword	load_m1_2048
	.type	load_m2_2048, #function
	.nword	load_m2_2048
	.type	store_res_2048, #function
	.nword	store_res_2048
	.type	mpmul_2048, #function
	.nword	mpmul_2048

	.type	mpm_yf_functions_table, #object
	.size	mpm_yf_functions_table, .-mpm_yf_functions_table

#ifdef	__cplusplus
}
#endif

#endif	/* _MPMUL_TABLES_H */
