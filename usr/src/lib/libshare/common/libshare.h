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

/*
 * basic API declarations for share management
 */
#ifndef _LIBSHARE_H
#define	_LIBSHARE_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <libnvpair.h>
#include <sys/sa_share.h>
#include <sys/mnttab.h>
#include <sys/acl.h>

/*
 * Feature set bit definitions (sharectl)
 */

#define	SA_FEATURE_NONE		0x0000	/* no feature flags set */
#define	SA_FEATURE_RESOURCE	0x0001	/* resource names are required */
#define	SA_FEATURE_DFSTAB	0x0002	/* need to manage in dfstab */
#define	SA_FEATURE_ALLOWSUBDIRS	0x0004	/* allow subdirs to be shared */
#define	SA_FEATURE_ALLOWPARDIRS	0x0008	/* allow parent dirs to be shared */
#define	SA_FEATURE_HAS_SECTIONS	0x0010	/* protocol supports sections */
#define	SA_FEATURE_ADD_PROPERTIES	0x0020	/* can add properties */
#define	SA_FEATURE_SERVER	0x0040	/* protocol supports server mode */
#define	SA_FEATURE_MULT_RESOURCES 0x0100 /* supports multiple resources */

extern int sa_share_parse(const char *, int, nvlist_t **, char *,
    size_t);
extern int sa_share_merge(nvlist_t *, nvlist_t *, int, char *, size_t);
extern int sa_share_validate_name(const char *, const char *, boolean_t,
    sa_proto_t, char *, size_t);
extern int sa_share_validate(nvlist_t *, boolean_t, char *, size_t);
extern int sa_share_publish(nvlist_t *, sa_proto_t, int);
extern int sa_share_unpublish(nvlist_t *, sa_proto_t, int);
extern int sa_share_unpublish_byname(const char *, const char *,
    sa_proto_t, int);
extern int sa_fs_publish(const char *, sa_proto_t, int);
extern int sa_fs_unpublish(const char *, sa_proto_t, int);
extern int sa_list_publish(nvlist_t *, sa_proto_t, int);

extern int sa_share_write(nvlist_t *);
extern void sa_path_to_shr_name(char *);
extern int sa_share_create_defaults(const char *, sa_proto_t,
    const char *, const char *);
extern int sa_share_remove(const char *, const char *);

extern int sa_share_lookup(const char *, const char *, sa_proto_t,
    nvlist_t **);
extern char *sa_strerror(int);

extern int sa_share_find_init(const char *, sa_proto_t, void **);
extern int sa_share_find_next(void *, nvlist_t **);
extern void sa_share_find_fini(void *);

extern int sa_share_read(const char *, const char *, nvlist_t **);
extern int sa_share_read_init(const char *, void **);
extern int sa_share_read_next(void *, nvlist_t **);
extern void sa_share_read_fini(void *);
extern sa_proto_t sa_sharing_enabled(const char *);
extern int sa_sharing_get_prop(const char *, sa_proto_t, char **);
extern int sa_sharing_set_prop(const char *, sa_proto_t, char *);
extern int sa_share_get_acl(const char *, const char *, acl_t **);
extern int sa_share_set_acl(const char *, const char *, acl_t *);
extern int sa_get_mntpnt_for_path(const char *, char *, size_t, char *, size_t,
    char *, size_t);
extern void sa_mnttab_cache(boolean_t);

extern int sa_share_set_def_proto(nvlist_t *, sa_proto_t);
extern int sa_share_format_props(nvlist_t *, sa_proto_t, char **);

extern boolean_t sa_prop_empty_list(nvlist_t *);
extern int sa_prop_cmp_list(const char *, char * const *);

extern int sa_path_is_shareable(const char *);
extern int sa_mntent_is_shareable(struct mnttab *);
extern boolean_t sa_path_in_current_zone(const char *);

extern int sa_locale_to_utf8(const char *, char **);
extern int sa_utf8_to_locale(const char *, char **);

/*
 * sharectl protocol property management routines
 */
extern uint64_t sa_proto_get_featureset(sa_proto_t);
extern nvlist_t *sa_proto_get_proplist(sa_proto_t);
extern char *sa_proto_get_status(sa_proto_t);
extern char *sa_proto_get_property(sa_proto_t, const char *, const char *);
extern int sa_proto_set_property(sa_proto_t, const char *, const char *,
    const char *);
extern int sa_proto_rem_section(sa_proto_t, const char *);

extern int sa_get_protocols(sa_proto_t **);

/*
 * routines for dtrace and logging
 */
extern void sa_trace(const char *);
extern void sa_tracef(const char *, ...);
extern void salog_error(int, const char *, ...);
extern void salog_debug(int, const char *, ...);
extern void salog_info(int, const char *, ...);

extern int sa_upgrade_smf_share_group(char *, boolean_t);
extern boolean_t sa_is_akd_present(void);
extern uint32_t sa_protocol_valid(char *);
#ifdef	__cplusplus
}
#endif

#endif /* _LIBSHARE_H */
