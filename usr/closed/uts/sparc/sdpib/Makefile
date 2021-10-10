#
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSCLOSED = ../..
UTSBASE   = ../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= sdpib
OBJECTS		= $(IBSDP_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(IBSDP_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
ROOTLINK	= $(ROOT_STRMOD_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSCLOSED)/common/io/ib/clients/sdpib
LDFLAGS		+= -dy -Nmisc/ibtl -Nmisc/ibcm -Ndrv/ip -Nmisc/dls

#
#	Include common rules.
#
include $(UTSCLOSED)/sparc/Makefile.sparc

#
#	Define targets
#
ALL_TARGET	= $(BINARY) $(SRC_CONFFILE)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOTLINK) $(ROOT_CONFFILE)

#
#	Overrides
#
#ALL_BUILDS	= $(ALL_BUILDSONLY64)
#DEF_BUILDS	= $(DEF_BUILDSONLY64)
#CLEANLINTFILES	+= $(LINT32_FILES)

# 
# lint pass one enforcement 
# 
CFLAGS += -D_SUN_TPI_VERSION=2 $(CCVERBOSE) 

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

$(ROOT_STRMOD_DIR)/%:   $(ROOTMODULE)
	$(INS.link)

#
#	Include common targets.
#
include $(UTSCLOSED)/sparc/Makefile.targ
