#
# Copyright (c) 1990, 2011, Oracle and/or its affiliates. All rights reserved.
#

include		../../../Makefile.cmd

# ftpusers file is common interface for many FTP servers. In Solaris it is
# used by proftpd
CONFIGFILES=	ftpusers
ETCFTPDDIR=	$(ROOTETC)/ftpd
ETCFTPDFILES=	$(CONFIGFILES:%=$(ETCFTPDDIR)/%)
$(ETCFTPDFILES):=	FILEMODE= 0644

.KEEP_STATE:

all:		$(CONFIGFILES)

$(ETCFTPDDIR)/%: %
		$(INS.file)

install:	all $(ETCFTPDFILES)
		$(RM) $(ROOTETC)/ftpusers
		$(SYMLINK) ./ftpd/ftpusers $(ROOTETC)/ftpusers

clean:

include		../../../Makefile.targ
