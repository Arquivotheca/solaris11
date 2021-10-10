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
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#
#

MACHCOBJS = libv12n.o

CMNOBJS=	mdesc_findname.o	\
		mdesc_findnodeprop.o	\
		mdesc_fini.o		\
		mdesc_getpropval.o	\
		mdesc_getpropstr.o	\
		mdesc_init_intern.o	\
		mdesc_nodecount.o	\
		mdesc_rootnode.o	\
		mdesc_scandag.o

OBJS += $(CMNOBJS)

include ../Makefile.com
include ../../Makefile.lib.64

C99MODE =	$(C99_ENABLE)

LDLIBS += -luuid -lc

CPPFLAGS += -I. -I$(SRC)/lib/libds/common -I$(SRC)/cmd/ldmad

install: stubinstall all $(ROOTLIBS64) $(ROOTLINKS64) $(ROOTLINT64)

stubinstall: stub $(STUBROOTLIBS64) $(STUBROOTLINKS64)

pics/%.o: $(SRC)/common/mdesc/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)
