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
# Copyright (c) 2000, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY=	preen_md.a 
VERS=          	.1 
OBJECTS=	mdpreen.o

include $(SRC)/lib/lvm/Makefile.lvm

ROOTLIBDIR=	$(ROOT)/usr/lib/drv
STUBROOTLIBDIR=	$(STUBROOT)/usr/lib/drv
LROOTLIBDIR=	$(LROOT)/usr/lib/drv
LIBS=		$(DYNLIB) 	# don't build a static lib
CPPFLAGS +=	-D_FILE_OFFSET_BITS=64
LDLIBS +=	-lmeta -ldevid -lc

.KEEP_STATE:

all: stub $(LIBS)

include $(SRC)/lib/lvm/Makefile.targ

$(ROOTLIBDIR)/$(DYNLIB) :=	FILEMODE= 555
