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

LIBRARY = libdtrace_jni.a
VERS = .1

LIBSRCS = \
	dtj_util.c \
	dtj_jnitab.c \
	dtj_error.c \
	dtj_probe.c \
	dtj_attr.c \
	dtj_consume.c \
	dtrace_jni.c

OBJECTS = $(MACHOBJS) $(LIBSRCS:%.c=%.o)

include ../../Makefile.lib

SRCS = $(LIBSRCS:%.c=../common/%.c)

SRCDIR = ../common

CPPFLAGS += -I../common -I.
CPPFLAGS += -I$(JAVA_ROOT)/include -I$(JAVA_ROOT)/include/solaris
CPPFLAGS += -I../java/native
CFLAGS += $(CCVERBOSE) -K PIC
CFLAGS64 += $(CCVERBOSE) -K PIC
LDLIBS += -lc -luutil -ldtrace -lproc

LINTLIB =

LFLAGS = -t -v

ROOTDLIBDIR = $(ROOT)/usr/lib/dtrace_jni
ROOTDLIBDIR64 = $(ROOT)/usr/lib/dtrace_jni/64

ROOTDLIBS = $(DLIBSRCS:%=$(ROOTDLIBDIR)/%)

.KEEP_STATE:

all: stub $(DYNLIB)

lint: lintcheck

%.o: ../common/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

$(ROOTDLIBDIR):
	$(INS.dir)

$(ROOTDLIBDIR64): $(ROOTDLIBDIR)
	$(INS.dir)

$(ROOTDLIBDIR)/%.o: %.o
	$(INS.file)

$(ROOTDLIBDIR64)/%.o: %.o
	$(INS.file)

$(ROOTDLIBS): $(ROOTDLIBDIR)

$(ROOTDOBJS): $(ROOTDLIBDIR)

$(ROOTDOBJS64): $(ROOTDLIBDIR64)

include ../../Makefile.targ
