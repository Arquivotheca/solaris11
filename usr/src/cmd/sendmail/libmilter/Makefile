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
# Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
#

include $(SRC)/lib/Makefile.lib

HDRS =		mfapi.h mfdef.h
HDRDIR =	../include/libmilter
ROOTHDRDIR =	$(ROOT)/usr/include/libmilter

SUBDIRS	=	$(MACH)

all := 		TARGET = all
clean :=	TARGET = clean
clobber :=	TARGET = clobber
install	:=	TARGET = install
lint :=		TARGET = lint

.KEEP_STATE:

all clean clobber lint: $(SUBDIRS)
install: $(SUBDIRS) $(ROOTHDRDIR)/README

install_h:	$(ROOTHDRS)

# Nothing in Solaris links to libmilter, so we do not need a stub.
# Provide NULL make rules to satisfy the parent makefiles.
stub:
stubinstall:

check:	$(CHECKHDRS)

$(ROOTHDRDIR)/README%:	README%
	$(INS.file)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

include $(SRC)/lib/Makefile.targ
