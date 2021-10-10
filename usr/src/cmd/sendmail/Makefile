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
# Copyright (c) 1983, 2011, Oracle and/or its affiliates. All rights reserved.
#
# cmd/sendmail/Makefile
#
# Makefile for top level sendmail source directory
#

# static libraries (db, libsm, libsmdb, libsmutil) must be built before
# src and aux
SUBDIRS=	cf lib db libsm libsmdb libsmutil .WAIT src aux

include 	../Makefile.cmd

ROOTDIRS=	$(ROOT)/var/spool/mqueue $(ROOT)/var/spool/clientmqueue
$(ROOTDIRS):=	DIRMODE = 0750
$(ROOT)/var/spool/clientmqueue:=	DIRMODE = 0770

all:=           TARGET= all
install:=       TARGET= install
clean:=         TARGET= clean
clobber:=       TARGET= clobber
lint:=          TARGET= lint

.KEEP_STATE:

all clean clobber lint:	$(SUBDIRS)

install: 	$(ROOTDIRS) $(SUBDIRS) $(ROOTMANIFEST) $(ROOTSVCSH)

all install:	

clobber:	FRC

.PARALLEL:	$(SUBDIRS)

$(ROOTDIRS):
	$(INS.dir)

$(SUBDIRS): 	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
