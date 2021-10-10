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
# Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG=	profiles
OBJS=	profiles.o profiles_lex.o profiles_grammar.tab.o

include ../Makefile.cmd

# profiles has a name clash with main() and libl.so.1.  However, profiles must
# still export a number of "yy*" (libl) interfaces.  Reduce all other symbols
# to local scope.
MAPFILES +=	$(MAPFILE.LEX) $(MAPFILE.NGB)
MAPOPTS =	$(MAPFILES:%=-M%)

LFLAGS =	-t
YFLAGS =	-d -b profiles_grammar
STATLIBDIR =	$(SRC)/cmd/oamuser/lib
LDLIBS +=	-lc -ll -ltecla -lbsm -lcmdutils -lsldap -lproject -L$(STATLIBDIR) -loamuser -ltsol -ltsnet -lnsl
LDAPINC =	$(SRC)/lib/libsldap/common
CPPFLAGS +=	-D_REENTRANT -I $(SRC)/cmd/oamuser/inc -I. -I$(LDAPINC)
LDFLAGS +=	$(MAPOPTS)
CLEANFILES +=	profiles_lex.c profiles_grammar.tab.c profiles_grammar.tab.h

ROOTBINPROFILES = $(PROG:%=$(ROOTBIN)/%)
LINTLIB=	llib-loamuser.ln


.KEEP_STATE:

all: $(PROG)


$(PROG): $(OBJS) $(MAPFILES)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all	$(ROOTBINPROFILES)

profiles_lex.c:	profiles_lex.l profiles_grammar.tab.h profiles.h
	$(LEX) $(LFLAGS) profiles_lex.l > $@

profiles_grammar.tab.h profiles_grammar.tab.c:	profiles_grammar.y profiles.h
	$(YACC) $(YFLAGS) profiles_grammar.y

profiles_lex.o profiles_grammar.tab.o := CCVERBOSE =

check:	$(PROG).c $(PROG).h
	$(CSTYLE) -pP $(PROG).c $(PROG).h
	$(HDRCHK) $(PROG).h

clean:
	$(RM) $(OBJS) $(CLEANFILES)

#
# We don't do lint of lex- and yacc- generated source files
#
$(LINTLIB):
	cd $(STATLIBDIR); pwd; $(MAKE) lint;
lint: $(LINTLIB)
	$(LINT.c) -c $(PROG).c $(LDLIBS)

include ../Makefile.targ
