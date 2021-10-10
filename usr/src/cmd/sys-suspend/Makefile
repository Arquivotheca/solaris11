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
# Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
#
# cmd/power/Makefile
#

PROG =		sys-suspend
SUSPEND_SRCS = 	sys-suspend.c pm_pam_conv.c
SUSPEND_OBJS =	$(SUSPEND_SRCS:%.c=%.o)
SRCS = 		$(SUSPEND_SRCS)
OBJS = 		$(SRCS:%.c=%.o)


include ../Makefile.cmd

TEXT_DOMAIN=	SUNW_OST_OSCMD

XGETFLAGS +=    -a -x sys-suspend.xcl
POFILE=		power_all.po
POFILES=	$(SUSPEND_SRCS:%.c=%.po)

SUSPEND_LDLIBS = $(LDLIBS.cmd) -lbsm -lpam -lpower
sparc_SUSPEND_LDLIBS = $(SUSPEND_LDLIBS)
i386_SUSPEND_LDLIBS = $(SUSPEND_LDLIBS) -lzfs -linstzones

ROOTUSRBINSUSPEND= $(PROG:%=$(ROOTBIN)/%)
$(ROOTUSRBINSUSPEND) := FILEMODE= 4555

#
# lint pass one enforcement
#

#
# need -I $(STUBROOT)usr/include libpower.h which is not shipped
#
CFLAGS += $(CCVERBOSE) -I$(STUBROOT)/usr/include

.PARALLEL: $(OBJS)

.KEEP_STATE:

all: $(PROG)

install clean:


$(PROG): $(SUSPEND_OBJS)
	$(LINK.c) -o $@ $(SUSPEND_OBJS) $(SUSPEND_LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTUSRBINSUSPEND)


$(OPOFILES):
	@

$(POFILE):	$(POFILES) $(OPOFILES)
	$(RM)	$@
	cat	$(POFILES) $(OPOFILES)	> $@

clean:
	$(RM) $(OBJS)
	$(RM) $(POFILE) $(POFILES)

LINTFLAGS += -erroff=E_GLOBAL_COULD_BE_STATIC2 -I$(STUBROOT)/usr/include
LINTFLAGS += -auxn
lint:
	$(LINT.c) $(SUSPEND_SRCS) $(SUSPEND_LDLIBS)

include ../Makefile.targ
