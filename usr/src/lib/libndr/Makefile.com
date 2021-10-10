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
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY =	libndr.a
VERS =		.1

OBJS_COMMON = 			\
	ndr_client.o		\
	ndr_heap.o		\
	ndr_marshal.o		\
	ndr_ops.o		\
	ndr_process.o		\
	ndr_server.o		\
	ndr_svc.o

NDLLIST = rpcpdu

OBJECTS=	$(OBJS_COMMON) $(OBJS_SHARED) $(NDLLIST:%=%_ndr.o)

include ../../Makefile.lib

# Install the library header files under /usr/include/smbsrv.
ROOTSMBHDRDIR=	$(ROOTHDRDIR)/smbsrv
ROOTSMBHDRS=	$(HDRS:%=$(ROOTSMBHDRDIR)/%)

SRCDIR=		../common
NDLDIR=		$(ROOT)/usr/include/smbsrv/ndl
LIBS=		$(DYNLIB) $(LINTLIB)
C99MODE =       -xc99=%all
C99LMODE =      -Xc99=%all
CPPFLAGS +=	-I$(SRCDIR) -I.
$(LINTLIB) := SRCS = $(SRCDIR)/$(LINTSRC)

CLEANFILES += $(OBJECTS:%_ndr.o=%_ndr.c)

INCS += -I$(SRC)/common/smbsrv

LDLIBS +=	$(MACH_LDLIBS)
LDLIBS +=	-lsmb -luuid -lc

CPPFLAGS += $(INCS) -D_REENTRANT

SRCS=   $(OBJS_COMMON:%.o=$(SRCDIR)/%.c)		\
	$(OBJS_SHARED:%.o=$(SRC)/common/smbsrv/%.c)

%_ndr.c: $(NDLDIR)/%.ndl
	$(NDRGEN) -Y $(CC) $<

pics/%.o:	$(SRC)/common/smbsrv/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

.KEEP_STATE:

all: $(LIBS)

lint: lintcheck

include ../../Makefile.targ
