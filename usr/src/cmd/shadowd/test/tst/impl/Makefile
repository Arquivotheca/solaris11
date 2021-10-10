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
# Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROGS=  stcredclear	\
	stcredset	\
	stfid		\
	stkthread	\
	stpending	\
	stpath		\
	stprocess

SRCS=$(PROGS:%=%.c)

stcredclear lint_stcredclear := LDLIBS += -lshadowtest
stcredset   lint_stcredset   := LDLIBS += -lshadowtest
stfid       lint_stfid       := LDLIBS += -lshadowtest
stkthread   lint_stkthread   := LDLIBS += -lshadowtest
stpending   lint_stpending   := LDLIBS += -lshadowtest

include ../Makefile.com
include ../Makefile.com.progs
