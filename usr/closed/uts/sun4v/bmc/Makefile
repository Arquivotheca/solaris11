#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#
# uts/sun4v/bmc/Makefile
#
#	This makefile drives the production of the bmc driver module.
#
#	sun4v implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSCLOSED	= ../..
UTSCLOSEDCOM	= $(UTSCLOSED)/common
UTSBASE		= ../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= bmc
OBJECTS		= $(BMC_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(BMC_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSCLOSEDCOM)/io/bmc
INC_PATH	+= -I$(UTSCLOSEDCOM)/io/bmc

#
#	Include common rules.
#
include $(UTSCLOSED)/sun4v/Makefile.sun4v

#
#	Define targets
#
ALL_TARGET	= $(BINARY) $(CONFMOD)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

CFLAGS += $(CCVERBOSE)

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
include $(UTSCLOSED)/$(PLATFORM)/Makefile.targ
