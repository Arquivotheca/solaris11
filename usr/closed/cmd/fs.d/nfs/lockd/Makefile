#
# Copyright (c) 1990, 2010, Oracle and/or its affiliates. All rights reserved.
#
#
# cmd/fs.d/nfs/lockd/Makefile

FSTYPE=		nfs
TYPEPROG=	lockd
ATTMK=		$(TYPEPROG)

include		$(SRC)/cmd/fs.d/Makefile.fstype
$(TONICBUILD)include    $(CLOSED)/Makefile.tonic

NFSLIB=		../../../../../src/cmd/fs.d/nfs/lib

LOCAL=		lockd.o
LIBOBJS=	nfs_tbind.o thrpool.o smfcfg.o
OBJS=		$(LOCAL) $(LIBOBJS)
SRCS=		$(LOCAL:%.o=%.c) $(LIBOBJS:%.o=$(NFSLIB)/%.c)
LDLIBS +=	-lnsl -lscf
CPPFLAGS +=	-I$(NFSLIB)

#
# Message catalog
#
POFILE= lockd.po

#
# message catalog
#
catalog:        $(POFILE)

$(POFILE):      $(SRCS)
	$(RM) $@
	$(COMPILE.cpp) $(SRCS)   > $(POFILE).i
	$(XGETTEXT) $(XGETFLAGS)        $(POFILE).i
	sed "/^domain/d"        messages.po     > $@
	$(RM) messages.po $(POFILE).i


$(TYPEPROG):	$(OBJS)
		$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

%.o:	$(NFSLIB)/%.c
	$(COMPILE.c) -o $@ $<

lint:		$(SRCS) lint_SRCS

clean:
		$(RM) $(OBJS)

# Build the .o's with the following CFLAGS for best results with
# Purify.  Remove the .o's before trying to build a regular
# (non-Purify) lockd.
# CFLAGS = -g $($(MACH)_CFLAGS) -DPURIFY

lockd.pure:	$(OBJS)
		purify $(LINK.c) -o $@ $(OBJS) $(LDLIBS)
