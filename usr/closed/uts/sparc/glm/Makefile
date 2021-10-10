#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"%Z%%M%	%I%	%E% SMI"
#
#	This makefile drives the production of the glm driver kernel module.
#
#	Sparc architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE   = ../../../../src/uts
UTSCLOSED = ../..

#
#	Define the module and object file sets.
#
MODULE		= glm
OBJECTS		= $(GLM_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(GLM_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
WARLOCK_OUT     = $(GLM_OBJS:%.o=%.ll)
WARLOCK_OK      = $(MODULE).ok
WLCMD_DIR       = $(UTSBASE)/common/io/warlock

#
#	Define targets
#
ALL_TARGET	= $(BINARY)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE)

#
#	Include common rules.
#
include $(UTSCLOSED)/sparc/Makefile.sparc

# 
# lint pass one enforcement 
# 
CFLAGS += $(CCVERBOSE)

#
# For now, disable these lint checks; maintainers should endeavor
# to investigate and remove these for maximum lint coverage.
# Please do not carry these forward to new Makefiles.
#
LINTTAGS	+= -erroff=E_BAD_PTR_CAST_ALIGN
LINTTAGS	+= -erroff=E_ASSIGN_NARROW_CONV
LINTTAGS	+= -erroff=E_SUSPICIOUS_COMPARISON

#
#	Default build targets.
#
.KEEP_STATE:

all:		$(ALL_DEPS)

def:		$(DEF_DEPS)

clean:		$(CLEAN_DEPS)
		$(RM) $(WARLOCK_OUT) $(WARLOCK_OK)

clobber:	$(CLOBBER_DEPS)
		$(RM) $(WARLOCK_OUT) $(WARLOCK_OK)

lint:		$(LINT_DEPS)

modlintlib:	$(MODLINTLIB_DEPS)

clean.lint:	$(CLEAN_LINT_DEPS)

install:	$(INSTALL_DEPS)

#
#	Include common targets.
#
include $(UTSCLOSED)/sparc/Makefile.targ

#
#	Defines for local commands.
#
WARLOCK		= warlock
WLCC		= wlcc
TOUCH		= touch
TEST		= test

#
#	lock_lint rules
#
SCSI_FILES = $(SCSI_OBJS:%.o= -l $(UTSBASE)/sparc/scsi/%.ll)

warlock: $(WARLOCK_OK)

$(WARLOCK_OK): $(WARLOCK_OUT)  warlock_ddi.files scsi.files \
	$(WLCMD_DIR)/glm.wlcmd
	$(WARLOCK) -c $(WLCMD_DIR)/glm.wlcmd $(WARLOCK_OUT) \
	    $(UTSBASE)/sparc/warlock/scsi.ll \
	    -l $(UTSBASE)/sparc/warlock/ddi_dki_impl.ll \
	    $(SCSI_FILES)
	$(TOUCH) $@

%.ll: $(UTSCLOSED)/common/io/scsi/adapters/%.c
	$(WLCC) $(CPPFLAGS) -DDEBUG -o $@ $<

warlock_ddi.files:
	@cd $(UTSBASE)/sparc/warlock; pwd; $(MAKE) warlock

scsi.files:
	@cd $(UTSBASE)/sparc/scsi; pwd; $(MAKE) warlock
