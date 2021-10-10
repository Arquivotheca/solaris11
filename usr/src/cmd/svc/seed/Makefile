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
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#

include ../../Makefile.cmd

ETCSVC = $(ROOTETC)/svc
LIBSVCSEED = $(ROOT)/lib/svc/seed
INSTALLSEED = $(ROOT)/usr/sadm/install

#
# Because seed repository construction requires a functioning repository, a
# working svccfg(1) binary, and XML support, the following libraries must exist
# on the build system or in the proto area:  libscf, libuutil, and libxml2.
#

#
# We deliver 3 seed repositories:
#	global.db	-- for a standalone Solaris (global zone)
#	nonglobal.db	-- for a Solaris Zone
#	miniroot.db	-- for the install miniroot
#
# These files are empty but are delivered to protect outside consolidation
# scripts.  The miniroot.db is no longer used by the distro-constructor but
# is still delivered for other consolidations that might use it.
#
# Once all consolidations have transitioned, these seeds will be completely
# removed.
#

FILEMODE = 0600
SEEDFILEMODE = 0444		# seeds are not intended for editing, but may
				# be copied

# These programs are needed for checking manifests:
#
CONFIGD = ../configd/svc.configd-native
SVCCFG = ../svccfg/svccfg-native

.KEEP_STATE:

all: global.db nonglobal.db miniroot.db

$(CONFIGD): FRC
	@cd ../configd; pwd; $(MAKE) $(MFLAGS) native

$(SVCCFG): FRC
	@cd ../svccfg; pwd; $(MAKE) $(MFLAGS) native

%.db:
	$(RM) $(@)
	$(TOUCH) $(@)

install: $(CONFIGD) $(SVCCFG) install_global install_nonglobal install_miniroot

install_global: global.db
	$(RM) $(LIBSVCSEED)/global.db
	$(INS) -f $(LIBSVCSEED) -m $(SEEDFILEMODE) -s global.db

install_nonglobal: nonglobal.db
	$(RM) $(LIBSVCSEED)/nonglobal.db
	$(INS) -f $(LIBSVCSEED) -m $(SEEDFILEMODE) -s nonglobal.db

install_miniroot: $(INSTALLSEED) miniroot.db
	$(RM) $(INSTALLSEED)/miniroot.db
	$(INS) -f $(INSTALLSEED) -m $(SEEDFILEMODE) -s miniroot.db

$(INSTALLSEED):
	$(INS.dir)

clean lint:
	$(RM) common.db

clobber:
	$(RM) common.db global.db nonglobal.db miniroot.db

FRC:
