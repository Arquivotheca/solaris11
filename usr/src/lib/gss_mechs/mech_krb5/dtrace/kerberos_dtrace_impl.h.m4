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
dnl Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
dnl 
divert(-1)

dnl This file contains definitions for the intermediate format shared between
dnl the Kerberos mech and DTrace. The definitions for DTrace end up in the
dnl library support file, kerberos.d. The definitions for the Kerberos mech are
dnl built into mech_krb5.so.1. If kerberos.d and mech_krb5.so.1 fall out of sync
dnl the kerberos provider will no longer work (as DTrace will not know the
dnl format of the arguments it copies in from mech_krb5 when a probe fires).
dnl This file is run through m4 twice:
dnl 
dnl 1) By directly generating kerberos_dtrace_impl.h which is included in
dnl    mech_krb5.
dnl 2) By being included in kerberos.d.m4 and thus ending up in kerberos.d used
dnl    by DTrace.

dnl A DTrace library support file should support both 32bit and 64bit data
dnl models for user processes. One way of determining what data model a
dnl particular process is using is to look at curthread->t_procp->p_model.
dnl Unfortunately this will only work in the global zone as it requires
dnl privileges which cannot be assigned to a local zone. In order to work around
dnl this limitation the data structures below are arranged so that they are
dnl identical when built as 32bit or 64bit. When building 32bit code
dnl (sparc/i386) any pointers are padded out to 64bits. All structures are
dnl padded out to 8 bytes (see various "uint32_t _pad"). This has the added
dnl advantage of simplifying the library support file as it no longer has to
dnl support different paths for 32bit and 64bit. The main downsides to doing it
dnl this way are added complexity and the strict requirement that all data
dnl structures are zero'ed out before use (as pointers must be padded on 32bit).

ifelse(
sparc, 1,
`define(`PTR',`uint32_t _pad_$2;
	$1 *$2')',

i386, 1,
`define(`PTR',`$1 *$2;
	uint32_t _pad_$2')',

`define(`PTR',`$1 *$2')')

divert(0)dnl
dnl
dnl These data structures closely match the external DTrace interfaces.
dnl
typedef struct k5_krbinfo {
	PTR(char, message_type);
	PTR(const char, message_id);
	PTR(char, message);
	uint32_t message_length;
	uint8_t version;
} k5_krbinfo_t;

typedef struct k5_kerrorinfo {
	PTR(char, error_code);
	PTR(char, client);
	PTR(char, server);
	PTR(char, e_text);
	PTR(char, e_data);
	uint32_t ctime;
	uint32_t cusec;
	uint32_t stime;
	uint32_t susec;
} k5_kerrorinfo_t;

typedef struct k5_kdcrepinfo {
	PTR(char, padata_types);
	PTR(char, client);
	PTR(char, enc_part_etype);
	PTR(char, enc_key_type);
	PTR(unsigned char, enc_key_value);
	PTR(char, enc_last_req);
	PTR(char, enc_flags);
	PTR(char, enc_server);
	PTR(char, enc_caddr);
	uint32_t enc_part_kvno;
	uint32_t enc_key_length;
	uint32_t enc_nonce;
	uint32_t enc_key_expiration;
	uint32_t enc_authtime;
	uint32_t enc_starttime;
	uint32_t enc_endtime;
	uint32_t enc_renew_till;
} k5_kdcrepinfo_t;

typedef struct k5_kaprepinfo {
	PTR(char, enc_part_etype);
	PTR(char, enc_subkey_type);
	PTR(unsigned char, enc_subkey_value);
	uint32_t enc_part_kvno;
	uint32_t enc_ctime;
	uint32_t enc_cusec;
	uint32_t enc_subkey_length;
	uint32_t enc_seq_number;
	uint32_t _pad;
} k5_kaprepinfo_t;

typedef struct k5_kauthenticatorinfo {
	PTR(char, client);
	PTR(char, cksum_type);
	PTR(unsigned char, cksum_value);
	PTR(char, subkey_type);
	PTR(unsigned char, subkey_value);
	PTR(char, authorization_data);
	uint32_t cksum_length;
	uint32_t cusec;
	uint32_t ctime;
	uint32_t subkey_length;
	uint32_t seq_number;
	uint32_t _pad;
} k5_kauthenticatorinfo_t;

typedef struct k5_ksafeinfo {
	PTR(char, user_data);
	PTR(char, s_address);
	PTR(char, r_address);
	PTR(char, cksum_type);
	PTR(unsigned char, cksum_value);
	uint32_t user_data_length;
	uint32_t timestamp;
	uint32_t usec;
	uint32_t seq_number;
	uint32_t cksum_length;
	uint32_t _pad;
} k5_ksafeinfo_t;

typedef struct k5_kprivinfo {
	PTR(char, enc_part_etype);
	PTR(char, enc_user_data);
	PTR(char, enc_s_address);
	PTR(char, enc_r_address);
	uint32_t enc_part_kvno;
	uint32_t enc_user_data_length;
	uint32_t enc_timestamp;
	uint32_t enc_usec;
	uint32_t enc_seq_number;
	uint32_t _pad;
} k5_kprivinfo_t;

typedef struct k5_kcredinfo {
	PTR(char, enc_part_etype);
	PTR(char, enc_s_address);
	PTR(char, enc_r_address);
	uint32_t enc_part_kvno;
	uint32_t tickets;
	uint32_t enc_nonce;
	uint32_t enc_timestamp;
	uint32_t enc_usec;
	uint32_t _pad;
} k5_kcredinfo_t;

typedef struct k5_kconninfo {
	PTR(char, remote);
	PTR(char, local);
	PTR(char, protocol);
	PTR(char, type);
	uint16_t localport;
	uint16_t remoteport;
	uint32_t _pad;
} k5_kconninfo_t;

typedef struct k5_kticketinfo {
	PTR(char,  server);
	PTR(char,  enc_part_etype);
	PTR(char,  enc_flags);
	PTR(char,  enc_key_type);
	PTR(unsigned char,  enc_key_value);
	PTR(char,  enc_client);
	PTR(char,  enc_transited);
	PTR(char,  enc_transited_type);
	PTR(char,  enc_addresses);
	PTR(char,  enc_authorization_data);
	uint32_t enc_part_kvno;
	uint32_t enc_key_length;
	uint32_t enc_authtime;
	uint32_t enc_starttime;
	uint32_t enc_endtime;
	uint32_t enc_renew_till;
} k5_kticketinfo_t;

typedef struct k5_kdcreqinfo {
	PTR(char, padata_types);
	PTR(char, kdc_options);
	PTR(char, client);
	PTR(char, server);
	PTR(char, etype);
	PTR(char, addresses);
	PTR(char, authorization_data);
	uint32_t from;
	uint32_t till;
	uint32_t rtime;
	uint32_t nonce;
	uint32_t num_additional_tickets;
	uint32_t _pad;
} k5_kdcreqinfo_t;

typedef struct k5_kapreqinfo {
	PTR(char, ap_options);
	PTR(char, authenticator_etype);
	uint32_t authenticator_kvno;
	uint32_t _pad;
} k5_kapreqinfo_t;

typedef struct k5_trace {
	PTR(k5_krbinfo_t, info);
	PTR(void, arg);
	PTR(k5_kticketinfo_t, tkt);
	PTR(k5_kauthenticatorinfo_t, auth);
} k5_trace_t;
