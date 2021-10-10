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

include		../../Makefile.com

THREAD_DEBUG =
$(NOT_RELEASE_BUILD)THREAD_DEBUG = -DTHREAD_DEBUG

IFLAGS =	-I$(SRC)/common/hrt/$(MACH) -I$(SRC)/uts/intel \
		-I$(SRC)/uts/i86pc -I$(LIBCBASE)/inc

CPPFLAGS +=	-D_ASM_INLINES $(EXTN_CPPFLAGS) $(THREAD_DEBUG) $(IFLAGS)

AS_CPPFLAGS +=	-D__STDC__ -D_ASM -DPIC -D$(MACH)
ASFLAGS +=	-P -K pic $(IFLAGS)

# Files which need the threads .il inline template
HRT_TIL =	pics/timestamp_cmn.o pics/map_hrt_info.o
