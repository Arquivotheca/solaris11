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
# Copyright (c) 2000, 2011, Oracle and/or its affiliates. All rights reserved.
#
# cmd/truss/Makefile.com
#

PROG=	truss

OBJS=	main.o listopts.o ipc.o actions.o expound.o codes.o print.o \
	ramdata.o systable.o procset.o stat.o fcall.o htbl.o
OBJS_SHARED=	ioctlname.o

SRCS=	$(OBJS:%.o=../%.c)
SRCS_SHARED=	$(OBJS_SHARED:%.o=$(SRC)/common/ioctlname/%.c)

include ../../Makefile.cmd

CFLAGS		+= $(CCVERBOSE)
CFLAGS64	+= $(CCVERBOSE)

LDLIBS	+= -lproc -lrtld_db -lc_db -lnsl -lsocket -ltsol -lnvpair -linetutil
CPPFLAGS += -D_REENTRANT -D_LARGEFILE64_SOURCE=1

# Needed to find include file sys/ioctlname.h
CPPFLAGS += -I$(SRC)/uts/common

# Needed in include <netinet/sctp.h>
C99MODE += $(C99_ENABLE)

.KEEP_STATE:

%.o:	../%.c
	$(COMPILE.c) $<

%.o:	$(SRC)/common/ioctlname/%.c
	$(COMPILE.c) $<

all: $(PROG)

$(PROG): $(OBJS) $(OBJS_SHARED)
	$(LINK.c) $(OBJS) $(OBJS_SHARED) -o $@ $(LDLIBS)
	$(POST_PROCESS)

clean:
	$(RM) $(OBJS) $(OBJS_SHARED)

lint:
	$(LINT.c) $(SRCS) $(SRCS_SHARED) $(LDLIBS)

include ../../Makefile.targ
