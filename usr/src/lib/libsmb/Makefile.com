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

LIBRARY= libsmb.a
VERS= .1

OBJS_SHARED = 			\
	smb_avl.o		\
	smb_idmap.o		\
	smb_inet.o		\
	smb_match.o 		\
	smb_msgbuf.o		\
	smb_oem.o		\
	smb_sid.o		\
	smb_string.o 		\
	smb_utf8.o		\
	smb_xdr.o

OBJS_COMMON = 			\
	smb_acl.o		\
	smb_auth.o 		\
	smb_cfg.o		\
	smb_crypt.o		\
	smb_domain.o		\
	smb_door_encdec.o	\
	smb_doorclnt.o		\
	smb_info.o		\
	smb_kmod.o		\
	smb_lgrp.o		\
	smb_mac.o		\
	smb_nic.o		\
	smb_pwdutil.o		\
	smb_privilege.o		\
	smb_reparse.o		\
	smb_sam.o		\
	smb_scfutil.o		\
	smb_sd.o		\
	smb_share.o		\
	smb_status_tbl.o	\
	smb_util.o		\
	smb_wksids.o

OBJECTS=	$(OBJS_COMMON) $(OBJS_SHARED)

include ../../Makefile.lib

# Install the library header files under /usr/include/smbsrv.
ROOTSMBHDRDIR=	$(ROOTHDRDIR)/smbsrv
ROOTSMBHDRS=	$(HDRS:%=$(ROOTSMBHDRDIR)/%)

SRCDIR=		../common
LIBS=		$(DYNLIB) $(LINTLIB)
C99MODE =       -xc99=%all
C99LMODE =      -Xc99=%all
CPPFLAGS +=	-I$(SRCDIR) -I.
$(LINTLIB) := SRCS = $(SRCDIR)/$(LINTSRC)

INCS += -I$(SRC)/common/smbsrv

LDLIBS +=	$(MACH_LDLIBS)
LDLIBS +=	-lscf -lmd -luuid -ldlpi -lnsl -lpkcs11 -lsec -lsocket -lresolv
LDLIBS +=	-lshare -lidmap -lreparse -lnvpair -lcmdutils -lavl -lc

CPPFLAGS +=	$(INCS) -D_REENTRANT

SRCS=   $(OBJS_COMMON:%.o=$(SRCDIR)/%.c)	\
	$(OBJS_SHARED:%.o=$(SRC)/common/smbsrv/%.c)

pics/%.o:	$(SRC)/common/smbsrv/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

.KEEP_STATE:

all: $(LIBS)

install_h: $(ROOTSMBHDRS)

lint: lintcheck

include ../../Makefile.targ

