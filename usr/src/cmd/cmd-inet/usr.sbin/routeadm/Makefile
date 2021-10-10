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
# Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
#

SVCMETHOD=	svc-legacy-routing
MANIFEST=	legacy-routing.xml

PROG=		routeadm
ROOTFS_PROG=	$(PROG)

include ../../../Makefile.cmd

ROOTSVCROUTINGMANIFESTS=	$(ROOTSVCNETWORKROUTING)/$(MANIFEST)

include ../../Makefile.cmd-inet

$(ROOTSVCROUTINGMANIFESTS)	:=	FILEMODE= 444

CPPFLAGS +=	-I../../../../common/svc -I$(CMDINETCOMMONDIR)
$(NOT_RELEASE_BUILD)CPPFLAGS +=	-DDEBUG

LDLIBS +=		-lscf -luutil -lsocket -lipadm

ROOTUSRSBINPROG=	$(ROOTFS_PROG:%=$(ROOTUSRSBIN)/%)

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTUSRSBINPROG) $(ROOTSVCNETWORKROUTING) \
	$(ROOTSVCROUTINGMANIFESTS) $(ROOTSVCMETHOD)

check: 	$(CHKMANIFEST)

clean:

lint: lint_PROG

$(ROOTSVCNETWORKROUTING):
	$(INS.dir)

$(ROOTSVCNETWORKROUTING)/%: %
	$(INS.file)

include ../../../Makefile.targ
