#
# uts/intel/iprb/Makefile
#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#	This makefile drives the production of the iprb
#	network driver kernel module.
#
#	intel architecture dependent
#

#
#	Paths to the base of the uts directory trees
#
UTSCLOSED	= ../..
UTSBASE		= ../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= iprb
OBJECTS		= $(IPRB_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(IPRB_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSCLOSED)/intel/Makefile.intel

#
#	Define targets
#
ALL_TARGET	= $(BINARY)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE)

CPPFLAGS	+= -DSUNDDI
LDFLAGS		+= -dy -Nmisc/mac -Nmisc/mii

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
include $(UTSCLOSED)/intel/Makefile.targ
