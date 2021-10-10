#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#

PROG=	localedef

OBJS1=	gram.o

OBJS2=	check.o \
	copy.o \
	err.o \
	gen.o \
	init.o \
	localedef.o \
	method.o \
	scan.o \
	sem_chr.o \
	sem_ctype.o \
	sem_xlat.o \
	sem_method.o  \
	semstack.o \
	sem_coll.o \
	symtab.o \
	sem_comp.o

OBJS=	$(OBJS1) $(OBJS2)

SRCS= $(OBJS1:%.o=%.c) $(OBJS2:%.o=../%.c)

include $(SRC)/cmd/Makefile.cmd
include ../Makefile.locale
include ../Makefile.target
include ../Makefile.msg

$(PROG)	catalog := CFLAGS += -D_LOCALEDEF -I. -I..
LINTFLAGS += -um -D_LOCALEDEF -I.
$(ISOOBJECT) := CFLAGS += $(C_PICFLAGS) $(CCVERBOSE)
YFLAGS += -d

CLOBBERFILES =	$(ISOOBJECT) $(ISOLOCALE) $(POFILE)

CLEANFILES = $(OBJS) gram.c y.tab.h $(ISOLOCALE).o $(POFILES)

.KEEP_STATE:

.PARALLEL: $(OBJS)

all: $(PROG) .WAIT $(ISOOBJECT)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTPROG) \
	 $(ROOTISOOBJECT)

clean:
	$(RM) $(CLEANFILES)

clobber:	clean
	$(RM) $(PROG) $(CLOBBERFILES)

lint:
	$(LINT.c) $(SRCS) $(LDLIBS)

gram.c + y.tab.h :	../gram.y
	$(RM) gram.c y.tab.h
	$(YACC.y) ../gram.y
	mv y.tab.c gram.c

scan.o :	y.tab.h

%.o:	%.c
	$(COMPILE.c) $<

%.o:	../%.c
	$(COMPILE.c) $<

catalog :	$(MSGDOMAIN) $(POFILE)
	$(RM) $(MSGDOMAIN)/$(POFILE)
	$(CP) $(POFILE) $(MSGDOMAIN)

#
# WARNING: if the output of localedef changes then this ISOLOCALE
# must be regenerated and the C source put here.
# If you know of a way to build a makefile dependency then please do so.
#

$(ISOOBJECT):	$(ISOLOCALE).o
	$(CC) -o $@ $(GSHARED) $(DYNFLAGS) $(ISOLOCALE).o \
		$(LDLIBS) -lc
	$(POST_PROCESS_SO)
