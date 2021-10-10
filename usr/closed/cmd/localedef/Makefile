#
# Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"%Z%%M%	%I%	%E% SMI"
#

include $(SRC)/cmd/Makefile.cmd

include ./Makefile.locale

SUBDIRS =	$(MACH)
$(BUILD64)SUBDIRS += $(MACH64)

all	:=	TARGET = all
install	:=	TARGET = install
clean	:=	TARGET = clean
clobber	:=	TARGET = clobber
lint	:=	TARGET = lint
_msg	:=	TARGET = catalog

POFILE=  localedef.po
POFILES= err.po
XGETFLAGS=
CLOBBERFILES +=	locale_description

.KEEP_STATE:

.NO_PARALLEL:	$(SUBDIRS)

all:	$(SUBDIRS)

install:	$(ROOTDIRS) $(ROOTSYMLN) .WAIT $(ROOTCDESC) \
	$(SUBDIRS) \
	$(ROOTI18NEXT)/$(SB_DENSE_EXTENSIONS) \
	$(ROOTI18NEXT)/$(GENERIC_EUCBC_EXTENSIONS) \
	$(ROOTISOSRCDIR) $(ROOTISOSRCS)

clean lint _msg:	$(SUBDIRS)

clobber:	$(SUBDIRS) clobber_local

clobber_local:
	$(RM) $(CLOBBERFILES)

$(SUBDIRS):	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

include ./Makefile.target
