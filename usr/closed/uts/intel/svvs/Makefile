#
# uts/intel/svvs/Makefile
#
# Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#pragma ident	"%Z%%M%	%I%	%E% SMI"
#
#	This makefile drives the production of all implementation architecture
#	independent svvs modules for the intel architecture.
#

#
#	Paths to the base of the uts directory trees
#
UTSCLOSED = ../..
UTSBASE   = ../../../../src/uts

include $(UTSCLOSED)/intel/Makefile.intel

def		:=	TARGET= def
all		:=	TARGET= all
install		:=	TARGET= install
clean		:=	TARGET= clean
clobber		:=	TARGET= clobber
lint		:=	TARGET= lint
modlintlib	:=	TARGET= modlintlib
modlist		:=	TARGET= modlist
modlist		:=	NO_STATE= -K $$MODSTATE$$$$
clean.lint	:=	TARGET= clean.lint

.KEEP_STATE:

.PARALLEL:	$(SVVS_KMODS)

def all install clean clobber lint modlintlib modlist clean.lint: $(SVVS_KMODS)

$(SVVS_KMODS):	FRC
	@cd $@; pwd; $(MAKE) $(NO_STATE) $(TARGET)

FRC:

