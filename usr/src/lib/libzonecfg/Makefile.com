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
# Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY=	libzonecfg.a
VERS=		.1
OBJECTS=	libzonecfg.o getzoneent.o scratchops.o

include ../../Makefile.lib

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-lc -lsocket -lnsl -luuid -lnvpair -lsysevent -lbrand \
		-lpool -lscf -lproc -luutil -lbsm
# DYNLIB libraries do not have lint libs and are not linted
$(DYNLIB) :=	LDLIBS += -lxml2

SRCDIR =	../common
CPPFLAGS +=	-I/usr/include/libxml2 -I$(SRCDIR) -D_REENTRANT
$(LINTLIB) := SRCS=	$(SRCDIR)/$(LINTSRC)

.KEEP_STATE:

all:	stub $(LIBS)

lint:	lintcheck

include ../../Makefile.targ
