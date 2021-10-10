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

HDRS =		libpower.h libpower_impl.h
HDRDIR =	common

#
# install the headers in the stub directory, since we do not ship them
#
STUBROOTINCDIR=	$(STUBROOT)/usr/include
STUBROOTHDRS=	$(HDRS:%=$(STUBROOTINCDIR)/%)

#
# only build 32bit versions of the library, since it is consolidation
# private and all the current consumers are 32bit only
#
SUBDIRS= $(MACH)

all :=		TARGET= all
clean :=	TARGET= clean
clobber :=	TARGET= clobber
install :=	TARGET= install
lint :=		TARGET= lint
_msg :=		TARGET = _msg
stub :=		TARGET= stub
stubinstall :=	TARGET= stubinstall

#
# Only error descriptions and templates are internationalized. They
# are not directly surrounded by gettext(), so we use '-a' to grab
# all the strings and create a message file.
#
MSGFILES =	$(SRCS)
POFILE =	libpower.po
XGETFLAGS +=	-a

.KEEP_STATE:

all clean clobber install: $(SUBDIRS)

stub stubinstall: $(SUBDIRS)

lint:		$(SUBDIRS)

install_h:	$(STUBROOTHDRS)

check:		$(CHECKHDRS)

$(POFILE):	pofile_$(MSGFILES)

_msg:		$(MSGDOMAINPOFILE)

$(STUBROOTHDRS): $(STUBROOTINCDIR)

$(STUBROOTINCDIR)/%: $(HDRDIR)/%
	$(INS.file)


$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

include ../Makefile.targ
include ../../Makefile.msg.targ
