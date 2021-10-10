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
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY =	libdevalloc.a
VERS = 		.1
OBJECTS=	getdaent.o \
		getdevicerange.o \
		getdment.o \
		getdadefs.o \
		libdevalloc.o

#
# Include common library definitions.
#
include ../../Makefile.lib

# install this library in the root filesystem
include ../../Makefile.rootfs

SRCDIR =	../common

LIBS =	 	$(DYNLIB) $(LINTLIB)

LINTSRC= $(LINTLIB:%.ln=%)
$(LINTLIB) :=	SRCS = ../common/$(LINTSRC)
ROOTLINTDIR=	$(ROOTLIBDIR)
ROOTLINT=	$(LINTSRC:%=$(ROOTLINTDIR)/%)

CLEANFILES +=	$(LINTOUT) $(LINTLIB)

CFLAGS	+=	$(CCVERBOSE)
LDLIBS +=	-ltsol -ldevinfo -lc

COMDIR=		../common

CPPFLAGS += -I$(COMDIR)
CPPFLAGS += -D_REENTRANT

#
# message catalogue file
#
TEXT_DOMAIN= SUNW_OST_OSLIB

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

# Include library targets
#
include ../../Makefile.targ

pics/%.o: ../common/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

# install rule for lint library target
$(ROOTLINTDIR)/%: ../common/%
	$(INS.file)
