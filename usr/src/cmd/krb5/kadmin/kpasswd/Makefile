#
# Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG= kpasswd

OBJS	= kpasswd.o kpasswd_solaris.o
SRCS	= $(OBJS:.o=.c)

CLOBBERFILES += $(TESTPROG)

include ../../../Makefile.cmd
include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

POFILE = $(PROG).po
POFILES = generic.po

#
# Override $ROOTLIB
#
KRB5LIBDIR=	$(LROOT)$(KERBLIBDIR)

CPPFLAGS += -I$(SRC)/lib/gss_mechs/mech_krb5/include \
        -I$(SRC)/lib/gss_mechs/mech_krb5/include/krb5 \
	-I$(SRC)/uts/common/gssapi/include/ \
	-I$(SRC)/uts/common/gssapi/mechs/krb5/include \
	-I$(SRC)/lib/krb5 \
	-DHAVE_LIBSOCKET=1 -DHAVE_LIBNSL=1 -DHAVE_UNISTD_H=1 \
	-DHAVE_SYS_TIMEB_H=1 -DHAVE_ALLOCA_H=1 -DHAVE_FTIME=1 \
	-DHAVE_TIMEZONE -DUSE_KADM5_API_VERSION=2

COPTFLAG += $(XESS) #-I$(KINCDIR) 

# c99=%all supports the inline keyword found in some krb header files
C99MODE=	-xc99=%all
C99LMODE=	-Xc99=%all

LDFLAGS += $(KRUNPATH) $(KERBRUNPATH)
LDLIBS += -L $(KRB5LIBDIR)
LDLIBS += $(KMECHLIB) -lkadm5clnt

.KEEP_STATE:

all: $(PROG)

$(PROG):	$(OBJS) 
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: $(KRB5PROG)

clean:
	$(RM) $(OBJS)

lint: lint_SRCS

include ../../../Makefile.targ

$(POFILE): $(DERIVED_FILES) .WAIT $(POFILES)
	$(RM) $@
	$(CAT) $(POFILES) > $@
	$(RM) generic.po

generic.po:
	$(RM) messages.po
	$(XGETTEXT) $(XGETFLAGS) `$(GREP) -l gettext *.[ch]`
	$(SED) "/^domain/d" messages.po > $@
	$(RM) messages.po

