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

LIBRARY=	libmenu.a
VERS=		.1

OBJECTS=  \
	affect.o \
	chk.o \
	connect.o \
	curitem.o \
	driver.o \
	global.o \
	itemcount.o \
	itemopts.o \
	itemusrptr.o \
	itemvalue.o \
	link.o \
	menuback.o \
	menucursor.o \
	menufore.o \
	menuformat.o \
	menugrey.o \
	menuitems.o \
	menumark.o \
	menuopts.o \
	menupad.o \
	menuserptr.o \
	menusub.o \
	menuwin.o \
	newitem.o \
	newmenu.o \
	pattern.o \
	post.o \
	scale.o \
	show.o \
	terminit.o \
	topitem.o \
	visible.o

# include library definitions
include ../../../Makefile.lib

LIBS =          $(DYNLIB) $(LINTLIB)

SRCDIR=		../common

$(LINTLIB) :=	SRCS=$(SRCDIR)/$(LINTSRC)

CPPFLAGS +=	-I../inc
CFLAGS +=       $(CCVERBOSE)
LDLIBS +=       -lcurses -lc

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

# include library targets
include ../../../Makefile.targ

pics/%.o:	../common/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)
