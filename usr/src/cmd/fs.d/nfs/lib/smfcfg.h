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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SMFCFG_H
#define	_SMFCFG_H

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdarg.h>
#include <rpc/rpc.h>
#include <synch.h>
#include <thread.h>
#include <libintl.h>
#include <strings.h>
#include <inttypes.h>
#include <limits.h>
#include <assert.h>
#include <libscf.h>
#include <locale.h>
#include <errno.h>
#include <sys/types.h>
#include <zone.h>
#include <sys/brand.h>
#include <rpcsvc/daemon_utils.h>
#include <deflt.h>

#ifdef	__cplusplus
extern	"C" {
#endif


#define	AUTOFSADMIN	"/etc/default/autofs"
#define	 NFSADMIN	"/etc/default/nfs"
#define	S10_BRANDNAME		"solaris10"
typedef enum {
	NFS_SMF = 1,
	AUTOFS_SMF
} smf_fstype_t;

typedef struct fs_smfhandle {
	scf_handle_t		*fs_handle;
	scf_service_t		*fs_service;
	scf_scope_t		*fs_scope;
	scf_instance_t		*fs_instance;
	scf_propertygroup_t	*fs_pg;
	scf_property_t		*fs_property;
	scf_value_t		*fs_value;
} fs_smfhandle_t;

#define	DEFAULT_INSTANCE	"default"

/*
 * NFS Property Group names.
 */
#define	SMF_PG_NFSPROPS		((const char *)"com.oracle.nfs,props")
#define	NFS_PROPS_PGNAME	((const char *)"nfs-props")
#define	SVC_NFS_CLIENT		"svc:/network/nfs/client"

/*
 * AUTOFS Property Group Names.
 */
#define	SMF_PG_AUTOFS		((const char *)"com.oracle.autofs,props")
#define	AUTOFS_PROPS_PGNAME	((const char *)"autofs-props")

#define	AUTOFS_FMRI		"svc:/system/filesystem/autofs"
#define	AUTOFS_DEFAULT_FMRI	"svc:/system/filesystem/autofs:default"
#define	MAXDIGITS	32

/*
 * ERRORS
 */
#define	SMF_OK			0
#define	SMF_SYSTEM_ERR		-1
#define	STATE_INITIALIZING	1
#define	SMF_NO_PERMISSION	2
#define	SMF_NO_PGTYPE		3

extern int nfs_smf_get_prop(char *, char *, char *, scf_type_t, char *, int *);
extern int nfs_smf_set_prop(char *, char *, char *, scf_type_t, char *);
extern int autofs_smf_set_prop(char *, char *, char *, scf_type_t, char *);
extern int autofs_smf_get_prop(char *, char *, char *, scf_type_t,
    char *, int *);
extern boolean_t string_to_boolean(const char *);
extern char *defread(char *);
extern int  defopen(char *);

enum autofs_nfs_prop_idx {
	AUTOMOUNT_TIMEOUT,
	AUTOMOUNT_VERBOSE,
	AUTOMOUNTD_ENV,
	AUTOMOUNTD_NOBROWSE,
	AUTOMOUNTD_TRACE,
	AUTOMOUNTD_VERBOSE,
	NFS_CLIENT_VERSMIN,
	NFS_CLIENT_VERSMAX,
	NFS_SERVER_VERSMIN,
	NFS_SERVER_VERSMAX
} autofs_nfs_prop_idx_t;

struct properties
{
	char *s10_name;
	char *nv_name;
	smf_fstype_t smf_fst;
	scf_type_t scftype;
	char *fmri;
};

extern int wrapped_get_prop(enum autofs_nfs_prop_idx, char *, char *,  int);
extern void wrapped_put_automountd_env(enum autofs_nfs_prop_idx);

#ifdef	__cplusplus
}
#endif

#endif	/* _SMFCFG_H */
