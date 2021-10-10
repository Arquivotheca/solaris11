#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.

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
# cmd/sgs/prof/Makefile.com
#

PROG=		prof

include 	$(SRC)/cmd/Makefile.cmd
include 	$(SRC)/cmd/sgs/Makefile.com

COMOBJS=	prof.o profv.o lookup.o rdelf.o \
		symintOpen.o symintClose.o symintUtil.o symintErr.o symintLoad.o

OBJS=		$(COMOBJS)

SRCS=		$(COMOBJS:%.o=../common/%.c)

INCLIST=	-I../common -I../../include -I../../include/$(MACH)

CPPFLAGS=	$(INCLIST) $(DEFLIST) $(CPPFLAGS.master) -I$(ELFCAP)
CFLAGS +=	$(CCVERBOSE)
C99MODE=	$(C99_ENABLE)
LDLIBS +=	$(CONV_LIB) -lelf
LINTSRCS =	$(SRCS)
LINTFLAGS +=	-x
CLEANFILES +=	$(LINTOUTS)

%.o:		../common/%.c
		$(COMPILE.c) $<
