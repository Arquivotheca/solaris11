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
# Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG = in.iked
OBJ1 = cert.o door.o getssh.o initiator.o ikelabel.o main.o pfkey.o \
	phase1.o policy.o preshared.o private.o readps.o gram.o
OBJ2 = lex.o
OBJS = $(OBJ1) $(OBJ2)
SRCS = $(OBJ1:%.o=../%.c)

include $(SRC)/cmd/Makefile.cmd
include $(SRC)/cmd/cmd-inet/usr.lib/Makefile.lib
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

POFILE=in.iked.po
POFILES=$(OBJS:%.o=%.po)

# Because of required libike callbacks (see policy.c for a good chunk of them),
# we must suppress defined-but-not-used errors.
lint := LINTFLAGS += -u
lint := LINTFLAGS64 += -u

# Disable warnings that affect all XPG applications.
LINTFLAGS += -erroff=E_INCONS_ARG_DECL2 -erroff=E_INCONS_VAL_TYPE_DECL2
LINTFLAGS64 += -erroff=E_INCONS_ARG_DECL2 -erroff=E_INCONS_VAL_TYPE_DECL2

# in.iked has a name clash with main() and libl.so.1.  However, in.iked must
# still export a number of "yy*" (libl) interfaces.  Reduce all other symbols
# to local scope.
MAPFILES +=	../$(MAPFILE.INT) $(MAPFILE.LEX) $(MAPFILE.NGB)
MAPOPTS =	$(MAPFILES:%=-M%)

CPPFLAGS +=     -I.. -D_REENTRANT -D_FILE_OFFSET_BITS=64
LDLIBS +=       -lnsl -like -lipsecutil -lavl -lsocket -ll -lscf
LDFLAGS +=      $(MAPOPTS)

LAZYLIBS = $(ZLAZYLOAD) -ltsol -ltsnet $(ZNOLAZYLOAD)
lint := LAZYLIBS = -ltsol -ltsnet
LDLIBS += $(LAZYLIBS)

CFLAGS += $(CCVERBOSE)
CFLAGS64 += $(CCVERBOSE)

FILEMODE = 0555

CLEANFILES += $(OBJS) *.ln

.KEEP_STATE:

.PARALLEL:

all: $(PROG)

$(PROG): $(OBJS) $(MAPFILES)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

_msg: $(MSGSUBDIRS) $(POFILE)

$(POFILE): $(POFILES)
	$(RM) $@
	cat $(POFILES) > $@

clean:
	-$(RM) $(CLEANFILES)

lint:	lint_SRCS

%.o:	../%.c
	$(COMPILE.c) $<

include $(SRC)/cmd/Makefile.targ
