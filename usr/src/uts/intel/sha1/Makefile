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
# uts/intel/sha1/Makefile
#
# Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
#
#
#	This makefile drives the production of the sha1 crypto kernel module.
#
#	intel architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..
COMDIR = $(COMMONBASE)/crypto

#
#	Define the module and object file sets.
#
MODULE		= sha1
SHA1_OBJS_32	=
SHA1_OBJS_64	= sha1-x86_64.o sha1-ssse3.o sha1-ssse3-1block.o \
			sha1-avx.o sha1-avx-1block.o
SHA1_OBJS	+= $(SHA1_OBJS_$(CLASS))
OBJECTS		= $(SHA1_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(SHA1_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_CRYPTO_DIR)/$(MODULE)
ROOTLINK	= $(ROOT_MISC_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/intel/Makefile.intel

#
#       Add FIPS-140 Checksum
#
POST_PROCESS    += ; $(FIPS140_CHECKSUM)

#
#	Override defaults
#
CLEANFILES += sha1-x86_64.s
#
# Linkage dependencies
#
LDFLAGS += -dy -Nmisc/kcf

CPPFLAGS	+= -I$(COMDIR)
AS_CPPFLAGS	+= -I../../$(PLATFORM)

#
#	Define targets
#
ALL_TARGET	= $(BINARY)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOTLINK)

#
#	Default build targets.
#
.KEEP_STATE:

def:		$(DEF_DEPS)

all:		$(ALL_DEPS)

clean:		$(CLEAN_DEPS)

clobber:	$(CLOBBER_DEPS)

lint:		$(LINT_DEPS)

modlintlib:	$(MODLINTLIB_DEPS)

clean.lint:	$(CLEAN_LINT_DEPS)

install:	$(INSTALL_DEPS)

$(ROOTLINK):	$(ROOT_MISC_DIR) $(ROOTMODULE)
	-$(RM) $@; ln $(ROOTMODULE) $@

#
#	Include common targets.
#
include $(UTSBASE)/intel/Makefile.targ

$(OBJS_DIR)/%.o: $(COMDIR)/sha1/amd64/%.s
	$(COMPILE.s) -o $@ $(COMDIR)/sha1/amd64/${@F:.o=.s}
	$(POST_PROCESS_O)

$(OBJS_DIR)/sha1-ssse3-1block.o: $(COMDIR)/sha1/amd64/sha1-ssse3.s
	$(COMPILE.s) -DINTEL_SHA1_SINGLEBLOCK -o $@ \
		$(COMDIR)/sha1/amd64/sha1-ssse3.s
	$(POST_PROCESS_O)

$(OBJS_DIR)/sha1-avx.o: $(COMDIR)/sha1/amd64/sha1-ssse3.s
	$(COMPILE.s) -DUSE_AVX -o $@ \
		$(COMDIR)/sha1/amd64/sha1-ssse3.s
	$(POST_PROCESS_O)

$(OBJS_DIR)/sha1-avx-1block.o: $(COMDIR)/sha1/amd64/sha1-ssse3.s
	$(COMPILE.s) -DUSE_AVX -DINTEL_SHA1_SINGLEBLOCK -o $@ \
		$(COMDIR)/sha1/amd64/sha1-ssse3.s
	$(POST_PROCESS_O)

$(OBJS_DIR)/%.o: %.s
	$(COMPILE.s) -o $@ ${@F:.o=.s}
	$(POST_PROCESS_O)

$(OBJS_DIR)/%.ln: $(COMDIR)/sha1/amd64/%.s
	@($(LHEAD) $(LINT.c) $(COMDIR)/sha1/amd64/${@F:.ln=.s} $(LTAIL))

$(OBJS_DIR)/sha1-ssse3-1block.ln: $(COMDIR)/sha1/amd64/sha1-ssse3.s
	@(set -x; $(LHEAD) $(LINT.c) -DINTEL_SHA1_SINGLEBLOCK \
		-C $(OBJS_DIR)/${@F:.ln=} \
		$(COMDIR)/sha1/amd64/sha1-ssse3.s $(LTAIL))

$(OBJS_DIR)/sha1-avx.ln: $(COMDIR)/sha1/amd64/sha1-ssse3.s
	@(set -x; $(LHEAD) $(LINT.c) -DUSE_AVX \
		-C $(OBJS_DIR)/${@F:.ln=} \
		$(COMDIR)/sha1/amd64/sha1-ssse3.s $(LTAIL))

$(OBJS_DIR)/sha1-avx-1block.ln: $(COMDIR)/sha1/amd64/sha1-ssse3.s
	@(set -x; $(LHEAD) $(LINT.c) -DUSE_AVX -DINTEL_SHA1_SINGLEBLOCK \
		-C $(OBJS_DIR)/${@F:.ln=} \
		$(COMDIR)/sha1/amd64/sha1-ssse3.s $(LTAIL))

$(OBJS_DIR)/%.ln: %.s
	@($(LHEAD) $(LINT.c) ${@F:.ln=.s} $(LTAIL))

%.s: $(COMDIR)/sha1/amd64/%.pl
	$(PERL) $? $@
