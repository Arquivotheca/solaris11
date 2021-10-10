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

#ifndef _MONTMUL_TABLES_H
#define	_MONTMUL_TABLES_H

#ifdef	__cplusplus
extern "C" {
#endif
	.section	".text", #alloc, #execinstr
	.align	8

montmul_64:
	.word	0x81b02920	! montmul 0
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_128:
	.word	0x81b02921	! montmul 1
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_192:
	.word	0x81b02922	! montmul 2
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_256:
	.word	0x81b02923	! montmul 3
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_320:
	.word	0x81b02924	! montmul 4
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_384:
	.word	0x81b02925	! montmul 5
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_448:
	.word	0x81b02926	! montmul 6
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_512:
	.word	0x81b02927	! montmul 7
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_576:
	.word	0x81b02928	! montmul 8
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_640:
	.word	0x81b02929	! montmul 9
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_704:
	.word	0x81b0292a	! montmul 10
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_768:
	.word	0x81b0292b	! montmul 11
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_832:
	.word	0x81b0292c	! montmul 12
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_896:
	.word	0x81b0292d	! montmul 13
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_960:
	.word	0x81b0292e	! montmul 14
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1024:
	.word	0x81b0292f	! montmul 15
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1088:
	.word	0x81b02930	! montmul 16
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1152:
	.word	0x81b02931	! montmul 17
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1216:
	.word	0x81b02932	! montmul 18
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1280:
	.word	0x81b02933	! montmul 19
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1344:
	.word	0x81b02934	! montmul 20
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1408:
	.word	0x81b02935	! montmul 21
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1472:
	.word	0x81b02936	! montmul 22
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1536:
	.word	0x81b02937	! montmul 23
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1600:
	.word	0x81b02938	! montmul 24
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1664:
	.word	0x81b02939	! montmul 25
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1728:
	.word	0x81b0293a	! montmul 26
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1792:
	.word	0x81b0293b	! montmul 27
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1856:
	.word	0x81b0293c	! montmul 28
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1920:
	.word	0x81b0293d	! montmul 29
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_1984:
	.word	0x81b0293e	! montmul 30
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montmul_2048:
	.word	0x81b0293f	! montmul 31
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_64:
	.word	0x81b02940	! montsqr 0
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_128:
	.word	0x81b02941	! montsqr 1
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_192:
	.word	0x81b02942	! montsqr 2
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_256:
	.word	0x81b02943	! montsqr 3
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_320:
	.word	0x81b02944	! montsqr 4
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_384:
	.word	0x81b02945	! montsqr 5
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_448:
	.word	0x81b02946	! montsqr 6
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_512:
	.word	0x81b02947	! montsqr 7
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_576:
	.word	0x81b02948	! montsqr 8
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_640:
	.word	0x81b02949	! montsqr 9
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_704:
	.word	0x81b0294a	! montsqr 10
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_768:
	.word	0x81b0294b	! montsqr 11
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_832:
	.word	0x81b0294c	! montsqr 12
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_896:
	.word	0x81b0294d	! montsqr 13
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_960:
	.word	0x81b0294e	! montsqr 14
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1024:
	.word	0x81b0294f	! montsqr 15
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1088:
	.word	0x81b02950	! montsqr 16
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1152:
	.word	0x81b02951	! montsqr 17
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1216:
	.word	0x81b02952	! montsqr 18
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1280:
	.word	0x81b02953	! montsqr 19
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1344:
	.word	0x81b02954	! montsqr 20
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1408:
	.word	0x81b02955	! montsqr 21
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1472:
	.word	0x81b02956	! montsqr 22
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1536:
	.word	0x81b02957	! montsqr 23
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1600:
	.word	0x81b02958	! montsqr 24
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1664:
	.word	0x81b02959	! montsqr 25
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1728:
	.word	0x81b0295a	! montsqr 26
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1792:
	.word	0x81b0295b	! montsqr 27
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1856:
	.word	0x81b0295c	! montsqr 28
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1920:
	.word	0x81b0295d	! montsqr 29
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_1984:
	.word	0x81b0295e	! montsqr 30
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5
montsqr_2048:
	.word	0x81b0295f	! montsqr 31
	ba	check_errors
	sub	%g5, PTR_SIZE, %g5


		.section	".data", #alloc, #write
	.align	8
	.global	mm_yf_functions_table

mm_yf_functions_table:
	.type	load_a_192, #function	!64
	.nword	load_a_192
	.type	store_a_192, #function	!64
	.nword	store_a_192
	.type	load_n_192, #function	!64
	.nword	load_n_192
	.type	load_b_192, #function	!64
	.nword	load_b_192
	.type	montmul_64, #function
	.nword	montmul_64
	.type	montsqr_64, #function
	.nword	montsqr_64
	.type	load_a_192, #function	!128
	.nword	load_a_192
	.type	store_a_192, #function	!128
	.nword	store_a_192
	.type	load_n_192, #function	!128
	.nword	load_n_192
	.type	load_b_192, #function	!128
	.nword	load_b_192
	.type	montmul_128, #function
	.nword	montmul_128
	.type	montsqr_128, #function
	.nword	montsqr_128
	.type	load_a_192, #function	!192
	.nword	load_a_192
	.type	store_a_192, #function	!192
	.nword	store_a_192
	.type	load_n_192, #function	!192
	.nword	load_n_192
	.type	load_b_192, #function	!192
	.nword	load_b_192
	.type	montmul_192, #function
	.nword	montmul_192
	.type	montsqr_192, #function
	.nword	montsqr_192
	.type	load_a_256, #function	!256
	.nword	load_a_256
	.type	store_a_256, #function	!256
	.nword	store_a_256
	.type	load_n_256, #function	!256
	.nword	load_n_256
	.type	load_b_256, #function	!256
	.nword	load_b_256
	.type	montmul_256, #function
	.nword	montmul_256
	.type	montsqr_256, #function
	.nword	montsqr_256
	.type	load_a_384, #function	!320
	.nword	load_a_384
	.type	store_a_384, #function	!320
	.nword	store_a_384
	.type	load_n_384, #function	!320
	.nword	load_n_384
	.type	load_b_384, #function	!320
	.nword	load_b_384
	.type	montmul_320, #function
	.nword	montmul_320
	.type	montsqr_320, #function
	.nword	montsqr_320
	.type	load_a_384, #function	!384
	.nword	load_a_384
	.type	store_a_384, #function	!384
	.nword	store_a_384
	.type	load_n_384, #function	!384
	.nword	load_n_384
	.type	load_b_384, #function	!384
	.nword	load_b_384
	.type	montmul_384, #function
	.nword	montmul_384
	.type	montsqr_384, #function
	.nword	montsqr_384
	.type	load_a_512, #function	!448
	.nword	load_a_512
	.type	store_a_512, #function	!448
	.nword	store_a_512
	.type	load_n_512, #function	!448
	.nword	load_n_512
	.type	load_b_512, #function	!448
	.nword	load_b_512
	.type	montmul_448, #function
	.nword	montmul_448
	.type	montsqr_448, #function
	.nword	montsqr_448
	.type	load_a_512, #function	!512
	.nword	load_a_512
	.type	store_a_512, #function	!512
	.nword	store_a_512
	.type	load_n_512, #function	!512
	.nword	load_n_512
	.type	load_b_512, #function	!512
	.nword	load_b_512
	.type	montmul_512, #function
	.nword	montmul_512
	.type	montsqr_512, #function
	.nword	montsqr_512
	.type	load_a_576, #function	!576
	.nword	load_a_576
	.type	store_a_576, #function	!576
	.nword	store_a_576
	.type	load_n_576, #function	!576
	.nword	load_n_576
	.type	load_b_576, #function	!576
	.nword	load_b_576
	.type	montmul_576, #function
	.nword	montmul_576
	.type	montsqr_576, #function
	.nword	montsqr_576
	.type	load_a_1024, #function	!640
	.nword	load_a_1024
	.type	store_a_1024, #function	!640
	.nword	store_a_1024
	.type	load_n_1024, #function	!640
	.nword	load_n_1024
	.type	load_b_1024, #function	!640
	.nword	load_b_1024
	.type	montmul_640, #function
	.nword	montmul_640
	.type	montsqr_640, #function
	.nword	montsqr_640
	.type	load_a_1024, #function	!704
	.nword	load_a_1024
	.type	store_a_1024, #function	!704
	.nword	store_a_1024
	.type	load_n_1024, #function	!704
	.nword	load_n_1024
	.type	load_b_1024, #function	!704
	.nword	load_b_1024
	.type	montmul_704, #function
	.nword	montmul_704
	.type	montsqr_704, #function
	.nword	montsqr_704
	.type	load_a_1024, #function	!768
	.nword	load_a_1024
	.type	store_a_1024, #function	!768
	.nword	store_a_1024
	.type	load_n_1024, #function	!768
	.nword	load_n_1024
	.type	load_b_1024, #function	!768
	.nword	load_b_1024
	.type	montmul_768, #function
	.nword	montmul_768
	.type	montsqr_768, #function
	.nword	montsqr_768
	.type	load_a_1024, #function	!832
	.nword	load_a_1024
	.type	store_a_1024, #function	!832
	.nword	store_a_1024
	.type	load_n_1024, #function	!832
	.nword	load_n_1024
	.type	load_b_1024, #function	!832
	.nword	load_b_1024
	.type	montmul_832, #function
	.nword	montmul_832
	.type	montsqr_832, #function
	.nword	montsqr_832
	.type	load_a_1024, #function	!896
	.nword	load_a_1024
	.type	store_a_1024, #function	!896
	.nword	store_a_1024
	.type	load_n_1024, #function	!896
	.nword	load_n_1024
	.type	load_b_1024, #function	!896
	.nword	load_b_1024
	.type	montmul_896, #function
	.nword	montmul_896
	.type	montsqr_896, #function
	.nword	montsqr_896
	.type	load_a_1024, #function	!960
	.nword	load_a_1024
	.type	store_a_1024, #function	!960
	.nword	store_a_1024
	.type	load_n_1024, #function	!960
	.nword	load_n_1024
	.type	load_b_1024, #function	!960
	.nword	load_b_1024
	.type	montmul_960, #function
	.nword	montmul_960
	.type	montsqr_960, #function
	.nword	montsqr_960
	.type	load_a_1024, #function	!1024
	.nword	load_a_1024
	.type	store_a_1024, #function	!1024
	.nword	store_a_1024
	.type	load_n_1024, #function	!1024
	.nword	load_n_1024
	.type	load_b_1024, #function	!1024
	.nword	load_b_1024
	.type	montmul_1024, #function
	.nword	montmul_1024
	.type	montsqr_1024, #function
	.nword	montsqr_1024
	.type	load_a_1536, #function	!1088
	.nword	load_a_1536
	.type	store_a_1536, #function	!1088
	.nword	store_a_1536
	.type	load_n_1536, #function	!1088
	.nword	load_n_1536
	.type	load_b_1536, #function	!1088
	.nword	load_b_1536
	.type	montmul_1088, #function
	.nword	montmul_1088
	.type	montsqr_1088, #function
	.nword	montsqr_1088
	.type	load_a_1536, #function	!1152
	.nword	load_a_1536
	.type	store_a_1536, #function	!1152
	.nword	store_a_1536
	.type	load_n_1536, #function	!1152
	.nword	load_n_1536
	.type	load_b_1536, #function	!1152
	.nword	load_b_1536
	.type	montmul_1152, #function
	.nword	montmul_1152
	.type	montsqr_1152, #function
	.nword	montsqr_1152
	.type	load_a_1536, #function	!1216
	.nword	load_a_1536
	.type	store_a_1536, #function	!1216
	.nword	store_a_1536
	.type	load_n_1536, #function	!1216
	.nword	load_n_1536
	.type	load_b_1536, #function	!1216
	.nword	load_b_1536
	.type	montmul_1216, #function
	.nword	montmul_1216
	.type	montsqr_1216, #function
	.nword	montsqr_1216
	.type	load_a_1536, #function	!1280
	.nword	load_a_1536
	.type	store_a_1536, #function	!1280
	.nword	store_a_1536
	.type	load_n_1536, #function	!1280
	.nword	load_n_1536
	.type	load_b_1536, #function	!1280
	.nword	load_b_1536
	.type	montmul_1280, #function
	.nword	montmul_1280
	.type	montsqr_1280, #function
	.nword	montsqr_1280
	.type	load_a_1536, #function	!1344
	.nword	load_a_1536
	.type	store_a_1536, #function	!1344
	.nword	store_a_1536
	.type	load_n_1536, #function	!1344
	.nword	load_n_1536
	.type	load_b_1536, #function	!1344
	.nword	load_b_1536
	.type	montmul_1344, #function
	.nword	montmul_1344
	.type	montsqr_1344, #function
	.nword	montsqr_1344
	.type	load_a_1536, #function	!1408
	.nword	load_a_1536
	.type	store_a_1536, #function	!1408
	.nword	store_a_1536
	.type	load_n_1536, #function	!1408
	.nword	load_n_1536
	.type	load_b_1536, #function	!1408
	.nword	load_b_1536
	.type	montmul_1408, #function
	.nword	montmul_1408
	.type	montsqr_1408, #function
	.nword	montsqr_1408
	.type	load_a_1536, #function	!1472
	.nword	load_a_1536
	.type	store_a_1536, #function	!1472
	.nword	store_a_1536
	.type	load_n_1536, #function	!1472
	.nword	load_n_1536
	.type	load_b_1536, #function	!1472
	.nword	load_b_1536
	.type	montmul_1472, #function
	.nword	montmul_1472
	.type	montsqr_1472, #function
	.nword	montsqr_1472
	.type	load_a_1536, #function	!1536
	.nword	load_a_1536
	.type	store_a_1536, #function	!1536
	.nword	store_a_1536
	.type	load_n_1536, #function	!1536
	.nword	load_n_1536
	.type	load_b_1536, #function	!1536
	.nword	load_b_1536
	.type	montmul_1536, #function
	.nword	montmul_1536
	.type	montsqr_1536, #function
	.nword	montsqr_1536
	.type	load_a_2048, #function	!1600
	.nword	load_a_2048
	.type	store_a_2048, #function	!1600
	.nword	store_a_2048
	.type	load_n_2048, #function	!1600
	.nword	load_n_2048
	.type	load_b_2048, #function	!1600
	.nword	load_b_2048
	.type	montmul_1600, #function
	.nword	montmul_1600
	.type	montsqr_1600, #function
	.nword	montsqr_1600
	.type	load_a_2048, #function	!1664
	.nword	load_a_2048
	.type	store_a_2048, #function	!1664
	.nword	store_a_2048
	.type	load_n_2048, #function	!1664
	.nword	load_n_2048
	.type	load_b_2048, #function	!1664
	.nword	load_b_2048
	.type	montmul_1664, #function
	.nword	montmul_1664
	.type	montsqr_1664, #function
	.nword	montsqr_1664
	.type	load_a_2048, #function	!1728
	.nword	load_a_2048
	.type	store_a_2048, #function	!1728
	.nword	store_a_2048
	.type	load_n_2048, #function	!1728
	.nword	load_n_2048
	.type	load_b_2048, #function	!1728
	.nword	load_b_2048
	.type	montmul_1728, #function
	.nword	montmul_1728
	.type	montsqr_1728, #function
	.nword	montsqr_1728
	.type	load_a_2048, #function	!1792
	.nword	load_a_2048
	.type	store_a_2048, #function	!1792
	.nword	store_a_2048
	.type	load_n_2048, #function	!1792
	.nword	load_n_2048
	.type	load_b_2048, #function	!1792
	.nword	load_b_2048
	.type	montmul_1792, #function
	.nword	montmul_1792
	.type	montsqr_1792, #function
	.nword	montsqr_1792
	.type	load_a_2048, #function	!1856
	.nword	load_a_2048
	.type	store_a_2048, #function	!1856
	.nword	store_a_2048
	.type	load_n_2048, #function	!1856
	.nword	load_n_2048
	.type	load_b_2048, #function	!1856
	.nword	load_b_2048
	.type	montmul_1856, #function
	.nword	montmul_1856
	.type	montsqr_1856, #function
	.nword	montsqr_1856
	.type	load_a_2048, #function	!1920
	.nword	load_a_2048
	.type	store_a_2048, #function	!1920
	.nword	store_a_2048
	.type	load_n_2048, #function	!1920
	.nword	load_n_2048
	.type	load_b_2048, #function	!1920
	.nword	load_b_2048
	.type	montmul_1920, #function
	.nword	montmul_1920
	.type	montsqr_1920, #function
	.nword	montsqr_1920
	.type	load_a_2048, #function	!1984
	.nword	load_a_2048
	.type	store_a_2048, #function	!1984
	.nword	store_a_2048
	.type	load_n_2048, #function	!1984
	.nword	load_n_2048
	.type	load_b_2048, #function	!1984
	.nword	load_b_2048
	.type	montmul_1984, #function
	.nword	montmul_1984
	.type	montsqr_1984, #function
	.nword	montsqr_1984
	.type	load_a_2048, #function	!2048
	.nword	load_a_2048
	.type	store_a_2048, #function	!2048
	.nword	store_a_2048
	.type	load_n_2048, #function	!2048
	.nword	load_n_2048
	.type	load_b_2048, #function	!2048
	.nword	load_b_2048
	.type	montmul_2048, #function
	.nword	montmul_2048
	.type	montsqr_2048, #function
	.nword	montsqr_2048
	.type	mm_yf_functions_table, #object
	.size	mm_yf_functions_table, .-mm_yf_functions_table

#ifdef	__cplusplus
}
#endif

#endif	/* _MONTMUL_TABLES_H */
