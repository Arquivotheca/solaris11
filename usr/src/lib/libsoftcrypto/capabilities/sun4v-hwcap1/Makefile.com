#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

PLATFORM =	sun4v

AES_PSM_OBJS =		aes_sun4v_modes.o yf_aes.o
DES_PSM_OBJS =		des_sun4v_modes.o yf_des.o
BIGNUM_PSM_OBJS =	montmul_vt.o montmul_vt_asm.o mpmul_vt_asm.o mpmul_vt.o
MODES_PSM_OBJS =	ctr.o

include		../../Makefile.com

# Redefine the objects required for this capabilities group.
OBJECTS =	$(AES_OBJS) $(BIGNUM_OBJS) $(DES_OBJS) $(RSA_OBJS) \
	 $(MODES_PSM_OBJS)

CFLAGS +=	-xO5 -xbuiltin=%all -dalign
CPPFLAGS +=	-D$(PLATFORM)
AES_FLAGS +=	-DHWCAP_AES
DES_FLAGS +=	-DHWCAP_DES
MODES_FLAGS +=	-DHWCAP_AES
BIGNUM_FLAGS +=	-DUMUL64 -DYF_MONTMUL -DYF_MPMUL -DYF_MODEXP
CLEANFILES +=	generate_montmul_offsets montmul_offsets.h
