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
# Copyright (c) 1991, 2011, Oracle and/or its affiliates. All rights reserved.
#
# lib/nsswitch/Makefile


include $(SRC)/Makefile.master
include $(SRC)/lib/Makefile.lib

MANIFEST=	switch.xml
MANIFESTDIR=	$(ROOT)/lib/svc/manifest/system/name-service
ROOTMANIFEST=	$(MANIFEST:%=$(MANIFESTDIR)/%)
$(ROOTMANIFEST) :=	FILEMODE = 0444

SVCMETHOD=	ns-switch
SVCMETHODDIR=	$(ROOT)/lib/svc/method
ROOTSVCMETHOD=	$(SVCMETHOD:%=$(SVCMETHODDIR)/%)
$(ROOTSVCMETHOD) :=	FILEMODE = 0555

SUBDIRS= files nis compat dns ldap user mdns ad pynss
STUBSUBDIRS = pynss

all:=		TARGET= all
clean:=		TARGET= clean
clobber:=	TARGET= clobber
install:=	TARGET= install
stub :=		TARGET= stub
stubinstall :=	TARGET= stubinstall
lint:=		TARGET= lint

# nsswitch objects are not linked to via the link-editor, and stubs are not
# needed. Provide NULL make rules to satisfy the parent makefiles.
stub: $(STUBSUBDIRS)
stubinstall: $(STUBSUBDIRS)

.KEEP_STATE:

all clean clobber lint: $(SUBDIRS)

install: $(SUBDIRS) $(ROOTMANIFEST) $(ROOTSVCMETHOD)

$(ROOTMANIFEST): $(MANIFESTDIR)

$(MANIFESTDIR):
	$(INS.dir)

$(MANIFESTDIR)/%: %
	$(INS.file)                                                             

$(ROOTSVCMETHOD): $(SVCMETHODDIR)

$(SVCMETHODDIR):
	$(INS.dir) 

$(SVCMETHODDIR)/%: %
	$(INS.file)

$(SUBDIRS): FRC
	@cd $@; $(MAKE) $(TARGET)

FRC:
