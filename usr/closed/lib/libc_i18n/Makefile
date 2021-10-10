#
# Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
#
#
# lib/libc_i18n/Makefile

include $(SRC)/Makefile.master
include $(SRC)/lib/Makefile.lib
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

SUBDIRS=	$(MACH)
$(BUILD64)SUBDIRS += $(MACH64)


# conditional assignments
all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint
test :=		TARGET= test

SYSHDRS=	sys/localedef.h sys/lc_core.h
CHECKHDRS=	$(SYSHDRS:sys/%.h=inc/%.check)
HDRS=		$(SYSHDRS)

# install rule for install_h target
$(ROOTHDRDIR)/sys/%:	inc/%
	$(INS.file)

.KEEP_STATE:

all install clean clobber test: $(SUBDIRS)

_msg:

install_h:	$(ROOTHDRS)

stub stubinstall:

check:		$(CHECKHDRS)


.PARALLEL:	$(SUBDIRS)

lint:	$(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
