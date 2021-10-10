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

LIBRARY=	libcurses.a
VERS=		.1

OBJECTS= 		\
	addch.o		\
	addstr.o	\
	box.o		\
	clear.o		\
	clrtobot.o	\
	clrtoeol.o	\
	cr_put.o	\
	cr_tty.o	\
	curses.o	\
	delch.o		\
	deleteln.o	\
	delwin.o	\
	endwin.o	\
	erase.o		\
	fullname.o	\
	getch.o		\
	getstr.o	\
	id_subwins.o	\
	idlok.o		\
	initscr.o	\
	insch.o		\
	insertln.o	\
	longname.o	\
	move.o		\
	mvprintw.o	\
	mvscanw.o	\
	mvwin.o		\
	newwin.o	\
	overlay.o	\
	overwrite.o	\
	printw.o	\
	putchar.o	\
	refresh.o	\
	scanw.o		\
	scroll.o	\
	standout.o	\
	toucholap.o	\
	touchwin.o	\
	tstp.o		\
	unctrl.o

# include library definitions
include $(SRC)/lib/Makefile.lib

ROOTLIBDIR=	$(ROOT)/usr/ucblib
ROOTLIBDIR64=	$(ROOT)/usr/ucblib/$(MACH64)

STUBROOTLIBDIR=		$(STUBROOT)/usr/ucblib
STUBROOTLIBDIR64=	$(STUBROOT)/usr/ucblib/$(MACH64)

LROOTLIBDIR=		$(LROOT)/usr/ucblib
LROOTLIBDIR64=		$(LROOT)/usr/ucblib/$(MACH64)

LIBS = $(DYNLIB)

CFLAGS	+=	$(CCVERBOSE)
CFLAGS64 +=	$(CCVERBOSE)
DYNFLAGS +=	$(ZINTERPOSE)
DYNFLAGS32 =	-R/usr/ucblib
DYNFLAGS64 =	-R/usr/ucblib/$(MACH64)
LDLIBS +=	-ltermcap -lucb -lc

CPPFLAGS = -I$(SRC)/ucbhead -I../../../lib/libc/inc $(CPPFLAGS.master)

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

#
# Include library targets
#
include $(SRC)/lib/Makefile.targ

objs/%.o pics/%.o: ../%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)
