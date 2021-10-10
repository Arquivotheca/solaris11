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
# Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY = libscsi.a
VERS = .1

OBJECTS = \
	scsi_engine.o \
	scsi_errno.o \
	scsi_status.o \
	scsi_subr.o

include ../../../Makefile.lib
include ../../Makefile.defs

SRCS = $(OBJECTS:%.o=../common/%.c)
C99MODE = $(C99_ENABLE)
CPPFLAGS += -I../common -I. -D_REENTRANT
$(NOT_RELEASE_BUILD)CPPFLAGS += -DDEBUG
CFLAGS += $(CCVERBOSE)
LDLIBS += \
	-lumem \
	-lc
LIBS =		$(DYNLIB) $(LINTLIB)
ROOTLIBDIR =		$(ROOTSCSILIBDIR)
ROOTLIBDIR64 =		$(ROOTSCSILIBDIR)/$(MACH64)
STUBROOTLIBDIR =	$(STUBROOTSCSILIBDIR)
STUBROOTLIBDIR64 =	$(STUBROOTSCSILIBDIR)/$(MACH64)
LROOTLIBDIR =		$(LROOTSCSILIBDIR)
LROOTLIBDIR64 =		$(LROOTSCSILIBDIR)/$(MACH64)

CLEANFILES += \
	../common/scsi_errno.c

$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

.KEEP_STATE:

all : stub $(LIBS)

lint : lintcheck

../common/scsi_errno.c: ../common/mkerrno.sh ../common/libscsi.h
	sh ../common/mkerrno.sh < ../common/libscsi.h > $@

pics/%.o: ../common/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

include ../../../Makefile.targ
include ../../Makefile.rootdirs
