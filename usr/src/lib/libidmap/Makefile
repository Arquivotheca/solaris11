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
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#

include $(SRC)/lib/Makefile.lib

HDRS =		idmap.h directory.h
HDRDIR =	common

SUBDIRS	=	$(MACH)
$(BUILD64)SUBDIRS += $(MACH64)

POFILE =	libidmap.po
MSGFILES:sh =	echo common/*.[ch]
XGETTEXT =	$(GNUXGETTEXT)
XGETFLAGS =	--foreign-user --strict -n -E --width=72 \
		--omit-header --keyword=directoryError:2 \
		--language=C --force-po

all := 		TARGET = all
clean :=	TARGET = clean
clobber :=	TARGET = clobber
install	:=	TARGET = install
lint :=		TARGET = lint
stub :=		TARGET = stub
stubinstall :=	TARGET = stubinstall

.KEEP_STATE:

all clean clobber install lint stub stubinstall: $(SUBDIRS)

install_h:	$(ROOTHDRS)

check:		$(CHECKHDRS)

$(POFILE):	$(MSGFILES)
		$(BUILDPO.msgfiles)

_msg:		$(MSGDOMAINPOFILE)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

include $(SRC)/Makefile.msg.targ
include $(SRC)/lib/Makefile.targ
