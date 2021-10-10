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
# Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
#

#
#	Create default so empty rules don't
#	confuse make
#
CLASS		= 32
UTSBASE 	= $(SRC)/uts

LIBRARY		= libprtdiag.a
VERS		= .1

include $(SRC)/lib/Makefile.lib
include $(SRC)/Makefile.psm

LIBS		= $(DYNLIB) $(LINTLIB)
IFLAGS		= -I ../../inc -I $(USR_PSM_INCL_DIR)
IFLAGS		+= -I $(SRC)/cmd/picl/plugins/inc
IFLAGS		+= -I $(UTSBASE)/sun4u 
IFLAGS		+= -I $(UTSBASE)/sun4u/sunfire
IFLAGS		+= -I $(UTSBASE)/sun4u/serengeti
CPPFLAGS	= $(IFLAGS) $(CPPFLAGS.master)
CFLAGS		+= $(CCVERBOSE)
LDLIBS		+= -lc -lkstat
DYNFLAGS	+= -Wl,-f/usr/platform/\$$PLATFORM/lib/$(DYNLIBPSR)

# There should be a mapfile here
MAPFILES =	../../common/mapfile-vers.$(PLATFORM)

SRCDIR		= ../../common
$(LINTLIB)	:= SRCS = $(SRCDIR)/$(LINTSRC)
USR_PSMLINT=	$(LINTSRC:%=$(USR_PSM_LIB_DIR)/%)

#
# install rule
#
$(USR_PSM_LIB_DIR)/%: % $(USR_PSM_LIB_DIR)
	$(INS.file) ;\
	$(RM) -r $(USR_PSM_LIB_DIR)/libprtdiag.so ;\
	$(SYMLINK) ./libprtdiag.so$(VERS) $(USR_PSM_LIB_DIR)/libprtdiag.so

$(USR_PSM_LIB_DIR)/%:	$(SRCDIR)/%
	$(INS.file)

$(STUBUSR_PSM_LIB_DIR):
	$(INS.dir)

$(STUBUSR_PSM_LIB_DIR)/%: stubs/% $(STUBUSR_PSM_LIB_DIR)
	$(INS.file) ;\
	$(RM) -r $(STUBUSR_PSM_LIB_DIR)/libprtdiag.so ;\
	$(SYMLINK) ./libprtdiag.so$(VERS) $(STUBUSR_PSM_LIB_DIR)/libprtdiag.so

# LIBS includes the lint library, but we don't want to build one
# for the stub proto.
$(STUBUSR_PSM_LIB_DIR)/$(LINTSRC).ln:

#
# build rules
#
objs/%.o pics/%.o:	../../common/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)
