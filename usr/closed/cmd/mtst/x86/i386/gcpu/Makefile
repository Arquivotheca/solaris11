#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"%Z%%M%	%I%	%E% SMI"

LIB = mtst_generic.so

OBJS = \
	gcpu_main.o \
	gcpu_synth.o

LIBSRCDIR = ../../common/gcpu

include ../../Makefile.cpumod

CPPFLAGS += -I$(SRC)/uts/intel

LDLIBS += -lc

include ../../Makefile.cpumod.targ
