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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#
# cmd/dpost/Makefile
#

include 	../Makefile.dpost

COMMON_SUBDIRS = \
		common		\
        	dpost

SUBDIRS= $(COMMON_SUBDIRS)

ROOTDIRS =	$(ROOTLIBLPPOST)

TXTS=		README

.KEEP_STATE:

all:  		$(TXTS) $(SUBDIRS)

install:	$(ROOTDIRS) .WAIT $(SUBDIRS)

clean clobber strip lint catalog:	$(SUBDIRS)

$(SUBDIRS):	FRC
		@cd $@; pwd; $(MAKE) $(TARGET)

$(ROOTDIRS):
		$(INS.dir)

FRC:
