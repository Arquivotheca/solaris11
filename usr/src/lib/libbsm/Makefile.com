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

LIBRARY =	libbsm.a
VERS = 		.1
OBJECTS=	adr.o \
		adrf.o \
		adrm.o \
		adt.o \
		adt_token.o \
		adt_xlate.o \
		au_open.o \
		au_preselect.o \
		au_to.o \
		au_usermask.o \
		audit_class.o \
		audit_cron.o \
		audit_crontab.o \
		audit_at.o \
		audit_event.o \
		audit_halt.o \
		audit_kadmind.o \
		audit_krb5kdc.o \
		audit_mountd.o \
		audit_plugin.o \
		audit_reboot.o \
		audit_rexd.o \
		audit_rexecd.o \
		audit_rshd.o \
		audit_scf.o \
		audit_scf_to_kernel.o \
		audit_settid.o \
		audit_shutdown.o \
		bsm.o \
		generic.o \
		getauditflags.o \
		getfaudflgs.o

#
# Include common library definitions.
include ../../Makefile.lib

#
# Install this library into /lib instead of /usr/lib.
include ../../Makefile.rootfs

LIBS =	 	$(DYNLIB) $(LINTLIB)

SRCDIR =	../common

$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)
CLEANFILES +=	$(LINTOUT) $(LINTLIB)

CFLAGS	+=	$(CCVERBOSE)
LDLIBS +=	-lsocket -lnsl -lmd -lc -ltsol -linetutil -lscf

AUDITD =	$(SRC)/cmd/auditd

CPPFLAGS += -I$(SRCDIR) -I$(AUDITD)
CPPFLAGS += -D_REENTRANT

#
# message catalogue file
#
TEXT_DOMAIN= SUNW_OST_OSLIB

.KEEP_STATE:

all: stub $(LIBS)

lint: $(LINTLIB) lintcheck

#
# Include library targets (and stubs).
include ../../Makefile.targ
