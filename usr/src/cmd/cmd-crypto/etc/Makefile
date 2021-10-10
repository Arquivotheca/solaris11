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
# Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
#

RELEASECERTS =		SUNW_SunOS_5.10
RELEASECRYPTO =		SUNW_SunOS_5.10

ETCCRYPTOFILES = \
	kcf.conf \
	pkcs11.conf \
	kmf.conf \
	$(RELEASECRYPTO:%=certs/%) \
	certs/CA \
	certs/SUNWObjectCA

ETCCERTSFILES = \
	$(RELEASECERTS) \
	SUNWSolarisCA \
	SUNWObjectCA

ANNOTATEDFILES = \
	certs/SUNWObjectCA.annot

include ../../Makefile.cmd
.KEEP_STATE:

ROOTETC=		$(ROOT)/etc
ROOTCRYPTODIR=		$(ROOTETC)/crypto
ROOTCRYPTOCERTSDIR=	$(ROOTCRYPTODIR)/certs

ROOTETCCERTSDIR=	$(ROOTETC)/certs

CADIR=			certs/CA
ROOTETCCADIR=		$(ROOTETC)/$(CADIR)

ROOTETCCALINKDIR=	$(ROOTETC)/openssl/certs
CATARGDIR=		../../$(CADIR)

IETCCRYPTOFILES=	$(ETCCRYPTOFILES:%=$(ROOTCRYPTODIR)/%)
IETCCERTSFILES=		$(ETCCERTSFILES:%=$(ROOTETCCERTSDIR)/%)

#
# CA certs are extracted from the NSS database libnssckbi.so. Each CA cert is
# pointed by a symbolic link. The name of the symbolic link file is determined
# by the "hash" value using openssl command. When the NSS database is updated,
# we will extract the CA certs out of it and deliver the new set of CA certs. If
# you add or remove a certificate from the $(CASRCDIR) directory ensure you
# update pkg/manifests/crypto-ca-certificates.mf.
#
# Note that the OpenSSL hash links in /etc/openssl/certs are managed by the
# svc:/system/ca-certificates service.
#
CASRCDIR =	CA-certs
OPENSSL =	/usr/bin/openssl

LISTCERTS = $(ECHO) $(CASRCDIR)/*.pem
CAFILES = $(LISTCERTS:sh)

IETCCAFILES +=		$(CAFILES:$(CASRCDIR)/%=$(ROOTETCCADIR)/%)

$(ROOTETCCADIR)/%:    $(CASRCDIR)/%
	$(INS.file)


$(ROOTCRYPTOCERTSDIR)/SUNWObjectCA: \
			$(ROOTETCCERTSDIR)/SUNWObjectCA
			$(RM) $@
			$(LN) $(ROOTETCCERTSDIR)/SUNWObjectCA $@
$(ROOTCRYPTODIR)/%:	%
			$(INS.file)
$(RELEASECRYPTO:%=$(ROOTCRYPTODIR)/certs/%): \
			certs/$(@F:SUNW_SunOS_5.%=SUNWCryptographic%)
			$(RM) $@
			$(INS) -s -m $(FILEMODE) -f $(@D) \
				certs/$(@F:SUNW_SunOS_5.%=SUNWCryptographic%)
			$(MV) $(@D)/$(@F:SUNW_SunOS_5.%=SUNWCryptographic%) $@

$(ROOTETCCERTSDIR)/SUNWObjectCA: \
			certs/$$(@F)
			$(RM) $@
			$(INS) -s -m $(FILEMODE) -f $(@D) certs/$(@F)
$(ROOTETCCERTSDIR)/%:	certs/%
			$(INS.file)
$(RELEASECERTS:%=$(ROOTETCCERTSDIR)/%): \
			certs/$(@F:SUNW_SunOS_5.%=SUNWSolaris%)
			$(RM) $@
			$(INS) -s -m $(FILEMODE) -f $(@D) \
				certs/$(@F:SUNW_SunOS_5.%=SUNWSolaris%)
			$(MV) $(@D)/$(@F:SUNW_SunOS_5.%=SUNWSolaris%) $@

ANNOT_DEL =	SOURCE ANNOTATION DELETE
$(ANNOTATEDFILES:%.annot=%): \
			$$(@:%=%.annot)
			$(RM) $@
			$(SED) -e '/${ANNOT_DEL} START/,/${ANNOT_DEL} END/d' \
				$(@:%=%.annot) > $@

$(IETCCRYPTOFILES):=	FILEMODE= 644
$(IETCCERTSFILES):=	FILEMODE= 644
$(IETCCAFILES):=	FILEMODE= 644

.PARALLEL:	$(IETCCAFILES)

install:	$(IETCCAFILES) $(IETCCRYPTOFILES) $(IETCCERTSFILES)

clean clobber:
		$(RM) $(ANNOTATEDFILES:%.annot=%)
