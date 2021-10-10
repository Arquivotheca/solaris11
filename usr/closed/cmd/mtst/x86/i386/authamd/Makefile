#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"%Z%%M%	%I%	%E% SMI"

LIB = mtst_AuthenticAMD.so

OBJS = \
	aamd_main.o \
	aamd_synth.o

LIBSRCDIR = ../../common/authamd

include ../../Makefile.cpumod

CPPFLAGS += -I$(SRC)/uts/intel

LDLIBS += -lc

include ../../Makefile.cpumod.targ
