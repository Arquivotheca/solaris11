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

LIBRARY= libss.a
VERS= .1

SSOBJS= \
	data.o \
	error.o \
	execute_cmd.o \
	help.o \
	invocation.o \
	list_rqs.o \
	listen.o \
	pager.o \
	parse.o \
	prompt.o \
	request_tbl.o \
	requests.o \
	std_rqs.o

OBJECTS= $(SSOBJS)

# include library definitions
include ../../Makefile.lib

SRCS=	$(SSOBJS:%.o=../%.c)

LIBS=		$(DYNLIB)

include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

#override liblink
INS.liblink=	-$(RM) $@; $(SYMLINK) $(LIBLINKS)$(VERS) $@

CPPFLAGS +=     -I$(SRC)/lib/gss_mechs/mech_krb5/include \
		-I$(SRC)/lib/krb5 \
	        -I$(SRC)/uts/common/gssapi/mechs/krb5/include

CFLAGS +=	-I..

DYNFLAGS +=	$(KRUNPATH) $(ZIGNORE)

LDLIBS +=	$(KMECHLIB) -lc -ltecla

$(PICS) :=      CFLAGS += $(XFFLAG)

.KEEP_STATE:

all:	stub $(LIBS)

lint: lintcheck

# include library targets
include ../../Makefile.targ
