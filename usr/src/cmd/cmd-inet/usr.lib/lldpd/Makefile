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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

PROG =		lldpd

DSRC =		lldp_provider.d
DTRACE_HEADER =	$(DSRC:%.d=%.h)

COBJS =		lldp_main.o lldp_door.o lldp_scf.o lldp_tlv.o lldp_dlpi.o \
		lldp_util.o lldp_rx.o lldp_tx.o lldp_timer.o \
		lldpsnmp_init.o lldpsnmp_stats.o lldpsnmp_locdata.o \
		lldpsnmp_remdata.o lldpsnmp_config.o lldpsnmp_util.o \
		lldp_events.o dcbx_main.o dcbx_ssm.o dcbx_pfc.o dcbx_appln.o

OBJS =		$(COBJS) $(DSRC:%.d=%.o)
SRCS =		$(COBJS:%.o=%.c)

MANIFEST =	lldp.xml
SVCMETHOD =	svc-lldpd

include		../../../Makefile.cmd

DMOD =		lldpd.so
ROOTDMOD =	$(ROOT)/usr/lib/mdb/proc/$(DMOD)
DMOD_SRCS =	lldp_mdb.c
DMOD_OBJS =	$(DMOD_SRCS:%.c=%.o)
DMOD_LINT =	$(DMOD_SRCS:%.c=%.ln)
DMOD_LDLIBS=	-L$(LROOT)/lib -lcmdutils -lc
MAPFILE-DMOD =	$(SRC)/cmd/mdb/common/modules/conf/mapfile-extern

ROOTMANIFESTDIR =	$(ROOTSVCNETWORK)

# No lint libraries are delivered for Net-SNMP yet
SNMPLIBS =	 -lnetsnmpagent -lnetsnmphelpers -lnetsnmp
lint :=		SNMPLIBS =

LDLIBS +=	$(SNMPLIBS) -lsocket -lnsl -ldlpi -ldladm -lumem \
		-lnvpair -llldp -lsysevent -lscf -lfcoe -lcmdutils

# Net-SNMP's headers use do {} while (0) a lot
LINTFLAGS += -msux -xerroff=E_CONSTANT_CONDITION

$(DMOD) := CFLAGS += $(CC_PICFLAGS) -G $(XREGSFLAG) -g
$(DMOD) := LDFLAGS += $(ZTEXT) $(ZDEFS) $(MAPFILE-DMOD:%=-M%)

#
# Instrument lldpd with CTF data to ease debugging.
#
include		../../../Makefile.ctf
CTFCONVERT_HOOK = && $(CTFCONVERT_O)
CTFMERGE_HOOK = && $(CTFMERGE) -L VERSION -o $@ $(OBJS)
$(OBJS) := CFLAGS += $(CTF_FLAGS)
$(PARFAIT_BUILD)__GNUC=
$(PARFAIT_BUILD)__GNUC64=

.KEEP_STATE:

all: $(PROG) $(DMOD)

$(PROG): $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS) $(CTFMERGE_HOOK)
	$(POST_PROCESS)

%.h: %.d
	$(DTRACE) -xnolibs -h -s $< -o $@

%.o:	%.c $(DTRACE_HEADER)
	$(COMPILE.c) $<
	$(POST_PROCESS_O)

%.o:	%.d $(COBJS)
	$(COMPILE.d) -xnolibs -s $< $(COBJS)
	$(POST_PROCESS_O)

$(DMOD): $(DMOD_OBJS)
	$(LINK.c) $(DMOD_OBJS) -o $@ $(DMOD_LDLIBS)
	$(POST_PROCESS_SO)

install: all $(ROOTLIBPROG) $(ROOTMANIFEST) $(ROOTSVCMETHOD) $(ROOTDMOD)

check:	$(CHKMANIFEST)

clean:
	$(RM) $(OBJS) $(DMOD_OBJS) $(DMOD_LINT) $(DTRACE_HEADER)

lint_dmod: $(DMOD_LINT)
	$(LINT) $(LINTFLAGS) $(DMOD_LINT) $(LDLIBS)

lint:	lint_SRCS lint_dmod

$(ROOTDMOD): $(ROOT)/usr/lib/mdb/proc $(DMOD)
	$(RM) $@; $(INS) -s -m 0555 -f $(@D) $(DMOD)

include		../../../Makefile.targ
