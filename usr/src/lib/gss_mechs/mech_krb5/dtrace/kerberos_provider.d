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

/*
 * Although the following "dummy" structures and translators are not used they
 * are necessary to prevent dtrace from error'ing out when generating
 * kerberos_provider.h.
 */
typedef struct k5_trace {
	int dummy;
} k5_trace_t;

typedef struct krbinfo {
	int dummy;
} krbinfo_t;

typedef struct kticketinfo {
	int dummy;
} kticketinfo_t;

typedef struct kauthenticatorinfo {
	int dummy;
} kauthenticatorinfo_t;

typedef struct kconninfo {
	int dummy;
} kconninfo_t;

typedef struct kerrorinfo {
	int dummy;
} kerrorinfo_t;

typedef struct kdcreqinfo {
	int dummy;
} kdcreqinfo_t;

typedef struct kdcrepinfo {
	int dummy;
} kdcrepinfo_t;

typedef struct kapreqinfo {
	int dummy;
} kapreqinfo_t;

typedef struct kaprepinfo {
	int dummy;
} kaprepinfo_t;

typedef struct ksafeinfo {
	int dummy;
} ksafeinfo_t;

typedef struct kprivinfo {
	int dummy;
} kprivinfo_t;

typedef struct kcredinfo {
	int dummy;
} kcredinfo_t;

translator kconninfo_t < k5_trace_t *x > {
	dummy = x->dummy;
};

translator kerrorinfo_t < k5_trace_t *x > {
	dummy = x->dummy;
};

translator kdcreqinfo_t < k5_trace_t *x > {
	dummy = x->dummy;
};

translator kdcrepinfo_t < k5_trace_t *x > {
	dummy = x->dummy;
};

translator kapreqinfo_t < k5_trace_t *x > {
	dummy = x->dummy;
};

translator kaprepinfo_t < k5_trace_t *x > {
	dummy = x->dummy;
};

translator krbinfo_t < k5_trace_t *x > {
	dummy = x->dummy;
};

translator kticketinfo_t < k5_trace_t *x > {
	dummy = x->dummy;
};

translator kauthenticatorinfo_t < k5_trace_t *x > {
	dummy = x->dummy;
};

translator ksafeinfo_t < k5_trace_t *x > {
	dummy = x->dummy;
};

translator kprivinfo_t < k5_trace_t *x > {
	dummy = x->dummy;
};

translator kcredinfo_t < k5_trace_t *x > {
	dummy = x->dummy;
};

provider kerberos {
	probe krb_message__send(k5_trace_t *x):
	    (krbinfo_t *x, kconninfo_t *x);

	probe krb_message__recv(k5_trace_t *x):
	    (krbinfo_t *x, kconninfo_t *x);

	probe krb_kdc_req__make(k5_trace_t *x):
	    (krbinfo_t *x, kdcreqinfo_t *x);

	probe krb_kdc_req__read(k5_trace_t *x):
	    (krbinfo_t *x, kdcreqinfo_t *x);

	probe krb_kdc_rep__make(k5_trace_t *x):
	    (krbinfo_t *x, kdcrepinfo_t *x, kticketinfo_t *x);

	probe krb_kdc_rep__read(k5_trace_t *x):
	    (krbinfo_t *x, kdcrepinfo_t *x, kticketinfo_t *x);

	probe krb_ap_req__make(k5_trace_t *x):
	    (krbinfo_t *x, kapreqinfo_t *x, kticketinfo_t *x,
	    kauthenticatorinfo_t *x);

	probe krb_ap_req__read(k5_trace_t *x):
	    (krbinfo_t *x, kapreqinfo_t *x, kticketinfo_t *x,
	    kauthenticatorinfo_t *x);

	probe krb_ap_rep__make(k5_trace_t *x):
	    (krbinfo_t *x, kaprepinfo_t *x);

	probe krb_ap_rep__read(k5_trace_t *x):
	    (krbinfo_t *x, kaprepinfo_t *x);

	probe krb_error__make(k5_trace_t *x):
	    (krbinfo_t *x, kerrorinfo_t *x);

	probe krb_error__read(k5_trace_t *x):
	    (krbinfo_t *x, kerrorinfo_t *x);

	probe krb_safe__make(k5_trace_t *x):
	    (krbinfo_t *x, ksafeinfo_t *x);

	probe krb_safe__read(k5_trace_t *x):
	    (krbinfo_t *x, ksafeinfo_t *x);

	probe krb_priv__make(k5_trace_t *x):
	    (krbinfo_t *x, kprivinfo_t *x);

	probe krb_priv__read(k5_trace_t *x):
	    (krbinfo_t *x, kprivinfo_t *x);

	probe krb_cred__make(k5_trace_t *x):
	    (krbinfo_t *x, kcredinfo_t *x);

	probe krb_cred__read(k5_trace_t *x):
	    (krbinfo_t *x, kcredinfo_t *x);
};

#pragma D attributes Evolving/Evolving/Common provider kerberos provider
#pragma D attributes Private/Private/Unknown provider kerberos module
#pragma D attributes Private/Private/Unknown provider kerberos function
#pragma D attributes Evolving/Evolving/Common provider kerberos name
#pragma D attributes Unstable/Unstable/Common provider kerberos args
