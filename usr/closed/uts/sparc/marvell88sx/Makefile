#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#
#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSCLOSED = ../..
UTSBASE	= ../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= marvell88sx 
OBJECTS		= $(MV_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(MV_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR     = $(UTSBASE)/common/io/sata/adapters/marvell88sx
WARLOCK_OUT     = $(MV_OBJS:%.o=%.ll)
WARLOCK_OK      = $(MODULE).ok
WLCMD_DIR       = $(UTSBASE)/common/io/warlock

#
#	Include common rules.
#
include $(UTSCLOSED)/sparc/Makefile.sparc

#
#	Define targets
#
ALL_TARGET	= $(BINARY)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

#
#	Overrides.
#
DEBUG_FLGS	=
DEBUG_DEFS	+= $(DEBUG_FLGS)

#
# lint pass one enforcement
#  
CFLAGS += $(CCVERBOSE)

#
#	Default build targets.
#
.KEEP_STATE:

def:		$(DEF_DEPS)

all:		$(ALL_DEPS)

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


SD_FILES = $(SD_OBJS:%.o=$(UTSBASE)/sparc/sd/%.ll)
SATA_FILES = $(SATA_OBJS:%.o= -l $(UTSBASE)/sparc/sata/%.ll)
SCSI_FILES = $(SCSI_OBJS:%.o= -l $(UTSBASE)/sparc/scsi/%.ll)
CMLB_FILES = $(CMLB_OBJS:%.o= -l $(UTSBASE)/sparc/cmlb/%.ll)

warlock: $(WARLOCK_OK)

$(WARLOCK_OK): $(WLCMD_DIR)/mv.wlcmd $(WARLOCK_OUT)  warlock_ddi.files \
	sata.files scsi.files sd.files cmlb.files
	$(WARLOCK) -c $(WLCMD_DIR)/mv.wlcmd $(WARLOCK_OUT) \
	$(SD_FILES) \
	$(CMLB_FILES) \
	$(SATA_FILES) \
	$(SCSI_FILES) \
	-l $(UTSBASE)/sparc/warlock/ddi_dki_impl.ll
	$(TOUCH) $@

%.ll: $(UTSCLOSED)/common/io/sata/adapters/marvell88sx/%.c
	$(WLCC) $(CPPFLAGS) -D __sparcv9 -o $@ $<

sata.files:
	@cd $(UTSBASE)/sparc/sata; pwd; $(MAKE) warlock

scsi.files:
	@cd $(UTSBASE)/sparc/scsi; pwd; $(MAKE) warlock

sd.files:
	@cd $(UTSBASE)/sparc/sd; pwd; $(MAKE) warlock_alone

cmlb.files:
	@cd $(UTSBASE)/sparc/cmlb; pwd; $(MAKE) warlock


warlock_ddi.files:
	@cd $(UTSBASE)/sparc/warlock; pwd; $(MAKE) warlock
