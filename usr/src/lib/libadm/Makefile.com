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
# Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY=	libadm.a
VERS=		.1

OBJECTS= \
ckdate.o     ckgid.o      ckint.o      ckitem.o     ckkeywd.o    ckpath.o  \
ckrange.o    ckstr.o      cktime.o     ckuid.o      ckyorn.o \
devattr.o    getinput.o   getvol.o     pkginfo.o  \
pkgnmchk.o   pkgparam.o   puterror.o   puthelp.o  \
putprmpt.o   puttext.o    rdwr_vtoc.o  regexp.o     space.o      fulldevnm.o

include	$(SRC)/lib/Makefile.lib

# install this library in the root filesystem
include ../../Makefile.rootfs

LIBS=		$(DYNLIB) $(LINTLIB)
SRCDIR=		../common

CPPFLAGS +=	-I ../inc

$(LINTLIB) :=	SRCS=$(SRCDIR)/$(LINTSRC)
LDLIBS +=	-lc

.KEEP_STATE:

all: stub $(LIBS)

lint:	lintcheck

include		../../Makefile.targ
