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
#	This makefile drives the production of the QLgic P3+
#	10G Ethernet driver module in x86 systems
#

#
#	Paths to the base of the uts directory trees
#
UTSBASE   = ../..

#
#	Define the module and object file sets.
#
MODULE		= qlcnic
OBJECTS		= $(QLCNIC_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(QLCNIC_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/common/io/qlcnic

#
#	Include common rules.
#
include $(UTSBASE)/intel/Makefile.intel

#
#	Define targets
#
ALL_TARGET	= $(BINARY) $(SRC_CONFILE)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

INC_PATH	+= -I$(CONF_SRCDIR)

CFLAGS		+=  -DSOLARIS11 -xO4
CFLAGS64	+=  -DSOLARIS11 -xO -I./

#
#	Driver depends on MAC & IP
#
LDFLAGS		+=  -dy -N misc/mac -N drv/ip

#	Lint flag
#
LINTFLAGS	+= $(QLCNIC_DFLAGS) $(QLCNIC_KFLAGS)
LINTFLAGS	+= -DSOLARIS11

#
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
include $(UTSBASE)/intel/Makefile.targ
