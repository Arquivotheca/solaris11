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
# Copyright (c) 1993, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG=		yacc

COMOBJS=	y1.o y2.o y3.o y4.o
POBJECTS=	$(COMOBJS)
POBJS=		$(POBJECTS:%=objs/%)

OBJECTS=	libmai.o libzer.o

LIBRARY=	liby.a
VERS=		.1
YACCPAR=	yaccpar

include ../../../../lib/Makefile.lib

SRCDIR =	../common

# Override default source file derivation rule (in Makefile.lib)
# from objects
#
COMSRCS=	$(COMOBJS:%.o=../common/%.c)
LIBSRCS=	$(OBJECTS:%.o=../common/%.c)
SRCS=		$(COMSRCS) $(LIBSRCS)

LIBS =          $(DYNLIB) $(LINTLIB)

# Tune ZDEFS to ignore undefined symbols for building the yacc shared library
# since these symbols (mainly yyparse) are to be resolved elsewhere.
#
$(DYNLIB):= ZDEFS = $(ZNODEFS)
$(DYNLIBCCC):= ZDEFS = $(ZNODEFS)
LINTSRCS=	../common/llib-l$(LIBNAME)

INCLIST=	-I../../include -I../../include/$(MACH)
CPPFLAGS=	$(INCLIST) $(DEFLIST) $(CPPFLAGS.master)
LDLIBS=		$(LDLIBS.cmd)
BUILD.AR=	$(AR) $(ARFLAGS) $@ `$(LORDER) $(OBJS) | $(TSORT)`
LINTFLAGS=	-amux
LINTPOUT=	lint.out

C99MODE= $(C99_ENABLE)
CFLAGS += $(CCVERBOSE)
CFLAGS64 += $(CCVERBOSE)

$(LINTLIB):=	LINTFLAGS = -nvx
$(ROOTPROG):= FILEMODE = 0555

ROOTYACCPAR=	$(YACCPAR:%=$(ROOTSHLIBCCS)/%)

ROOTLINTDIR=	$(ROOTLIBDIR)
ROOTLINT=	$(LINTSRCS:../common/%=$(ROOTLINTDIR)/%)

DYNLINKLIBDIR=	$(ROOTLIBDIR)
DYNLINKLIB=	$(LIBLINKS:%=$(DYNLINKLIBDIR)/%)

$(DYNLIB) :=	LDLIBS += -lc

CLEANFILES +=	$(LINTPOUT)
CLOBBERFILES +=	$(LIBS) $(LIBRARY) stubs/$(DYNLIB) stubs/$(LIBLINKS)
