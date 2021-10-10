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
# Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
# 

PROG= krb5kdc
MANIFEST=	krb5kdc.xml

OBJS	= \
	dispatch.o\
	do_as_req.o\
	do_tgs_req.o\
	extern.o\
	kdc_preauth.o\
	kdc_util.o\
	main.o\
	network.o\
	policy.o\
	replay.o \
        fast_util.o \
        kdc_authdata.o

SRCS	= $(OBJS:.o=.c)

DEFS =  -DHAVE_LIBSOCKET=1 -DHAVE_LIBNSL=1 \
	-DHAVE_SYSLOG_H=1 -DHAVE_STDARG_H=1 \
	-DHAVE_SYS_SELECT_H=1 -DHAVE_OPENLOG=1 \
	-DHAVE_SYSLOG=1 -DHAVE_CLOSELOG=1 -DHAVE_STRFTIME=1\
	-DHAVE_VSPRINTF=1 -DHAVE_COMPILE=1 -DHAVE_STEP=1 \
	-DHAVE_NETINET_IN_H=1 -DHAVE_INET_NTOP=1 \
	-DHAVE_SYS_SOCKIO_H=1 -DHAVE_SYS_SELECT_H=1 -DPOSIX_SIGNALS=1 \
	-DLIBDIR=\"/usr/lib\"

CLOBBERFILES += $(RSRC) 

include ../../Makefile.cmd
include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

POFILE = $(PROG).po
POFILES = generic.po

ROOTMANIFESTDIR=	$(ROOTSVCNETWORKSECURITY)

CFLAGS += $(CCOPTS) $(DEFS) $(LOCALINCLUDE)

CPPFLAGS += \
	-I$(SRC)/lib/gss_mechs/mech_krb5/include \
	-I$(SRC)/lib/gss_mechs/mech_krb5/include/krb5\
	-I$(SRC)/uts/common/gssapi/mechs/krb5/include \
	-DHAVE_SYSLOG_H=1 

# c99=%all supports the inline keyword found in some krb header files
C99MODE=	-xc99=%all
C99LMODE=	-Xc99=%all

LDFLAGS += $(KRUNPATH) $(KERBRUNPATH)
LDLIBS += -L$(LROOT_KLIBDIR) -L$(LKRB5LIB) \
	  -lmech_krb5 -lkdb -lkadm5srv -lnsl -lsocket -lbsm -lc

.KEEP_STATE:

all: $(PROG) 

$(PROG):       $(OBJS)  
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(KRB5LIBPROG):=	FILEMODE = 500

install: $(KRB5LIBPROG) $(ROOTSVCMETHOD) $(ROOTMANIFEST)

check:	$(CHKMANIFEST)

clean:
	$(RM) $(OBJS)

lint: lint_SRCS

include ../../Makefile.targ

$(POFILE): $(DERIVED_FILES) .WAIT $(POFILES)
	$(RM) $@
	$(CAT) $(POFILES) > $@

generic.po: FRC
	$(RM) messages.po
	$(XGETTEXT) $(XGETFLAGS) `$(GREP) -l gettext *.[ch]`
	$(SED) "/^domain/d" messages.po > $@
	$(RM) messages.po

FRC:
