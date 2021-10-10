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

PROGS=		boot.wrapper halt.wrapper attach boot clone detach halt \
		image_install p2v pkgcreatezone poststate prestate \
		sysboot uninstall verify_cfg verify_adm postattach

USERFILES=	pkgrm.conf smf_disable.conf

TEMPLATES=	SYSdefault.xml SYSblank.xml SYSdefault-shared-ip.xml

XMLDOCS=	config.xml platform.xml common.ksh developerenv.ksh pkgrm.lst \
		smf_disable.lst

include $(SRC)/cmd/Makefile.cmd
include ../Makefile.solaris

SUNWTEMPLATE=	$(ROOTTEMPLATEDIR)/SUNWdefault.xml
SOLTEMPLATE=	$(ROOTTEMPLATEDIR)/SYSsolaris.xml

CLOBBERFILES=	$(ROOTPROGS) $(ROOTXMLDOCS) $(ROOTTEMPLATES) $(ETCUSER) $(SUNWTEMPLATE) $(SOLTEMPLATE)

all:	$(PROGS)

POFILES =       $(PROGS:%=%.po) common.po
POFILE =        solaris_zone.po

$(POFILE): $(POFILES)
	$(RM) $@
	$(CAT) $(POFILES) > $@

install: $(PROGS) $(ROOTPROGS) $(ROOTXMLDOCS) $(ROOTTEMPLATES) $(SUNWTEMPLATE) $(SOLTEMPLATE) $(ETCUSER)

$(SUNWTEMPLATE) $(SOLTEMPLATE):
	$(RM) $@
	$(SYMLINK) ./SYSdefault.xml $@

clean:
	-$(RM) $(PROGS) $(POFILES) $(POFILE)


lint:

include $(SRC)/cmd/Makefile.targ
