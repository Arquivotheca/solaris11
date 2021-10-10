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
#

LIBRARY =	libreparse_smb.a
VERS =		.1

OBJS_COMMON =	smbrp_plugin.o

OBJECTS=        $(OBJS_COMMON)

include ../../Makefile.lib

ROOTSMBHDRDIR=	$(ROOTHDRDIR)/smbsrv
ROOTSMBHDRS=	$(HDRS:%=$(ROOTSMBHDRDIR)/%)

SRCDIR=		../common
NDLDIR=		$(ROOT)/usr/include/smbsrv/ndl
LIBS=		$(DYNLIB) $(LINTLIB)
C99MODE =       -xc99=%all
C99LMODE =      -Xc99=%all
CPPFLAGS +=	-I$(SRCDIR) -I.
$(LINTLIB) := SRCS = $(SRCDIR)/$(LINTSRC)

ROOTLIBDIR =		$(ROOT)/usr/lib/reparse
ROOTLIBDIR64 =		$(ROOT)/usr/lib/reparse/$(MACH64)

STUBROOTLIBDIR =	$(STUBROOT)/usr/lib/reparse
STUBROOTLIBDIR64 =	$(STUBROOT)/usr/lib/reparse/$(MACH64)

LROOTLIBDIR =		$(LROOT)/usr/lib/reparse
LROOTLIBDIR64 =		$(LROOT)/usr/lib/reparse/$(MACH64)

LDLIBS +=	$(MACH_LDLIBS)
LDLIBS += -lc

CPPFLAGS += -D_REENTRANT

SRCS=   $(OBJS_COMMON:%.o=$(SRCDIR)/%.c)

pics/%.o:	$(SRC)/common/smbsrv/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

.KEEP_STATE:

all: $(LIBS)

lint: lintcheck

include ../../Makefile.targ
