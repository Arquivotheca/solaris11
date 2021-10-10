#
# Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#

.KEEP_STATE:


include ../Makefile.com
include $(SRC)/cmd/Makefile.cmd.64

#catalog: $(MSGDOMAIN64) $(POFILE)
#	$(RM) $(MSGDOMAIN64)/$(POFILE)
#	$(CP) $(POFILE) $(MSGDOMAIN64)

catalog:

# iconv and kbdcomp are built but not installed into proto
# 'make install' installs only $(ROOTLIB)/iconv/$(MACH64) directory

all: $(PROG) $(KPROG)

install: all $(ROOTDIRS64)

iconv_tbl:	$(CODESETS)

