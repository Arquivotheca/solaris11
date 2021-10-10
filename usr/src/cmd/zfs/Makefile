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
# Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG=		zfs
OBJS=		zfs_main.o zfs_iter.o
SRCS=		$(OBJS:%.o=%.c)
POFILES=	zfs_main.po zfs_iter.po
POFILE=		zfs.po

include ../Makefile.cmd
include ../Makefile.ctf

FSTYPE=         zfs
LINKPROGS=	mount umount
ROOTETCFSTYPE=  $(ROOTETC)/fs/$(FSTYPE)
USRLIBFSTYPE=	$(ROOTLIB)/fs/$(FSTYPE)

LDLIBS += -lzfs -luutil -lumem -lnvpair -lshare

INCS += -I../../common/zfs

C99MODE=	-xc99=%all
C99LMODE=	-Xc99=%all

CPPFLAGS += -D_LARGEFILE64_SOURCE=1 -D_REENTRANT $(INCS)
$(NOT_RELEASE_BUILD)CPPFLAGS += -DDEBUG

# lint complains about unused _umem_* functions
LINTFLAGS += -xerroff=E_NAME_DEF_NOT_USED2 
LINTFLAGS64 += -xerroff=E_NAME_DEF_NOT_USED2 

USRLIBFSTYPELINKS = $(LINKPROGS:%=$(USRLIBFSTYPE)/%)

CLOBBERFILES += $(USRLIBFSTYPELINKS) $(ROOTUSRSBINPROG) $(ROOTETCFSTYPELINK)

.KEEP_STATE:

.PARALLEL:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTUSRSBINPROG) $(USRLIBFSTYPELINKS) $(ROOTETCFSTYPE)

$(POFILE): $(POFILES)
	$(RM) $@
	cat $(POFILES) > $@

clean:
	$(RM) $(OBJS)

lint:	lint_SRCS

# Links from /usr/lib/fs/zfs to /usr/sbin
$(USRLIBFSTYPELINKS):
	-$(RM) $@; $(SYMLINK) ../../../../usr/sbin/$(PROG) $@

# Links /etc/fs/zfs to /usr/lib/fs/zfs
$(ROOTETCFSTYPE):
	-$(RM) $@; $(SYMLINK) ../../usr/lib/fs/$(PROG) $@

include ../Makefile.targ
