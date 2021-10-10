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
# Copyright (c) 1990, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY= 	liboamuser.a

DATEFILE= 	ugdates
DATEFILESRC= 	ugdates.dat
TXT= 		$(DATEFILESRC)

OBJECTS=	putgrent.o \
		file.o \
		vgid.o \
		vgname.o \
		vgroup.o \
		vuid.o \
		vlogin.o \
		vproj.o \
		dates.o \
		vexpire.o \
		putprojent.o \
		vprojid.o \
		vprojname.o \
		files_repops.o \
		utils.o \
		nss_repops.o \
		ldap_repops.o \
		groups.o \
		proj.o
#
# message catalogue file
#
PROG=		lib
POFILE=		lib.po

# include library definitions
include ../../Makefile.cmd
include ../../../lib/Makefile.lib

POFILES=	files_repops.po groups.po ldap_repops.po proj.po \
		utils.po	
SRCDIR =	.

GREP= 		grep
FILEMODE=	$(LIBFILEMODE)

PRODUCT=	$(LIBRARY) $(DATEFILE) $(POFILE)

# Must retain `lib', since default expands to nothing
LLINTLIB=	llib-l$(LIBRARY:lib%.a=lib).ln

CLEANFILES=	$(LLINTLIB) $(POFILE) $(POFILES) $(LIBRARY)
CLOBBERFILES=	$(DATEFILE)

GENERAL=	../inc
LDAPINC=        $(SRC)/lib/libsldap/common
CPPFLAGS=       -I. -I$(GENERAL) -I$(LDAPINC) $(CPPFLAGS.master)
ARFLAGS=	cr
AROBJS=		`$(LORDER) $(OBJS) | $(TSORT)`

ROOTUSRSADM=	$(ROOT)/usr/sadm
ROOTUSRSADMFILE=$(DATEFILE:%=$(ROOTUSRSADM)/%)

.KEEP_STATE:

all:		$(PRODUCT) $(TXT)

$(DATEFILE):	$(DATEFILESRC)
	$(GREP) -v "^#ident" $(DATEFILESRC) > $(DATEFILE)

install:	all $(DATEFILE) $(ROOTUSRSADMFILE)


$(POFILE): $(POFILES)
	$(RM) $@
	$(CAT) $(POFILES) > $@

_msg:	$(MSGDOMAINPOFILE)

$(ROOTUSRSADM)/%: %
	$(INS.file)

lint:		$(LLINTLIB)

$(LLINTLIB):	$(SRCS)
	$(LINT.c) $(LINTCHECKFLAGS) $(CPPFLAGS) -o oamuser $(SRCS) $(LDLIBS)

include ../../../lib/Makefile.targ
