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
# Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
#
# usr/src/lib/pam_modules/krb5/Makefile.com
#

LIBRARY=	pam_krb5.a
VERS=		.1


PRIV_OBJ=	krb5_authenticate.o \
		krb5_setcred.o \
		krb5_acct_mgmt.o \
		krb5_password.o \
		krb5_session.o \
		utils.o

OBJECTS=	$(PRIV_OBJ)

include 	../../Makefile.pam_modules
include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

CPPFLAGS +=	-I../../../gss_mechs/mech_krb5/include \
		-I$(SRC)/uts/common/gssapi/include/ \
		-I$(SRC)/uts/common/gssapi/mechs/krb5/include \
		-I$(SRC)/lib/gss_mechs/mech_krb5/include/krb5 \
		-I$(SRC)/lib/gss_mechs/mech_krb5 \
		-I$(SRC)/lib/krb5 \
		-I.

# c99=%all supports inline keyword that appears in some krb header files
C99MODE=	-xc99=%all
# erroff=E_SUPPRESSION_DIRECTIVE_UNUSED used to deal with lint suppression
# keywords that are not consistently used in krb header files
C99LMODE=	-Xc99=%all -erroff=E_SUPPRESSION_DIRECTIVE_UNUSED

# module needs to be unloadable because the key destructor might be
# called after dlclose()
DYNFLAGS +=	$(ZNODELETE)

CLOBBERFILES += $(LINTLIB) $(LINTOUT) $(POFILE)

#
# Don't lint derived files
#
lint    :=      SRCS= $(PRIV_OBJ:%.o=$(SRCDIR)/%.c)

all:	$(LIBS)

lint:	lintcheck

include	$(SRC)/lib/Makefile.targ
