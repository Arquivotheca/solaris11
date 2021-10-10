#
# Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
#


include $(SRC)/cmd/Makefile.cmd
include $(SRC)/cmd/Makefile.cmd.64
include ../Makefile.locale
include ../Makefile.target

LINTFLAGS += -um -D_LOCALEDEF -I. -I..
$(ISOOBJECT) := CFLAGS += -K pic $(CCVERBOSE) -xregs=no%appl
$(ISOOBJECT) := CFLAGS64 += -K pic $(CCVERBOSE) -xregs=no%appl 

CLOBBERFILES =	$(ISOOBJECT) $(ISOLOCALE)

CLEANFILES = $(ISOLOCALE).o

.KEEP_STATE:

.NO_PARALLEL:

all: $(ISOOBJECT)

install: all $(ROOTDIRS64) $(ROOTISOOBJECT64)

%.o:	../%.c
	$(COMPILE.c) $<

clean:
	$(RM) $(CLEANFILES)

clobber:	clean
	$(RM) $(PROG) $(CLOBBERFILES)

lint:	FRC

catalog:	FRC

FRC:

#
# WARNING: if the output of localedef changes then this ISOLOCALE
# must be regenerated and the C source put here.
# If you know of a way to build a makefile dependency then please do so.
#

$(ISOOBJECT):	$(ISOLOCALE).o
	$(CC) $(CFLAGS) -o $@ -G $(DYNFLAGS) $(ISOLOCALE).o \
		-L $(LDLIBROOT)/usr/lib/$(MACH64) $(LDLIBS) -lc
	$(POST_PROCESS_SO)
