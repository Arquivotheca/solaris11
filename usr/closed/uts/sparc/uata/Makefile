#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#ident	"%Z%%M%	%I%	%E% SMI"

#
#	This makefile drives the production of the ata driver kernel module.
#
#	Sparc architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSCLOSED = ../..
UTSBASE	  = ../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= uata
OBJECTS		= $(ATA_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(ATA_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR     = $(UTSCLOSED)/sun/io/dada/adapters
WARLOCK_OUT     = $(ATA_OBJS:%.o=%.ll)
WARLOCK_OK      = $(MODULE).ok
WLCMD_DIR	= $(UTSBASE)/common/io/warlock

#
#	Include common rules.
#
include $(UTSCLOSED)/sparc/Makefile.sparc
#
#       Define targets
#
ALL_TARGET	= $(BINARY)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

#
#	Overrides.
#
CLOBBERFILES	+= $(MODULE)
CFLAGS		+= $(CCVERBOSE)

#
# For now, disable these lint checks; maintainers should endeavor
# to investigate and remove these for maximum lint coverage.
# Please do not carry these forward to new Makefiles.
#
LINTTAGS	+= -erroff=E_BAD_PTR_CAST_ALIGN
LINTTAGS	+= -erroff=E_ASSIGN_NARROW_CONV
LINTTAGS	+= -erroff=E_STATIC_UNUSED
LINTTAGS	+= -erroff=E_SUPPRESSION_DIRECTIVE_UNUSED

#
#       Default build targets.
#
.KEEP_STATE:

all:		$(ALL_DEPS)

def:		$(DEF_DEPS)

clean:		$(CLEAN_DEPS)
		$(RM) $(WARLOCK_OUT) $(WARLOCK_OK)

clobber:	$(CLOBBER_DEPS)
		$(RM) $(WARLOCK_OUT) $(WARLOCK_OK)

lint:		$(LINT_DEPS)

modlintlib:	$(MODLINTLIB_DEPS) lint64

clean.lint:	$(CLEAN_LINT_DEPS)

install:	$(INSTALL_DEPS)



#
#	Include common targets.
#
include $(UTSCLOSED)/sparc/Makefile.targ

#
#       Defines for local commands.
#
WARLOCK		= warlock
WLCC            = wlcc
TOUCH           = touch
TEST		= test

#
#       Warlock targets
#

SCSI_FILES = $(SCSI_OBJS:%.o= -l $(UTSBASE)/sparc/scsi/%.ll)

warlock:	$(WARLOCK_OK)

$(WARLOCK_OK):	$(WARLOCK_OUT) $(WLCMD_DIR)/uata.wlcmd warlock_ddi.files \
	scsi.files
	$(WARLOCK) -c $(WLCMD_DIR)/uata.wlcmd $(WARLOCK_OUT)  \
	$(UTSBASE)/sparc/warlock/scsi.ll \
	$(SCSI_FILES) \
	-l $(UTSBASE)/sparc/warlock/ddi_dki_impl.ll
	$(TOUCH) $@

%.ll:	$(UTSCLOSED)/sun/io/dada/adapters/%.c
	$(WLCC) $(CPPFLAGS) -o $@ $<

%.ll:	$(UTSCLOSED)/sun/io/dada/adapters/ghd/%.c
	$(WLCC) $(CPPFLAGS) -o $@ $<

scsi.files:
	@cd $(UTSBASE)/sparc/scsi; pwd; $(MAKE) warlock

warlock_ddi.files:
	@cd $(UTSBASE)/sparc/warlock; pwd; $(MAKE) warlock
