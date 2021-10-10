#
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#

include		$(SRC)/lib/Makefile.lib
$(TONICBUILD)include $(CLOSED)/Makefile.tonic


SUBDIRS=	$(MACH)
$(BUILD64)SUBDIRS += $(MACH64)

all :=		TARGET= all
check :=	TARGET= check
clean :=	TARGET= clean
clobber :=	TARGET= clobber
install :=	TARGET= install
lint :=		TARGET= lint
stub :=		TARGET= stub
stubinstall :=	TARGET= stubinstall

.KEEP_STATE:

all clean clobber install lint stub stubinstall: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
