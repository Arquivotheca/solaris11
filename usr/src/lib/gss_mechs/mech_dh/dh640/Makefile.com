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
# Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
#

#
# This make file will build dh640.so.1. This shared object
# contains the functionality needed to initialize the  Diffie-Hellman GSS-API
# mechanism with 640 bit key length. This library, in turn, loads the 
# generic Diffie-Hellman GSS-API backend, dhmech.so
#

LIBRARY= dh640-0.a
VERS = .1

DH640=	dh640.o dh_common.o generic_key.o

MECH =	context.o context_establish.o cred.o crypto.o dhmech.o \
	MICwrap.o name.o oid.o seq.o token.o support.o validate.o

DERIVED_OBJS = xdr_token.o

CRYPTO = md5.o

OBJECTS= $(DH640) $(MECH) $(CRYPTO) $(DERIVED_OBJS)

# include library definitions
include ../../../../Makefile.lib

MAKEFILE_EXPORT = $(CLOSED)/lib/gss_mechs/mech_dh/dh640/Makefile.export
$(EXPORT_RELEASE_BUILD)include $(MAKEFILE_EXPORT)

CPPFLAGS += -I../../backend/mech -I../../backend/crypto
CPPFLAGS += -I$(SRC)/lib/libnsl/include
CPPFLAGS += -I$(SRC)/uts/common/gssapi/include

$(PICS) := 	CFLAGS += $(XFFLAG)
$(PICS) := 	CCFLAGS += $(XFFLAG)
$(PICS) :=	CFLAGS64 += $(XFFLAG)
$(PICS) :=	CCFLAGS64 += $(XFFLAG)

DYNFLAGS +=	$(ZIGNORE)

LIBS = $(DYNLIB)
LIBNAME = $(LIBRARY:%.a=%)

MAPFILES =	../mapfile-vers
$(EXPORT_RELEASE_BUILD)MAPFILES = \
	$(CLOSED)/lib/gss_mechs/mech_dh/dh640/mapfile-vers-export

LDLIBS += -lgss -lnsl -lmp -lc

.KEEP_STATE:

# backend sources
BESRCS= $(MECH:%.o=../../backend/mech/%.c) \
	$(CRYPTO:%.o=../../backend/crypto/%.c)

SRCS=	../dh640.c ../../dh_common/dh_common.c ../../dh_common/generic_key.c \
	$(BESRCS)

ROOTLIBDIR = $(ROOT)/usr/lib/gss
ROOTLIBDIR64 = $(ROOT)/usr/lib/$(MACH64)/gss
STUBROOTLIBDIR = $(STUBROOT)/usr/lib/gss
STUBROOTLIBDIR64 = $(STUBROOT)/usr/lib/$(MACH64)/gss
LROOTLIBDIR = $(LROOT)/usr/lib/gss
LROOTLIBDIR64 = $(LROOT)/usr/lib/$(MACH64)/gss

#LINTFLAGS += -errfmt=simple
#LINTFLAGS64 += -errfmt=simple
LINTOUT =	lint.out
LINTSRC =	$(LINTLIB:%.ln=%)
ROOTLINTDIR =	$(ROOTLIBDIR)
#ROOTLINT = 	$(LINTSRC:%=$(ROOTLINTDIR)/%)

CLEANFILES += $(LINTOUT) $(LINTLIB)

lint: lintcheck

$(ROOTLIBDIR) $(STUBROOTLIBDIR):
	$(INS.dir)

$(ROOTLIBDIR64) $(STUBROOTLIBDIR64):
	$(INS.dir)

# include library targets
include ../../../../Makefile.targ

objs/%.o pics/%.o: ../../backend/crypto/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: ../../backend/mech/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: ../%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: ../../dh_common/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: ../profile/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)
