#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSCLOSED = ../..
UTSBASE	= ../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= bcm_sata 
OBJECTS		= $(BCM_SATA_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(BCM_SATA_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR     = $(UTSCLOSED)/common/io/sata/adapters/bcm_sata
WARLOCK_OUT     = $(BCM_SATA_OBJS:%.o=%.ll)
WARLOCK_OK      = $(MODULE).ok
WLCMD_DIR	= $(UTSBASE)/common/io/warlock

#
#	Include common rules.
#
include $(UTSCLOSED)/intel/Makefile.intel

#
#	Define targets
#
ALL_TARGET	= $(BINARY)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE)

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
# dependency on sata module
#
LDFLAGS += -dy -N misc/sata

#
# For now, disable these lint checks; maintainers should endeavor
# to investigate and remove these for maximum lint coverage.
# Please do not carry these forward to new Makefiles.
#
LINTTAGS	+= -erroff=E_BAD_PTR_CAST_ALIGN
LINTTAGS	+= -erroff=E_ASSIGN_NARROW_CONV
LINTTAGS	+= -erroff=E_STATIC_UNUSED

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
include $(UTSCLOSED)/intel/Makefile.targ


#
#	Defines for local commands.
#
WARLOCK		= warlock
WLCC		= wlcc
TOUCH		= touch
TEST		= test

SD_FILES = $(SD_OBJS:%.o=$(UTSBASE)/intel/sd/%.ll)
SATA_FILES = $(SATA_OBJS:%.o= -l $(UTSBASE)/intel/sata/%.ll)
SCSI_FILES = $(SCSI_OBJS:%.o= -l $(UTSBASE)/intel/scsi/%.ll)
CMLB_FILES = $(CMLB_OBJS:%.o= -l $(UTSBASE)/intel/cmlb/%.ll)

warlock: $(WARLOCK_OK)

$(WARLOCK_OK): $(WLCMD_DIR)/bcm_sata.wlcmd $(WARLOCK_OUT)  warlock_ddi.files \
	sata.files scsi.files sd.files cmlb.files
	$(WARLOCK) -c $(WLCMD_DIR)/bcm_sata.wlcmd $(WARLOCK_OUT) \
	$(SD_FILES) \
	$(CMLB_FILES) \
	$(SATA_FILES) \
	$(SCSI_FILES) \
	-l $(UTSBASE)/intel/warlock/ddi_dki_impl.ll
	$(TOUCH) $@

%.ll: $(UTSCLOSED)/common/io/sata/adapters/bcm_sata/%.c
	$(WLCC) $(CPPFLAGS) -o $@ $<

sata.files:
	@cd $(UTSBASE)/intel/sata; pwd; $(MAKE) warlock

scsi.files:
	@cd $(UTSBASE)/intel/scsi; pwd; $(MAKE) warlock

sd.files:
	@cd $(UTSBASE)/intel/sd; pwd; $(MAKE) warlock_alone

cmlb.files:
	@cd $(UTSBASE)/intel/cmlb; pwd; $(MAKE) warlock

warlock_ddi.files:
	@cd $(UTSBASE)/intel/warlock; pwd; $(MAKE) warlock
