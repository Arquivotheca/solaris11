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
# Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
#

.KEEP_STATE:
.KEEP_STATE_FILE: .make.state.$(MACH)


include		$(SRC)/cmd/sgs/Makefile.var

SRCBASE =	../../../..

ARCH_i386 =	intel
ARCH_sparc =	sparc
ARCH =		$(ARCH_$(MACH))

PLAT_sparc =	sparc
PLAT_i386 =	intel/ia32
PLAT_amd64 =	intel/amd64

# Establish any global flags.

# Setting DEBUG = -DDEBUG (or "make DEBUG=-DDEBUG ...") enables ASSERT()
# checking.  This is automatically enabled for DEBUG builds, not for non-debug
# builds.  Unset the global C99_DISABLE flag to insure we uncover all compiler
# warnings/errors.
DEBUG =
$(NOT_RELEASE_BUILD)DEBUG = -DDEBUG

C99_DISABLE =	$(C99_ENABLE)

CFLAGS +=	$(CCVERBOSE) $(DEBUG) $(XFFLAG)
CFLAGS64 +=	$(CCVERBOSE) $(DEBUG) $(XFFLAG)

#
# Location of shared code
#
ELFCAP =	$(SRC)/common/elfcap
AVLDIR =	$(SRC)/common/avl

# Reassign CPPFLAGS so that local search paths are used before any parent
# $ROOT paths.
CPPFLAGS =	-I. -I../common -I../../include -I../../include/$(MACH) \
		$(CPPFLAGS.master) -I$(ELFCAP)

# PICS64 is unique to our environment
$(PICS64) :=	sparc_CFLAGS += -xregs=no%appl -K pic
$(PICS64) :=	sparcv9_CFLAGS += -xregs=no%appl -K pic
$(PICS64) :=	CPPFLAGS += -DPIC -D_REENTRANT

# gcrt1.o, which is included in an application built with -xpg, inspects
# ld.so.1 for the string "Editor" to verify compatibility.  This string exists
# within a .note entry (see libconv:vernote.s).  This .note section within
# ld.so.1 can't be dropped until the following bug is resolved.
#
#    7038157 remove unnecessary ld.so.1 version check from gcrt1.o
#
# In the mean time, we might as well keep .note sections in the rest of the
# link-editor deliverables too.
LDFLAGS +=	$(ZIGNORE) $(ZSTRIPCLASS_N_NOTE)
DYNFLAGS +=	$(ZIGNORE) $(ZSTRIPCLASS_N_NOTE)

# Establish the local tools, proto and package area.

SGSHOME =	$(SRC)/cmd/sgs
SGSPROTO =	$(SGSHOME)/proto/$(MACH)
SGSTOOLS =	$(SGSHOME)/tools
SGSMSGID =	$(SGSHOME)/messages
SGSMSGDIR =	$(SGSHOME)/messages/$(MACH)
SGSONLD =	$(ROOT)/opt/SUNWonld

#
# Macros to be used to include link against libconv and include vernote.o
#
# Normally, shared object dependencies will be found in the workspace
# STUBROOT or ROOT proto areas. However, we switch to the libconv directories
# when building the native tools in the local proto
#
VERSREF =	-ulink_ver_string

CONVLIBDIR =	-L$(SGSHOME)/libconv/$(MACH)
CONVLIBDIR64 =	-L$(SGSHOME)/libconv/$(MACH64)


# The cmd/Makefile.com and lib/Makefile.com define TEXT_DOMAIN.  We don't need
# this definition as the sgs utilities obtain their domain via sgsmsg(1l).

DTEXTDOM =

# Define any generic sgsmsg(1l) flags.  The default message generation system
# is to use gettext(3i), add the -C flag to switch to catgets(3c).

SGSMSG =		$(SGSTOOLS)/$(MACH)/sgsmsg
SGSMSG_PIGLATIN_NL =	perl $(SGSTOOLS)/common/sgsmsg_piglatin_nl.pl
CHKMSG =		$(SGSTOOLS)/chkmsg.sh

SGSMSGVFLAG =
SGSMSGFLAGS =	$(SGSMSGVFLAG) -i $(SGSMSGID)/sgs.ident
CHKMSGFLAGS =	$(SGSMSGTARG:%=-m %) $(SGSMSGCHK:%=-m %)

# Native targets should use the minimum of ld(1) flags to allow building on
# previous releases.  We use mapfiles to scope, but don't bother versioning.

native :=	DYNFLAGS = -R$(SGSPROTO) -L$(SGSPROTO) $(ZNOVERSION)

# Comment out the following two lines to have the sgs built from the system
# link-editor, rather than the local proto link-editor.
CC_USE_PROTO =	-Yl,$(SGSPROTO)
LD_USE_PROTO =	$(SGSPROTO)/

#
# lint-related stuff
#
LIBNAME32 =	$(LIBNAME:%=%32)
LIBNAME64 =	$(LIBNAME:%=%64)
LIBNAMES =	$(LIBNAME32) $(LIBNAME64)

SGSLINTOUT =	lint.out
LINTOUT1 =	lint.out.1
LINTOUT32 =	lint.out.32
LINTOUT64 =	lint.out.64
LINTOUTS =	$(SGSLINTOUT) $(LINTOUT1) $(LINTOUT32) $(LINTOUT64)

LINTLIBSRC =	$(LINTLIB:%.ln=%)
LINTLIB32 =	$(LINTLIB:%.ln=%32.ln)
LINTLIB64 =	$(LINTLIB:%.ln=%64.ln)
LINTLIBS =	$(LINTLIB32) $(LINTLIB64)

LINTFLAGS =	-m -errtags=yes -erroff=E_SUPPRESSION_DIRECTIVE_UNUSED
LINTFLAGS64 =	-m -errtags=yes -erroff=E_SUPPRESSION_DIRECTIVE_UNUSED \
		    $(VAR_LINTFLAGS64)

#
# When building a lint library, no other lint libraries are verified as
# dependencies, nor is the stardard C lint library processed.  All dependency
# verification is carried out through linting the sources themselves.
#
$(LINTLIB) :=	LINTFLAGS += -n
$(LINTLIB) :=	LINTFLAGS64 += -n

$(LINTLIB32) :=	LINTFLAGS += -n
$(LINTLIB32) :=	LINTFLAGS64 += -n
$(LINTLIB64) :=	LINTFLAGS += -n
$(LINTLIB64) :=	LINTFLAGS64 += -n

#
# These libraries have two resulting lint libraries.  If a dependency is
# declared using these variables, the substitution for the 32/64 versions at
# lint time happens automatically (see Makefile.targ).
#
LD_LIB =	-lld
LD_LIB32 =	-lld32
LD_LIB64 =	-lld64

LDDBG_LIB =	-llddbg
LDDBG_LIB32 =	-llddbg32
LDDBG_LIB64 =	-llddbg64

CONV_LIB =	-lconv
CONV_LIB32 =	-lconv32
CONV_LIB64 =	-lconv64
