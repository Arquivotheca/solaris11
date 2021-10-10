#
# Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
#
# lib/krb5/kadm5/Makefile
#
# include global definitions
include ../../../Makefile.master

#
# Build everything in parallel; use .WAIT for dependencies
.PARALLEL: $(SUBDIRS)

SUBDIRS= clnt srv

all :=		TARGET= all
clean :=	TARGET= clean
clobber :=	TARGET= clobber
install :=	TARGET= install
lint :=		TARGET= lint
_msg :=		TARGET= _msg
stub :=		TARGET = stub
stubinstall :=	TARGET = stubinstall

.KEEP_STATE:

all clean clobber install lint _msg stub stubinstall: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
