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
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY= 	libbe.a
VERS= 		.1

OBJECTS= 	\
		be_activate.o \
		be_create.o \
		be_list.o \
		be_mount.o \
		be_rename.o \
		be_snapshot.o \
		be_utils.o \
		be_zones.o

include ../../Makefile.lib

LIBS=		$(DYNLIB) $(LINTLIB)

SRCDIR= 	../common
BOOTSRCDIR=	../../../cmd/boot/common

INCS += -I$(SRCDIR) -I$(BOOTSRCDIR) -I/usr/include/python2.6

C99MODE= 	$(C99_ENABLE)

LDLIBS +=	-lzfs -linstzones -luuid -lnvpair -lc -lgen
CPPFLAGS +=	$(INCS)

$(LINTLIB) := SRCS=	$(SRCDIR)/$(LINTSRC)

.KEEP_STATE:

all install := LDLIBS += -lpython2.6

all: stub $(LIBS) $(LIBRARY)

lint: lintcheck

install: $(ROOTLIBS)

include ../../Makefile.targ
