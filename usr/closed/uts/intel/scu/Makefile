#
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#
#	This makefile drives the production of the
#	"kernel/drv/scu" kernel module.
#
#	intel architecture dependent
#

#
#	Paths to the base of the uts directory trees
#
UTSCLOSED = ../..
UTSBASE = ../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= scu 
SCU_OBJECTS	= $(SCU_BASE_OBJS) $(SCU_OSSL_OBJS)	\
		  $(SCU_SATI_OBJS) $(SCU_SCIC_OBJS)	\
		  $(SCU_SCIF_OBJS) $(SCU_UTIL_OBJS)
SCU_LINT_OBJECTS	= $(SCU_OSSL_OBJS)
OBJECTS		= $(SCU_OBJECTS:%=$(OBJS_DIR)/%)
LINTS		= $(SCU_LINT_OBJECTS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSCLOSED)/common/io/scsi/adapters/scu/ossl

#
#	Include common rules.
#
include $(UTSCLOSED)/intel/Makefile.intel

#
#	Define targets
#
ALL_TARGET	= $(BINARY) $(CONFMOD)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

#
#	Compile switches
#

MAJOR_VERSION_INPUT	= 1
MINOR_VERSION_INPUT	= 8
BUILD_MAJOR_VERSION_INPUT	= 2

SCI_BASE_FLAGS	= -D__SCI_LIBRARY_MAJOR_VERSION__=$(MAJOR_VERSION_INPUT)	\
		  -D__SCI_LIBRARY_MINOR_VERSION__=$(MINOR_VERSION_INPUT)	\
		  -D__SCI_LIBRARY_BUILD_VERSION__=$(BUILD_MAJOR_VERSION_INPUT)	\
		  -DSCI_OBJECT_USE_ASSOCIATION_FUNCTIONS			\
		  -DSCI_LOGGING


SCI_SATI_FLAGS	= -DSATI_TRANSPORT_SUPPORTS_SAS	\
		  -DSATI_TRANSPORT_SUPPORTS_SATA

PHY_MAX_LINK_SPEED_GENERATION	= 3

SCI_SCIC_FLAGS	= -DPBG_BUILD	\
		  -DSCIC_SDS_4_ENABLED	\
		  -DUSE_ABSTRACT_LIST_FUNCTIONS \
		  -DPHY_MAX_LINK_SPEED_GENERATION=$(PHY_MAX_LINK_SPEED_GENERATION)

CPPFLAGS	+= $(SCI_BASE_FLAGS) $(SCI_SATI_FLAGS) $(SCI_SCIC_FLAGS)

#
#	Include specific header files
#
INC_PATH	+= -I$(UTSCLOSED)/common/io/scsi/adapters/scu/include
INC_PATH	+= -I$(UTSCLOSED)/common/io/scsi/adapters/scu/base/include
INC_PATH	+= -I$(UTSCLOSED)/common/io/scsi/adapters/scu/ossl
INC_PATH	+= -I$(UTSCLOSED)/common/io/scsi/adapters/scu/sati/include
INC_PATH	+= -I$(UTSCLOSED)/common/io/scsi/adapters/scu/scic/include
INC_PATH	+= -I$(UTSCLOSED)/common/io/scsi/adapters/scu/scic/sds/include
INC_PATH	+= -I$(UTSCLOSED)/common/io/scsi/adapters/scu/scif/include
INC_PATH	+= -I$(UTSCLOSED)/common/io/scsi/adapters/scu/scif/sas/include
INC_PATH	+= -I$(UTSCLOSED)/common/io/scsi/adapters/scu/util/include

#
#	Add the below flags to compile the shared SCI library from Intel
#
CFLAGS	+= -erroff=E_STATEMENT_NOT_REACHED	\
	   -erroff=E_ENUM_TYPE_MISMATCH_OP	\
	   -erroff=E_INTEGER_OVERFLOW_DETECTED	\
	   -erroff=E_ENUM_TYPE_MISMATCH_ARG

#	Kernel Module Dependencies
#
LDFLAGS		+= -dy -Nmisc/scsi

#
#	Default build targets.
#
.KEEP_STATE:

all:		$(ALL_DEPS)

def:		$(DEF_DEPS)

clean:		$(CLEAN_DEPS)

clobber:	$(CLOBBER_DEPS)

lint:		$(LINT_DEPS)

modlintlib:	$(MODLINTLIB_DEPS)

clean.lint:	$(CLEAN_LINT_DEPS)

install:	$(INSTALL_DEPS)

#
#	Include common targets.
#
include $(UTSCLOSED)/intel/Makefile.targ
