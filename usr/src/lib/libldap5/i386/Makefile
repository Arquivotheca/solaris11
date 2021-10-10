#
# Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
#
# lib/libldap5/i386/Makefile
#

PR_MDUSOBJS = os_SunOS_x86.o

include ../Makefile.com

LDLIBS +=	-L$(NSS_LDPATH) $(NSS_LIBS)
DYNFLAGS +=	-R$(NSS_LDPATH)

all: stub $(LIBS)

install: stubinstall all $(ROOTLIBS) $(ROOTLINKS) $(ROOTLINT)

stubinstall: stub $(STUBROOTLIBS) $(STUBROOTLINKS)

FRC:
