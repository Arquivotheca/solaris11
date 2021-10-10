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
# uts/intel/config/Makefile
# Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
#
#	This makefile drives the production of the kernel configuration files.
#
#	intel architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Include common rules.
#
include $(UTSBASE)/intel/Makefile.intel

#
#	Define the module and object file sets.
#
SRCDIR		= $(UTSBASE)/intel/os
SRCDRIVERDRV	= $(UTSBASE)/common/etc/driver/drv

PATH2INST	= path_to_inst
CONFIGFILES	= name_to_major minor_perm driver_aliases name_to_sysnum \
		  driver_classes dacf.conf $(PATH2INST) mach
SECCONFIG	= device_policy priv_names
BOOTIMAGE	= bootbg1050.png bootbg1200.png bootbg480.png bootbg800.png
BOOTANIM	= spinner0.png spinner1.png spinner2.png spinner3.png \
		  spinner4.png spinner5.png spinner6.png spinner7.png \
		  spinner8.png spinner9.png spinner10.png spinner11.png \
		  spinner12.png spinner13.png spinner14.png
SHUTDOWNANIM	= bar0.png bar1.png bar2.png bar3.png bar4.png bar5.png \
		  bar6.png bar7.png bar8.png bar9.png bar10.png bar11.png \
		  bar12.png bar13.png bar14.png bar15.png bar16.png bar17.png \
		  bar18.png bar19.png
SHUTDOWNIMAGE	= shutdownbg1050.png shutdownbg1200.png shutdownbg480.png \
		  shutdownbg800.png

BOOTENVRC	= bootenv.rc
DEVMASTER	= master
DRIVERREADME	= README.driver

FILEMODE	= 644
ROOTETC		= $(ROOT)/etc
ROOTETCSEC	= $(ROOTETC)/security
ROOTBOOT	= $(ROOT)/boot
BOOTSOLARIS	= $(ROOTBOOT)/solaris
BOOTDEVICEDB	= $(BOOTSOLARIS)/devicedb
BOOTIMAGES	= $(BOOTSOLARIS)/boot-images
IMAGES		= $(BOOTSOLARIS)/images

ROOTKERNEL	= $(ROOT)/kernel
ROOTKERNELDRV	= $(ROOTKERNEL)/drv
ROOTETCDRIVER	= $(ROOTETC)/driver
ROOTETCDRIVERDRV = $(ROOTETCDRIVER)/drv

SRCFILES	= $(CONFIGFILES:%=$(SRCDIR)/%) $(SECCONFIG:%=$(SRCDIR)/%)
SRCFILES	+= $(BOOTIMAGE:%=$(SRCDIR)/%)
SRCFILES	+= $(BOOTANIM:%=$(SRCDIR)/%)
SRCFILES	+= $(SHUTDOWNANIM:%=$(SRCDIR)/%)
SRCFILES	+= $(SHUTDOWNIMAGE:%=$(SRCDIR)/%)
SRCFILES	+= $(BOOTENVRC:%=$(SRCDIR)/%)
SRCFILES	+= $(DEVMASTER:%=$(SRCDIR)/%)
SRCFILES	+= $(DRIVERREADME:%=$(SRCDRIVERDRV)/%)

ROOTCONFIGFILES	= $(CONFIGFILES:%=$(ROOTETC)/%) $(SECCONFIG:%=$(ROOTETCSEC)/%)
ROOTCONFIGFILES	+= $(BOOTIMAGE:%=$(BOOTIMAGES)/%)
ROOTCONFIGFILES	+= $(BOOTANIM:%=$(BOOTIMAGES)/%)
ROOTCONFIGFILES += $(SHUTDOWNANIM:%=$(IMAGES)/%)
ROOTCONFIGFILES += $(SHUTDOWNIMAGE:%=$(IMAGES)/%)
ROOTCONFIGFILES	+= $(BOOTENVRC:%=$(BOOTSOLARIS)/%)
ROOTCONFIGFILES	+= $(DEVMASTER:%=$(BOOTDEVICEDB)/%)
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
#	etc/path_to_inst has permissions different from the other config files
#
$(ROOTETC)/$(PATH2INST):= FILEMODE=444

$(ROOTETC)/%: $(SRCDIR)/%
	$(INS.file)

$(ROOTETCSEC)/%: $(SRCDIR)/%
	$(INS.file)

$(ROOTBOOT):
	$(INS) -s -d -m $(DIRMODE) $@

$(ROOTBOOT)/%: $(ROOTBOOT) $(SRCDIR)/%
	$(INS.file)

$(BOOTSOLARIS):
	$(INS) -s -d -m $(DIRMODE) $@

$(BOOTIMAGES):
	$(INS) -s -d -m $(DIRMODE) $@

$(IMAGES):
	$(INS) -s -d -m $(DIRMODE) $@

$(BOOTSOLARIS)/%: $(BOOTSOLARIS) $(SRCDIR)/%
	$(INS.file)

$(BOOTIMAGES)/%: $(BOOTIMAGES) $(SRCDIR)/%
	$(INS.file)

$(IMAGES)/%: $(IMAGES) $(SRCDIR)/%
	$(INS.file)

$(BOOTDEVICEDB):
	$(INS) -s -d -m $(DIRMODE) $@

$(BOOTDEVICEDB)/%: $(BOOTDEVICEDB) $(SRCDIR)/%
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
include $(UTSBASE)/intel/Makefile.targ
