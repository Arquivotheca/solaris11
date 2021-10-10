#
#
# Copyright (c) 1994, 2010, Oracle and/or its affiliates. All rights reserved.
#
# cmd/pax/Makefile
#
# note: you can modify the make as follows:
#
#       make COPTFLAG=-g ENVCPPFLAGS3="-DDEBUG"

include		$(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include		$(CLOSED)/Makefile.tonic

CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -D_NOIDENT

LINTFLAGS += -erroff=E_FUNC_DECL_VAR_ARG2

PROG =		pax

OBJS =		append.o buffer.o cpio.o create.o extract.o \
		fileio.o hash.o isyesno.o link.o list.o main.o \
		mem.o namelist.o names.o parseopt.o pass.o pathname.o \
		replace.o tar.o ttyio.o warn.o

SRCS =		$(OBJS:%.o=%.c)

POFILES=	$(OBJS:%.o=%.po)

DCFILE=		$(PROG).dc

DCFILES=	$(OBJS:%.o=%.dc)

CLOBBERFILES +=	$(DCFILES)

_dc :=		XGETFLAGS += -t

#CFLAGS +=	$(CCVERBOSE)

LDLIBS +=	-lgen -lnvpair -lcmdutils -lavl

.PARALLEL:	$(OBJS)

.KEEP_STATE:

all :		$(PROG)

install :	all .WAIT $(ROOTPROG)

$(PROG) :	$(OBJS)
		$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

$(POFILE):	$(POFILES)
		$(RM) $@
		cat $(POFILES) > $@

clean :
		$(RM) $(OBJS)

lint :		lint_SRCS

.SUFFIXES:	$(SUFFIXES) .dc

.c.dc:
	$(COMPILE.cpp) $< > $<.i
	$(BUILD.po)

$(DCFILE):	$(DCFILES)
	$(RM) $@
	cat $(DCFILES) > $@

include		$(SRC)/cmd/Makefile.targ
