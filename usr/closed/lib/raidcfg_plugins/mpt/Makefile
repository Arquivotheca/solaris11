#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved. 
# Use is subject to license terms.
#
#pragma ident	"%Z%%M%	%I%	%E% SMI"
#
# lib/raidcfg_plugins/mpt/Makefile

include		$(SRC)/lib/Makefile.lib

SUBDIRS =	$(MACH)
$(BUILD64)SUBDIRS +=	$(MACH64)

all :=		TARGET= all
clean :=	TARGET= clean
clobber :=	TARGET= clobber
install :=	TARGET= install
lint :=		TARGET= lint
_msg :=		TARGET= _msg

POFILE=		mpt.po
MSGSRCS :sh=	echo */*.c
MSGFILES =	$(MSGSRCS:%.c=%.i)
CPPFLAGS +=     -I$(SRC)/uts/common -I$(CLOSED)/uts/common/sys/scsi/adapters

.KEEP_STATE:

all clean clobber install lint: $(SUBDIRS)

$(SUBDIRS):	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

_msg:	$(MSGDOMAINPOFILE)

$(POFILE):	$(MSGFILES)
		$(BUILDPO.msgfiles)

FRC:

include $(SRC)/Makefile.msg.targ
