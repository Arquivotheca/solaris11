#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#

SRCSC1 =	itmcomp.c assemble.c disassemble.c itm_util.c
SRCY1 =		itm_comp.y
SRCL1 =		itm_comp.l
SRCI1 =		geniconvtbl.c

YTABC =		y.tab.c
YTABH =		y.tab.h
LEXYY =		lex.yy.c
YOUT =		y.output

SRCSC =		$(SRCSC1:%.c=../%.c)
SRCI =		$(SRCI1:%.c=../%.c)
SRCY =		$(SRCY1:%.y=../%.y)
SRCL =		$(SRCL1:%.l=../%.l)

SRCYC =		$(SRCY:%.y=%.c)
SRCLC =		$(SRCL:%.l=%.c)

SRCS =		$(SRCSC) $(YTABC) $(LEXYY)
HDRS =		$(SRCCH1) $(ERNOSTRH)

LEXSED =	../lex.sed
YACCSED =	../yacc.sed

OBJS =		$(SRCSC1:%.c=%.o) $(YTABC:.c=.o) $(LEXYY:.c=.o)

CLEANFILES = 	$(OBJS) $(YTABC) $(YTABH) $(LEXYY) $(YOUT)

CPPFLAGS +=	-I. -I..
CFLAGS +=	-D_FILE_OFFSET_BITS=64
LDLIBS +=	-lgen
YFLAGS +=	-d -v

$(PROG):	$(OBJS)
		$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

$(YTABC) $(YTABH): $(SRCY)
		$(YACC) $(YFLAGS) $(SRCY)
		@ $(MV) $(YTABC) $(YTABC)~
		@ $(SED) -f $(YACCSED) $(YTABC)~ > $(YTABC)
		@ $(RM) $(YTABC)~

$(LEXYY):	$(SRCL) $(YTABH)
		$(LEX) -t $(SRCL) | $(SED) -f $(LEXSED) > $(LEXYY)

%.o:		../%.c
		$(COMPILE.c) $<

clean:
		$(RM) $(CLEANFILES)
