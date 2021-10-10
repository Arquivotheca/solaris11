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
# Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
#

#
#	This makefile drives the production of the sun4u ents platform
#	module.
#
#	sun4u implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSCLOSED = ../..
UTSBASE	  = ../../../../src/uts

#
#	Include common rules.
#
include $(UTSCLOSED)/sun4u/ents/Makefile.ents

def		:=	TARGET= def
all		:=	TARGET= all
install		:=	TARGET= install
install_h	:=	TARGET= install_h
clean		:=	TARGET= clean
clobber		:=	TARGET= clobber
lint		:=	TARGET= lint
lintlib		:=	TARGET= lintlib
modlintlib	:=	TARGET= modlintlib
modlist		:=	TARGET= modlist
modlist		:=	NO_STATE= -K $$MODSTATE$$$$
clean.lint	:=	TARGET= clean.lint
check		:=	TARGET= check

#
#	Default build targets.
#
.KEEP_STATE:

def all clean clean.lint clobber modlist: $(ENTS_KMODS)

lintlib:	unix

modlintlib:	$(ENTS_KMODS)

IMPLEMENTED_PLATFORM	= SUNW,Sun-Fire-V250
LINKED_PLATFORMS	=

install:	$(ROOT_ENTS_DIR) $(USR_ENTS_DIR) \
		$(USR_ENTS_INC_DIR) \
		$(USR_ENTS_SBIN_DIR) \
		$(USR_ENTS_SBIN_PRTDIAG) \
		$(USR_ENTS_SBIN_FRUADM) \
		$(USR_ENTS_LIB_DIR) \
		$(LINKED_PLATFORMS:%=$(USR_PLAT_DIR)/%) \
		$(LINKED_PLATFORMS:%=$(ROOT_PLAT_DIR)/%) \
		$(ENTS_CRYPTO_LINKS) \
		.WAIT $(ENTS_KMODS)

$(ENTS_KMODS): FRC
	@cd $@; pwd; $(MAKE) $(NO_STATE) $(TARGET)

$(ENTS_CRYPTO_LINKS): $(ROOT_ENTS_CRYPTO_DIR_64)
	-$(RM) $(ROOT_ENTS_CRYPTO_DIR_64)/$@;
	$(SYMLINK) $(ROOT_US3_CRYPTO_LINK)/$@ $(ROOT_ENTS_CRYPTO_DIR_64)/$@
	if [ -z "$(TONICBUILD)" ]; then \
		$(RM) \
		$(ROOT_ENTS_CRYPTO_DIR_64:$(ROOT)/%=$(CLOSEDROOT)/%)/$(@F) ; \
		$(SYMLINK) \
		$(ROOT_US3_CRYPTO_LINK:$(ROOT)/%=$(CLOSEDROOT)/%)/$(@F) \
		$(ROOT_ENTS_CRYPTO_DIR_64:$(ROOT)/%=$(CLOSEDROOT)/%)/$(@F) ; \
	fi

install_h check:	FRC


lint:		modlintlib

LINT_LIBS	 = $(LINT_LIB) \
		   -L$(ENTS_LINT_LIB_DIR) \
		   -L$(LINT_LIB_DIR) $(LINT_KMODS:%=-l%) \
		       $(CLOSED_LINT_KMODS:%=-l%) \
		   -L$(SPARC_LIB_DIR) $(SPARC_LINTS:%=-l%)

lint.platmod:	modlintlib
	@-$(ECHO) "\nENTS Platform-dependent module: global crosschecks:"
	@-$(LINT) $(LINTFLAGS) $(LINT_LIBS) 2>&1 | $(LGREP.2)

#
#	Include common targets.
#
include $(UTSCLOSED)/sun4u/ents/Makefile.targ
