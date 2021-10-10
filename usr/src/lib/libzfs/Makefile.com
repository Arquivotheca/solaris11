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

LIBRARY= libzfs.a
VERS= .1

OBJS_SHARED=			\
	zfs_comutil.o		\
	zfs_deleg.o		\
	zfs_fletcher.o		\
	zfs_namecheck.o		\
	zfs_prop.o		\
	zcrypt_common.o		\
	zpool_prop.o		\
	zprop_common.o

OBJS_COMMON=			\
	libzfs_changelist.o	\
	libzfs_config.o		\
	libzfs_crypto.o		\
	libzfs_dataset.o	\
	libzfs_diff.o		\
	libzfs_fru.o		\
	libzfs_graph.o		\
	libzfs_import.o		\
	libzfs_mount.o		\
	libzfs_pool.o		\
	libzfs_sendrecv.o	\
	libzfs_shadow.o		\
	libzfs_share.o		\
	libzfs_status.o		\
	libzfs_util.o

OBJECTS= $(OBJS_COMMON) $(OBJS_SHARED)

include $(SRC)/lib/Makefile.lib

# libzfs must be installed in the root filesystem for mount(1M)
include $(SRC)/lib/Makefile.rootfs

LIBS=	$(DYNLIB) $(LINTLIB)

SRCDIR =	../common

INCS += -I$(SRCDIR)
INCS += -I$(SRC)/uts/common/fs/zfs
INCS += -I$(SRC)/common/zfs
INCS += -I$(SRC)/lib/libc/inc
INCS += -I$(SRC)/uts/common/brand/solaris10

C99MODE=	-xc99=%all -K PIC 
C99LMODE=	-Xc99=%all

# libcurl (which comes from a different consolidaiton)
# has some declarations that are inconsistent with the C99
# definitions that get used with libzfs, rather than turn off
# all of those lint warnings we just don't lint against libcurl.

LDLIBS +=	-lc -lm -ldevid -lgen -lnvpair -luutil -lavl -lefi -lshare \
	-ladm -lidmap -ltsol -lmd -lumem -lcryptoutil -lpkcs11 -lkmf -ldevinfo

$(DYNLIB) := LDLIBS += -lcurl

CPPFLAGS +=	$(INCS) -D_LARGEFILE64_SOURCE=1 -D_REENTRANT

SRCS=	$(OBJS_COMMON:%.o=$(SRCDIR)/%.c)	\
	$(OBJS_SHARED:%.o=$(SRC)/common/zfs/%.c)

$(LINTLIB) := SRCS=	$(SRCDIR)/$(LINTSRC)

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

pics/%.o: ../../../common/zfs/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

include $(SRC)/lib/Makefile.targ
