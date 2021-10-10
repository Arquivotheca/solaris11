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
# cmd/dpost/src/common/Makefile
#

include		../../Makefile.dpost

OBJS =		$(SRCS:%.c=%.o)

TXTS =		README

ENCODING=	2
CPPFLAGS =	-DDFLTENCODING=$(ENCODING)	\
		-DSYSV				\
		-I. 				\
		$(CPPFLAGS.master)

.KEEP_STATE :

all :		$(TXTS) $(OBJS)

install strip :

clean :
		$(RM) $(OBJS)

lint :		lint_SRCS

clobber: clean
	$(RM) $(CLOBBERFILES)

lint_SRCS:
	$(LINT.c) $(SRCS)
