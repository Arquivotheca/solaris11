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
# uts/sun4u/chalupa/Makefile
#
# Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
#

#
#	This makefile drives the production of the sun4u chalupa platform
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
include $(UTSCLOSED)/sun4u/chalupa/Makefile.chalupa

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

def all clean clean.lint clobber modlist: $(CHALUPA_KMODS)

lintlib:	unix

modlintlib:	$(CHALUPA_KMODS)

IMPLEMENTED_PLATFORM	= SUNW,Sun-Fire-V440

LINKED_PLATFORMS	= SUNW,Netra-440

install:	$(ROOT_CHALUPA_DIR) $(USR_CHALUPA_DIR) \
		$(USR_CHALUPA_INC_DIR) \
		$(USR_CHALUPA_SBIN_DIR) \
		$(USR_CHALUPA_LIB_DIR) \
		$(LINKED_PLATFORMS:%=$(USR_PLAT_DIR)/%) \
		$(LINKED_PLATFORMS:%=$(ROOT_PLAT_DIR)/%) \
		$(CHALUPA_CRYPTO_LINKS) \
		.WAIT $(CHALUPA_KMODS) \
		tsalarm tsalarm.conf \
		ntwdt ntwdt.conf

$(CHALUPA_KMODS): FRC
	@cd $@; pwd; $(MAKE) $(NO_STATE) $(TARGET)

$(CHALUPA_CRYPTO_LINKS): $(ROOT_CHALUPA_CRYPTO_DIR_64)
	-$(RM) $(ROOT_CHALUPA_CRYPTO_DIR_64)/$@;
	$(SYMLINK) $(ROOT_US3_CRYPTO_LINK)/$@ $(ROOT_CHALUPA_CRYPTO_DIR_64)/$@
	if [ -z "$(TONICBUILD)" ]; then \
		$(RM) \
		$(ROOT_CHALUPA_CRYPTO_DIR_64:$(ROOT)/%=$(CLOSEDROOT)/%)/$@ ; \
		$(SYMLINK) \
		$(ROOT_US3_CRYPTO_LINK:$(ROOT)/%=$(CLOSEDROOT)/%)/$@ \
		$(ROOT_CHALUPA_CRYPTO_DIR_64:$(ROOT)/%=$(CLOSEDROOT)/%)/$@ ; \
	fi


tsalarm:	$(ROOT_CHALUPA_DRV_DIR_64)
	-@$(RM) $(ROOT_CHALUPA_DRV_DIR_64)/$@
	$(SYMLINK) $(ROOT_CHALUPA_DRV_LINK_64)/$@ $(ROOT_CHALUPA_DRV_DIR_64)/$@
	if [ -z "$(TONICBUILD)" ];  then \
		$(RM) \
		$(ROOT_CHALUPA_DRV_DIR_64:$(ROOT)/%=$(CLOSEDROOT)/%)/$(@F) ; \
		$(SYMLINK) \
		$(ROOT_CHALUPA_DRV_LINK_64:$(ROOT)/%=$(CLOSEDROOT)/%)/$(@F) \
		$(ROOT_CHALUPA_DRV_DIR_64:$(ROOT)/%=$(CLOSEDROOT)/%)/$(@F) ; \
	fi

tsalarm.conf:	$(ROOT_CHALUPA_DRV_DIR)
	-@$(RM) $(ROOT_CHALUPA_DRV_DIR)/$@
	$(SYMLINK) $(ROOT_CHALUPA_DRV_LINK)/$@ $(ROOT_CHALUPA_DRV_DIR)/$@
	if [ -z "$(TONICBUILD)" ]; then \
		$(RM) \
		$(ROOT_CHALUPA_DRV_DIR:$(ROOT)/%=$(CLOSEDROOT)/%)/$(@F) ; \
		$(SYMLINK) \
		$(ROOT_CHALUPA_DRV_LINK:$(ROOT)/%=$(CLOSEDROOT)/%)/$(@F) \
		$(ROOT_CHALUPA_DRV_DIR:$(ROOT)/%=$(CLOSEDROOT)/%)/$(@F) ; \
	fi

ntwdt:	$(ROOT_CHALUPA_DRV_DIR_64)
	-@$(RM) $(ROOT_CHALUPA_DRV_DIR_64)/$@
	$(SYMLINK) $(ROOT_CHALUPA_DRV_LINK_64)/$@ $(ROOT_CHALUPA_DRV_DIR_64)/$@
	if [ -z "$(TONICBUILD)" ] ; then \
		$(RM) \
		$(ROOT_CHALUPA_DRV_DIR_64:$(ROOT)/%=$(CLOSEDROOT)/%)/$(@F) ; \
		$(SYMLINK) \
		$(ROOT_CHALUPA_DRV_LINK_64:$(ROOT)/%=$(CLOSEDROOT)/%)/$(@F) \
		$(ROOT_CHALUPA_DRV_DIR_64:$(ROOT)/%=$(CLOSEDROOT)/%)/$(@F) ; \
	fi


ntwdt.conf:	$(ROOT_CHALUPA_DRV_DIR)
	-@$(RM) $(ROOT_CHALUPA_DRV_DIR)/$@
	$(SYMLINK) $(ROOT_CHALUPA_DRV_LINK)/$@ $(ROOT_CHALUPA_DRV_DIR)/$@
	if [ -z "$(TONICBUILD)" ]; then \
		$(RM) \
		$(ROOT_CHALUPA_DRV_DIR:$(ROOT)/%=$(CLOSEDROOT)/%)/$(@F) ; \
		$(SYMLINK) \
		$(ROOT_CHALUPA_DRV_LINK:$(ROOT)/%=$(CLOSEDROOT)/%)/$(@F) \
		$(ROOT_CHALUPA_DRV_DIR:$(ROOT)/%=$(CLOSEDROOT)/%)/$(@F) ; \
	fi

install_h check:	FRC


lint:		modlintlib

LINT_LIBS	 = $(LINT_LIB) \
		   -L$(CHALUPA_LINT_LIB_DIR) \
		   -L$(LINT_LIB_DIR) $(LINT_KMODS:%=-l%) \
		       $(CLOSED_LINT_KMODS:%=-l%) \
		   -L$(SPARC_LIB_DIR) $(SPARC_LINTS:%=-l%)

lint.platmod:	modlintlib
	@-$(ECHO) "\nChalupa Platform-dependent module: global crosschecks:"
	@-$(LINT) $(LINTFLAGS) $(LINT_LIBS) 2>&1 | $(LGREP.2)

#
#	Include common targets.
#
include $(UTSCLOSED)/sun4u/chalupa/Makefile.targ
