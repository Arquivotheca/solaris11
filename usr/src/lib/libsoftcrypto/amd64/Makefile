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

LIBRARY =	libsoftcrypto.a
VERS =		.1

AES_PSM_COBJS= aeskey.o aesni_modes.o
AES_PSM_SOBJS= aes_amd64.o aes_intel.o aesni_cbc.o aesni_ctr.o aesni_ecb.o
AES_PSM_OBJS= $(AES_PSM_COBJS) $(AES_PSM_SOBJS)

ARCFOUR_PSM_OBJS =	arcfour-x86_64.o
BIGNUM_PSM_OBJS =	bignum_amd64.o bignum_amd64_asm.o
MODES_PSM_OBJS =	gcm_intel.o

AES_PSM_SRC= $(AES_PSM_COBJS:%.o=$(AES_DIR)/$(MACH64)/%.c) \
		$(AES_PSM_SOBJS:%.o=$(AES_DIR)/$(MACH64)/%.s)

ARCFOUR_PSM_SRC =	arcfour-x86_64.s
BIGNUM_PSM_SRC =	$(BIGNUM_DIR)/$(MACH64)/bignum_amd64.c \
			$(BIGNUM_DIR)/$(MACH64)/bignum_amd64_asm.s
MODES_PSM_SRC = $(MODES_DIR)/$(MACH64)/gcm_intel.s

include		../Makefile.com
include		$(SRC)/lib/Makefile.lib.64

LIBS =		$(DYNLIB) $(LINTLIB)

CFLAGS +=	-xO4 -xipo
BIGNUM_FLAGS +=	-DPSR_MUL
CLEANFILES +=	arcfour-x86_64.s

.KEEP_STATE:

all:		stub $(LIBS)

lint:		$$(SRCS)

install:	stubinstall all $(ROOTLIBS64) $(ROOTLINKS64) $(ROOTLINT64)

include		../Makefile.targ

stubinstall:	stub $(STUBROOTLIBS64) $(STUBROOTLINKS64)

pics/%.o: $(AES_DIR)/$(MACH64)/%.c
		$(COMPILE.c) $(AES_FLAGS) -o $@ $<
		$(POST_PROCESS_O)

pics/%.o: $(AES_DIR)/$(MACH64)/%.s
		$(COMPILE.s) $(AES_FLAGS) -o $@ $<
		$(POST_PROCESS_O)

pics/%.o: $(BIGNUM_DIR)/$(MACH64)/%.c
		$(COMPILE.c) $(BIGNUM_FLAGS) -o $@ $<
		$(POST_PROCESS_O)

pics/%.o: $(BIGNUM_DIR)/$(MACH64)/%.s
		$(COMPILE64.s) $(BIGNUM_FLAGS) -o $@ $<
		$(POST_PROCESS_O)

pics/%.o: $(MODES_DIR)/$(MACH64)/%.s
		$(COMPILE.s) $(MODES_FLAGS) -o $@ $<
		$(POST_PROCESS_O)

arcfour-x86_64.s: $(ARCFOUR_DIR)/amd64/arcfour-x86_64.pl
		$(PERL) $? $@

pics/%.o: arcfour-x86_64.s
		$(COMPILE64.s) $(ARCFOUR_FLAGS) -o $@ $<
		$(POST_PROCESS_O)
