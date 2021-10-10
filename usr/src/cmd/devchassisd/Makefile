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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#
#
# cmd/devchassisd/Makefile
#

PROG= 		devchassisd devchassis_cleanstart
OBJS=		devchassisd.o devchassisd_work.o
SRCS=		$(OBJS:.o=.c)

SVCMETHOD=	svc-devchassis
MANIFEST=	devchassis.xml
ETCDEVFILES=	chassis_aliases
ETCDEVDOTFILES=	.chassis_aliases

include ../Makefile.cmd

ROOTCMDDIR=		$(ROOTLIB)/devchassis
ROOTMANIFESTDIR=	$(ROOTSVCSYSTEM)
ROOTETCDEVFILES=	$(ETCDEVFILES:%=$(ROOTETC)/dev/%)
ROOTETCDEVDOTFILES=	$(ETCDEVDOTFILES:%=$(ROOTETC)/dev/%)

$(ROOTMANIFEST)	:=		FILEMODE=	444
$(ROOTETCDEVFILES) :=		FILEMODE=	0644
$(ROOTETCDEVDOTFILES) :=	FILEMODE=	0644

LDLIBS += -lumem -ldevinfo -lsysevent

.KEEP_STATE:

all:	$(PROG)

devchassisd: $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

.PARALLEL: $(OBJS)

install: all $(ROOTCMD) $(ROOTETCDEVFILES) $(ROOTETCDEVDOTFILES) \
	$(ROOTMANIFEST) $(ROOTSVCMETHOD)

clean:
	$(RM) $(PROG) $(OBJS) $(LLOBJS)

lint:	lint_SRCS

$(ROOTETC)/dev/% : %
	$(INS.file)

$(ROOTETC)/dev/.% : $(ROOTETC)/dev/% 
	$(RM) $@
	$(CP) $< $@

check: $(CHKMANIFEST)

include ../Makefile.targ
