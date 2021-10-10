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
# Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
#
# uts/common/fs/Makefile
#
# include global definitions
include ../../../Makefile.master

HDRS=  fs_subr.h  \
	fs_reparse.h

PROCHDRS=   prdata.h

SHADOWHDRS=	shadowtest.h

ROOTINCLUDE=	$(ROOT)/usr/include
ROOTDIR=	$(ROOTINCLUDE)/sys
ROOTDIRS=	$(ROOTDIR) $(ROOTDIR)/proc

ROOTHDRS=	$(HDRS:%=$(ROOTDIR)/%)
ROOTSHADOWHDRS=	$(SHADOWHDRS:%=$(ROOTINCLUDE)/%)
ROOTPROCHDRS=	$(PROCHDRS:%=$(ROOTDIR)/proc/%)

# install rules
$(ROOTINCLUDE)/%: shadowtest/%
	$(INS.file)

$(ROOTDIR)/%: %
	$(INS.file)

$(ROOTDIR)/proc/%: proc/%
	$(INS.file)

# standards checking rules
proc/%.check:      proc/%.h
	$(DOT_H_CHECK)
shadowtest/%.check:      shadowtest/%.h
	$(DOT_H_CHECK)

CHECKHDRS= $(HDRS:%.h=%.check)  \
	$(PROCHDRS:%.h=proc/%.check) \
	$(SHADOWHDRS:%.h=shadowtest/%.check)

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS)

install_h: $(ROOTDIRS) $(ROOTHDRS) $(ROOTPROCHDRS) $(ROOTSHADOWHDRS)

$(ROOTDIRS):
	$(INS.dir)

check:	$(CHECKHDRS)
