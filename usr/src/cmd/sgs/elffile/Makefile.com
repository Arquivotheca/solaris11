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

PROG=		elffile

include		$(SRC)/cmd/Makefile.cmd
include		$(SRC)/cmd/sgs/Makefile.com

COMOBJ =	elffile.o

BLTOBJ =	msg.o

OBJS=		$(BLTOBJ) $(COMOBJ)

MAPFILE=	$(MAPFILE.NGB)
MAPOPT=		$(MAPFILE:%=-M%)

CPPFLAGS=	-I. -I../common -I../../include \
		$(CPPFLAGS.master) -I$(ELFCAP) -I$(SRC)/common/elf_file
LLDFLAGS =	$(VAR_ELFFILE_LLDFLAGS)
LLDFLAGS64 =	$(VAR_ELFFILE_LLDFLAGS64)
LDFLAGS +=	$(VERSREF) $(CC_USE_PROTO) $(MAPOPT) $(LLDFLAGS)
LDLIBS +=	-lelf $(CONV_LIB)

LINTFLAGS +=	-x
LINTFLAGS64 +=	-x

BLTDEFS =	msg.h
BLTDATA =	msg.c
BLTMESG =	$(SGSMSGDIR)/elffile

BLTFILES =	$(BLTDEFS) $(BLTDATA) $(BLTMESG)

SGSMSGCOM =	../common/elffile.msg
SGSMSGTARG =	$(SGSMSGCOM)
SGSMSGALL =	$(SGSMSGCOM)
SGSMSGFLAGS +=	-h $(BLTDEFS) -d $(BLTDATA) -m $(BLTMESG) -n elffile_msg

SRCS =		$(COMOBJ:%.o=../common/%.c) $(BLTDATA)
LINTSRCS =	$(SRCS) ../common/lintsup.c

CLEANFILES +=	$(LINTOUTS) $(BLTFILES)