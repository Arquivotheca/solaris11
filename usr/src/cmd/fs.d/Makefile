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

# The filesystem independent utilities clri, fsdb, fssnap, dcopy, labelit, and
# mkfs are all synonyms for the 'fsexec' dispatching program. They are all then
# links to the same object. This is accomplished by:
#
#	1) building fsexec from fsexec.c
#	2) installing it to /usr/lib/fsexec
#	3) linking the others to fsexec.
#
# ncheck is also linked to ff.

DFPROG=		df
PROG=		$(DFPROG) fsck volcopy ff
ROOTFS_PROG=	mount umount
# /usr/xpg4/bin/df is a symlink to /usr/bin/df
ROOTXPG4SYMLINK=	$(ROOTXPG4BIN)/df
FSEXECPROG=	fsexec
MNTTAB=		mnttab
DEFAULTFILES=	fs.dfl

include ../Makefile.cmd

SUBDIR1= lofs zfs
SUBDIR2= dev fd pcfs nfs hsfs proc ctfs udfs ufs tmpfs autofs \
		mntfs objfs sharefs smbclnt reparsed uvfs quota
SUBDIRS= $(SUBDIR1) $(SUBDIR2)
I18NDIRS= $(SUBDIR2)

all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint
_msg:=          TARGET= catalog

USRLIBF=	$(FSEXECPROG)
USRSBINF=	df fsck volcopy ff
USRSBINFSEXEC=	clri dcopy fsdb fssnap labelit mkfs
USRSBINFF=	ncheck

USRBIN2USRSBIN=	df

FSLIB=		fslib.o

ROOTUSRSBINPROG = $(ROOTFS_PROG:%=$(ROOTUSRSBIN)/%)

ROOTUSRLIBF=		$(USRLIBF:%=$(ROOTLIB)/%)
ROOTUSRSBINF=		$(USRSBINF:%=$(ROOTUSRSBIN)/%)
ROOTUSRSBINFSEXEC=	$(USRSBINFSEXEC:%=$(ROOTUSRSBIN)/%)
ROOTUSRSBINFF=		$(USRSBINFF:%=$(ROOTUSRSBIN)/%)
ROOTETCMNTTAB=		$(MNTTAB:%=$(ROOTETC)/%)
SYMUSRBIN2USRSBIN=	$(USRBIN2USRSBIN:%=$(ROOTBIN)/%)
SYMDEVNM=		$(ROOTUSRSBIN)/devnm

CPPFLAGS += -D_LARGEFILE64_SOURCE
$(FSEXECPROG) :=	LDLIBS += -lkstat

$(ROOTETCMNTTAB) := FILEMODE = 444

# for messaging catalog
#
POFILE= fs.d.po
POFILES1= $(PROG:%=%.po) $(ROOTFS_PROG:%=%.po) fsexec.po fssnapsup.po
POFILES2= $(I18NDIRS:%=%/%.po)
POFILES=  $(POFILES1) $(POFILES2)
volcopy.po :=   XGETFLAGS += -a -x volcopy.xcl
$(DFPROG).po := XGETFLAGS += -a -x df.xcl

.KEEP_STATE:

# This is too intense when building the whole world.
# .PARALLEL:	$(SUBDIRS)

all:		$(FSLIB) .WAIT $(SUBDIRS) .WAIT all_local

_msg: $(I18NDIRS) $(POFILES1)
	$(RM) $(POFILE)
	cat $(POFILES) > $(POFILE)
	$(RM) $(MSGDOMAIN)/$(POFILE)
	cp $(POFILE) $(MSGDOMAIN)

all_local:	$(PROG) $(ROOTFS_PROG) $(FSEXECPROG) $(MNTTAB) \
		$(DEFAULTFILES)

ff volcopy: deffs.o $$(@F).o
		$(LINK.c) -o $@ $@.o deffs.o $(LDLIBS)
		$(POST_PROCESS)

df: deffs.o $(FSLIB) $$(@F).o
		$(LINK.c) -o $@ $@.o deffs.o $(FSLIB) $(LDLIBS)
		$(POST_PROCESS)

fsck: fsck.o deffs.o preenlib.o
	$(LINK.c) -o $@ fsck.o deffs.o preenlib.o $(LDLIBS)
	$(POST_PROCESS)

mount: deffs.o mount.o $(FSLIB)
	$(LINK.c) -o $@ mount.o deffs.o $(FSLIB) $(LDLIBS)
	$(POST_PROCESS)

umount: umount.o $(FSLIB)
	$(LINK.c) -o $@ umount.o $(FSLIB) $(LDLIBS)
	$(POST_PROCESS)

$(FSEXECPROG):	fsexec.o deffs.o fssnapsup.o
	$(LINK.c) -o $@ fsexec.o deffs.o fssnapsup.o $(LDLIBS) -ldiskmgt
	$(POST_PROCESS)

install: $(FSLIB) .WAIT $(SUBDIRS) .WAIT install_local

install_local:	all_local $(ROOTUSRSBINPROG) \
		$(ROOTUSRSBINF) $(ROOTUSRLIBF) \
		$(ROOTUSRLIBFSEXEC) $(ROOTUSRSBINFSEXEC) \
		$(ROOTUSRSBINFF) $(ROOTETCMNTTAB) $(ROOTETCDEFAULTFILES) \
		$(ROOTXPG4SYMLINK) \
		$(SYMUSRBIN2USRSBIN) $(SYMDEVNM)

$(ROOTXPG4SYMLINK):	$(DFPROG)
	-$(RM)  $@
	-$(SYMLINK)     ../../bin/$(DFPROG) $@

# Links from /usr/bin to /usr/sbin such as /usr/bin/df -> ../sbin/df
$(SYMUSRBIN2USRSBIN):
	-$(RM) $@; $(SYMLINK) ../sbin/$(@F) $@

# Symlink from devnm to df in /usr/sbin
$(SYMDEVNM):
	-$(RM) $@; $(SYMLINK) ./df $@

# Symlinks to fsexec (clri, dcopy, fsdb, fssnap, labelit, mkfs)
$(ROOTUSRSBINFSEXEC):	$(ROOTLIB)/fsexec
	-$(RM) $@; $(SYMLINK) ../lib/fsexec $@

$(MNTTAB):
	touch $(MNTTAB)

fs.dfl:
	$(RM) $@; $(ECHO) "LOCAL=ufs" >$@

# Multiple names for ff (ncheck)
$(ROOTUSRSBINFF):	$(ROOTUSRSBIN)/ff
	-$(RM) $@; $(SYMLINK) ./ff $@

clean: $(SUBDIRS) .WAIT clean_local

clean_local:

clobber: $(SUBDIRS) .WAIT clobber_local

clobber_local:	clean_local
	$(RM) $(PROG) $(ROOTFS_PROG) $(FSEXECPROG) $(MNTTAB) $(DEFAULTFILES) \
	$(CLOBBERFILES)

lint:

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(MFLAGS) $(TARGET)

FRC:
