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

# Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.

#
# lib/libsecdb/help/auths/Makefile
#

include ../../../../Makefile.master

HTMLENTS = \
	AuditHeader.html \
	DevAllocHeader.html \
	DevAllocate.html \
	DevConfig.html \
	DevCDRW.html \
	DevRevoke.html \
	HotplugHeader.html \
	HotplugModify.html \
	JobHeader.html \
	AuthJobsAdmin.html \
	AuthJobsUser.html \
	MailHeader.html \
	MailQueue.html \
	RoleHeader.html \
	SysDate.html \
	SysHeader.html \
	SysShutdown.html \
	AllSolAuthsHeader.html \
	SysMaintenance.html \
	DhcpmgrHeader.html \
	DhcpmgrWrite.html \
	BindStates.html \
	SmfAllocate.html \
	SmfAutofsStates.html \
	SmfCoreadmStates.html \
	SmfCronStates.html \
	SmfEnvironment.html \
	SmfExAcctFlowStates.html \
	SmfExAcctProcessStates.html \
	SmfExAcctTaskStates.html \
	SmfExAcctNetStates.html \
	SmfHeader.html \
	SmfILBStates.html \
	SmfInetdStates.html \
	SmfIPsecStates.html \
	SmfLLDPStates.html \
	SmfLocationStates.html \
	SmfManageAudit.html \
	SmfManageHeader.html \
	SmfManageHotplug.html \
	SmfMDNSStates.html \
	SmfIpmpStates.html \
	SmfModifyAppl.html \
	SmfModifyDepend.html \
	SmfModifyFramework.html \
	SmfModifyHeader.html \
	SmfModifyMethod.html \
	SmfNetPhysStates.html \
	SmfNsDNSClientStates.html \
	SmfNsLDAPClientStates.html \
	SmfNsNISClientStates.html \
	SmfNsNISDomainStates.html \
	SmfNsNISPasswdStates.html \
	SmfNsNISServerStates.html \
	SmfNsNISStates.html \
	SmfNsNISUpdateStates.html \
	SmfNsNISXfrStates.html \
	SmfNsStates.html \
	SmfNscdStates.html \
	SmfNsswitchStates.html \
	SmfNDMPStates.html \
	SmfReparseStates.html \
	SmfRoutingStates.html \
	SmfSendmailStates.html \
	SmfShareGroupStates.html \
	SmfShareStates.html \
	SmfSshStates.html \
	SmfSyslogStates.html \
	SmfValueAudit.html \
	SmfValueCoreadm.html \
	SmfValueExAcctFlow.html \
	SmfValueExAcctProcess.html \
	SmfValueExAcctTask.html \
	SmfValueExAcctNet.html \
	SmfValueFinger.html \
	SmfValueFirewall.html \
	SmfVbiosdStates.html \
	SmfVtStates.html \
	SmfValueEnvironment.html \
	SmfValueHeader.html \
	SmfValueInetd.html \
	SmfValueIPsec.html \
	SmfValueMDNS.html \
	SmfValueNDMP.html \
	AuthReadNDMP.html \
	SmfValueNs.html \
	SmfValueNsDNSClient.html \
	SmfValueNsLDAP.html \
	SmfReadNsLDAP.html \
	SmfValueNsNIS.html \
	SmfValueNsNISClient.html \
	SmfValueNsNISDomain.html \
	SmfValueNsNISServer.html \
	SmfValueNscd.html \
	SmfValueNsswitch.html \
	SmfValueNWAM.html \
	SmfValueRouting.html \
	SmfValueSMB.html \
	AuthReadSMB.html \
	SmfSMBFSStates.html \
	SmfSMBStates.html \
	SmfValueVscan.html \
	SmfVscanStates.html \
	SmfValueVbiosd.html \
	SmfValueVt.html	\
	SmfVRRPStates.html \
	SmfWpaStates.html \
	NetworkAutoconfRead.html \
	NetworkAutoconfSelect.html \
	NetworkAutoconfWlan.html \
	NetworkAutoconfWrite.html \
	NetworkHeader.html \
	NetworkILBconf.html \
	NetworkILBenable.html \
	NetworkInterfaceConfig.html \
	NetworkLLDP.html \
	NetworkVRRP.html \
	WifiConfig.html \
	WifiWep.html \
	LinkSecurity.html \
	IdmapRules.html \
	SmfIdmapStates.html \
	SmfValueIdmap.html \
	FileChown.html \
	FileHeader.html \
	FileOwner.html \
	LabelFileDowngrade.html \
	LabelFileUpgrade.html \
	LabelHeader.html \
	LabelRange.html \
	LabelServer.html \
	LabelWinDowngrade.html \
	LabelWinNoView.html \
	LabelWinUpgrade.html \
	TNDaemon.html \
	TNctl.html \
	ValueTND.html \
	SysPowerMgmtHeader.html \
	SysPowerMgmtSuspend.html \
	SysPowerMgmtSuspendtoDisk.html \
	SysPowerMgmtSuspendtoRAM.html \
	SysPowerMgmtBrightness.html \
	SysCpuPowerMgmt.html \
	SysSyseventRead.html \
	SysSyseventWrite.html \
	SmfManageZFSSnap.html \
	VntsdConsoles.html \
	VntsdGrant.html \
	VntsdHeader.html \
	ZoneCloneFrom.html \
	ZoneHeader.html \
	ZoneLogin.html \
	ZoneManage.html \
	AuthAssign.html \
	AuthDelegate.html \
	AuthHeader.html \
	AuthManage.html \
	GroupHeader.html \
	GroupAssign.html \
	GroupDelegate.html \
	GroupManage.html \
	LabelDelegate.html \
	LabelNetworkManage.html \
	LabelZoneManage.html \
	PrivilegeAssign.html \
	PrivilegeDelegate.html \
	ProjectAssign.html \
	ProjectDelegate.html \
	ProjectHeader.html \
	ProjectManage.html \
	RoleAssign.html \
	RoleDelegate.html \
	RoleManage.html \
	UserManage.html \
	AuditAssign.html \
	ProfileHeader.html \
	ProfileManage.html \
	ProfileAssign.html \
	ProfileDelegate.html \
	ProfileCommandManage.html \
	ProfileCommandSetuid.html \
	SessionSetPolicy.html \
	AccountActivate.html \
	AccountSetPolicy.html \
	AccountHeader.html \
	PrivilegeHeader.html \
	ProjectHeader.html \
	MediaHeader.html \
	MediaExtract.html \
	SmfValuePMConf.html \
	SmfValuePMCont.html

HELPDIR=$(ROOT)/usr/lib/help
AUTHDIR=$(HELPDIR)/auths
LOCALEDIR=$(AUTHDIR)/locale
CDIR=$(LOCALEDIR)/C
DIRS=$(HELPDIR) $(AUTHDIR) $(LOCALEDIR) $(CDIR)
HELPFILES=$(HTMLENTS:%=$(CDIR)/%)

MSGDIR=		$(LOCALEDIR)
MSGDIRS =       $(HELPDIR) $(AUTHDIR) $(LOCALEDIR)

MSGFILES=	$(HTMLENTS)
MSGS=		$(MSGFILES:%=$(MSGDIR)/%)

FILEMODE= 0444

.KEEP_STATE:

all:	$(HTMLENTS)

install:	all $(DIRS) $(HELPFILES)

_msg: $(MSGDIRS) $(MSGS)

$(CDIR)/%: %
	$(INS.file)

$(DIRS):
	$(INS.dir)

$(MSGDIR)/%: %
	$(INS.file)

clean clobber lint:
