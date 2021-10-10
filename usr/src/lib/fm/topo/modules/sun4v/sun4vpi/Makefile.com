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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

MODULE	= sun4vpi
ARCH	= sun4v
CLASS	= arch

MODULESRCS =	pi_defer.c pi_ldom.c pi_walker.c pi_subr.c \
		pi_cpu.c pi_mem.c pi_generic.c pi_pciexrc.c \
		pi_hostbridge.c pi_niu.c pi_top.c sun4vpi.c \
		pi_meth.c pi_bay.c

TOPODIR	= $(SRC)/lib/fm/topo/libtopo/common
BAYDIR = $(SRC)/lib/fm/topo/modules/common/bay/common

include $(SRC)/lib/fm/topo/modules/Makefile.com

LDLIBS += -ldevinfo -ldevid -lmdesc -lldom -luutil -lfmd_agent

CPPFLAGS += -I../common -I$(ROOT)/usr/platform/sun4v/include -I$(TOPODIR) -I$(BAYDIR)

%.o: $(SRC)/common/mdesc/%.c
	$(COMPILE.c) $<
	$(CTFCONVERT_O)
