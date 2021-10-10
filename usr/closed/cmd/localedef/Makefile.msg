#
# Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"%Z%%M%	%I%	%E% SMI"
#

POFILE=		localedef_all.po
POFILES=	$(OBJS:%.o=%.po)

include $(SRC)/Makefile.msg.targ

$(POFILE):	$(POFILES)
	$(BUILDPO.pofiles)

%.po:	../%.c
	$(COMPILE.cpp) $< > $(<F).i
	$(XGETTEXT) $(XGETFLAGS) -d $(<F) $(<F).i
	$(RM) $@
	sed "/^domain/d" < $(<F).po > $@
	$(RM) $(<F).po $(<F).i
