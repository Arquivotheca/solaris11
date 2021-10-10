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
# Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG= init 
ROOTFS_PROG= $(PROG)

DEFAULTFILES= init.dfl

MANIFEST =      environment.xml
SVCMETHOD =     svc-initial-environment

include ../Makefile.cmd

ROOTMANIFESTDIR = $(ROOTSVCSYSTEM)

LDLIBS += -lpam -lbsm -lcontract -lscf
LINTFLAGS += -erroff=E_NAME_USED_NOT_DEF2
CLOBBERFILES= $(STATIC)

.KEEP_STATE:

ROOTETCDEFAULTINIT=	$(DEFAULTFILES:%.dfl=$(ROOTETCDEFAULT)/%)
$(ROOTETCDEFAULTINIT) :=	FILEMODE = 0444

all: $(ROOTFS_PROG)

install: all $(ROOTETCDEFAULTINIT) $(ROOTUSRSBINPROG) $(ROOTMANIFEST) $(ROOTSVCMETHOD)
	$(RM) $(ROOTETC)/TIMEZONE
	cd $(ROOTETC); $(SYMLINK) default/init TIMEZONE

check:	$(CHKMANIFEST)

test:
	rtest $(PROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
