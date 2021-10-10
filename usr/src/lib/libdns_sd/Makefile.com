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
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY =	libdns_sd.a
VERS =		.1
OBJECTS =	dnssd_clientlib.o dnssd_clientstub.o dnssd_ipc.o

include ../../Makefile.lib

LIBS =		$(DYNLIB) $(LINTLIB)
$(LINTLIB):=    SRCS = $(SRCDIR)/$(LINTSRC)

SRCDIR =	../common

LDLIBS +=	-lsocket -lc

C99MODE =	$(C99_ENABLE)
CFLAGS +=	-erroff=E_ASSIGNMENT_TYPE_MISMATCH
CPPFLAGS +=	-I$(SRCDIR) -DNOT_HAVE_SA_LEN 

.PARALLEL =     $(OBJECTS)
.KEEP_STATE:

lint: lintcheck

all: stub $(LIBS)

include ../../Makefile.targ
