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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY =	nssscf.a
VERS =
OBJECTS =	nssscf.o

PYSRCS=		__init__.py dns.py files.py ldap.py messages.py \
		nis.py nscd.py nssbase.py nsswitch.py

include ../../../Makefile.lib

LIBLINKS = 
SRCDIR =	../common
ROOTLIBDIR=	$(ROOT)/usr/lib/python2.6/vendor-packages/nss
STUBROOTLIBDIR=	$(STUBROOT)/usr/lib/python2.6/vendor-packages/nss
LROOTLIBDIR=	$(LROOT)/usr/lib/python2.6/vendor-packages/nss
PYTHON=		$(PYTHON_26)
PYOBJS=		$(PYSRCS:%.py=$(SRCDIR)/%.pyc)
PYFILES=	$(PYSRCS) $(PYSRCS:%.py=%.pyc)
ROOTPYNSSFILES= $(PYFILES:%=$(ROOTLIBDIR)/%)

C99MODE=        -xc99=%all
C99LMODE=       -Xc99=%all

LIBS =		$(DYNLIB)
LDLIBS +=	-lc -lscf
CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-I/usr/include/python2.6
CPPFLAGS +=	-I../../../libsldap/common

LINTFLAGS += -erroff=E_FUNC_ARG_UNUSED -erroff=E_NOP_IF_STMT
LINTFLAGS64 += -erroff=E_FUNC_ARG_UNUSED -erroff=E_NOP_IF_STMT

.KEEP_STATE:

all install := LDLIBS += -lpython2.6

all: stub $(PYOBJS) $(LIBS)

install: stubinstall all $(ROOTPYNSSFILES)

$(ROOTLIBDIR)/%: %
	$(INS.pyfile)

lint: lintcheck

include ../../../Makefile.targ
