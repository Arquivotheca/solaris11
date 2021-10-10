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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

#
# The DTrace Toolkit is third-party open source licensed using the
# CDDL.  As such, the license should be passed through as-is.
#
INS_STRIP_CDDL_OVR= $(POUND_SIGN)

include ../Makefile.cmd

DTTDIRS:sh = (cd DTT; find . -type d)
DTTEXECS:sh = (cd DTT; find . -type f -perm -a+x)
DTTFILES:sh = (cd DTT; find . -type f ! -perm -a+x)

LICENSEFILES = license.copyright DTT/Docs/cddl1.txt
EXECATTRDFILES = exec_attr.developer-dtrace-toolkit
PROFATTRDFILES = prof_attr.developer-dtrace-toolkit
DOCLINKS = Docs/Examples Docs/Notes

ROOTDTTDIR = $(ROOT)/usr/dtrace/DTT
ROOTDTTDIRS = $(DTTDIRS:%=$(ROOTDTTDIR)/%)
ROOTDTTEXECS = $(DTTEXECS:%=$(ROOTDTTDIR)/%)
ROOTDTTFILES = $(DTTFILES:%=$(ROOTDTTDIR)/%)
ROOTDTTDOCLINKS = $(DOCLINKS:%=$(ROOTDTTDIR)/%)

all: THIRDPARTYLICENSE
        
install: all THIRDPARTYLICENSE $(ROOTDTTDIR) $(ROOTDTTDIRS) $(ROOTDTTEXECS) \
    $(ROOTDTTFILES) dttlinks $(ROOTDTTDOCLINKS) $(ROOTEXECATTRDFILES) \
    $(ROOTPROFATTRDFILES)

clobber clean:
	$(RM) THIRDPARTYLICENSE

clean.lint install_h lint:

THIRDPARTYLICENSE: $(LICENSEFILES)
	$(CAT) $(LICENSEFILES) > $@

dttlinks:
	cd DTT; find . -type l | /usr/bin/cpio -pmu $(ROOTDTTDIR)

$(ROOTDTTDOCLINKS):
	$(LN) -s ../$(@F) $@

$(ROOTLICENSE): THIRDPARTYLICENSE

$(ROOTDTTEXECS) $(ROOTDTTFILES):
	$(INS.file)

$(ROOTDTTDIRS):
	$(INS.dir)

$(ROOTEXECATTRDFILES) $(ROOTPROFATTRDFILES) := FILEMODE = 0444

$(ROOTDTTEXECS) := FILEMODE = 0755

$(ROOTDTTFILES) := FILEMODE = 0644

$(ROOTDTTEXECS) $(ROOTDTTFILES) := INS.file = $(RM) $@; $(INS) -s -m $(FILEMODE) -f $(@D) $(@:$(ROOTDTTDIR)/%=DTT/%)

