#
# Copyright (c) 1989, 2010, Oracle and/or its affiliates. All rights reserved.
#

PROG=			ksh
XPG4PROG=		sh
LIBSH=			libsh.a
LINKS=			rksh
ROOTSUNOSLINKS=		$(LINKS:%=$(ROOTSUNOSBIN)/%)

SUBDIRS=		ksh-builtins

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

OBJS= \
	arith.o		args.o		builtin.o			\
	cmd.o		ctype.o		defs.o		edit.o		\
	echo.o		error.o		expand.o	fault.o		\
	history.o	io.o		jobs.o		macro.o		\
	main.o		msg.o		name.o		print.o		\
	service.o	string.o	stak.o		test.o		\
	word.o		xec.o		emacs.o		vi.o

LIBSHOBJS= \
	adjust.o	assign.o	assnum.o	cannon.o	\
	chkid.o		convert.o	findnod.o	gettree.o	\
	strmatch.o	growaray.o	gsort.o		linknod.o	\
	namscan.o	rjust.o		strdata.o	streval.o	\
	tilde.o		unassign.o	utos.o		valup.o		\
	optget.o	csi.o

XPG4OBJS= $(OBJS:%.o=xpg4_%.o)			\
	$(LIBSHOBJS:%.o=xpg4_%.o) 

TXTS=	sh/apollo.c sh/editlib.c sh/vfork.c

MAINSRCS=	$(OBJS:%.o=sh/%.c)
LIBSHSRCS=	$(LIBSHOBJS:%.o=shlib/%.c)
SRCS=		$(MAINSRCS) $(LIBSHSRCS)
POFILES=	$(OBJS:%.o=sh/%.po) $(PFPOFILES) $(LIBSHOBJS:%.o=shlib/%.po)

XGETFLAGS +=	-a -n -x ksh.xcl
CPPFLAGS =	-DWEXP -DKSHELL -Iinclude $(CPPFLAGS.master)
CPPFLAGS +=	-I ../../../src/cmd/sh
CPPFLAGS +=	-D_FILE_OFFSET_BITS=64
CPPFLAGS +=	-DCSI_ASCIIACCEL
$(XPG4) := CPPFLAGS += -DXPG4

LDLIBS += -lsocket

CLOBBERFILES += $(PROG) $(LIBSH) $(XPG4)

install	:= TARGET= install
clobber	:= TARGET= clobber

.KEEP_STATE:

.PARALLEL: $(OBJS) $(LIBSHOBJS) $(XPG4OBJS)

all: $(PROG) $(TXTS) $(XPG4)

%.o:	sh/%.c
	$(COMPILE.c) -o $@ $<

xpg4_%.o:	sh/%.c
	$(COMPILE.c) -o $@ $<

%.o:	shlib/%.c
	$(COMPILE.c) -o $@ $<

xpg4_%.o:	shlib/%.c
	$(COMPILE.c) -o $@ $<

%.o:	../../../src/cmd/sh/%.c
	$(COMPILE.c) -o $@ $<

xpg4_%.o:	../../../src/cmd/sh/%.c
	$(COMPILE.c) -o $@ $<

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

$(LIBSH): $(LIBSHOBJS)
	$(AR) cr $(LIBSH) $(LIBSHOBJS)

$(PROG): $(OBJS) $(LIBSH)
	$(LINK.c) $(OBJS) $(LIBSH) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(XPG4): $(XPG4OBJS)
	$(LINK.c) $(XPG4OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(POFILE): $(POFILES)
	$(RM) $@
	cat $(POFILES) > $@

# based on .c.po, BUILD.po from Makefile.master
%.po:	../../../src/cmd/sh/%.c
	$(COMPILE.cpp) $<  > $(<F).i
	$(XGETTEXT) $(XGETFLAGS) $(<F).i ;\
	$(RM)	$@ ;\
	sed "/^domain/d" < messages.po  > $@ ;\
	$(RM) messages.po $(<F).i

$(ROOTSUNOSBIN)/%: $(ROOTSUNOSBINPROG)
	$(INS.link)

install: all $(ROOTXPG4PROG) $(ROOTSUNOSBINPROG) $(ROOTSUNOSLINKS)

install clobber: $(SUBDIRS)

clean:
	$(RM) $(OBJS) $(LIBSHOBJS) $(XPG4OBJS)

lint: lint_SRCS

include $(SRC)/cmd/Makefile.targ

FRC:
