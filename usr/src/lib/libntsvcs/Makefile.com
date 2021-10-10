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
LIBRARY =	libntsvcs.a
VERS =		.1

OBJS_COMMON =		\
	dfs.o		\
	dssetup_clnt.o	\
	dssetup_svc.o	\
	eventlog_svc.o	\
	eventlog_log.o	\
	lsalib.o	\
	lsar_clnt.o	\
	lsar_svc.o	\
	ntsvcs_clnt.o	\
	msgsvc_svc.o	\
	netdfs.o	\
	netr_clnt.o	\
	netr_svc.o	\
	samlib.o	\
	samr_clnt.o	\
	samr_svc.o	\
	smb_autohome.o	\
	srvsvc_clnt.o	\
	srvsvc_sd.o	\
	srvsvc_svc.o	\
	svcctl_scm.o	\
	svcctl_svc.o	\
	winreg_svc.o	\
	wkssvc_clnt.o	\
	wkssvc_svc.o

# Automatically generated from .ndl files
NDLLIST =		\
	dssetup		\
	eventlog	\
	lsarpc		\
	msgsvc		\
	netdfs		\
	netlogon	\
	samrpc		\
	srvsvc		\
	svcctl		\
	winreg

OBJECTS=	$(OBJS_COMMON) $(NDLLIST:%=%_ndr.o)

include ../../Makefile.lib

# Install the library header files under /usr/include/smbsrv.
ROOTSMBHDRDIR=	$(ROOTHDRDIR)/smbsrv
ROOTSMBHDRS=	$(HDRS:%=$(ROOTSMBHDRDIR)/%)

SRCDIR=		../common
NDLDIR=		$(ROOT)/usr/include/smbsrv/ndl
LIBS=		$(DYNLIB) $(LINTLIB)
C99MODE =       -xc99=%all
C99LMODE =      -Xc99=%all
CPPFLAGS +=	-I$(SRCDIR) -I.
$(LINTLIB) := SRCS = $(SRCDIR)/$(LINTSRC)

include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

CLEANFILES += $(OBJECTS:%_ndr.o=%_ndr.c)

INCS += -I$(SRC)/common/smbsrv

LDLIBS +=	$(MACH_LDLIBS)
LDLIBS += -lndr -lsmb -lshare -lsmbfs -lnsl -lpkcs11 -lscf -lsec -lcmdutils
LDLIBS += -lnvpair -luutil -luuid -lgen -lc

CPPFLAGS += $(INCS) -D_REENTRANT

SRCS=   $(OBJS_COMMON:%.o=$(SRCDIR)/%.c)

%_ndr.c: $(NDLDIR)/%.ndl
	$(NDRGEN) -Y $(CC) $<

pics/%.o:	$(SRC)/common/smbsrv/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

.KEEP_STATE:

all: $(LIBS)

lint: lintcheck

include ../../Makefile.targ
