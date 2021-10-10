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
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG= nc
OBJS= atomicio.o netcat.o socks.o strtonum.o
SRCS= $(OBJS:%.o=%.c)

include		$(SRC)/cmd/Makefile.cmd

# /usr/bin/netcat is a symlink to /usr/bin/nc
ROOTBINSYMLINK= $(ROOTBIN)/netcat

POFILE= $(PROG).po
MSGFILES= $(SRCS)

LDLIBS +=	-lresolv -lsocket

.KEEP_STATE:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

catalog: $(POFILE)

$(POFILE): $(MSGFILES)
	$(BUILDPO.msgfiles)

_msg: $(POFILE)

$(ROOTBINSYMLINK): $(ROOTPROG)
	-$(RM) $@
	-$(SYMLINK) ./$(PROG) $@

install: all $(ROOTPROG) $(ROOTBINSYMLINK)

clean:
	$(RM) $(PROG) $(OBJS)

lint:   lint_SRCS

# Makefile.msg.targ is included for message catalog handling.
include		$(SRC)/Makefile.msg.targ
include		$(SRC)/cmd/Makefile.targ
