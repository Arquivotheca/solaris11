/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * lib/kadm5/admin.h
 *
 * Copyright 2001, 2008 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 */
/*
 * Copyright 1993 OpenVision Technologies, Inc., All Rights Reserved
 *
 * $Header$
 */

/*
 * This API is not considered as stable as the main krb5 API.
 *
 * - We may make arbitrary incompatible changes between feature
 *   releases (e.g. from 1.7 to 1.8).
 * - We will make some effort to avoid making incompatible changes for
 *   bugfix releases, but will make them if necessary.
 */

/*
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 *
 *	Openvision retains the copyright to derivative works of
 *	this source code.  Do *NOT* create a derivative of this
 *	source code before consulting with your legal department.
 *	Do *NOT* integrate *ANY* of this source code into another
 *	product before consulting with your legal department.
 *
 *	For further information, read the top-level Openvision
 *	copyright which is contained in the top-level MIT Kerberos
 *	copyright.
 *
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 *
 */

#ifndef __KADM5_ADMIN_H__
#define __KADM5_ADMIN_H__

#include        <sys/types.h>
#include        <rpc/types.h>
#include        <rpc/rpc.h>
#include        <krb5.h>
#include        <kdb.h>
#include        <com_err.h>
#include        <kadm5/kadm_err.h>
#include        <kadm5/chpass_util_strings.h>

#ifndef KADM5INT_BEGIN_DECLS
#if defined(__cplusplus)
#define KADM5INT_BEGIN_DECLS    extern "C" {
#define KADM5INT_END_DECLS      }
#else
#define KADM5INT_BEGIN_DECLS
#define KADM5INT_END_DECLS
#endif
#endif

KADM5INT_BEGIN_DECLS
/*
 * Solaris Kerberos:
 * The kadmin/admin principal is unused on Solaris. This principal is used
 * in AUTH_GSSAPI but Solaris doesn't support AUTH_GSSAPI. RPCSEC_GSS can only
 * be used with host-based principals. 
 *
 */
#define KADM5_ADMIN_SERVICE     "kadmin/admin"
#define KADM5_CHANGEPW_SERVICE  "kadmin/changepw"
#define KADM5_HIST_PRINCIPAL    "kadmin/history"
#define KADM5_KIPROP_HOST_SERVICE "kiprop"

#define KADM5_ADMIN_SERVICE_P    "kadmin@admin"
#define KADM5_CHANGEPW_SERVICE_P "kadmin@changepw"
#define KADM5_ADMIN_HOST_SERVICE "kadmin"
#define KADM5_CHANGEPW_HOST_SERVICE "changepw"

typedef krb5_principal  kadm5_princ_t;
typedef char            *kadm5_policy_t;
typedef long            kadm5_ret_t;

#define KADM5_PW_FIRST_PROMPT                           \
    (error_message(CHPASS_UTIL_NEW_PASSWORD_PROMPT))
#define KADM5_PW_SECOND_PROMPT                                  \
    (error_message(CHPASS_UTIL_NEW_PASSWORD_AGAIN_PROMPT))

/*
 * Successful return code
 */
#define KADM5_OK        0

/*
 * Field masks
 */

/* kadm5_principal_ent_t */
#define KADM5_PRINCIPAL         0x000001
#define KADM5_PRINC_EXPIRE_TIME 0x000002
#define KADM5_PW_EXPIRATION     0x000004
#define KADM5_LAST_PWD_CHANGE   0x000008
#define KADM5_ATTRIBUTES        0x000010
#define KADM5_MAX_LIFE          0x000020
#define KADM5_MOD_TIME          0x000040
#define KADM5_MOD_NAME          0x000080
#define KADM5_KVNO              0x000100
#define KADM5_MKVNO             0x000200
#define KADM5_AUX_ATTRIBUTES    0x000400
#define KADM5_POLICY            0x000800
#define KADM5_POLICY_CLR        0x001000
/* version 2 masks */
#define KADM5_MAX_RLIFE         0x002000
#define KADM5_LAST_SUCCESS      0x004000
#define KADM5_LAST_FAILED       0x008000
#define KADM5_FAIL_AUTH_COUNT   0x010000
#define KADM5_KEY_DATA          0x020000
#define KADM5_TL_DATA           0x040000
#ifdef notyet /* Novell */
#define KADM5_CPW_FUNCTION      0x080000
#define KADM5_RANDKEY_USED      0x100000
#endif
#define KADM5_LOAD              0x200000
/* Solaris Kerberos: adding support for key history in LDAP KDB */
#define KADM5_KEY_HIST		0x400000

/* all but KEY_DATA, TL_DATA, LOAD */
#define KADM5_PRINCIPAL_NORMAL_MASK 0x41ffff


/* kadm5_policy_ent_t */
#define KADM5_PW_MAX_LIFE       0x004000
#define KADM5_PW_MIN_LIFE       0x008000
#define KADM5_PW_MIN_LENGTH     0x010000
#define KADM5_PW_MIN_CLASSES    0x020000
#define KADM5_PW_HISTORY_NUM    0x040000
#define KADM5_REF_COUNT         0x080000
#define KADM5_PW_MAX_FAILURE            0x100000
#define KADM5_PW_FAILURE_COUNT_INTERVAL 0x200000
#define KADM5_PW_LOCKOUT_DURATION       0x400000

/* kadm5_config_params */
#define KADM5_CONFIG_REALM              0x00000001
#define KADM5_CONFIG_DBNAME             0x00000002
#define KADM5_CONFIG_MKEY_NAME          0x00000004
#define KADM5_CONFIG_MAX_LIFE           0x00000008
#define KADM5_CONFIG_MAX_RLIFE          0x00000010
#define KADM5_CONFIG_EXPIRATION         0x00000020
#define KADM5_CONFIG_FLAGS              0x00000040
#define KADM5_CONFIG_ADMIN_KEYTAB       0x00000080
#define KADM5_CONFIG_STASH_FILE         0x00000100
#define KADM5_CONFIG_ENCTYPE            0x00000200
#define KADM5_CONFIG_ADBNAME            0x00000400
#define KADM5_CONFIG_ADB_LOCKFILE       0x00000800
/*#define KADM5_CONFIG_PROFILE          0x00001000*/
#define KADM5_CONFIG_ACL_FILE           0x00002000
#define KADM5_CONFIG_KADMIND_PORT       0x00004000
#define KADM5_CONFIG_ENCTYPES           0x00008000
#define KADM5_CONFIG_ADMIN_SERVER       0x00010000
#define KADM5_CONFIG_DICT_FILE          0x00020000
#define KADM5_CONFIG_MKEY_FROM_KBD      0x00040000
#define KADM5_CONFIG_KPASSWD_PORT       0x00080000
#define KADM5_CONFIG_OLD_AUTH_GSSAPI    0x00100000
#define KADM5_CONFIG_NO_AUTH            0x00200000
#define KADM5_CONFIG_AUTH_NOFALLBACK    0x00400000
/* Solaris Kerberos */
#define KADM5_CONFIG_KPASSWD_SERVER	0x00800000
#define KADM5_CONFIG_KPASSWD_PROTOCOL   0x01000000
#ifdef notyet /* Novell */
#define KADM5_CONFIG_KPASSWD_SERVER     0x00800000
#endif
#define KADM5_CONFIG_IPROP_ENABLED      0x01000000
#define KADM5_CONFIG_ULOG_SIZE          0x02000000
#define KADM5_CONFIG_POLL_TIME          0x04000000
#define KADM5_CONFIG_IPROP_LOGFILE      0x08000000
#define KADM5_CONFIG_IPROP_PORT         0x10000000
#define KADM5_CONFIG_KVNO               0x20000000

/* password change constants */
#define	KRB5_KPASSWD_SUCCESS		0
#define	KRB5_KPASSWD_MALFORMED		1
#define	KRB5_KPASSWD_HARDERROR		2
#define	KRB5_KPASSWD_AUTHERROR		3
#define	KRB5_KPASSWD_SOFTERROR		4
#define	KRB5_KPASSWD_ACCESSDENIED	5
#define	KRB5_KPASSWD_BAD_VERSION	6
#define	KRB5_KPASSWD_INITIAL_FLAG_NEEDED	7
#define	KRB5_KPASSWD_POLICY_REJECT	8
#define	KRB5_KPASSWD_BAD_PRINCIPAL	9
#define	KRB5_KPASSWD_ETYPE_NOSUPP	10

/*
 * permission bits
 */
#define KADM5_PRIV_GET          0x01
#define KADM5_PRIV_ADD          0x02
#define KADM5_PRIV_MODIFY       0x04
#define KADM5_PRIV_DELETE       0x08

/*
 * API versioning constants
 */
#define KADM5_MASK_BITS         0xffffff00

#define KADM5_STRUCT_VERSION_MASK       0x12345600
#define KADM5_STRUCT_VERSION_1  (KADM5_STRUCT_VERSION_MASK|0x01)
#define KADM5_STRUCT_VERSION    KADM5_STRUCT_VERSION_1

#define KADM5_API_VERSION_MASK  0x12345700
#define KADM5_API_VERSION_2     (KADM5_API_VERSION_MASK|0x02)
#define KADM5_API_VERSION_3     (KADM5_API_VERSION_MASK|0x03)

typedef struct _kadm5_principal_ent_t {
    krb5_principal  principal;
    krb5_timestamp  princ_expire_time;
    krb5_timestamp  last_pwd_change;
    krb5_timestamp  pw_expiration;
    krb5_deltat     max_life;
    krb5_principal  mod_name;
    krb5_timestamp  mod_date;
    krb5_flags      attributes;
    krb5_kvno       kvno;
    krb5_kvno       mkvno;
    char            *policy;
    long            aux_attributes;

    /* version 2 fields */
    krb5_deltat max_renewable_life;
    krb5_timestamp last_success;
    krb5_timestamp last_failed;
    krb5_kvno fail_auth_count;
    krb5_int16 n_key_data;
    krb5_int16 n_tl_data;
    krb5_tl_data *tl_data;
    krb5_key_data *key_data;
} kadm5_principal_ent_rec, *kadm5_principal_ent_t;

typedef struct _kadm5_policy_ent_t {
    char            *policy;
    long            pw_min_life;
    long            pw_max_life;
    long            pw_min_length;
    long            pw_min_classes;
    long            pw_history_num;
    long            policy_refcnt;

    /* version 3 fields */
    krb5_kvno       pw_max_fail;
    krb5_deltat     pw_failcnt_interval;
    krb5_deltat     pw_lockout_duration;
} kadm5_policy_ent_rec, *kadm5_policy_ent_t;

/*
 * New types to indicate which protocol to use when sending
 * password change requests
 */
typedef enum {
    KRB5_CHGPWD_RPCSEC,
    KRB5_CHGPWD_CHANGEPW_V2
} krb5_chgpwd_prot;

/*
 * Data structure returned by kadm5_get_config_params()
 */
typedef struct _kadm5_config_params {
    long               mask;
    char *             realm;
    int                kadmind_port;
    int                kpasswd_port;

    char *             admin_server;
#ifdef notyet /* Novell */ /* ABI change? */
    char *             kpasswd_server;
#endif

    /* Deprecated except for db2 backwards compatibility.  Don't add
       new uses except as fallbacks for parameters that should be
       specified in the database module section of the config
       file.  */
    char *             dbname;

    /* dummy fields to preserve abi for now */
    char *             admin_dbname_was_here;
    char *             admin_lockfile_was_here;

    char *             admin_keytab;
    char *             acl_file;
    char *             dict_file;

    int                mkey_from_kbd;
    char *             stash_file;
    char *             mkey_name;
    krb5_enctype       enctype;
    krb5_deltat        max_life;
    krb5_deltat        max_rlife;
    krb5_timestamp     expiration;
    krb5_flags         flags;
    krb5_key_salt_tuple *keysalts;
    krb5_int32         num_keysalts;
    krb5_kvno          kvno;
    bool_t              iprop_enabled;
    uint32_t            iprop_ulogsize;
    krb5_deltat         iprop_poll_time;
    char *              iprop_logfile;
/*    char *            iprop_server;*/
    int                 iprop_port;
    char                *kpasswd_server;
    krb5_chgpwd_prot    kpasswd_protocol;
} kadm5_config_params;

/***********************************************************************
 * This is the old krb5_realm_read_params, which I mutated into
 * kadm5_get_config_params but which old code (kdb5_* and krb5kdc)
 * still uses.
 ***********************************************************************/

/*
 * Data structure returned by krb5_read_realm_params()
 */
typedef struct __krb5_realm_params {
    char *              realm_profile;
    char *              realm_dbname;
    char *              realm_mkey_name;
    char *              realm_stash_file;
    char *              realm_kdc_ports;
    char *              realm_kdc_tcp_ports;
    char *              realm_acl_file;
    char *              realm_host_based_services;
    char *              realm_no_host_referral;
    krb5_int32          realm_kadmind_port;
    krb5_enctype        realm_enctype;
    krb5_deltat         realm_max_life;
    krb5_deltat         realm_max_rlife;
    krb5_timestamp      realm_expiration;
    krb5_flags          realm_flags;
    krb5_key_salt_tuple *realm_keysalts;
    unsigned int        realm_reject_bad_transit:1;
    unsigned int        realm_kadmind_port_valid:1;
    unsigned int        realm_enctype_valid:1;
    unsigned int        realm_max_life_valid:1;
    unsigned int        realm_max_rlife_valid:1;
    unsigned int        realm_expiration_valid:1;
    unsigned int        realm_flags_valid:1;
    unsigned int        realm_reject_bad_transit_valid:1;
    krb5_int32          realm_num_keysalts;
} krb5_realm_params;

/*
 * functions
 */

/* Solaris Kerberos */
kadm5_ret_t
kadm5_get_adm_host_srv_name(krb5_context context,
                            const char *realm, char **host_service_name);

kadm5_ret_t
kadm5_get_cpw_host_srv_name(krb5_context context,
                            const char *realm, char **host_service_name);

krb5_error_code kadm5_get_config_params(krb5_context context,
                                        int use_kdc_config,
                                        kadm5_config_params *params_in,
                                        kadm5_config_params *params_out);

krb5_error_code kadm5_free_config_params(krb5_context context,
                                         kadm5_config_params *params);

krb5_error_code kadm5_free_realm_params(krb5_context kcontext,
                                        kadm5_config_params *params);

krb5_error_code kadm5_get_admin_service_name(krb5_context, char *,
                                             char *, size_t);

/*
 * For all initialization functions, the caller must first initialize
 * a context with kadm5_init_krb5_context which will survive as long
 * as the resulting handle.  The caller should free the context with
 * krb5_free_context.
 */

kadm5_ret_t    kadm5_init(krb5_context context, char *client_name,
                          char *pass, char *service_name,
                          kadm5_config_params *params,
                          krb5_ui_4 struct_version,
                          krb5_ui_4 api_version,
                          char **db_args,
                          void **server_handle);
/* Solaris Kerberos */
kadm5_ret_t kadm5_init2(krb5_context context, char *client_name,
                          char *pass, char *service_name,
                          kadm5_config_params *params,
                          krb5_ui_4 struct_version,
                          krb5_ui_4 api_version,
                          char **db_args,
                          void **server_handle,
                          char **emsg);

kadm5_ret_t kadm5_init_anonymous(krb5_context context, char *client_name,
                                 char *service_name,
                                 kadm5_config_params *params,
                                 krb5_ui_4 struct_version,
                                 krb5_ui_4 api_version,
                                 char **db_args,
                                 void **server_handle);
kadm5_ret_t    kadm5_init_with_password(krb5_context context,
                                        char *client_name,
                                        char *pass,
                                        char *service_name,
                                        kadm5_config_params *params,
                                        krb5_ui_4 struct_version,
                                        krb5_ui_4 api_version,
                                        char **db_args,
                                        void **server_handle);
kadm5_ret_t    kadm5_init_with_skey(krb5_context context,
                                    char *client_name,
                                    char *keytab,
                                    char *service_name,
                                    kadm5_config_params *params,
                                    krb5_ui_4 struct_version,
                                    krb5_ui_4 api_version,
                                    char **db_args,
                                    void **server_handle);
kadm5_ret_t    kadm5_init_with_creds(krb5_context context,
                                     char *client_name,
                                     krb5_ccache cc,
                                     char *service_name,
                                     kadm5_config_params *params,
                                     krb5_ui_4 struct_version,
                                     krb5_ui_4 api_version,
                                     char **db_args,
                                     void **server_handle);
kadm5_ret_t    kadm5_lock(void *server_handle);
kadm5_ret_t    kadm5_unlock(void *server_handle);
kadm5_ret_t    kadm5_flush(void *server_handle);
kadm5_ret_t    kadm5_destroy(void *server_handle);
kadm5_ret_t    kadm5_check_min_life(void *server_handle,	/* Solaris Kerberos */
                                    krb5_principal principal,
                                    char *msg_ret,
                                    unsigned int msg_len);
kadm5_ret_t    kadm5_create_principal(void *server_handle,
                                      kadm5_principal_ent_t ent,
                                      long mask, char *pass);
kadm5_ret_t    kadm5_create_principal_3(void *server_handle,
                                        kadm5_principal_ent_t ent,
                                        long mask,
                                        int n_ks_tuple,
                                        krb5_key_salt_tuple *ks_tuple,
                                        char *pass);
kadm5_ret_t    kadm5_delete_principal(void *server_handle,
                                      krb5_principal principal);
kadm5_ret_t    kadm5_modify_principal(void *server_handle,
                                      kadm5_principal_ent_t ent,
                                      long mask);
kadm5_ret_t    kadm5_rename_principal(void *server_handle,
                                      krb5_principal,krb5_principal);
kadm5_ret_t    kadm5_get_principal(void *server_handle,
                                   krb5_principal principal,
                                   kadm5_principal_ent_t ent,
                                   long mask);
kadm5_ret_t    kadm5_chpass_principal(void *server_handle,
                                      krb5_principal principal,
                                      char *pass);
kadm5_ret_t    kadm5_chpass_principal_3(void *server_handle,
                                        krb5_principal principal,
                                        krb5_boolean keepold,
                                        int n_ks_tuple,
                                        krb5_key_salt_tuple *ks_tuple,
                                        char *pass);

/*
 * Solaris Kerberos:
 * this routine is only implemented in the client library.
 */
kadm5_ret_t    kadm5_randkey_principal_old(void *server_handle,
                                           krb5_principal principal,
                                           krb5_keyblock **keyblocks,
                                           int *n_keys);

kadm5_ret_t    kadm5_randkey_principal(void *server_handle,
                                       krb5_principal principal,
                                       krb5_keyblock **keyblocks,
                                       int *n_keys);
kadm5_ret_t    kadm5_randkey_principal_3(void *server_handle,
                                         krb5_principal principal,
                                         krb5_boolean keepold,
                                         int n_ks_tuple,
                                         krb5_key_salt_tuple *ks_tuple,
                                         krb5_keyblock **keyblocks,
                                         int *n_keys);
kadm5_ret_t    kadm5_setv4key_principal(void *server_handle,
                                        krb5_principal principal,
                                        krb5_keyblock *keyblock);

kadm5_ret_t    kadm5_setkey_principal(void *server_handle,
                                      krb5_principal principal,
                                      krb5_keyblock *keyblocks,
                                      int n_keys);

kadm5_ret_t    kadm5_setkey_principal_3(void *server_handle,
                                        krb5_principal principal,
                                        krb5_boolean keepold,
                                        int n_ks_tuple,
                                        krb5_key_salt_tuple *ks_tuple,
                                        krb5_keyblock *keyblocks,
                                        int n_keys);

kadm5_ret_t    kadm5_decrypt_key(void *server_handle,
                                 kadm5_principal_ent_t entry, krb5_int32
                                 ktype, krb5_int32 stype, krb5_int32
                                 kvno, krb5_keyblock *keyblock,
                                 krb5_keysalt *keysalt, int *kvnop);

kadm5_ret_t    kadm5_create_policy(void *server_handle,
                                   kadm5_policy_ent_t ent,
                                   long mask);
/*
 * kadm5_create_policy_internal is not part of the supported,
 * exposed API.  It is available only in the server library, and you
 * shouldn't use it unless you know why it's there and how it's
 * different from kadm5_create_policy.
 */
kadm5_ret_t    kadm5_create_policy_internal(void *server_handle,
                                            kadm5_policy_ent_t
                                            entry, long mask);
kadm5_ret_t    kadm5_delete_policy(void *server_handle,
                                   kadm5_policy_t policy);
kadm5_ret_t    kadm5_modify_policy(void *server_handle,
                                   kadm5_policy_ent_t ent,
                                   long mask);
/*
 * kadm5_modify_policy_internal is not part of the supported,
 * exposed API.  It is available only in the server library, and you
 * shouldn't use it unless you know why it's there and how it's
 * different from kadm5_modify_policy.
 */
kadm5_ret_t    kadm5_modify_policy_internal(void *server_handle,
                                            kadm5_policy_ent_t
                                            entry, long mask);
kadm5_ret_t    kadm5_get_policy(void *server_handle,
                                kadm5_policy_t policy,
                                kadm5_policy_ent_t ent);
kadm5_ret_t    kadm5_get_privs(void *server_handle,
                               long *privs);

kadm5_ret_t    kadm5_chpass_principal_util(void *server_handle,
                                           krb5_principal princ,
                                           char *new_pw,
                                           char **ret_pw,
                                           char *msg_ret,
                                           unsigned int msg_len);

kadm5_ret_t    kadm5_free_principal_ent(void *server_handle,
                                        kadm5_principal_ent_t
                                        ent);
kadm5_ret_t    kadm5_free_policy_ent(void *server_handle,
                                     kadm5_policy_ent_t ent);

kadm5_ret_t    kadm5_get_principals(void *server_handle,
                                    char *exp, char ***princs,
                                    int *count);

kadm5_ret_t    kadm5_get_policies(void *server_handle,
                                  char *exp, char ***pols,
                                  int *count);

kadm5_ret_t kadm5_get_master(krb5_context, const char *, char **);
kadm5_ret_t kadm5_is_master(krb5_context context, const char *realm,
                            krb5_boolean *is_master);


kadm5_ret_t    kadm5_free_key_data(void *server_handle,
                                   krb5_int16 *n_key_data,
                                   krb5_key_data *key_data);

kadm5_ret_t    kadm5_free_name_list(void *server_handle, char **names,
                                    int count);

krb5_error_code kadm5_init_krb5_context (krb5_context *);

krb5_error_code kadm5_init_iprop(void *server_handle, char **db_args);

/*
 * kadm5_get_principal_keys is used only by kadmin.local to extract existing
 * keys from the database without changing them.  It should never be exposed
 * to the network protocol.
 */
kadm5_ret_t    kadm5_get_principal_keys(void *server_handle,
                                        krb5_principal principal,
                                        krb5_keyblock **keyblocks,
                                        int *n_keys);

krb5_chgpwd_prot _kadm5_get_kpasswd_protocol(void *server_handle);
kadm5_ret_t	kadm5_chpass_principal_v2(void *server_handle,
                                          krb5_principal princ,
                                          char *new_password,
                                          kadm5_ret_t *srvr_rsp_code,
                                          krb5_data *srvr_msg);

void handle_chpw(krb5_context context, int s, void *serverhandle,
                 kadm5_config_params *params);

/* Solaris Kerberos */
#define MAXPRINCLEN 125
void trunc_name(size_t *len, char **dots);

KADM5INT_END_DECLS

#endif /* __KADM5_ADMIN_H__ */
