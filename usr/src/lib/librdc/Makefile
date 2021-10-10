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
# lib/librdc/Makefile

include	    ../Makefile.lib

HDRS=		librdc.h rdcerr.h rdcrules.h
HDRDIR=		common

SUBDIRS=	$(MACH)

ROOTHDRDIR= $(ROOT)/usr/include/sys/nsctl

ROOTHDRS= $(HDRS:%=$(ROOTHDRDIR)/%)

all :=		TARGET= all
clean :=	TARGET= clean
clobber :=	TARGET= clobber
install :=	TARGET= install
lint :=		TARGET= lint
stub :=		TARGET = stub
stubinstall :=	TARGET = stubinstall

.KEEP_STATE:

all clean clobber install lint stub stubinstall: $(SUBDIRS)

$(MACH):	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

install_h: $(ROOTHDRDIR) $(ROOTHDRS)

$(ROOTHDRDIR)/%:	common/%
	$(INS.file)

$(ROOTHDRDIR):
	$(INS.dir)

check:  $(CHECKHDRS)

FRC:
