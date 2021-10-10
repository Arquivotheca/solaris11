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
# Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
#
# Architecture independent makefile for svm utilities
#
# cmd/lvm/util/Makefile.com
#

# programs that are installed in /usr/sbin
PROG= \
	medstat \
	metaclear \
	metadetach \
	metahs \
	metaoffline \
	metaonline \
	metaparam \
	metarename \
	metareplace \
	metaset \
	metasync \
	metattach \
	metaimport \
	metadb \
	metadevadm \
	metainit \
	metarecover \
	metastat

# programs that are installed in /usr/lib/lvm
METACLUST= metaclust

OBJECTS =  \
	medstat.o \
	metaclear.o \
	metadb.o \
	metadetach.o \
	metadevadm.o \
	metahs.o \
	metainit.o \
	metaoffline.o \
	metaonline.o \
	metaparam.o \
	metarecover.o \
	metarename.o \
	metareplace.o \
	metaset.o \
	metastat.o \
	metasync.o \
	metattach.o \
	metaclust.o \
	metaimport.o

SRCS=	$(OBJECTS:%.o=../%.c)

include ../../../Makefile.cmd
include ../../Makefile.lvm

ROOTLIBSVM = $(ROOTLIB)/lvm

ROOTUSRSBINPROG = $(PROG:%=$(ROOTUSRSBIN)/%)

POFILE= utilp.po
DEFINES += -DDEBUG
CPPFLAGS += $(DEFINES)

metainit := CPPFLAGS += -I$(SRC)/lib/lvm/libmeta/common/hdrs
metaset := LDLIBS += -ldevid

LDLIBS +=	-lmeta

lint := LINTFLAGS += -m

install		:= TARGET = install
clean		:= TARGET = clean

.KEEP_STATE:

%.o:	../%.c
	$(COMPILE.c) $<
	$(POST_PROCESS_O)

all:     $(PROG) $(METACLUST)

catalog: $(POFILE)

$(PROG): $$(@).o
	$(LINK.c) -o $@ $(@).o $(LDLIBS)
	$(POST_PROCESS)

$(METACLUST): $$(@).o
	$(LINK.c) -o $@ $(@).o $(LDLIBS)
	$(POST_PROCESS)


install: all .WAIT $(ROOTLIBSVM) $(ROOTUSRSBINPROG) $(ROOTLIBSVM)/$(METACLUST)

cstyle:
	$(CSTYLE) $(SRCS)

lint:
	for f in $(SRCS) ; do \
		if [ $$f = "../metainit.c" ]; then \
		    $(LINT.c) $(LINTFLAGS) \
			-I$(SRC)/lib/lvm/libmeta/common/hdrs $$f ; \
		else \
			$(LINT.c) $(LINTFLAGS) $$f ; \
		fi \
	done

clean:
	$(RM) $(OBJECTS) $(PROG)

include ../../../Makefile.targ

${ROOTLIBSVM}/%: %
	${INS.file}

${ROOTLIBSVM}:
	${INS.dir}

