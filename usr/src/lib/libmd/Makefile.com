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
# Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
#

#
#	Add FIPS-140 Checksum
#
POST_PROCESS_SO +=	; $(FIPS140_CHECKSUM)

LIBS =		$(DYNLIB) $(LINTLIB)
SRCS =		$(COMDIR)/md4/md4.c \
		$(COMDIR)/md5/md5.c \
		$(COMDIR)/sha1/sha1.c \
		$(COMDIR)/sha2/sha2.c

COMDIR =	$(SRC)/common/crypto
SRCDIR =	../common
MAPFILEDIR =	$(SRCDIR)

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-I$(SRCDIR)
LDLIBS +=	-lc

$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)
