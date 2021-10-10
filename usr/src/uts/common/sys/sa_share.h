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

/*
 * API declarations for share management
 */
#ifndef _SYS_SA_SHARE_H
#define	_SYS_SA_SHARE_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/time.h>
#include <sys/nvpair.h>
#include <sys/sa_error.h>

#define	SA_PROP_NAME	"name"
#define	SA_PROP_PATH	"path"
#define	SA_PROP_DESC	"desc"
#define	SA_PROP_TRANS	"trans"
#define	SA_PROP_MNTPNT	"mntpnt"
#define	SA_PROP_STATUS	"status"
#define	SA_PROP_PROT	"prot"

/*
 * share protocol types
 */
#define	SA_PROT_NONE	0x0000
#define	SA_PROT_NFS	0x0001
#define	SA_PROT_SMB	0x0002
#define	SA_PROT_AUTOFS	0x0004
#define	SA_PROT_ALL	(SA_PROT_NFS | SA_PROT_SMB)
#define	SA_PROT_ANY	SA_PROT_ALL

typedef uint32_t sa_proto_t;

/*
 * share manipulation routines
 */
extern nvlist_t *sa_share_alloc(const char *, const char *);
extern void sa_share_free(nvlist_t *);

extern char *sa_share_get_name(nvlist_t *);
extern int sa_share_set_name(nvlist_t *, const char *);

extern char *sa_share_get_path(nvlist_t *);
extern int sa_share_set_path(nvlist_t *, const char *);

extern char *sa_share_get_desc(nvlist_t *);
extern int sa_share_set_desc(nvlist_t *, const char *);
extern int sa_share_rem_desc(nvlist_t *);

extern char *sa_share_get_mntpnt(nvlist_t *);
extern int sa_share_set_mntpnt(nvlist_t *, const char *);

extern boolean_t sa_share_is_transient(nvlist_t *);
extern int sa_share_set_transient(nvlist_t *);

extern sa_proto_t sa_share_get_status(nvlist_t *);
extern int sa_share_set_status(nvlist_t *, sa_proto_t);

extern char *sa_share_get_prop(nvlist_t *, const char *);
extern int sa_share_set_prop(nvlist_t *, const char *, const char *);
extern int sa_share_rem_prop(nvlist_t *, const char *);

extern nvlist_t *sa_share_get_proto(nvlist_t *, sa_proto_t);
extern int sa_share_set_proto(nvlist_t *, sa_proto_t, nvlist_t *);
extern int sa_share_rem_proto(nvlist_t *, sa_proto_t);
extern int sa_share_proto_count(nvlist_t *);

extern sa_proto_t sa_proto_first(void);
extern sa_proto_t sa_proto_next(sa_proto_t);
extern char *sa_proto_to_val(sa_proto_t);
extern sa_proto_t sa_val_to_proto(const char *);

extern uint32_t sa_crc_gen(uint8_t *, size_t);
#ifdef	__cplusplus
}
#endif

#endif /* _SYS_SA_SHARE_H */
