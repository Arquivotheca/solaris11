#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# closed/cmd/fwflash/plugins
#

CLOSED_SUBDIRS= $(MACH)

SUBDIRS=	$(CLOSED_SUBDIRS)


all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint
_msg :=		TARGET= _msg
msg :=		TARGET= msg


all lint install clean clobber msg _msg: $(SUBDIRS)

include $(SRC)/cmd/fwflash/Makefile.com

$(SUBDIRS): FRC
	@if [ -f $@/Makefile ]; then \
		cd $@; pwd; $(MAKE) $(TARGET); \
	else \
		true; \
	fi

FRC:



