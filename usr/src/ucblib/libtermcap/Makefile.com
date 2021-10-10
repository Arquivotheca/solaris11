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

LIBRARY=	libtermcap.a
VERS=		.1

OBJECTS= termcap.o	\
	 tgoto.o	\
	 tputs.o

# include library definitions
include $(SRC)/lib/Makefile.lib

ROOTLIBDIR=	$(ROOT)/usr/ucblib
ROOTLIBDIR64=	$(ROOT)/usr/ucblib/$(MACH64)

STUBROOTLIBDIR=		$(STUBROOT)/usr/ucblib
STUBROOTLIBDIR64=	$(STUBROOT)/usr/ucblib/$(MACH64)

LROOTLIBDIR=		$(LROOT)/usr/ucblib
LROOTLIBDIR64=		$(LROOT)/usr/ucblib/$(MACH64)

LIBS = $(DYNLIB)

CFLAGS	+=	$(CCVERBOSE)
CFLAGS64 +=	$(CCVERBOSE)
LDLIBS +=	-lc

DEFS= -DCM_N -DCM_GT -DCM_B -DCM_D
CPPFLAGS = $(DEFS) -I$(SRC)/ucbhead $(CPPFLAGS.master)

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

#
# Include library targets
#
include $(SRC)/lib/Makefile.targ

objs/%.o pics/%.o: ../%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)
