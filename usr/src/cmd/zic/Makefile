#
# Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
#

NATIVE=native
PROG= zic
KSHPROG= tzselect
NPROG= $(NATIVE)/$(PROG)

OBJS= zic.o scheck.o ialloc.o
SRCS= $(OBJS:%.o=%.c)
NOBJS= $(OBJS:%=$(NATIVE)/%)

CLOBBERFILES= $(NOBJS) $(KSHPROG) $(NPROG) $(NATIVE)/zic.c $(NATIVE)/tzfile.h

include ../Makefile.cmd

LDLIBS += -lscf
NCPPFLAGS=	-I$(NATIVE)

ROOTKSHPROG=    $(ROOTBIN)/$(KSHPROG)

#
# Message catalog
#
POFILE2=tzselect.po

.KEEP_STATE:

all: $(PROG) $(NPROG) $(KSHPROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTUSRSBINPROG) $(ROOTKSHPROG)

$(NPROG): $(NATIVE) $(NOBJS)
	$(NATIVECC) -o $@ $(NOBJS) $(LDLIBS)

#
# XXX
# There is a .ksh rule in Makefile.targ, but we need our custom POFILE rules.
#
$(KSHPROG):	$(KSHPROG).ksh
	$(RM) $@
	$(CAT) $(KSHPROG).ksh > $@
	$(CHMOD) +x $@

$(NATIVE):
	-@mkdir -p $@

clean:
	$(RM) $(OBJS)

lint: lint_SRCS

$(NOBJS): $$(@F:.o=.c)
	$(NATIVECC) -O $(NCPPFLAGS) -c -o $@ $(@F:.o=.c)

$(NATIVE)/zic.o: $(NATIVE)/tzfile.h

#$(NATIVE)/scheck.o: scheck.c
#	$(NATIVECC) -c -o $@ scheck.c

#$(NATIVE)/ialloc.o: ialloc.c
#	$(NATIVECC) -c -o $@ ialloc.c

$(NATIVE)/tzfile.h: $(ROOT)/usr/include/tzfile.h
	$(RM) $@; cp $(ROOT)/usr/include/tzfile.h $@

#
# From ../Makefile.targ
#
clobber: clean
	-$(RM) $(PROG) $(CLOBBERFILES)

lint_PROG:
	$(LINT.c) $(PROG).c $(LDLIBS)

lint_SRCS:
	$(LINT.c) $(SRCS) $(LDLIBS)

#
# For message catalogue files
#
_msg: $(MSGDOMAIN) $(POFILE) $(POFILE2)
	$(RM) $(MSGDOMAIN)/$(POFILE)
	$(CP) $(POFILE) $(MSGDOMAIN)
	$(RM) $(MSGDOMAIN)/$(POFILE2)
	$(CP) $(POFILE2) $(MSGDOMAIN)

$(MSGDOMAIN) $(DCMSGDOMAIN):
	$(INS.dir)

