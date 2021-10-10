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
# Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
#
# cmd/oamuser/group/Makefile
#

include ../../Makefile.cmd


GROUPADD= 	groupadd
GROUPDEL= 	groupdel
GROUPMOD= 	groupmod

SBINPROG=	$(GROUPADD)   $(GROUPDEL)   $(GROUPMOD)

PROG=		$(SBINPROG)

ADD_OBJ=	groupadd.o    gid.o       messages.o
DEL_OBJ=	groupdel.o    messages.o
MOD_OBJ=	groupmod.o    messages.o

OBJECTS=	$(ADD_OBJ)    $(DEL_OBJ)    $(MOD_OBJ)
SRCS=		$(OBJECTS:.o=.c)
ADD_SRCS=	$(ADD_OBJ:.o=.c)
DEL_SRCS=	$(DEL_OBJ:.o=.c)
MOD_SRCS=	$(MOD_OBJ:.o=.c)

MESSAGESPO=     messages.po
POFILES=	$(MESSAGESPO)
POFILE=		group.po

STATLIBDIR=		$(SRC)/cmd/oamuser/lib
LDLIBS +=	-lc -lsldap -lproject -L$(STATLIBDIR) -loamuser -lnsl -ltsol -ltsnet
LOCAL=		../inc
HERE=		.
LINTLIB=	llib-loamuser.ln

INSSBINPROG=	$(SBINPROG:%=$(ROOTUSRSBIN)/%)

CPPFLAGS=	-I$(HERE) -I$(LOCAL) -I$(SRC)/lib/libsldap/common  $(CPPFLAGS.master)
FILEMODE= 	0555

$(MESSAGESPO)	:=	XGETTEXT += -a
$(GROUPADD) :=	OBJS = $(ADD_OBJ)

$(GROUPDEL) :=	OBJS = $(DEL_OBJ)

$(GROUPMOD) :=	OBJS = $(MOD_OBJ)

all: 		$(PROG) $(MESSAGESPO) $(POFILE)

$(PROG):	$$(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LIBS) $(LDLIBS)
	$(POST_PROCESS)

$(GROUPADD):	$(ADD_OBJ)
$(GROUPMOD):	$(MOD_OBJ)
$(GROUPDEL):	$(DEL_OBJ)

install:	all $(INSSBINPROG)


$(POFILE):	$(POFILES)
	$(RM)	$@
	$(CAT)	$(POFILES) > $@

clean:
	$(RM) $(PROG) $(OBJECTS) $(POFILES) $(POFILE)

$(LINTLIB):
	cd $(STATLIBDIR); pwd; $(MAKE) lint;

lint:	$(LINTLIB) lint_ADD lint_DEL lint_MOD

lint_ADD:	$$(ADD_SRCS)
	$(LINT.c) -c $(ADD_SRCS) $(LDLIBS)

lint_DEL:	$$(DEL_SRCS)
	$(LINT.c) -c $(DEL_SRCS) $(LDLIBS)

lint_MOD:	$$(MOD_SRCS)
	$(LINT.c) -c $(MOD_SRCS) $(LDLIBS)

include ../../Makefile.targ
