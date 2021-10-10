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
# Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
#

### Core definitions for each crypto component included in this library

# AES
AES_DIR =		$(SRC)/common/crypto/aes
AES_COMMON_OBJS =	aes_impl.o aes_modes.o
AES_COMMON_SRC =	$(AES_COMMON_OBJS:%.o=$(AES_DIR)/%.c)
AES_FLAGS =		-I$(AES_DIR)

# Blowfish
BLOWFISH_DIR =		$(SRC)/common/crypto/blowfish
BLOWFISH_COMMON_OBJS =	blowfish_impl.o
BLOWFISH_COMMON_SRC =	$(BLOWFISH_COMMON_OBJS:%.o=$(BLOWFISH_DIR)/%.c)
BLOWFISH_FLAGS =	-I$(BLOWFISH_DIR)

# ARCFour
ARCFOUR_DIR =		$(SRC)/common/crypto/arcfour
ARCFOUR_COMMON_OBJS =	arcfour_crypt.o
ARCFOUR_COMMON_SRC =	$(ARCFOUR_COMMON_OBJS:%.o=$(ARCFOUR_DIR)/%.c)
ARCFOUR_FLAGS =		-I$(ARCFOUR_DIR)

# DES
DES_DIR =		$(SRC)/common/crypto/des
DES_COMMON_OBJS =	des_impl.o des_ks.o des_modes.o
DES_COMMON_SRC =	$(DES_COMMON_OBJS:%.o=$(DES_DIR)/%.c)
DES_FLAGS =		-I$(DES_DIR)

# BIGNUM -- needed by DH, DSA, RSA
BIGNUM_DIR =		$(SRC)/common/bignum
BIGNUM_COMMON_OBJS =	bignumimpl.o
BIGNUM_COMMON_SRC =	$(BIGNUM_COMMON_OBJS:%.o=$(BIGNUM_DIR)/%.c)
BIGNUM_FLAGS =		-I$(BIGNUM_DIR)

# Modes
MODES_DIR =		$(SRC)/common/crypto/modes
MODES_COMMON_OBJS =	modes.o ecb.o cbc.o ccm.o cfb.o ctr.o gcm.o prov_lib.o
MODES_COMMON_SRC =	$(MODES_COMMON_OBJS:%.o=$(MODES_DIR)/%.c)
MODES_FLAGS =		-I$(MODES_DIR)

# DH
DH_DIR =		$(SRC)/common/crypto/dh
DH_COMMON_OBJS =	dh_impl.o
DH_COMMON_SRC =		$(DH_COMMON_OBJS:%.o=$(DH_DIR)/%.c)
DH_FLAGS =		$(BIGNUM_FLAGS) -I$(DH_DIR)

# DSA
DSA_DIR =		$(SRC)/common/crypto/dsa
DSA_COMMON_OBJS =	dsa_impl.o
DSA_COMMON_SRC =	$(DSA_COMMON_OBJS:%.o=$(DSA_DIR)/%.c)
DSA_FLAGS =		$(BIGNUM_FLAGS) -I$(DSA_DIR)

# ECC
ECC_DIR =		$(SRC)/common/crypto/ecc
ECC_COMMON_OBJS =	ec.o ec2_163.o ec2_mont.o ecdecode.o ecl_mult.o \
			ecp_384.o ecp_jac.o ec2_193.o ecl.o ecp_192.o \
			ecp_521.o ecp_jm.o ec2_233.o ecl_curve.o ecp_224.o \
			ecp_aff.o ecp_mont.o ec2_aff.o ec_naf.o ecl_gf.o \
			ecp_256.o oid.o secitem.o ec2_test.o ecp_test.o
ECC_COMMON_SRC =	$(ECC_COMMON_OBJS:%.o=$(ECC_DIR)/%.c)
ECC_FLAGS =		$(MPI_FLAGS) -I$(ECC_DIR) -DNSS_ECC_MORE_THAN_SUITE_B

# MPI -- needed by ECC
MPI_DIR =		$(SRC)/common/mpi
MPI_COMMON_OBJS =	mp_gf2m.o mpi.o mplogic.o mpmontg.o mpprime.o
MPI_COMMON_SRC =	$(MPI_COMMON_OBJS:%.o=$(MPI_DIR)/%.c)
MPI_FLAGS =		-I$(MPI_DIR) -DMP_API_COMPATIBLE

# RSA
RSA_DIR =		$(SRC)/common/crypto/rsa
RSA_COMMON_OBJS =	rsa_impl.o
RSA_COMMON_SRC =	$(RSA_COMMON_OBJS:%.o=$(RSA_DIR)/%.c)
RSA_FLAGS =		$(BIGNUM_FLAGS) -I$(RSA_DIR)

# PADDING -- needed by RSA
PAD_DIR =		$(SRC)/common/crypto/padding
PAD_COMMON_OBJS =	pkcs1.o pkcs7.o
PAD_COMMON_SRC =	$(PAD_COMMON_OBJS:%.o=$(PAD_DIR)/%.c)
PAD_FLAGS =		-I$(PAD_DIR)

# libsoftcrypto common (ucrypto api)
UCRYPTO_COMMON_OBJS =	crypt.o mechstr.o
UCRYPTO_COMMON_SRC =	$(UCRYPTO_COMMON_OBJS:%.o=$(SRCDIR)/%.c)
UCRYPTO_FLAGS =		-xO5 -xbuiltin=%all -I$(SRCDIR)

### All the objects

# Bring in platform-specific objects if any
AES_OBJS =		$(AES_COMMON_OBJS)	$(AES_PSM_OBJS)
ARCFOUR_OBJS =		$(ARCFOUR_COMMON_OBJS)	$(ARCFOUR_PSM_OBJS)
BLOWFISH_OBJS =		$(BLOWFISH_COMMON_OBJS)	$(BLOWFISH_PSM_OBJS)
DES_OBJS =		$(DES_COMMON_OBJS)	$(DES_PSM_OBJS)
BIGNUM_OBJS =		$(BIGNUM_COMMON_OBJS)	$(BIGNUM_PSM_OBJS)
MODES_OBJS =		$(MODES_COMMON_OBJS)	$(MODES_PSM_OBJS)
DH_OBJS =		$(DH_COMMON_OBJS)	$(DH_PSM_OBJS)
DSA_OBJS =		$(DSA_COMMON_OBJS)	$(DSA_PSM_OBJS)
ECC_OBJS =		$(ECC_COMMON_OBJS)	$(ECC_PSM_OBJS)
MPI_OBJS =		$(MPI_COMMON_OBJS)	$(MPI_PSM_OBJS)
RSA_OBJS =		$(RSA_COMMON_OBJS)	$(RSA_PSM_OBJS)
PAD_OBJS =		$(PAD_COMMON_OBJS)	$(PAD_PSM_OBJS)
UCRYPTO_OBJS =		$(UCRYPTO_COMMON_OBJS)

OBJECTS =		$(AES_OBJS) $(ARCFOUR_OBJS) $(BIGNUM_OBJS) \
			$(BLOWFISH_OBJS) $(DES_OBJS) $(MODES_OBJS) \
			$(DH_OBJS) $(DSA_OBJS) $(ECC_OBJS) $(MPI_OBJS) \
			$(RSA_OBJS) $(PAD_OBJS) $(UCRYPTO_OBJS)

### All the sources

# Bring in platform-specific objects if any
AES_SRC =		$(AES_COMMON_SRC)	$(AES_PSM_SRC)
ARCFOUR_SRC =		$(ARCFOUR_COMMON_SRC)	$(ARCFOUR_PSM_SRC)
BLOWFISH_SRC =		$(BLOWFISH_COMMON_SRC)	$(BLOWFISH_PSM_SRC)
DES_SRC =		$(DES_COMMON_SRC)	$(DES_PSM_SRC)
BIGNUM_SRC =		$(BIGNUM_COMMON_SRC)	$(BIGNUM_PSM_SRC)
MODES_SRC =		$(MODES_COMMON_SRC)	$(MODES_PSM_SRC)
DH_SRC =		$(DH_COMMON_SRC)	$(DH_PSM_SRC)
DSA_SRC =		$(DSA_COMMON_SRC)	$(DSA_PSM_SRC)
ECC_SRC =		$(ECC_COMMON_SRC)	$(ECC_PSM_SRC)
MPI_SRC =		$(MPI_COMMON_SRC)	$(MPI_PSM_SRC)
RSA_SRC =		$(RSA_COMMON_SRC)	$(RSA_PSM_SRC)
PAD_SRC =		$(PAD_COMMON_SRC)	$(PAD_PSM_SRC)
UCRYPTO_SRC =		$(UCRYPTO_COMMON_SRC)

### Make it so that consumer Makefiles just need to include this file.

include  $(SRC)/lib/Makefile.lib

SRCS =			$(AES_SRC) $(ARCFOUR_SRC) $(BIGNUM_SRC) \
			$(BLOWFISH_SRC) $(DES_SRC) $(MODES_SRC) \
			$(DH_SRC) $(DSA_SRC) $(ECC_SRC) $(MPI_SRC) \
			$(RSA_SRC) $(PAD_SRC) $(UCRYPTO_SRC)

### All the commonly shared flags

# ECC and MPI currently do not lint, do not include them in lint targets
EXTRA_LINT_FLAGS =	$(AES_FLAGS) $(BLOWFISH_FLAGS) $(ARCFOUR_FLAGS) \
			$(DES_FLAGS) $(BIGNUM_FLAGS) $(MODES_FLAGS) \
			$(DH_FLAGS) $(DSA_FLAGS) $(RSA_FLAGS) $(PAD_FLAGS)
LINTABLE =		$(AES_SRC) $(ARCFOUR_SRC) $(BIGNUM_SRC) \
			$(BLOWFISH_SRC) $(DES_SRC) $(MODES_SRC) \
			$(DH_SRC) $(DSA_SRC) $(RSA_SRC) $(PAD_SRC) \
			$(UCRYPTO_SRC)
$(LINTLIB) :=		SRCS = $(LINTABLE)

# Common source and header directories and flags shared by entire module
SRCDIR =		$(SRC)/lib/libsoftcrypto/common
CRYPTODIR =		$(SRC)/common/crypto
UTSDIR =		$(SRC)/uts/common/

# Clear -xspace/$SPACEFLAG due to SS12 compiler loop-unrolling bug that
# results in loss of performance on 32-bit x86.  Overriding the compiler
# option here clears it for x86 platform-specific capabilities also.
i386_SPACEFLAG =

CFLAGS +=		$(CCVERBOSE)
CPPFLAGS +=		-I$(SRCDIR) -I$(BIGNUM_DIR) -I$(CRYPTODIR) -I$(UTSDIR) \
			-D_POSIX_PTHREAD_SEMANTICS

ASFLAGS +=		$(AS_PICFLAGS) -P
AS_CPPFLAGS +=		-D__STDC__ -D_ASM -DPIC -D_REENTRANT -D$(MACH)

LINTFLAGS +=		$(EXTRA_LINT_FLAGS)
LINTFLAGS64 +=		$(EXTRA_LINT_FLAGS)

LDLIBS +=		-lmd -lcryptoutil -lc

#
#	Add FIPS-140 Checksum
#
POST_PROCESS_SO +=	; $(FIPS140_CHECKSUM)
