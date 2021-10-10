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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY=	libpower.a
VERS=		.1

CMNOBJS = libpower.o libpower_error.o libpower_subr.o \
	  pm_smf.o pm_kernel.o pm_suspend.o
ISAOBJS = pm_se.o
OBJECTS = $(CMNOBJS) $(ISAOBJS)

include	../../Makefile.lib
include ../../Makefile.rootfs

LIBS =		$(DYNLIB) $(LINTLIB)


SRCDIR =	../common
SRCS =		$(CMNOBJS:%.o=$(SRCDIR)/%.c) $(ISAOBJS:%.o=%.c)

$(LINTLIB) :=	SRCS=$(SRCDIR)/$(LINTSRC)

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-I../common
LDLIBS +=	-lscf -luutil -lnvpair -lc

.KEEP_STATE:

all: stub $(LIBS)

lint:	$(LINTLIB) lintcheck

include ../../Makefile.targ
