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
#

# Define hwcap specific definitions, for use in building hwcap libraries, and
# any symbol capabilities objects that are required by these libraries.

EXTN_CPPFLAGS_i386 =		-Di386 -D__i386 \
				-D_CMOV_INSN -D_SSE_INSN -D_MMX_INSN
EXTN_CPPFLAGS_i386_hwcap1 =	$(EXTN_CPPFLAGS_i386) -D_SEP_INSN
EXTN_CPPFLAGS_i386_hwcap2 =	$(EXTN_CPPFLAGS_i386) -D_SYSC_INSN -D_SSE2_INSN
EXTN_CPPFLAGS_i386_hwcap3 =	$(EXTN_CPPFLAGS_i386)
EXTN_CFLAGS_i386 =		-xtarget=pentium_pro
