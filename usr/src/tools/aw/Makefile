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
# Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
#
# tools/aw/Makefile
#

PROG	= aw

include ../Makefile.tools

CFLAGS += $(CCVERBOSE)

LINTFLAGS += -ux -Xa -errchk=locfmtchk,parentheses

CPPFLAGS +=	-DDEFAULT_AS_DIR='"$(GNU_ROOT)/bin"'
CPPFLAGS +=	-DDEFAULT_AS64_DIR='"$(GNU_ROOT)/bin"'
CPPFLAGS +=	-DDEFAULT_M4_DIR='"/usr/bin"'
CPPFLAGS +=	-DDEFAULT_M4LIB_DIR='"/usr/lib"'
CPPFLAGS +=	-DDEFAULT_CPP_DIR='"/usr/lib"'

.KEEP_STATE:

all:    $(PROG)

install: all .WAIT $(ROOTONBLDMACHPROG)

lint:	lint_PROG

clean:

include ../Makefile.targ
