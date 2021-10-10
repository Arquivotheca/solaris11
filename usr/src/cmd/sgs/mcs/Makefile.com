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
# Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG=		mcs
STRIPFILE=	strip

ROOTLINKS=	$(VAR_SGSBIN)/$(STRIPFILE)

include		$(SRC)/cmd/Makefile.cmd
include		$(SRC)/cmd/sgs/Makefile.com

# avoid bootstrap problems
MCS =		/usr/bin/mcs

COMOBJS =	main.o		file.o		utils.o		global.o \
		message.o
TOOLSOBJS =	alist.o

OBJS =		$(COMOBJS) $(TOOLSOBJS)

LLDFLAGS =	$(VAR_LLDFLAGS)
LLDFLAGS64 =	$(VAR_LLDFLAGS64)
LDFLAGS +=	$(VERSREF) $(LLDFLAGS)
LDLIBS +=	$(CONV_LIB) -lelf

LINTFLAGS +=	-x
LINTFLAGS64 +=	-x

SRCS =		$(COMOBJS:%.o=../common/%.c) \
		$(TOOLSOBJS:%.o=$(SGSTOOLS)/common/%.c)
LINTSRCS =	$(SRCS) ../common/lintsup.c

CLEANFILES +=	$(OBJS) $(LINTOUTS)
