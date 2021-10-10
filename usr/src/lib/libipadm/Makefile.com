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
#

LIBRARY = libipadm.a
VERS    = .1
OBJECTS = libipadm.o ipadm_prop.o ipadm_persist.o ipadm_addr.o ipadm_if.o \
	  ipadm_ndpd.o ipadm_ngz.o ipadm_cong.o

include ../../Makefile.lib

# install this library in the root filesystem
include ../../Makefile.rootfs

UTSBASE =	$(SRC)/uts
CPPFLAGS += -I$(UTSBASE)/common/brand/solaris10

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-lc -lnsl -linetutil -lsocket -ldlpi -lnvpair \
	        -lcmdutils -ldhcpagent -ldhcputil -ldladm \
		-lscf -luutil

SRCDIR =	../common
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-I$(SRCDIR) -D_REENTRANT

.KEEP_STATE:

all:		stub $(LIBS)

lint:		lintcheck

include $(SRC)/lib/Makefile.targ
