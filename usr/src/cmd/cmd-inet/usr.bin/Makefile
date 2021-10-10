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

PROG=		dns-sd finger rdate ruptime rwho whois
SUIDPROG=	rcp rlogin rsh
ALL=		$(PROG) $(SUIDPROG)
SRCS=		$(ALL:%=%.c)
KCMDPROGS=	rcp rlogin rsh

SUBDIRS=	chat ftp nc nca netstat \
		pppd pppdump pppstats talk telnet tftp
SUBDIR1=	talk 
MSGSUBDIRS=	nc nca talk

# As programs get lint-clean, add them here.  Eventually.
# This hack should go away, and all in PROG should be lint-clean.
LINTCLEAN=	rlogin.c rsh.c rcp.c rdate.c rwho.c

# Likewise, as subdirs get lint-clean, add them here.  Once
# they're all clean, replace the dependency of the lint target
# with SUBDIRS.  Also (sigh) deal with the commented-out build lines
# for the lint rule.
LINTSUBDIRS=	nca netstat pppd pppstats tftp ftp

include ../../Makefile.cmd
include ../Makefile.cmd-inet

COMMONOBJS=	kcmd.o
COMMONPOFILES=	$(COMMONOBJS:.o=.po)
COMMONSRCS=	$(CMDINETCOMMONDIR)/$(COMMONOBJS:.o=.c)

POFILES=	rlogin.po rsh.po rcp.po $(COMMONPOFILES)
POFILE=		usr.bin.po

all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint
_msg:=		TARGET= _msg

ROOTSUIDPROG=	$(SUIDPROG:%=$(ROOTBIN)/%)
$(ROOTSUIDPROG)	:=	FILEMODE=	04555

CPPFLAGS +=	-DSYSV -DSTRNET -DBSD_COMP -I$(CMDINETCOMMONDIR)

# Eventually just plain CFLAGS should be += -v, but not until all in
# PROGS are lint clean.
$(LINTCLEAN)	:=	CFLAGS += $(CCVERBOSE)

dns-sd :=	CFLAGS += $(C99_ENABLE)
finger :=	CFLAGS += $(CCVERBOSE)
# Enable large file support for reading the lastlog file.
finger :=	CPPFLAGS += -D_FILE_OFFSET_BITS=64

dns-sd :=		LDLIBS += -lsocket -ldns_sd
finger :=		LDLIBS += -lnsl -lcurses -lsocket
rcp lint-rcp :=		LDLIBS += -lsocket -lsec -lsendfile
rdate lint-rdate:=	LDLIBS += -lsocket
rlogin lint-rlogin :=	LDLIBS += -lnsl -lsocket
rsh lint-rsh :=		LDLIBS += -lsocket
whois :=		LDLIBS += -lsocket

include  $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5
$(KCMDPROGS)	:=	LDLIBS += -lnsl -lmech_krb5
$(KCMDPROGS)	:=	LDFLAGS += $(ZIGNORE) $(KRUNPATH) \
				-L$(LROOT)$(KLIBDIR_DO) \
				-L$(LROOT)$(KLIBDIR_GL)
KCMDLINTS=	$(KCMDPROGS:%=lint-%)

$(COMMONPOFILES) \
rlogin.po rcp.po rsh.po \
$(KCMDPROGS) \
$(KCMDLINTS)	:=	CPPFLAGS += -DKERBEROS \
			-I$(CMDINETCOMMONDIR) \
			-I$(SRC)/lib/gss_mechs/mech_krb5 \
			-I$(SRC)/uts/common/gssapi/mechs/krb5/include \
			-I$(SRC)/lib/gss_mechs/mech_krb5/include \
			-I$(SRC)/lib/gss_mechs/mech_krb5/include/krb5

# c99=%all supports the inline keyword used in some krb header files
$(KCMDPROGS)   := C99MODE = -xc99=%all

# "-erroff=E_NAME_USED_NOT_DEF2", "-erroff=E_NAME_DEF_NOT_USED2" and
# "-erroff=E_SUPPRESSION_DIRECTIVE_UNUSED" are required because lint problems
# in the Kerberos 5 framework.
$(KCMDLINTS)	:=	LINTFLAGS += -lnsl \
			-erroff=E_NAME_USED_NOT_DEF2 \
			-erroff=E_NAME_DEF_NOT_USED2 \
			-erroff=E_SUPPRESSION_DIRECTIVE_UNUSED

# Extra source files to lint with
LINTXTRA=
$(KCMDLINTS)	:=	LINTXTRA += $(COMMONSRCS)
# lint with -Xc99=%all supports the inline keyword used in some krb header
# files
$(KCMDLINTS)	:=	C99LMODE = -Xc99=%all

ROOTSUNWRCP=	$(ROOT)/usr/lib/sunw,rcp
ROOTRSHSYMLINK=	$(ROOT)/usr/ucb/rsh
ROOTREMSHSYMLINK=$(ROOT)/usr/bin/remsh

.KEEP_STATE:

all: $(ALL) $(SUBDIRS)

install: all .WAIT $(ROOTPROG) $(ROOTSUIDPROG) \
	$(SUBDIRS) $(ROOTSUNWRCP) $(ROOTRSHSYMLINK) $(ROOTREMSHSYMLINK)

# Messaging - copy $POFILES to $POFILE to work with the parent directory
# Makefile's '_msg' target.
#
_msg: $(MSGSUBDIRS) $(POFILES)
	$(RM) $(POFILE)
	$(CAT) $(POFILES)	> $(POFILE)

$(COMMONPOFILES): $(COMMONSRCS)
	$(COMPILE.cpp) $(COMMONSRCS) > $(@:.po=.c).i
	$(XGETTEXT) $(XGETFLAGS) $(@:.po=.c).i
	$(RM) $@
	sed "/^domain/d" < messages.po  > $@
	$(RM) messages.po $(@:.po=.c).i

$(COMMONOBJS): $(COMMONSRCS)
	$(COMPILE.c) $(COMMONSRCS)

rlogin: rlogin.o $(COMMONOBJS)
	$(LINK.c) $@.o $(COMMONOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)
 
rcp: rcp.o $(COMMONOBJS)
	$(LINK.c) $@.o $(COMMONOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

rsh: rsh.o $(COMMONOBJS)
	$(LINK.c) $@.o $(COMMONOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)
 
$(ROOTSUNWRCP):
	$(RM) $@; $(SYMLINK) ../bin/rcp $@

$(ROOTRSHSYMLINK):
	$(RM) $@; $(SYMLINK) ../bin/rsh $@

$(ROOTREMSHSYMLINK):
	$(RM) $@; $(SYMLINK) rsh $@

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(MFLAGS) $(TARGET)

FRC:

clean: $(SUBDIRS)

clobber: $(SUBDIRS) clobber_local

clobber_local:
	echo $(CLOBBERFILES)
	$(RM) $(ALL)  $(CLOBBERFILES) 


LINTLOCALS=	$(LINTCLEAN:%.c=lint-%)

lint:	$(LINTSUBDIRS) $(LINTLOCALS)

$(LINTLOCALS):
	$(LINT.c) $(@:lint-%=%.c) $(LINTXTRA) $(LDLIBS)
