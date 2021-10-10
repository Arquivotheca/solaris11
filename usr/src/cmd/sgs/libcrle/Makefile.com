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
# Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY=	libcrle.a
VERS=		.1

COMOBJS=	audit.o		dump.o		util.o
BLTOBJ=		msg.o

OBJECTS=	$(BLTOBJ)  $(COMOBJS)


include		$(SRC)/lib/Makefile.lib
include		$(SRC)/cmd/sgs/Makefile.com

SRCDIR =	../common

lint :=		ZRECORD =
LDLIBS +=	$(ZRECORD) -lmapmalloc $(CONV_LIB) -lc

#
# We link to libmapmalloc as an interposer for the benefit of the
# alternative link map. As libcrle does not call it directly, ld
# will see it as unused, so we need to disable unused dependency guidance.
#
ZGUIDANCE =     $(ZGUIDANCE_NOUNUSED)

LINTFLAGS +=	-u
LINTFLAGS64 +=	-u

CPPFLAGS +=	-I$(SRCBASE)/lib/libc/inc -I$(SRC)/common/sgsrtcid
DYNFLAGS +=	$(VERSREF) $(CC_USE_PROTO)


BLTDEFS=	msg.h
BLTDATA=	msg.c
BLTMESG=	$(SGSMSGDIR)/libcrle

BLTFILES=	$(BLTDEFS) $(BLTDATA) $(BLTMESG)

SGSMSGCOM=	../common/libcrle.msg
SGSMSGALL=	$(SGSMSGCOM)
SGSMSGTARG=	$(SGSMSGCOM)
SGSMSGFLAGS +=	-h $(BLTDEFS) -d $(BLTDATA) -m $(BLTMESG) -n libcrle_msg

LIBSRCS=	$(COMOBJS:%.o=../common/%.c)  $(BLTDATA)
LINTSRCS=	$(LIBSRCS)

CLEANFILES +=	$(LINTOUTS) $(BLTFILES)
CLOBBERFILES +=	$(DYNLIB) $(LINTLIB) $(LIBLINKS)

ROOTDYNLIB=	$(DYNLIB:%=$(ROOTLIBDIR)/%)
