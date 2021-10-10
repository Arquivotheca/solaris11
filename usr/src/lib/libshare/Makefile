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
#
# Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
#

include ../Makefile.lib

HDRS =		libshare.h libshare_impl.h
HDRDIR =	common

MACHS = $(MACH)
$(BUILD64)MACHS += $(MACH64)

STUBSUBDIRS =	$(MACHS)
SUBDIRS =	$(STUBSUBDIRS)

MSGFILES=	common/libshare.c \
		common/libshare_plugin.c \
		common/libshare_util.c \
		common/libshare_proto.c \
		common/libshare_fs.c \
		common/libshare_cache.c \
		common/libshare_upgrade.c

POFILE=		libshare.po

all :=		TARGET = all
clean :=	TARGET = clean
clobber	:=	TARGET = clobber
install	:=	TARGET = install
lint :=		TARGET = lint
stub :=		TARGET = stub
stubinstall :=	TARGET = stubinstall

.KEEP_STATE:

all clean clobber install lint: $(SUBDIRS)

stub stubinstall: $(STUBSUBDIRS)

install_h:	$(ROOTHDRS)

check:		$(CHECKHDRS)

$(POFILE):	$(MSGFILES)
	$(BUILDPO.msgfiles)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

_msg:	$(MSGDOMAINPOFILE)


FRC:

include ../Makefile.targ
include ../../Makefile.msg.targ
