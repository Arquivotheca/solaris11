/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright 1994 OpenVision Technologies, Inc., All Rights Reserved
 *
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

/*
 * Solaris Kerberos - 183resync
 * Mostly left as-is. In future should be resynced closer to MIT just
 * be wary of auditing and GSS auth diffs.
 */

#ifndef _MISC_H
#define _MISC_H 1





kadm5_ret_t
chpass_principal_wrapper_3(void *server_handle,
                           krb5_principal principal,
                           krb5_boolean keepold,
                           int n_ks_tuple,
                           krb5_key_salt_tuple *ks_tuple,
                           char *password);

kadm5_ret_t
randkey_principal_wrapper_3(void *server_handle,
                            krb5_principal principal,
                            krb5_boolean keepold,
                            int n_ks_tuple,
                            krb5_key_salt_tuple *ks_tuple,
                            krb5_keyblock **keys, int *n_keys);

kadm5_ret_t
schpw_util_wrapper(void *server_handle, krb5_principal princ,
		   char *new_pw, char **ret_pw,
		   char *msg_ret, unsigned int msg_len);


krb5_error_code process_chpw_request(krb5_context context, 
				     void *server_handle, 
				     char *realm, int s, 
				     krb5_keytab keytab, 
				     struct sockaddr_in *sockin, 
				     krb5_data *req, krb5_data *rep);

#ifdef SVC_GETARGS
void  kadm_1(struct svc_req *, SVCXPRT *);
#endif

void trunc_name(size_t *len, char **dots);

void
audit_kadmind_auth(
	SVCXPRT *xprt,
	in_port_t l_port,
	char *op,
	char *prime_arg,
	char *clnt_name,
	int sorf);

void
audit_kadmind_unauth(
	SVCXPRT *xprt,
	in_port_t l_port,
	char *op,
	char *prime_arg,
	char *clnt_name);

kadm5_ret_t
randkey_principal_wrapper(void *server_handle, krb5_principal princ,
			  krb5_keyblock ** keys, int *n_keys);

kadm5_ret_t
__kadm5_get_priv(void *server_handle, long *privs, gss_name_t client);

#endif /* !_MISC_H */

