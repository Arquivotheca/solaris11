#
#
# Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
#
# cmd/iconv/Makefile.com

PROG=iconv
KPROG=kbdcomp 

IOBJS= iconv.o gettab.o process.o symtab.o err.o list_conv.o scan.o \
	use_charmap.o
YIOBJS=	charmap.o	
KOBJS= main.o lexan.o output.o reach.o sort.o sym.o tree.o
YKOBJS=	gram.o
IDIR= iconv_src
KDIR= kbdcomp_src
ISRCS= $(IOBJS:%.o=../$(IDIR)/%.c)
KSRCS= $(KOBJS:%.o=../$(KDIR)/%.c)
YISRC= ../$(IDIR)/charmap.y
YKSRC=  ../$(KDIR)/gram.y
ROOTKPROG= $(KPROG:%=$(ROOTBIN)/%)
#ROOTKPROG32= $(KPROG:%=$(ROOTBIN32)/%)
#ROOTKPROG64= $(KPROG:%=$(ROOTBIN64)/%)

CLOBBERFILES= $(KPROG)
CLEANFILES= $(IOBJS) $(YIOBJS) $(KOBJS) $(YKOBJS) \
	gram.c y.tab.h charmap.c iconv.tab.h \
	$(CODESETS) \
	$(POFILES) $(POFILE)

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

#
# Message catalog
#
POFILES= $(IOBJS:%.o=%.po) $(YIOBJS:%.o=%.po)
POFILE= iconv_.po

$(PROG) $(POFILE) lint_iconv :=	YFLAGS += -d -b iconv
$(KPROG) lint_kbdcomp := YFLAGS += -d
LINTFLAGS += -um
CPPFLAGS= -I. -I../inc -I../iconv_src $(CPPFLAGS.master)

ROOTDIRS32=	$(ROOTLIB)/iconv
ROOTDIRS64=	$(ROOTLIB)/iconv/$(MACH64)

CODESETS=\
646da.8859.t 646de.8859.t 646en.8859.t \
646es.8859.t 646fr.8859.t 646it.8859.t \
646sv.8859.t 8859.646.t   8859.646da.t \
8859.646de.t 8859.646en.t 8859.646es.t \
8859.646fr.t 8859.646it.t 8859.646sv.t

ROOTCODESETS32=	$(CODESETS:%=$(ROOTDIRS32)/%) $(ROOTDIRS32)/iconv_data
ROOTCODESETS64=	$(CODESETS:%=$(ROOTDIRS64)/%) $(ROOTDIRS64)/iconv_data

# conditional assignments
#
$(ROOTCODESETS32) := FILEMODE = 444
$(ROOTCODESETS64) := FILEMODE = 444

.KEEP_STATE:

.PARALLEL: $(IOBJS) $(YIOBJS) $(KOBJS) $(YKOBJS)

.SUFFIXES: .p .t

#
# install rule
# 
$(ROOTDIRS32)/%:	%
	$(INS.file)

$(ROOTDIRS32)/%:	../$(KDIR)/%
	$(INS.file)

$(ROOTDIRS32):
	$(INS.dir)

$(ROOTDIRS64):	$(ROOTDIRS32)
	$(INS.dir)

#
# Message catalog
#
$(POFILE):  $(POFILES)
	$(RM) $@
	cat $(POFILES) > $@

charmap.c + iconv.tab.h :	$(YISRC)
	$(RM) charmap.c iconv.tab.h
	$(YACC.y) $(YISRC)
	mv iconv.tab.c charmap.c

gram.c + y.tab.h : $(YKSRC)
	$(RM) gram.c y.tab.h
	$(YACC.y) $(YKSRC)
	mv y.tab.c gram.c

#
# build rule
#
$(ROOTCODESETS64):	$(ROOTDIRS64)

$(ROOTCODESETS32):	$(ROOTDIRS32)

$(CODESETS):	$(KPROG)

$(PROG): $(IOBJS) $(YIOBJS)
	$(LINK.c) $(IOBJS) $(YIOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(KPROG): $(KOBJS) $(YKOBJS)
	$(LINK.c) $(KOBJS) $(YKOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

scan.o scan.po:		iconv.tab.h

lexan.o:	y.tab.h

%.t:	../$(KDIR)/%.p
	./$(KPROG) -o $@ $<

%.o:	%.c
	$(COMPILE.c) $<

%.o:	../$(KDIR)/%.c
	$(COMPILE.c) $<

%.o:	../$(IDIR)/%.c
	$(COMPILE.c) $<

%.po:	%.c
	$(COMPILE.cpp) $< > $(<F).i
	$(XGETTEXT) $(XGETFLAGS) -d $(<F) $(<F).i
	$(RM)	$@
	sed "/^domain/d" < $(<F).po > $@
	$(RM) $(<F).po $(<F).i

%.po:	../$(IDIR)/%.c
	$(COMPILE.cpp) $< > $(<F).i
	$(XGETTEXT) $(XGETFLAGS) -d $(<F) $(<F).i
	$(RM)	$@
	sed "/^domain/d" < $(<F).po > $@
	$(RM) $(<F).po $(<F).i

lint: lint_iconv #lint_kbdcomp

lint_iconv:	charmap.c iconv.tab.h
	$(LINT.c) $(ISRCS) $(LDLIBS)

lint_kbdcomp:	gram.c y.tab.h
	$(LINT.c) $(KSRCS) $(LDLIBS)

clean:
	$(RM) $(CLEANFILES)

include $(SRC)/cmd/Makefile.targ
