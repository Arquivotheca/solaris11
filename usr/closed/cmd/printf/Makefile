#
# Copyright (c) 1990, 2010, Oracle and/or its affiliates. All rights reserved.
#
#

PROG= printf

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

LDLIBS += -lgen

CFLAGS += $(CCVERBOSE)

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include $(SRC)/cmd/Makefile.targ
