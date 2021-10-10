#
# Copyright (c) 1990, 2010, Oracle and/or its affiliates. All rights reserved.
#

PROG= od
XPG4PROG= od

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

.KEEP_STATE:

CFLAGS += $(CCVERBOSE) -D_FILE_OFFSET_BITS=64
$(XPG4):= CPPFLAGS += -DXPG4 -D_FILE_OFFSET_BITS=64
XGETFLAGS += -a -x od.xcl

all: $(PROG) $(XPG4)

install: all $(ROOTPROG) $(ROOTXPG4PROG)

clean:

lint:	lint_PROG

include $(SRC)/cmd/Makefile.targ
