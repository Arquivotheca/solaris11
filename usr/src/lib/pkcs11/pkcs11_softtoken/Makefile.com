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
# Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY = pkcs11_softtoken.a
VERS= .1

LCL_OBJECTS = \
	softGeneral.o 		\
	softSlotToken.o 	\
	softSession.o 		\
	softObject.o 		\
	softDigest.o	 	\
	softSign.o 		\
	softVerify.o 		\
	softDualCrypt.o 	\
	softKeys.o 		\
	softRand.o		\
	softSessionUtil.o	\
	softDigestUtil.o	\
	softAttributeUtil.o	\
	softObjectUtil.o	\
	softDESCrypt.o		\
	softEncrypt.o		\
	softDecrypt.o		\
	softEncryptUtil.o	\
	softDecryptUtil.o	\
	softSignUtil.o		\
	softVerifyUtil.o	\
	softMAC.o		\
	softRSA.o		\
	softKeysUtil.o		\
	softARCFourCrypt.o	\
	softDSA.o		\
	softDH.o		\
	softAESCrypt.o		\
	softKeystore.o		\
	softKeystoreUtil.o	\
	softSSL.o		\
	softASN1.o		\
	softBlowfishCrypt.o	\
	softEC.o		\
	softFipsPost.o		\
	softFipsPostUtil.o

RNG_DIR =	$(SRC)/common/crypto/rng
RNG_OBJECTS =	fips_random.o

FIPS_DIR =	$(SRC)/common/crypto/fips
FIPS_OBJECTS =	fips_aes_util.o fips_des_util.o \
		fips_sha1_util.o fips_sha2_util.o \
		fips_dsa_util.o fips_rsa_util.o \
		fips_ecc_util.o fips_random_util.o \
		fips_test_vectors.o

LDAPSRC =	$(SRC)/lib/libldap5
BER_INC =	$(LDAPSRC)/include/ldap
BER_DIR =	$(LDAPSRC)/sources/ldap/ber
BER_OBJECTS =	bprint.o decode.o encode.o io.o
BER_FLAGS =	-D_SOLARIS_SDK -I$(BER_INC)

OBJECTS = \
	$(LCL_OBJECTS)		\
	$(RNG_OBJECTS)		\
	$(FIPS_OBJECTS)		\
	$(BER_OBJECTS)

include $(SRC)/lib/Makefile.lib

#	Add FIPS-140 Checksum
POST_PROCESS_SO +=	; $(FIPS140_CHECKSUM)

# set signing mode
POST_PROCESS_SO +=	; $(ELFSIGN_CRYPTO)

SRCDIR= $(SRC)/lib/pkcs11/pkcs11_softtoken/common

SRCS = \
	$(LCL_OBJECTS:%.o=$(SRCDIR)/%.c) \
	$(RNG_OBJECTS:%.o=$(RNG_DIR)/%.c) \
	$(FIPS_OBJECTS:%.o=$(FIPS_DIR)/%.c)

# libelfsign needs a static pkcs11_softtoken
LIBS =		$(DYNLIB)
LDLIBS +=	-lc -lmd -lcryptoutil -lsoftcrypto -lgen

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-I$(SRCDIR) -I$(SRC)/common -I$(SRC)/common/crypto \
		-I$(SRC)/uts/common -I$(SRC)/common/bignum \
		-DMP_API_COMPATIBLE -DNSS_ECC_MORE_THAN_SUITE_B \
		-D_POSIX_PTHREAD_SEMANTICS

LINTFLAGS64 +=	-errchk=longptr64

ROOTLIBDIR =	$(ROOT)/usr/lib/security
ROOTLIBDIR64 =	$(ROOT)/usr/lib/security/$(MACH64)

.KEEP_STATE:

all:	stub $(LIBS)

lint:	lintcheck

pics/%.o:	$(BER_DIR)/%.c
	$(COMPILE.c) $(BER_FLAGS) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o:	$(RNG_DIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o:	$(FIPS_DIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

include $(SRC)/lib/Makefile.targ
