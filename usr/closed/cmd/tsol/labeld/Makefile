#
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#
# cmd/tsol/labeld/Makefile
#

PROG= labeld
GFI= gfi
DEA_OBJS= btosd.o ltosd.o main.o miscd.o privated.o \
	stobd.o stold.o setflabel.o utils.o 
GFI_OBJS= l_error.o l_init.o std_labels.o l_eof.o
OBJS= $(DEA_OBJS) $(GFI_OBJS)
SRCS= $(DEA_OBJS:%.o=%.c) $(GFI_OBJS:%.o=$(GFI)/%.c)
HDRS= labeld.h impl.h $(GFI)/std_labels.h
POFILES= $(SRCS:%.c=%.po)

LABEL_ENCODINGS= label_encodings

ENCODINGS= \
	label_encodings.gfi.single \
	label_encodings.example \
	label_encodings.gfi.multi \
	label_encodings.multi \
	label_encodings.single

ETCTSOLENCODINGS= $(ENCODINGS:%=$(ROOTETCTSOL)/%)
ENCODINGSSRC= $(ENCODINGS:%=$(GFI)/%)

ETCTSOLFILES=$(LABEL_ENCODINGS) $(ENCODINGS)

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

all:=		TARGET= all
clean:=		TARGET= clean
clobber:=	TARGET= clobber
install:=	TARGET= install
lint:=		TARGET= lint
_msg:=		TARGET= _msg

$(ROOTETCTSOL)/$(LABEL_ENCODINGS) := FILEMODE= 0400
$(ETCTSOLENCODINGS) := FILEMODE= 0444

IFLAGS += -I. -I$(SRC)/lib/libtsol/common
DFLAGS += -D_REENTRANT -DTSOL -D_FILE_OFFSET_BITS=64
CPPFLAGS += $(IFLAGS) $(DFLAGS)
LDLIBS += -ltsol -lbsm

# XXX RAMPART carlsonj@east: work around broken auditing interfaces
LINTFLAGS +=	-erroff=E_NAME_USED_NOT_DEF2

.KEEP_STATE:

.PARALLEL: $(OBJS)

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

%.o:	$(GFI)/%.c
	$(COMPILE.c) $(GFI)/$(@:.o=.c)

install: $(PROG) $(ROOTLIBPROG) $(ROOTETCTSOLFILES)

$(LABEL_ENCODINGS) : $(GFI)/label_encodings.simple
	-$(RM) $@
	$(CP) $(GFI)/label_encodings.simple $@
$(ENCODINGS): $(ENCODINGSSRC)
	-$(RM) $@
	$(CP) $(GFI)/$@ $@

$(POFILE): $(POFILES)
	$(RM) -f $@
	$(CAT) $(POFILES) > $@

clean:
	$(RM) $(OBJS) $(LABEL_ENCODINGS) $(ENCODINGS)

lint:	lint_SRCS

_msg:

include $(SRC)/cmd/Makefile.targ

FRC:
