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
# Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG=		telnet
OBJS=		telnet.o commands.o main.o network.o ring.o \
		sys_bsd.o terminal.o utilities.o genget.o

include		$(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5
AUTH_OBJS=	auth.o authenc.o kerberos5.o
AUTH_INCLUDES=	-I$(SRC)/uts/common/gssapi/mechs/krb5/include \
		-I$(SRC)/lib/gss_mechs/mech_krb5/include \
		-I$(SRC)/lib/gss_mechs/mech_krb5

ENC_OBJS=	enc_des.o encrypt.o

OBJS +=		$(AUTH_OBJS) $(ENC_OBJS)

SRCS=		$(OBJS:%.o=%.c)

include		../../../Makefile.cmd

# -Dinline= needed because some of the krb header files use that keyword.
#  setting c99=%all could work but it causes the compile to fail and I don't
#  want to fix all those compile issues.
CPPFLAGS +=	-DKLUDGELINEMODE -DUSE_TERMIO -DENV_HACK -DOLD_ENVIRON -Dinline=
CPPFLAGS +=	$(AUTH_INCLUDES)

# The LINTFLAGS line below suppresses lint warnings about unused lint keywords
# in header files.  This happens to be the case for some krb headers.
LINTFLAGS +=  -erroff=E_SUPPRESSION_DIRECTIVE_UNUSED

LDLIBS +=	-lsocket -lnsl -ltermlib
LDLIBS +=	-lmech_krb5 
LDFLAGS +=	$(KRUNPATH) \
		-L$(LROOT)$(KLIBDIR_DO) -L$(LROOT)$(KLIBDIR_GL)

.KEEP_STATE:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTPROG)

clean:
	$(RM) $(OBJS)

lint:   lint_SRCS

include		../../../Makefile.targ
