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
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#

PLATFORM =	sun4v

include		../../Makefile.com

OBJECTS =	md5.o sha1.o sha2.o yf_md5.o yf_sha1.o yf_sha2.o

include		$(SRC)/lib/Makefile.lib

INLINES =	$(COMDIR)/md5/$(MACH)/$(PLATFORM)/byteswap.il

AS_CPPFLAGS +=	-D__STDC__ -D_ASM -DPIC -D_REENTRANT -D$(MACH)
ASFLAGS +=	$(AS_PICFLAGS) -P
CFLAGS +=	$(CCVERBOSE) -xarch=sparcvis
CPPFLAGS +=	-D$(PLATFORM)
CPPFLAGS +=	-DHWCAP_SHA1 -DHWCAP_SHA256 -DHWCAP_SHA512 -DHWCAP_MD5
