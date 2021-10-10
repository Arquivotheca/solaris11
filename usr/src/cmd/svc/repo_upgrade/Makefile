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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

#
# Makefile for repo_upgrade.c
#

PROG = repo_upgrade
MYOBJS = repo_upgrade.o

OBJS = $(MYOBJS)
SRCS = $(MYOBJS:%.o=%.c)

include ../../Makefile.cmd
include ../../Makefile.ctf

ROOTCMDDIR=	$(ROOT)/lib/svc/bin

CPPFLAGS += -I. -I../configd -I../common -I../../../common/svc \
	    -I$(ROOT)/usr/include/sqlite -D_REENTRANT
CFLAGS	+= -v
LDLIBS += -lumem -luutil -lcontract
LINTFLAGS += -errtags -erroff=E_BAD_FORMAT_ARG_TYPE2

DIRMODE = 0755
FILEMODE = 0555

LIBSQLITE = $(ROOT)/usr/lib/libsqlite.o
SQLITELINT = $(ROOT)/usr/lib/llib-lsqlite.ln

OBJS += $(LIBSQLITE)

install := TARGET = install
clobber := TARGET = clobber

lint	:= LDLIBS += $(SQLITELINT)

.KEEP_STATE:
.PARALLEL: $(OBJS)

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

%.o: %.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

install: all $(ROOTCMD) $(ROOTVARSADMFILE)

clean: FRC
	$(RM) $(MYOBJS)

clobber:

lint:   lint_SRCS

lint_SRCS:

include ../../Makefile.targ

FRC:

