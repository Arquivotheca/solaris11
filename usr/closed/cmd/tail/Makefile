#
#
# Copyright (c) 1990, 2010, Oracle and/or its affiliates. All rights reserved.
# All rights reserved.
#

PROG= tail
XPG4PROG= tail

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

$(XPG4) := CFLAGS += -DXPG4
CPPFLAGS += -D_FILE_OFFSET_BITS=64
C99MODE= $(C99_ENABLE)

.KEEP_STATE:

all: $(PROG) $(XPG4)

install: all $(ROOTPROG) $(ROOTXPG4PROG)

clean:

lint:	lint_PROG

include $(SRC)/cmd/Makefile.targ
