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
# Copyright (c) 2000, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY=	libsysevent.a

VERS=		.1

OBJECTS=	libsysevent.o		\
		libevchannel.o

# include library definitions
include ../../Makefile.lib
include ../../Makefile.rootfs

LIBS =		$(DYNLIB) $(LINTLIB)

LINTSRC =       $(LINTLIB:%.ln=%)

CPPFLAGS +=	-D_REENTRANT -I..
CFLAGS +=	$(CCVERBOSE)
LDLIBS +=	-lnvpair -lc

$(LINTLIB) :=	SRCS = ../llib-lsysevent

.KEEP_STATE:

all : stub $(LIBS)

lint : lintcheck

# include library targets
include ../../Makefile.targ

pics/%.o:	../%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

$(ROOTLINTDIR)/%: ../%
	$(INS.file)
