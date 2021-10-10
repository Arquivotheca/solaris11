#
# Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
#
# lib/krb5/Makefile
#
# include global definitions
include ../../Makefile.master

#
# Build everything in parallel; use .WAIT for dependencies
.PARALLEL:

SUBDIRS= \
	dyn	\
	.WAIT	\
	kdb	\
	.WAIT	\
	kadm5	\
	.WAIT	\
	ss	\
	plugins

C99MODE=	-xc99=%all
C99LMODE=	-Xc99=%all

all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint
_msg :=		TARGET= _msg
stub :=		TARGET = stub
stubinstall :=	TARGET = stubinstall

.KEEP_STATE:

all install clean clobber lint _msg stub stubinstall: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
