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
# lib/gss_mechs/mech_dh/backend/Makefile
#
PROTOCOL_DIR = mech

PROTO_FILE = dhmech_prot.x
DERIVED_FILES = mech/dhmech_prot.h mech/xdr_token.c
CLEANFILES += $(DERIVED_FILES)

# include library definitions
include ../../../Makefile.lib

TEXT_DOMAIN =	SUNW_OST_NETRPC
POFILES =	generic.po
POFILE = mech_dh.po

SED=    sed
GREP=   grep

all :=          TARGET= all
clean :=        TARGET= clean
clobber :=      TARGET= clobber
install :=      TARGET= install
_msg :=		TARGET= _msg

.KEEP_STATE:
all:            $(DERIVED_FILES)

install:        $(DERIVED_FILES)

#
# Rules for building the derived files
#
mech/xdr_token.c: $(PROTOCOL_DIR)/dhmech_prot.x
	$(RPCGEN) -c $(PROTOCOL_DIR)/dhmech_prot.x |\
	$(SED) -e 's!$(PROTOCOL_DIR)/dhmech_prot.h!dhmech_prot.h!' > $@

mech/dhmech_prot.h: mech/dhmech_prot.x
	$(RPCGEN) -h $(PROTOCOL_DIR)/dhmech_prot.x > $@

# include library targets
#include ../../../Makefile.targ

# EXPORT DELETE START
# Special target to clean up the source tree for export distribution
# Warning: This target changes the source tree
EXPORT_SRC:
	$(RM) Makefile+ mech/crypto.c+ mech/dhmech.c+
	$(SED) -e "/^# EXPORT DELETE START/,/^# EXPORT DELETE END/d"	\
			< Makefile > Makefile+
	$(MV) Makefile+ Makefile
	$(SED) -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< mech/crypto.c > mech/crypto.c+
	$(MV) mech/crypto.c+ mech/crypto.c
	$(SED) -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< mech/dhmech.c > mech/dhmech.c+
	$(MV) mech/dhmech.c+ mech/dhmech.c
	$(CHMOD) 444 Makefile mech/crypto.c mech/dhmech.c

# CRYPT DELETE START
CRYPT_SRC:
	$(RM) Makefile+ mech/dhmech.c+
	$(SED) -e "/^# CRYPT DELETE START/,/^# CRYPT DELETE END/d"	\
			< Makefile				\
	   | $(SED) -e "/EXPORT DELETE/d"				\
			> Makefile+
	$(MV) Makefile+ Makefile
	$(SED) -e "/CRYPT DELETE START/,/CRYPT DELETE END/d" \
			< mech/dhmech.c > mech/dhmech.c+
	$(MV) mech/dhmech.c+ mech/dhmech.c
	$(CHMOD) 444 Makefile mech/dhmech.c

# CRYPT DELETE END
# EXPORT DELETE END

clean:
	-$(RM) $(CLEANFILES)

clobber:
	-$(RM) $(CLEANFILES)

_msg: $(MSGDOMAIN) $(POFILE)
	$(RM) $(MSGDOMAIN)/$(POFILE)
	$(CP) $(POFILE) $(MSGDOMAIN)

$(POFILE): $(DERIVED_FILES) .WAIT $(POFILES)
	$(RM) $@
	$(CAT) $(POFILES) > $@

generic.po:
	$(RM) messages.po
	$(XGETTEXT) $(XGETFLAGS) `$(GREP) -l gettext mech/*.[ch]`
	$(SED) "/^domain/d" messages.po > $@
	$(RM) messages.po

$(MSGDOMAIN):
	$(INS.dir)
