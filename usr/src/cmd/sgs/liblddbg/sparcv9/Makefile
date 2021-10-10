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
# Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
#

include		../Makefile.com

ROOTFS_LIBDIR64 = \
		$(ROOT)/lib/$(MACH64)
ROOTFS_DYNLIB64 = \
		$(DYNLIB:%=$(ROOTFS_LIBDIR64)/%)

LINTFLAGS64 +=	$(VAR_LINTFLAGS64)

.KEEP_STATE:

$(ROOTFS_DYNLIB64) :=	FILEMODE= 755

all:		stub $(DYNLIB) $(LIBLINKS) $(LINTLIBS)

install \
package:	stubinstall all $(ROOTFS_DYNLIB64) $(STUBROOTFS_LINTLIBS64)

stubinstall:	stub $(STUBROOTFS_LIBS64) $(STUBROOTFS_LINKS64)

include		../Makefile.targ
include		../../Makefile.sub.64
