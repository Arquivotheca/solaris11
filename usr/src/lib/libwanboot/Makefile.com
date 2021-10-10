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
# Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY =	libwanboot.a
VERS =		.1

# List of locally located modules.
LOC_DIR =	../common
LOC_OBJS =	socket_inet.o bootinfo_aux.o
LOC_SRCS =	$(LOC_OBJS:%.o=$(LOC_DIR)/%.c)

# List of common wanboot objects.
COM_DIR =	../../../common/net/wanboot
COM_OBJS =	auxutil.o \
		boot_http.o \
		bootconf.o \
		bootconf_errmsg.o \
		bootinfo.o \
		bootlog.o \
		http_errorstr.o \
		p12access.o \
		p12auxpars.o \
		p12auxutl.o \
		p12err.o \
		p12misc.o \
		parseURL.o
COM_SRCS =	$(COM_OBJS:%.o=$(COM_DIR)/%.c)

# List of common DHCP modules.
DHCP_DIR =	$(SRC)/common/net/dhcp
DHCP_OBJS =	dhcpinfo.o
DHCP_SRCS =	$(DHCP_OBJS:%.o=$(DHCP_DIR)/%.c)

OBJECTS =	$(LOC_OBJS) $(COM_OBJS) $(DHCP_OBJS)

include ../../Makefile.lib

LIBS +=		$(LINTLIB)
EXTRALIBS =	-lcrypto -lssl
lint := EXTRALIBS =
LDLIBS +=	-lnvpair -lresolv -lnsl -lsocket -ldevinfo -ldhcputil \
    		-linetutil -lc $(EXTRALIBS)
CPPFLAGS =	-I$(SRC)/common/net/wanboot/crypt $(CPPFLAGS.master)

# Must override SRCS from Makefile.lib since sources have
# multiple source directories.
SRCS =		$(LOC_SRCS) $(COM_SRCS) $(DHCP_SRCS)

# Must define location of lint library source.
SRCDIR =	$(LOC_DIR)
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

# OpenSSL requires us to turn this off
LINTFLAGS +=    -erroff=E_BAD_PTR_CAST_ALIGN

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-I$(LOC_DIR) -I$(COM_DIR) -I$(DHCP_DIR)

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

pics/%.o: $(COM_DIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(DHCP_DIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

include ../../Makefile.targ
