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

LIBRARY=	liba5k.a
VERS=		.2

OBJECTS =	diag.o \
		lhot.o \
		mon.o

include ../../../Makefile.lib
include ../../../Makefile.rootfs

LIBS =	$(DYNLIB) $(LINTLIB)

SRCDIR	= ../common

.KEEP_STATE:

COMMON_LINTFLAGS = -erroff=E_SEC_SPRINTF_UNBOUNDED_COPY
COMMON_LINTFLAGS += -erroff=E_SEC_PRINTF_VAR_FMT

LINTFLAGS += $(COMMON_LINTFLAGS) 
LINTFLAGS64 +=  $(COMMON_LINTFLAGS)

LDLIBS += -lc -ldevice -lg_fc

$(LINTLIB) := SRCS=	$(SRCDIR)/$(LINTSRC)

all: stub $(LIBS)

lint: lintcheck

include  ../../../Makefile.targ
