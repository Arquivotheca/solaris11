#
# Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
#
#
# This makefile drives the production of the ipmi
# driver kernel module.
#
# intel architecture dependent
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
MODULE		= ipmi
OBJECTS		= $(IPMI_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(IPMI_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSCLOSEDCOM)/io/ipmi
INC_PATH	+= -I$(UTSBASE)/i86pc -I$(UTSCLOSEDCOM)/io/ipmi

#
#	Include common rules.
#
include $(UTSCLOSED)/intel/Makefile.intel

#
#	Define targets
#
ALL_TARGET	= $(BINARY) $(CONFMOD)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

DEBUG_FLGS      =
$(NOT_RELEASE_BUILD)DEBUG_DEFS	+= $(DEBUG_FLGS)

CPPFLAGS	+= -DSUNDDI

#
# For now, disable these lint checks; maintainers should endeavor
# to investigate and remove these for maximum lint coverage.
# Please do not carry these forward to new Makefiles.
#
LINTTAGS	+= -erroff=E_BAD_PTR_CAST_ALIGN
LINTTAGS	+= -erroff=E_ASSIGN_NARROW_CONV

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
