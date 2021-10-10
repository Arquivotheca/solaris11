/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * This is an internal header file. Not to be shipped.
 */

#ifndef	_AUTH_LIST_H
#define	_AUTH_LIST_H

#ifdef	__cplusplus
extern "C" {
#endif


/*
 * Names of authorizations currently in use in the system
 */

#define	AUTOCONF_READ_AUTH	"solaris.network.autoconf.read"
#define	AUTOCONF_SELECT_AUTH	"solaris.network.autoconf.select"
#define	AUTOCONF_WLAN_AUTH	"solaris.network.autoconf.wlan"
#define	AUTOCONF_WRITE_AUTH	"solaris.network.autoconf.write"
#define	CDRW_AUTH		"solaris.device.cdrw"
#define	CRONADMIN_AUTH		"solaris.jobs.admin"
#define	CRONUSER_AUTH		"solaris.jobs.user"
#define	DEFAULT_DEV_ALLOC_AUTH	"solaris.device.allocate"
#define	DEVICE_REVOKE_AUTH	"solaris.device.revoke"
#define	LINK_SEC_AUTH		"solaris.network.link.security"
#define	MAILQ_AUTH		"solaris.mail.mailq"
#define	NET_ILB_CONFIG_AUTH	"solaris.network.ilb.config"
#define	NET_ILB_ENABLE_AUTH	"solaris.network.ilb.enable"
#define	SET_DATE_AUTH		"solaris.system.date"
#define	WIFI_CONFIG_AUTH	"solaris.network.wifi.config"
#define	WIFI_WEP_AUTH		"solaris.network.wifi.wep"
#define	HP_MODIFY_AUTH		"solaris.hotplug.modify"

/*
 * The following authorizations can be qualified by appending <zonename>
 */
#define	ZONE_CLONEFROM_AUTH	"solaris.zone.clonefrom"
#define	ZONE_LOGIN_AUTH		"solaris.zone.login"
#define	ZONE_MANAGE_AUTH	"solaris.zone.manage"

#define	ZONE_AUTH_PREFIX	"solaris.zone."

/*
 * Authorizations used by Trusted Extensions.
 */
#define	BYPASS_FILE_VIEW_AUTH	"solaris.label.win.noview"
#define	DEVICE_CONFIG_AUTH	"solaris.device.config"
#define	FILE_CHOWN_AUTH		"solaris.file.chown"
#define	FILE_DOWNGRADE_SL_AUTH	"solaris.label.file.downgrade"
#define	FILE_OWNER_AUTH		"solaris.file.owner"
#define	FILE_UPGRADE_SL_AUTH	"solaris.label.file.upgrade"
#define	MAINTENANCE_AUTH	"solaris.system.maintenance"
#define	SHUTDOWN_AUTH		"solaris.system.shutdown"
#define	SYS_ACCRED_SET_AUTH	"solaris.label.range"
#define	SYSEVENT_READ_AUTH	"solaris.system.sysevent.read"
#define	SYSEVENT_WRITE_AUTH	"solaris.system.sysevent.write"
#define	WIN_DOWNGRADE_SL_AUTH	"solaris.label.win.downgrade"
#define	WIN_UPGRADE_SL_AUTH	"solaris.label.win.upgrade"

/*
 * Authorizations to manage User/RBAC administration.
 */
#define	ACCOUNT_ACTIVATE_AUTH	"solaris.account.activate"
#define	ACCOUNT_SETPOLICY_AUTH	"solaris.account.setpolicy"
#define	AUDIT_ASSIGN_AUTH	"solaris.audit.assign"
#define	AUTHORIZATION_ASSIGN_AUTH	"solaris.auth.assign"
#define	AUTHORIZATION_DELEGATE_AUTH	"solaris.auth.delegate"
#define	AUTHORIZATION_MANAGE_AUTH	"solaris.auth.manage"
#define	GROUP_ASSIGN_AUTH	"solaris.group.assign"
#define	GROUP_DELEGATE_AUTH	"solaris.group.delegate"
#define	GROUP_MANAGE_AUTH	"solaris.group.manage"
#define	LABEL_DELEGATE_AUTH	"solaris.label.delegate"
#define	MEDIA_EXTRACT_AUTH	"solaris.media.extract"
#define	PASSWD_ASSIGN_AUTH	"solaris.passwd.assign"
#define	PRIVILEGE_ASSIGN_AUTH	"solaris.privilege.assign"
#define	PRIVILEGE_DELEGATE_AUTH	"solaris.privilege.delegate"
#define	PROJECT_ASSIGN_AUTH	"solaris.project.assign"
#define	PROJECT_DELEGATE_AUTH	"solaris.project.delegate"
#define	PROJECT_MANAGE_AUTH	"solaris.project.manage"
#define	PROFILE_ASSIGN_AUTH	"solaris.profile.assign"
#define	PROFILE_DELEGATE_AUTH	"solaris.profile.delegate"
#define	PROFILE_MANAGE_AUTH	"solaris.profile.manage"
#define	PROFILE_CMD_MANAGE_AUTH	"solaris.profile.cmd.manage"
#define	PROFILE_CMD_SETUID_AUTH	"solaris.profile.cmd.setuid"
#define	ROLE_ASSIGN_AUTH	"solaris.role.assign"
#define	ROLE_DELEGATE_AUTH	"solaris.role.delegate"
#define	ROLE_MANAGE_AUTH	"solaris.role.manage"
#define	SESSION_SETPOLICY_AUTH	"solaris.session.setpolicy"
#define	USER_MANAGE_AUTH	"solaris.user.manage"

#ifdef	__cplusplus
}
#endif

#endif	/* _AUTH_LIST_H */
