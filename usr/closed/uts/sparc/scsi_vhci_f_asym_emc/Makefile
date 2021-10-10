#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
#pragma ident	"%Z%%M%	%I%	%E% SMI"
#
# This makefile drives the production of misc/scsi_vhci/scsi_vhci_f_asym_emc
# sparc architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSCLOSED = ../..
UTSBASE   = ../../../../src/uts


#
# Define the module and object file sets.
#
MODULE		= scsi_vhci_f_asym_emc
OBJECTS		= $(SCSI_VHCI_F_ASYM_EMC_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(SCSI_VHCI_F_ASYM_EMC_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_SCSI_VHCI_DIR)/$(MODULE)

#
# Include common rules.
#
include $(UTSCLOSED)/sparc/Makefile.sparc

#
# Define targets.
#
ALL_TARGET	= $(BINARY)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE)

#
# Note dependancy on misc/scsi.
#
LDFLAGS += -dy -N"misc/scsi" -N"drv/scsi_vhci"

#
# Default build targets.
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
# Include common targets.
#
include $(UTSCLOSED)/sparc/Makefile.targ
