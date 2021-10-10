#
# Copyright (c) 1997, 2010, Oracle and/or its affiliates. All rights reserved.
#
# cmd/iconv/Makefile

include $(SRC)/cmd/Makefile.cmd

SUBDIRS=	$(MACH)
$(BUILD64)SUBDIRS +=	$(MACH64)

all	:= TARGET = all
install	:= TARGET = install
clean	:= TARGET = clean
clobber	:= TARGET = clobber
lint	:= TARGET = lint
_msg	:= TARGET = catalog

.KEEP_STATE:

#.PARALLEL: $(SUBDIRS)

all install: $(SUBDIRS)

_msg:	$(SUBDIRS)

clean:	$(SUBDIRS)

clobber:	$(SUBDIRS)

lint:	$(SUBDIRS)

$(SUBDIRS):	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
