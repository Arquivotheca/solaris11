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
# Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
#

RTLD=		ld.so.1

AVLOBJ=		avl.o
DTROBJ=		dtrace_data.o
TOOLOBJS=	alist.o strhash.o
BLTOBJ=		msg.o
ELFCAPOBJ=	elfcap.o
OBJECTS=	$(BLTOBJ) \
		$(AVLOBJ) \
		$(DTROBJ) \
		$(TOOLOBJS) \
		$(ELFCAPOBJ) \
		$(P_ASOBJS)   $(P_COMOBJS)   $(P_MACHOBJS)   $(G_MACHOBJS)  \
		$(S_ASOBJS)   $(S_COMOBJS)   $(S_MACHOBJS)   $(CP_MACHOBJS)

COMOBJS=	$(P_COMOBJS)  $(S_COMOBJS)
ASOBJS=		$(P_ASOBJS)   $(S_ASOBJS)
MACHOBJS=	$(P_MACHOBJS) $(S_MACHOBJS)
NOCTFOBJS=	$(ASOBJS)

include		$(SRC)/lib/Makefile.lib
include		$(SRC)/cmd/sgs/Makefile.com

SRCDIR =	../common
DTRDIR =	$(SRC)/common/dtrace
PLAT =		$(PLAT_$(BASEPLAT))

# DTrace needs an executable data segment.
MAPFILE.NED=

MAPFILES +=	../common/mapfile-order

# For the libc/libthread unified world:
# This library needs to be placed in /lib to allow
# dlopen() functionality while in single-user mode.
ROOTFS_DYNLIB=	$(RTLD:%=$(ROOTFS_LIBDIR)/%)
ROOTFS_DYNLIB64=	$(RTLD:%=$(ROOTFS_LIBDIR64)/%)

ROOTDYNLIB=	$(RTLD:%=$(ROOTFS_LIBDIR)/%)
ROOTDYNLIB64=	$(RTLD:%=$(ROOTFS_LIBDIR64)/%)


FILEMODE =	755

CPPFLAGS +=	-I$(SRCBASE)/lib/libc/inc \
		-I$(SRCBASE)/uts/common/krtld \
		-I$(SRCBASE)/uts/$(PLAT) \
		-I$(SRCBASE)/uts/$(PLAT)/krtld \
		-I$(SRC)/common/sgsrtcid \
		-I$(ELFCAP)

ASFLAGS=	-P -D_ASM $(CPPFLAGS)

# libc_pic.a comes from building libc, and is installed in the stub
# proto for the benefit of rtld. This requires that libc be built before
# the runtime linker. For the system's self-consistency, libc and rtld
# should be built in the same workspace.
CLIB =		-lc_pic

LDLIBS +=	$(CONV_LIB) $(CLIB) $(LDDBG_LIB) -lrtld  $(LD_LIB) 

DYNFLAGS +=	-i -e _rt_boot $(VERSREF) $(ZNODLOPEN) \
		$(ZINTERPOSE) -zdtrace=dtrace_data '-R$$ORIGIN'

BUILD.s=	$(AS) $(ASFLAGS) $< -o $@

BLTDEFS=	msg.h
BLTDATA=	msg.c
BLTMESG=	$(SGSMSGDIR)/rtld

BLTFILES=	$(BLTDEFS) $(BLTDATA) $(BLTMESG)

SGSMSGCOM=	../common/rtld.msg
SGSMSG32=	../common/rtld.32.msg
SGSMSG64=	../common/rtld.64.msg
SGSMSGSPARC=	../common/rtld.sparc.msg
SGSMSGSPARC32=	../common/rtld.sparc32.msg
SGSMSGSPARC64=	../common/rtld.sparc64.msg
SGSMSGINTEL=	../common/rtld.intel.msg
SGSMSGINTEL32=	../common/rtld.intel32.msg
SGSMSGINTEL64=	../common/rtld.intel64.msg
SGSMSGCHK=	../common/rtld.chk.msg
SGSMSGTARG=	$(SGSMSGCOM)
SGSMSGALL=	$(SGSMSGCOM) $(SGSMSG32) $(SGSMSG64) \
		$(SGSMSGSPARC) $(SGSMSGSPARC32) $(SGSMSGSPARC64) \
		$(SGSMSGINTEL) $(SGSMSGINTEL32) $(SGSMSGINTEL64)

SGSMSGFLAGS1=	$(SGSMSGFLAGS) -m $(BLTMESG)
SGSMSGFLAGS2=	$(SGSMSGFLAGS) -h $(BLTDEFS) -d $(BLTDATA) -n rtld_msg

SRCS=		$(AVLOBJ:%.o=$(AVLDIR)/%.c) \
		$(DTROBJ:%.o=$(DTRDIR)/%.c) \
		$(TOOLOBJS:%.o=$(SGSTOOLS)/common/%.c) \
		$(COMOBJS:%.o=../common/%.c)  $(MACHOBJS:%.o=%.c) $(BLTDATA) \
		$(G_MACHOBJS:%.o=$(SRCBASE)/uts/$(PLAT)/krtld/%.c) \
		$(CP_MACHOBJS:%.o=../$(MACH)/%.c) \
		$(ASOBJS:%.o=%.s)
LINTSRCS=	$(SRCS) ../common/lintsup.c

LINTFLAGS +=	-u -Dsun -D_REENTRANT -erroff=E_EMPTY_TRANSLATION_UNIT \
		-erroff=E_NAME_DECL_NOT_USED_DEF2
LINTFLAGS64 +=	-u -D_REENTRANT -erroff=E_CAST_INT_TO_SMALL_INT \
		-erroff=E_NAME_DECL_NOT_USED_DEF2

CLEANFILES +=	$(LINTOUTS)  $(CRTS)  $(BLTFILES)
CLOBBERFILES +=	$(RTLD)
