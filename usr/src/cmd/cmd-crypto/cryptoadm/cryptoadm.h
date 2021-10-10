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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_CRYPTOADM_H
#define	_CRYPTOADM_H

#include <sys/types.h>
#include <sys/crypto/ioctladmin.h>
#include <cryptoutil.h>
#include <security/cryptoki.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	_PATH_KCFD		"/lib/crypto/kcfd"
#define	TMPFILE_TEMPLATE	"/etc/crypto/admXXXXXX"

#define	ERROR_USAGE	2
#define	NO_CHANGE	0x400000	/* neither SUCCESS nor FAILURE */

/*
 * Common keywords and delimiters for pkcs11.conf and kcf.conf files are
 * defined in usr/lib/libcryptoutil/common/cryptoutil.h.  The following is
 * the extra keywords and delimiters used in kcf.conf file.
 */
#define	SEP_SLASH		'/'
#define	EF_SUPPORTED		"supportedlist="
#define	EF_UNLOAD		"unload"
#define	RANDOM			"random"
#define	UEF_FRAME_LIB		"/usr/lib/libpkcs11.so"

#define	ADD_MODE	1
#define	DELETE_MODE	2
#define	MODIFY_MODE	3

#define	REFRESH		1
#define	NOT_REFRESH	0

typedef char prov_name_t[MAXNAMELEN];
typedef char mech_name_t[CRYPTO_MAX_MECH_NAME];

typedef struct mechlist {
	mech_name_t	name;
	struct mechlist	*next;
} mechlist_t;


typedef struct entry {
	prov_name_t	name;
	mechlist_t	*suplist; /* supported list */
	uint_t		sup_count;
	mechlist_t	*dislist; /* disabled list */
	uint_t		dis_count;
	boolean_t	load; /* B_FALSE after cryptoadm unload */
} entry_t;


typedef struct entrylist {
	entry_t	*pent;
	struct entrylist *next;
} entrylist_t;

typedef enum {
	NO_RNG,
	HAS_RNG
} flag_val_t;

extern int errno;

/* adm_util */
extern boolean_t is_in_list(char *, mechlist_t *);
extern mechlist_t *create_mech(char *);
extern void free_mechlist(mechlist_t *);

/* adm_kef_util */
extern boolean_t is_device(char *);
extern int fips_update_pkcs11conf(int);
extern void fips_status_pkcs11conf(int *);
extern char *ent2str(entry_t *);
extern entry_t *getent_kef(char *provname,
		entrylist_t *pdevlist, entrylist_t *psoftlist);
extern int check_kernel_for_soft(char *provname,
		crypto_get_soft_list_t *psoftlist, boolean_t *in_kernel);
extern int check_kernel_for_hard(char *provname,
		crypto_get_dev_list_t *pdevlist, boolean_t *in_kernel);
extern int disable_mechs(entry_t **, mechlist_t *, boolean_t, mechlist_t *);
extern int enable_mechs(entry_t **, boolean_t, mechlist_t *);
extern int get_kcfconf_info(entrylist_t **, entrylist_t **);
extern int get_admindev_info(entrylist_t **, entrylist_t **);
extern int get_mech_count(mechlist_t *);
extern entry_t *create_entry(char *provname);
extern int insert_kcfconf(entry_t *);
extern int split_hw_provname(char *, char *, int *);
extern int update_kcfconf(entry_t *, int);
extern void free_entry(entry_t *);
extern void free_entrylist(entrylist_t *);
extern void print_mechlist(char *, mechlist_t *);
extern void print_kef_policy(char *provname, entry_t *pent,
		boolean_t has_random, boolean_t has_mechs);
extern boolean_t filter_mechlist(mechlist_t **, const char *);
extern uentry_t *getent_uef(char *);


/* adm_uef */
extern int list_mechlist_for_lib(char *, mechlist_t *, flag_val_t *,
		boolean_t, boolean_t, boolean_t);
extern int list_policy_for_lib(char *);
extern int disable_uef_lib(char *, boolean_t, boolean_t, mechlist_t *);
extern int enable_uef_lib(char *, boolean_t, boolean_t, mechlist_t *);
extern int install_uef_lib(char *);
extern int uninstall_uef_lib(char *);
extern int print_uef_policy(uentry_t *);
extern void display_token_flags(CK_FLAGS flags);
extern int convert_mechlist(CK_MECHANISM_TYPE **, CK_ULONG *, mechlist_t *);
extern void display_verbose_mech_header();
extern void display_mech_info(CK_MECHANISM_INFO *);
extern int display_policy(uentry_t *);
extern int update_pkcs11conf(uentry_t *);
extern int update_policylist(uentry_t *, mechlist_t *, int);

/* adm_kef */
extern int list_mechlist_for_soft(char *provname,
		entrylist_t *phardlist, entrylist_t *psoftlist);
extern int list_mechlist_for_hard(char *);
extern int list_policy_for_soft(char *provname,
		entrylist_t *phardlist, entrylist_t *psoftlist);
extern int list_policy_for_hard(char *provname,
		entrylist_t *phardlist, entrylist_t *psoftlist,
		crypto_get_dev_list_t *pdevlist);
extern int disable_kef_software(char *, boolean_t, boolean_t, mechlist_t *);
extern int disable_kef_hardware(char *, boolean_t, boolean_t, mechlist_t *);
extern int enable_kef(char *, boolean_t, boolean_t, mechlist_t *);
extern int install_kef(char *, mechlist_t *);
extern int uninstall_kef(char *);
extern int unload_kef_soft(char *provname);
extern int refresh(void);
extern int start_daemon(void);
extern int stop_daemon(void);

/* adm_ioctl */
extern crypto_load_soft_config_t *setup_soft_conf(entry_t *);
extern crypto_load_soft_disabled_t *setup_soft_dis(entry_t *);
extern crypto_load_dev_disabled_t *setup_dev_dis(entry_t *);
extern crypto_unload_soft_module_t *setup_unload_soft(entry_t *);
extern int get_dev_info(char *, int, int, mechlist_t **);
extern int get_dev_list(crypto_get_dev_list_t **);
extern int get_soft_info(char *provname, mechlist_t **ppmechlist,
		entrylist_t *phardlist, entrylist_t *psoftlist);
extern int get_soft_list(crypto_get_soft_list_t **);
extern int do_fips_actions(int, int);

/* adm_metaslot */
extern int list_metaslot_info(boolean_t, boolean_t, mechlist_t *);
extern int list_metaslot_policy();
extern int disable_metaslot(mechlist_t *, boolean_t, boolean_t);
extern int enable_metaslot(char *, char *, boolean_t, mechlist_t *, boolean_t,
    boolean_t);

/* adm_fips_hw */
extern int do_fips_hw_actions(int);

#ifdef __cplusplus
}
#endif

#endif /* _CRYPTOADM_H */
