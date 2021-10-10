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

# ucbcmd/Makefile

# include global definitions; SRC should be defined in the shell.
# SRC is needed until RFE 1026993 is implemented.
include $(SRC)/Makefile.master
include $(SRC)/ucbcmd/Makefile.ucbcmd

SUBDIRS	=		\
	basename	\
	biff		\
	chown		\
	df		\
	du		\
	echo		\
	expr		\
	fastboot	\
	fasthalt	\
	file		\
	from		\
	groups		\
	install.d	\
	ln		\
	ls		\
	mkstr		\
	printenv	\
	rusage		\
	sed		\
	shutdown	\
	stty		\
	sum		\
	test		\
	touch		\
	tr		\
	tset		\
	users		\
	vipw		\
	whereis		\
	whoami

# commands messaged
MSGSUBDIRS = biff install.d mkstr rusage shutdown

BWOSDIRS=

all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint
_msg:=          TARGET= _msg

ROOTUSR = $(ROOT)/usr
DICTLINK = $(ROOTUSR)/dict

# Symbolic links for source compatibility.  Many utilities that
# were under /usr/ucb in SunOS 4.1, are now in the base;  the
# symbolic links are provided for compatibility.
LINKDEST=	../bin/$(@F)

BINLINKS=		\
	arch		\
	clear		\
	e		\
	edit		\
	ex		\
	vedit		\
	vi		\
	view		\
	finger		\
	fmt		\
	fold		\
	ftp		\
	head		\
	hostid		\
	hostname	\
	logger		\
	mach		\
	more		\
	page		\
	pagesize	\
	netstat		\
	quota		\
	rcp		\
	rdate		\
	renice		\
	rlogin		\
	rsh		\
	ruptime		\
	rwho		\
	script		\
	tail		\
	talk		\
	tcopy		\
	telnet		\
	tftp		\
	uptime		\
	vacation	\
	vmstat		\
	w		\
	wc		\
	whois		\
	xstr

ROOTBINLINKS = $(BINLINKS:%=$(ROOTBIN)/%)

OTHERLINKS = $(ROOTETC)/termcap

$(DICTLINK) :=		LINKDEST= share/lib/dict
$(ROOTBIN)/e :=		LINKDEST= ../bin/edit
$(ROOTBIN)/ex :=	LINKDEST= ../bin/edit
$(ROOTBIN)/page :=	LINKDEST= ../bin/more
$(ROOTBIN)/quota :=	LINKDEST= ../sbin/quota
$(ROOTETC)/termcap :=	LINKDEST= ../usr/share/lib/termcap
$(ROOTBIN)/vi :=	LINKDEST= ../bin/edit
$(ROOTBIN)/view :=	LINKDEST= ../bin/edit

.KEEP_STATE:

.PARALLEL:	$(SUBDIRS)

all clean clobber: $(SUBDIRS)

install: $(SUBDIRS) .WAIT rootlinks

# For messaging catalog
#
_msg:   $(MSGSUBDIRS)



bwos: $(BWOSDIRS) .WAIT $(SUBDIRS)

$(BWOSDIRS) $(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

rootlinks: $(DICTLINK) $(ROOTBINLINKS) $(OTHERLINKS)

$(DICTLINK) $(ROOTBINLINKS) $(OTHERLINKS):
	$(RM) $@; $(SYMLINK) $(LINKDEST) $@

FRC:
