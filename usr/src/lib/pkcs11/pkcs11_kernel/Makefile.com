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

LIBRARY= pkcs11_kernel.a
VERS= .1

CORE_OBJECTS= \
	kernelGeneral.o 	\
	kernelSlottable.o 	\
	kernelSlotToken.o 	\
	kernelObject.o 		\
	kernelDigest.o	 	\
	kernelSign.o 		\
	kernelVerify.o 		\
	kernelDualCrypt.o 	\
	kernelKeys.o 		\
	kernelRand.o		\
	kernelSession.o		\
	kernelSessionUtil.o	\
	kernelUtil.o		\
	kernelEncrypt.o		\
	kernelDecrypt.o		\
	kernelObjectUtil.o	\
	kernelAttributeUtil.o	\
	kernelEmulate.o

OTHER_OBJECTS = kernelSoftCommon.o
ST_OBJECTS = softDigestUtil.o softMAC.o

OBJECTS= \
	$(CORE_OBJECTS)		\
	$(OTHER_OBJECTS)	\
	$(ST_OBJECTS)

CRYPTODIR=	$(SRC)/common/crypto/
AESDIR=		$(SRC)/common/crypto/aes
ARCFOURDIR=	$(SRC)/common/crypto/arcfour
BLOWFISHDIR=	$(SRC)/common/crypto/blowfish
DESDIR=		$(SRC)/common/crypto/des
ECCDIR=		$(SRC)/common/crypto/ecc
ST_DIR=		$(SRC)/lib/pkcs11/pkcs11_softtoken/common

lint \
pics/kernelAttributeUtil.o := \
	CPPFLAGS += -I$(AESDIR) -I$(BLOWFISHDIR) -I$(ARCFOURDIR) -I$(DESDIR) \
	-I$(ECCDIR) -I$(CRYPTODIR)
pics/kernelKeys.o := \
	CPPFLAGS += -I$(ECCDIR)
pics/kernelSoftCommon.o := \
	CPPFLAGS = -I$(ST_DIR) $(CPPFLAGS.master)

include $(SRC)/lib/Makefile.lib

#	Add FIPS-140 Checksum
POST_PROCESS_SO +=      ; $(FIPS140_CHECKSUM)

#	set signing mode
POST_PROCESS_SO	+=	; $(ELFSIGN_CRYPTO)

SRCDIR=		../common
CORESRCS =  \
	$(CORE_OBJECTS:%.o=$(SRCDIR)/%.c)

LIBS	=	$(DYNLIB)
LDLIBS  +=      -lc -lcryptoutil -lmd

CFLAGS  +=      $(CCVERBOSE)

ROOTLIBDIR=     $(ROOT)/usr/lib/security
ROOTLIBDIR64=   $(ROOT)/usr/lib/security/$(MACH64)

.KEEP_STATE:

all:    $(LIBS)

# we don't need to lint ST_OBJECTS since they are linted elsewhere.
lintcheck := SRCS = $(CORESRCS)
lintother := OSRCS = ../common/kernelSoftCommon.c
lintother := CPPFLAGS = -I$(ST_DIR) $(CPPFLAGS.master)

lintother: $$(OSRCS)
	$(LINT.c) $(LINTCHECKFLAGS) $(OSRCS) $(LDLIBS)

lint: lintcheck lintother

pics/%.o:	$(ST_DIR)/%.c
	$(COMPILE.c) -o $@ $< -I$(ST_DIR)
	$(POST_PROCESS_O)

include $(SRC)/lib/Makefile.targ
