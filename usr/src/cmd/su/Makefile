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

PROG =		su
DEFAULTFILES =	su.dfl

include		../Makefile.cmd

EMB_PROG =	embedded_su
ROOTEMB_PROG =	$(ROOTLIB)/$(EMB_PROG)

FILEMODE =	04555

$(PROG) :=	LDLIBS += -lbsm -lpam

CLOBBERFILES	+= $(EMB_PROG)

lint :=		LDLIBS += -lbsm -lpam

.KEEP_STATE:

all:		$(PROG) $(EMB_PROG)

install:	all $(ROOTPROG) $(ROOTUSRSBINPROG) \
		$(ROOTETCDEFAULTFILES) $(ROOTEMB_PROG)

$(ROOTUSRSBINPROG): $(ROOTPROG)
		$(RM) $(ROOTUSRSBINPROG); \
		$(SYMLINK) ../bin/$(PROG) $(ROOTUSRSBINPROG)

$(ROOTEMB_PROG):
		$(RM) $(ROOTEMB_PROG); \
		$(SYMLINK) ../bin/$(PROG) $(ROOTEMB_PROG)

$(EMB_PROG):
		$(RM) $(EMB_PROG); \
		$(SYMLINK) $(PROG) $(EMB_PROG)

clean:

lint:		lint_PROG

include		../Makefile.targ
