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

LIBRARY =	libld.a
VERS =		.4

COMOBJS =	debug.o		globals.o	util.o

COMOBJS32 =	args32.o	asdeflib32.o	entry32.o	exit32.o \
		groups32.o	ldentry32.o	ldlibs32.o	ldmachdep32.o \
		ldmain32.o	libs32.o	files32.o	map32.o \
		map_core32.o	map_support32.o	map_v232.o	order32.o \
		outfile32.o	place32.o	relocate32.o	resolve32.o \
		sections32.o	sunwmove32.o	support32.o	syms32.o \
		update32.o	unwind32.o	version32.o	wrap32.o

COMOBJS64 =	args64.o	asdeflib64.o	entry64.o	exit64.o \
		groups64.o	ldentry64.o	ldlibs64.o	ldmachdep64.o \
		ldmain64.o	libs64.o	files64.o	map64.o \
		map_core64.o	map_support64.o	map_v264.o	order64.o \
		outfile64.o	place64.o	relocate64.o	resolve64.o \
		sections64.o	sunwmove64.o	support64.o	syms64.o \
		update64.o	unwind64.o	version64.o	wrap64.o

TOOLOBJS =	alist.o		findprime.o	string_table.o	strhash.o
AVLOBJ =	avl.o

# Relocation engine objects. These are kept separate from the L_XXX_MACHOBJS
# lists below in order to facilitate linting them.
G_MACHOBJS32 =	doreloc_sparc_32.o doreloc_x86_32.o
G_MACHOBJS64 =	doreloc_sparc_64.o doreloc_x86_64.o

# Target specific objects (sparc/sparcv9)
L_SPARC_MACHOBJS32 =	machrel.sparc32.o	machsym.sparc32.o
L_SPARC_MACHOBJS64 =	machrel.sparc64.o	machsym.sparc64.o

# Target specific objects (i386/amd64)
E_X86_TOOLOBJS =	leb128.o
L_X86_MACHOBJS32 =	machrel.intel32.o
L_X86_MACHOBJS64 =	machrel.amd64.o


# All target specific objects rolled together
E_TOOLOBJS =	$(E_SPARC_TOOLOBJS) \
	$(E_X86_TOOLOBJS)
L_MACHOBJS32 =	$(L_SPARC_MACHOBJS32) \
	$(L_X86_MACHOBJS32)
L_MACHOBJS64 =	$(L_SPARC_MACHOBJS64) \
	$(L_X86_MACHOBJS64)


BLTOBJ =	msg.o
ELFCAPOBJ =	elfcap.o

OBJECTS =	$(BLTOBJ) $(G_MACHOBJS32) $(G_MACHOBJS64) \
		$(L_MACHOBJS32) $(L_MACHOBJS64) \
		$(COMOBJS) $(COMOBJS32) $(COMOBJS64) \
		$(TOOLOBJS) $(E_TOOLOBJS) $(AVLOBJ) $(ELFCAPOBJ)

include 	$(SRC)/lib/Makefile.lib
include 	$(SRC)/cmd/sgs/Makefile.com

SRCDIR =	../common


# Location of the shared relocation engines maintained under usr/src/uts.
#
KRTLD_I386 =	$(SRCBASE)/uts/$(PLAT_i386)/krtld
KRTLD_AMD64 =	$(SRCBASE)/uts/$(PLAT_amd64)/krtld
KRTLD_SPARC =	$(SRCBASE)/uts/$(PLAT_sparc)/krtld


CPPFLAGS +=	-DUSE_LIBLD_MALLOC -I$(SRCBASE)/lib/libc/inc \
		    -I$(SRCBASE)/uts/common/krtld -I$(SRCBASE)/uts/sparc
LDLIBS +=	$(CONV_LIB) $(LDDBG_LIB) -lelf -lc

LINTFLAGS +=	-u -D_REENTRANT
LINTFLAGS64 +=	-u -D_REENTRANT

DYNFLAGS +=	$(VERSREF) '-R$$ORIGIN'
$(DYNLIB) :=		DYNFLAGS += $(CC_USE_PROTO)
stubs/$(DYNLIB) :=	LD=$(LD_USE_PROTO)ld


native:=	DYNFLAGS	+= $(CONVLIBDIR)

BLTDEFS =	msg.h
BLTDATA =	msg.c
BLTMESG =	$(SGSMSGDIR)/libld

BLTFILES =	$(BLTDEFS) $(BLTDATA) $(BLTMESG)

# Due to cross linking support, every copy of libld contains every message.
# However, we keep target specific messages in their own separate files for
# organizational reasons.
#
SGSMSGCOM =	../common/libld.msg
SGSMSGSPARC =	../common/libld.sparc.msg
SGSMSGINTEL =	../common/libld.intel.msg
SGSMSGTARG =	$(SGSMSGCOM) $(SGSMSGSPARC) $(SGSMSGINTEL)
SGSMSGALL =	$(SGSMSGCOM) $(SGSMSGSPARC) $(SGSMSGINTEL)

SGSMSGFLAGS1 =	$(SGSMSGFLAGS) -m $(BLTMESG)
SGSMSGFLAGS2 =	$(SGSMSGFLAGS) -h $(BLTDEFS) -d $(BLTDATA) -n libld_msg

CHKSRCS =	$(SRCBASE)/uts/common/krtld/reloc.h \
		$(COMOBJS32:%32.o=../common/%.c) \
		$(L_MACHOBJS32:%32.o=../common/%.c) \
		$(L_MACHOBJS64:%64.o=../common/%.c) \
		$(KRTLD_I386)/doreloc.c \
		$(KRTLD_AMD64)/doreloc.c \
		$(KRTLD_SPARC)/doreloc.c

SRCS =		../common/llib-lld
LIBSRCS =	$(TOOLOBJS:%.o=$(SGSTOOLS)/common/%.c) \
		$(E_TOOLOBJS:%.o=$(SGSTOOLS)/common/%.c) \
		$(COMOBJS:%.o=../common/%.c) \
		$(AVLOBJS:%.o=$(AVLDIR)/%.c) \
		$(BLTDATA)

LINTSRCS =	$(LIBSRCS) ../common/lintsup.c
LINTSRCS32 =	$(COMOBJS32:%32.o=../common/%.c) \
		$(L_MACHOBJS32:%32.o=../common/%.c)
LINTSRCS64 =	$(COMOBJS64:%64.o=../common/%.c) \
		$(L_MACHOBJS64:%64.o=../common/%.c)

# Add the shared relocation engine source files to the lint
# sources and add the necessary command line options to lint them
# correctly. Make can't derive the files since the source and object
# names are not directly related
$(LINTOUT32) :=	CPPFLAGS += -DDO_RELOC_LIBLD
$(LINTOUT64) :=	CPPFLAGS += -DDO_RELOC_LIBLD -D_ELF64
$(LINTLIB32) :=	CPPFLAGS += -DDO_RELOC_LIBLD
$(LINTLIB64) :=	CPPFLAGS += -DDO_RELOC_LIBLD -D_ELF64
LINTSRCS32 +=	$(KRTLD_I386)/doreloc.c	\
		$(KRTLD_SPARC)/doreloc.c
LINTSRCS64 +=	$(KRTLD_AMD64)/doreloc.c \
		$(KRTLD_SPARC)/doreloc.c

# The lint libraries are not delivered with the product,
# so we install them in the stub proto.
STUBROOTFS_LINTLIBS =	$(STUBROOTFS_LIBDIR)/$(LINTLIB32) \
			$(STUBROOTFS_LIBDIR)/$(LINTLIB64)
STUBROOTFS_LINTLIBS64 =	$(STUBROOTFS_LIBDIR64)/$(LINTLIB32) \
			$(STUBROOTFS_LIBDIR64)/$(LINTLIB64)

CLEANFILES +=	$(LINTOUTS) $(BLTFILES)
CLOBBERFILES +=	$(DYNLIB) $(LINTLIBS) $(LIBLINKS)

ROOTFS_DYNLIB =	$(DYNLIB:%=$(ROOTFS_LIBDIR)/%)
