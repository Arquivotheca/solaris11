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
# Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY =	libsoftcrypto.a
VERS =		.1

include		../Makefile.com

LIBS =		$(DYNLIB) $(LINTLIB)

# MPI library needs this for 32-bit compilation only
MPI_FLAGS +=	-DMP_USE_UINT_DIGIT

# Symbol capabilities objects are added for i386.
EXTPICS =	../capabilities/intel/i386/pics/symcap.o

.KEEP_STATE:

all:		stub $(LIBS)

lint:		$$(SRCS)

install:	stubinstall all $(ROOTLIBS) $(ROOTLINKS) $(ROOTLINT)

stubinstall:	stub $(STUBROOTLIBS) $(STUBROOTLINKS)

include		../Makefile.targ
