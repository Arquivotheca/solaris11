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

include ../Makefile.master

#	Note that libcurses installs commands along with its library.
#	This is a minor bug which probably should be fixed.
#	Note also that a few extra libraries are kept in cmd source.
#
# Certain libraries are linked with, hence depend on, other libraries.
#
# We have historically used .WAIT to express dependencies, with the
# undesirable effect of reducing the amount of parallelism achieved,
# and lengthening the time it takes to build the libraries. To minimize
# this, we would also express makefile dependencies between the objects
# that would otherwise have had .WAIT barriers between them. This approach
# was limiting:
#
#    - The make rules are complex, change over time, and errors are not
#      automatically detectable. As such, the rules at any given time are
#      likely to be at best a close approximation to the real situation.
#
#    - Requiring a build order means that objects cannot have a mutual
#      dependency on each other, even in cases where the runtime linker
#      would have no issue with them.
#
#    - There is still serialization in the builds, though less than there
#      would be otherwise.
#
# We therefore now use stub shared objects, which can be built very quickly,
# and which eliminate the need for ordering. Before starting to build the
# real objects, we create an alternative proto root containing stub versions
# of the shared objects built by this consolidation. Then, we proceed to build
# most of the real objects in the workspace in parallel, using the stub
# objects at link-time.
#
.PARALLEL:

#
# The $(CLOSED_BUILD) additions to SUBDIRS & MSGSUBDIRS are unfortunate,
# but required due to the "dependencies" of using .WAIT to barrier the
# parallel dmake builds.  once 4631488 has been fixed, they can be
# consolidated into one $(CLOSED_BUILD)SUBDIRS += (all closed libs) as
# shown in HDRSUBDIRS
#
SUBDIRS= \
	common				\
	../cmd/sgs/libconv		\
	../cmd/sgs/libdl

$(CLOSED_BUILD)SUBDIRS += \
	$(CLOSED)/lib/libc_i18n

SUBDIRS += \
	libc				\
	../cmd/sgs/libelf		\
	c_synonyms			\
	libmd				\
	libmd5				\
	libmp				\
	libnsl				\
	libsecdb			\
	librpcsvc			\
	libsocket			\
	libsctp			\
	libsip			\
	libcommputil		\
	libresolv			\
	libresolv2			\
	libw				\
	libintl				\
	../cmd/sgs/librtld_db	\
	libaio			\
	libast			\
	libdll			\
	libcmd			\
	libshell		\
	libsum			\
	librt			\
	libadm			\
	libctf			\
	libdtrace		\
	libdtrace_jni		\
	libcurses		\
	libfuse			\
	libgen			\
	libgss			\
	libpam			\
	libuuid			\
	libthread		\
	libpthread		\
	libslp		\
	libbsdmalloc	\
	libdoor		\
	libdevinfo	\
	libdladm	\
	libdlpi		\
	libeti		\
	libcrypt	\
	libdns_sd	\
	libefi		\
	libfstyp	\
	libwanboot	\
	libwanbootutil	\
	libcryptoutil	\
	libinetutil	\
	libipadm	\
	libipmp		\
	libiscsit	\
	libkmf		\
	libsoftcrypto
$(CLOSED_BUILD)SUBDIRS += \
	$(CLOSED)/lib/libkmsagent
SUBDIRS += \
	libkstat	\
	libkvm		\
	liblm		\
	libmalloc	\
	libmapmalloc	\
	libmtmalloc	\
	libnls		\
	libnwam		\
	libsmbios	\
	libtecla	\
	libumem		\
	libnvpair	\
	libexacct	\
	libplot		\
	libldap4	\
	libsasl		\
	libldap5	\
	libsldap	\
	libbsm		\
	libdevalloc	\
	libsys		\
	libsysevent	\
	libnisdb	\
	libpool		\
	libpp		\
	libproc		\
	libproject	\
	libsendfile	\
	nametoaddr	\
	ncad_addr	\
	hbaapi		\
	smhba		\
	sun_fc		\
	sun_sas		\
	gss_mechs/mech_krb5	\
	libkrb5		\
	krb5		\
	libndr		\
	libntsvcs	\
	libsmb		\
	libsmbfs	\
	libsmbns	\
	libsmbrp	\
	libfcoe		\
	libsrpt		\
	libstmf		\
	libstmfproxy	\
	libnsctl	\
	libunistat	\
	libdscfg	\
	librdc		\
	libinstzones	\
	libpkg		\
	libiov		\
	libuvfs		

SUBDIRS += \
	passwdutil	\
	pam_modules	\
	crypt_modules	\
	libadt_jni	\
	abi		\
	auditd_plugins	\
	libvolmgt	\
	libdevice	\
	libdevid	\
	libdhcpsvc	\
	libc_db		\
	libndmp		\
	libsec		\
	libdhcpagent	\
	libdhcpdu	\
	libdhcputil	\
	libxnet		\
	libipsecutil
$(CLOSED_BUILD)SUBDIRS += \
	$(CLOSED)/lib/libike
SUBDIRS += \
	nsswitch	\
	print		\
	libuutil	\
	libscf		\
	libinetsvc	\
	librestart	\
	libsched	\
	libelfsign	\
	pkcs11			\
	libpctx			\
	libcpc		\
	watchmalloc	\
	extendedFILE	\
	madv		\
	mpss		\
	libdisasm	\
	libwrap		\
	libxcurses	\
	libxcurses2	\
	libbrand	\
	libzonecfg	\
	libzoneinfo	\
	libzonestat	\
	libtsnet	\
	libtsol		\
	gss_mechs/mech_spnego	\
	gss_mechs/mech_dummy	\
	gss_mechs/mech_dh	\
	rpcsec_gss	\
	libraidcfg	\
	librcm		\
	libcfgadm	\
	libpicl		\
	libpicltree	\
	raidcfg_plugins	\
	cfgadm_plugins	\
	libmail		\
	lvm		\
	libsmedia	\
	libipp		\
	libdiskmgt	\
	liblgrp		\
	liblldp		\
	libfsmgt	\
	fm		\
	libavl		\
	libcmdutils	\
	libcontract	\
	../cmd/sendmail/libmilter	\
	sasl_plugins	\
	udapl		\
	libzpool	\
	libzfs		\
	libbe 		\
	pylibbe 	\
	libzfs_jni	\
	pyzfs		\
	pysolaris	\
	libmapid	\
	brand		\
	policykit	\
	hal		\
	libshare	\
	libshare_plugins	\
	libsqlite	\
	libidmap	\
	libadutils	\
	libipmi		\
	libexacct/demo	\
	libvrrpadm	\
	libvscan	\
	libgrubmgmt	\
	libilb		\
	scsi		\
	libima		\
	libsun_ima	\
	mpapi		\
	librstp		\
	libreparse	\
	libhotplug	\
	libfruutils	\
	libfru		\
	libshadowfs	\
	libshadowtest	\
	libpower	\
	$($(MACH)_SUBDIRS)

i386_SUBDIRS=		\
	libntfs		\
	libparted	\
	libfdisk

sparc_SUBDIRS=		\
	efcode		\
	libds		\
	libdscp		\
	libprtdiag	\
	libprtdiag_psr	\
	libpri		\
	librsc		\
	storage		\
	libpcp		\
	libtsalarm	\
	libv12n

FM_sparc_DEPLIBS= libpri

fm:			\
	libexacct	\
	libipmi		\
	libzfs		\
	scsi		\
	$(FM_$(MACH)_DEPLIBS)

DCSUBDIRS =		\
	lvm

MSGSUBDIRS=		\
	abi		\
	auditd_plugins	\
	brand		\
	cfgadm_plugins	\
	gss_mechs/mech_dh	\
	gss_mechs/mech_krb5	\
	krb5		\
	libast		\
	libbsm		\
	libc		\
	libcfgadm	\
	libcmd		\
	libcontract	\
	libcurses	\
	libdhcpsvc 	\
	libdhcputil	\
	libfuse		\
	libipsecutil	\
	libdiskmgt	\
	libdladm	\
	libdll		\
	libgrubmgmt	\
	libgss		\
	libidmap	\
	libipmp		\
	libilb		\
	libinetutil	\
	libinstzones	\
	libipadm	\
	libnsl		\
	libnwam		\
	libpam		\
	libpicl		\
	libpool		\
	libpkg		\
	libpp		\
	libscf		\
	libsasl		\
	libldap5	\
	libsecdb	\
	libshadowfs	\
	libshare	\
	libshare_plugins	\
	libshell	\
	libsldap	\
	libslp		\
	libsmbfs	\
	libsmedia	\
	libsum		\
	libtsol		\
	libuutil	\
	libvrrpadm	\
	libvscan	\
	libwanboot	\
	libwanbootutil	\
	libzfs		\
	libzonecfg	\
	lvm		\
	madv		\
	mpss		\
	pam_modules	\
	pyzfs		\
	pysolaris	\
	rpcsec_gss	\
	libreparse
MSGSUBDIRS += \
	$($(MACH)_MSGSUBDIRS)

sparc_MSGSUBDIRS=	\
	libprtdiag	\
	libprtdiag_psr

i386_MSGSUBDIRS= libfdisk

HDRSUBDIRS=		\
	auditd_plugins	\
	libast		\
	libbe		\
	libbrand	\
	libbsm		\
	libc		\
	libcmd		\
	libcmdutils	\
	libcommputil	\
	libcontract	\
	libcpc		\
	libctf		\
	libcurses	\
	libcryptoutil	\
	libdevalloc	\
	libdevice	\
	libdevid	\
	libdevinfo	\
	libdiskmgt	\
	libdladm	\
	libdll		\
	libdlpi		\
	libdhcpagent	\
	libdhcpsvc	\
	libdhcputil	\
	libdisasm	\
	libdns_sd	\
	libdscfg	\
	libdtrace	\
	libdtrace_jni	\
	libelfsign	\
	libeti		\
	libfru		\
	libfuse		\
	libfstyp	\
	libgen		\
	libhotplug	\
	libipadm	\
	libipsecutil	\
	libinetsvc	\
	libinetutil	\
	libinstzones	\
	libipmi		\
	libipmp		\
	libipp		\
	libiscsit	\
	libkmf		\
	libkstat	\
	libkvm		\
	liblldp		\
	libmail		\
	libmd		\
	libmtmalloc	\
	libndmp		\
	libnvpair	\
	libnsctl	\
	libnsl		\
	libnwam		\
	libpam		\
	libpctx		\
	libpicl		\
	libpicltree	\
	libplot		\
	libpool		\
	libpower	\
	libpp		\
	libproc		\
	libraidcfg	\
	librcm		\
	librdc		\
	libscf		\
	libsip		\
	libsmbios	\
	libsoftcrypto	\
	librestart	\
	librpcsvc	\
	librstp		\
	libsasl		\
	libsec		\
	libshell	\
	libslp		\
	libsmedia	\
	libsocket	\
	libsqlite	\
	libfcoe		\
	libsrpt		\
	libstmf		\
	libstmfproxy	\
	libsum		\
	libsysevent	\
	libtecla	\
	libtsnet	\
	libtsol		\
	libvrrpadm	\
	libvolmgt	\
	libumem		\
	libunistat	\
	libuutil	\
	libuvfs		\
	libwanboot	\
	libwanbootutil	\
	libwrap		\
	libxcurses2	\
	libzfs		\
	libzfs_jni	\
	libzoneinfo	\
	libzonestat	\
	hal		\
	policykit	\
	lvm		\
	pkcs11		\
	passwdutil	\
	../cmd/sendmail/libmilter	\
	fm		\
	udapl		\
	libmapid	\
	libkrb5		\
	libndr		\
	libntsvcs	\
	libsmb		\
	libsmbfs	\
	libsmbns	\
	libsmbrp	\
	libshare	\
	libidmap	\
	libvscan	\
	libgrubmgmt	\
	libilb		\
	scsi		\
	hbaapi		\
	smhba		\
	libima		\
	libsun_ima	\
	mpapi		\
	libreparse	\
	libiov		\
	libshadowfs	\
	libshadowtest	\
	$($(MACH)_HDRSUBDIRS)

$(CLOSED_BUILD)HDRSUBDIRS += \
	$(CLOSED)/lib/libc_i18n	\
	$(CLOSED)/lib/libike

i386_HDRSUBDIRS=	\
	libparted	\
	libfdisk

sparc_HDRSUBDIRS=	\
	libds		\
	libdscp		\
	libpri		\
	libv12n		\
	storage

all :=		TARGET= all
check :=	TARGET= check
clean :=	TARGET= clean
clobber :=	TARGET= clobber
install :=	TARGET= install
install_h :=	TARGET= install_h
stub :=		TARGET= stub
stubinstall :=	TARGET= stubinstall
lint :=		TARGET= lint
_dc :=		TARGET= _dc
_msg :=		TARGET= _msg

.KEEP_STATE:

all clean clobber lint stub:	$(SUBDIRS)

install:		$(SUBDIRS) install_extra

stubinstall:	$(SUBDIRS) stubinstall_extra

# extra libraries kept in other source areas
install_extra stubinstall_extra:
	@cd ../cmd/sgs; pwd; $(MAKE) $@
	@pwd

install_h check:	$(HDRSUBDIRS)

_msg:			$(MSGSUBDIRS) .WAIT _dc

_dc:			$(DCSUBDIRS)

#
# Library interdependencies are called out explicitly here. Due to the
# use of stub objects, this should only be necessary when an object
# depends on something other than a shared object, such as compiled
# objects from usr/closed.
#

#
# libc and libike depend on closed bits
#
$(CLOSED_BUILD)libc:		$(CLOSED)/lib/libc_i18n
$(CLOSED_BUILD)libike:	libipsecutil libxnet libcryptoutil
$(CLOSED_BUILD)libkmsagent:	libsoftcrypto libpam

#
# These sgs libraries require the libconv.a archive to be built first.
#
../cmd/sgs/libdl:	../cmd/sgs/libconv
../cmd/sgs/libelf:	../cmd/sgs/libconv
../cmd/sgs/librtld_db:	../cmd/sgs/libconv

#
# libscf/native requires libuutil/native to be built first
#
libscf: libuutil


#
# The reason this rule checks for the existence of the
# Makefile is that some of the directories do not exist
# in certain situations (e.g., exportable source builds,
# OpenSolaris).
#
$(SUBDIRS): FRC
	@if [ -f $@/Makefile  ]; then \
		cd $@; pwd; $(MAKE) $(TARGET); \
	else \
		true; \
	fi

FRC:
