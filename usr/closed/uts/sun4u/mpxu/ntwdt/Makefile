#
# uts/sun4u/mpxu/ntwdt/Makefile
# Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"%Z%%M%	%I%	%E% SMI"
#
#       This makes the "ntwdt" driver module, which
#       provides for an application-level watchdog
#       timer.
#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSCLOSED = ../../..
UTSBASE   = ../../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= ntwdt
OBJECTS		= $(NTWDT_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(NTWDT_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_MPXU_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSCLOSED)/sun4u/mpxu/io

#
#	Include common rules.
#
include $(UTSCLOSED)/sun4u/mpxu/Makefile.mpxu
#
#	Overrides
#
ALL_BUILDS	= $(ALL_BUILDSONLY64)
DEF_BUILDS	= $(DEF_BUILDSONLY64)
CLEANLINTFILES	+= $(LINT32_FILES)

#
# lint pass one enforcement

#
#	Define targets
#
ALL_TARGET	= $(BINARY) $(SRC_CONFFILE)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

# Turn this on once compiler understands v9 in it's backend
#INLINES		+= $(UTSCLOSED)/sun4u/io/pci.il

#
# lint pass one enforcement
#
CFLAGS += -v

#
# Turn on doubleword alignment for 64 bit registers
#
CFLAGS += -dalign

LDFLAGS	+= -dy -Ndrv/rmc_comm -Ndrv/pmugpio -Ndrv/rmclomv

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
include $(UTSCLOSED)/sun4u/mpxu/Makefile.targ
