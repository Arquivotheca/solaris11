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

PROG= kvno

OBJS	= kvno.o                       
SRCS	= $(OBJS:.o=.c)

include ../../Makefile.cmd
include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

TEXT_DOMAIN = SUNW_OST_OSCMD
POFILE = kvno.po
POFILES = generic.po

DEFS = -DHAVE_UNISTD_H

CPPFLAGS += -I$(SRC)/lib/gss_mechs/mech_krb5/include \
	-I$(SRC)/uts/common/gssapi/mechs/krb5/include \
	$(DEFS)

# c99=%all supports the inline keyword found in some krb header files
C99MODE=	-xc99=%all
C99LMODE=	-Xc99=%all

LDFLAGS += $(KRUNPATH)
LDLIBS += $(KMECHLIB)

.KEEP_STATE:

all: $(PROG)

install: $(KRB5PROG)

clean:
	$(RM) $(OBJS)

lint: lint_SRCS

include ../../Makefile.targ

$(POFILE): $(DERIVED_FILES) .WAIT $(POFILES) 
	$(RM) $@ 
	$(CAT) $(POFILES) > $@ 

$(POFILES):
	$(RM) messages.po
	$(XGETTEXT) $(XGETFLAGS) `($(GREP) -l gettext *.[ch] || echo /dev/null)`
	$(SED) "/^domain/d" messages.po > $@
	$(RM) messages.po
