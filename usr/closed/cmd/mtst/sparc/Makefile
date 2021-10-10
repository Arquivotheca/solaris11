#
# Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
#

PROG		= mtst

include $(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include $(CLOSED)/Makefile.tonic

ASFLAGS		+= -P -D_ASM -xarch=v8plusa -I$(ROOT)/usr/platform/sun4u/include
CFLAGS		+= $(CCVERBOSE)
CPPFLAGS	+= -I$(ROOT)/usr/platform/sun4u/include
CPPFLAGS	+= -I$(ROOT)/usr/platform/sun4v/include

LINTFLAGS	+= -x -erroff=E_GLOBAL_COULD_BE_STATIC2 -errtags=yes

OBJS_C		= mtst.o mtst_ch.o mtst_chp.o mtst_ja.o mtst_jg.o mtst_kt.o \
			mtst_ni.o mtst_n2.o mtst_oc.o mtst_pn.o mtst_sf.o \
			mtst_sr.o mtst_vf.o

OBJS_S		= mtst_asm.o
OBJS		= $(OBJS_C) $(OBJS_S)

SRCS_C		= $(OBJS_C:%.o=%.c)
SRCS_S		= $(OBJS_S:%.o=%.s)
SRCS		= $(SRCS_C) $(SRCS_S)

.KEEP_STATE:

all:		$(PROG)

install:	all $(ROOTPROG)

$(PROG):	$(OBJS)
		$(LINK.c) -o $(PROG) $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

clean:
		-$(RM) $(OBJS)

lint:		lint_$(MACH)

lint_i386:

lint_sparc:
		$(LINT.c) $(SRCS_C) $(SRCS_S) $(LDLIBS)

include $(SRC)/cmd/Makefile.targ
