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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG = 		vbiosd

MANIFEST = 	vbiosd.xml
MANIFEST += 	console-reset.xml

SVCMETHOD = 	svc-vbiosd
SVCMETHOD += 	console-reset

include ../Makefile.cmd

ROOTMANIFESTDIR = $(ROOTSVCSYSTEM)

CPPFLAGS += 	-I. -I./x86emu/
LDLIBS += 	-lscf

LINTFLAGS += 	-erroff=E_CONSTANT_CONDITION
LINTFLAGS +=	-erroff=E_FUNC_HAS_NO_RETURN_STMT
LINTFLAGS +=	-erroff=E_NAME_USED_NOT_DEF2
LINTFLAGS +=	-erroff=E_NAME_DEF_NOT_USED2

VBIOSDOBJS = 	vbiosd_door.o vbiosd_vesa_cmd.o vbiosd_vga_cmd.o \
		vbiosd_error.o vbiosd.o
X86EMUOBJS = 	x86emu_decode.o x86emu_fpu.o x86emu_ops.o x86emu_ops2.o \
		x86emu_prim_ops.o x86emu_sys.o vbiosd_x86emu.o

LINTOBJS = 	$(VBIOSDOBJS)
OBJS = 		$(LINTOBJS) $(X86EMUOBJS)

SRCS = 		$(LINTOBJS:%.o=%.c)

C99MODE=        -xc99=%all
C99LMODE=       -Xc99=%all

.KEEP_STATE:

all: 		$(PROG)

$(PROG): 	$(OBJS)
		$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

clean:		
		$(RM) $(OBJS)

install: 	all $(ROOTUSRSBINPROG) $(ROOTMANIFEST) \
		$(ROOTSVCMETHOD)

lint:		lint_SRCS

check:		$(CHKMANIFEST)

FRC:
include ../Makefile.targ
