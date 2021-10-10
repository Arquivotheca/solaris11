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
# Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
#
# Makefile for KMF Plugins
#

LIBRARY=	kmf_openssl.a
VERS=		.1

OBJECTS=	openssl_spi.o

include	$(SRC)/lib/Makefile.lib

LIBLINKS=	$(DYNLIB:.so.1=.so)
KMFINC=		-I../../../include -I../../../ber_der/inc

BERLIB=		-lkmf -lkmfberder
BERLIB64=	$(BERLIB)

OPENSSLLIBS=	$(BERLIB) -lcrypto -lcryptoutil -lc
OPENSSLLIBS64=	$(BERLIB64) -lcrypto -lcryptoutil -lc

LINTSSLLIBS	= $(BERLIB) -lcryptoutil -lc
LINTSSLLIBS64	= $(BERLIB64) -lcryptoutil -lc

SRCDIR=		../common
INCDIR=		../../include

CFLAGS		+=	$(CCVERBOSE) 
CPPFLAGS	+=	-D_REENTRANT $(KMFINC) \
			-I$(INCDIR) -I/usr/include/libxml2

PICS=	$(OBJECTS:%=pics/%)

lint:=	OPENSSLLIBS=	$(LINTSSLLIBS)
lint:=	OPENSSLLIBS64=	$(LINTSSLLIBS64)

LDLIBS32 	+=	$(OPENSSLLIBS)

ROOTLIBDIR=	$(ROOTFS_LIBDIR)/crypto
ROOTLIBDIR64=	$(ROOTFS_LIBDIR)/crypto/$(MACH64)

.KEEP_STATE:

LIBS	=	$(DYNLIB)
all:	$(DYNLIB) $(LINTLIB)

lint: lintcheck

FRC:

include $(SRC)/lib/Makefile.targ
