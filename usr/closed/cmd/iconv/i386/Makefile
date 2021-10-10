#
#ident	"%Z%%M%	%I%	%E% SMI"        
#
# Copyright (c) 1997 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/iconv/i386/Makefile

.KEEP_STATE:

include ../Makefile.com

catalog: $(MSGDOMAIN) $(POFILE)
	$(RM) $(MSGDOMAIN)/$(POFILE)
	$(CP) $(POFILE) $(MSGDOMAIN)

all:	$(PROG) $(KPROG) $(CODESETS)

install: all $(ROOTDIRS32) $(ROOTPROG) $(ROOTKPROG) $(ROOTCODESETS32)
