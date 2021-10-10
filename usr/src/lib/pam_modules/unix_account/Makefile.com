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
# Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#
# usr/src/lib/pam_modules/unix_account/Makefile.com
#

LIBRARY=	pam_unix_account.a
VERS=		.1
OBJECTS=	unix_acct.o

include		../../Makefile.pam_modules

LDLIBS		+= -lpam -lc
# Enable large file support for reading the lastlog file.
CPPFLAGS	+= -D_FILE_OFFSET_BITS=64

all:	$(LIBS)

lint:	lintcheck

include	$(SRC)/lib/Makefile.targ
