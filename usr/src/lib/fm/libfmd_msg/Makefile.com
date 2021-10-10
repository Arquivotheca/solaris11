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
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#

LIBRARY = libfmd_msg.a
VERS = .1

LIBSRCS = fmd_msg.c
OBJECTS = $(LIBSRCS:%.c=%.o)

include ../../../Makefile.lib
include ../../Makefile.lib

SRCS = $(LIBSRCS:%.c=../common/%.c)
LIBS = $(DYNLIB) $(LINTLIB)

SRCDIR =	../common

CPPFLAGS += -I../common -I.
CFLAGS += $(CCVERBOSE) $(C_BIGPICFLAGS)
CFLAGS64 += $(CCVERBOSE) $(C_BIGPICFLAGS)
LDLIBS += -lnvpair -lc

LINTFLAGS = -msux
LINTFLAGS64 = -msux -m64

CLOBBERFILES += fmd_msg_test fmd_msg_test.core fmd_msg_test.out

$(LINTLIB) := SRCS = $(SRCDIR)/$(LINTSRC)
$(LINTLIB) := LINTFLAGS = -nsvx
$(LINTLIB) := LINTFLAGS64 = -nsvx -m64

.KEEP_STATE:

all: stub $(LIBS)

lint: $(LINTLIB) lintcheck

include ../../../Makefile.targ
include ../../Makefile.targ

LDLIBS_$(MACH) =	-L$(LROOT)/usr/lib/fm \
			-R$(ROOT)/usr/lib/fm

LDLIBS_$(MACH64) =	-L$(LROOT)/usr/lib/fm/$(MACH64) \
			-R$(ROOT)/usr/lib/fm/$(MACH64)

#
# To ease the development and maintenance of libfmd_msg, a test suite is built
# directly into the library.  The test program fmd_msg_test.c includes a set of
# tests for all the code paths in the library.  The test program executes the
# calls, and then forks into the background and dumps core.  After the test
# runs, we diff the output against the master hand-verified output, and confirm
# no leaks or corruption exist.  To run the entire suite, type "make test" and
# inspect the results (the make target will fail on an error).
#
# The cmp skips the first 900 bytes of $(SRCDIR)/fmd_msg_test.out to get us
# passed the CDDL header and copyright in that file.
#
test: install fmd_msg_test
	@echo; echo "Running `pwd | sed 's/.*\///'` fmd_msg test suite ... \c"
	@coreadm -p core $$$$
	@UMEM_DEBUG=default,verbose UMEM_LOGGING=fail,contents LANG=C \
	    LC_ALL=C ./fmd_msg_test | grep -v EVENT-TIME > fmd_msg_test.out
	@chmod 0444 core; mv -f core fmd_msg_test.core
	@echo; echo "Checking test output ... \c"
	@cmp -s $(SRCDIR)/fmd_msg_test.out fmd_msg_test.out 900 0 && echo pass \
	    || ( echo FAIL && diff $(SRCDIR)/fmd_msg_test.out fmd_msg_test.out )
	@echo; echo Checking for memory leaks:
	@echo ::findleaks | mdb fmd_msg_test.core
	@echo; echo Checking for latent corruption:
	@echo ::umem_verify | mdb fmd_msg_test.core | grep -v clean
	@echo

fmd_msg_test: $(SRCDIR)/fmd_msg_test.c
	$(LINT.c) $(SRCDIR)/fmd_msg_test.c
	$(LINK.c) -o fmd_msg_test $(SRCDIR)/fmd_msg_test.c \
	    $(LDLIBS_$(TARGETMACH)) -lfmd_msg -lnvpair -lumem
