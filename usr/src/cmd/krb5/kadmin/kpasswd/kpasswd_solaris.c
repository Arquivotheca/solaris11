/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */

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
 * Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <kadm5/admin.h>
#include <krb5.h>

#include "kpasswd_strings_solaris.h"
#define string_text error_message

#include "kpasswd_solaris.h"

#include <stdio.h>
#include <pwd.h>
#include <string.h>
#include <libintl.h>


#define MISC_EXIT_STATUS 6

void
display_intro_message(const char *whoami,
                           const char *fmt_string,
                           const char *arg_string)
{
    com_err(whoami, 0, fmt_string, arg_string);
}

long
read_old_password(krb5_context context,
                       char *password,
                       unsigned int *pwsize)
{
    long code = krb5_read_password(context,
                                   (char *) string_text(KPW_STR_OLD_PASSWORD_PROMPT),
                                   0, password, pwsize);
    return code;
}

long
read_new_password(void *server_handle,
                  char *password,
                  unsigned int *pwsize,
                  char *msg_ret,
                  int msg_len,
                  krb5_principal princ)
{
    return (kadm5_chpass_principal_util(server_handle, princ, NULL,
                                        NULL /* don't need new pw back */,
                                        msg_ret, msg_len));
}


int
change_kpassword_solaris(
    krb5_context context,
    char *whoami,
    krb5_principal princ)
{
    kadm5_ret_t code;
    char *princ_str;
    struct passwd *pw = 0;
    unsigned int pwsize;
    char password[255];  /* I don't really like 255 but that's what kinit uses */
    char msg_ret[1024], admin_realm[1024];
    kadm5_principal_ent_rec principal_entry;
    kadm5_policy_ent_rec policy_entry;
    void *server_handle;
    kadm5_config_params params;
    char *cpw_service;

    if ((code = krb5_unparse_name(context, princ, &princ_str))) {
        com_err(whoami, code, gettext("unparsing client name"));
        exit(1);
    }

    display_intro_message(whoami, string_text(KPW_STR_CHANGING_PW_FOR), princ_str);

    pwsize = sizeof(password);
    code = read_old_password(context, password, &pwsize);

    if (code != 0) {
        memset(password, 0, sizeof(password));
        com_err(whoami, code, string_text(KPW_STR_WHILE_READING_PASSWORD));
        krb5_free_principal(context, princ);
        free(princ_str);
        return(MISC_EXIT_STATUS);
    }
    if (pwsize == 0) {
        memset(password, 0, sizeof(password));
        com_err(whoami, 0, string_text(KPW_STR_NO_PASSWORD_READ));
        krb5_free_principal(context, princ);
        free(princ_str);
        return(5);
    }

    memset(&params, 0, sizeof (params));

    snprintf(admin_realm, sizeof (admin_realm),
             krb5_princ_realm(context, princ)->data);
    params.mask = KADM5_CONFIG_REALM; /* remember to |= any other masks */
    params.realm = admin_realm;

    if (kadm5_get_cpw_host_srv_name(context, admin_realm, &cpw_service)) {
        fprintf(stderr, gettext("%s: unable to get host based "
                                "service name for realm %s\n"),
                whoami, admin_realm);
        exit(1);
    }

    code = kadm5_init_with_password(context, princ_str, password, cpw_service,
                                    &params, KADM5_STRUCT_VERSION,
                                    KADM5_API_VERSION_2, NULL,
                                    &server_handle);
    free(cpw_service);
    if (code != 0) {
        if (code == KADM5_BAD_PASSWORD)
            com_err(whoami, 0,
                    string_text(KPW_STR_OLD_PASSWORD_INCORRECT));
        else
            com_err(whoami, 0,
                    string_text(KPW_STR_CANT_OPEN_ADMIN_SERVER),
                    admin_realm,
                    error_message(code));
        krb5_free_principal(context, princ);
        free(princ_str);
        return ((code == KADM5_BAD_PASSWORD) ? 2 : 3);
    }

    /*
     * we can only check the policy if the server speaks
     * RPCSEC_GSS
     */
    if (_kadm5_get_kpasswd_protocol(server_handle) == KRB5_CHGPWD_RPCSEC) {
        /* Explain policy restrictions on new password if any. */
        /*
         * Note: copy of this exists in login
         * (kverify.c/get_verified_in_tkt).
         */

        code = kadm5_get_principal(server_handle, princ,
                                   &principal_entry,
                                   KADM5_PRINCIPAL_NORMAL_MASK);
        if (code != 0) {
            com_err(whoami, 0,
                    string_text((code == KADM5_UNK_PRINC)
                                ? KPW_STR_PRIN_UNKNOWN :
                                KPW_STR_CANT_GET_POLICY_INFO),
                    princ_str);
            krb5_free_principal(context, princ);
            free(princ_str);
            (void) kadm5_destroy(server_handle);
            return ((code == KADM5_UNK_PRINC) ? 1 :
                    MISC_EXIT_STATUS);
        }
        if ((principal_entry.aux_attributes & KADM5_POLICY) != 0) {
            code = kadm5_get_policy(server_handle,
                                    principal_entry.policy,
                                    &policy_entry);
            if (code != 0) {
                /*
                 * doesn't matter which error comes back,
                 * there's no nice recovery or need to
                 * differentiate to the user
                 */
                com_err(whoami, 0,
                        string_text(KPW_STR_CANT_GET_POLICY_INFO),
                        princ_str);
                (void) kadm5_free_principal_ent(server_handle,
                                                &principal_entry);
                krb5_free_principal(context, princ);
                free(princ_str);
                free(princ_str);
                (void) kadm5_destroy(server_handle);
                return (MISC_EXIT_STATUS);
            }
            com_err(whoami, 0,
                    string_text(KPW_STR_POLICY_EXPLANATION),
                    princ_str, principal_entry.policy,
                    policy_entry.pw_min_length,
                    policy_entry.pw_min_classes);
            if (code = kadm5_free_principal_ent(server_handle,
                                                &principal_entry)) {
                (void) kadm5_free_policy_ent(server_handle,
                                             &policy_entry);
                krb5_free_principal(context, princ);
                free(princ_str);
                com_err(whoami, code,
                        string_text(KPW_STR_WHILE_FREEING_PRINCIPAL));
                (void) kadm5_destroy(server_handle);
                return (MISC_EXIT_STATUS);
            }
            if (code = kadm5_free_policy_ent(server_handle,
                                             &policy_entry)) {
                krb5_free_principal(context, princ);
                free(princ_str);
                com_err(whoami, code,
                        string_text(KPW_STR_WHILE_FREEING_POLICY));
                (void) kadm5_destroy(server_handle);
                return (MISC_EXIT_STATUS);
            }
        } else {
            /*
             * kpasswd *COULD* output something here to
             * encourage the choice of good passwords,
             * in the absence of an enforced policy.
             */
            if (code = kadm5_free_principal_ent(server_handle,
                                                &principal_entry)) {
                krb5_free_principal(context, princ);
                free(princ_str);
                com_err(whoami, code,
                        string_text(KPW_STR_WHILE_FREEING_PRINCIPAL));
                (void) kadm5_destroy(server_handle);
                return (MISC_EXIT_STATUS);
            }
        }
    } /* if protocol == KRB5_CHGPWD_RPCSEC */

    pwsize = sizeof(password);
    code = read_new_password(server_handle, password, &pwsize, msg_ret, sizeof (msg_ret), princ);
    memset(password, 0, sizeof(password));

    if (code)
        com_err(whoami, 0, msg_ret);

    krb5_free_principal(context, princ);
    free(princ_str);

    (void) kadm5_destroy(server_handle);
  
    if (code == KRB5_LIBOS_CANTREADPWD)
        return(5);
    else if (code)
        return(4);
    else
        return(0);
}
