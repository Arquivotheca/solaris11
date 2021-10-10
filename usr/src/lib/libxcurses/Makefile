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
# Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
#

include	../Makefile.lib

SUBDIRS =	$(MACH)
$(BUILD64)SUBDIRS += $(MACH64)

all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint
stub :=		TARGET = stub
stubinstall :=	TARGET = stubinstall

# capability NAMES generated source
NAMES= \
src/libc/xcurses/boolcode.c	src/libc/xcurses/boolfnam.c \
src/libc/xcurses/boolname.c	src/libc/xcurses/numcode.c \
src/libc/xcurses/numfnam.c	src/libc/xcurses/numname.c \
src/libc/xcurses/strcode.c	src/libc/xcurses/strfnam.c \
src/libc/xcurses/strname.c

GENERATED= \
$(NAMES)	src/libc/xcurses/keyindex.c	h/term.h

# definitions for check target
HDRS=		h/term.h h/curses.h h/unctrl.h

.KEEP_STATE:

LIBS=		$(DYNLIB) $(LINTLIB)

all install lint: $(GENERATED) .WAIT $(SUBDIRS)

clean clobber: $(SUBDIRS)
	$(RM) $(GENERATED)

stub stubinstall: $(SUBDIRS)

# still need to implement cstyle check of term.h
# do this when we go through and make this library cstyle clean.
check: $(HDRS)

$(SUBDIRS):	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

# derived C files
# mknames.awk defines functions so needs nawk
$(NAMES) : src/libc/xcurses/mknames.awk src/libc/xcurses/caps
	$(RM) $(NAMES)
	cd src/libc/xcurses; nawk -f mknames.awk caps

h/term.h : src/libc/xcurses/mkterm.awk src/libc/xcurses/caps
	$(RM) $@
	cd src/libc/xcurses; awk -f mkterm.awk caps > term.h
	$(MV) src/libc/xcurses/term.h h/term.h

src/libc/xcurses/keyindex.c : src/libc/xcurses/mkkey.awk src/libc/xcurses/caps
	$(RM) $@
	cd src/libc/xcurses; nawk -f mkkey.awk caps > keyindex.c

FRC:
