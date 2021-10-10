#
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG=		nfsmig

POFILE=		nfsmig.po

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include    $(CLOSED)/Makefile.tonic

NFSLIB=		../../../../../src/cmd/fs.d/nfs/lib

CFLAGS +=	$(CCVERBOSE) -I$(NFSLIB)
LDLIBS +=	-lreparse -lzfs -lnvpair -lcmdutils
OBJS=		nfsmig.o libmig.o
SRCS=		$(OBJS:%.o=%.c)

$(PROG):	$(OBJS)
		$(LINK.c) -o $@ $(LDLIBS) $(OBJS)
		$(POST_PROCESS)

FILEMODE=	0555

.KEEP_STATE:

all:		$(PROG)

install:	all $(ROOTUSRSBINPROG)

catalog:	$(POFILE)

$(POFILE):	$(SRCS)
	$(RM) $@
	$(COMPILE.cpp) $(SRCS)	> $(POFILE).i
	$(XGETTEXT) $(XGETFLAGS) $(POFILE).i
	sed "/^domain/d"	messages.po	> $@
	$(RM) messages.po $(POFILE).i

lint:
	$(LINT.c) $(SRCS) $(LDLIBS)

clean:
	$(RM) $(OBJS)

include $(SRC)/cmd/Makefile.targ
