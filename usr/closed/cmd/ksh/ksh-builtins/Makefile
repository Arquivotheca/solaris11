#
# Copyright (c) 1995, 2010, Oracle and/or its affiliates. All rights reserved.
#

XPG4PROG= alias
LINKS= bg cd command fc fg getopts hash jobs kill read test type ulimit umask unalias wait
ROOTXPG4LINKS= $(LINKS:%=$(ROOTXPG4BIN)/%)

SRCS= $(XPG4:%=%.sh)

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

.KEEP_STATE:

$(ROOTXPG4BIN)/%: $(ROOTXPG4PROG)
	$(INS.link)

all: $(XPG4)

install: all $(ROOTXPG4PROG) $(ROOTXPG4LINKS)

clean:

lint:

include $(SRC)/cmd/Makefile.targ
