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
# Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY =	libpool.a
VERS =		.1

OBJECTS = \
	pool.o \
	pool_internal.o \
	pool_xml.o \
	pool_kernel.o \
	pool_commit.o \
	pool_value.o \
	dict.o

include ../../Makefile.lib

LIBS =		$(DYNLIB) $(LINTLIB)
LXML2 = -lxml2
lint := LXML2 =
LDLIBS +=	$(LXML2) -lscf -lnvpair -lexacct -lc

SRCDIR =	../common
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-D_REENTRANT -D_FILE_OFFSET_BITS=64 -I/usr/include/libxml2


.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

include ../../Makefile.targ
