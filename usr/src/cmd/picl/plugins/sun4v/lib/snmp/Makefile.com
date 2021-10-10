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
# cmd/picl/plugins/sun4v/lib/snmp/Makefile.com
#

LIBRARY=	libpiclsnmp.a
VERS=		.1
OBJECTS=	snmplib.o pdu.o asn1.o debug.o

# include library definitions
include $(SRC)/Makefile.psm
include $(SRC)/lib/Makefile.lib

ROOT_PLATFORM = $(USR_PLAT_DIR)/sun4v

include $(SRC)/cmd/picl/plugins/Makefile.com

SRCS=		$(OBJECTS:%.o=$(SRCDIR)/%.c)
LIBS=		$(DYNLIB)

ROOTLIBDIR      = $(ROOT_PLATFORM)/lib
ROOTLIBDIR64    = $(ROOT_PLATFORM)/lib/sparcv9

CLEANFILES=	$(LINTOUT) $(LINTLIB)
CLOBBERFILES += $(LIBLINKS)

CPPFLAGS +=	-I.. -I../../../include -I$(SRC)/uts/sun4v
CPPFLAGS +=	-D_REENTRANT

#
# Be careful when enabling SNMP_DEBUG; the debug log can quickly grow
# very very large. Never run cycle stress test with SNMP_DEBUG enabled!
#
#CPPFLAGS +=	-DSNMP_DEBUG

#
# Do NOT uncomment the following two lines, unless you want to test
# the behavior of the library with an SNMP agent over network.
#
#CPPFLAGS +=	-DUSE_SOCKETS
#LDLIBS +=	-lsocket -lnsl

CFLAGS +=	$(CCVERBOSE) -DBIG_ENDIAN
LDLIBS +=	-lc -lnvpair

# It's OK not to build debug.c except when SNMP_DEBUG is enabled.
# Don't let lint complain about it.
#
ALWAYS_LINT_DEFS +=	-erroff=E_EMPTY_TRANSLATION_UNIT

.KEEP_STATE:


all:  $(DYNLIB) $(LIBLINKS)

include $(SRC)/cmd/picl/plugins/Makefile.targ
include $(SRC)/lib/Makefile.targ

lint :
	$(LINT.c) $(SRCS)
