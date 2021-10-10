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
# Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY=	libparted.a
VERS=		.8

PARTED_VERSION=	2.3.0

#
# All relative to SRCDIR
#

PLIBDIR=	lib
LIBPDIR=	libparted
LIBPADIR=	libparted/arch
LIBPCSDIR=	libparted/cs
LIBAMIGAFS=	libparted/fs/amiga
LIBEXT2FS=	libparted/fs/ext2
LIBFATFS=	libparted/fs/fat
LIBHFS=		libparted/fs/hfs
LIBJFS=		libparted/fs/jfs
LIBLINUXSWAP=	libparted/fs/linux_swap
LIBNTFS=	libparted/fs/ntfs
LIBREISERFS=	libparted/fs/reiserfs
LIBSOLARISX86=	libparted/fs/solaris_x86
LIBUFS=		libparted/fs/ufs
LIBXFS=		libparted/fs/xfs
LIBLABELS=	libparted/labels


OBJECTS=	$(PLIBDIR)/argmatch.o \
		$(PLIBDIR)/basename.o		$(PLIBDIR)/quotearg.o \
		$(PLIBDIR)/basename-lgpl.o \
		$(PLIBDIR)/close-stream.o	$(PLIBDIR)/closeout.o \
		$(PLIBDIR)/dirname.o		$(PLIBDIR)/safe-read.o \
		$(PLIBDIR)/dirname-lgpl.o	$(PLIBDIR)/error.o \
		$(PLIBDIR)/exitfail.o		$(PLIBDIR)/stripslash.o \
		$(PLIBDIR)/strndup.o \
		$(PLIBDIR)/version-etc-fsf.o \
		$(PLIBDIR)/localcharset.o	$(PLIBDIR)/version-etc.o \
		$(PLIBDIR)/xalloc-die.o		$(PLIBDIR)/xmalloc.o \
		$(PLIBDIR)/xstrndup.o		$(PLIBDIR)/progname.o \
		$(PLIBDIR)/quote.o		$(LIBPDIR)/architecture.o \
		$(LIBPDIR)/debug.o		$(LIBPDIR)/exception.o \
		$(LIBPDIR)/device.o		$(LIBPDIR)/filesys.o \
		$(LIBPDIR)/timer.o		$(LIBPDIR)/unit.o \
		$(LIBPDIR)/disk.o		$(LIBPDIR)/libparted.o \
		$(LIBPADIR)/solaris.o \
		$(LIBPCSDIR)/constraint.o	$(LIBPCSDIR)/geom.o \
		$(LIBPCSDIR)/natmath.o \
		$(LIBAMIGAFS)/affs.o		$(LIBAMIGAFS)/amiga.o  \
		$(LIBAMIGAFS)/apfs.o		$(LIBAMIGAFS)/asfs.o  \
		$(LIBAMIGAFS)/interface.o \
		$(LIBEXT2FS)/interface.o	$(LIBEXT2FS)/ext2.o \
		$(LIBEXT2FS)/ext2_inode_relocator.o \
		$(LIBEXT2FS)/parted_io.o	$(LIBEXT2FS)/ext2_meta.o \
		$(LIBEXT2FS)/ext2_block_relocator.o \
		$(LIBEXT2FS)/ext2_mkfs.o	$(LIBEXT2FS)/tune.o \
		$(LIBEXT2FS)/ext2_buffer.o	$(LIBEXT2FS)/ext2_resize.o \
		$(LIBFATFS)/table.o		$(LIBFATFS)/bootsector.o \
		$(LIBFATFS)/clstdup.o		$(LIBFATFS)/count.o \
		$(LIBFATFS)/fatio.o		$(LIBFATFS)/traverse.o \
		$(LIBFATFS)/calc.o		$(LIBFATFS)/context.o \
		$(LIBFATFS)/fat.o		$(LIBFATFS)/resize.o \
		$(LIBHFS)/cache.o		$(LIBHFS)/probe.o \
		$(LIBHFS)/advfs.o		$(LIBHFS)/hfs.o \
		$(LIBHFS)/file.o		$(LIBHFS)/reloc.o \
		$(LIBHFS)/advfs_plus.o		$(LIBHFS)/journal.o \
		$(LIBHFS)/file_plus.o		$(LIBHFS)/reloc_plus.o \
		$(LIBJFS)/jfs.o \
		$(LIBLINUXSWAP)/linux_swap.o \
		$(LIBNTFS)/ntfs.o \
		$(LIBREISERFS)/geom_dal.o	$(LIBREISERFS)/reiserfs.o \
		$(LIBSOLARISX86)/solaris_x86.o \
		$(LIBUFS)/ufs.o \
		$(LIBXFS)/xfs.o \
		$(LIBLABELS)/dos.o		$(LIBLABELS)/efi_crc32.o \
		$(LIBLABELS)/mac.o		$(LIBLABELS)/sun.o \
		$(LIBLABELS)/aix.o		$(LIBLABELS)/dvh.o \
		$(LIBLABELS)/gpt.o		$(LIBLABELS)/pc98.o \
		$(LIBLABELS)/bsd.o		$(LIBLABELS)/loop.o \
		$(LIBLABELS)/pt-tools.o		$(LIBLABELS)/rdb.o

# include library definitions
include		../../Makefile.lib

SRCDIR =	../common

C99MODE=	$(C99_ENABLE)
CERRWARN +=	-erroff=E_EXTERN_INLINE_UNDEFINED
CERRWARN +=	-erroff=E_CONST_PROMOTED_UNSIGNED_LONG

LIBS =		$(DYNLIB)

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-I$(SRCDIR)/lib -I$(SRCDIR)/include -I$(SRCDIR)/libparted
DYNFLAGS +=	$(ZINTERPOSE)
LDLIBS +=	-ldiskmgt -luuid -lc -lnvpair

LIBPCSRC =	libparted.pc
ROOTLIBPCDIR =	$(ROOT)/usr/lib/pkgconfig
ROOTLIBPC =	$(LIBPCSRC:%=$(ROOTLIBPCDIR)/%)

.KEEP_STATE:

#
# This open source is exempted from lint
#
lint:

all:

$(ROOTLIBPCDIR):
	$(INS.dir)

$(ROOTLIBPC): $(ROOTLIBPCDIR) $(LIBPCSRC)
	$(INS.file) $(LIBPCSRC)

$(LIBPCSRC): ../common/$(LIBPCSRC).in
	$(SED)	-e "s@__VERSION__@$(PARTED_VERSION)@" \
		 < ../common/$(LIBPCSRC).in > $(LIBPCSRC)

# include library targets
include		../../Makefile.targ
