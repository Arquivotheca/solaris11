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
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */
#include <stdarg.h>
#include <libintl.h>
#include "users.h"

extern	char	*cmdname;

char *errmsgs[] = {
	"WARNING: uid %ld is reserved.\n",
	"WARNING: more than NGROUPS_MAX(%d) groups specified.\n",
	"ERROR: Invalid syntax.\n"
	"usage:  useradd [-u uid [-o] | -g group | -G group[[,group]...] |"
	"-d dir | -b base_dir |\n"
	"\t\t-s shell | -c comment | -m [-k skel_dir] | -f inactive |\n"
	"\t\t-e expire | -A authorization[[,authorization]...] |\n"
	"\t\t-P profile[[,profile]...] | -R role[[,role]...] |\n"
	"\t\t-K key=value | -p project[[,project]...] |\n"
	"\t\t[-S [files | ldap]] login\n"
	"\tuseradd -D [-g group | -b base_dir | -f inactive | -e expire\n"
	"\t\t-A authorization [[,authorization]...] |\n"
	"\t\t-P profile [[,profile]...] | -R role [[,role]...] |\n"
	"\t\t-K key=value ... -p project] | [-s shell] | [-k skel_dir]\n",
	"ERROR: Invalid syntax.\nusage:  userdel [-r] [-S files| ldap] login\n",
	"ERROR: Invalid syntax.\n"
	"usage:  usermod -u uid [-o] | -g group |"
	"-G [+|-]group[[,group]...] |\n"
	"\t\t-d dir [-m] | -s shell | -c comment |\n"
	"\t\t-l new_logname | -f inactive | -e expire |\n"
	"\t\t-A [+|-]authorization[[,authorization]...] |"
	"-K key[+|-]=value ... |\n"
	"\t\t-P [+|-]profile[[,profile]...] | -R [+|-]role[[,role]...] |\n"
	"\t\t[-S [files | ldap]] login\n",
	"ERROR: Unexpected failure.  Defaults unchanged.\n",
	"ERROR: Unable to remove files from home directory.\n",
	"ERROR: Unable to remove home directory. %s\n",
	"ERROR: Cannot update system files - login cannot be created.\n",
	"ERROR: uid %ld is already in use.  Choose another.\n",
	"ERROR: %s is already in use.  Choose another.\n",
	"ERROR: %s does not exist in the %s repository.\n",
	"ERROR: '%s' is not a valid %s.  Choose another.\n",
	"ERROR: %s is in use.  Cannot %s it.\n",
	"WARNING: %s has no permissions to use %s.\n",
	"ERROR: There is not sufficient space to move %s home directory to %s"
	"\n",
	"ERROR: %s %ld is too big.  Choose another.\n",
	"ERROR: group %s does not exist.  Choose another.\n",
	"ERROR: %s is not a valid path name.  Choose another.\n",
	"ERROR: %s is the primary group name.  Choose another.\n",
	"ERROR: Inconsistent password files.  See pwconv(1M).\n",
	"ERROR: %s is not a local user.\n",
	"ERROR: Permission denied.\n",
	"WARNING: Group entry exceeds 2048 char: /etc/group entry truncated.\n",
	"ERROR: Invalid syntax.\n"
	"usage:  roleadd [-u uid [-o] | -g group | -G group[[,group]...] |"
	"-d dir |\n"
	"\t\t-s shell | -c comment | -m [-k skel_dir] | -f inactive |\n"
	"\t\t-e expire | -A authorization[[,authorization]...] |\n"
	"\t\t-P profile[[,profile]...] | -K key=value ] |\n"
	"\t\t [-S [files | ldap]] login\n"
	"\troleadd -D [-g group | -b base_dir | -f inactive | -e expire\n"
	"\t\t-A authorization[[,authorization]...] |\n"
	"\t\t-P profile[[,profile]...] login\n",
	"ERROR: Invalid syntax.\n"
	"usage:  roledel [-r] [-S [files | ldap]] login\n",
	"ERROR: Invalid syntax.\n"
	"usage:  rolemod -u uid [-o] | -g group | "
	"-G [+|-]group[[,group]...] |\n"
	"\t\t-d dir [-m] | -s shell | -c comment |\n"
	"\t\t-l new_logname | -f inactive | -e expire |\n"
	"\t\t-A [+|-]authorization[[,authorization]...] | -K key[+|-]=value |\n"
	"\t\t-P [+|-]profile[[,profile]...] [-S [files | ldap]] login\n",
	"ERROR: project %s does not exist.  Choose another.\n",
	"WARNING: more than NPROJECTS_MAX(%d) projects specified.\n",
	"WARNING: Project entry exceeds %d char: /etc/project entry truncated."
	"\n",
	"ERROR: Invalid key: %s.\n",
	"ERROR: Missing value specification.\n",
	"ERROR: Multiple definitions of key \"%s\".\n",
	"ERROR: Roles must be modified with 'rolemod'.\n",
	"ERROR: Users must be modified with 'usermod'.\n",
	"WARNING: gid %ld is reserved.\n",
	"ERROR: Failed to read /etc/group file due to invalid entry or"
	" read error.\n",
	"WARNING: failed to rename /var/user/%s",
	"ERROR: Roles must be deleted with 'roledel'.\n",
	"ERROR: Users must be deleted with 'userdel'.\n",
	"ERROR: Multiple definitions for \"%s\".\n",
	"ERROR: '%s' is not a valid path.  Choose another.\n",
	"ERROR: '%s' is not a valid shell.  Choose another.\n",
	"ERROR: '%s' is not a valid directory.  Choose another.\n",
	"ERROR: Cannot update system - login cannot be created.\n",
	"ERROR: Cannot update system - login cannot be modified.\n",
	"ERROR: Cannot update system - login cannot be deleted.\n",
	"ERROR: Create home directory failed. %s\n",
	"ERROR: Determine real uid failed.\n",
	"ERROR: Update group database failed.\n",
	"ERROR: Update project database failed.\n",
	"ERROR: Update auto_home database failed.\n",
	"ERROR: Determine auto_home entry failed.\n",
	"ERROR: Change ownership of home directory failed. %s\n",
	"ERROR: Change group of home directory failed. %s\n",
	"ERROR: Determine hostname failed. %s\n",
	"ERROR: Determine zoneid failed.\n",
	"ERROR: Allocate memory failed.\n",
	"ERROR: ZFS create failed for %s.\n",
	"ERROR: Set delegate failed for zfs dataset - %s.\n",
	"ERROR: ZFS destroy failed for %s.\n",
	"ERROR: Copy skeleteon directory into home directory failed. %s\n",
	"ERROR: Determine zonename failed.\n",
	"ERROR: ZFS dataset not mounted.\n",
	"ERROR: Cannot assign group, requires %s authorization.\n",
	"ERROR: Cannot find groups for %s.\n",
	"ERROR: Cannot assign group, not member of group gid: %d.\n",
	"ERROR: Cannot assign project, requires %s authorization.\n",
	"ERROR: Cannot set auto_home, requires %s authorization.\n",
	"WARNING: Home directory not created at %s.\n",
	"ERROR: Cannot delete account, requires %s authorization.\n",
	"Login deleted.\n",
	"ERROR: Cannot resolve hostname - %s.\n",
	"ERROR: Determine hostname failed. Default to localhost.\n",
	"ERROR: Path must not start with '/home/'.\n",
	"WARNING: Hostname not set. Adding auto_home entry localhost:%s.\n",
	"ERROR: Mountpoint is not valid.\n",
	"ERROR: User name is not valid.\n",
	"ERROR: ZFS dataset unmount failed for %s.\n",
	"WARNING: Cannot find authorization %s.\n",
	"ERROR: Cannot modify account. Marked as read-only.\n",
	"ERROR: Cannot delete root.\n",
};

int lasterrmsg = sizeof (errmsgs) / sizeof (char *);

/*
 *	synopsis: errmsg(msgid, (arg1, ..., argN))
 */

void
errmsg(int msgid, ...)
{
	va_list	args;

	va_start(args, msgid);

	if (msgid >= 0 && msgid < lasterrmsg) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) vfprintf(stderr, gettext(errmsgs[msgid]), args);
	}

	va_end(args);
}

void
warningmsg(int what, char *name)
{
	if ((what & WARN_NAME_TOO_LONG) != 0) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) fprintf(stderr, gettext("%s name too long.\n"), name);
	}
	if ((what & WARN_BAD_GROUP_NAME) != 0) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) fprintf(stderr, gettext("%s name should be all lower"
		    "case or numeric.\n"), name);
	}
	if ((what & WARN_BAD_PROJ_NAME) != 0) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) fprintf(stderr, gettext("%s name should be all lower"
		    "case or numeric.\n"), name);
	}
	if ((what & WARN_BAD_LOGNAME_CHAR) != 0) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) fprintf(stderr, gettext("%s name should be all"
		    " alphanumeric, '-', '_', or '.'\n"), name);
	}
	if ((what & WARN_BAD_LOGNAME_FIRST) != 0) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) fprintf(stderr, gettext("%s name first character"
		    " should be alphabetic.\n"), name);
	}
	if ((what & WARN_NO_LOWERCHAR) != 0) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) fprintf(stderr, gettext("%s name should have at least"
		    " one lower case character.\n"), name);
	}
	if ((what & WARN_LOGGED_IN) != 0) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) fprintf(stderr, gettext("%s is currently logged in,"
		    " some changes may not take effect until next login.\n"),
		    name);
	}
}
