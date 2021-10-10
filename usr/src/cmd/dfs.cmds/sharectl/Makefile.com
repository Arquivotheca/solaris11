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
# Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG=	sharectl
SRCS=	../$(PROG).c

include ../../../Makefile.cmd

MYCPPFLAGS = 	-I..
CPPFLAGS += $(MYCPPFLAGS)
LDLIBS += -lshare -lumem -lnvpair

.KEEP_STATE:

all: $(PROG)

lint:	lint_SRCS

$(PROG): $(SRCS)
	$(LINK.c) -o $@ $(SRCS) $(LDFLAGS) $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTUSRSBINPROG)

clean:
	$(RM) $(PROG)

include ../../../Makefile.targ
