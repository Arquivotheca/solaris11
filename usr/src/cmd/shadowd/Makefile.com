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
PROG= shadowd
SRCS= ../$(PROG).c

include ../../Makefile.cmd

ROOTCMDDIR=	$(ROOT)/usr/lib/fs

C99MODE= -xc99=%all
C99LMODE= -Xc99=%all
CFLAGS += $(CTF_FLAGS) $(CCVERBOSE)
LDLIBS += -lcmdutils -lshadowfs -lzfs

.KEEP_STATE:

all: $(PROG)

$(PROG): $(SRCS)
	$(LINK.c) -o $@ $(SRCS) $(LDLIBS)
	$(POST_PROCESS)

clean:
	-$(RM) $(OBJS)

lint:	lint_SRCS

check:
	$(CSTYLE) -p -P $(SRCS:%=%)

include ../../Makefile.targ
