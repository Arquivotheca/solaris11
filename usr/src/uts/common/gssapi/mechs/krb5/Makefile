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
# Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
#
#
# /usr/src/uts/common/gssapi/mechs/krb5/Makefile

include ../../../../../Makefile.master

# EXPORT DELETE START

all:
	@$(ECHO) " This Makefile is used to clean up the source tree\n" \
		"for export distribution.\n" \
		"[Usage]: make [EXPORT_SRC] [CRYPT_SRC]\n\n" \
		"WARNING: EXPORT_SRC, CRYPT_SRC targets change the\n" \
		"source tree and remove the Makefile."

# Special target to clean up the source tree for export distribution
# Warning: This target changes the source tree
EXPORT_SRC:
	$(RM) krb5mech.c+ include/gssapiP_krb5.h+
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< include/gssapiP_krb5.h > include/gssapiP_krb5.h+
	$(MV) include/gssapiP_krb5.h+ include/gssapiP_krb5.h
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< krb5mech.c > krb5mech.c+
	$(MV) krb5mech.c+ krb5mech.c

	$(RM) crypto/des/f_cbc.c+ crypto/des/f_cksum.c+ \
	crypto/des/d3_cbc.c+ mech/gssapi_krb5.c+ mech/seal.c+ mech/unseal.c+

	$(SED) -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< mech/gssapi_krb5.c > mech/gssapi_krb5.c+
	$(MV) mech/gssapi_krb5.c+ mech/gssapi_krb5.c

	$(SED) -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< mech/seal.c > mech/seal.c+
	$(MV) mech/seal.c+ mech/seal.c

	$(SED) -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< mech/unseal.c > mech/unseal.c+
	$(MV) mech/unseal.c+ mech/unseal.c

	$(SED) -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< crypto/des/f_cbc.c > crypto/des/f_cbc.c+
	$(MV) crypto/des/f_cbc.c+ crypto/des/f_cbc.c

	$(SED) -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< crypto/des/d3_cbc.c > crypto/des/d3_cbc.c+
	$(MV) crypto/des/d3_cbc.c+ crypto/des/d3_cbc.c

	$(SED) -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< crypto/des/f_cksum.c > crypto/des/f_cksum.c+
	$(MV) crypto/des/f_cksum.c+ crypto/des/f_cksum.c

	$(RM) ../../../Makefile.files+
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< ../../../Makefile.files > ../../../Makefile.files+
	$(MV) ../../../Makefile.files+ ../../../Makefile.files

	$(RM) Makefile+
	sed -e "/^# EXPORT DELETE START/,/^# EXPORT DELETE END/d" \
		< Makefile > Makefile+
	$(MV) Makefile+ Makefile

	$(CHMOD) 444 krb5mech.c include/gssapiP_krb5.h crypto/des/f_cbc.c \
		crypto/des/f_cksum.c crypto/des/d3_cbc.c \
		mech/gssapi_krb5.c mech/seal.c mech/unseal.c

# CRYPT DELETE START
# Special target to clean up the source tree for domestic distribution
# Warning: This target changes the source tree
CRYPT_SRC:
	$(RM) krb5mech.c+ mech/gssapi_krb5.c+
	sed -e "/CRYPT DELETE START/,/CRYPT DELETE END/d" \
		< krb5mech.c > krb5mech.c+
	$(MV) krb5mech.c+ krb5mech.c

	$(SED) -e "/CRYPT DELETE START/,/CRYPT DELETE END/d" \
		< mech/gssapi_krb5.c > mech/gssapi_krb5.c+
	$(MV) mech/gssapi_krb5.c+ mech/gssapi_krb5.c

	$(RM)  Makefile+
	sed -e "/^# CRYPT DELETE START/,/^# CRYPT DELETE END/d" \
		< Makefile > Makefile+
	$(MV) Makefile+ Makefile

	$(CHMOD) 444 krb5mech.c mech/gssapi_krb5.c Makefile
# CRYPT DELETE END
# EXPORT DELETE END
