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
 * Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _PKGINSTALL_H_
#define	_PKGINSTALL_H_


#ifdef __cplusplus
extern "C" {
#endif

/* cppath() variables */
#define	DIR_DISPLAY	0x0001	/* display implied directories created */
#define	MODE_SRC	0x0002	/* set mode to mode of source file */
#define	MODE_SET	0x0004	/* set mode to mode passed in as argument */
#define	MODE_0666	0x0008	/* force mode to 0666 */

/* special stdin for request scripts */
#define	REQ_STDIN	"/dev/tty"

/* response file writability status */
#define	RESP_WR		0	/* Response file is writable. */
#define	RESP_RO		1	/* Read only. */

extern int	cppath(int ctrl, char *f1, char *f2, mode_t mode);
extern void	backup(char *path, int mode);
extern void	pkgvolume(struct pkgdev *devp, char *pkg, int part,
		    int nparts);
extern void	quit(int exitval);
extern void	ckreturn(int retcode, char *msg);
extern int	sortmap(struct cfextra ***extlist, VFP_T *pkgmapVfp,
			PKGserver serv, VFP_T *tmpvfp, char *a_zoneName);
extern void merginfo(struct cl_attr **pclass, int install_from_pspool);
extern void	set_infoloc(char *real_pkgsav);
extern int	pkgenv(char *pkginst, char *p_pkginfo, char *p_pkgmap);
extern void	instvol(struct cfextra **extlist, char *srcinst, int part,
			int nparts, PKGserver server, VFP_T **a_cfTmpVfp,
			char **r_updated, char *a_zoneName);
extern int	reqexec(int update, char *script, int non_abi_scripts,
			boolean_t enable_root_user);
extern int	chkexec(int update, char *script);
extern int	rdonly_respfile(void);
extern int	is_a_respfile(void);
extern char	*get_respfile(void);
extern int	set_respfile(char *respfile, char *pkginst,
		    int resp_stat);
extern void	predepend(char *oldpkg);
extern void	cksetPreinstallCheck(boolean_t a_preinstallCheck);
extern void	cksetZoneName(char *a_zoneName);
extern int	cksetuid(void);
extern int	ckconflct(void);
extern int	ckpkgdirs(void);
extern int	ckspace(void);
extern int	ckdepend(void);
extern int	ckrunlevel(void);
extern int	ckpartial(void);
extern int	ckpkgfiles(void);
extern int	ckpriv(void);
extern void	is_WOS_arch(void);
extern void	ckdirs(void);
extern char	*getinst(int *updatingExisting, struct pkginfo *info,
			int npkgs, boolean_t a_preinstallCheck);
extern int	is_samepkg(void);
extern int	dockspace(char *spacefile);

extern int	special_contents_add(int, struct cfextra **, const char *);
extern boolean_t	rm_all_pkg_entries(char *, char *);

#ifdef __cplusplus
}
#endif

#endif	/* _PKGINSTALL_H_ */
