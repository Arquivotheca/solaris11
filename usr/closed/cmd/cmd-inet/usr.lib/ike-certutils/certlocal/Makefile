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
# cmd/cmd-inet/ike-certutils/certlocal/Makefile

PROG=	certlocal
OBJ1=	certlocal.o abstime.o
OBJ2=	../dumputils.o
OBJS=	$(OBJ1) $(OBJ2)
SRCS=	$(OBJ1:%.o=%.c) 
LINTS=	$(OBJ1:%.o=%.ln)

include		$(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include		$(CLOSED)/Makefile.tonic

LDLIBS +=	-like -lnsl -lmd5 -lm
CPPFLAGS +=	-I..

.KEEP_STATE:

.PARALLEL:	$(OBJ1)

all:	$(PROG)

$(PROG):	$(OBJ1)
# CRYPT DELETE START
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

nop:
	# Dummy target for when I create CRYPT source.
# CRYPT DELETE END
	$(ECHO) "#!/bin/false" > ./certlocal
	$(CHMOD) 0755 ./certlocal

include  $(SRC)/cmd/cmd-inet/usr.lib/Makefile.lib

install:	all .WAIT $(ROOTLIBINETPROG)

clean:
	$(RM) $(OBJ1) $(CLEANFILES)

lint:	$(LINTS)
	$(LINT.c) $(OBJS:%.o=%.ln) $(LDLIBS)

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

include $(SRC)/cmd/Makefile.targ
