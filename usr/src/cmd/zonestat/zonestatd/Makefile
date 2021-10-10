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
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#

MANIFEST =	zonestat.xml
SVCMETHOD =	svc-zonestat
PROG =		zonestatd

include ../../Makefile.cmd
include ../../Makefile.ctf

ROOTCMDDIR =		$(ROOTLIB)/zones
ROOTMANIFESTDIR =	$(ROOTSVCSYSTEM)

SRCS =	zonestatd.c

CPPFLAGS			+= -I/usr/include/libxml2
LDLIBS 				+= -lkstat -lpool -lexacct -lscf \
				    -lcontract -lcmdutils -lumem \
				    -ldladm -ldlpi

LINTFLAGS			+= -u

OBJS =			$(SRCS:%.c=%.o)

.KEEP_STATE:

.PARALLEL:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTCMD)

install: all $(ROOTCMD) $(ROOTMANIFEST) $(ROOTSVCMETHOD)

check: $(CHKMANIFEST)

clean:
	$(RM) $(OBJS)

lint: lint_PROG

include ../../Makefile.targ
