#
# Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG= klist

OUTPUT_OPTION = -I. 

OBJS	= klist.o                       
SRCS	= $(OBJS:.o=.c)

include ../../Makefile.cmd
include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

TEXT_DOMAIN = SUNW_OST_OSCMD
POFILE = klist.po
POFILES = generic.po

DEFS = -DHAVE_GETIPNODEBYADDR=1 -DHAVE_INET_NTOP=1

CPPFLAGS += -I$(SRC)/lib/gss_mechs/mech_krb5/include \
		-I$(SRC)/uts/common/gssapi/mechs/krb5/include \
		$(DEFS)

# c99=%all supports the inline keyword found in some krb header files
C99MODE=	-xc99=%all
C99LMODE=	-Xc99=%all

LDFLAGS += $(KRUNPATH)
LDLIBS += $(KMECHLIB)

$(GPROGS) := CPPFLAGS += -DSYSV -DSunOS=50 

.KEEP_STATE:

all: $(PROG)

$(PROG):       $(OBJS)  
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: $(KRB5PROG)

install_h:

clean:
	$(RM) $(OBJS)

lint: lint_SRCS

include ../../Makefile.targ

$(POFILE): $(DERIVED_FILES) .WAIT $(POFILES) 
	$(RM) $@ 
	$(CAT) $(POFILES) > $@ 

generic.po:
	$(RM) messages.po
	$(XGETTEXT) $(XGETFLAGS) `($(GREP) -l gettext *.[ch] || echo /dev/null)`
	$(SED) "/^domain/d" messages.po > $@
	$(RM) messages.po
