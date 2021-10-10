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

MANIFEST=	upgrade.xml
SVCMETHOD=	nss-upgrade

include ../Makefile.cmd

ROOTMANIFESTDIR=	$(ROOTSVCSYSTEMNAMESERVICE)

FILEMODE=0555

PYTHON=         $(PYTHON_26)

PYCMD=          nscfg.py
PYCMDFILE=      $(PYCMD:%.py=%)

ROOTCMDFILE=    $(PYCMDFILE:%=$(ROOTUSRSBIN)/%)
ROOTMODFILES=	$(PYMODFILES:%=$(ROOTMODDIR)/%)

POFILE = 	nscfg.po
MSGFILES = 	nscfg.py
XGETTEXT = 	$(GNUXGETTEXT)
XGETFLAGS = 	$(GNUXGETFLAGS)

.KEEP_STATE:

all: $(PYCMDFILE)

install: all $(ROOTCMDFILE) $(ROOTMANIFEST) $(ROOTSVCMETHOD)

check:	$(CHKMANIFEST)

clean:
	$(RM) $(PYCMDFILE)

clobber: clean

$(ROOTCMDDIR)/%: %
	$(INS.pyfile)

$(POFILE): 	$(MSGFILES)
		$(BUILDPO.msgfiles)

_msg: 		$(MSGDOMAINPOFILE)

include $(SRC)/Makefile.msg.targ
include ../Makefile.targ
