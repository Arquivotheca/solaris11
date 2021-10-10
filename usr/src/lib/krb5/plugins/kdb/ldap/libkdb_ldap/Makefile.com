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

LIBRARY= libkdb_ldap.a
VERS= .1

LIBKLDAP_OBJS= \
	kdb_ext.o \
	kdb_ldap.o \
	kdb_ldap_conn.o \
	kdb_xdr.o \
	ldap_create.o \
	ldap_err.o \
	ldap_fetch_mkey.o \
	ldap_handle.o \
	ldap_krbcontainer.o \
	ldap_misc.o \
	ldap_misc_solaris.o \
	ldap_principal.o \
	ldap_principal2.o \
	ldap_pwd_policy.o \
	ldap_realm.o \
	ldap_service_rights.o \
	ldap_service_stash.o \
	ldap_services.o \
	ldap_tkt_policy.o \
	lockout.o \
	princ_xdr.o 

OBJECTS= $(LIBKLDAP_OBJS)

# include library definitions
include $(SRC)/lib/krb5/Makefile.lib

SRCS= $(LIBKLDAP_OBJS:%.o=../%.c)

LIBS=		$(DYNLIB)

include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

POFILE = $(LIBRARY:%.a=%.po)
POFILES = generic.po

#override liblink
INS.liblink=	-$(RM) $@; $(SYMLINK) $(LIBLINKS)$(VERS) $@

CPPFLAGS += 	-I$(SRC)/lib/krb5 \
		-I$(SRC)/lib/krb5/kdb \
		-I$(SRC)/lib/gss_mechs/mech_krb5/include \
		-I$(SRC)/lib/gss_mechs/mech_krb5/krb5/os \
		-I$(SRC)/lib/gss_mechs/mech_krb5/include/krb5 \
		-I$(SRC)/uts/common/gssapi/include/ \
		-I$(SRC)/uts/common/gssapi/mechs/krb5/include

#gtbtmp183 ldap_pwd_policy.c
CFLAGS +=       -erroff=E_ARG_INCOMPATIBLE_WITH_ARG_L

CFLAGS +=	$(CCVERBOSE) -I..
DYNFLAGS +=	$(KRUNPATH) $(KERBRUNPATH)
LDLIBS +=	$(KMECHLIB) -L $(LROOTLIBDIR) -lkadm5srv -lc -lnsl -lldap -lkdb

$(PARFAIT_BUILD)__GNUC=
$(PARFAIT_BUILD)__GNUC64=

# The LINTFLAGS line below suppresses lint warnings about unused lint keywords
# in header files.  This happens to be the case for some krb headers.
LINTFLAGS +=  -erroff=E_SUPPRESSION_DIRECTIVE_UNUSED

.KEEP_STATE:

all:	stub $(LIBS)

lint:	lintcheck

# include library targets
include $(SRC)/lib/krb5/Makefile.targ

FRC:

generic.po: FRC
	$(RM) messages.po
	$(XGETTEXT) $(XGETFLAGS) `$(GREP) -l gettext ../*.[ch]`
	$(SED) "/^domain/d" messages.po > $@
	$(RM) messages.po
