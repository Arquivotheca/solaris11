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
# This Makefile builds the sun4v-cpu Performance Counter BackEnd (PCBE).
#

UTSBASE = ../..

#
#	Define module and object file sets.
#
MODULE		= pcbe.sun4v-cpu
OBJECTS		= $(SUN4V_PCBE_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(SUN4V_PCBE_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_PCBE_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sun4v/Makefile.sun4v

#
#	Define targets.
#
ALL_TARGET	= $(BINARY)
LINT_MODULE	= sun4v_pcbe
LINT_TARGET	= $(LINT_MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE)

#
#	Default build targets.
#
.KEEP_STATE:

def:		$(DEF_DEPS)

all:		$(ALL_DEPS)

clean:		$(CLEAN_DEPS)

clobber:	$(CLOBBER_DEPS)

lint:		$(LINT_DEPS)

modlintlib:	$(MODLINTLIB_DEPS)

clean.lint:	$(CLEAN_LINT_DEPS)

install:	$(INSTALL_DEPS)

#
#	Include common targets.
#
include $(UTSBASE)/sun4v/Makefile.targ

#
# Custom rules for sun4v_ni2.pcbe.c and sun4v_vt_pcbe.c
# KT_IMPL or VT_IMPL is defined as appropriate in order
# to compile with the appropriate values from niagara2regs.h
#
$(OBJS_DIR)/sun4v_ni2_pcbe.o:	$(UTSBASE)/sun4v/pcbe/sun4v_ni2_pcbe.c
	$(COMPILE.c) -DKT_IMPL -o $(OBJS_DIR)/sun4v_ni2_pcbe.o	\
		$(UTSBASE)/sun4v/pcbe/sun4v_ni2_pcbe.c
	$(CTFCONVERT_O)

$(OBJS_DIR)/sun4v_vt_pcbe.o:	$(UTSBASE)/sun4v/pcbe/sun4v_vt_pcbe.c
	$(COMPILE.c) -DVT_IMPL -o $(OBJS_DIR)/sun4v_vt_pcbe.o	\
		$(UTSBASE)/sun4v/pcbe/sun4v_vt_pcbe.c
	$(CTFCONVERT_O)

$(LINTS_DIR)/sun4v_ni2_pcbe.ln:		$(UTSBASE)/sun4v/pcbe/sun4v_ni2_pcbe.c
	$(LINT.c) -DKT_IMPL $(UTSBASE)/sun4v/pcbe/sun4v_ni2_pcbe.c

$(LINTS_DIR)/sun4v_vt_pcbe.ln:		$(UTSBASE)/sun4v/pcbe/sun4v_vt_pcbe.c
	$(LINT.c) -DVT_IMPL $(UTSBASE)/sun4v/pcbe/sun4v_vt_pcbe.c
