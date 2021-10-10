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

# Common files for the IKE certificate utilities are compiled and linted up
# here, with each subdir containing utility-specific sources.

# Dummy holders for EXPORT/CRYPT builds
OBJS=
SUBDIRS=
# EXPORT DELETE START
# CRYPT DELETE START
OBJS=	dumputils.o
# CRYPT DELETE END
SUBDIRS=	certdb certlocal certrldb
# EXPORT DELETE END
LINTS=	$(OBJS:%.o=%.ln)

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

all :=		TARGET= all
clean :=	TARGET= clean
clobber :=	TARGET= clobber
install :=	TARGET= install
lint :=		TARGET= lint
EXPORT_SRC :=	TARGET= EXPORT_SRC
CRYPT_SRC :=	TARGET= CRYPT_SRC

.KEEP_STATE:

all install:	$(OBJS) .WAIT $(SUBDIRS)
# CRYPT DELETE START

nop:
	# Dummy target for when I create CRYPT source.
# CRYPT DELETE END
	$(RM) $(ROOT)/usr/lib/inet/certdb
	$(ECHO) "#!/bin/false" > $(ROOT)/usr/lib/inet/certdb
	$(CHMOD) 0755 $(ROOT)/usr/lib/inet/certdb
	$(RM) $(ROOT)/usr/lib/inet/certlocal
	$(ECHO) "#!/bin/false" > $(ROOT)/usr/lib/inet/certlocal
	$(CHMOD) 0755 $(ROOT)/usr/lib/inet/certlocal
	$(RM) $(ROOT)/usr/lib/inet/certrldb
	$(ECHO) "#!/bin/false" > $(ROOT)/usr/lib/inet/certrldb
	$(CHMOD) 0755 $(ROOT)/usr/lib/inet/certrldb


clean clobber:	$(SUBDIRS)
	$(RM) $(OBJS) $(LINTS)

lint:		$(LINTS) $(SUBDIRS)

.PARALLEL:	$(OBJS) $(LINTS)

$(SUBDIRS):	FRC
	@cd $@; pwd ; $(MAKE) $(TARGET)

# EXPORT DELETE START
EXPORT_SRC:	CRYPT_SRC .WAIT $(SUBDIRS)
	sed -e "/^# EXPORT DELETE START/,/^# EXPORT DELETE END/d" \
		< Makefile > Makefile+
	$(MV) Makefile+ Makefile

CRYPT_SRC:	$(SUBDIRS)
# CRYPT DELETE START
	sed -e "/^# CRYPT DELETE START/,/^# CRYPT DELETE END/d" < Makefile | \
	    sed -e 's/(PROG):	.*/(PROG):/g' > Makefile+
	$(MV) Makefile+ Makefile
# CRYPT DELETE END

# EXPORT DELETE END

FRC:
