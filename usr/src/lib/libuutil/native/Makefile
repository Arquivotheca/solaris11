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

NOT_NATIVE = $(POUND_SIGN)		# tell Makefile.com we're native
USESTUBTARG = $(POUND_SIGN)		# tell Makefile.targ not to build stubs

include ../Makefile.com

CC =		$(NATIVECC)
LD =		$(NATIVELD)
CFLAGS =	$(NATIVE_CFLAGS)
CPPFLAGS =	-I. $(MY_NATIVE_CPPFLAGS)
LDFLAGS =
LDLIBS =	$(MY_NATIVE_LDLIBS)

#
# We must use the build's avl headers, to match the common avl.c
#
AVLHEADERS = sys/avl.h sys/avl_impl.h
CLOBBERFILES += $(AVLHEADERS)

all: $(LIBS)

stub:

install: all

$(PICS): $(AVLHEADERS)

sys/%: $(SRC)/uts/common/sys/%
	@mkdir -p sys
	cp -f $< $@

