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
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG=		rpc.rexd on
# where under
#RPCGEN=		rpcgen
REXDPROG=	rpc.rexd
ONPROG	=	on

MANIFEST=	rex.xml

include ../Makefile.cmd

ROOTMANIFESTDIR=	$(ROOTSVCNETWORKRPC)

SRCS=		$(PROG:%=%.c)

#OBJS=		errprintf.o mount_nfs.o rex_xdr.o unix_login.o mntent.o
TYPEOBJS=	nfs_subr.o smfcfg.o
OBJS=		errprintf.o mount_nfs.o rex.o unix_login.o mntent.o $(TYPEOBJS)
REXDOBJS=	rpc.rexd.o errprintf.o mount_nfs.o rex.o unix_login.o mntent.o
ONOBJS= 	on.o sharetab.o where.o rex.o mntent.o
#WHEREOBJS=	sharetab.o where.o where_main.o mntent.o
CLEANOBJS=	exportent.o sharetab.o on.o rpc.rexd.o where.o
#under.o where.o where_main.o
#UNDEROBJS=	errprintf.o sharetab.o mount_nfs.o

ROOTREXD=	$(ROOTUSRSBIN)
ROOTON=		$(ROOTBIN)
ROOTREXDPROG=	$(REXDPROG:%=$(ROOTREXD)/%)
ROOTONPROG=	$(ONPROG:%=$(ROOTON)/%)

CPPFLAGS =	-I. $(CPPFLAGS.master)

LDLIBS +=	-lsocket -lnsl
rpc.rexd :=	LDLIBS += -lrpcsvc -lbsm -lpam

TYPESRCS=	../nfs/lib/nfs_subr.c ../nfs/lib/smfcfg.c

# install rule
# don't re-install directories created by Targetdirs
#$(ROOTREXD)/%: %
#	$(INS.file)
#$(ROOTON)/%: %
#	$(INS.file)

.KEEP_STATE:

all:	$(PROG)

rpc.rexd:	$$@.o $(OBJS)
	$(LINK.c) -o $@ $@.o $(OBJS) $(LDLIBS) -lscf
	$(POST_PROCESS)

on:	$(ONOBJS)
	$(LINK.c) -o $@ $(ONOBJS) $(LDLIBS)
	$(POST_PROCESS)

#under: $$@.o $(UNDEROBJS)
#	$(LINK.c) -o $@ $@.o $(UNDEROBJS) $(LDLIBS)
#
#where: $(WHEREOBJS)
#	$(LINK.c) -o $@ $(WHEREOBJS) $(LDLIBS)
#
install: all $(ROOTREXDPROG) $(ROOTONPROG) $(ROOTMANIFEST)

$(ROOTREXD):	rpc.rexd
	$(INS.dir)
$(ROOTON):	on
	$(INS.dir)

check:	$(CHKMANIFEST)

clean:
	$(RM) $(OBJS) $(CLEANOBJS) $(PROG)

lint:	 lint_SRCS

include ../Makefile.targ

# derived source files and explicit dependencies

#rex_xdr.o \
#rex_svc.o \
#rex_clnt.o:	rex.h rex.c
#	$(LINK.c) -c -o $@ rex.c

on.o:		rex.h

rpc.rexd.o:		rex.h

unix_login.o:	rex.h

rex.o:		rex.h

nfs_subr.o:	../fs.d/nfs/lib/nfs_subr.c
	$(COMPILE.c)  ../fs.d/nfs/lib/nfs_subr.c

smfcfg.o:	../fs.d/nfs/lib/smfcfg.c
	$(COMPILE.c)  ../fs.d/nfs/lib/smfcfg.c
