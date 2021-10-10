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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

#
#	This makefile drives the production of the efb graphics driver 
#	in sparc systems, which is used with ATI Radeon family devices 
#	like the XVR-50 (ES1000), XVR-100, and XVR-300.
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE		= ../..

#
#	Define the module and object file sets.
#
MODULE		= efb
OBJECTS		= $(EFB_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(EFB_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)

# Need to build some sources found in common/io/drm, but using local copy of
# drmP.h instead of the one found there, until the two are reconciled, so we
# use -I- to block the directory the source file was found in from being used 
# for #include "drmP.h"
INC_PATH        += -I- -I$(UTSBASE)/sparc/efb -I$(UTSBASE)/common/io/drm

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Define targets
#
ALL_TARGET	= $(BINARY)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE)

#
#	Overrides
#
LINTFLAGS += -erroff=E_BAD_PTR_CAST_ALIGN

# Need to suppress warnings about static inline radeon_check_offset in 
# radeon_drv.h that is used in some *.c files but not others.
LINTFLAGS += -erroff=E_STATIC_UNUSED

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

#
#	Include common targets.
#
include $(UTSBASE)/sparc/Makefile.targ
