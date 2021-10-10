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

OLIBRARY =	libmp.so.1

include ../Makefile.com

# For libmp.so.1

CLEANFILES +=	pics/pics_mp.o
CLOBBERFILES += $(OLIBRARY)

# permissions for old library
$(ROOTLIBDIR)/$(OLIBRARY) :=	FILEMODE= 755

.KEEP_STATE:

OLIBFLAGS =	-h $(OLIBRARY) $(DYNFLAGS.lib) -xnolib $(OMAPFILE:%=-M%)
$(OLIBRARY):	pics/pics_mp.o libmp.so.2 $(OMAPFILE)
	$(CC) -o $@ $(GSHARED) $(OLIBFLAGS) pics/pics_mp.o libmp.so.2
	$(POST_PROCESS_SO)

pics/pics_mp.o:
	$(COMPILE.c) -o $@ $(C_PICFLAGS) ../common/old_mp.c
	$(POST_PROCESS_O)

all: stub $(LIBS) $(OLIBRARY)

install: stubinstall all $(ROOTLIBS) $(ROOTLINKS) $(ROOTLIBDIR)/$(OLIBRARY)

stubinstall: stub $(STUBROOTLIBS) $(STUBROOTLINKS)
