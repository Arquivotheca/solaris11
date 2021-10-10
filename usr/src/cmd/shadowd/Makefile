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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

SUBDIRS = $(MACH) test

include ../Makefile.cmd

POFILE=shadowd.po
MSGFILES= shadowd.c

MANIFEST=		shadowd.xml
ROOTMANIFESTDIR=	$(ROOTSVCSYSTEMFILESYSTEM)
$(ROOTMANIFEST) := FILEMODE = 444

.KEEP_STATE:

all := TARGET += all
install := TARGET += install
clean := TARGET += clean
clobber := TARGET += clobber
check := TARGET += check
lint := TARGET += lint

all: $(MANIFEST) $(SUBDIRS)

install: all $(ROOTMANIFEST)

$(POFILE):	pofile_MSGFILES

clean: $(SUBDIRS)
	$(RM) $(ROOTMANIFEST)

clobber: clean $(SUBDIRS)
	$(RM) $(POFILE)

check lint: $(SUBDIRS)

_msg: $(MSGDOMAINPOFILE)

$(SUBDIRS): FRC
	@cd $@; pwd; VERSION='$(VERSION)' $(MAKE) $(TARGET)

FRC:

include ../../Makefile.msg.targ
