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
# Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
#
LIBRARY =	libshare.a
VERS =		.1

LIBOBJS =	libshare.o \
		libshare_plugin.o \
		libshare_util.o \
		libshare_proto.o \
		libshare_fs.o \
		libshare_cache.o \
		sharetab.o	\
		libshare_upgrade.o
OTHOBJS =	share_util.o
OBJECTS =	$(LIBOBJS) $(OTHOBJS)

include ../../Makefile.lib

# libshare must be installed in the root filesystem for mount(1M)/libzfs
include ../../Makefile.rootfs

SRCDIR =	../common

LIBSRCS =	$(LIBOBJS:%.o=$(SRCDIR)/%.c)	\
	      $(OTHOBJS:%.o=$(SRC)/common/share/%.c)

lintcheck := SRCS = $(LIBSRCS)

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-lc -lnvpair -lscf -luuid -lsec
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)


CPPFLAGS +=	-D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS -D_LARGEFILE64_SOURCE

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

pics/%.o:	$(SRC)/common/share/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

include ../../Makefile.targ
