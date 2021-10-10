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
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#

# Needed for ROOTFS_LIBDIR definition
include		../../../../lib/Makefile.lib

PROG=		ipmgmtd
OBJS=		ipmgmt_main.o ipmgmt_door.o ipmgmt_persist.o ipmgmt_util.o \
		ipmgmt_watcher.o
SRCS=		$(OBJS:.o=.c)
SVCMETHOD=	net-ipmgmt
MANIFEST=	network-ipmgmt.xml
CFGFILES=	ipadm.conf

# Needed for ROOTETC definition
include		../../../Makefile.cmd

DMOD =		ipmgmtd.so
ROOTDMOD =	$(ROOT)/usr/lib/mdb/proc/$(DMOD)
DMOD_SRCS =	ipmgmt_mdb.c
DMOD_OBJS =	$(DMOD_SRCS:%.c=%.o)
DMOD_LINT =	$(DMOD_SRCS:%.c=%.ln)
DMOD_LDLIBS=	-L$(LROOT)/lib -lc -lnsl
MAPFILE-DMOD =	$(SRC)/cmd/mdb/common/modules/conf/mapfile-extern

ROOTCFGDIR=		$(ROOTETC)/ipadm
ROOTCFGFILES=		$(CFGFILES:%=$(ROOTCFGDIR)/%)
ROOTMANIFESTDIR=	$(ROOTSVCNETWORK)

$(ROOTCFGFILES)	:= OWNER= ipadm
$(ROOTCFGFILES)	:= GROUP= sys
$(ROOTCFGFILES)	:= FILEMODE= 644

ROOTCMDDIR=	$(ROOTFS_LIBDIR)/inet

UTSBASE =	$(SRC)/uts
CPPFLAGS += -I$(UTSBASE)/common/brand/solaris10

LDLIBS += -lipadm -lnvpair -lnsl -lumem -lscf -lsocket -linetutil

#
# Instrument ipmgmtd with CTF data to ease debugging.
#
CTFCONVERT_HOOK = && $(CTFCONVERT_O)
CTFMERGE_HOOK = && $(CTFMERGE) -L VERSION -o $@ $(OBJS)
$(OBJS) := CFLAGS += $(CTF_FLAGS)

$(DMOD) := CFLAGS += $(CC_PICFLAGS) -G $(XREGSFLAG) -g
$(DMOD) := LDFLAGS += $(ZTEXT) $(ZDEFS) $(MAPFILE-DMOD:%=-M%)

.KEEP_STATE:

.PARALLEL:

all: $(PROG) $(DMOD)

$(PROG): $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS) $(CTFMERGE_HOOK)
	$(POST_PROCESS)

install: $(ROOTCMD) $(ROOTMANIFEST) $(ROOTSVCMETHOD) $(ROOTCFGDIR) \
	$(ROOTCFGFILES) $(ROOTDMOD)

check:  $(SRCS) $(HEADERS) $(CHKMANIFEST)
	$(CSTYLE) -cpP $(SRCS) $(HEADERS)

$(ROOTCMD): $(PROG)

$(DMOD): $(DMOD_OBJS)
	$(LINK.c) $(DMOD_OBJS) -o $@ $(DMOD_LDLIBS)
	$(POST_PROCESS_SO)

clean:
	$(RM) $(OBJS) $(DMOD_OBJS) $(DMOD_LINT)

lint_dmod: $(DMOD_LINT)
	$(LINT) $(LINTFLAGS) $(DMOD_LINT) $(LDLIBS)

lint:	lint_SRCS

$(ROOTCFGDIR):
	$(INS.dir)

$(ROOTCFGDIR)/%: $(ROOTCFGDIR) %
	$(INS.file)

$(ROOTDMOD): $(ROOT)/usr/lib/mdb/proc $(DMOD)
	$(RM) $@; $(INS) -s -m 0555 -f $(@D) $(DMOD)

include 	../../../Makefile.targ
