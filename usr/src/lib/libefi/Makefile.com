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
# Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY =	libefi.a
VERS =		.1
OBJECTS =	rdwr_efi.o crc32_efi.o

include ../../Makefile.lib

# install this library in the root filesystem
include ../../Makefile.rootfs

SRCDIR =	../common

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-luuid -lc
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)
CFLAGS +=	$(CCVERBOSE)

LINTFLAGS64 +=	-errchk=longptr64

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

include ../../Makefile.targ
