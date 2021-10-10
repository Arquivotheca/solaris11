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

LIBRARY= libsmedia.a
VERS=.1

OBJECTS=	smed_clnt.o smed_xdr.o l_generic.o l_misc.o

DERIVED_FILES = smed.h smed_clnt.c smed_xdr.c

# include library definitions
include ../../../Makefile.lib

SRCDIR =	../common

# We can't lint the rpcgen-generated files.
# (Well, we could, but we couldn't fix them.)
lintcheck := SRCS = ../common/l_generic.c ../common/l_misc.c

LIBS = $(DYNLIB) $(LINTLIB)

# definitions for lint

LINTSRC=        $(LINTLIB:%.ln=%)
$(LINTLIB):= SRCS = ../common/$(LINTSRC)

CLEANFILES +=	$(DERIVED_FILES:%=../common/%)

CPPFLAGS += -D_REENTRANT -I$(SRC)/cmd/smserverd/
CFLAGS +=	$(CCVERBOSE) 
CFLAGS64 +=	$(CCVERBOSE)

LDLIBS +=	-lnsl -lc

.KEEP_STATE:

all: stub $(LIBS)

lint:	lintcheck

# include library targets
include ../../../Makefile.targ

objs/%.o pics/%.o: ../common/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

# install rule for lint library target

$(ROOTLINTDIR64)/%: ../common/%
	$(INS.file)

$(ROOTLINTDIR)/%: ../common/%
	$(INS.file)

#
# Derived files
#

../common/smed.h:	$(SRC)/cmd/smserverd/smed.x
	$(RPCGEN) -C -h $(SRC)/cmd/smserverd/smed.x | \
	$(SED) -e 's!$(SRC)/cmd/smserverd/smed.h!smed.h!' > $@

../common/smed_clnt.c: $(SRC)/cmd/smserverd/smed.x ../common/smed.h
	$(RPCGEN) -l $(SRC)/cmd/smserverd/smed.x | \
	$(SED) -e 's!$(SRC)/cmd/smserverd/smed.h!smed.h!' > $@

../common/smed_xdr.c: $(SRC)/cmd/smserverd/smed.x ../common/smed.h
	$(RPCGEN) -c $(SRC)/cmd/smserverd/smed.x | \
	$(SED) -e 's!$(SRC)/cmd/smserverd/smed.h!smed.h!' > $@

