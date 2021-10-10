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
# Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
#

#
# uts/sun4v/forthdebug/Makefile
#
#       This makefile drives the production of the forthdebug kernel file
#
#       sun4v implementation architecture dependent
#

#
#       Path to the base of the uts directory tree (usually /usr/src/uts).
#

UTSCLOSED = ../..
UTSBASE = ../../../../src/uts

#
#       Define the module and object file sets.
#

INPUTDIR	= $(UTSCLOSED)/sun4/forthdebug
FORTHDEBUG	= forthdebug
FDBG_MODULE	= $(OBJS_DIR)/$(FORTHDEBUG)
ROOTMODULE      = $(ROOT_PSM_MISC_DIR)/$(FORTHDEBUG)

# the first 5 *.fth should not be moved 

FTH_FILES 	= \
	common.fth	\
	sunddi.fth	\
	sparc.fth	\
	sun4v.fth	\
	sfmmu.fth	\
			\
	memnode.fth	\
	px.fth

# EXPORT DELETE START
XMOD_FTH_FILES = \
	tavor.fth

# EXPORT DELETE END

FILES		= $(FTH_FILES:%=$(OBJS_DIR)/%) \
		  $(XMOD_FTH_FILES:%=$(OBJS_DIR)/%)
KDBG		= $(INPUTDIR)/kdbg.fth
DBP		= $(INPUTDIR)/dbp.fth
SYM		= $(OBJS_DIR)/sym.fth
MODELFILE	= $(INPUTDIR)/data64.fth

TMP_C		= $(OBJS_DIR)/$(*F).c
TMP_O		= $(OBJS_DIR)/$(*F).o
TMP_TMP		= $(OBJS_DIR)/$(*F).tmp

ALL_FDBG	= $(FILES) $(SYM)
ALL_TEMPS	= $(ALL_FDBG:%.fth=%.c)	\
		  $(ALL_FDBG:%.fth=%.o)	\
		  $(ALL_FDBG:%.fth=%.tmp)

#
#       Include common rules.
#
include $(UTSCLOSED)/sun4v/Makefile.sun4v

#
#	Override defaults
#
CLEANFILES	= $(FILES) $(SYM) $(ALL_TEMPS) Nothing_to_remove
CLOBBERFILES	= $(FDBG_MODULE) $(CLEANFILES) Nothing_to_remove

#
#	Define targets
#
ALL_TARGET	= $(FDBG_MODULE)
INSTALL_TARGET	= $(FDBG_MODULE) $(ROOTMODULE)

#
# Emit DWARF information for all types
#
CTF_FLAGS      += $(CALLSYMS)

#
#	Default build targets
#
.KEEP_STATE:

.SUFFIXES: .o .c .fdbg .fth

.PARALLEL:	$(FILES)

def:		$(DEF_DEPS)

all:		$(ALL_DEPS)

clean:		$(CLEAN_DEPS)

clobber:	$(CLOBBER_DEPS)

lint:

modlintlib:

clean.lint:

install:	$(INSTALL_DEPS)

$(FDBG_MODULE):	$(MODELFILE) $(KDBG) $(SYM) $(DBP) $(FILES)
	$(CAT) $(MODELFILE) $(KDBG) $(SYM) $(DBP) $(FILES)  | \
		$(SED) -e '/CDDL HEADER START/,/CDDL HEADER END/d' > $@

$(OBJS_DIR)/%.fth: %.fdbg
	$(GREP) '^#' < $< > $(TMP_C)
	$(GREP) -v '^#' < $< > $(TMP_TMP)
	$(CC) $(CFLAGS) $(CTF_FLAGS) $(CPPFLAGS) -c -o $(TMP_O) $(TMP_C)
	$(CTFCONVERT) $(CTFCVTFLAGS) $(TMP_O)
	$(CTFSTABS) -t forth -i $(TMP_TMP) -o $@ $(TMP_O)
	$(RM) $(TMP_C) $(TMP_O) $(TMP_TMP)

$(OBJS_DIR)/%.fth: $(INPUTDIR)/%.fdbg
	$(GREP) '^#' < $< > $(TMP_C)
	$(GREP) -v '^#' < $< > $(TMP_TMP)
	$(CC) $(CFLAGS) $(CTF_FLAGS) $(CPPFLAGS) -c -o $(TMP_O) $(TMP_C)
	$(CTFCONVERT) $(CTFCVTFLAGS) $(TMP_O)
	$(CTFSTABS) -t forth -i $(TMP_TMP) -o $@ $(TMP_O)
	$(RM) $(TMP_C) $(TMP_O) $(TMP_TMP)

# EXPORT DELETE START
EXPORT_SRC:
	$(RM) Makefile+
	$(SED) -e "/^# EXPORT DELETE START/,/^# EXPORT DELETE END/d" \
			< Makefile > Makefile+
	$(MV) Makefile+ Makefile
	$(CHMOD) 444 Makefile
# EXPORT DELETE END

#	Include common targets.
#
include $(UTSCLOSED)/$(PLATFORM)/Makefile.targ
