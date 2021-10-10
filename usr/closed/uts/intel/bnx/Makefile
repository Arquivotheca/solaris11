#
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#
#	This makefile drives the production of the Broadcom NetXtreme II
#	Gigabit Ethernet driver.
#

#
#	Path to the base of the uts directory tree.
#
UTSCLOSED = ../..
UTSBASE   = ../../../../src/uts
SRCHEAD   = ../../../../src/head

#
#	Define the module and object file sets.
#
MODULE		= bnx
OBJECTS		= $(BNX_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(BNX_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSCLOSED)/common/io/bnx

MAJVERSION=6
MINVERSION=0
REVVERSION=3

REL_BRCMVERSION=$(MAJVERSION).$(MINVERSION).$(REVVERSION)

#
#	Include common rules.
#
include $(UTSCLOSED)/intel/Makefile.intel

OPT_LEVEL = -xO5
PLATFLAGS = -DLITTLE_ENDIAN -UBIG_ENDIAN -D_X86_

BNX_DRV_FLAGS = -I$(UTSCLOSED)/common/io/bnx \
	    -I$(UTSCLOSED)/common/io/bnx/src \
	    -I$(UTSCLOSED)/common/io/bnx/include \
	    -I$(UTSCLOSED)/common/io/bnx/common/include \
	    -I$(UTSCLOSED)/common/io/bnx/570x/common/include \
	    -I$(UTSCLOSED)/common/io/bnx/570x/driver/common/lmdev \
	    -I$(SRCHEAD) \
	    -DBNX_ENABLE_IOCTL \
	    -D_USE_FRIENDLY_NAME \
            -DEXCLUDE_RSS_SUPPORT \
            -DEXCLUDE_KQE_SUPPORT \
	    -DL2_ONLY \
	    -DSOLARIS \
	    -D_ANSI_C_ \
	    -DLM_MAX_MC_TABLE_SIZE=256 \
	    -DBRCMVERSION="\"$(REL_BRCMVERSION)\""
		
CFLAGS += ${OPT_LEVEL} ${PLATFLAGS} ${BNX_DRV_FLAGS}

#
#	Define targets
#
ALL_TARGET	= $(BINARY) $(CONFMOD)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

#
# Driver depends on GLD & IP
#
LDFLAGS		+= -dy -N drv/ip -N misc/mac 

#
#	Default build targets.
#
.KEEP_STATE:

def:		$(DEF_DEPS)

all:		$(ALL_DEPS)

clean:		$(CLEAN_DEPS)

clobber:	$(CLOBBER_DEPS)

lint:

modlintlib:

clean.lint:	$(CLEAN_LINT_DEPS)

install:	$(INSTALL_DEPS)


#
#	Include common targets.
#
include $(UTSCLOSED)/intel/Makefile.targ
