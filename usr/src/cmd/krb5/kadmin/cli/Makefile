#
# Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
#


PROG= kadmin kadmin.local
SHFILES= k5srvutil
CLOBBERFILES= $(SHFILES) 

KRB5SBINSHFILES= $(SHFILES:%=$(KRB5SBIN)/%)

COMMON_OBJS   = kadmin.o kadmin_ct.o ss_wrapper.o getdate.o
RMT_OBJS= $(COMMON_OBJS) keytab.o
LOC_OBJS= $(COMMON_OBJS) keytab_local.o
OBJS	= $(COMMON_OBJS) $(RMT_OBJS) $(LOC_OBJS)
SRCS	= $(OBJS:.o=.c)

include ../../../Makefile.cmd
include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

POFILE = generic.po
POFILES = kadmin.po k5srvutil.po

CPPFLAGS += -I$(SRC)/uts/common/gssapi/include/ \
		-I$(SRC)/uts/common/gssapi/mechs/krb5/include \
		-I$(SRC)/uts/common/gssapi/include \
		-I$(SRC)/cmd/krb5/iprop \
		-I$(SRC)/lib/krb5 \
                -I$(SRC)/lib/gss_mechs/mech_krb5/include/krb5 \
		-I$(SRC)/lib/gss_mechs/mech_krb5/include $(DEFS)

C99MODE=	-xc99=%all
C99LMODE=	-Xc99=%all

COPTFLAG += $(XESS) #-I$(KINCDIR) 

SHAREDLIBS = -lmech_krb5 -lss

CLLIBS = $(LDLIBS) $(KRUNPATH) $(KERBRUNPATH) \
	-L$(LROOT_KLIBDIR) -L$(LKRB5LIB) \
	$(SHAREDLIBS) -lkadm5clnt

SRVLIBS = $(LDLIBS) $(KRUNPATH) $(KERBRUNPATH) \
	-L$(LROOT_KLIBDIR) -L$(LKRB5LIB) \
	$(SHAREDLIBS) -lkadm5srv

.KEEP_STATE:

all: $(PROG) $(SHFILES)

kadmin:	$(RMT_OBJS)
	$(LINK.c) $(RMT_OBJS) -o $@ $(CLLIBS)
	$(POST_PROCESS)

kadmin.local:	$(LOC_OBJS)
	$(LINK.c) $(LOC_OBJS) -o $@ $(SRVLIBS)
	$(POST_PROCESS)

$(SHFILES): $(SHFILES).sh
	$(RM) $(SHFILES)
	$(CP) $(SHFILES).sh $(SHFILES)

install: $(KRB5SBINPROG) $(KRB5SBINSHFILES)

clean:
	$(RM) $(OBJS)

lint: lint_SRCS

include ../../../Makefile.targ

$(POFILE): $(DERIVED_FILES) .WAIT $(POFILES)
	$(RM) $@
	$(CAT) $(POFILES) > $@

kadmin.po: FRC
	$(RM) messages.po
	$(XGETTEXT) $(XGETFLAGS) `$(GREP) -l gettext *.[ch]`
	$(SED) "/^domain/d" messages.po > $@
	$(RM) messages.po

FRC:
