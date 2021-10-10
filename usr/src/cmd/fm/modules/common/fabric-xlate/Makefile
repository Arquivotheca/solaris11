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
# Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
#

MODULE = fabric-xlate
CLASS = common
SRCS = fabric-xlate.c fx_epkt.c fx_fabric.c fx_fire.c fx_subr.c

include ../../Makefile.plugin

CPPFLAGS += -I/usr/include/libxml2 -I$(KMFDIR)/include -I.
CPPFLAGS += -I$(SRC)/uts/sun4v/io/px
INCDIRS = $(SRC)/uts/common
CFLAGS += -I$(INCDIRS)
LINTFLAGS += -I$(INCDIRS)

LDFLAGS += -L$(LROOT)/usr/lib/fm -R/usr/lib/fm
LXML2 = -lxml2
lint := LXML2 =
LDLIBS += -luutil $(LXML2) -L$(LROOTLIB)/fm -ltopo
