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
# Copyright (c) 1990, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG = halt

include ../Makefile.cmd

#
# Currently Fast Reboot is only supported on x86.
# A new propery config/uadmin_boot_archive_sync is added to
# boot-config service. Which needs a support on sparc also.
#
sparc_SUBDIRS = smf.sparc
i386_SUBDIRS = smf.i386
SUBDIRS = $($(MACH)_SUBDIRS)

ROOTLINKS = $(ROOTUSRSBIN)/poweroff $(ROOTUSRSBIN)/reboot

FILEMODE = 0755

.KEEP_STATE:

CFLAGS_i386 = -DUSE_GRUB -DARCH_X86 -DELF_LSB
CFLAGS += $(CFLAGS_$(MACH))

CPPFLAGS += -I../../lib/libzpool/common
CPPFLAGS += -I../../lib/libscf/inc
CPPFLAGS += -I../../uts/common/fs/zfs

LDLIBS += -lbsm -lscf -lzfs -lgen
LDLIBS_i386 +=	-lbe
LDLIBS +=	$(LDLIBS_$(MACH))

CLOBBERFILES += $(ROOTLINKS)

all     :=	TARGET = all
install :=	TARGET = install
clean   :=	TARGET = clean
clobber :=	TARGET = clobber
lint    :=	TARGET = lint
lint	:=	LINTFLAGS = -u


all: $(PROG) 

install: all $(ROOTUSRSBINPROG) $(ROOTLINKS) $(SUBDIRS)

$(ROOTLINKS): $(ROOTUSRSBINPROG)
	$(RM) $@
	$(LN) $(ROOTUSRSBINPROG) $@

$(SUBDIRS):	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

clean:

clobber: $(SUBDIRS)

lint:	$$(PROG).c
	$(LINT.c) $(PROG).c $(LDLIBS) $(CFLAGS_$(MACH))

check: $(CHKMANIFEST)

FRC:

include ../Makefile.targ
