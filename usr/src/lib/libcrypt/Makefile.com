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

LIBRARY=	libcrypt.a
VERS=		.1

OBJECTS=  \
	des.o \
	des_crypt.o \
	des_encrypt.o \
	des_decrypt.o \
	des_soft.o

include ../../Makefile.lib

SRCDIR=		../common

LIBS=		$(DYNLIB) $(LINTLIB)

$(LINTLIB):=	SRCS=$(SRCDIR)/$(LINTSRC)

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-D_REENTRANT -I../inc -I../../common/inc
LDLIBS +=       -lc

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

include ../../Makefile.targ

#
# In addition to libcrypt, we deliver symlinks named libcrypt_{d,i} that
# point at libcrypt. We also do this for the {d,i} compilation symlinks.
#
EXTRA_LINKNAMES=	libcrypt_d.so$(VERS)	libcrypt_d.so \
			libcrypt_i.so$(VERS)	libcrypt_i.so

EXTRA_ROOTLINKS=	$(EXTRA_LINKNAMES:%=$(ROOTLIBDIR)/%)
EXTRA_ROOTLINKS64=	$(EXTRA_LINKNAMES:%=$(ROOTLIBDIR64)/%)

EXTRA_STUBROOTLINKS=	$(EXTRA_LINKNAMES:%=$(STUBROOTLIBDIR)/%)
EXTRA_STUBROOTLINKS64=	$(EXTRA_LINKNAMES:%=$(STUBROOTLIBDIR64)/%)

$(ROOTLIBDIR)/libcrypt_i.% \
$(ROOTLIBDIR)/libcrypt_d.% : $(ROOTLIBDIR)/libcrypt.%
	$(RM) $@
	$(SYMLINK) $(<F) $@

$(ROOTLIBDIR64)/libcrypt_i.% \
$(ROOTLIBDIR64)/libcrypt_d.% : $(ROOTLIBDIR64)/libcrypt.%
	$(RM) $@
	$(SYMLINK) $(<F) $@

$(STUBROOTLIBDIR)/libcrypt_i.% \
$(STUBROOTLIBDIR)/libcrypt_d.% : $(STUBROOTLIBDIR)/libcrypt.%
	$(RM) $@
	$(SYMLINK) $(<F) $@

$(STUBROOTLIBDIR64)/libcrypt_i.% \
$(STUBROOTLIBDIR64)/libcrypt_d.% : $(STUBROOTLIBDIR64)/libcrypt.%
	$(RM) $@
	$(SYMLINK) $(<F) $@
