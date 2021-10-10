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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

include		../Makefile.lib

HDRS=		libshadowfs.h libshadowfs_impl.h

HDRDIR=		common

SUBDIRS=	$(MACH)
$(BUILD64)SUBDIRS += $(MACH64)

all :=		TARGET= all
clean :=	TARGET= clean
clobber :=	TARGET= clobber
install :=	TARGET= install
lint :=		TARGET= lint
stub :=		TARGET = stub
stubinstall :=	TARGET = stubinstall

ERRSRC= $(HDRDIR)/shadow_errno.c
ERRPO= $(HDRDIR)/errpo

clean :=	CLEANFILES += $(ERRSRC) $(ERRPO)

MSGFILES =	$(ERRPO) `$(GREP) -l gettext $(HDRDIR)/*.[ch]`
POFILE =	libshadowfs.po

.KEEP_STATE:

$(ERRSRC): $(HDRDIR)/mkerrno.sh $(HDRDIR)/libshadowfs_impl.h
	$(SHELL) $(HDRDIR)/mkerrno.sh < $(HDRDIR)/libshadowfs_impl.h > $@

$(ERRPO): $(HDRDIR)/mkerrpo.sh $(HDRDIR)/libshadowfs_impl.h
	$(SHELL) $(HDRDIR)/mkerrpo.sh < $(HDRDIR)/libshadowfs_impl.h > $@

all clean clobber install lint stub stubinstall: $(ERRSRC) $(SUBDIRS)

clean clobber: $(SUBDIRS)

$(POFILE):	pofile_MSGFILES

install_h: $(ROOTHDRS)

check: $(CHECKHDRS)

_msg: $(ERRPO) $(MSGDOMAINPOFILE)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

include ../Makefile.targ
include ../../Makefile.msg.targ
