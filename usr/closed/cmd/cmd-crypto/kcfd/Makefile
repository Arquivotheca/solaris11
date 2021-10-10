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

# Needed for ROOTFS_LIBDIR definition
include		$(SRC)/lib/Makefile.lib

PROG = kcfd

OBJS_CLOSED =	modverify.o
OBJS =		kcfd.o $(OBJS_CLOSED)

SRCS =		$(OBJS:.o=.c)
OBSCURE =	obscurity_check

include		$(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include 	$(CLOSED)/Makefile.tonic

ROOTCMDDIR=            $(ROOTFS_LIBDIR)/crypto

CPPFLAGS +=	-D_REENTRANT

#
# libkcfd.so.1 is an interposer whose sole task is to make sure
# kcfd uses pkcs11_softtoken.so.1 during verification to
# ensure only modules within the FIPS-140 boundary are used
# for validation and to avoid an infinite loop that would
# happen if it used libpkcs11.so.1
#
LDLIBS +=	-lmd -lcryptoutil -lelfsign -lkcfd

POFILES =	$(OBJS:%.o=%.po)
POFILE =	${PROG}_cmd.po

TEE =		/usr/bin/tee
TEST =		/usr/bin/test

.KEEP_STATE:

all:    $(PROG)

$(PROG): $(OBSCURE) $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

#
# Verify local symbols from kcfd are obscured
#	allow kcfd_modverify() and kcfd_modverify_exit() entry points
#	allow *stat(), mknod() for i386 implementation
#	allow FIPS 140 boundary files as they are not secret.
#
$(OBSCURE):	$(OBJS_CLOSED)
	$(RM) $@ $@.list
	$(NM) -p $(OBJS_CLOSED) | \
		$(GREP) " [DTdt] " | \
		$(SED) -e 's/^.* [DTdt] //' -e '/_C01A7/d' \
			-e '/^Ttdata.data$$/d' \
			-e '/^kcfd_modverify$$/d' \
			-e '/^kcfd_modverify_exit$$/d' \
			-e '/^fips140_verify_boundary/d' \
			-e '/^kcf_fips140_files/d' \
			-e '/^kcf_psr_fips140_files/d' \
			-e '/^userland_fips140_files/d' \
			-e '/^common_fips140_files/d' \
			-e '/^common_psr_fips140_files/d' \
			-e '/^fstat$$/d' \
			-e '/^lstat$$/d' \
			-e '/^mknod$$/d' \
			-e '/^stat$$/d' | \
		$(SED) -e '/_C01A7/d' -e 's/^.* [DTdt] //' | \
		$(TEE) $@.list
	$(TEST) ! -s $@.list && $(TOUCH) $@

$(POFILE): ${POFILES}
	$(RM) $@; cat $(POFILES) > $@

install: all $(ROOTCMD)

clean:
	$(RM) -f $(OBJS) $(OBSCURE)* $(PROG)

lint:   lint_SRCS

include $(SRC)/cmd/Makefile.targ
