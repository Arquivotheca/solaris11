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
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY =	libbe_py.a
VERS =
OBJECTS =	libbe_py.o
PYSRCS =	libbe.py

include ../../Makefile.lib

PYTHON = 	$(PYTHON_26)
LIBLINKS = 
SRCDIR =	../common
ROOTLIBDIR=	$(ROOT)/usr/lib/python2.6/vendor-packages
STUBROOTLIBDIR=	$(STUBROOT)/usr/lib/python2.6/vendor-packages
LROOTLIBDIR=	$(LROOT)/usr/lib/python2.6/vendor-packages
PYOBJS=		$(PYSRCS:%.py=$(SRCDIR)/%.pyc)
PYFILES=	$(PYSRCS) $(PYSRCS:%.py=%.pyc)
ROOTPYBEFILES=  $(PYFILES:%=$(ROOTLIBDIR)/%)

C99MODE=        $(C99_ENABLE)

LIBS =		$(DYNLIB)
LDLIBS +=	-lbe -lnvpair -lc
CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-I/usr/include/python2.6 -D_FILE_OFFSET_BITS=64 -I../../libbe/common
FILEMODE=444

.KEEP_STATE:

all install := LDLIBS += -lpython2.6

all: stub $(PYOBJS) $(LIBS)

install: all $(ROOTPYBEFILES)

$(ROOTLIBDIR)/%: %
	$(INS.pyfile)

lint: lintcheck

include ../../Makefile.targ
