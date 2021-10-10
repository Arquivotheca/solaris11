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

LIBRARY= libshadowfs.a
VERS= .1

OBJECTS= shadow_control.o	\
 	 shadow_errno.o		\
	 shadow_hash.o		\
	 shadow_list.o		\
	 shadow_migrate.o	\
	 shadow_open.o		\
	 shadow_pq.o		\
	 shadow_status.o	\
	 shadow_subr.o		\
	 shadow_conspiracy.o

include ../../Makefile.lib

LIBS=	$(DYNLIB) $(LINTLIB)

SRCDIR =	../common

INCS += -I$(SRCDIR)

C99MODE=	-xc99=%all
C99LMODE=	-Xc99=%all
LDLIBS +=	-lc -lm -lscf -lzfs
CPPFLAGS +=	$(INCS) -D_REENTRANT
CFLAGS +=	$(CCVERBOSE) $(C_BIGPICFLAGS)
CFLAGS64 +=	$(CCVERBOSE) $(C_BIGPICFLAGS)

$(LINTLIB) := SRCS=	$(SRCDIR)/$(LINTSRC)

#
# On SPARC, gcc emits DWARF assembler directives for TLS data that are not
# understood by the Sun assembler.  Until this problem is fixed, we turn down
# the amount of generated debugging information, which seems to do the trick.
#
$(SPARC_BLD)CTF_FLAGS += -_gcc=-g1

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

include ../../Makefile.targ
