#
# Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
#
# lib/raidcfg_plugins/mpt/Makefile.com
#

LIBRARY= mpt.a
VERS= .1

OBJECTS= mpt.o

# include library definitions
include $(SRC)/lib/Makefile.lib
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

ROOTLIBDIR=	$(ROOT)/usr/lib/raidcfg
ROOTLIBDIR64=	$(ROOTLIBDIR)/$(MACH64)

SRCDIR=		../common

LIBS=	$(DYNLIB)

LINTFLAGS +=	-DDEBUG

CPPFLAGS +=	-I$(SRC)/uts/common -I$(CLOSED)/uts/common
CFLAGS +=	$(CCVERBOSE) $(CPPFLAGS)

LDLIBS +=	-lc -ldevid -lcfgadm -ldevinfo

.KEEP_STATE:

all:	$(LIBS)

SECLEVEL = standard

lint:	lintcheck

# Install rules

$(ROOTLIBDIR)/%: % $(ROOTLIBDIR)
	$(INS.file)

$(ROOTLIBDIR64)/%: % $(ROOTLIBDIR64)
	$(INS.file)

$(ROOTLIBDIR) $(ROOTLIBDIR64):
	$(INS.dir)

# include library targets
include $(SRC)/lib/Makefile.targ

objs/%.o pics/%.o: ../common/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)
