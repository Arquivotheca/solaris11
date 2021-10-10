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
 * Solaris Kerberos
 * This is a private header file, therefore the interfaces that this file
 * declares are subject to change without prior notice.
 */

/*
 * profile.h
 */

#ifndef _KRB5_PROFILE_H
#define _KRB5_PROFILE_H

#if defined(_WIN32)
#include <win-mac.h>
#endif

#if defined(__MACH__) && defined(__APPLE__)
#    include <TargetConditionals.h>
#    if TARGET_RT_MAC_CFM
#        error "Use KfM 4.0 SDK headers for CFM compilation."
#    endif
#endif

#ifndef KRB5_CALLCONV
#define KRB5_CALLCONV
#define KRB5_CALLCONV_C
#endif

typedef struct _profile_t *profile_t;

/*
 * Used by the profile iterator in prof_get.c
 */
#define PROFILE_ITER_LIST_SECTION	0x0001
#define PROFILE_ITER_SECTIONS_ONLY	0x0002
#define PROFILE_ITER_RELATIONS_ONLY	0x0004

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef char* profile_filespec_t;	/* path as C string */
typedef char* profile_filespec_list_t;	/* list of : separated paths, C string */
typedef const char * const_profile_filespec_t;	/* path as C string */
typedef const char * const_profile_filespec_list_t;	/* list of : separated paths, C string */

long KRB5_CALLCONV profile_init
	(const_profile_filespec_t *files, profile_t *ret_profile);

long KRB5_CALLCONV profile_init_path
	(const_profile_filespec_list_t filelist, profile_t *ret_profile);

long KRB5_CALLCONV profile_flush
	(profile_t profile);
long KRB5_CALLCONV profile_flush_to_file
	(profile_t profile, const_profile_filespec_t outfile);
long KRB5_CALLCONV profile_flush_to_buffer
	(profile_t profile, char **bufp);
void KRB5_CALLCONV profile_free_buffer
	(profile_t profile, char *buf);

long KRB5_CALLCONV profile_is_writable
	(profile_t profile, int *writable);
long KRB5_CALLCONV profile_is_modified
	(profile_t profile, int *modified);

void KRB5_CALLCONV profile_abandon
	(profile_t profile);

void KRB5_CALLCONV profile_release
	(profile_t profile);

long KRB5_CALLCONV profile_get_values
	(profile_t profile, const char *const *names, char ***ret_values);

void KRB5_CALLCONV profile_free_list
	(char **list);

long KRB5_CALLCONV profile_get_string
	(profile_t profile, const char *name, const char *subname,
			const char *subsubname, const char *def_val,
			char **ret_string);
long KRB5_CALLCONV profile_get_integer
	(profile_t profile, const char *name, const char *subname,
			const char *subsubname, int def_val,
			int *ret_default);

long KRB5_CALLCONV profile_get_boolean
	(profile_t profile, const char *name, const char *subname,
			const char *subsubname, int def_val,
			int *ret_default);

long KRB5_CALLCONV profile_get_relation_names
	(profile_t profile, const char **names, char ***ret_names);

long KRB5_CALLCONV profile_get_subsection_names
	(profile_t profile, const char **names, char ***ret_names);

long KRB5_CALLCONV profile_iterator_create
	(profile_t profile, const char *const *names,
		   int flags, void **ret_iter);

void KRB5_CALLCONV profile_iterator_free
	(void **iter_p);

long KRB5_CALLCONV profile_iterator
	(void	**iter_p, char **ret_name, char **ret_value);

void KRB5_CALLCONV profile_release_string (char *str);

long KRB5_CALLCONV profile_update_relation
	(profile_t profile, const char **names,
		   const char *old_value, const char *new_value);

long KRB5_CALLCONV profile_clear_relation
	(profile_t profile, const char **names);

long KRB5_CALLCONV profile_rename_section
	(profile_t profile, const char **names,
		   const char *new_name);

long KRB5_CALLCONV profile_add_relation
	(profile_t profile, const char **names,
		   const char *new_value);

/*
 * Solaris Kerberos: Provide abstract declarations for applications, such as
 * kconf and smb.
 */
#define	K5_PROFILE_VAL_SUCCESS			0
#define	K5_PROFILE_VAL_DEF_REALM_CASE		1
#define	K5_PROFILE_VAL_REALM_CASE		2
#define	K5_PROFILE_VAL_NO_DEF_IN_REALM		3
#define	K5_PROFILE_VAL_NO_DEF_REALM		4
#define	K5_PROFILE_VAL_NULL_REALM		5
#define	K5_PROFILE_VAL_NO_DOM_REALM_MAP		6
#define	K5_PROFILE_VAL_KDC_NO_REALM		7
#define	K5_PROFILE_VAL_ADMIN_NO_REALM		8
#define	K5_PROFILE_VAL_DOM_REALM_CASE		9
#define	K5_PROFILE_VAL_NO_REALM			10

long k5_profile_init(char *filename, profile_t *profile);
long k5_profile_release(profile_t profile);
void k5_profile_abandon(profile_t profile);
long k5_profile_add_domain_mapping(profile_t profile, char *domain,
    char *realm);
long k5_profile_remove_domain_mapping(profile_t profile, char *realm);
long k5_profile_get_realm_entry(profile_t profile, char *realm, char *name,
    char ***ret_value);
long k5_profile_add_realm_entry(profile_t profile, char *realm, char *name,
    char **values);
long k5_profile_get_default_realm(profile_t profile, char **realm);
long k5_profile_get_realms(profile_t profile, char ***realms);
long k5_profile_add_realm(profile_t profile, char *realm, char *master,
    char **kdcs, boolean_t set_change, boolean_t default_realm);
long k5_profile_remove_xrealm_mapping(profile_t profile, char *realm);
long k5_profile_remove_realm(profile_t profile, char *realm);
long k5_profile_add_xrealm_mapping(profile_t profile, char *source,
    char *target, char *inter);
long k5_profile_validate(profile_t profile, char *realm, int *val_err,
    char **val);
long k5_profile_validate_get_error_msg(profile_t profile, int err, char *val,
    char **err_msg);
long k5_profile_set_libdefaults(profile_t profile, char *realm);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _KRB5_PROFILE_H */
/*
 * et-h-prof_err.h:
 * This file is automatically generated; please do not edit it.
 */

#include <com_err.h>

#define PROF_VERSION                             (-1429577728L)
#define PROF_MAGIC_NODE                          (-1429577727L)
#define PROF_NO_SECTION                          (-1429577726L)
#define PROF_NO_RELATION                         (-1429577725L)
#define PROF_ADD_NOT_SECTION                     (-1429577724L)
#define PROF_SECTION_WITH_VALUE                  (-1429577723L)
#define PROF_BAD_LINK_LIST                       (-1429577722L)
#define PROF_BAD_GROUP_LVL                       (-1429577721L)
#define PROF_BAD_PARENT_PTR                      (-1429577720L)
#define PROF_MAGIC_ITERATOR                      (-1429577719L)
#define PROF_SET_SECTION_VALUE                   (-1429577718L)
#define PROF_EINVAL                              (-1429577717L)
#define PROF_READ_ONLY                           (-1429577716L)
#define PROF_SECTION_NOTOP                       (-1429577715L)
#define PROF_SECTION_SYNTAX                      (-1429577714L)
#define PROF_RELATION_SYNTAX                     (-1429577713L)
#define PROF_EXTRA_CBRACE                        (-1429577712L)
#define PROF_MISSING_OBRACE                      (-1429577711L)
#define PROF_MAGIC_PROFILE                       (-1429577710L)
#define PROF_MAGIC_SECTION                       (-1429577709L)
#define PROF_TOPSECTION_ITER_NOSUPP              (-1429577708L)
#define PROF_INVALID_SECTION                     (-1429577707L)
#define PROF_END_OF_SECTIONS                     (-1429577706L)
#define PROF_BAD_NAMESET                         (-1429577705L)
#define PROF_NO_PROFILE                          (-1429577704L)
#define PROF_MAGIC_FILE                          (-1429577703L)
#define PROF_FAIL_OPEN                           (-1429577702L)
#define PROF_EXISTS                              (-1429577701L)
#define PROF_BAD_BOOLEAN                         (-1429577700L)
#define PROF_BAD_INTEGER                         (-1429577699L)
#define PROF_MAGIC_FILE_DATA                     (-1429577698L)
#define ERROR_TABLE_BASE_prof (-1429577728L)

/* Solaris Kerberos */
#if !USE_BUNDLE_ERROR_STRINGS

extern const struct error_table et_prof_error_table;

#if !defined(_WIN32)
/* for compatibility with older versions... */
extern void initialize_prof_error_table (void) /*@modifies internalState@*/;
#else
#define initialize_prof_error_table()
#endif

#if !defined(_WIN32)
#define init_prof_err_tbl initialize_prof_error_table
#define prof_err_base ERROR_TABLE_BASE_prof
#endif

#endif /* USE_BUNDLE_ERROR_STRINGS */

