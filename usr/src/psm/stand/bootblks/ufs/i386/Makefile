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
# psm/stand/bootblks/ufs/i386/Makefile
#
.KEEP_STATE:

BASEDIR = ../..

include $(BASEDIR)/ufs/Makefile.ufs


$(NOTPARFAIT_BUILD)CC	= $(GNU_ROOT)/bin/gcc
$(PARFAIT_BUILD)CC	= $(PARFAIT_HOME)/bin/parfait-gcc

ASFLAGS		= -B$(GNU_ROOT)/bin/ -fno-builtin -nostdinc
CPPFLAGS	=

$(NOTPARFAIT_BUILD)LD	= $(GNU_ROOT)/bin/gld
$(PARFAIT_BUILD)LD	= $(PARFAIT_HOME)/bin/parfait-gld
LDFLAGS		= -nostdlib -N -Ttext 600

OBJCOPY		= $(GNU_ROOT)/bin/gobjcopy

INSTALL_DIR	= $(USR)/lib/fs/ufs
INSTALL_TARGETS	= $(PROGS:%=$(INSTALL_DIR)/%)

$(INSTALL_TARGETS) := FILEMODE = 0444

PROGS		= mboot


all: $(INSTALLBOOT) $(PROGS)

$(PROGS): $$(@).exec
	$(OBJCOPY) -O binary $@.exec $@

%.exec: %.o
	$(LD) $(LDFLAGS) -o $@ $(@:exec=o)


install: all $(INSTALL_TARGETS) $(USR_SBIN_INSTALLBOOT)

$(INSTALL_DIR)/%: $(INSTALL_DIR) %
	$(INS.file)

$(INSTALL_DIR):
	$(INS.dir)

lint:

clean:
	$(RM) *.exec *.o

clobber: clean
	$(RM) $(PROGS) $(INSTALLBOOT)


#
# Pattern matching rules for source in this directory
#
%: %.sh
	$(RM) $@
	cat $< > $@
	chmod +x $@
