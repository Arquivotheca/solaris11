#
# Copyright (c) 1998, 2010, Oracle and/or its affiliates. All rights reserved.
#
# cmd/llc2/etc/Makefile
#

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

ETCFILES = llc2_start.default
PROG = llc2
ROOTINIT_D = $(ROOTETC)/init.d
ROOTRC0_D = $(ROOTETC)/rc0.d
ROOTRC1_D = $(ROOTETC)/rc1.d
ROOTRC2_D = $(ROOTETC)/rc2.d
ROOTRCS_D = $(ROOTETC)/rcS.d

FILEMODE = 0744

ROOTETCDIR	=	$(ROOTETC)/llc2
ROOTETCDEFDIR	= 	$(ROOTETC)/llc2/default
ROOTETCFILES	= 	$(ETCFILES:%=$(ROOTETC)/llc2/%)
ROOTETCRCDIRS	=	$(ROOTINIT_D) $(ROOTRC0_D) $(ROOTRC1_D) \
	$(ROOTRC2_D) $(ROOTRCS_D)

$(ROOTETCDIR):
	$(INS.dir)

$(ROOTETCDEFDIR):
	$(INS.dir)

$(ROOTETCRCDIRS):
	$(INS.dir)

$(ROOTETC)/llc2/%: %
	$(INS.file)

ROOTINIT_DPROG = $(ROOTINIT_D)/$(PROG)

ROOTLNKPROGS = 	$(ROOTRC0_D)/K52llc2 \
		$(ROOTRC1_D)/K52llc2 \
		$(ROOTRCS_D)/K52llc2 \
		$(ROOTRC2_D)/S40llc2

$(ROOTLNKPROGS): $(ROOTETCDEFDIR) $(ROOTETCRCDIRS)

.KEEP_STATE:

all:	$(PROG) $(ETCFILES)

installdirs: $(ROOTETCDIR) $(ROOTETCDEFDIR) $(ROOTETCRCDIRS)
installfiles: $(ROOTETCFILES) $(ROOTINIT_DPROG) $(ROOTLNKPROGS)
install: installdirs installfiles

$(ROOTINIT_DPROG): $(PROG)
	$(INS.file) $(PROG)

lint:
	@echo "nothing to lint"

clean:

clobber: 

$(ROOTRC0_D)/%:	$(ROOTINIT_DPROG)
	$(INS.link)

$(ROOTRC1_D)/%: $(ROOTINIT_DPROG)
	$(INS.link)

$(ROOTRCS_D)/%: $(ROOTINIT_DPROG)
	$(INS.link)

$(ROOTRC2_D)/%: $(ROOTINIT_DPROG)
	$(INS.link)
