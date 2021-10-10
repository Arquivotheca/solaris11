#
# Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#
# uts/intel/adpu320/Makefile
#
#
#	This makefile drives the production of the adpu320
#	network driver kernel module.
#
#	intel architecture dependent
#

#
#	Paths to the base of the uts directory trees
#
UTSCLOSED = ../..
UTSBASE   = ../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= adpu320
OBJECTS		= $(ADPU320_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(ADPU320_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSCLOSED)/intel/io/adpu320/drv

CHIMDIR=$(UTSCLOSED)/intel/io/adpu320/chim
ADPU_HEADERS=$(CHIMDIR)/chimhdr/chim.h $(CHIMDIR)/chimhdr/scsichim.h
DRVDIR=$(UTSCLOSED)/intel/io/adpu320/drv

#
#	Include common rules.
#
include $(UTSCLOSED)/intel/Makefile.intel

CFLAGS += -D__i386__ -D_MEMMAP \
          -DHIM_64ADDR -I$(DRVDIR) \
	  -I$(CHIMDIR)/chimhdr -I$(CHIMDIR)/hwm -I$(CHIMDIR)/xlm \
	  -I$(CHIMDIR)/rsm

CFLAGS_CPP_COMMENTS = -xCC

CFLAGS += $(CFLAGS_CPP_COMMENTS)

#
#	Define targets
#
ALL_TARGET	= $(BINARY) $(CONFMOD)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

#
#	Default build targets.
#
.KEEP_STATE:

def:		$(DEF_DEPS)

all:		$(ALL_DEPS)

clean:		$(CLEAN_DEPS)

clobber:	$(CLOBBER_DEPS)

#
#  we're not linting adpu320 driver. Will revisit 
#

lint:

modlintlib:

clean.lint:	$(CLEAN_LINT_DEPS)

install:	$(INSTALL_DEPS)

#
#	Include common targets.
#
include $(UTSCLOSED)/intel/Makefile.targ
