#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
#

PROG= in.iked

include $(SRC)/cmd/Makefile.cmd

SUBDIRS += $(MACH)
$(BUILD64)SUBDIRS += $(MACH64)

MANIFEST=ike.xml
ROOTMANIFESTDIR=	$(ROOTSVCNETWORKIPSEC)
$(ROOTMANIFEST)		:= FILEMODE = 444

YFLAGS += -d
CLEANFILES += gram.c lex.c y.tab.h

all	:=	TARGET = all
install	:=	TARGET = install
clean	:=	TARGET = clean
clobber	:=	TARGET = clobber
lint	:=	TARGET = lint

.KEEP_STATE:

all:	gram.c lex.c .WAIT $(SUBDIRS)

lint:	gram.c lex.c .WAIT $(SUBDIRS)

gram.c + y.tab.h:	gram.y

lex.c:	lex.l

clean:	$(SUBDIRS)
	$(RM) $(CLEANFILES)

clobber: $(SUBDIRS)

include $(SRC)/cmd/cmd-inet/usr.lib/Makefile.lib
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

$(ROOTMANIFESTDIR)/%: %
	$(INS.file)

install:	gram.c lex.c .WAIT $(SUBDIRS) $(ROOTMANIFEST)
# EXPORT DELETE START
# CRYPT DELETE START

nop2:
# CRYPT DELETE END
# EXPORT DELETE END
	$(CP) ./in.iked $(ROOTLIBINETPROG32)
	$(CP) ./in.iked $(ROOTLIBINETPROG64)
	$(TONICBUILD)$(CP) ./in.iked $(ROOTLIBINETPROG32:$(ROOT)/%=$(CLOSEDROOT)/%)
	$(TONICBUILD)$(CP) ./in.iked $(ROOTLIBINETPROG64:$(ROOT)/%=$(CLOSEDROOT)/%)

check:	$(CHKMANIFEST)

# EXPORT DELETE START
EXPORT_SRC:	CRYPT_SRC
	sed -e "/^# EXPORT DELETE START/,/^# EXPORT DELETE END/d" \
		< Makefile > Makefile+
	$(MV) Makefile+ Makefile

CRYPT_SRC:
# CRYPT DELETE START
	sed -e "/^# CRYPT DELETE START/,/^# CRYPT DELETE END/d" < Makefile | \
	    sed -e 's/(PROG):	.*/(PROG):/g' > Makefile+
	$(MV) Makefile+ Makefile
# CRYPT DELETE END

# EXPORT DELETE END

$(SUBDIRS):	FRC
# EXPORT DELETE START
# CRYPT DELETE START
	@cd $@; pwd; $(MAKE) $(TARGET)

nop:
	# Dummy target for when I create CRYPT source.
# CRYPT DELETE END
# EXPORT DELETE END
	$(ECHO) "#!/bin/sh" > ./in.iked
	$(ECHO) "echo Not running due to export control." >> ./in.iked
	$(ECHO) "exit 1" >> ./in.iked
	$(CHMOD) 0755 ./in.iked

FRC:

include $(SRC)/cmd/Makefile.targ
