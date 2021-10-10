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
# Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
#
#

LIBRARY=	audit_remote.a
VERS=		.1
OBJECTS=	audit_remote.o transport.o

LIBBSM=		$(SRC)/lib/libbsm/common

include		$(SRC)/lib/Makefile.lib

LIBS=		$(DYNLIB)
LDLIBS		+= -lbsm -lnsl -lsocket -lgss -lc

CFLAGS		+= $(CCVERBOSE)
CPPFLAGS	+= -D_REENTRANT -I$(LIBBSM)
CPPFLAGS	+= -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64

ROOTLIBDIR=	$(ROOT)/usr/lib/security

.KEEP_STATE:

all:	$(LIBS)

lint:	lintcheck

include		../../../Makefile.targ
