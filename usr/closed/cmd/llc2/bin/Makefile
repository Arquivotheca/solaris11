#
# Copyright (c) 1998, 2010, Oracle and/or its affiliates. All rights reserved.
#


include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

AUTOCONFPROG=	llc2_autoconfig
CONFIGPROG=	llc2_config
LLC2UTILS=	llc2_stats llc2_loop2 llc2_loop3 llc2_loop4 llc2_tcap \
		llc2_tparser
PROG=		$(CONFIGPROG) $(AUTOCONFPROG) $(LLC2UTILS)
SRCS=		llc2_stats.c llc2_loop2.c llc2_loop3.c llc2_loop4.c \
		llc2_tcap.c llc2_tparser.c
OBJS=		llc2_autoconfig.o llc2_config.o llc2_conf.o $(SRCS:%.c=%.o)
AUTOCONFOBJS=	llc2_autoconfig.o llc2_conf.o
CONFIGOBJS=	llc2_config.o llc2_conf.o
AUTOCONFSRCS=	$(AUTOCONFOBJS:%.o=%.c)
CONFIGSRCS=	$(CONFIGOBJS:%.o=%.c)

POFILES = $(OBJS:%.o=%.po)

GENHEADERS=	/bin/sh ./genheaders
SRCTAG=		ild_ftbl.h dlpi_ftbl.h llc2_ftbl.h SAM_ftbl.h

CLOBBERFILES=	$(PROG) $(OBJS) $(SRCTAG)

INC_PATH += ../../../uts/common/io/llc2 -I../../../uts/common/sys

CPPFLAGS +=	-I$(INC_PATH) -DSTATIC=""  -DSOLARIS

ROOTUSRLIBDIR	=	$(ROOTLIB)/llc2
ROOTUSRLIBPROG	=	$(PROG:%=$(ROOTLIB)/llc2/%)

$(AUTOCONFPROG)	:= LDLIBS += -ldlpi
$(CONFIGPROG)	:= LDLIBS += -ldlpi

.KEEP_STATE:

all: $(PROG)

$(AUTOCONFPROG): $(AUTOCONFOBJS) llc2_conf.h
	$(CC) $(AUTOCONFOBJS) -o $@ $(LDFLAGS) $(LDLIBS)
	$(POST_PROCESS)

$(CONFIGPROG): $(CONFIGOBJS) llc2_conf.h
	$(CC) $(CONFIGOBJS) -o $@ $(LDFLAGS) $(LDLIBS)
	$(POST_PROCESS)

llc2_tparser: $(SRCTAG) llc2_tparser.o
	$(CC) llc2_tparser.o -o $@ $(LDFLAGS) $(LDLIBS)
	$(POST_PROCESS)

ild_ftbl.h: ../../../uts/common/io/llc2/ild.c
	$(GENHEADERS) ../../../uts/common/io/llc2/ild.c > $@ 2>/dev/null

dlpi_ftbl.h: ../../../uts/common/io/llc2/dlpi.c
	$(GENHEADERS) ../../../uts/common/io/llc2/dlpi.c > $@ 2>/dev/null

llc2_ftbl.h: ../../../uts/common/io/llc2/llc2.c
	$(GENHEADERS) ../../../uts/common/io/llc2/llc2.c > $@ 2>/dev/null

SAM_ftbl.h: ../../../uts/common/io/llc2/SAM.c
	$(GENHEADERS) ../../../uts/common/io/llc2/SAM.c > $@ 2>/dev/null

FILEMODE=	555

$(ROOTUSRLIBDIR):
	$(INS.dir)

$(ROOTLIB)/llc2/%: %
	$(INS.file)

install: $(PROG) $(ROOTUSRLIBDIR) $(ROOTUSRLIBPROG)

clobber:
	$(RM) $(CLOBBERFILES) $(POFILES)

clean: 
	$(RM) $(CLOBBERFILES) $(POFILES)

lint:	#$(SUBDIRS)
	$(LINT.c) $(AUTOCONFSRCS)
	$(LINT.c) $(CONFIGSRCS)
	$(LINT.c) $(SRCS)

# for messaging catalog
#
_msg: $(POFILES)
	cp $(POFILES) $(MSGDOMAIN)

