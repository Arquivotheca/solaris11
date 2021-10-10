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

LIBRARY =	libuvfs.a
VERS =		.1

UVFS_OBJS = \
	fs.o \
	door.o \
	fsid.o \
	ioctl.o \
	svc.o \
	stash.o \
	name.o \
	dir.o
COM_OBJS = list.o
OBJECTS = $(UVFS_OBJS) $(COM_OBJS)
	

include ../../Makefile.lib

LIBS =		$(DYNLIB) $(LINTLIB)

LDLIBS +=	-lavl -lnvpair -lumem -lscf -lc

SRCDIR =	../common
COMDIR =	$(SRC)/common/list
C99MODE=	$(C99_ENABLE)
SRCS = \
	$(UVFS_OBJS:%.o=$(SRCDIR)/%.c) \
	$(COM_OBJS:%.o=$(COMDIR)/%.c)
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

INCS	 	+= -I$(SRCDIR)
CFLAGS 		+= $(CCVERBOSE) -Wp,-xc99=%all -mt
CPPFLAGS	+= $(INCS) -D_FILE_OFFSET_BITS=64

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

pics/%.o: $(COMDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

include $(SRC)/lib/Makefile.targ
