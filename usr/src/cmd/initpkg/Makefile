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
# Copyright (c) 1990, 2011, Oracle and/or its affiliates. All rights reserved.
#

SHFILES=	dfstab vfstab
CPFILES=	rcS rc0 rc1 rc2 rc3 mountall shutdown swapadd umountall
ALL=		$(SHFILES) $(CPFILES)
CLOBBERFILES=	$(ALL)

include ../Makefile.cmd

ETCDFSD=	$(ROOTETC)/dfs

USRSBINL=	rc5 rc6
USRSBINF=	rcS mountall rc0 rc1 rc2 rc3 shutdown swapadd umountall

ETCTABS=	vfstab inittab nscd.conf security/crypt.conf

DFSTAB=		dfstab

FILEMODE=	0744

ROOTUSRSBINL=	$(USRSBINL:%=$(ROOTUSRSBIN)/%)
ROOTUSRSBINF=	$(USRSBINF:%=$(ROOTUSRSBIN)/%)
ROOTETCTABS=	$(ETCTABS:%=$(ROOTETC)/%)
ROOTDFSTAB=	$(DFSTAB:%=$(ETCDFSD)/%)

$(ROOTETC)/inittab		:= FILEMODE =	0644
$(ROOTETC)/vfstab		:= FILEMODE =	0644
$(ROOTETC)/nscd.conf		:= FILEMODE =	0644
$(ROOTETC)/security/crypt.conf	:= FILEMODE =	0644
$(ROOTDFSTAB)			:= FILEMODE =	0644
$(ROOTUSRSBIN)/mountall		:= FILEMODE =	0555
$(ROOTUSRSBIN)/umountall	:= FILEMODE =	0555
$(ROOTUSRSBIN)/shutdown		:= FILEMODE =	0755

$(ETCDFSD)/% : %
	$(INS.file)

.KEEP_STATE:

all: $(ALL) all_init.d $(TXTS)

$(ROOTUSRSBINL):	$(ROOTUSRSBIN)/rc0
	$(RM) $@; $(LN) $(ROOTUSRSBIN)/rc0 $@

all_init.d: FRC
	@cd init.d; pwd; $(MAKE) $(MFLAGS) all

ins_init.d: FRC
	@cd init.d; pwd; $(MAKE) $(MFLAGS) install

$(SHFILES):
	sh $@.sh $(ROOT)

install: $(ALL) ins_all ins_init.d $(RCDIRS)

ins_all : $(ROOTUSRSBINF) $(ROOTUSRSBINL) $(ROOTETCTABS) \
	$(ROOTDFSTAB)

# Don't re-install directories already installed by Targetdirs
#$(DIRS):
#	$(INS.dir)

FRC:

clean lint:

include	../Makefile.targ
