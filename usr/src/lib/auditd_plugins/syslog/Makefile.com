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
# Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY=	audit_syslog.a
VERS=		.1
PRAUDIT=	$(SRC)/cmd/praudit

OBJECTS=	sysplugin.o		\
		systoken.o		\
		toktable.o

include		$(SRC)/lib/Makefile.lib

SRCS=		$(SRCDIR)/sysplugin.c	\
		$(PRAUDIT)/toktable.c	\
		$(SRCDIR)/systoken.c

LIBBSM=		$(SRC)/lib/libbsm/common

LIBS=		$(DYNLIB)
LDLIBS		+= -lbsm -lc -lnsl

CFLAGS		+= $(CCVERBOSE)
CPPFLAGS	+= -D_REENTRANT
CPPFLAGS	+= -I$(PRAUDIT)
CPPFLAGS	+= -I$(LIBBSM)

ROOTLIBDIR=	$(ROOT)/usr/lib/security

.KEEP_STATE:

all:	$(LIBS)

lint:	lintcheck

include		../../../Makefile.targ

pics/%.o: $(SRCDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/toktable.o: $(PRAUDIT)/toktable.c
	$(COMPILE.c) $(PRAUDIT)/toktable.c -o $@ $<
	$(POST_PROCESS_O)
