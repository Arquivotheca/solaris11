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
# Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
#
# lib/libsldap/Makefile.com

LIBRARY= libsldap.a
VERS= .1

SLDAPOBJ=	ns_common.o	ns_reads.o	ns_writes.o \
		ns_connect.o	ns_config.o	ns_error.o \
		ns_cache_door.o ns_getalias.o	ns_trace.o \
		ns_init.o	ns_crypt.o	ns_confmgr.o \
		ns_mapping.o	ns_wrapper.o	ns_sasl.o \
		ns_standalone.o ns_connmgmt.o

OBJECTS=	$(SLDAPOBJ)

include ../../Makefile.lib

SRCS =		$(SLDAPOBJ:%.o=../common/%.c)
LIBS =		$(DYNLIB) $(LINTLIB)
$(LINTLIB):= 	SRCS=../common/llib-lsldap
LDLIBS +=	-lnsl -lldap -lscf -lc

SRCDIR =	../common

CFLAGS +=	$(CCVERBOSE)
LOCFLAGS +=	-D_REENTRANT -DSUNW_OPTIONS
CPPFLAGS +=	-I../common -I$(SRC)/lib/libldap5/include/ldap \
		-I/usr/include/mps $(LOCFLAGS)

# Stop lazy loading of libraries so that libldap.so 
# is initialized at run time.
DYNFLAGS +=	$(ZNOLAZYLOAD)

LINTFLAGS +=	-erroff=E_BAD_PTR_CAST_ALIGN
LINTFLAGS64 +=	-erroff=E_BAD_PTR_CAST_ALIGN

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

include ../../Makefile.targ
