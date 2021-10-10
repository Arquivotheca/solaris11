dnl
dnl CDDL HEADER START
dnl
dnl The contents of this file are subject to the terms of the
dnl Common Development and Distribution License (the "License").
dnl You may not use this file except in compliance with the License.
dnl
dnl You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
dnl or http://www.opensolaris.org/os/licensing.
dnl See the License for the specific language governing permissions
dnl and limitations under the License.
dnl
dnl When distributing Covered Code, include this CDDL HEADER in each
dnl file and include the License file at usr/src/OPENSOLARIS.LICENSE.
dnl If applicable, add the following below this CDDL HEADER, with the
dnl fields enclosed by brackets "[]" replaced with your own identifying
dnl information: Portions Copyright [yyyy] [name of copyright owner]
dnl
dnl CDDL HEADER END
dnl
dnl
dnl
dnl Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
dnl
divert(-1)

dnl There are three main types of parameter data being copyin()ed and
dnl translated:
dnl 	- integers
dnl 	- strings
dnl 	- raw data 
dnl For each type there is a corresponding macro. 
dnl PREFIX(), INT_TYPE() and ARG() are defined by a call to BEGIN_XLATOR() which
dnl marks the beginning of a translator.
dnl
dnl The translators tranlate between the intermediate data structures shared
dnl between mech_krb5 and DTrace and the external data structures exposed to the
dnl user of the kerberos provider.
dnl The translators take care of copyin() the data and converting data types to
dnl the exposed interface types - e.g.
dnl    uint32_t        -> uint32_t    [no conversion required, just copyin()]
dnl    char *          -> string      [string]
dnl    char * + length -> uintptr_t   [raw data]

define(`COPYIN',
`PREFIX()_$2 = *(($1 *)copyin((uintptr_t)(&
	    (*((INT_TYPE() **)(copyin((uintptr_t)&x->ARG(),
	    sizeof (INT_TYPE() *)))))->$2),
	    sizeof ($1)));')

dnl Check for a NULL pointer and returns "<NULL>" if found. 
define(`COPYINSTR',
`PREFIX()_$1 = *((char **)copyin((uintptr_t)(&
	    (*((INT_TYPE() **)(copyin((uintptr_t)&x->ARG(),
	    sizeof (INT_TYPE() *)))))->$1),
	    sizeof (char *))) != NULL ?
	    copyinstr((uintptr_t)*((char **)copyin((uintptr_t)(&
	    (*((INT_TYPE() **)(copyin((uintptr_t)&x->ARG(),
	    sizeof (INT_TYPE() *)))))->$1),
	    sizeof (char *)))) : "<NULL>";')

dnl
dnl Given a length and a pointer copyin that amount of data from the
dnl pointer. Returns NULL if pointer is NULL.
dnl
define(`COPYINDATA',
`PREFIX()_$1 = *((char **)copyin((uintptr_t)(&
	    (*((INT_TYPE() **)(copyin((uintptr_t)&x->ARG(),
	    sizeof (INT_TYPE() *)))))->$1),
	    sizeof (char *))) != NULL ?
	    (uintptr_t)copyin((uintptr_t)*((char **)copyin((uintptr_t)(&
	    (*((INT_TYPE() **)(copyin((uintptr_t)&x->ARG(),
	    sizeof (INT_TYPE() *)))))->$1),
	    sizeof (char *))),
	    *((uint32_t *)copyin((uintptr_t)(&
	    (*((INT_TYPE() **)(copyin((uintptr_t)&x->ARG(),
	    sizeof (INT_TYPE() *)))))->$2),
	    sizeof (uint32_t)))) : (self->__krb = (char *)alloca(1),
	    self->__krb[0] = NULL, (uintptr_t)self->__krb);')

dnl
dnl The internal data structures closely mimic the external interfaces
dnl both in name and in content.
dnl
define(`BEGIN_XLATOR',
`define(`ARG', $2)dnl'
`define(`EXT_TYPE', `$1`'info_t')dnl'
`define(`INT_TYPE', `k5_`'EXT_TYPE()')dnl'
dnl Use kauth instead of kauthenticator kauthenticator is just too long
`define(`PREFIX', ifelse($1, kauthenticator, kauth, $1))dnl'
`translator $1`'info_t < k5_trace_t *x > {')

define(`END_XLATOR', `};')

dnl A translator defined like this:
dnl 
dnl BEGIN_XLATOR(krb, info)
dnl 
dnl         COPYIN(uint8_t, version)
dnl 
dnl         COPYINSTR(message_type)
dnl 
dnl         COPYINDATA(message, message_length)
dnl END_XLATOR()
dnl 
dnl Will produce something like this:
dnl 
dnl translator krbinfo_t < k5_trace_t *x > {
dnl 
dnl         krb_version = *((uint8_t *)copyin((uintptr_t)(&
dnl             (*((k5_krbinfo_t **)(copyin((uintptr_t)&x->info,
dnl             sizeof (k5_krbinfo_t *)))))->version),
dnl             sizeof (uint8_t)));
dnl 
dnl         krb_message_type = *((char **)copyin((uintptr_t)(&
dnl             (*((k5_krbinfo_t **)(copyin((uintptr_t)&x->info,
dnl             sizeof (k5_krbinfo_t *)))))->message_type),
dnl             sizeof (char *))) != NULL ?
dnl             copyinstr((uintptr_t)*((char **)copyin((uintptr_t)(&
dnl             (*((k5_krbinfo_t **)(copyin((uintptr_t)&x->info,
dnl             sizeof (k5_krbinfo_t *)))))->message_type),
dnl             sizeof (char *)))) : "<NULL>";
dnl 
dnl         krb_message = *((char **)copyin((uintptr_t)(&
dnl             (*((k5_krbinfo_t **)(copyin((uintptr_t)&x->info,
dnl             sizeof (k5_krbinfo_t *)))))->message),
dnl             sizeof (char *))) != NULL ?
dnl             (uintptr_t)copyin((uintptr_t)*((char **)copyin((uintptr_t)(&
dnl             (*((k5_krbinfo_t **)(copyin((uintptr_t)&x->info,
dnl             sizeof (k5_krbinfo_t *)))))->message),
dnl             sizeof (char *))),
dnl             *((uint32_t *)copyin((uintptr_t)(&
dnl             (*((k5_krbinfo_t **)(copyin((uintptr_t)&x->info,
dnl             sizeof (k5_krbinfo_t *)))))->message_length),
dnl             sizeof (uint32_t)))) : (self->__krb = (char *)alloca(1),
dnl             self->__krb[0] = NULL, (uintptr_t)self->__krb);
dnl };
dnl 
dnl which may look complicated but is really pretty straightforward.
dnl e.g. to dnl copy in a uint8_t member value from a structure from userspace
dnl (mech_krb5) into the kernel (DTrace) the following must be done:
dnl 
dnl 1. Copy in the pointer to the data structure which contains the member
dnl    we're interested in.
dnl         copyin((uintptr_t)&x->info, sizeof (k5_krbinfo_t *))
dnl 
dnl 2. Cast and dereference the returned value to get the actual pointer.
dnl copyin() will return a pointer to the copyin()ed value.
dnl         (*((k5_krbinfo_t **)( #1 )))
dnl 
dnl 3. Do 1. again to get a pointer to the value of the member
dnl         copyin((uintptr_t)(& #2 ->version), sizeof (uint8_t))
dnl 
dnl 4. Do 2. again to get the member value
dnl         *((uint8_t *)( #3 ))
dnl 
dnl The above four steps are what is being done for COPYIN(). COPYINSTR()
dnl and COPYINDATA() are very similar to COPYIN() but copy in strings and
dnl data respectively instead of integer values.

divert(0)dnl
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
 * When distributing Covered Code, `include' this CDDL HEADER in each
 * file and `include' the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Internal Interfaces.
 */

dnl Shared intermediate data structures.
include(`../dtrace/kerberos_dtrace_impl.h.m4')dnl


/*
 * External Interfaces.
 */

typedef struct krbinfo {
	uint8_t krb_version;
	string krb_message_type;
	uint64_t krb_message_id;
	uint32_t krb_message_length;
	uintptr_t krb_message;
} krbinfo_t;

typedef struct kerrorinfo {
	uint32_t kerror_ctime;
	uint32_t kerror_cusec;
	uint32_t kerror_stime;
	uint32_t kerror_susec;
	string kerror_error_code;
	string kerror_client;
	string kerror_server;
	string kerror_e_text;
	string kerror_e_data;
} kerrorinfo_t;

typedef struct kdcreqinfo {
	string kdcreq_padata_types;
	string kdcreq_kdc_options;
	string kdcreq_client;
	string kdcreq_server;
	uint32_t kdcreq_from;
	uint32_t kdcreq_till;
	uint32_t kdcreq_rtime;
	uint32_t kdcreq_nonce;
	string kdcreq_etype;
	string kdcreq_addresses;
	string kdcreq_authorization_data;
	uint32_t kdcreq_num_additional_tickets;
} kdcreqinfo_t;

typedef struct kdcrepinfo {
	string kdcrep_padata_types;
	string kdcrep_client;
	uint32_t kdcrep_enc_part_kvno;
	string kdcrep_enc_part_etype;
	string kdcrep_enc_key_type;
	uint32_t kdcrep_enc_key_length;
	uintptr_t kdcrep_enc_key_value;
	string kdcrep_enc_last_req;
	uint32_t kdcrep_enc_nonce;
	uint32_t kdcrep_enc_key_expiration;
	string kdcrep_enc_flags;
	uint32_t kdcrep_enc_authtime;
	uint32_t kdcrep_enc_starttime;
	uint32_t kdcrep_enc_endtime;
	uint32_t kdcrep_enc_renew_till;
	string kdcrep_enc_server;
	string kdcrep_enc_caddr;
} kdcrepinfo_t;

typedef struct kticketinfo_t {
	string kticket_server;
	uint32_t kticket_enc_part_kvno;
	string kticket_enc_part_etype;
	string kticket_enc_flags;
	string kticket_enc_key_type;
	uint32_t kticket_enc_key_length;
	uintptr_t kticket_enc_key_value;
	string kticket_enc_client;
	string kticket_enc_transited;
	string kticket_enc_transited_type;
	uint32_t kticket_enc_authtime;
	uint32_t kticket_enc_starttime;
	uint32_t kticket_enc_endtime;
	uint32_t kticket_enc_renew_till;
	string kticket_enc_addresses;
	string kticket_enc_authorization_data;
} kticketinfo_t;

typedef struct kaprepinfo {
	uint32_t kaprep_enc_part_kvno;
	string kaprep_enc_part_etype;
	uint32_t kaprep_enc_ctime;
	uint32_t kaprep_enc_cusec;
	string kaprep_enc_subkey_type;
	uint32_t kaprep_enc_subkey_length;
	uintptr_t kaprep_enc_subkey_value;
	uint32_t kaprep_enc_seq_number;
} kaprepinfo_t;

typedef struct kapreqinfo {
	string kapreq_ap_options;
	uint32_t kapreq_authenticator_kvno;
	string kapreq_authenticator_etype;
} kapreqinfo_t;

typedef struct kauthenticatorinfo {
	string kauth_client;
	string kauth_cksum_type;
	uint32_t kauth_cksum_length;
	uintptr_t kauth_cksum_value;
	uint32_t kauth_cusec;
	uint32_t kauth_ctime;
	string kauth_subkey_type;
	uint32_t kauth_subkey_length;
	uintptr_t kauth_subkey_value;
	uint32_t kauth_seq_number;
	string kauth_authorization_data;
} kauthenticatorinfo_t;

typedef struct ksafeinfo {
	uintptr_t ksafe_user_data;
	uint32_t ksafe_user_data_length;
	uint32_t ksafe_timestamp;
	uint32_t ksafe_usec;
	uint32_t ksafe_seq_number;
	string ksafe_s_address;
	string ksafe_r_address;
	string ksafe_cksum_type;
	uint32_t ksafe_cksum_length;
	uintptr_t ksafe_cksum_value;
} ksafeinfo_t;

typedef struct kprivinfo {
	uint32_t kpriv_enc_part_kvno;
	string kpriv_enc_part_etype;
	uintptr_t kpriv_enc_user_data;
	uint32_t kpriv_enc_user_data_length;
	uint32_t kpriv_enc_timestamp;
	uint32_t kpriv_enc_usec;
	uint32_t kpriv_enc_seq_number;
	string kpriv_enc_s_address;
	string kpriv_enc_r_address;
} kprivinfo_t;

typedef struct kcredinfo {
	uint32_t kcred_enc_part_kvno;
	string kcred_enc_part_etype;
	uint32_t kcred_tickets;
	uint32_t kcred_enc_nonce;
	uint32_t kcred_enc_timestamp;
	uint32_t kcred_enc_usec;
	string kcred_enc_s_address;
	string kcred_enc_r_address;
} kcredinfo_t;

typedef struct kconninfo {
	string kconn_remote;
	string kconn_local;
	string kconn_protocol;
	string kconn_type;
	uint16_t kconn_localport;
	uint16_t kconn_remoteport;
} kconninfo_t;


/*
 * Translators.
 */

BEGIN_XLATOR(krb, info)

	COPYIN(uint8_t, version)

	COPYINSTR(message_type)

	COPYIN(uint64_t, message_id)

	COPYIN(uint32_t, message_length)

	COPYINDATA(message, message_length)
END_XLATOR()

BEGIN_XLATOR(kerror, arg)

	COPYIN(uint32_t, ctime)

	COPYIN(uint32_t, cusec)

	COPYIN(uint32_t, stime)

	COPYIN(uint32_t, susec)

	COPYINSTR(error_code)

	COPYINSTR(client)

	COPYINSTR(server)

	COPYINSTR(e_text)

	COPYINSTR(e_data)
END_XLATOR()

BEGIN_XLATOR(kdcreq, arg)

	COPYINSTR(padata_types)

	COPYINSTR(kdc_options)

	COPYINSTR(client)

	COPYINSTR(server)

	COPYIN(uint32_t, from)

	COPYIN(uint32_t, till)

	COPYIN(uint32_t, rtime)

	COPYIN(uint32_t, nonce)

	COPYINSTR(etype)

	COPYINSTR(addresses)

	COPYINSTR(authorization_data)

	COPYIN(uint32_t, num_additional_tickets)

END_XLATOR()

BEGIN_XLATOR(kdcrep, arg)

	COPYINSTR(padata_types)

	COPYINSTR(client)

	COPYIN(uint32_t, enc_part_kvno)

	COPYINSTR(enc_part_etype)

	COPYINSTR(enc_key_type)

	COPYIN(uint32_t, enc_key_length)

	COPYINDATA(enc_key_value, enc_key_length)

	COPYINSTR(enc_last_req)

	COPYIN(uint32_t, enc_nonce)

	COPYIN(uint32_t, enc_key_expiration)

	COPYINSTR(enc_flags)

	COPYIN(uint32_t, enc_authtime)

	COPYIN(uint32_t, enc_starttime)

	COPYIN(uint32_t, enc_endtime)

	COPYIN(uint32_t, enc_renew_till)

	COPYINSTR(enc_server)

	COPYINSTR(enc_caddr)

END_XLATOR()

BEGIN_XLATOR(kticket, tkt)

	COPYINSTR(server)

	COPYIN(uint32_t, enc_part_kvno)

	COPYINSTR(enc_part_etype)

	COPYINSTR(enc_flags)

	COPYINSTR(enc_key_type)

	COPYIN(uint32_t, enc_key_length)

	COPYINDATA(enc_key_value, enc_key_length)

	COPYINSTR(enc_client)

	COPYINSTR(enc_transited_type)

	COPYINSTR(enc_transited)

	COPYIN(uint32_t, enc_authtime)

	COPYIN(uint32_t, enc_starttime)

	COPYIN(uint32_t, enc_endtime)

	COPYIN(uint32_t, enc_renew_till)

	COPYINSTR(enc_addresses)

	COPYINSTR(enc_authorization_data)
END_XLATOR()

BEGIN_XLATOR(kaprep, arg)

	COPYIN(uint32_t, enc_part_kvno)

	COPYINSTR(enc_part_etype)

	COPYIN(uint32_t, enc_ctime)

	COPYIN(uint32_t, enc_cusec)

	COPYINSTR(enc_subkey_type)

	COPYIN(uint32_t, enc_subkey_length)

	COPYINDATA(enc_subkey_value, enc_subkey_length)

	COPYIN(uint32_t, enc_seq_number)
END_XLATOR()

BEGIN_XLATOR(kapreq, arg)

	COPYINSTR(ap_options)

	COPYIN(uint32_t, authenticator_kvno)

	COPYINSTR(authenticator_etype)
END_XLATOR()

BEGIN_XLATOR(kauthenticator, auth)

	COPYINSTR(client)

	COPYINSTR(cksum_type)

	COPYIN(uint32_t, cksum_length)

	COPYINDATA(cksum_value, cksum_length)

	COPYIN(uint32_t, cusec)

	COPYIN(uint32_t, ctime)

	COPYINSTR(subkey_type)

	COPYIN(uint32_t, subkey_length)

	COPYINDATA(subkey_value, subkey_length)

	COPYIN(uint32_t, seq_number)

	COPYINSTR(authorization_data)
END_XLATOR()

BEGIN_XLATOR(ksafe, arg)

	COPYINDATA(user_data, user_data_length)

	COPYIN(uint32_t, user_data_length)

	COPYIN(uint32_t, timestamp)

	COPYIN(uint32_t, usec)

	COPYIN(uint32_t, seq_number)

	COPYINSTR(s_address)

	COPYINSTR(r_address)

	COPYINSTR(cksum_type)

	COPYIN(uint32_t, cksum_length)

	COPYINDATA(cksum_value, cksum_length)
END_XLATOR()

BEGIN_XLATOR(kpriv, arg)

	COPYINSTR(enc_part_etype)

	COPYIN(uint32_t, enc_part_kvno)

	COPYINDATA(enc_user_data, enc_user_data_length)

	COPYIN(uint32_t, enc_user_data_length)

	COPYIN(uint32_t, enc_timestamp)

	COPYIN(uint32_t, enc_usec)

	COPYIN(uint32_t, enc_seq_number)

	COPYINSTR(enc_s_address)

	COPYINSTR(enc_r_address)
END_XLATOR()

BEGIN_XLATOR(kcred, arg)

	COPYINSTR(enc_part_etype)

	COPYIN(uint32_t, enc_part_kvno)

	COPYIN(uint32_t, tickets)

	COPYIN(uint32_t, enc_nonce)

	COPYIN(uint32_t, enc_timestamp)

	COPYIN(uint32_t, enc_usec)

	COPYINSTR(enc_s_address)

	COPYINSTR(enc_r_address)
END_XLATOR()

BEGIN_XLATOR(kconn, arg)

	COPYINSTR(remote)

	COPYINSTR(local)

	COPYINSTR(protocol)

	COPYINSTR(type)

	COPYIN(uint16_t, localport)

	COPYIN(uint16_t, remoteport)
END_XLATOR()
