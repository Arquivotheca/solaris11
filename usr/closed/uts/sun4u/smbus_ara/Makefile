# Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.

#
#ident	"%Z%%M%	%I%	%E% SMI"
#

#	This makefile drives the smbus_ara build.
#
#	Path to the base of the uts directory tree (usually /usr/src/uts).

UTSCLOSED = ../..
UTSBASE   = ../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= smbus_ara
OBJECTS		= $(SMBUS_ARA_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(SMBUS_ARA_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSCLOSED)/sun4u/Makefile.sun4u

# 
# lint pass one enforcement 
# 
CFLAGS += $(CCVERBOSE)

LDFLAGS += -dy -N misc/i2c_svc

#
#	Define targets
#
ALL_TARGET	= $(BINARY)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE)

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
include $(UTSCLOSED)/sun4u/Makefile.targ
