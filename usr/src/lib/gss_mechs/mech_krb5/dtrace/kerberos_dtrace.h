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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef KERBEROS_DTRACE_H
#define	KERBEROS_DTRACE_H

#include "krb5.h"
#include "kerberos_dtrace_impl.h"

k5_krbinfo_t *k5_krbinfo_build(const krb5_data *);
void k5_krbinfo_free(k5_krbinfo_t *);

k5_kerrorinfo_t *k5_kerrorinfo_build(const krb5_error *);
void k5_kerrorinfo_free(k5_kerrorinfo_t *);

k5_kdcreqinfo_t *k5_kdcreqinfo_build(const krb5_kdc_req *);
void k5_kdcreqinfo_free(k5_kdcreqinfo_t *);

k5_kdcrepinfo_t *k5_kdcrepinfo_build(const krb5_kdc_rep *,
    const krb5_enc_kdc_rep_part *);
void k5_kdcrepinfo_free(k5_kdcrepinfo_t *);

k5_kticketinfo_t *k5_kticketinfo_build(const krb5_ticket *);
void k5_kticketinfo_free(k5_kticketinfo_t *);

k5_kaprepinfo_t *k5_kaprepinfo_build(const krb5_ap_rep *,
    const krb5_ap_rep_enc_part *);
void k5_kaprepinfo_free(k5_kaprepinfo_t *);

k5_kapreqinfo_t *k5_kapreqinfo_build(const krb5_ap_req *);
void k5_kapreqinfo_free(k5_kapreqinfo_t *);

k5_kauthenticatorinfo_t *k5_kauthenticatorinfo_build(
    const krb5_authenticator *);
void k5_kauthenticatorinfo_free(k5_kauthenticatorinfo_t *);

k5_ksafeinfo_t *k5_ksafeinfo_build(const krb5_safe *);
void k5_ksafeinfo_free(k5_ksafeinfo_t *);

k5_kprivinfo_t *k5_kprivinfo_build(const krb5_priv *,
    const krb5_priv_enc_part *);
void k5_kprivinfo_free(k5_kprivinfo_t *);

k5_kcredinfo_t *k5_kcredinfo_build(const krb5_cred *,
    const krb5_cred_enc_part *);
void k5_kcredinfo_free(k5_kcredinfo_t *);

k5_kconninfo_t *k5_kconninfo_build(const int);
void k5_kconninfo_free(k5_kconninfo_t *);

void k5_trace_kdc_rep_read(const krb5_data *, const krb5_kdc_rep *);
void k5_trace_kdc_req_read(const krb5_data *, const krb5_kdc_req *);

void k5_trace_message_send(const int, char *, const unsigned int);
void k5_trace_message_recv(const int, char *, const unsigned int);

/*
 * Macros for DTrace probe points for the kerberos provider. Each probe point
 * has a corresponding macro here. Each macro follows a similar pattern:
 * 1. Check for enabled probe point.
 * 2. Setup probe arguments allocating memory as required.
 * 3. Fire probe.
 * 4. Cleanup.
 *
 * The *_ENABLED macros are provided by DTrace and allow the code to avoid
 * unnecessary argument setup and cleanup when the probes are not enabled. The
 * actual cost of an individual *_ENABLED probe is minimal.
 */

#define	KERBEROS_PROBE_KRB_ERROR(type, asn1msg, error)		\
	if (KERBEROS_KRB_ERROR_##type##_ENABLED()) {		\
		k5_trace_t ktrace;				\
		memset(&ktrace, 0, sizeof (k5_trace_t));	\
								\
		ktrace.info = k5_krbinfo_build((asn1msg));	\
		ktrace.arg = k5_kerrorinfo_build((error));	\
		KERBEROS_KRB_ERROR_##type(&ktrace);		\
		k5_kerrorinfo_free(ktrace.arg);			\
		k5_krbinfo_free(ktrace.info);			\
	}

#define	KERBEROS_PROBE_KRB_KDC_REQ(type, asn1msg, kdcreq)	\
	if (KERBEROS_KRB_KDC_REQ_##type##_ENABLED()) {		\
		k5_trace_t ktrace;				\
		memset(&ktrace, 0, sizeof (k5_trace_t));	\
								\
		ktrace.info = k5_krbinfo_build((asn1msg));	\
		ktrace.arg = k5_kdcreqinfo_build((kdcreq));	\
		KERBEROS_KRB_KDC_REQ_##type(&ktrace);		\
		k5_kdcreqinfo_free(ktrace.arg);			\
		k5_krbinfo_free(ktrace.info);			\
	}

#define	KERBEROS_PROBE_KRB_KDC_REP(type, asn1msg, kdcrep, encp, ticket)	\
	if (KERBEROS_KRB_KDC_REP_##type##_ENABLED()) {		\
		k5_trace_t ktrace;				\
		memset(&ktrace, 0, sizeof (k5_trace_t));	\
								\
		ktrace.info = k5_krbinfo_build((asn1msg));	\
		ktrace.arg = k5_kdcrepinfo_build((kdcrep), (encp));	\
		ktrace.tkt = k5_kticketinfo_build(ticket);	\
		KERBEROS_KRB_KDC_REP_##type(&ktrace);		\
		k5_kticketinfo_free(ktrace.tkt);		\
		k5_kdcrepinfo_free(ktrace.arg);			\
		k5_krbinfo_free(ktrace.info);			\
	}

#define	KERBEROS_PROBE_KRB_AP_REP(type, asn1msg, aprep, encp)	\
	if (KERBEROS_KRB_AP_REP_##type##_ENABLED()) {		\
		k5_trace_t ktrace;				\
		memset(&ktrace, 0, sizeof (k5_trace_t));	\
								\
		ktrace.info = k5_krbinfo_build((asn1msg));	\
		ktrace.arg = k5_kaprepinfo_build((aprep), (encp));	\
		KERBEROS_KRB_AP_REP_##type(&ktrace);		\
		k5_kaprepinfo_free(ktrace.arg);			\
		k5_krbinfo_free(ktrace.info);			\
	}

#define	KERBEROS_PROBE_KRB_AP_REQ(type, asn1msg, apreq, authen, ticket)	\
	if (KERBEROS_KRB_AP_REQ_##type##_ENABLED()) {		\
		k5_trace_t ktrace;				\
		memset(&ktrace, 0, sizeof (k5_trace_t));	\
								\
		ktrace.info = k5_krbinfo_build((asn1msg));	\
		ktrace.arg = k5_kapreqinfo_build((apreq));	\
		ktrace.tkt = k5_kticketinfo_build((ticket));	\
		ktrace.auth = k5_kauthenticatorinfo_build((authen));	\
		KERBEROS_KRB_AP_REQ_##type(&ktrace);		\
		k5_kauthenticatorinfo_free(ktrace.auth);	\
		k5_kticketinfo_free(ktrace.tkt);		\
		k5_kapreqinfo_free(ktrace.arg);			\
		k5_krbinfo_free(ktrace.info);			\
	}

#define	KERBEROS_PROBE_KRB_SAFE(type, asn1msg, safe)		\
	if (KERBEROS_KRB_SAFE_##type##_ENABLED()) {		\
		k5_trace_t ktrace;				\
		memset(&ktrace, 0, sizeof (k5_trace_t));	\
								\
		ktrace.info = k5_krbinfo_build((asn1msg));	\
		ktrace.arg = k5_ksafeinfo_build((safe));	\
		KERBEROS_KRB_SAFE_##type(&ktrace);		\
		k5_ksafeinfo_free(ktrace.arg);			\
		k5_krbinfo_free(ktrace.info);			\
	}

#define	KERBEROS_PROBE_KRB_PRIV(type, asn1msg, priv, encp)	\
	if (KERBEROS_KRB_PRIV_##type##_ENABLED()) {		\
		k5_trace_t ktrace;				\
		memset(&ktrace, 0, sizeof (k5_trace_t));	\
								\
		ktrace.info = k5_krbinfo_build((asn1msg));	\
		ktrace.arg = k5_kprivinfo_build((priv), (encp));	\
		KERBEROS_KRB_PRIV_##type(&ktrace);		\
		k5_kprivinfo_free(ktrace.arg);			\
		k5_krbinfo_free(ktrace.info);			\
	}

#define	KERBEROS_PROBE_KRB_CRED(type, asn1msg, cred, encp)	\
	if (KERBEROS_KRB_CRED_##type##_ENABLED()) {		\
		k5_trace_t ktrace;				\
		memset(&ktrace, 0, sizeof (k5_trace_t));	\
								\
		ktrace.info = k5_krbinfo_build((asn1msg));	\
		ktrace.arg = k5_kcredinfo_build((cred), (encp));	\
		KERBEROS_KRB_CRED_##type(&ktrace);		\
		k5_kcredinfo_free(ktrace.arg);			\
		k5_krbinfo_free(ktrace.info);			\
	}

#define	KERBEROS_PROBE_KRB_MESSAGE(type, fd, data, length)	\
	if (KERBEROS_KRB_MESSAGE_##type##_ENABLED()) {		\
		k5_trace_t ktrace;				\
		krb5_data d;					\
								\
		memset(&ktrace, 0, sizeof (k5_trace_t));	\
		d.data = data;					\
		d.length = length;				\
		ktrace.info = k5_krbinfo_build(&d);		\
		ktrace.arg = k5_kconninfo_build((fd));		\
		KERBEROS_KRB_MESSAGE_##type(&ktrace);		\
		k5_kconninfo_free(ktrace.arg);			\
		k5_krbinfo_free(ktrace.info);			\
	}

#include "kerberos_provider.h"

#endif /* KERBEROS_DTRACE_H */
