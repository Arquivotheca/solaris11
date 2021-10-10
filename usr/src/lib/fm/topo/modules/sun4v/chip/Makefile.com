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

ARCH = sun4v
sun4v_SRCS = chip_sun4v.c cpu_mdesc.c pi_meth.c

PRIDIR = $(SRC)/lib/fm/topo/modules/sun4v/platform-cpu/common
PIDIR = $(SRC)/lib/fm/topo/modules/sun4v/sun4vpi/common

INCDIRS = $(ROOT)/usr/platform/sun4v/include \
		$(PRIDIR)

include $(SRC)/lib/fm/topo/modules/sun4/chip/Makefile.chip

LDLIBS += -lumem -lmdesc -lldom -lfmd_agent

%.o: $(PRIDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(CTFCONVERT_O)

%.ln: $(PRIDIR)/%.c
	$(LINT.c) -c $<

%.o: $(PIDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(CTFCONVERT_O)

%.ln: $(PIDIR)/%.c
	$(LINT.c) -c $<
