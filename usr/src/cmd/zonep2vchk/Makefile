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

include ../Makefile.cmd

.SUFFIXES:      .ksh

# Add micro version to script by applying changeset id.
.ksh:
	$(RM) $@
	@micro=`hg log -l 1 --template '{rev}' $<'` ; \
	micro=$(RELEASE_MAJOR)-$(RELEASE_MINOR)-$$micro ; \
	$(SED) "s/__MICRO__/$$micro/" $< > $@
	$(CHMOD) +x $@

# Make po file based on calls to gettext.  This script handles multi-line
# gettexts, as well as escaped (\") "'s.
.so.po .ksh.po:
	./make_po $< > $@

# Generate separate po file.  This allows translated locals (mo files) to
# be included with the unbundled version.
TEXT_DOMAIN=SUNW_OST_ZONEP2VCHK
PROG=	zonep2vchk
POMAKER= make_po
MOFILES=zonep2vchk.mo

.KEEP_STATE:

all: $(PROG)

install: all .WAIT $(ROOTUSRSBINPROG)

clean:

lint:

_msg: $(PROG) $(POMAKER)

include ../Makefile.targ
