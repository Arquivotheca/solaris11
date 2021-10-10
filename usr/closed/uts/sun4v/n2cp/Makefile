#
# uts/sun4v/n2cp/Makefile
#
# Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
#
#	This makefile drives the production of the n2cp driver kernel module.
#
#	sun4v implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSCLOSED = ../..
UTSBASE   = ../../../../src/uts
COM_DIR   = $(COMMONBASE)/crypto

#
#	Define the module and object file sets.
#
MODULE		= n2cp
OBJECTS		= $(N2CP_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(N2CP_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)
CONF_SRCDIR     = $(UTSCLOSED)/sun4v/io/n2cp

#
#	Include common rules.
#
include $(UTSCLOSED)/sun4v/Makefile.sun4v

#
#	Add FIPS-140 Checksum
#
POST_PROCESS	+= ; $(FIPS140_CHECKSUM)

#
#	Override defaults to build a unique, local modstubs.o.
#
MODSTUBS_DIR	= $(OBJS_DIR)

CLEANFILES	+= $(MODSTUBS_O)

#
#	Define targets
#
ALL_TARGET	= $(BINARY) $(SRC_CONFFILE)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

#
#	Include closed sun4v headers
#
INC_PATH	+= -I${UTSCLOSED}/sun4v -I$(COM_DIR)/fips

#
# lint pass one enforcement
#
CFLAGS += -v -DN2 -DN2_ERRATUM_175 -I$(COM_DIR)
LINTFLAGS += -DN2 -DN2_ERRATUM_175 -I$(COM_DIR)

#
# module dependencies
#
LDFLAGS += -dy -Ndrv/vnex -Nmisc/ds -Nmisc/kcf

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
