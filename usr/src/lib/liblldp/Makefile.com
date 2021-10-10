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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY = liblldp.a
VERS    = .1
OBJECTS = liblldp.o liblldp_tlv.o liblldp_prop.o

include ../../Makefile.lib

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-lnsl -lc -lscf -lnvpair

SRCDIR =	../common
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-I$(SRCDIR) -D_REENTRANT

.KEEP_STATE:

all:		stub $(LIBS)

lint:		lintcheck

include $(SRC)/lib/Makefile.targ
