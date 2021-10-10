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
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#

PLATFORM =	sun4u

AES_PSM_OBJS =		aes_crypt_asm.o
ARCFOUR_PSM_OBJS =	arcfour_crypt_asm.o
DES_PSM_OBJS =		des_crypt_asm.o
BIGNUM_PSM_OBJS =	mont_mulf_asm.o

include		../../Makefile.com

# Redefine the objects required for this capabilities group.
OBJECTS =	$(AES_OBJS) $(ARCFOUR_OBJS) $(DES_OBJS) $(BIGNUM_OBJS) \
		$(MODES_OBJS)

CPPFLAGS +=	-D$(PLATFORM)

# Do not remove big PIC flags because bignum needs it for the hand-coded
# asm derived from mont_mulf.c, specifically mont_mulf_{v8plus,v9}.s.
CFLAGS +=	$(C_BIGPICFLAGS)
ASFLAGS +=	$(AS_BIGPICFLAGS)

BIGNUM_FLAGS +=	-DUSE_FLOATING_POINT -DNO_BIG_ONE -DNO_BIG_TWO
