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

LIBRARY= libmail.a
VERS= .1

OBJECTS= 	abspath.o  casncmp.o   copystream.o delempty.o \
		getdomain.o maillock.o notifyu.o    popenvp.o \
		s_string.o  setup_exec.o strmove.o  skipspace.o \
		substr.o   systemvp.o  trimnl.o     xgetenv.o

include ../../Makefile.lib

SRCDIR =	../common

MAPFILES +=	$(MAPFILE32)

LIBS =		$(DYNLIB) $(LINTLIB)

$(LINTLIB):= SRCS = ../common/llib-lmail

LINTSRC=	$(LINTLIB:%.ln=%)

CPPFLAGS =	-I../inc -I../../common/inc $(CPPFLAGS.master)
CFLAGS +=	$(CCVERBOSE)
LDLIBS +=	-lc

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

include ../../Makefile.targ

pics/%.o: ../common/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: ../inc/%.h

# install rule for lint library target
$(ROOTLINTDIR)/%: ../common/%
	$(INS.file)

# install rule for 64 bit lint library target
$(ROOTLINTDIR64)/%: ../common/%
	$(INS.file)
