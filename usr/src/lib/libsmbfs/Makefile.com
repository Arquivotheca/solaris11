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
# Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
#

#
# lib/libsmbfs/Makefile.com
#

LIBRARY=	libsmbfs.a
VERS=		.1

# leaving out: kiconv.o

OBJ_LIB=\
	acl_api.o \
	acl_print.o \
	charsets.o \
	connect.o \
	crypt.o \
	ctx.o \
	derparse.o \
	door.o \
	file.o \
	findvc.o \
	getaddr.o \
	iod_cl.o \
	iod_wk.o \
	keychain.o \
	krb5ssp.o \
	mbuf.o \
	nb.o \
	nb_name.o \
	nb_net.o \
	nb_ssn.o \
	nbns_rq.o \
	negprot.o \
	netshareenum.o \
	newvc.o \
	nls.o \
	ntlm.o \
	ntlmssp.o \
	print.o \
	pwdutil.o \
	rap.o \
	rq.o \
	signing.o \
	spnego.o \
	spnegoparse.o \
	ssnsetup.o \
	ssp.o \
	subr.o \
	ui-sun.o \
	utf_str.o

OBJ_CMN= smbfs_ntacl.o 

OBJECTS= $(OBJ_LIB) $(OBJ_CMN)

include $(SRC)/lib/Makefile.lib

LIBS =		$(DYNLIB) $(LINTLIB)

SRCDIR=		../smb
CMNDIR=		$(SRC)/common/smbclnt

SRCS=		$(OBJ_LIB:%.o=$(SRCDIR)/%.c) \
		$(OBJ_CMN:%.o=$(CMNDIR)/%.c)

$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

C99MODE=	$(C99_ENABLE)

LDLIBS += -lscf -lsocket -lnsl -lc -lmd -lpkcs11 -lkrb5 -lsec -lidmap -lnvpair \
	-lsmb

# normal warnings...
CFLAGS	+=	$(CCVERBOSE) 

CPPFLAGS += -D__EXTENSIONS__ -D_REENTRANT -DMIA \
	-I$(SRCDIR) -I.. \
	-I$(SRC)/uts/common \
	-I$(SRC)/common/smbclnt

# Debugging
${NOT_RELEASE_BUILD} CPPFLAGS += -DDEBUG

# uncomment these for dbx debugging
#COPTFLAG = -g
#CTF_FLAGS =
#CTFCONVERT_O=
#CTFMERGE_LIB=

# Filter out the less important lint.
# See lgrep.awk
LGREP =	nawk -f $(SRCDIR)/lgrep.awk
LTAIL	+=	2>&1 | $(LGREP)

all:	stub $(LIBS)

lint:	lintcheck_t

include ../../Makefile.targ

lintcheck_t: $$(SRCS)
	$(LINT.c) $(LINTCHECKFLAGS) $(SRCS) $(LDLIBS) $(LTAIL)

objs/%.o pics/%.o: $(CMNDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

.KEEP_STATE:
