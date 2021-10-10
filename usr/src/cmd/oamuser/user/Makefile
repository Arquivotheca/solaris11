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
#
# Copyright (c) 1990, 2011, Oracle and/or its affiliates. All rights reserved.
#
# cmd/oamuser/user/Makefile
#

include ../../Makefile.cmd

GREP=		grep

USERADD= 	useradd
USERDEL= 	userdel
USERMOD= 	usermod
ROLEADD=	roleadd
ROLEDEL=	roledel
ROLEMOD=	rolemod

SBINPROG=	$(USERADD)   $(USERDEL)   $(USERMOD)
#
#	Removing sysadm: deleted $(SYSADMPROG) from this target.
#
PROG=		$(SBINPROG)
PRODUCT=	$(PROG)


ADD_OBJ=	useradd.o	uid.o		homedir.o \
		call_pass.o	userdefs.o	messages.o \
		val_lgrp.o	funcs.o		val_lprj.o	rmfiles.o 

DEL_OBJ=	userdel.o     call_pass.o      homedir.o   \
		messages.o       funcs.o	uid.o \
		isbusy.o	rmfiles.o

MOD_OBJ=	usermod.o     uid.o 	movedir.o \
		rmfiles.o	call_pass.o	isbusy.o	homedir.o \
		userdefs.o    messages.o       val_lgrp.o   funcs.o \
		val_lprj.o

OBJECTS=	$(ADD_OBJ)     $(DEL_OBJ)    $(MOD_OBJ)

SRCS=		$(OBJECTS:.o=.c)
ADD_SRCS=	$(ADD_OBJ:.o=.c)
DEL_SRCS=	$(DEL_OBJ:.o=.c)
MOD_SRCS=	$(MOD_OBJ:.o=.c)
MESSAGESPO=	messages.po
POFILES=	$(MESSAGESPO)
POFILE=		user.po

LIBDIR=		../lib
LIBUSRGRP=	$(LIBDIR)/liboamuser.a
LOCAL=		../inc
HERE=		.

ROOTSKEL=	$(ROOTETC)/skel
INSSBINPROG=	$(SBINPROG:%=$(ROOTUSRSBIN)/%)
INSSKELFILE=	$(SKELFILE:%=$(ROOTSKEL)/%)

CPPFLAGS=	-I$(HERE) -I$(LOCAL) -I$(SRC)/lib/libsldap/common  $(CPPFLAGS.master)

$(INSSBINPROG)  := FILEMODE = 0555
$(INSSYSADMPROG):= FILEMODE = 0500
$(INSSKELFILE)  := FILEMODE = 0644

$(USERADD) :=	OBJS = $(ADD_OBJ)
$(USERADD) :=	LIBS = $(LIBUSRGRP)

$(USERDEL) :=	OBJS = $(DEL_OBJ)
$(USERDEL) :=	LIBS = $(LIBUSRGRP)

$(USERMOD) :=	OBJS = $(MOD_OBJ)
$(USERMOD) :=	LIBS = $(LIBUSRGRP)

$(MESSAGESPO)	:=	XGETTEXT += -a

LDLIBS +=	-lbsm -lproject -ltsol -lsldap -lnsl -lzfs -lnvpair -ltsnet
STATLIBDIR=	$(SRC)/cmd/oamuser/lib

.PARALLEL: $(OBJECTS)

all: 		$(PRODUCT) $(POFILE)

$(PROG):	$$(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LIBS) $(LDLIBS)
	$(POST_PROCESS)

$(USERADD):	$(ADD_OBJ)
$(USERMOD):	$(MOD_OBJ)
$(USERDEL):	$(DEL_OBJ)

install:	all .WAIT $(ROOTSKEL) $(INSSBINPROG) $(INSSKELFILE)
		$(RM) $(ROOTUSRSBIN)/$(ROLEADD)
		$(LN) $(ROOTUSRSBIN)/$(USERADD) $(ROOTUSRSBIN)/$(ROLEADD)
		$(RM) $(ROOTUSRSBIN)/$(ROLEDEL)
		$(LN) $(ROOTUSRSBIN)/$(USERDEL) $(ROOTUSRSBIN)/$(ROLEDEL)
		$(RM) $(ROOTUSRSBIN)/$(ROLEMOD)
		$(LN) $(ROOTUSRSBIN)/$(USERMOD) $(ROOTUSRSBIN)/$(ROLEMOD)

$(POFILE): $(POFILES)
	$(RM) $@
	$(CAT) $(POFILES) >  $@

clean:
	$(RM) $(OBJECTS) $(USERADD) $(USERMOD) $(USERDEL) $(POFILES) \
	$(POFILE)
	

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
