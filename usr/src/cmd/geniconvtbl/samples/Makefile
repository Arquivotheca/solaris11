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
# Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
#

SRCFILES =	ISO8859-1_to_UTF-8.src \
		UTF-8_to_ISO8859-1.src \
		eucJP_to_ISO-2022-JP.src \
		ISO-2022-JP_to_eucJP.src \
		ISO646_to_ISO8859-1.src \
		ISO8859-1_to_ISO646.src
		
BTFILES =	ISO646%ISO8859-1.bt \
		ISO8859-1%ISO646.bt

GENICONVTBL =	../native/geniconvtbl

include		../../Makefile.cmd

ROOTICONVDIR =	$(ROOTLIB)/iconv
ROOTGENDIR =	$(ROOTICONVDIR)/geniconvtbl
SRCDIR =	$(ROOTGENDIR)/srcs
BTDIR =		$(ROOTGENDIR)/binarytables
ROOTSRCFILES =	$(SRCFILES:%=$(SRCDIR)/%)
ROOTBTFILES =	$(BTFILES:%=$(BTDIR)/%)

$(ROOTSRCFILES) :=	FILEMODE = 444
$(ROOTBTFILES) :=	FILEMODE = 444

.KEEP_STATE:

.PARALLEL:	$(BTFILES)

all:		$(BTFILES)

install:	all $(ROOTSRCFILES) $(ROOTBTFILES)

clean clobber:
		$(RM) $(BTFILES)

ISO646%ISO8859-1.bt: ISO646_to_ISO8859-1.src
		$(GENICONVTBL) -o $@ -f ISO646_to_ISO8859-1.src

ISO8859-1%ISO646.bt: ISO8859-1_to_ISO646.src
		$(GENICONVTBL) -o $@ -f ISO8859-1_to_ISO646.src

$(SRCDIR)/%:	$(SRCDIR) %
		$(INS.file)

$(BTDIR)/%:	$(BTDIR) %
		$(INS.file)

$(SRCDIR) $(BTDIR): $(ROOTGENDIR)
		$(INS.dir)

$(ROOTGENDIR):
		$(INS.dir)
