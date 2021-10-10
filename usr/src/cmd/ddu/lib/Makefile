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
# Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
#

include		$(SRC)/lib/Makefile.lib

LIBRARY =	libddudev.a
VERS=		.1

SRCDIR =	.
ROOTLIBDIR =	$(ROOT)/usr/ddu/lib

OBJECTS =	libddudev.o
LINTLIB =	llib-lddudev.ln

SRCS =		$(LINTSRC)
CLEANFILES +=	$(LIBLINKS)

CPPFLAGS +=	-I$(SRC)/cmd/ddu/include
CFLAGS +=	$(CCVERBOSE) -m32 -Kpic
LDLIBS +=	-ldevinfo -lc

.KEEP_STATE:

all:		$(DYNLIB) $(LIBLINKS) $(LINTLIB)

install:	all $(ROOTLIBDIR) .WAIT $(ROOTLIBS) $(ROOTLINKS)

lint:		$(LINTLIB)

$(LIBLINKS):	$(DYNLIB)
		$(RM) $@; $(SYMLINK) ./$(DYNLIB) $@

include		$(SRC)/lib/Makefile.targ
