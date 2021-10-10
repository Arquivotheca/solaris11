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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _ZONEADM_H
#define	_ZONEADM_H

#include <sys/types.h>
#include <sys/list.h>

#define	CMD_HELP	0
#define	CMD_BOOT	1
#define	CMD_SHUTDOWN	2
#define	CMD_HALT	3
#define	CMD_READY	4
#define	CMD_REBOOT	5
#define	CMD_LIST	6
#define	CMD_VERIFY	7
#define	CMD_INSTALL	8
#define	CMD_UNINSTALL	9
#define	CMD_MOUNT	10
#define	CMD_UNMOUNT	11
#define	CMD_CLONE	12
#define	CMD_MOVE	13
#define	CMD_DETACH	14
#define	CMD_ATTACH	15
#define	CMD_MARK	16
#define	CMD_APPLY	17
#define	CMD_SYSBOOT	18

#define	CMD_MIN		CMD_HELP
#define	CMD_MAX		CMD_SYSBOOT

#if !defined(TEXT_DOMAIN)		/* should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it wasn't */
#endif

#define	Z_ERR		1
#define	Z_USAGE		2
#define	Z_FATAL		3

#define	SW_CMP_NONE	0x0
#define	SW_CMP_SRC	0x01
#define	SW_CMP_SILENT	0x02

#define	RMCOMMAND	"/usr/bin/rm -rf"

/*
 * zoneadm.c
 */
extern char *target_zone;

extern int zfm_print(const struct mnttab *mntp, void *unused);
extern int clone_copy(char *source_zonepath, char *zonepath);
extern char *cmd_to_str(int cmd_num);
extern int do_subproc(char *cmdbuf);
extern int subproc_status(const char *cmd, int status,
    boolean_t verbose_failure);
extern void zerror(const char *fmt, ...);
extern void zperror(const char *str, boolean_t zonecfg_error);
extern void zperror2(const char *zone, const char *str);

/*
 * zfs.c
 */
extern int clone_snapshot_zfs(char *snap_name, char *zonepath,
    char *validatesnap);
extern int clone_zfs(char *source_zonepath, char *zonepath, char *presnapbuf,
    char *postsnapbuf);
extern int create_zfs_zonepath(char *zonepath, boolean_t ancestors_only);
extern int destroy_zfs_zonepath(char *zonepath);
extern boolean_t is_zonepath_zfs(char *zonepath);
extern int move_zfs(char *zonepath, char *new_zonepath);
extern int verify_datasets(zone_dochandle_t handle);
extern int verify_fs_zfs(struct zone_fstab *fstab);
extern int init_zfs(void);
extern int verify_zonepath_datasets(char *path, const char *cmdname);
extern boolean_t is_mountpnt(char *path, char *fstype);

#endif	/* _ZONEADM_H */
