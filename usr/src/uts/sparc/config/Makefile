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
# uts/sparc/config/Makefile
# Copyright (c) 1991, 2011, Oracle and/or its affiliates. All rights reserved.
#
#	This makefile drives the production of the kernel configuration files.
#
#	sparc architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Define the module and object file sets.
#
SRCDIR		= $(UTSBASE)/sparc/os
SRCDRIVERDRV	= $(UTSBASE)/common/etc/driver/drv

PATH2INST	= path_to_inst
CONFIGFILES	= name_to_major minor_perm driver_aliases name_to_sysnum \
		  driver_classes dacf.conf $(PATH2INST)
SECCONFIG	= device_policy priv_names
DRIVERREADME	= README.driver

FILEMODE	= 644
ROOTETC		= $(ROOT)/etc
ROOTETCSEC	= $(ROOTETC)/security
ROOTKERNEL	= $(ROOT)/kernel
ROOTKERNELDRV	= $(ROOTKERNEL)/drv
ROOTETCDRIVER	= $(ROOTETC)/driver
ROOTETCDRIVERDRV = $(ROOTETCDRIVER)/drv

SRCFILES	= $(CONFIGFILES:%=$(SRCDIR)/%) $(SECCONFIG:%=$(SRCDIR)/%)
SRCFILES	+= $(DRIVERDRVREADME:%=$(SRCDRIVERDRV)/%)

ROOTCONFIGFILES	= $(CONFIGFILES:%=$(ROOTETC)/%) $(SECCONFIG:%=$(ROOTETCSEC)/%)
ROOTCONFIGFILES	+= $(DRIVERREADME:%=$(ROOTKERNELDRV)/%)
ROOTCONFIGFILES	+= $(DRIVERREADME:%=$(ROOTETCDRIVERDRV)/%)

LINKTARGET	= ../../../kernel/drv/$(DRIVERREADME)

$(ROOTKERNELDRV)/$(DRIVERREADME) := FILEMODE = 0444

#
#	Default build targets.
#
.KEEP_STATE:

def all: $(SRCFILES)

install: all $(ROOTCONFIGFILES)

#
#	The ROOTETC directory is made by ../../../Targetdirs
#	through the rootdirs target in ../../../Makefile.
#
#     etc/path_to_inst has permissions different from the other config files
#
$(ROOTETC)/$(PATH2INST):= FILEMODE=444

$(ROOTETC)/%: $(SRCDIR)/%
	$(INS.file)

$(ROOTETCSEC)/%: $(SRCDIR)/%
	$(INS.file)

$(ROOTKERNELDRV)/%: $(SRCDRIVERDRV)/%
	$(INS.file)

$(ROOTETCDRIVERDRV)/%: $(SRCDRIVERDRV)/%
	$(SYMLINK) $(LINKTARGET) $@

$(SRCDIR)/priv_names: $(PRIVS_DEF) $(PRIVS_AWK)
	$(NAWK) -f $(PRIVS_AWK) < $(PRIVS_DEF) -v pnamesfile=$@

clean clobber lint:

# 
#	No lints, so keep the clobber and clean targets reasonable. 
# 
CLEANLINTFILES	= Nothing_to_remove
CLEANFILES	= Nothing_to_remove
CLOBBERFILES	= Nothing_to_remove

#
#	Include common targets.
#
include $(UTSBASE)/sparc/Makefile.targ
