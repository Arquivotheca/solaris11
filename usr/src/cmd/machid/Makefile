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
# Copyright (c) 1995, 2010, Oracle and/or its affiliates. All rights reserved.
#

PROG=	machid

include ../Makefile.cmd

#
#	List of all links present on all architectures and machines.
#
#	Note that this function is obsolesent and we don't generally
#	add to this list (see psarc/1992/171).
#
FIRSTLINK =	i386
LINKS =		i486 i86pc sparc sun sun4m
		

ROOTFIRSTLINK =	$(ROOTBIN)/$(FIRSTLINK)
ROOTLINKS =	$(LINKS:%=$(ROOTBIN)/%)

#
#     Install the program as the first machine in the list.
#
INSTALLIT=	$(INS.link)
$(ROOTFIRSTLINK):=	INSTALLIT = $(INS.rename)
$(ROOTLINKS):=		INSLINKTARGET = $(ROOTFIRSTLINK)

$(ROOTLINKS):	$(ROOTFIRSTLINK)

#
#	Link installation rules
#
$(ROOTBIN)/%:	$(PROG)
	$(INSTALLIT)

$(ROOTFIRSTLINK): $(ROOTBIN)

$(ROOTBIN):
	$(INS.dir)

CFLAGS += $(CCVERBOSE)

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTLINKS)

clean:

lint:	lint_PROG

include ../Makefile.targ
