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
# Copyright (c) 2000, 2011, Oracle and/or its affiliates. All rights reserved.
#

include ../Makefile.com
include $(SRC)/lib/Makefile.lib.64

LDLIBS += -lfruutils
LDLIBS += -L$(SRC)/lib/libfru/$(MACH64) -lfru
LDLIBS += -lc -lpicl

.KEEP_STATE:

all: $(ROOTLIBDIR64) $(LIBS) $(LIBLINKS)

$(LIBLINKS):    FRC
	$(RM) $@; $(SYMLINK) $(DYNLIB) $@

$(ROOTLIBDIR64) $(STUBROOTLIBDIR64):
	$(INS.dir)

install: stubinstall all $(ROOTLIBS64) $(ROOTLINKS64)

stubinstall: stub $(STUBROOTLIBS64) $(STUBROOTLINKS64)

FRC:
