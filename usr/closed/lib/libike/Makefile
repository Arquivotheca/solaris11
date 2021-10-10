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
# Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
#

include $(SRC)/lib/Makefile.lib
include Makefile.sshdefs
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

HDRDIR =	common 
IKEHDRDIR=	$(ROOT)/usr/include/ike
IKEHDRS=	$(HDRS:%=$(IKEHDRDIR)/%)
SUBDIRS =	$(MACH)
$(BUILD64)SUBDIRS +=	$(MACH64)

all	:=	TARGET = all
clean	:=	TARGET = clean
clobber	:=	TARGET = clobber
install	:=	TARGET = install
lint	:=	TARGET = lint
stub :=		TARGET = stub
stubinstall :=	TARGET = stubinstall

.KEEP_STATE:

.PARALLEL:	$(IKEHDRS)

all install clean clobber lint stub stubinstall:	$(SUBDIRS)

# Empty target for lint, check, and _msg because it's not Sun code.
check _msg:

install_h:	$(IKEHDRS)

# NOTE: For export source, "make" in this library should generate an
# empty file.  Keep this up to date with VERS over in Makefile.com.
CRYPT_SRC:	EXPORT_SRC
	$(ECHO) "EXPORT_SRC:" > Makefile+
	$(ECHO) '	/bin/tail +5 Makefile > Makefile+' >> Makefile+
	$(ECHO) '	\044(MV) Makefile+ Makefile' >> Makefile+
	$(ECHO) "" >> Makefile+
	$(MV) Makefile Makefile++
	$(CAT)	Makefile+ Makefile++ > Makefile
	$(RM) Makefile+ Makefile++

EXPORT_SRC:
	$(RM) -rf common/ssh* sparc i386 Makefile.sshdefs Makefile
	$(ECHO) "# This file intentionally left blank" > Makefile.sshdefs
	$(ECHO) "include \044(SRC)/lib/Makefile.lib" > Makefile
	$(ECHO) "" >> Makefile
	$(ECHO) "all lint clean clobber check install_h _msg stub stubinstall:" >> Makefile
	$(ECHO) "" >> Makefile
	$(ECHO) "install:" >> Makefile
	$(ECHO) "\t\044(RM) \044(ROOTLIBDIR)/libike.so.1" >> Makefile
	$(ECHO) "\t\044(TOUCH) \044(ROOTLIBDIR)/libike.so.1" >> Makefile
	$(ECHO) "\t\044(CHMOD) 0755 \044(ROOTLIBDIR)/libike.so.1" >> Makefile
	$(ECHO) "\t\044(RM) \044(ROOTLIBDIR64)/libike.so.1" >> Makefile
	$(ECHO) "\t\044(TOUCH) \044(ROOTLIBDIR64)/libike.so.1" >> Makefile
	$(ECHO) "\t\044(CHMOD) 0755 \044(ROOTLIBDIR64)/libike.so.1" >> Makefile

# The following pattern rules map 1:1 to directories within the
# vendor-supplied tree containing header files to be installed in
# $(IKEHDRDIR).  This list will need to change over time as our
# supplier reorganizes their source tree.  
# 
# NOTE: we tried using VPATH to construct this list, but it caused too
# many spurious rebuilds during incrementals with pmake.  
# So, we used the next best thing, a keyboard macro.

$(IKEHDRDIR)/%:	$(IKEHDRDIR) $(MACH)/%
	$(INS.file)
$(IKEHDRDIR)/%:	$(IKEHDRDIR) common/%
	$(INS.file)
$(IKEHDRDIR)/%:	$(IKEHDRDIR) common/sshapputil/%
	$(INS.file)
$(IKEHDRDIR)/%:	$(IKEHDRDIR) common/sshasn1/%
	$(INS.file)
$(IKEHDRDIR)/%:	$(IKEHDRDIR) common/sshcert/%
	$(INS.file)
$(IKEHDRDIR)/%:	$(IKEHDRDIR) common/sshcryptoaux/%
	$(INS.file)
$(IKEHDRDIR)/%:	$(IKEHDRDIR) common/sshcrypto/%
	$(INS.file)
$(IKEHDRDIR)/%:	$(IKEHDRDIR) common/sshcrypto/sshcipher/%
	$(INS.file)
$(IKEHDRDIR)/%:	$(IKEHDRDIR) common/sshcrypto/sshcryptocore/%
	$(INS.file)
$(IKEHDRDIR)/%:	$(IKEHDRDIR) common/sshcrypto/sshhash/%
	$(INS.file)
$(IKEHDRDIR)/%:	$(IKEHDRDIR) common/sshcrypto/sshmac/%
	$(INS.file)
$(IKEHDRDIR)/%:	$(IKEHDRDIR) common/sshcrypto/sshpk/%
	$(INS.file)
$(IKEHDRDIR)/%:	$(IKEHDRDIR) common/sshcrypto/sshrandom/%
	$(INS.file)
$(IKEHDRDIR)/%:	$(IKEHDRDIR) common/sshenroll/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshexternalkey/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshgafp/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshhttp/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshisakmp/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshldap/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshmath/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshpkcs/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshsession/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshutil/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshutil/sshfsm/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshutil/sshadt/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshutil/sshnet/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshutil/sshasn1/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshutil/sshcore/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshutil/sshmisc/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshutil/sshaudit/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshutil/ssheloop/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshutil/ssholdadt/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshutil/sshstream/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshutil/sshstrutil/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshutil/sshsysutil/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshutil/sshtestutil/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshutil/sshpacketstream/%
	$(INS.file)
$(IKEHDRDIR)/%: $(IKEHDRDIR) common/sshvalidator/%
	$(INS.file)

$(IKEHDRDIR):
	$(INS.dir)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

include $(SRC)/lib/Makefile.targ
