#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY= librsc.a
VERS= .1

# PLATFORM_OBJECTS is defined in platform Makefile
OBJECTS= $(PLATFORM_OBJECTS)

include $(SRC)/lib/Makefile.lib
include $(SRC)/Makefile.psm

CPPFLAGS +=	$(PLATINCS)

LINKED_DIRS	= $(LINKED_PLATFORMS:%=$(USR_PLAT_DIR)/%)
STUBLINKED_DIRS	= $(LINKED_PLATFORMS:%=$(STUBUSR_PLAT_DIR)/%)
LINKED_LIB_DIRS	= $(LINKED_PLATFORMS:%=$(USR_PLAT_DIR)/%/lib)
STUBLINKED_LIB_DIRS	= $(LINKED_PLATFORMS:%=$(STUBUSR_PLAT_DIR)/%/lib)
LINKED_LIBRSC_DIR	= \
	$(LINKED_PLATFORMS:%=$(USR_PLAT_DIR)/%/lib/librsc.so)
STUBLINKED_LIBRSC_DIR	= \
	$(LINKED_PLATFORMS:%=$(STUBUSR_PLAT_DIR)/%/lib/librsc.so)
LINKED_LIBRSC1_DIR	= \
	$(LINKED_PLATFORMS:%=$(USR_PLAT_DIR)/%/lib/librsc.so.1)
STUBLINKED_LIBRSC1_DIR	= \
	$(LINKED_PLATFORMS:%=$(STUBUSR_PLAT_DIR)/%/lib/librsc.so.1)
LINKED_LLIBLRSC_DIR 	= \
	$(LINKED_PLATFORMS:%=$(USR_PLAT_DIR)/%/lib/llib-lrsc.ln)

SRCDIR =	common
LIBS = $(DYNLIB) $(LINTLIB)
CFLAGS +=	$(CCVERBOSE)
LDLIBS +=	-lc
PLATLIBS =	$(USR_PLAT_DIR)/$(PLATFORM)/lib/
STUBPLATLIBS =	$(STUBUSR_PLAT_DIR)/$(PLATFORM)/lib/
INS.slink6=	$(RM) -r $@; $(SYMLINK) ../../$(PLATFORM)/lib/librsc.so.1 $@
INS.slink7=	$(RM) -r $@; $(SYMLINK) ../../$(PLATFORM)/lib/librsc.so $@
INS.slink8=	$(RM) -r $@; $(SYMLINK) ../../$(PLATFORM)/lib/llib-lrsc.ln $@

.KEEP_STATE:

#
# build/lint rules
#
all:	stub $(LIBS)
lint:	lintcheck

#
# install rules
#
$(PLATLIBS)/librsc.so $(STUBPLATLIBS)/librsc.so:
	$(RM) -r $@; $(SYMLINK) librsc.so.1 $@

install:	stubinstall all $(USR_PSM_LIBS) $(PLATLIBS)/librsc.so \
		$(LINKED_DIRS) $(LINKED_LIB_DIRS) \
		$(LINKED_LIBRSC_DIR) $(LINKED_LIBRSC1_DIR) \
		$(LINKED_LLIBLRSC_DIR)

stubinstall:	stub $(STUBUSR_PSM_LIBS) $(STUBPLATLIBS)/librsc.so \
		$(STUBLINKED_DIRS) $(STUBLINKED_LIB_DIRS) \
		$(STUBLINKED_LIBRSC_DIR) $(STUBLINKED_LIBRSC1_DIR)

$(STUBUSR_PSM_LIB_DIR):
	$(INS.dir)

$(USR_PSM_LIB_DIR)/%: % $(USR_PSM_LIB_DIR)
	$(INS.file)

$(STUBUSR_PSM_LIB_DIR)/%: stubs/% $(STUBUSR_PSM_LIB_DIR)
	$(INS.file)

$(LINKED_DIRS):	$(USR_PLAT_DIR)
	-$(INS.dir)

$(STUBLINKED_DIRS):	$(STUBUSR_PLAT_DIR)
	-$(INS.dir)

$(LINKED_LIB_DIRS):	$(LINKED_DIRS)
	-$(INS.dir)

$(STUBLINKED_LIB_DIRS):	$(STUBLINKED_DIRS)
	-$(INS.dir)

$(LINKED_LIBRSC_DIR): $(USR_PLAT_DIR)
	-$(INS.slink7)

$(STUBLINKED_LIBRSC_DIR): $(STUBUSR_PLAT_DIR)
	-$(INS.slink7)

$(LINKED_LIBRSC1_DIR): $(USR_PLAT_DIR)
	-$(INS.slink6)

$(STUBLINKED_LIBRSC1_DIR): $(STUBUSR_PLAT_DIR)
	-$(INS.slink6)

$(LINKED_LLIBLRSC_DIR): $(USR_PLAT_DIR)
	-$(INS.slink8)

$(STUBLINKED_LLIBLRSC_DIR): $(STUBUSR_PLAT_DIR)
	-$(INS.slink8)

# LIBS includes the lint library, but we don't want to build one
# for the stub proto.
$(STUBUSR_PSM_LIB_DIR)/llib-lrsc.ln:

include $(SRC)/lib/Makefile.targ
