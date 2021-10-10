#
# Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY =	libike.a
VERS =		.1
include $(SRC)/../closed/lib/libike/Makefile.sshdefs
include $(SRC)/lib/Makefile.lib
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

LIBS = $(DYNLIB) $(LINTLIB)

SRCDIR =	../common

MAPFILES +=	../common/mapfile.extern

LDLIBS +=	-lc -lnsl -lxnet -lcryptoutil -lmd -lipsecutil

CFLAGS +=	$(C_BIGPICFLAGS) $(CCVERBOSE)
CFLAGS64 +=	$(C_BIGPICFLAGS64) $(CCVERBOSE)
# to compile a debug version, "make LIBIKE_DEBUG=-DDEBUG_LIGHT"
CFLAGS += ${LIBIKE_DEBUG}
CFLAGS64 += ${LIBIKE_DEBUG}
CFLAGS += -_gcc=-fasm

CPPFLAGS +=	-I$(SRCDIR) -I$(ROOT)/usr/include/ike
CPPFLAGS +=	-D_XOPEN_SOURCE=500 -D__EXTENSIONS__ 
CPPFLAGS +=	-DHAVE_POSIX_STYLE_SOCKET_PROTOTYPES

.KEEP_STATE:

all:		stub $(SUBDIRS) $(DYNLIB)

$(SUBDIRS):
	-@mkdir -p pics/$@

$(LINTLIB):=	SRCS = $(SRCDIR)/$(LINTSRC)

lint: lintcheck

include $(SRC)/lib/Makefile.targ
