#
# Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG=	patch
XPG4PROG= $(PROG)

SRCS1=	patch.c pch.c util.c file.c
SRCS=	$(SRCS1) $(SRC)/common/util/getresponse.c

OBJS1=	$(SRCS1:%.c=%.o)	
OBJS=	$(OBJS1) getresponse.o

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

CPPFLAGS += -I $(SRC)/common/util
CFLAGS += $(CCVERBOSE)
LINTFLAGS += -u

.KEEP_STATE:

.PARALLEL: $(OBJS)

all: $(XPG4)

%.o: $(SRC)/common/util/%.c
	$(COMPILE.c) $(OUTPUT_OPTION) $<
	$(POST_PROCESS_O)

POFILES=	$(OBJS1:%.o=%.po)
POFILE=		$(PROG)_cmd.po

XGETFLAGS += -a -x patch.xcl

$(POFILE): $(POFILES)
	$(RM) $@
	cat $(POFILES) > $@

install: all $(ROOTXPG4PROG)
	cd $(ROOTSUNOSBIN); \
	$(RM) $(PROG); \
	$(SYMLINK) ../../xpg4/bin/$(PROG) $(PROG)

lint:	lint_SRCS

clean:
	$(RM) $(OBJS)

$(XPG4):	$(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

include		$(SRC)/cmd/Makefile.targ
