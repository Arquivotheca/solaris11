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

LIBRARY=	libform.a
VERS=		.1

OBJECTS=  \
	chg_char.o \
	chg_data.o \
	chg_field.o \
	chg_page.o \
	driver.o \
	field.o \
	field_back.o \
	field_buf.o \
	field_fore.o \
	field_init.o \
	field_just.o \
	field_opts.o \
	field_pad.o \
	field_stat.o \
	field_term.o \
	field_user.o \
	fieldtype.o \
	form.o \
	form_init.o \
	form_opts.o \
	form_sub.o \
	form_term.o \
	form_user.o \
	form_win.o \
	post.o \
	regcmp.o \
	regex.o \
	ty_alnum.o \
	ty_alpha.o \
	ty_enum.o \
	ty_int.o \
	ty_num.o \
	ty_regexp.o \
	utility.o

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
