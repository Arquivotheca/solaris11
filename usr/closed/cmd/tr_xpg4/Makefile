#
#
# Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
#

PROG= tr
XPG4PROG= tr
XPG6PROG= tr

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

CFLAGS += $(CCVERBOSE)

$(XPG4) := CFLAGS += -DXPG4
$(XPG6) := CFLAGS += -DXPG6

POFILE= tr.po
POFILES_BIN= tr_bin.po
POFILES_XPG4= tr_xpg4.po
POFILES_XPG6= tr_xpg6.po
$(POFILES_XPG6) := CFLAGS += -DXPG6
$(POFILES_XPG4) := CFLAGS += -DXPG4
CLOBBERFILES += $(POFILES_BIN) $(POFILES_XPG4) $(POFILES_XPG6)
LINTFLAGS += -u

.KEEP_STATE:

all: $(PROG) $(XPG4) $(XPG6)

install: all $(ROOTPROG) $(ROOTXPG4PROG) $(ROOTXPG6PROG)

$(POFILE):	$(POFILES_BIN) $(POFILES_XPG4) $(POFILES_XPG6)
	$(RM) $@; cat $(POFILES_BIN) $(POFILES_XPG6) $(POFILES_XPG6) > $@

%_bin.po %_xpg4.po %_xpg6.po:	%.c
	$(COMPILE.cpp) $< > $<.i
	$(BUILD.po)

clean:

lint:	lint_PROG lint_xpg4 lint_xpg6

lint_xpg4:	$(PROG).c
	$(LINT.c) -DXPG4 $(PROG).c $(LDLIBS)

lint_xpg6:	$(PROG).c
	$(LINT.c) -DXPG6 $(PROG).c $(LDLIBS)

include $(SRC)/cmd/Makefile.targ
