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

#ifndef	_MESSAGES_H
#define	_MESSAGES_H

extern void errmsg(int, ...);

/* WARNING: uid %d is reserved. */
#define	M_RESERVED		0

/* WARNING: more than NGROUPS_MAX(%d) groups specified. */
#define	M_MAXGROUPS	1

/* ERROR: invalid syntax.\nusage:  useradd ... */
#define	M_AUSAGE		2

/* ERROR: Invalid syntax.\nusage:  userdel [-r] login\n" */
#define	M_DUSAGE		3

/* ERROR: Invalid syntax.\nusage:  usermod ... */
#define	M_MUSAGE		4


/* ERROR: Unexpected failure.  Defaults unchanged. */
#define	M_FAILED	5

/* ERROR: Unable to remove files from home directory. */
#define	M_RMFILES	6

/* ERROR: Unable to remove home directory. */
#define	M_RMHOME		7

/* ERROR: Cannot update system files - login cannot be %s. */
#define	M_UPDATE		8

/* ERROR: uid %d is already in use.  Choose another. */
#define	M_UID_USED	9

/* ERROR: %s is already in use.  Choose another. */
#define	M_USED	10

/* ERROR: %s does not exist. */
#define	M_EXIST	11

/* ERROR: %s is not a valid %s.  Choose another. */
#define	M_INVALID		12

/* ERROR: %s is in use.  Cannot %s it. */
#define	M_BUSY	13

/* WARNING: %s has no permissions to use %s. */
#define	M_NO_PERM	14

/* ERROR: There is not sufficient space to move %s home directory to %s */
#define	M_NOSPACE		15

/* ERROR: %s %d is too big.  Choose another. */
#define	M_TOOBIG	16

/* ERROR: group %s does not exist.  Choose another. */
#define	M_GRP_NOTUSED	17

/* ERROR: %s is not a full path name.  Choose another. */
#define	M_RELPATH	18

/* ERROR: %s is the primary group name.  Choose another. */
#define	M_SAME_GRP	19

/* ERROR: Inconsistent password files.  See pwconv(1M). */
#define	M_HOSED_FILES	20

/* ERROR: %s is not a local user. */
#define	M_NONLOCAL	21

/* ERROR: Permission denied. */
#define	M_PERM_DENIED	22

/* WARNING: Group entry exceeds 2048 char: /etc/group entry truncated. */
#define	M_GROUP_ENTRY_OVF  23

/* ERROR: invalid syntax.\nusage:  roleadd ... */
#define	M_ARUSAGE		24

/* ERROR: Invalid syntax.\nusage:  roledel [-r] login\n" */
#define	M_DRUSAGE		25

/* ERROR: Invalid syntax.\nusage:  rolemod -u ... */
#define	M_MRUSAGE		26

/* ERROR: project %s does not exist.  Choose another. */
#define	M_PROJ_NOTUSED 27

/* WARNING: more than NPROJECTS_MAX(%d) projects specified. */
#define	M_MAXPROJECTS	28

/* WARNING: Project entry exceeds 512 char: /etc/project entry truncated. */
#define	M_PROJ_ENTRY_OVF  29

/* ERROR: Invalid key. */
#define	M_INVALID_KEY	30

/* ERROR: Missing value specification. */
#define	M_INVALID_VALUE	31

/* ERROR: Multiple definitions of key ``%s''. */
#define	M_REDEFINED_KEY	32

/* ERROR: Roles must be modified with rolemod */
#define	M_ISROLE	33

/* ERROR: Users must be modified with usermod */
#define	M_ISUSER	34

/* WARNING: gid %d is reserved. */
#define	M_RESERVED_GID	35

/* ERROR: Failed to read /etc/group file due to invalid entry or read error. */
#define	M_READ_ERROR	36

/* WARNING: failed to rename /var/user/%s */
#define	M_RENAME_WARNING	37

/* ERROR: Roles must be deleted with roledel */
#define	M_DELROLE	38

/* ERROR: Users must be deleted with userdel */
#define	M_DELUSER	39

/* ERROR: Multiple definitions for */
#define	M_MULTI_DEFINED	40

/* ERROR: %s is not a valid path.  Choose another. */
#define	M_INVALID_PATH	41

/* ERROR: %s is not a valid shell.  Choose another. */
#define	M_INVALID_SHELL	42

/* ERROR: %s is not a valid directory.  Choose another. */
#define	M_INVALID_DIRECTORY	43

/* ERROR: Cannot update system files - login cannot be created. */
#define	M_UPDATE_CREATED	44

/* ERROR: Cannot update system files - login cannot be modified. */
#define	M_UPDATE_MODIFIED	45

/* ERROR: Cannot update system files - login cannot be deleted. */
#define	M_UPDATE_DELETED	46

/* ERROR: Create home directory failed. */
#define	M_HOME_CREATE		47

/* ERROR: Determine real uid failed.  */
#define	M_GETUID		48

/* ERROR: Update group database failed. */
#define	M_UPDATE_GROUP 		49

/* ERROR: Update project database failed. */
#define	M_UPDATE_PROJECT 	50

/* ERROR: Update autohome database failed. */
#define	M_UPDATE_AUTOHOME 	51

/* ERROR: Determine autohome entry failed. */
#define	M_AUTOHOME 		52

/* ERROR: Change ownership of home directory failed. */
#define	M_HOME_OWNER		53

/* ERROR: Change group of home directory failed. */
#define	M_HOME_GROUP		54

/* ERROR: Determine hostname failed. */
#define	M_HOSTNAME		55

/* ERROR: Determine zoneid failed. */
#define	M_GET_ZONEID		56

/* ERROR: Allocate memory failed. */
#define	M_MEM_ALLOCATE		57

/* ERROR: ZFS create failed for %s. */
#define	M_ZFS_CREATE		58

/* ERROR: Set delegate failed for zfs dataset - %s. */
#define	M_ZFS_DELEGATE		59

/* ERROR: ZFS destroy failed for %s.  */
#define	M_ZFS_DESTROY		60

/* ERROR: Copy skeleton directory into home directory failed. */
#define	M_COPY_SKELETON		61

/* ERROR: Determine zonename failed. */
#define	M_GET_ZONENAME		62

/* ERROR: ZFS dataset not mounted. */
#define	M_ZFS_MOUNT		63

/* ERROR: Cannot assign group, requires %s authorization. */
#define	M_GROUP_ASSIGN		64

/* ERROR: Cannot determine groups for %s. */
#define	M_USER_GROUPS		65

/* ERROR: Cannot assign group, not member of group gid:%d. */
#define	M_GROUP_MEMBER		66

/* ERROR: Cannot assign group, not member of group gid:%d. */
#define	M_GROUP_MEMBER		66

/* ERROR: Cannot set project, requires %s authorization. */
#define	M_PROJECT_ASSIGN	67

/* ERROR: Cannot set auto_home, requires %s authorization. */
#define	M_AUTOHOME_SET		68

/* WARNING: Home directory not created at %s. */
#define	M_HOMEDIR_CREATE	69

/* ERROR: Cannot delete account, requires %s authorization. */
#define	M_ACCOUNT_DELETE	70

/* Login deleted. */
#define	M_LOGIN_DELETED		71

/* ERROR: Cannot resolve hostname %s. */
#define	M_HOSTNAME_RESOLVE	72

/* WARNING: Determine hostname failed. Default to localhost. */
#define	M_HOSTNAME_LOCALHOST	73

/* ERROR: Path must not start with /home/. */
#define	M_PATH_HOME		74

/* WARNING: Hostname not set. Adding auto_home entry localhost:%s. */
#define	M_AUTOHOME_LOCALHOST	75

/* ERROR: Mountpoint is not valid. */
#define	M_MOUNTPOINT_INVALID	76

/* ERROR: User name is not valid. */
#define	M_USERNAME_INVALID	77

/* ERROR: zfs dataset unmount failed for %s. */
#define	M_ZFS_UMOUNT		78

/* WARNING: Cannot find authorization %s. */
#define	M_AUTH_NOT_FOUND	79

/* WARNING: Cannot modify account. Marked as read-only. */
#define	M_ACCOUNT_READONLY	80

/* WARNING: Cannot delete root. */
#define	M_DEL_ROOT		81

#endif /* _MESSAGES_H */
