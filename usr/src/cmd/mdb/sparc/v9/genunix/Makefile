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
# Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
#

MODULE = genunix.so
MDBTGT = kvm

include	../../../common/modules/genunix/Makefile.files

COMMONSRCS = \
	$(GENUNIX_SRCS)

KMODSRCS = \
	$(COMMONSRCS)

MODSRCS = \
	$(COMMONSRCS) \
	typegraph.c

#
# This signals that $(KMODSRCS) != $(MODSRCS).  Typegraph is not usable under
# kmdb.  As such, we don't bother compiling it.
KMOD_SOURCES_DIFFERENT=$(POUND_SIGN)

include ../../../../Makefile.cmd
include ../../../../Makefile.cmd.64
include ../../Makefile.sparcv9
include ../../../Makefile.module

dmod/$(MODULE) := LDLIBS += -lm

#
# We are not actually hardwiring some dependency on sun4u, we just need to
# include sys/param.h with _MACHDEP defined, and this forces the inclusion of
# machparam.h, even though we don't use anything there.  This is a temporary
# kludge until we invent -DDONTINCLUDEMACHPARAM or something.
#
CPPFLAGS += -I$(SRC)/uts/sun4u

# Needed to include c2/audit.h (from cred.h)
CPPFLAGS += -I$(SRC)/uts/common
# Needed to find include file mutex_impl.h
CPPFLAGS += -I$(SRC)/uts/sparc/v9
# Needed to find include file hrt.h
CPPFLAGS += -I$(SRC)/uts/sun4

# Needed to compile <netinet/sctp.h> as it uses flexible array member.
C99MODE += $(C99_ENABLE)

#
# Sources comming from usr/src/common
#
GENUNIX_IOCTLNAME_DIR = $(SRC)/common/ioctlname

dmod/%.o kmod/%.o: $$(GENUNIX_IOCTLNAME_DIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(CTFCONVERT_O)

dmod/%.ln kmod/%.ln: $$(GENUNIX_IOCTLNAME_DIR)/%.c
	$(LINT.c) -dirout=$(@D) -c $<
