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
# Copyright (c) 1995, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY=	librtld.a
VERS=		.1

MACHOBJS=	_relocate.o
COMOBJS=	dldump.o	dynamic.o	relocate.o	syms.o \
		util.o
BLTOBJ=		msg.o

OBJECTS=	$(BLTOBJ)  $(MACHOBJS)  $(COMOBJS)


include		$(SRC)/lib/Makefile.lib
include		$(SRC)/cmd/sgs/Makefile.com

SRCDIR =	../common
CPPFLAGS +=	-I../../rtld/common -I$(SRCBASE)/lib/libc/inc \
		-I$(SRCBASE)/uts/common/krtld -I$(SRC)/common/sgsrtcid \
		-I$(SRCBASE)/uts/sparc
DYNFLAGS +=	$(VERSREF) '-R$$ORIGIN'

$(DYNLIB) :=		DYNFLAGS +=	$(CC_USE_PROTO)
stubs/$(DYNLIB) :=	LD=$(LD_USE_PROTO)ld

LDLIBS +=	$(CONV_LIB) -lelf -lc

LINTFLAGS +=	-u -erroff=E_NAME_DECL_NOT_USED_DEF2
LINTFLAGS64 +=	-u -erroff=E_NAME_DECL_NOT_USED_DEF2

BLTDEFS=	msg.h
BLTDATA=	msg.c
BLTMESG=	$(SGSMSGDIR)/librtld

BLTFILES=	$(BLTDEFS) $(BLTDATA) $(BLTMESG)

SGSMSGCOM=	../common/librtld.msg
SGSMSGALL=	$(SGSMSGCOM)
SGSMSGTARG=	$(SGSMSGCOM)
SGSMSGFLAGS +=	-h $(BLTDEFS) -d $(BLTDATA) -m $(BLTMESG) -n librtld_msg

SRCS=		../common/llib-lrtld
LINTSRCS=	$(MACHOBJS:%.o=%.c)  $(COMOBJS:%.o=../common/%.c) \
		$(BLTDATA)

# The lint library is not delivered with the product,
# so we install it in the stub proto.
STUBROOTFS_LINTLIB =	$(STUBROOTFS_LIBDIR)/$(LINTLIB)
STUBROOTFS_LINTLIB64 =	$(STUBROOTFS_LIBDIR64)/$(LINTLIB)

CLEANFILES +=	$(BLTFILES) $(LINTOUTS)
CLOBBERFILES +=	$(DYNLIB) $(LINTLIB) $(LIBLINKS)

ROOTFS_DYNLIB=	$(DYNLIB:%=$(ROOTFS_LIBDIR)/%)
