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
# Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#

LIBRARY= sata.a
VERS= .1

OBJECTS= cfga_sata.o cfga_rcm.o

# include library definitions
include ../../../Makefile.lib

SRCDIR =	../common
ROOTLIBDIR=	$(ROOT)/usr/lib/cfgadm
ROOTLIBDIR64=	$(ROOTLIBDIR)/$(MACH64)

LIBS=	$(DYNLIB)

LINTFLAGS +=	-DDEBUG
LINTFLAGS64 +=	-DDEBUG

CFLAGS +=	$(CCVERBOSE)
CFLAGS64 +=	$(CCVERBOSE)

LDLIBS +=	-lc -ldevice -ldevinfo -lrcm -lnvpair

.KEEP_STATE:

all:	$(LIBS)

lint:	lintcheck

# Install rules

$(ROOTLIBDIR)/%: % $(ROOTLIBDIR)
	$(INS.file)

$(ROOTLIBDIR64)/%: % $(ROOTLIBDIR64)
	$(INS.file)

$(ROOTLIBDIR) $(ROOTLIBDIR64):
	$(INS.dir)

# include library targets
include ../../../Makefile.targ

objs/%.o pics/%.o: ../common/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)
