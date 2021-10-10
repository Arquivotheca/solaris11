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

PROG= stty ttymon sttydefs
XPG4PROG= stty

# the 'stty' objects have to be made in a separate directory
# since only they are built with the -DEUC flag (see below).
STTYOBJ=	sttyobjs/stty.o sttyobjs/sttytable.o sttyobjs/sttyparse.o

XPG4STTYOBJ=	sttyobjs.xpg4/stty.o sttyobjs.xpg4/sttytable.o \
		sttyobjs.xpg4/sttyparse.o

TTYMONOBJ=	ttymon.o tmglobal.o tmttydefs.o \
		tmparse.o tmchild.o tmautobaud.o tmterm.o \
		tmutmp.o tmpeek.o tmlog.o tmlock.o tmutil.o tmexpress.o \
		sttytable.o sttyparse.o ulockf.o

STTYDEFSOBJ=	sttydefs.o admutil.o tmttydefs.o tmparse.o sttytable.o \
		sttyparse.o checkver.o

OBJS= $(STTYOBJ) $(XPG4STTYOBJ) $(TTYMONOBJ) $(STTYDEFSOBJ)
STTYSRC= stty.c sttytable.c sttyparse.c
TTYMONSRC= $(TTYMONOBJ:%.o=%.c)
STTYDEFSSRC= $(STTYDEFSOBJ:%.o=%.c)
SRCS= $(STTYSRC) $(TTYMONSRC) $(STTYDEFSSRC)

ETCFILES=ttydefs

CLOBBERFILES += $(ROOTUSRSBINF) $(ROOTBINF) \
    $(ROOTXPG4PROG) $(ROOTXPG6SYMLINK) $(ROOTETCF) $(ROOTTTYMONLINK)

.PARALLEL:	$(OBJS) $(PROG)

include ../Makefile.cmd

$(XPG4):= CPPFLAGS += -DXPG4
TTYMONLDLIBS= -lsec -ldevinfo
ttymon := LDLIBS += $(TTYMONLDLIBS)

# Only stty can be built with -DEUC.  ttymon will dump core unless further
# changes are made to it.
$(STTYOBJ) := CPPFLAGS += -DEUC
$(XPG4STTYOBJ) := CPPFLAGS += -DEUC

CFLAGS += $(CCVERBOSE)
LDFLAGS += $(MAPFILE.NGB:%=-M%)

#
# Message catalog
#
POFILES= $(STTYOBJ:sttyobjs/%.o=%.po)
POFILE= ttymon.po

ROOTLIBSAFD= $(ROOTLIB)/saf
ROOTTTYMONLINK=$(ROOTLIBSAFD)/ttymon
DIRS= $(ROOTLIBSAFD)
USRSBINF= sttydefs ttymon
BINF= stty
ROOTETCF= $(ETCFILES:%=$(ROOTETC)/%)
ROOTUSRSBINF= $(USRSBINF:%=$(ROOTUSRSBIN)/%)
ROOTBINF= $(BINF:%=$(ROOTBIN)/%)
$(ROOTETCF) :=			FILEMODE =	0644

$(ROOTETC)/% : %
	$(INS.file)

$(ROOTTTYMONLINK): $(ROOTUSRSBIN)/ttymon
	-$(RM) $@
	-$(SYMLINK) ../../sbin/ttymon $@

# /usr/xpg6/bin/stty is a symlink to /usr/bin/stty
ROOTXPG6SYMLINK= $(ROOTXPG6BIN)/stty
.KEEP_STATE:

all: $(PROG) $(XPG4) $(ETCFILES)

stty:	sttyobjs $(STTYOBJ) $(MAPFILE.NGB)
	$(LINK.c) $(STTYOBJ) -o $@ $(LDLIBS)
	$(POST_PROCESS)

stty.xpg4: $(XPG4STTYOBJ) $(MAPFILE.NGB)
	$(LINK.c) $(XPG4STTYOBJ) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(XPG4STTYOBJ): sttyobjs.xpg4

sttyobjs/%.o:	%.c
	$(COMPILE.c) -o $@ $<

sttyobjs.xpg4/%.o:	%.c
	$(COMPILE.c) -o $@ $<

sttyobjs:
	-@mkdir -p $@

sttyobjs.xpg4:
	-@mkdir -p $@

ttymon:	$(TTYMONOBJ) $(MAPFILE.NGB)
	$(LINK.c) $(TTYMONOBJ) -o $@ $(LDLIBS)
	$(POST_PROCESS)

sttydefs: $(STTYDEFSOBJ) $(MAPFILE.NGB)
	$(LINK.c) $(STTYDEFSOBJ) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(DIRS) $(ROOTUSRSBINF) $(ROOTBINF) \
	$(ROOTXPG4PROG) $(ROOTXPG6SYMLINK) $(ROOTETCF) $(ROOTTTYMONLINK)

$(ROOTXPG6SYMLINK): $(ROOTBINF)
	-$(RM) $@
	-$(SYMLINK) ../../bin/$(BINF) $@

$(POFILE):      $(POFILES)
	$(RM) $@
	cat $(POFILES)  > $@

$(DIRS):
	$(INS.dir)

clean:
	$(RM) $(OBJS)

lint:
	$(LINT.c) $(STTYSRC) $(LDLIBS)
	$(LINT.c) $(TTYMONSRC) $(LDLIBS) $(TTYMONLDLIBS)
	$(LINT.c) $(STTYDEFSSRC) $(LDLIBS)

include ../Makefile.targ
