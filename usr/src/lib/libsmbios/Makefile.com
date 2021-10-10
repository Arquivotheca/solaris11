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
# Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY = libsmbios.a
VERS = .1

COMMON_OBJS = \
	smb_error.o \
	smb_info.o \
	smb_open.o

LIB_OBJS = \
	smb_lib.o \
	smb_subr.o \
	smb_tables.o

OBJECTS = $(COMMON_OBJS) $(LIB_OBJS)

include ../../Makefile.lib

COMMON_SRCDIR = ../../../common/smbios
COMMON_HDR = $(SRC)/uts/common/sys/smbios.h

SRCS = $(COMMON_OBJS:%.o=$(COMMON_SRCDIR)/%.c) $(LIB_OBJS:%.o=../common/%.c)
LIBS = $(DYNLIB) $(LINTLIB)

SRCDIR = ../common

CLEANFILES += ../common/smb_tables.c

CPPFLAGS += -I../common -I$(COMMON_SRCDIR)
CFLAGS += $(CCVERBOSE)
LDLIBS += -lc

$(LINTLIB) := SRCS = $(SRCDIR)/$(LINTSRC)

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

include ../../Makefile.targ

objs/%.o pics/%.o: ../../../common/smbios/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

../common/smb_tables.c: $(COMMON_SRCDIR)/mktables.sh $(COMMON_HDR)
	sh $(COMMON_SRCDIR)/mktables.sh $(COMMON_HDR) > $@
