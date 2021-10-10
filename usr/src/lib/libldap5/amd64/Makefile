#
# Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
#

PR_MDUSOBJS = os_SunOS_x86.o

include ../Makefile.com
include ../../Makefile.lib.64

LDLIBS +=	-L$(NSS_LDPATH64) $(NSS_LIBS)
DYNFLAGS +=	-R$(NSS_LDPATH64)

all: stub $(LIBS)

install: stubinstall all $(ROOTLIBS64) $(ROOTLINKS64) 

stubinstall: stub $(STUBROOTLIBS64) $(STUBROOTLINKS64)
