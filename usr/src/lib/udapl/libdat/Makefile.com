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
# Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY=       	libdat.a 
VERS=          	.1 

OBJECTS =	\
	dat_dictionary.o \
	dat_init.o \
	dat_dr.o \
	dat_osd.o \
	dat_sr.o \
	dat_strerror.o \
	dat_api.o \
	udat.o	\
	udat_api.o \
	udat_sr_parser.o 

include ../../../Makefile.lib

LIBS = 	$(DYNLIB) $(LINTLIB)
LDLIBS += -lc

SRCDIR =	../common

CPPFLAGS +=     -I../include 
CFLAGS +=	$(CCVERBOSE)
LINTFLAGS +=	-DDEBUG
LINTFLAGS64 +=	-DDEBUG
$(LINTLIB):=	SRCS = $(SRCDIR)/$(LINTSRC)

$(NOT_RELEASE_BUILD)CPPFLAGS += -DDEBUG
debug := COPTFLAG = -g
debug := COPTFLAG64 = -g

.KEEP_STATE:

all: stub $(LIBS)

debug: all

lint: lintcheck

include ../../../Makefile.targ
