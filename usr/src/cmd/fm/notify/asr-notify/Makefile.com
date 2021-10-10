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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

.KEEP_STATE:
.SUFFIXES:

SRCS += asradm.c asr_notify.c asr_notify_scf.c \
asr_dts.c asr_scrk.c asr_stag.c asr_ssl.c asr_curl.c asr_base64.c

OBJS = $(SRCS:%.c=%.o)
LINTFILES = $(SRCS:%.c=%.ln)

PROG = asr-notify
ROOTLIBFM = $(ROOT)/usr/lib/fm
ROOTLIBNOTIFY = $(ROOT)/usr/lib/fm/notify
ROOTPROG = $(ROOTLIBNOTIFY)/$(PROG)

ROOTMANIFESTDIR = $(ROOTSVCSYSTEM)/fm
ROOTMANIFEST = $(ROOTMANIFESTDIR)/$(PROG).xml
ROOTNOTIFYPARAMS = $(ROOTMANIFESTDIR)/notify-params.xml
$(ROOTMANIFEST) := FILEMODE = 0444
$(ROOTNOTIFYPARAMS) := FILEMODE = 0444
ROOTLINK = $(ROOT)/usr/sbin/asradm
INSLINKTARGET = ../lib/fm/notify/asr-notify

$(NOT_RELEASE_BUILD)CPPFLAGS += -DDEBUG
CPPFLAGS += -I. -I../common -I../../../../../lib/fm/libfmnotify/common \
-I../../../../../lib/fm/libasr/common
CFLAGS += $(CTF_FLAGS) $(CCVERBOSE) $(XSTRCONST)
LDLIBS += -L$(ROOT)/usr/lib/fm -lfmd_adm -lfmd_msg -lfmnotify
LDLIBS += -lasr -lscf -lcurl -lcrypto -lnvpair -lfmevent -luuid -lsocket
LDFLAGS += -R/usr/lib/fm
LINTFLAGS += -mnu

.NO_PARALLEL:
.PARALLEL: $(OBJS) $(LINTFILES)

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(CTFMERGE) -L VERSION -o $@ $(OBJS)
	$(POST_PROCESS)


%.o: ../common/%.c
	$(COMPILE.c) $<
	$(CTFCONVERT_O)

%.o: %.c
	$(COMPILE.c) $<
	$(CTFCONVERT_O)

clean:
	$(RM) $(OBJS) $(LINTFILES)

clobber: clean
	$(RM) $(PROG)

%.ln: ../common/%.c
	$(LINT.c) -c $<

%.ln: %.c
	$(LINT.c) -c $<

lint: $(LINTFILES)
	$(LINT) $(LINTFLAGS) $(LINTFILES)

$(ROOTLIBNOTIFY):
	$(INS.dir)

$(ROOTLIBNOTIFY)/%: %
	$(INS.file)

$(ROOTMANIFESTDIR):
	$(INS.dir)

$(ROOTMANIFESTDIR)/%.xml: ../common/%.xml
	$(INS.file)

$(ROOTMANIFESTDIR)/notify-params.xml: ../../notify-params.xml
	$(INS.file) ../../notify-params.xml

$(ROOTLINK):
	$(INS.symlink)

install_h:

install: all $(ROOTLIBNOTIFY) $(ROOTPROG) $(ROOTMANIFESTDIR) \
$(ROOTMANIFEST) $(ROOTNOTIFYPARAMS) $(ROOTLINK)
