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
# cmd/cmd-inet/usr.sbin/netcfg/Makefile

PROG=	netcfg
OBJS=	netcfg.o netcfg_lex.o netcfg_grammar.tab.o

include ../../../Makefile.cmd

XGETFLAGS +=	-a -x $(PROG).xcl
LFLAGS =	-t
LDLIBS +=	-ll -ltecla -lnwam -lumem
YFLAGS +=	-d -b netcfg_grammar
CLEANFILES +=	netcfg_lex.c netcfg_grammar.tab.c netcfg_grammar.tab.h

.KEEP_STATE:

all: $(PROG) 

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTUSRSBINPROG)

netcfg_lex.c:	netcfg_lex.l netcfg_grammar.tab.h netcfg.h
	$(LEX) $(LFLAGS) netcfg_lex.l > $@

netcfg_grammar.tab.h netcfg_grammar.tab.c:	netcfg_grammar.y netcfg.h
	$(YACC) $(YFLAGS) netcfg_grammar.y

netcfg_lex.o netcfg_grammar.tab.o := CCVERBOSE =

check:  $(PROG).c $(PROG).h
	$(CSTYLE) -pP $(PROG).c $(PROG).h
	$(HDRCHK) $(PROG).h

clean:
	$(RM) $(OBJS) $(CLEANFILES)

#
# We don't do lint of lex- and yacc- generated source files
#
lint:
	$(LINT.c) -c $(PROG).c $(LDLIBS)

include ../../../Makefile.targ
