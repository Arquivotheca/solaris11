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
# Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
#

include ../Makefile.lib

SUBDIRS =	$(MACH)
$(BUILD64)SUBDIRS += $(MACH64)

XGETFLAGS_ADT += -a

all :=		TARGET= all
clean :=	TARGET= clean
clobber :=	TARGET= clobber
install :=	TARGET= install
lint :=		TARGET= lint
package :=	TARGET= package
stub :=		TARGET = stub
stubinstall :=	TARGET = stubinstall

.KEEP_STATE:

COMMONDIR = common

#
# Macros for libbsm header files. These define user-level only interfaces.
#
GEN_EVENT_H =	audit_uevents.h
GEN_XML_C =	adt_xlate.c
GEN_XML_H =	adt_event.h
GENHDRS =	$(GEN_XML_H) $(GEN_EVENT_H) 
GENSRCS =	$(GEN_XML_C)
GENFILES =	$(GENSRCS:%=$(COMMONDIR)/%) $(GENHDRS:%=$(COMMONDIR)/%)
HDRS =		libbsm.h adt.h audit_private.h $(GENHDRS)
COMMONHDRS =	$(HDRS:%=$(COMMONDIR)/%)
ROOTHDRDIR = 	$(ROOT)/usr/include/bsm
ROOTHDRS = 	$(HDRS:%=$(ROOTHDRDIR)/%)
CHECKCHDRS =	$(COMMONHDRS:%.h=%.check)
$(ROOTHDRS) :=	FILEMODE = 0644

all install lint: $(GENFILES) $(SUBDIRS)
clean clobber stub stubinstall: $(SUBDIRS)

#
# Macros for libbsm database files. These should probably be installed
# from somewhere else. Until we find that better place, install them
# from here.
#

ROOTETCSECURITY = 	$(ROOT)/etc/security
$(ROOTETCSECURITY) := 	DIRMODE = 0755

ESFILES =		audit_class audit_event
ESSRC =			$(ESFILES:%=%.txt)
ROOTETCSECURITYFILES =	$(ESFILES:%=$(ROOTETCSECURITY)/%)
$(ROOTETCSECURITYFILES) := FILEMODE = 0644

VARAUDIT=$(ROOT)/var/audit
$(VARAUDIT) := DIRMODE = 0755

#
# message catalogue file
#
MSGFILES =	`$(GREP) -l gettext $(COMMONDIR)/*.c`
POFILE =	libbsm.po

#
# Definitions for XML (DTD AND XSL)
#
DTD =		adt_record.dtd.1
XSL =		adt_record.xsl.1
ROOTXMLDIR =	$(ROOT)/usr/share/lib/xml
ROOTDTDDIR=	$(ROOTXMLDIR)/dtd
ROOTXSLDIR=	$(ROOTXMLDIR)/style
ROOTDTD=	$(DTD:%=$(ROOTDTDDIR)/%)
ROOTXSL=	$(XSL:%=$(ROOTXSLDIR)/%)
ROOTXMLDIRS =	$(ROOTXMLDIR) $(ROOTDTDDIR) $(ROOTXSLDIR)
ROOTXMLFILES =	$(ROOTDTD) $(ROOTXSL)
$(ROOTXMLDIRS) :=  DIRMODE = 0755
$(ROOTXMLFILES) :=  FILEMODE = 0444

CPPFLAGS += -I$(COMMONDIR)
CPPFLAGS += -D_REENTRANT

CLEANFILES += $(GENFILES)

ADTXMLFILE =	$(COMMONDIR)/adt.xml
ADTXSDFILE =	$(COMMONDIR)/adt.xsd
AUDITXML =	auditxml

.KEEP_STATE:

check: $(CHECKCHDRS)
	xmllint --schema $(ADTXSDFILE) --noout $(ADTXMLFILE)

install_h: $(ROOTHDRDIR) $(ROOTHDRS)

install: install_dirs install_data

install_data: $(ESSRC) $(ROOTETCSECURITYFILES) $(ROOTXMLFILES)

install_dirs: $(VARAUDIT) $(ROOTETCSECURITY) $(ROOTXMLDIRS)

$(GEN_EVENT_H:%=$(COMMONDIR)/%): mkhdr.sh audit_event.txt
	sh mkhdr.sh

$(GEN_XML_H:%=$(COMMONDIR)/%) $(GEN_XML_C:%=$(COMMONDIR)/%): \
	$(ADTXMLFILE) $(AUDITXML)
	$(PERL) $(AUDITXML) -o $(COMMONDIR) $(ADTXMLFILE)

$(ROOTHDRDIR):
	$(INS.dir)

$(ROOTHDRDIR)/%: $(COMMONDIR)/%
	$(INS.file)

$(ROOTXMLDIRS):
	$(INS.dir)

$(ROOTDTDDIR)/% $(ROOTXSLDIR)/%: %
	$(INS.file)

$(VARAUDIT):
	$(INS.dir)

$(ROOTETCSECURITY):
	$(INS.dir)

$(ROOTETCSECURITY)/%: %.txt
	$(INS.rename)

$(POFILE):	 pofile_MSGFILES

_msg:	$(MSGDOMAINPOFILE)

# has strings but doesn't use gettext
adt_xlate.po: $(COMMONDIR)/adt_xlate.c
	$(RM) adt_xlate.po
	$(XGETTEXT) $(XGETFLAGS_ADT) $(COMMONDIR)/adt_xlate.c
	$(SED) "/^domain/d" < messages.po > adt_xlate.po
	$(RM) messages.po

$(SUBDIRS):	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

include ../Makefile.targ
include ../../Makefile.msg.targ
