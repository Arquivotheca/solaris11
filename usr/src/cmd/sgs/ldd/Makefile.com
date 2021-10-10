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
# Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG=		ldd

include		$(SRC)/cmd/Makefile.cmd
include		$(SRC)/cmd/sgs/Makefile.com

COMOBJ=		ldd.o
BLTOBJ=		msg.o
TOOLSOBJ=	alist.o

OBJS=		$(BLTOBJ) $(COMOBJ) $(TOOLSOBJ)

MAPFILE=	$(MAPFILE.NGB)
MAPOPTS=	$(MAPFILE:%=-M%)

CPPFLAGS +=	-I. -I../../include -I../../include/$(MACH) \
		-I$(SRCBASE)/uts/$(ARCH)/sys \
		$(CPPFLAGS.master)
LLDFLAGS =	'-R$$ORIGIN/../../lib'
LLDFLAGS64 =	'-R$$ORIGIN/../../../lib/$(MACH64)'
LDFLAGS +=	$(VERSREF) $(CC_USE_PROTO) $(MAPOPTS) $(LLDFLAGS)
LDLIBS +=	$(CONV_LIB) -lelf
LINTFLAGS +=	-x
LINTFLAGS64 +=	-x

BLTDEFS=        msg.h
BLTDATA=        msg.c
BLTMESG=        $(SGSMSGDIR)/ldd

BLTFILES=	$(BLTDEFS) $(BLTDATA) $(BLTMESG)

SGSMSGCOM=	../common/ldd.msg
SGSMSGTARG=	$(SGSMSGCOM)
SGSMSGALL=	$(SGSMSGCOM)
SGSMSGFLAGS +=	-h $(BLTDEFS) -d $(BLTDATA) -m $(BLTMESG) -n ldd_msg

SRCS=		$(COMOBJ:%.o=../common/%.c) $(BLTDATA) \
		$(TOOLSOBJ:%.o=$(SGSTOOLS)/common/%.c)
LINTSRCS=	$(SRCS) ../common/lintsup.c

CLEANFILES +=	$(LINTOUTS) $(BLTFILES)
