#
# uts/sun4v/memtest/Makefile
# Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"%Z%%M%	%I%	%E% SMI"
#
#	Drives the production of the memtest driver
#	(cpu/memory error injector driver)
#
#	sun4v implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSCLOSED = ../..
UTSBASE   = ../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= memtest
OBJECTS		= $(MEMTEST_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(MEMTEST_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSCLOSED)/sun4/io/memtest

#
#	Include common rules.
#
include $(UTSCLOSED)/sun4v/Makefile.sun4v

#
#	Define targets.
#
ALL_TARGET	= $(BINARY) $(SRC_CONFFILE)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

#
#	Include sun4 and sun4v header files.
#
INC_PATH	+= -I${UTSBASE}/sun4 -I${UTSCLOSED}/sun4
INC_PATH	+= -I${UTSBASE}/sun4v -I${UTSCLOSED}/sun4v

#
#	Override ASFLAGS.
#
ASFLAGS += -xarch=v9v

#
#	Lint pass one enforcement.
#
CFLAGS += $(CCVERBOSE)

#
# 	Turn on doubleword alignment for 64 bit registers.
#
CFLAGS += -dalign -DNCPU=558 -DNCPU_LOG2=10

#
# For now, disable these lint checks; maintainers should endeavor
# to investigate and remove these for maximum lint coverage.
# Please do not carry these forward to new Makefiles.
#
LINTTAGS	+= -erroff=E_BAD_PTR_CAST_ALIGN
LINTTAGS	+= -erroff=E_ASSIGN_NARROW_CONV
LINTTAGS	+= -erroff=E_STATIC_UNUSED
LINTTAGS	+= -erroff=E_SUSPICIOUS_COMPARISON

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
include $(UTSCLOSED)/sun4v/Makefile.targ

