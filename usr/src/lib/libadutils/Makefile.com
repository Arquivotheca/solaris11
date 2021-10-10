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

LIBRARY =	libadutils.a
VERS =		.1
OBJECTS =	adutils.o addisc.o adutils_threadfuncs.o
LINT_OBJECTS =	adutils.o addisc.o adutils_threadfuncs.o

include ../../Makefile.lib

C99MODE=	-xc99=%all
C99LMODE=	-Xc99=%all

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-lc -lldap -lresolv -lsocket -lnsl
SRCDIR =	../common
$(LINTLIB):=	SRCS = $(SRCDIR)/$(LINTSRC)

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-D_REENTRANT -I$(SRCDIR) -I$(SRC)/lib/libldap5/include/ldap \
		-I/usr/include/mps

lint := OBJECTS = $(LINT_OBJECTS)

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

LINTFLAGS += -erroff=E_CONSTANT_CONDITION
LINTFLAGS64 += -erroff=E_CONSTANT_CONDITION

include ../../Makefile.targ
