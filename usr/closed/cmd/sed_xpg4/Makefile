#
#
# Copyright (c) 1995, 2010, Oracle and/or its affiliates. All rights reserved.
#

PROG= sed
XPG4PROG= sed

OBJS= sed0.o sed1.o
SRCS= $(OBJS:%.o=%.c)

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

CFLAGS += $(CCVERBOSE)
CPPFLAGS += -D_FILE_OFFSET_BITS=64

# for messaging catalog
POFILE= sed_xpg4.po
POFILES= $(OBJS:%.o=%.po)
XGETFLAGS += -a -x sed.xcl


.KEEP_STATE:

all: $(XPG4) 

$(XPG4): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(POFILE): $(POFILES)
	$(RM) $@
	cat $(POFILES) > $@



install: all $(ROOTXPG4PROG)

clean:
	$(RM) $(OBJS)

lint: lint_SRCS

include $(SRC)/cmd/Makefile.targ
