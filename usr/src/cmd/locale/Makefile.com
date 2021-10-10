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
# Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG= locale

OBJS= locale.o
SRCS= $(OBJS:%.o=../%.c)

include ../../Makefile.cmd

POFILE= locale.po

CLEANFILES += $(OBJS)

#
# The following is needed since locale(1) needs additional data fields
# defined at struct lconv for IEEE Std 1003.1-2001 and ISO/IEC 9899:1999
# compatibility while llib-lc shouldn't for other source files.
#
# Please do not carry forward the following to any other Makefiles.
#
ALWAYS_LINT_DEFS += -erroff=E_INCONS_VAL_TYPE_DECL2

.KEEP_STATE:

all: $(PROG) 

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

lint: lint_SRCS

%.o:	../%.c
	$(COMPILE.c) $<

%.po:	../%.c
	$(COMPILE.cpp) $< > `basename $<`.i
	$(XGETTEXT) $(XGETFLAGS) `basename $<`.i
	$(RM)	$@
	sed "/^domain/d" < messages.po > $@
	$(RM) messages.po `basename $<`.i

clean:
	$(RM) $(CLEANFILES)

include ../../Makefile.targ
