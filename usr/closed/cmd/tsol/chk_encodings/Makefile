#
# Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
#

#
# cmd/tsol/chk_encodings
#
PROG=	chk_encodings

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

LABELD= ../labeld
GFI= $(LABELD)/gfi
PROG_OBJS= chk_encodings.o
GFI_OBJS= l_init.o std_labels.o l_eof.o
OBJS= $(PROG_OBJS) $(GFI_OBJS)
SRCS= $(PROG_OBJS:%.o=%.c) $(GFI_OBJS:%.o=$(GFI)/%.c)
HDRS= $(GFI)/std_labels.h

LDLIBS += -ltsol
IFLAGS += -I$(LABELD) -I$(SRC)/lib/libtsol/common
DFLAGS += -DTSOL
CPPFLAGS += $(IFLAGS) $(DFLAGS)

.KEEP_STATE:

.PARALLEL: $(OBJS)

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

%.o:    $(GFI)/%.c
	$(COMPILE.c) $(GFI)/$(@:.o=.c)

install: all $(ROOTUSRSBINPROG)

clean:
	$(RM) $(OBJS)

lint:

include $(SRC)/cmd/Makefile.targ
