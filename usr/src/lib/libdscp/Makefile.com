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
# Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY=	libdscp.a
VERS=		.1
MANIFEST=	dscp.xml
OBJECTS=	libdscp.o

include	../../Makefile.lib
include	../../Makefile.rootfs

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-lc -lsocket -lnsl
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

CPPFLAGS +=	-I..
CFLAGS +=	$(CCVERBOSE)

.KEEP_STATE:

# Defintions for installation of the library
USR_PLAT_DIR		= $(ROOT)/usr/platform
STUBUSR_PLAT_DIR	= $(STUBROOT)/usr/platform
LUSR_PLAT_DIR		= $(LROOT)/usr/platform

USR_PSM_DIR		= $(USR_PLAT_DIR)/SUNW,SPARC-Enterprise
STUBUSR_PSM_DIR		= $(STUBUSR_PLAT_DIR)/SUNW,SPARC-Enterprise
LUSR_PSM_DIR		= $(LUSR_PLAT_DIR)/SUNW,SPARC-Enterprise

USR_PSM_LIB_DIR		= $(USR_PSM_DIR)/lib
STUBUSR_PSM_LIB_DIR	= $(STUBUSR_PSM_DIR)/lib
LUSR_PSM_LIB_DIR	= $(LUSR_PSM_DIR)/lib

ROOTLIBDIR=		$(USR_PSM_LIB_DIR)
STUBROOTLIBDIR=		$(STUBUSR_PSM_LIB_DIR)
LROOTLIBDIR=		$(LUSR_PSM_LIB_DIR)

$(ROOTLIBDIR) $(STUBROOTLIBDIR):
	$(INS.dir)

.KEEP_STATE:

all: stub $(LIBS)

install: all .WAIT $(ROOTLIBDIR) $(ROOTLIB)

stubinstall: $(STUBROOTLIBDIR) $(STUBROOTLIB)

lint: lintcheck

include ../../Makefile.targ
