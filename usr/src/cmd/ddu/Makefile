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
# Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
#

include $(SRC)/Makefile.master

i386_dir= \
	bat_detect

sparc_dir=

SUBDIRS= \
	lib \
	all_devices \
	cd_detect \
	dmi \
	find_driver \
	hd_detect \
	media_event \
	AI \
	config \
	data \
	ddu-text \
	help \
	scripts \
	utils \
	$($(MACH)_dir)

LINTSUBDIRS= \
	lib \
	all_devices \
	dmi \
	cd_detect \
	find_driver \
	hd_detect \
	media_event \
	$($(MACH)_dir)

include Makefile.com

DDUDIRS= \
	$(ROOTUSRLIBPY_2.6_DDU) \
	$(ROOTUSRSHAREPIX) \
	$(ROOTUSRSHAREAPP) \
	$(ROOTDDU) \
	$(ROOTDDU)/bin \
	$(ROOTDDU)/bin/$(MACH) \
	$(ROOTDDU)/data \
	$(ROOTDDU)/ddu-text \
	$(ROOTDDU)/ddu-text/utils \
	$(ROOTDDU)/help \
	$(ROOTDDU)/help/gnome \
	$(ROOTDDU)/help/gnome/help \
	$(ROOTDDU)/help/gnome/help/ddu \
	$(ROOTDDU)/help/gnome/help/ddu/C \
	$(ROOTDDU)/help/gnome/help/ddu/C/figures \
	$(ROOTDDU)/lib \
	$(ROOTDDU)/scripts \
	$(ROOTDDU)/utils

PYSRCS = \
	ddu.py
CONFFILES = \
	ddu.conf

PYOBJS =	$(PYSRCS:%.py=%.pyc)
PYFILES = $(PYSRCS) $(PYOBJS)
ROOTUSRBINDDU = $(ROOTUSRBIN)/ddu

$(ROOTDDUCONFFILES) := FILEMODE=      644

all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint

all: $(PYOBJS) $(SUBDIRS)

install: $(ROOTDDUCONFFILES) $(ROOTUSRBINDDU) $(SUBDIRS)

clean: $(SUBDIRS)
	$(RM) *.pyc

clobber: $(SUBDIRS)
	$(RM) *.pyc
	$(RM) -rf $(ROOTDDU) $(ROOTUSRLIBPY_2.6_DDU)

lint: $(LINTSUBDIRS)

$(ROOTUSRBINDDU): $(ROOTDDUPYFILES)
	$(RM) $@; $(SYMLINK) ../ddu/ddu.py $@

$(ROOTDDUCONFFILES) $(ROOTDDUPYFILES): $(DDUDIRS) $(PYFILES)

$(SUBDIRS): FRC	
	@cd $@; pwd; $(MAKE) $(TARGET)

$(DDUDIRS):
	$(INS.dir)
FRC:
