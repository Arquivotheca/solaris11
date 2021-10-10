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
# Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
#

include ../../Makefile.com

PROG= dmi_info

OBJS1= \
	bios_info.o \
	cpuid_info.o \
	dmi_info.o \
	mem_info.o \
	processor_info.o \
	dmi.o \
	mb_info.o \
	sys_info.o
OBJS2= \
	cpuid.o

SRCS1=	$(OBJS1:%.o=%.c)
SRCS2=	$(OBJS2:%.o=%.s)
SRCS= $(SRCS1) $(SRCS2)
OBJS= $(OBJS1) $(OBJS2)

CLEANFILES= $(OBJS)

CFLAGS += $(CCVERBOSE)

.KEEP_STATE:

.PARALLEL: $(OBJS)

all:	$(PROG)

$(PROG): $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

install: all .WAIT  $(ROOTDDUBINMACHPROG)

clean:
	$(RM) $(CLEANFILES)

lint: lint_SRCS

include ../../Makefile.targ
