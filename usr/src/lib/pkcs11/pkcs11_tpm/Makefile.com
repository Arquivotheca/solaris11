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
# Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
#
LIBRARY =	pkcs11_tpm.a
VERS =		.1

OBJECTS= api_interface.o \
	apiutil.o \
	asn1.o \
	cert.o \
	data_obj.o \
	decr_mgr.o \
	dig_mgr.o \
	encr_mgr.o \
	globals.o \
	hwf_obj.o \
	key.o \
	key_mgr.o \
	loadsave.o \
	log.o \
	mech_md5.o \
	mech_rsa.o \
	mech_sha.o \
	new_host.o \
	obj_mgr.o \
	object.o \
	sess_mgr.o \
	sign_mgr.o \
	template.o \
	tpm_specific.o \
	utility.o \
	verify_mgr.o


include $(SRC)/lib/Makefile.lib

SRCDIR= ../common

SRCS=	$(OBJECTS:%.o=$(SRCDIR)/%.c)

#       set signing mode
POST_PROCESS_SO +=      ; $(ELFSIGN_CRYPTO)

ROOTLIBDIR=$(ROOT)/usr/lib/security
ROOTLIBDIR64=$(ROOT)/usr/lib/security/$(MACH64)

LIBS=$(DYNLIB) $(DYNLIB64)

LDLIBS += $(EXTRALIBS) -lc -luuid -lmd -ltspi -lcrypto
CPPFLAGS += -xCC -D_POSIX_PTHREAD_SEMANTICS $(TSSINC)
CPPFLAGS64 += $(CPPFLAGS)
C99MODE=        $(C99_ENABLE)

# llib-lcrypto and llib-ltspi are delivered from SFW in /lib and /usr/lib
# respectively, and the use of -xCC requires we convince lint to look there.
EXTRALIBS32 =		-L/lib -L/usr/lib
EXTRALIBS64 =		-L/lib/$(MACH64) -L/usr/lib/$(MACH64)
lint :=	EXTRALIBS =	$(EXTRALIBS32)

LINTSRC= $(OBJECTS:%.o=$(SRCDIR)/%.c)

$(LINTLIB):=	SRCS	=	$(SRCDIR)/$(LINTSRC)
LINTSRC= $(SRCS)

.KEEP_STATE:

all: stub $(LIBS)
 
lint: $$(LINTSRC)
	$(LINT.c) $(LINTCHECKFLAGS) $(LINTSRC) $(LDLIBS)

pics/%.o: $(SRCDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

include $(SRC)/lib/Makefile.targ
