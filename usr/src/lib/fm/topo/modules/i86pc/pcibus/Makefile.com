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

MODULE = pcibus
ARCH = i86pc
CLASS = arch

UTILDIR = $(SRC)/lib/fm/topo/modules/common/pcibus
HBDIR = $(SRC)/lib/fm/topo/modules/common/hostbridge

UTILSRCS = did.c did_hash.c did_props.c util.c
PCISRCS = pcibus.c pcibus_labels.c pcibus_hba.c

MODULESRCS = $(UTILSRCS) $(PCISRCS) pci_i86pc.c

include $(SRC)/lib/fm/topo/modules/Makefile.com

LDLIBS += -ldevinfo -lsmbios

CPPFLAGS += -I$(UTILDIR) -I$(HBDIR)

%.o: $(UTILDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(CTFCONVERT_O)
				
%.o: $(HBDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(CTFCONVERT_O)

%.ln: $(UTILDIR)/%.c
	$(LINT.c) -c $<

%.ln: $(HBDIR)/%.c
	$(LINT.c) -c $<
