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
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY=	libipmi.a
VERS=		.1

OBJECTS=	ipmi_bmc.o	\
		ipmi_entity.o	\
		ipmi_event.o	\
		ipmi_fru.o	\
		ipmi_hash.o	\
		ipmi_lan.o	\
		ipmi_lancfg.o	\
		ipmi_list.o	\
		ipmi_misc.o	\
		ipmi_sdr.o	\
		ipmi_sel.o	\
		ipmi_sensor.o	\
		ipmi_sunoem.o	\
		ipmi_tables.o	\
		ipmi_user.o	\
		ipmi_util.o	\
		libipmi.o

SRCS=		$(OBJECTS:%.o:$(SRCDIR)/%c.)

include ../../Makefile.lib

LIBS=		$(DYNLIB) $(LINTLIB)

SRCDIR=		../common

CLEANFILES +=	$(SRCDIR)/ipmi_tables.c	
INCS +=		-I$(SRCDIR)
LDLIBS +=	-lc -lm -lnvpair -lsocket -lnsl
CPPFLAGS +=	$(INCS)
C99MODE = $(C99_ENABLE)

$(LINTLIB) := SRCS=	$(SRCDIR)/$(LINTSRC)

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

$(SRCDIR)/ipmi_tables.c: $(SRCDIR)/mktables.sh $(SRCDIR)/libipmi.h
	sh $(SRCDIR)/mktables.sh $(SRCDIR)/libipmi.h > $@

include ../../Makefile.targ
