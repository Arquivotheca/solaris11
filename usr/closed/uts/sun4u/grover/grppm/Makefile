#
# uts/sun4u/grover/grppm/Makefile
#
# Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#pragma ident	"%Z%%M%	%I%	%E% SMI"

#
#	This makefile drives the production of the platform power
#	management driver in the sun4u grover system.
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSCLOSED = ../../..
UTSBASE	  = ../../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= grppm
OBJECTS		= $(GRPPM_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(GRPPM_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_GROVER_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSCLOSED)/sun4u/grover/io

#
#	Include common rules.
#
include $(UTSCLOSED)/sun4u/grover/Makefile.grover

#
#	Define targets
#
ALL_TARGET	= $(BINARY) $(SRC_CONFFILE)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

#
# lint pass one enforcement
#
CFLAGS += $(CCVERBOSE)

#
# Turn on doubleword alignment for 64-bit registers
#
CFLAGS += -dalign

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

LINT_LIB_DIR	= $(GROVER_LINT_LIB_DIR)

#
#	Include common targets.
#
include $(UTSCLOSED)/sun4u/grover/Makefile.targ
