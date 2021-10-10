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
# cmd/newtask/Makefile.com

PROG= newtask
OBJS= newtask.o utils.o
SRCS= $(OBJS:%.o=../%.c)

FILEMODE= 4555
LDLIBS += -lproc -lproject

CFLAGS += $(CCVERBOSE)
CFLAGS64 += $(CCVERBOSE)

CPPFLAGS += -I..

.KEEP_STATE:

%.o:	../%.c
	$(COMPILE.c) $<

all:	$(PROG)

$(PROG):	$(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

clean:
	$(RM) $(OBJS)

lint:
	$(LINT.c) $(SRCS) $(LDLIBS)
