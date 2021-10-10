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
# Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
#
# lib/libelfsign/Makefile
#

include $(SRC)/lib/Makefile.lib

SUBDIRS=	$(MACH)

POFILE=		libelfsign.po
MSGFILES=	common/elfsignlib.c common/elfcertlib.c
XGETFLAGS=	-a

HDRS=		libelfsign.h
ROOTHDRDIR=	$(ROOT)/usr/include
HDRDIR=		common

all:=		TARGET= all
clean:=		TARGET= clean
clobber:=	TARGET= clobber
install:=	TARGET= install
lint:=		TARGET= lint
stub :=		TARGET = stub
stubinstall :=	TARGET = stubinstall

.KEEP_STATE:

all clean clobber install stub stubinstall: .WAIT $(SUBDIRS)

install_h:	$(ROOTHDRS)

check:		$(CHECKHDRS)

lint:	$(SUBDIRS)

$(SUBDIRS):	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

$(POFILE): $(MSGFILES)
	$(BUILDPO.msgfiles)

_msg: $(MSGDOMAINPOFILE)

include $(SRC)/Makefile.msg.targ

FRC:

include $(SRC)/lib/Makefile.targ
