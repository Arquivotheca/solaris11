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

SCRIPTS=		\
	shadowtest	\
	utility.ksh

PROGS=			\
	holey		\
	stprogress	\
	stspin		\
	stresume	\
	strotate	\
	stsize		\
	stsuspend	\
	zfsguid

SRCS=$(PROGS:%=%.c)
OBJS=$(PROGS:%=%.o)

include ../Makefile.com 

ROOTPROGS=$(PROGS:%=$(TSTTOOLS)/%)
ROOTSCRIPTS=$(SCRIPTS:%=$(TSTTOOLS)/%)

$(ROOTPROGS) := FILEMODE = 0555
$(ROOTSCRIPTS) := FILEMODE = 0555

stprogress := LDLIBS += -lshadowfs
stresume := LDLIBS += -lshadowtest
strotate := LDLIBS += -lshadowtest
stsuspend := LDLIBS += -lshadowtest
stspin := LDLIBS += -lshadowtest
zfsguid := LDLIBS += -lzfs

all: $(TSTTOOLS) $(PROGS)

install: all $(ROOTPROGS) $(ROOTSCRIPTS)

clean:
	$(RM) $(OBJS)

clobber: clean
	$(RM) $(PROGS)

check:
	$(CSTYLE) -p -P $(SRCS:%=%)

lint:
	$(LINT.c) holey.c $(LDLIBS)
	$(LINT.c) stprogress.c $(LDLIBS) -lshadowfs
	$(LINT.c) stspin.c $(LDLIBS) -lshadowtest
	$(LINT.c) stresume.c $(LDLIBS) -lshadowtest
	$(LINT.c) strotate.c $(LDLIBS) -lshadowtest
	$(LINT.c) stsize.c $(LDLIBS)
	$(LINT.c) stsuspend.c $(LDLIBS) -lshadowtest
	$(LINT.c) zfsguid.c $(LDLIBS) -lzfs

$(TSTTOOLS):
	$(INS.dir)

$(TSTTOOLS)/%: %.pl
	$(INS.rename)

$(TSTTOOLS)/%: %
	$(INS.file)
