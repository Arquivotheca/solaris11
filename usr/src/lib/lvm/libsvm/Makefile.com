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
# Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY=      	libsvm.a 
VERS=          	.1 
OBJECTS=	check_svm.o \
		getdrvname.o \
		metaconf.o \
		metainterfaces.o \
		modops.o \
		start_svm.o \
		debug.o \
		update_mdconf.o

include $(SRC)/lib/lvm/Makefile.lvm

ROOTLIBDIR=	$(ROOT)/usr/snadm/lib
STUBROOTLIBDIR=	$(STUBROOT)/usr/snadm/lib
LROOTLIBDIR=	$(LROOT)/usr/snadm/lib

#
# XXX There isn't a lint library for libspmicommon.  For now, we work
# around this by only using the library when we build (as opposed to lint).
#
# A related issue: libspmicommon relies on libsocket and libnsl, but does
# not specify them as dependencies. Instead, it links to libwanboot, which
# brings them in. In a stub object environment, libwanboot does not
# include the NEEDED entires for libsocket/libnsl, which causes the
# link of libsvm to fail. To address this, we must link libsvm against
# those objects, and disable the ZDEFS guidance checking. This can be removed
# once libspmicommon is fixed:
#    7009024 libspmicommon.so should be linked with libsocket and libnsl
#
all debug install := LDLIBS_LIBSPMICOMMON += -L/usr/snadm/lib -lspmicommon \
			-lsocket -lnsl
ZGUIDANCE = $(ZGUIDANCE_NOUNUSED)

LIBS =		$(DYNLIB) # don't build a static lib
LDLIBS +=	-lmeta -ldevid $(LDLIBS_LIBSPMICOMMON) -lc

DYNFLAGS +=	-R/usr/snadm/lib
CPPFLAGS +=	-D_FILE_OFFSET_BITS=64
CPPFLAGS +=	-I$(SRC)/lib/lvm/libsvm/common/hdrs
ZDEFS =

.KEEP_STATE:

all: stub $(LIBS)

include $(SRC)/lib/lvm/Makefile.targ
