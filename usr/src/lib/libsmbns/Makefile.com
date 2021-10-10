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
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
# 
LIBRARY= libsmbns.a
VERS= .1

OBJS_SHARED =			\
	smb_netbios_util.o

OBJS_COMMON=			\
	smbns_ads.o		\
	smbns_browser.o		\
	smbns_dyndns.o		\
	smbns_hash.o		\
	smbns_kpasswd.o		\
	smbns_krb.o		\
	smbns_netbios.o		\
	smbns_netbios_cache.o	\
	smbns_netbios_datagram.o\
	smbns_netbios_name.o	\
	smbns_netlogon.o

OBJECTS=	$(OBJS_COMMON) $(OBJS_SHARED)

include ../../Makefile.lib

# Install the library header files under /usr/include/smbsrv.
ROOTSMBHDRDIR=	$(ROOTHDRDIR)/smbsrv
ROOTSMBHDRS=	$(HDRS:%=$(ROOTSMBHDRDIR)/%)

STUBROOTLIBDIR =	$(STUBROOT)/usr/lib
STUBROOTLIBDIR64 =	$(STUBROOT)/usr/lib/$(MACH64)

LROOTLIBDIR =		$(LROOT)/usr/lib
LROOTLIBDIR64 =		$(LROOT)/usr/lib/$(MACH64)

SRCDIR=		../common
LIBS=		$(DYNLIB) $(LINTLIB)
C99MODE =       -xc99=%all
C99LMODE =      -Xc99=%all
CPPFLAGS +=	-I$(SRCDIR) -I.
$(LINTLIB) := SRCS = $(SRCDIR)/$(LINTSRC)

CLEANFILES += $(GENSRCS)

include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

INCS += -I$(SRC)/lib/gss_mechs/mech_krb5/include \
	-I$(SRC)/lib/gss_mechs/mech_krb5/krb5/keytab

SRCS=   $(OBJS_COMMON:%.o=$(SRCDIR)/%.c)	\
	$(OBJS_SHARED:%.o=$(SRC)/common/smbsrv/%.c)

# for smbns_dyndns.c
OBJS_DYNDNS =  pics/smbns_dyndns.o
$(OBJS_DYNDNS) lintcheck :=     INCS +=     -I$(SRC)/lib/libresolv2/include

LDLIBS +=	$(MACH_LDLIBS)
LDLIBS +=	-lsmb -lgss -lcmdutils -lldap -lresolv -lnsl -lsocket
LDLIBS +=	-lc -lcryptoutil
CPPFLAGS +=	$(INCS) -D_REENTRANT

# DYNLIB libraries do not have lint libs and are not linted
$(DYNLIB) :=	LDLIBS += -lmech_krb5

pics/%.o:	$(SRC)/common/smbsrv/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

.KEEP_STATE:

all: stub $(LIBS)

lint: lintcheck

include ../../Makefile.targ
