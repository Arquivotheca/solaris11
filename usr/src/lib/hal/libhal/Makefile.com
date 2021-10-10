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
# usr/src/lib/hal/libhal/Makefile.com
#

LIBRARY =	libhal.a
VERS =		.1.0.0
VERS_MAJ =	.1
OBJECTS =	libhal.o
LIBPCSRC =	hal.pc

include ../../Makefile.com

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-lc -ldbus-1
$(LINTLIB) := 	SRCS = $(SRCDIR)/$(LINTSRC)

SRCDIR =	../common

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	$(HAL_CONFIG_CPPFLAGS)
CPPFLAGS +=	-DGETTEXT_PACKAGE=\"$(HAL_GETTEXT_PACKAGE)\" -DENABLE_NLS

ROOTMAJLINK =	$(ROOTLIBDIR)/$(LIBRARY:.a=.so)$(VERS_MAJ)
ROOTMAJLINK64 =	$(ROOTLIBDIR64)/$(LIBRARY:.a=.so)$(VERS_MAJ)
STUBROOTMAJLINK =	$(STUBROOTLIBDIR)/$(LIBRARY:.a=.so)$(VERS_MAJ)
STUBROOTMAJLINK64 =	$(STUBROOTLIBDIR64)/$(LIBRARY:.a=.so)$(VERS_MAJ)

.KEEP_STATE:

all:		stub $(LIBS)

$(ROOTMAJLINK) $(STUBROOTMAJLINK):
	-$(RM) $@; $(SYMLINK) $(DYNLIB) $@

$(ROOTMAJLINK64) $(STUBROOTMAJLINK64):
	-$(RM) $@; $(SYMLINK) $(DYNLIB) $@

include $(SRC)/lib/Makefile.targ
