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

PROG= kadmind
MANIFEST=	kadmin.xml

OBJS	= kadm_rpc_svc.o server_stubs.o ovsec_kadmd.o misc.o \
          ipropd_svc.o

DERIVED_OBJS= iprop_xdr.o

SRCS=	$(OBJS:.o=.c)
SRCS+=	$(DERIVED_OBJS:.o=.c)

# Definitions needed to rpcgen iprop-related files
ISRC= iprop.h iprop_xdr.c
KRB5IPROPDIR= $(SRC)/cmd/krb5/iprop
CMD= grep -v "usr/src/cmd/krb5/iprop" > $@

CLOBBERFILES += $(TESTPROG)

include ../../../Makefile.cmd
include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

POFILE = $(PROG).po
POFILES = generic.po

ROOTMANIFESTDIR=	$(ROOTSVCNETWORKSECURITY)

DEFS = -DHAVE_LIBSOCKET=1 -DHAVE_LIBNSL=1 -DHAVE_COMPILE=1 -DHAVE_STEP=1 \
	-DPOSIX_SIGNALS=1 -DUSE_KADM5_API_VERSION=2

CPPFLAGS += -I. -I$(SRC)/uts/common/gssapi/mechs/krb5/include \
		-I$(SRC)/uts/common/gssapi \
		-I$(SRC)/lib/krb5 \
		-I$(SRC)/lib/krb5/kadm5 \
		-I$(SRC)/lib/krb5/kadm5/srv \
		-I$(SRC)/lib/gss_mechs/mech_krb5/include \
		-I$(SRC)/lib/gss_mechs/mech_krb5/include/krb5 \
		-I$(KRB5IPROPDIR) \
		-I$(SRC)/uts/common/gssapi/include/ $(DEFS)

COPTFLAG += $(XESS) #-I$(KINCDIR) 

# c99=%all supports the inline keyword found in some krb header files
C99MODE=	-xc99=%all
C99LMODE=	-Xc99=%all

LDFLAGS += $(KRUNPATH) $(KERBRUNPATH)
LDLIBS += -lgss -L$(LKRB5LIB) \
	-lmech_krb5 -lkadm5srv -lkdb -lnsl -lresolv -lbsm -lpam -lsocket

.KEEP_STATE:

all: $(PROG)

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

$(KRB5LIBPROG):=	FILEMODE = 0500

install: $(KRB5LIBPROG) $(ROOTSVCMETHOD) $(ROOTMANIFEST)

check:	$(CHKMANIFEST)

clean:
	$(RM) $(OBJS) $(DERIVED_OBJS) $(ISRC)

lint: lint_SRCS

include ../../../Makefile.targ

$(POFILE): $(DERIVED_FILES) .WAIT $(POFILES)
	$(RM) $@
	$(CAT) $(POFILES) > $@

generic.po: FRC
	$(RM) messages.po
	$(XGETTEXT) $(XGETFLAGS) `$(GREP) -l gettext *.[ch]`
	$(SED) "/^domain/d" messages.po > $@
	$(RM) messages.po

FRC:
