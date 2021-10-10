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

.KEEP_STATE:
.SUFFIXES:

include ../../../../Makefile.cmd
SCHEME :sh= cd ..; basename `pwd`

#
# For now, we assume a scheme is named after its directory and its source file
# is named 'scheme.c'.  If schemes get more complicated this can be enhanced
# to derive the values from the lower-level Makefiles.
#
PROG = $(SCHEME:%=%.so)
SRCS = scheme.c
OBJS = $(SRCS:%.c=%.o)

ROOTPROG = $(ROOT)/usr/lib/fm/fmd/schemes/$(PROG)
ROOTPROG64 = $(ROOT)/usr/lib/fm/fmd/schemes/$(MACH64)/$(PROG)
MAPFILE = ../../../fmd/common/fmd_fmri.map

LINTFLAGS += -u
LINTFLAGS64 += -u
LINTFILES = $(SRCS:%.c=%.ln)

CFLAGS += $(CTF_FLAGS) $(CCVERBOSE) $(CC_PICFLAGS)
CFLAGS += -G $(XREGSFLAG)
CFLAGS64 += $(CTF_FLAGS) $(CCVERBOSE) $(CC_PICFLAGS)
CFLAGS64 += -G $(XREGSFLAG)
CPPFLAGS += -D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT -D$(MACH) -I..
LDFLAGS += $(ZTEXT) $(ZDEFS) $(ZIGNORE) -M$(MAPFILE)
LDLIBS += -lnvpair -lc
