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
# Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
#
# cmd/sgs/nm/sparcv9/Makefile
#

PROG=		nm

include 	../../../Makefile.cmd
include 	../../Makefile.com

COMOBJS=	nm.o

OBJS=		$(COMOBJS)

SRCS=		$(COMOBJS:%.o=../common/%.c)

CPPFLAGS +=	-I../../include -I../../include/sparc -I$(ELFCAP)
LDFLAGS +=	$(VAR_LLDFLAGS64)
LDLIBS +=	$(CONV_LIB) -lelf
LINTFLAGS64 +=	-x $(LDLIBS) -m64
LINTSRCS=	$(SRCS)
CLEANFILES +=	$(LINTOUTS)

%.o:		../common/%.c
		$(COMPILE.c) $<

.KEEP_STATE:

all:		$(PROG)

$(PROG):	$(OBJS)
		$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

package \
install:	all $(VAR_SGSBINPROG64)

clean:
		$(RM) $(OBJS) $(CLEANFILES)

lint:		$(LINTOUT64) $(SGSLINTOUT)

include		../../Makefile.targ
include		../../../Makefile.targ
include		../../Makefile.sub.64
