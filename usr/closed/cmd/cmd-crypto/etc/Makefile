#
# Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
#

# EXPORT DELETE START
# CRYPT DELETE START

ETCCRYPTOFILES +=	\
			certs/SUNWosnet \
			certs/SUNWosnetCF
ETCCERTSFILES +=	\
			SUNWosnetSE \
			SUNWosnetSolaris

# CRYPT DELETE END
# EXPORT DELETE END

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

.KEEP_STATE:

ROOTCRYPTODIR=		$(ROOT)/etc/crypto

ROOTETCCERTSDIR=	$(ROOT)/etc/certs

IETCCRYPTOFILES=	$(ETCCRYPTOFILES:%=$(ROOTCRYPTODIR)/%)
IETCCERTSFILES=		$(ETCCERTSFILES:%=$(ROOTETCCERTSDIR)/%)

$(ROOTCRYPTODIR)/%:	%
			$(INS.file)

$(ROOTETCCERTSDIR)/%:	certs/%
			$(INS.file)

$(IETCCRYPTOFILES):=	FILEMODE= 644
$(IETCCERTSFILES):=	FILEMODE= 644

install:	$(IETCCRYPTOFILES) $(IETCCERTSFILES)

# EXPORT DELETE START

EXPORT_SRC:
	$(RM) Makefile+
	$(SED) -e "/^# EXPORT DELETE START/,/^# EXPORT DELETE END/d" \
		< Makefile > Makefile+
	$(MV) -f Makefile+ Makefile
	$(CHMOD) 444 Makefile

# CRYPT DELETE START
CRYPT_SRC:
	$(RM) -r keys
	$(RM) Makefile+
	$(SED) -e "/^# CRYPT DELETE START/,/^# CRYPT DELETE END/d" \
		< Makefile > Makefile+
	$(MV) -f Makefile+ Makefile
	$(CHMOD) 444 Makefile
# CRYPT DELETE END

# EXPORT DELETE END
