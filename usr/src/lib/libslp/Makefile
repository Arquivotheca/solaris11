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
# Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
#

include $(SRC)/Makefile.master

SLP_SUBDIRS= etc
SLP_MSGSUBDIRS= javalib
STUBSUBDIRS =	$(MACH)
$(BUILD64)STUBSUBDIRS += $(MACH64)
SUBDIRS =	$(SLP_SUBDIRS) $(SLP_MSGSUBDIRS) $(STUBSUBDIRS)

# include library definitions
include ../Makefile.lib

HDRS=clib/slp.h clib/slp-internal.h clib/slp_net_utils.h clib/slp_ami.h
ROOTHDRDIR=     $(ROOT)/usr/include
ROOTHDRS=       $(ROOTHDRDIR)/slp.h

CHECKHDRS= $(HDRS:%.h=%.check)

MSGFILE=libslp.po

# install rule for install_h target
$(ROOTHDRDIR)/%: clib/%
	$(INS.file)

#install rule
$(ROOTDIRS)/%: %
	$(INS.file)

all :=		TARGET= all
clean :=	TARGET= clean
clobber :=	TARGET= clobber
install :=	TARGET= install
lint :=		TARGET= lint
_msg :=		TARGET= _msg
stub :=		TARGET = stub
stubinstall :=	TARGET = stubinstall

.PARALLEL: $(SUBDIRS)

all lint install: install_h .WAIT $(SUBDIRS)

stub stubinstall: $(STUBSUBDIRS)

clean clobber: $(SUBDIRS)

$(MSGDOMAIN)/%: clib/%
	$(INS.file)

_msg: $(SLP_MSGSUBDIRS) $(MSGDOMAIN)/$(MSGFILE)

# include library targets
include ../Makefile.targ

install_h:	$(ROOTHDRS)

check: $(CHECKHDRS)

$(SUBDIRS):	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
