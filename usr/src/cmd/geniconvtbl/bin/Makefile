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
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG =		geniconvtbl

include		$(SRC)/cmd/Makefile.cmd
include		../Makefile.com

#
# Message catalog
#
POFILES =	$(SRCSC1:%.c=%.po) $(SRCI1:%.c=%.po) \
		$(SRCY1:%.y=%.po) $(SRCL1:%.l=%.po)

POFILE =	geniconvtbl_.po

CLEANFILES +=	$(POFILES) $(POFILE)

.KEEP_STATE:

.PARALLEL:	$(OBJS)

all:		$(PROG)

install:	$(ROOTPROG)

catalog:	$(MSGDOMAIN) $(POFILE)

$(POFILE):	$(POFILES)
		$(RM) $@
		$(CAT) $(POFILES) >$@

$(POFILES):	$(SRCSC) $(SRCI) $(SRCY) $(SRCL)

%.po:		../%.c
		$(COMPILE.cpp) $<  > $<.i
		$(BUILD.po)

lint:		lint_SRCS  lint_SRCI

lint_SRCS:	$(SRCS)
		$(LINT.c) $(SRCS) $(LDLIBS)

lint_SRCI:	$(SRCI)
		$(LINT.c) $(SRCI) $(LDLIBS)

include		$(SRC)/cmd/Makefile.targ
