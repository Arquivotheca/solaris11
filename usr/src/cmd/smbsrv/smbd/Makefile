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


PROG=	smbd
SRCS=				\
	smbd_auth.c		\
	smbd_doorsvc.c		\
	smbd_dyndns.c		\
	smbd_join.c		\
	smbd_krb5.c		\
	smbd_main.c		\
	smbd_nicmon.c		\
	smbd_ntlm.c		\
	smbd_opipe_doorsvc.c	\
	smbd_pac.c		\
	smbd_quota.c		\
	smbd_share.c		\
	smbd_spool.c		\
	smbd_token.c		\
	smbd_vss.c		\
	spoolss_svc.c

NDLDIR = $(ROOT)/usr/include/smbsrv/ndl
NDLLIST = spoolss
NDLSRCS = $(NDLLIST:%=%_ndr.c)
NDLOBJS = $(NDLLIST:%=%_ndr.o)

include ../../Makefile.cmd
include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

MANIFEST=	server.xml
SVCMETHOD=	svc-smbd
LOGDLL=		eventlog.dll

ROOTMANIFESTDIR	= $(ROOTSVCSMB)
ROOTVARSMBDLLDIR = $(ROOTVARSMB)/cvol/windows/system32
ROOTVARSMBDLL= $(LOGDLL:%=$(ROOTVARSMBDLLDIR)/%)

$(ROOTMANIFEST):= FILEMODE = 0444
$(ROOTSVCMETHOD):= FILEMODE = 0555
$(ROOTVARSMBDLL):= FILEMODE = 0755

include ../Makefile.smbsrv.defs
INCS += -I$(SRC)/uts/common/gssapi/mechs/krb5/include \
	-I$(SRC)/lib/gss_mechs/mech_krb5/include

LDLIBS += -lntsvcs -lndr -lsmbns -lsmb -lsmbfs \
	-lnvpair -lavl -lsec -lzfs -lbsm -lsocket -lnsl -lscf \
	-lresolv -lcmdutils -lgss -lumem -lshare -L$(LROOT_KLIBDIR)
$(PROG) := LDLIBS += -lmech_krb5
LDFLAGS += -R$(KLIBDIR)

ROOTSMBDFILE = $(PROG:%=$(ROOTLIB)/%)

FILEMODE = 0444
$(ROOTSMBDFILE):= FILEMODE = 0555

$(ROOTLIB)/%: %
	$(INS.file)

$(ROOTVARSMBDLLDIR)/%: %
	$(INS.file)

%_ndr.c: $(NDLDIR)/%.ndl
	$(NDRGEN) -Y $(CC) $<

all:		$(PROG)

clean:
	$(RM) $(OBJS) $(NDLOBJS) $(NDLSRCS)

lint:		lint_SRCS

$(OBJS):	smbd.h

$(PROG):	$(OBJS) $(NDLOBJS) $(NDLSRCS)
	$(LINK.c) -o $@ $(OBJS) $(NDLOBJS) $(LDLIBS)
	$(POST_PROCESS)

check:		$(CHKMANIFEST)

_msg:

include ../../Makefile.targ

install: all .WAIT $(ROOTETCDEFAULTFILES) $(ROOTMANIFEST) \
		$(ROOTSMBDFILE) ${ROOTSVCMETHOD} $(ROOTVARSMBDLL)
