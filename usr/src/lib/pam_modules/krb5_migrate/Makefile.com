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
# Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
#
#
# usr/src/lib/pam_modules/krb5_migrate/Makefile.com
#

LIBRARY=	pam_krb5_migrate.a
VERS=		.1
OBJECTS=	krb5_migrate_authenticate.o

include		../../Makefile.pam_modules
include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

CPPFLAGS +=	-I../../../gss_mechs/mech_krb5/include \
		-I$(SRC)/uts/common/gssapi/include/ \
		-I$(SRC)/uts/common/gssapi/mechs/krb5/include \
		-I$(SRC)/lib/gss_mechs/mech_krb5/include/krb5 \
		-I$(SRC)/lib/gss_mechs/mech_krb5 \
		-I$(SRC)/lib/krb5

# c99=%all supports inline keyword that appears in some krb header files
C99MODE =       -xc99=%all
# erroff=E_SUPPRESSION_DIRECTIVE_UNUSED used to deal with lint suppression
# keywords that are not consistently used in krb header files
C99LMODE =      -Xc99=%all -erroff=E_SUPPRESSION_DIRECTIVE_UNUSED

LDLIBS +=	-lpam -lc

all:	$(LIBS)

lint:	lintcheck

include		../../../Makefile.targ
