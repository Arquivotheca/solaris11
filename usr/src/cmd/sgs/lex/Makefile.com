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

PROG=		lex

MACHOBJS=	main.o sub1.o sub2.o sub3.o header.o parser.o
POBJECTS=	$(MACHOBJS)
POBJS=		$(POBJECTS:%=objs/%)

LIBRARY=	libl.a
VERS=		.1

LIBOBJS=	allprint.o libmain.o reject.o yyless.o yywrap.o
LIBOBJS_W=	allprint_w.o reject_w.o yyless_w.o
LIBOBJS_E=	reject_e.o yyless_e.o
OBJECTS=	$(LIBOBJS) $(LIBOBJS_W) $(LIBOBJS_E)

FORMS=		nceucform ncform nrform

include 	../../../../lib/Makefile.lib

SRCDIR =	../common

C99MODE=	$(C99_ENABLE)

# Override default source file derivation rule (in Makefile.lib)
# from objects
#
MACHSRCS=	$(MACHOBJS:%.o=../common/%.c)
LIBSRCS =	$(LIBOBJS:%.o=../common/%.c)
SRCS=		$(MACHSRCS) $(LIBSRCS)
		

LIBS =          $(DYNLIB) $(LINTLIB)

LINTSRCS=	../common/llib-l$(LIBNAME)

INCLIST=	$(INCLIST_$(MACH)) -I../../include -I../../include/$(MACH)
DEFLIST=	-DELF

# It is not very clean to base the conditional definitions as below, but
# this will have to do for now.
#
#$(LIBOBJS_W):=	DEFLIST = -DEUC -DJLSLEX  -DWOPTION -D$*=$*_w
objs/%_w.o:=	DEFLIST = -DEUC -DJLSLEX  -DWOPTION -D$*=$*_w
pics/%_w.o:=	DEFLIST = -DEUC -DJLSLEX  -DWOPTION -D$*=$*_w

#$(LIBOBJS_E):=	DEFLIST = -DEUC -DJLSLEX  -DEOPTION -D$*=$*_e
objs/%_e.o:=	DEFLIST = -DEUC -DJLSLEX  -DEOPTION -D$*=$*_e
pics/%_e.o:=	DEFLIST = -DEUC -DJLSLEX  -DEOPTION -D$*=$*_e

CPPFLAGS=	$(INCLIST) $(DEFLIST) $(CPPFLAGS.master)
BUILD.AR=	$(AR) $(ARFLAGS) $@ `$(LORDER) $(OBJS) | $(TSORT)`
LINTFLAGS=	-amux
LINTPOUT=	lint.out

$(LINTLIB):=	LINTFLAGS = -nvx
$(ROOTPROG):=	FILEMODE = 0555

ROOTFORMS=	$(FORMS:%=$(ROOTSHLIBCCS)/%)

ROOTLINTDIR=	$(ROOTLIBDIR)
ROOTLINT=	$(LINTSRCS:../common/%=$(ROOTLINTDIR)/%)

DYNLINKLIBDIR=	$(ROOTLIBDIR)
DYNLINKLIB=	$(LIBLINKS:%=$(DYNLINKLIBDIR)/%)

# Need to make sure lib-make's are warning free
$(DYNLIB) :=	CFLAGS += $(CCVERBOSE)
$(DYNLIB) :=	CFLAGS64 += $(CCVERBOSE)

$(DYNLIB) :=	LDLIBS += -lc

CLEANFILES +=	../common/parser.c $(LINTPOUT)
CLOBBERFILES +=	$(LIBS) $(LIBRARY) stubs/$(DYNLIB) stubs/$(LIBLINKS)
