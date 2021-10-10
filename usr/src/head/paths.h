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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _PATHS_H
#define	_PATHS_H

#include <sys/paths.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Definitions of pathnames for Solaris.  Pathnames used by the kernel
 * are defined in <sys/paths.h>
 */

#define	_PATH_BSHELL		"/usr/bin/sh"
#define	_PATH_CONSOLE		"/dev/console"
#define	_PATH_CONSOLE_USER_LINK	"/dev/vt/console_user"
#define	_PATH_CP		"/usr/bin/cp"
#define	_PATH_CSHELL		"/usr/bin/csh"
#define	_PATH_DEFAULT_LOGIN	"/etc/default/login"
#define	_PATH_DEVNULL		"/dev/null"
#define	_PATH_ED		"/usr/bin/ed"
#define	_PATH_ETHERS		"/etc/ethers"
#define	_PATH_GROUP		"/etc/group"
#define	_PATH_HEQUIV		"/etc/hosts.equiv"
#define	_PATH_HESIOD_CONF	"/etc/hesiod.conf"
#define	_PATH_HOSTS		"/etc/inet/hosts"
#define	_PATH_IPNODES		"/etc/inet/ipnodes"
#define	_PATH_IPSECALGS		"/etc/inet/ipsecalgs"
#define	_PATH_IRS_CONF		"/etc/irs.conf"
#define	_PATH_KMEM		"/dev/kmem"
#define	_PATH_LASTLOG		"/var/adm/lastlog"
#define	_PATH_MAILDIR		"/var/mail"
#define	_PATH_MSGLOG		"/dev/msglog"
#define	_PATH_NETGROUP		"/etc/netgroup"
#define	_PATH_NETMASKS		"/etc/netmasks"
#define	_PATH_NETWORKS		"/etc/networks"
#define	_PATH_NOLOGIN		"/etc/nologin"
#define	_PATH_OPENPROM		"/dev/openprom"
#define	_PATH_POWER_MGMT	"/dev/pm"
#define	_PATH_PROTOCOLS		"/etc/protocols"
#define	_PATH_RANDOM		"/dev/random"
#define	_PATH_RESCONF		"/etc/resolv.conf"
#define	_PATH_RSH		"/usr/bin/rsh"
#define	_PATH_SELF_AS_REQ	"/.SELF-ASSEMBLY-REQUIRED"
#define	_PATH_SERVICES		"/etc/services"
#define	_PATH_SHELLS		"/etc/shells"
#define	_PATH_SYSCON		"/dev/syscon"
#define	_PATH_SYSEVENT		"/dev/sysevent"
#define	_PATH_SYSMSG		"/dev/sysmsg"
#define	_PATH_SYSTTY		"/dev/systty"
#define	_PATH_TTY		"/dev/tty"
#define	_PATH_UNIX		"/dev/ksyms"
#define	_PATH_URANDOM		"/dev/urandom"
#define	_PATH_UTMP		do not use _PATH_UTMP
#define	_PATH_UTMPX		_PATH_SYSVOL "/utmpx"
#define	_PATH_UTMPX_OLD		"/var/adm/utmpx"
#define	_PATH_VI		"/usr/bin/vi"
#define	_PATH_WTMP		do not use _PATH_WTMP
#define	_PATH_WTMPX		"/var/adm/wtmpx"

/*
 * Directories; these are often concatenated to create filenames,
 * only in those cases they should end with a slash.  _PATH_SYSVOL
 * is use as a directory by itself and _PATH_VARRUN is always defined
 * with the trailing slash.
 */
#define	_PATH_TMP		"/tmp/"
#define	_PATH_USRTMP		_PATH_VARTMP
#define	_PATH_VARRUN		_PATH_SYSVOL "/"
#define	_PATH_VARTMP		"/var/tmp/"

#ifdef __cplusplus
}
#endif

#endif /* _PATHS_H */
