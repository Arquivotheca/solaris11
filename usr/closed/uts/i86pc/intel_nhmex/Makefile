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
# Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE = ../../../../src/uts
UTSCLOSED = ../..

#
#       Define the module and object file sets.
#
MODULE		= cpu_ms.GenuineIntel.6.46
#
OBJECTS		= $(CPU_GENINTEL_NHMEX_OBJS:%=$(OBJS_DIR)/%)
LINTS           = $(CPU_GENINTEL_NHMEX_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE      = $(ROOT_PSM_CPU_DIR)/$(MODULE)
HARDLINKS	= cpu_ms.GenuineIntel.6.47
ROOTHARDLINKS   = $(HARDLINKS:%=$(ROOT_PSM_CPU_DIR)/%)

SRCDIR		= ../cpu/intel_nhmex

#
#       Include common rules.
#
include ../cpu/Makefile.files
include $(UTSBASE)/i86pc/cpu/Makefile.cpu
include ../Makefile.rules
$(TONICBUILD)include $(UTSCLOSED)/../Makefile.tonic

#
#	Our lint library has a different name from that of the module we build.
#
LINT_MODULE	= intel_nhmex

#
#       Define targets
#
ALL_TARGET      = $(BINARY)
LINT_TARGET     = $(LINT_MODULE).lint
INSTALL_TARGET  = $(BINARY) $(ROOTMODULE) $(ROOTHARDLINKS)

#
#	Overrides and additions
#
CPPFLAGS	+= -I$(SRCDIR) -I$(OBJS_DIR) -I$(UTSCLOSED)/intel
ASFLAGS		+= -I$(SRCDIR) -I$(OBJS_DIR) -I$(UTSCLOSED)/intel
LDFLAGS		+= -dy -N misc/acpica

#
#       Default build targets.
#
.KEEP_STATE:

def:            $(DEF_DEPS)

all:            $(ALL_DEPS)

clean:          $(CLEAN_DEPS)

clobber:        $(CLOBBER_DEPS)

lint:           $(LINT_DEPS)

modlintlib:     $(MODLINTLIB_DEPS)

clean.lint:     $(CLEAN_LINT_DEPS)

install:        $(INSTALL_DEPS)

$(ROOTHARDLINKS) := INSLINKTARGET = $(ROOTMODULE)

$(ROOTHARDLINKS):	$(ROOTMODULE)
	$(INS.link)
#
#       Include common targets.
#
include ../Makefile.targ
