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
# Copyright (c) 1990, 2011, Oracle and/or its affiliates. All rights reserved.
#
#

include ../Makefile.cmd

SUBDIRS= lib group user

POFILES=	$(SUBDIRS:%=%/%.po)
POFILE=		oamuser.po

all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint
_msg:=		TARGET= _msg

.KEEP_STATE:

all install clean clobber lint: $(SUBDIRS)

_msg:
	$(RM) $(POFILE)
	$(CAT) $(POFILES)  > $(POFILE)

# the build of the $(DCFILE) should be defined locally
# its .dc extension gets renamed to .po upon installation
#
_dc: $(DCMSGDOMAIN) $(DCFILE)
	$(RM) $(DCMSGDOMAIN)/$(DCFILE)
	$(CP) $(DCFILE) $(DCMSGDOMAIN)/$(DCFILE:.dc=.po)

$(MSGDOMAIN) $(DCMSGDOMAIN):
	$(INS.dir)


$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:


include ../../Makefile.msg.targ
