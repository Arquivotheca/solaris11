#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"%Z%%M%	%I%	%E% SMI"
#
#	This makefile drives the production of the Intel 10gb
#	driver.
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSCLOSED = ../..
UTSBASE   = ../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= ixgb
OBJECTS		= $(IXGB_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(IXGB_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSCLOSED)/sparc/Makefile.sparc

#
#	Define targets
#
ALL_TARGET	= $(BINARY)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE)

#
# lint pass one enforcement
#
CFLAGS          += $(CCVERBOSE)

#
# Turn on doubleword alignment for 64 bit registers
#
CFLAGS		+= -dalign

#
# Driver depends on MAC & IP
#
LDFLAGS		+= -dy -N misc/mac -N drv/ip

#
# For now, disable these lint checks; maintainers should endeavor
# to investigate and remove these for maximum lint coverage.
# Please do not carry these forward to new Makefiles.
#
LINTTAGS	+= -erroff=E_BAD_PTR_CAST_ALIGN

#
#	Default build targets.
#
.KEEP_STATE:

def:		$(DEF_DEPS)

all:		$(ALL_DEPS)

clean:		$(CLEAN_DEPS)

clobber:	$(CLOBBER_DEPS)

lint:		$(LINT_DEPS)

modlintlib:	$(MODLINTLIB_DEPS) lint64

clean.lint:	$(CLEAN_LINT_DEPS)

install:	$(INSTALL_DEPS)

#
#	Include common targets.
#
include $(UTSCLOSED)/sparc/Makefile.targ

