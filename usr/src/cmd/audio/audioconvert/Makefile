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
# Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
#
# Makefile for cmd/audio/audioconvert

PROG= audioconvert

include 	../../Makefile.cmd

INCLUDES += -I../include -I.

CPPFLAGS += $(INCLUDES)

# This is 32-bit C++ code that was originally developed with -compat=4
CCFLAGS +=	$(CCCNOLOCALFOR)

PROGSRCS= convert.cc file.cc main.cc parse.cc

OBJS= $(PROGSRCS:%.cc=%.o)

ZASSERTDEFLIB_LIST_STUDIO +=	libCrun.so
LDLIBS +=			-laudio -lm -lc

LDFLAGS += -L../utilities

.PARALLEL:	$(OBJS)

.KEEP_STATE:

all:		$(PROG)

install:	all .WAIT $(ROOTPROG)

_msg:

$(PROG):	$(OBJS)
		$(LINK.cc) $(OBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

clean:

clobber:
	$(RM) $(PROG) $(CLOBBERFILES)

lint:
