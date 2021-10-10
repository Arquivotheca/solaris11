#
# Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
#

include ../Makefile.lib

HDRS =		tcpd.h
CHECKHDRS =
HDRDIR =	.
SUBDIRS	=	$(MACH)

all := 		TARGET = all
clean :=	TARGET = clean
clobber :=	TARGET = clobber
install	:=	TARGET = install
lint :=		TARGET = lint
stub :=		TARGET = stub
stubinstall :=	TARGET = stubinstall

.KEEP_STATE:

all clean clobber install lint stub stubinstall: $(SUBDIRS)

all install: THIRDPARTYLICENSE

install_h:	$(ROOTHDRS)

check:		$(CHECKHDRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

THIRDPARTYLICENSE: DISCLAIMER
	$(GREP) -v '\*\*\*\*' DISCLAIMER > $@

CLOBBERFILES += THIRDPARTYLICENSE

include ../Makefile.targ
