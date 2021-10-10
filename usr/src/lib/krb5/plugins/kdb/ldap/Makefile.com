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
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#


LIBRARY= kldap.a
VERS= .1

# XXX need to set the install path to plugin dir

# ldap plugin objects
LDAP_OBJS= \
	ldap_exp.o

OBJECTS= $(LDAP_OBJS)

# include library definitions
include $(SRC)/lib/krb5/Makefile.lib

LIBS=		$(DYNLIB)
SRCS=	$(LDAP_OBJS:%.o=../%.c)

include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

#override liblink
INS.liblink=	-$(RM) $@; $(SYMLINK) $(LIBLINKS)$(VERS) $@

CPPFLAGS += 	-I$(SRC)/cmd/krb5/iprop \
		-I$(SRC)/lib/krb5 \
		-I$(SRC)/lib/krb5/kdb \
		-I$(SRC)/lib/krb5/plugins/kdb/ldap/libkdb_ldap \
		-I$(SRC)/lib/gss_mechs/mech_krb5/include \
		-I$(SRC)/lib/gss_mechs/mech_krb5/krb5/os \
		-I$(SRC)/lib/gss_mechs/mech_krb5/include/krb5 \
		-I$(SRC)/uts/common/gssapi/include/ \
		-I$(SRC)/uts/common/gssapi/mechs/krb5/include

CFLAGS +=	$(CCVERBOSE)
# The LINTFLAGS line below suppresses lint warnings about unused lint keywords
# in header files.  This happens to be the case for some krb headers.
LINTFLAGS +=  -erroff=E_SUPPRESSION_DIRECTIVE_UNUSED

DYNFLAGS +=	$(KRUNPATH) $(KERBRUNPATH) $(KMECHLIB)
# setting -L $(ROOT)/usr/lib/gss because libkdb_ldap needs mech_krb5
LDLIBS +=	-L $(LROOT)/usr/lib/gss -L $(LROOTLIBDIR) -lkdb_ldap -lc

.KEEP_STATE:

all:	stub $(LIBS)

lint:	lintcheck

# include library targets
include $(SRC)/lib/krb5/Makefile.targ

FRC:
