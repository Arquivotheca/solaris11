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
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#
#

LIBRARY =	libidmap.a
VERS =		.1
LINT_OBJECTS =	\
	directory_client.o	\
	directory_error.o	\
	directory_helper.o	\
	directory_rpc_clnt.o	\
	sidutil.o		\
	sized_array.o		\
	idmap_api.o		\
	idmap_api_nfsv4.o	\
	idmap_cache.o		\
	utils.o

OBJECTS = $(LINT_OBJECTS)	\
	idmap_xdr.o

include ../../Makefile.lib
C99MODE = $(C99_ENABLE)

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-lc -lavl -lnsl -lnvpair -luutil

SRCDIR =	../common
$(LINTLIB):=	SRCS = $(SRCDIR)/$(LINTSRC)

IDMAP_PROT_X =		$(SRC)/uts/common/rpcsvc/idmap_prot.x

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-D_REENTRANT -I$(SRCDIR)

CLOBBERFILES +=	idmap_xdr.c

lint := OBJECTS = $(LINT_OBJECTS)

.KEEP_STATE:

all: stub $(LIBS)

idmap_xdr.c:	$(IDMAP_PROT_X)
	$(RM) $@; $(RPCGEN) -CMNc -o $@ $(IDMAP_PROT_X)

lint: lintcheck

LINTFLAGS += -erroff=E_CONSTANT_CONDITION
LINTFLAGS64 += -erroff=E_CONSTANT_CONDITION

include ../../Makefile.targ
