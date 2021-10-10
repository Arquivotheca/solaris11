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
# Copyright (c) 2011 by Chelsio Communications, Inc. All rights reserved.
#

#
#	This makefile drives the production of the Chelsio 
#	Terminator 3 10G Ethernet (CXGE) driver module in SPARC
#	systems
#

#
#	Paths to the base of the uts directory trees
#
UTSBASE   = ../../..

#
#	Define the module and object file sets.
#
MODULE		= cxge
OBJECTS		= $(CXGE_COM_OBJS:%=$(OBJS_DIR)/%) $(CXGE_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(CXGE_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)

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

CFLAGS		+= -g -DCONFIG_CHELSIO_T3_CORE \
		-I$(UTSBASE)/common/io/cxge \
		-I$(UTSBASE)/common/io/cxge/nexus \
		-I$(UTSBASE)/common/io/cxge/common

CFLAGS		+= -xc99=%all

#
#	Driver depends on GLD & IP
#
LDFLAGS		+= -dy -N drv/cxgen -N misc/mac -N drv/ip

#
#	Lint flag
#
LINTFLAGS	+= -DCONFIG_CHELSIO_T3_CORE \
		-I$(UTSBASE)/common/io/cxge \
		-I$(UTSBASE)/common/io/cxge/nexus \
		-I$(UTSBASE)/common/io/cxge/common

LINTFLAGS	+= -Xc99=%all

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
