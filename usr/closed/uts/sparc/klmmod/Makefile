#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"%Z%%M%	%I%	%E% SMI"
#
#	This makefile drives the production of the klmmod
#	kernel module.
#
#	sparc architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSCLOSED = ../..
UTSBASE   = ../../../../src/uts

#
#	Define the module and object file sets.
#
MODULE		= klmmod
OBJECTS		= $(KLMMOD_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(KLMMOD_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_MISC_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSCLOSED)/sparc/Makefile.sparc

#
#	Define targets
#
ALL_TARGET	= $(BINARY)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE)

#
#	Overrides.
#
CFLAGS		+= $(CCVERBOSE)

#
# For now, disable these lint checks; maintainers should endeavor
# to investigate and remove these for maximum lint coverage.
# Please do not carry these forward to new Makefiles.
#
LINTTAGS	+= -erroff=E_BAD_PTR_CAST_ALIGN

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
#	Warlock targets and overrides.
#
WLDIR		= /net/suntools.Eng/export/tools/sparc/bin
WLCC		= $(WLDIR)/wlcc
WARLOCK		= $(WLDIR)/warlock
WLFILES		= $(KLMMOD_OBJS:%.o=%.ll)
WLDEPS		= $(WLFILES:%.ll=%.dep)
DEP_GENERATOR	= \
	if [ -f *.dep ] ; then \
		$(ECHO) *.dep ; \
	else \
		$(ECHO) /dev/null ; \
	fi

.PARALLEL:	$(WLFILES) $(WLDEPS)

CLEANFILES	+= $(WLFILES) $(WLDEPS) warlock.out

include $(DEP_GENERATOR:sh)

warlock: warlock.out

warlock.out: $(WLFILES) klmmod.wlc $(WLDEPS)
	$(WARLOCK) -c klmmod.wlc $(WLFILES) > warlock.out

#	Make a .ll file from a .c file.
%.ll: ../../common/klm/%.c $(WLDEPS)
	$(WLCC) $(CFLAGS) $(CPPFLAGS) -o $@ $<

#
#	wlcc does not generate dependencies for .make.state, so we generate
#	them manually with `cc -xM' each time a .ll file is made because it's
#	better than not having them at all.  When wlcc and make start doing
#	this automatically, all of this *.dep stuff can be removed.
#
SED		= /usr/bin/sed
SORT		= /usr/bin/sort
UNIQ		= /usr/bin/uniq
%.dep: ../../common/klm/%.c
	$(CC) -xM $(CFLAGS) $(CPPFLAGS) $< | $(SED) 's/\.o/.ll/' | \
		$(SORT) | $(UNIQ) > $*.dep

#
#	Include common targets.
#
include $(UTSCLOSED)/sparc/Makefile.targ
