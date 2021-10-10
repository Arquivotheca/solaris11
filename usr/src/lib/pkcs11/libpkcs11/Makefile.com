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

LIBRARY= libpkcs11.a
VERS= .1


CORE_OBJECTS= \
	metaAttrManager.o \
	metaCrypt.o \
	metaDigest.o \
	metaDualCrypt.o \
	metaGeneral.o \
	metaKeys.o \
	metaMechManager.o \
	metaObject.o \
	metaObjectManager.o \
	metaRand.o \
	metaSession.o \
	metaSessionManager.o \
	metaSign.o \
	metaSlotManager.o \
	metaSlotToken.o \
	metaUtil.o \
	metaVerify.o \
	pkcs11General.o 	\
	pkcs11SlotToken.o 	\
	pkcs11Session.o 	\
	pkcs11Object.o 		\
	pkcs11Crypt.o 		\
	pkcs11Digest.o	 	\
	pkcs11Sign.o 		\
	pkcs11Verify.o 		\
	pkcs11DualCrypt.o 	\
	pkcs11Keys.o 		\
	pkcs11Rand.o		\
	pkcs11Slottable.o	\
	pkcs11Conf.o		\
	pkcs11Sessionlist.o	\
	pkcs11SUNWExtensions.o

FIPS_OBJECTS += fips_checksum.o

OBJECTS	= $(CORE_OBJECTS) $(FIPS_OBJECTS)

include ../../../Makefile.lib

SRCDIR=		../common
INCDIR=		../../include
COM_DIR=	$(SRC)/common/crypto
FIPS_DIR=	$(COM_DIR)/fips

SRCS	= $(CORE_OBJECTS:%.o=$(SRCDIR)/%.c)
SRCS	+= $(FIPS_OBJECTS:%.o=$(FIPS_DIR)/%.c)

#
#	Add FIPS-140 Checksum
#
POST_PROCESS_SO +=      ; $(FIPS140_CHECKSUM)

FIPS_SRC = fip_checksum.c


LIBS =		$(DYNLIB) $(LINTLIB)
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)
LDLIBS +=	-lcryptoutil -lc -lscf -lmd -lelf

CFLAGS	+=	$(CCVERBOSE)
CPPFLAGS +=	-I$(INCDIR) -I$(SRCDIR) -I$(COM_DIR) -D_REENTRANT

LINTFLAGS64 += -errchk=longptr64

.KEEP_STATE:

all:	stub $(LIBS)

lint: lintcheck

pics/%.o:	$(FIPS_DIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

include $(SRC)/lib/Makefile.targ
