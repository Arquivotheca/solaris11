#
# Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG		=	kproplog

OBJS		=	kproplog.o
DERIVED_OBJS	=	iprop_xdr.o

SRCS=	$(OBJS:.o=.c)
SRCS+=	$(DERIVED_OBJS:.o=.c)

RSRC=	kproplog.c

# Definitions needed to rpcgen iprop-related files
ISRC= iprop.h iprop_xdr.c
KRB5IPROPDIR= $(SRC)/cmd/krb5/iprop
CMD= grep -v "usr/src/cmd/krb5/iprop" > $@

include	../../Makefile.cmd
include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

TEXT_DOMAIN = SUNW_OST_OSCMD
POFILE	= kproplog.po
POFILES	= generic.po

LDFLAGS		+=	-R$(KRB5RUNPATH) $(KRUNPATH)
LDLIBS		+=	-L$(LKRB5LIB) $(KLIB) \
			-lnsl -lkdb -lkadm5clnt

CPPFLAGS	+=	-I. -I$(SRC)/lib/krb5 \
			-I$(KRB5IPROPDIR) \
			-I$(SRC)/lib/gss_mechs/mech_krb5/include \
			-I$(SRC)/lib/gss_mechs/mech_krb5/include/krb5 \
			-I$(SRC)/uts/common/gssapi/mechs/krb5/include \
			-DUSE_KADM5_API_VERSION=2

# c99=%all supports the inline keyword found in some krb header files
C99MODE=	-xc99=%all
C99LMODE=	-Xc99=%all

all:		$(PROG)

$(PROG):	$(OBJS) $(DERIVED_OBJS)
	$(LINK.c) $(OBJS) $(DERIVED_OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

# Rules to rpcgen-erate derived files from the iprop.x spec file
iprop.h:	$(KRB5IPROPDIR)/iprop.x
	$(RM) $@
	$(RPCGEN) -h $(KRB5IPROPDIR)/iprop.x > $@

iprop_xdr.c:	iprop.h $(KRB5IPROPDIR)/iprop.x
	$(RM) $@
	$(RPCGEN) -c $(KRB5IPROPDIR)/iprop.x | $(CMD)

# Explicitly state the dependancy on iprop.h
$(OBJS): iprop.h

install:	$(KRB5SBINPROG)

lint:	$(ISRC) .WAIT lint_SRCS

clean:
	$(RM) $(OBJS) $(DERIVED_OBJS) $(ISRC)

include ../../Makefile.targ

$(POFILE): $(DERIVED_FILES) .WAIT $(POFILES)
	$(RM) $@
	$(CAT) $(POFILES) > $@

generic.po:
	$(RM) messages.po
	$(XGETTEXT) $(XGETFLAGS) `$(GREP) -l gettext *.[ch]`
	$(SED) "/^domain/d" messages.po > $@
	$(RM) messages.po
