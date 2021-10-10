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
# Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY= libpcp.a
VERS= .1

# PLATFORM_OBJECTS is defined in platform Makefile
OBJECTS= $(PLATFORM_OBJECTS)

include $(SRC)/lib/Makefile.lib
include $(SRC)/Makefile.psm

SRCDIR =	../common

CPPFLAGS +=	$(PLATINCS) -DPCP_CKSUM_ENABLE

LINKED_DIRS	= $(LINKED_PLATFORMS:%=$(USR_PLAT_DIR)/%)
STUBLINKED_DIRS	= $(LINKED_PLATFORMS:%=$(STUBUSR_PLAT_DIR)/%)
LINKED_LIB_DIRS	= $(LINKED_PLATFORMS:%=$(USR_PLAT_DIR)/%/lib)
STUBLINKED_LIB_DIRS	= $(LINKED_PLATFORMS:%=$(STUBUSR_PLAT_DIR)/%/lib)
LINKED_LIBPCP_DIR	= \
	$(LINKED_PLATFORMS:%=$(USR_PLAT_DIR)/%/lib/libpcp.so)
STUBLINKED_LIBPCP_DIR	= \
	$(LINKED_PLATFORMS:%=$(STUBUSR_PLAT_DIR)/%/lib/libpcp.so)
LINKED_LIBPCP1_DIR	= \
	$(LINKED_PLATFORMS:%=$(USR_PLAT_DIR)/%/lib/libpcp.so.1)
STUBLINKED_LIBPCP1_DIR	= \
	$(LINKED_PLATFORMS:%=$(STUBUSR_PLAT_DIR)/%/lib/libpcp.so.1)
LINKED_LLIBLPCP_DIR 	= \
	$(LINKED_PLATFORMS:%=$(USR_PLAT_DIR)/%/lib/llib-lpcp.ln)

LIBS = $(DYNLIB) $(LINTLIB)
CFLAGS +=	$(CCVERBOSE)
LDLIBS +=	-lc -lumem -ldevinfo
PLATLIBS =	$(USR_PLAT_DIR)/$(PLATFORM)/lib
STUBPLATLIBS =	$(STUBUSR_PLAT_DIR)/$(PLATFORM)/lib
INS.slink6=	$(RM) -r $@; $(SYMLINK) ../../$(PLATFORM)/lib/libpcp.so.1 $@
INS.slink7=	$(RM) -r $@; $(SYMLINK) ../../$(PLATFORM)/lib/libpcp.so $@
INS.slink8=	$(RM) -r $@; $(SYMLINK) ../../$(PLATFORM)/lib/llib-lpcp.ln $@

.KEEP_STATE:

#
# build/lint rules
#
all:	stub $(LIBS)

lint:	lintcheck

include $(SRC)/lib/Makefile.targ
