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
# Copyright (c) 1990, 2011, Oracle and/or its affiliates. All rights reserved.
#
# Makefile for cmd/man/src
#

include		../../Makefile.cmd

BINPROG =	man
LNKPROG =	catman apropos whatis
MANIFEST =	man-index.xml
SGMLUTILDIR = 	util

SRCS =		man.c \
                index.c \
                indexParser.c \
                indexWriter.c \
                indexReader.c \
                indexUtil.c

OBJS =		$(SRCS:%.c=%.o)

CLOBBERFILES +=	$(LNKPROG)

ROOTMANIFESTDIR = $(ROOTSVCAPPLICATION)
ROOTMETHOD =	$(ROOTLIBSVCMETHOD)/man-index

ROOTPROG =	$(BINPROG:%=$(ROOTBIN)/%)
ROOTLNKPROG =	$(LNKPROG:%=$(ROOTBIN)/%)

# for messaging catalog files
POFILE= src.po
POFILES= $(SRCS:%.c=%.po)

.KEEP_STATE :

all :		$(BINPROG) $(LNKPROG) buildsubs

man :		$(OBJS)
		$(LINK.c) -o $@ $(OBJS) $(LDLIBS) -lm
		$(POST_PROCESS)

makewhatis :	$$@.sh
		cat $@.sh > $@

buildsubs :		
		cd $(SGMLUTILDIR); pwd; $(MAKE) all

install :	all $(ROOTPROG) $(ROOTLNKPROG) installsubs \
		$(ROOTMANIFEST) $(ROOTMETHOD)

installsubs :		
		cd $(SGMLUTILDIR); pwd; $(MAKE) install

$(ROOTLNKPROG) :	$(ROOTBIN)/man
		-$(RM) $@; $(LN) $(ROOTBIN)/man $@

$(LNKPROG) :	man
		-$(RM) $@; $(LN) man $@

catalog:        $(POFILE)

$(POFILE):      $(POFILES)
	$(RM)	$@
	cat     $(POFILES)      > $@


clean :		sgml_clean
		$(RM) $(OBJS)
		$(RM) $(BINPROG) $(LNKPROG)

sgml_clean :
		cd $(SGMLUTILDIR); pwd; $(MAKE) clean

clobber :	sgml_clobber

sgml_clobber :
		cd $(SGMLUTILDIR); pwd; $(MAKE) clobber

strip :
		$(STRIP) $(PROG)

lint :		$(SRCS)
		$(LINT.c) $(SRCS) $(LDLIBS) -lm

include		../../Makefile.targ
