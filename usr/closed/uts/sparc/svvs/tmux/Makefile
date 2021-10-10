#
# uts/sparc/tmux/Makefile
# Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"%Z%%M%	%I%	%E% SMI"
#
#	This makefile drives the production of the lo driver kernel module.
#
#	sparc architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSCLOSED = ../../..
UTSBASE   = ../../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= tmux
OBJECTS		= $(TMUX_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(TMUX_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(USR_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSCLOSED)/common/io

#
#	Include common rules.
#
include $(UTSCLOSED)/sparc/Makefile.sparc

#
#	Define targets
#
ALL_TARGET	= $(BINARY) $(SRC_CONFILE)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

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
LINTTAGS	+= -erroff=E_PTRDIFF_OVERFLOW

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
include $(UTSCLOSED)/sparc/Makefile.targ
