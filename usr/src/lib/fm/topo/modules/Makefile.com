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

MODCLASS = plugins

#
# Set PROG and OBJS based on the values of MODULE and SRCS.  We expect that
# these macros to be defined by the Makefile that is including this file.
#
# SHAREDSRCS is used to share sources between multiple libtopo modules.
#
SRCS = $(MODULESRCS:%.c=%.c) $(SHAREDSRCS:%.c=%.c)
PROG = $(MODULE:%=%.so)
OBJS = $(MODULESRCS:%.c=%.o) $(SHAREDSRCS:%.c=%.o)

#
# Set ROOTPROG and ROOTCONF based on the values of MODULE, CLASS, and PLATFORMS
# We expect these macros to be defined by the Makefile that is including us.
#
common_ROOTPROG = $(ROOT)/usr/lib/fm/topo/plugins/$(PROG)
arch_ROOTPROG = $(ROOT)/usr/platform/$(ARCH)/lib/fm/topo/plugins/$(PROG)
plat_ROOTPROG = $(PLATFORMS:%=$(ROOT)/usr/platform/%/lib/fm/topo/plugins/$(PROG))
ROOTPROG = $($(CLASS)_ROOTPROG)

common_ROOTCONF = $(ROOT)/usr/lib/fm/topo/plugins/$(CONF)
arch_ROOTCONF = $(ROOT)/usr/platform/$(ARCH)/lib/fm/topo/plugins/$(CONF)
plat_ROOTCONF = $(PLATFORMS:%=$(ROOT)/usr/platform/%/lib/fm/topo/plugins/$(CONF))
ROOTCONF = $($(CLASS)_ROOTCONF)

common_ROOTLIB = $(ROOT)/usr/lib/fm/topo/plugins/
arch_ROOTLIB = $(ROOT)/usr/platform/$(ARCH)/lib/fm/topo/plugins/
plat_ROOTLIB = $(PLATFORMS:%=$(ROOT)/usr/platform/%/lib/fm/topo/plugins/)
ROOTLIB = $($(CLASS)_ROOTLIB)
ROOTPROG64 = $(ROOTLIB)/$(MACH64)/$(PROG)

PLUGINDIR = $(ROOTLIB)
PLUGINDIR64 = $(ROOTLIB)/$(MACH64)

LINTFLAGS = -msux
LINTFILES = $(MODULESRCS:%.c=%.ln)

APIMAP = ../../../../libtopo/common/topo_mod.map
MAPFILES =		# use APIMAP instead

C99MODE  = $(C99_ENABLE)
CFLAGS += $(CTF_FLAGS) $(CCVERBOSE) $(CC_PICFLAGS)
CFLAGS += -G $(XREGSFLAG)
CFLAGS64 += $(CTF_FLAGS) $(CCVERBOSE) $(CC_PICFLAGS)
CFLAGS64 += -G $(XREGSFLAG)

CPPFLAGS += -D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT
LDFLAGS += $(ZTEXT) $(ZIGNORE) -M$(APIMAP)

LDLIBS += -ltopo -lnvpair -lc
LDLIBS32 += -L$(LROOTLIBDIR)
LDLIBS64 += -L$(LROOTLIBDIR)/$(MACH64)

DYNFLAGS32 += -R/usr/lib/fm
DYNFLAGS64 += -R/usr/lib/fm/$(MACH64)

all: $(PROG)

.NO_PARALLEL:
.PARALLEL: $(OBJS) $(LINTFILES)

$(PROG): $(OBJS) $(APIMAP)
	$(LINK.c) $(DYNFLAGS) $(OBJS) -o $@ $(LDLIBS)
	$(CTFMERGE) -L VERSION -o $@ $(OBJS)
	$(POST_PROCESS_SO)

%.o: $(SRC)/lib/fm/topo/modules/common/$(MODULE)/common/%.c
	$(COMPILE.c) $<
	$(CTFCONVERT_O)

%.o: $(SRC)/lib/fm/topo/modules/common/$(SHAREDMODULE)/common/%.c
	$(COMPILE.c) $<
	$(CTFCONVERT_O)

%.o: ../../$(MODULE)/common/%.c
	$(COMPILE.c) $<
	$(CTFCONVERT_O)

clean:
	$(RM) $(OBJS) $(LINTFILES) $(CLEANFILES)

clobber: clean
	$(RM) $(PROG)

%.ln: $(SRC)/lib/fm/topo/modules/common/$(MODULE)/common/%.c
	$(LINT.c) -c $<

%.ln: ../../$(MODULE)/common/%.c
	$(LINT.c) -c $<

lint: $(LINTFILES)
	$(LINT) $(LINTFLAGS) $(LINTFILES) $(LDLIBS)

check:	$(CHECKHDRS)

install_h:

$(ROOTPROG): $$(@D) $(PROG)
	$(RM) $@; $(INS) -s -m 0755 -f $(@D) $(PROG)

$(ROOTPROG64): $$(@D) $(PROG)
	$(RM) $@; $(INS) -s -m 0755 -f $(@D) $(PROG)

$(PLUGINDIR) :
	${INS.dir}

$(PLUGINDIR64) :
	${INS.dir}

$(PLUGINDIR)/% : %
	${INS.file}

$(PLUGINDIR64)/% : %
	${INS.file}
include ../../../../Makefile.rootdirs
