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
# Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
#


LIBRARY =	libSMHBAAPI.a
VERS =		.1
OBJECTS	=	SMHBAAPILIB.o
CONFIGFILE=	smhba.conf
ROOTETC=	$(ROOT)/etc

include ../../Makefile.lib

HETCFILES=	$(CONFIGFILE:%=$(ROOTETC)/%)

LIBS =		$(DYNLIB) $(LINTLIB)
SRCDIR=		../common

INCS +=		-I$(SRCDIR)
INCS +=		-I$(SRC)/lib/hbaapi/common
CFLAGS +=	-DSOLARIS
CFLAGS +=	-DVERSION='"Version 1"'
CFLAGS +=	-DUSESYSLOG
CPPFLAGS +=	$(INCS)
CPPFLAGS +=	-DPOSIX_THREADS

LDLIBS +=	-lc

$(LINTLIB) := SRCS=	$(SRCDIR)/$(LINTSRC)

$(ROOTETC)/%:	../common/%
	$(INS.file)

.KEEP_STATE:

all:	stub $(LIBS) $(HETCFILES)

lint: lintcheck

include ../../Makefile.targ
