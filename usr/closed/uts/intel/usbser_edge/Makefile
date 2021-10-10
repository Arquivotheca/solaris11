#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#


#
#	This makefile drives the production of the Edgeport USB Serial
#	Adapter driver.
#
#	Paths to the base of the uts directory trees
#
UTSCLOSED = ../..
UTSBASE   = ../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= usbser_edge
OBJECTS		= $(USBSER_EDGE_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(USBSER_EDGE_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSCLOSED)/common/io/usb/clients/usbser/usbser_edge
WARLOCK_OUT     = $(USBSER_EDGE_OBJS:%.o=%.ll)
WARLOCK_OK	= $(MODULE).ok
WLCMD_DIR	= $(UTSBASE)/common/io/warlock

#
#	Include common rules.
#
include $(UTSCLOSED)/intel/Makefile.intel

LDFLAGS         += -dy -Nmisc/usba -Nmisc/usbser

#
#	Define targets
#
ALL_TARGET	= $(BINARY) $(SRC_CONFFILE)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

#
# For now, disable these lint checks; maintainers should endeavor
# to investigate and remove these for maximum lint coverage.
# Please do not carry these forward to new Makefiles.
#
LINTTAGS	+= -erroff=E_BAD_PTR_CAST_ALIGN

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
include $(UTSCLOSED)/intel/Makefile.targ

#
#	Defines for local commands.
#
WLCC		= wlcc
TOUCH		= touch
WARLOCK		= warlock
TEST		= test

#
#	warlock
#
WARLOCK_CMD	= $(WLCMD_DIR)/$(MODULE).wlcmd
USBSER_FILES = $(USBSER_OBJS:%.o=$(UTSBASE)/intel/usbser/%.ll)
USBA_FILES = $(USBA_WITHOUT_WUSB_OBJS:%.o=$(UTSBASE)/intel/usba/%.ll)
UHCI_FILES = $(UHCI_OBJS:%.o=$(UTSBASE)/intel/uhci/%.ll)
OHCI_FILES = $(OHCI_OBJS:%.o=$(UTSBASE)/intel/ohci/%.ll)
EHCI_FILES = $(EHCI_OBJS:%.o=$(UTSBASE)/intel/ehci/%.ll)

warlock: $(WARLOCK_OK)

$(WARLOCK_OK): warlock_with_usbser warlock_with_usba
	$(TOUCH) $@

%.ll: $(UTSCLOSED)/common/io/usb/clients/usbser/usbser_edge/%.c
	$(WLCC) $(CPPFLAGS) -DDEBUG -o $@ $<

warlock_with_usbser: $(WARLOCK_OUT) usbser_files warlock_ddi.files \
	$(WARLOCK_CMD)
	$(WARLOCK) -c $(WARLOCK_CMD) $(WARLOCK_OUT) $(USBSER_FILES) \
		-l $(UTSBASE)/intel/warlock/ddi_dki_impl.ll 

warlock_with_usba: $(WLCMD_DIR)/usbser_edge_with_usba.wlcmd $(WARLOCK_OUT) \
	usbser_files usba_files ohci_files uhci_files ehci_files \
	warlock_ddi.files 
	$(WARLOCK) -c $(WLCMD_DIR)/usbser_edge_with_usba.wlcmd \
		$(USBA_FILES) $(OHCI_FILES) $(EHCI_FILES) $(UHCI_FILES) $(USBSER_FILES) \
		$(WARLOCK_OUT) -l $(UTSBASE)/intel/warlock/ddi_dki_impl.ll

usbser_files:
	@cd $(UTSBASE)/intel/usbser; pwd; $(MAKE) warlock 

usba_files:
	@cd $(UTSBASE)/intel/usba;pwd; $(MAKE) warlock

uhci_files:
	@cd $(UTSBASE)/intel/uhci;pwd; $(MAKE) warlock

ohci_files:
	@cd $(UTSBASE)/intel/ohci;pwd; $(MAKE) warlock

ehci_files:
	@cd $(UTSBASE)/intel/ehci;pwd; $(MAKE) warlock

warlock_ddi.files:
	cd $(UTSBASE)/intel/warlock; pwd; $(MAKE) warlock
