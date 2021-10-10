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
# Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY= libkadm5clnt.a
VERS= .1

CLNT_OBJS = clnt_policy.o \
	client_rpc.o \
	client_principal.o \
	client_init.o \
	clnt_privs.o \
	clnt_chpass_util.o \
	logger.o \
	changepw.o \
	chpw.o

SHARED_OBJS = \
	alt_prof.o \
	chpass_util.o \
	kadm_rpc_xdr.o \
	misc_free.o \
	kadm_host_srv_names.o \
	str_conv.o

OBJECTS= $(CLNT_OBJS) $(SHARED_OBJS)

ISRCHDR= ../iprop.h
KRB5IPROPDIR=	$(SRC)/cmd/krb5/iprop

# include library definitions
include ../../../Makefile.lib

SRCS=		$(CLNT_OBJS:%.o=../%.c) \
		$(SHARED_OBJS:%.o=../../%.c)

LIBS=		$(DYNLIB)

include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

POFILE = $(LIBRARY:%.a=%.po)
POFILES = generic.po

#override liblink
INS.liblink=	-$(RM) $@; $(SYMLINK) $(LIBLINKS)$(VERS) $@

CPPFLAGS += -I.. -I../.. -I../../.. -I$(SRC)/lib/gss_mechs/mech_krb5/include \
	-I$(SRC)/lib/krb5 \
        -I$(SRC)/lib/krb5/kdb \
	-I$(SRC)/lib/gss_mechs/mech_krb5/include/krb5 \
	-I$(SRC)/uts/common/gssapi/ \
	-I$(SRC)/uts/common/gssapi/include/ \
	-I$(SRC)/uts/common/gssapi/mechs/krb5/include \
	-I$(SRC)/lib/gss_mechs/mech_krb5/krb5/os \
	-I$(KRB5IPROPDIR)

CFLAGS +=	-I..
LDLIBS +=	-lc

.KEEP_STATE:

all:	stub $(LIBS)

# Rpcgen-erate iprop.h from the iprop.x spec file
$(ISRCHDR):	$(KRB5IPROPDIR)/iprop.x
	$(RM)	$@
	$(RPCGEN) -h $(KRB5IPROPDIR)/iprop.x > $@

# Explicitly state the dependancy on iprop.h
$(LIBS): $(ISRCHDR)

CLEANFILES +=	$(ISRCHDR)

lint:	lintcheck

# include library targets
include ../../../Makefile.targ

pics/%.o: ../../%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

FRC:

generic.po: FRC
	$(RM) messages.po
	$(XGETTEXT) $(XGETFLAGS) `$(GREP) -l gettext ../*.[ch] ../../*.[ch]`
	$(SED) "/^domain/d" messages.po > $@
	$(RM) messages.po
