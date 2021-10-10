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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libintl.h>

#include "k5-int.h"
#include "krb5.h"
#include "kerberos_dtrace.h"

/*
 * Lookup functions for various Kerberos types - errors, encryption types,
 * message types etc. Lookup functions generally take an integer and return a
 * string (pointer to static memory). They cannot fail returning only NULL if
 * the value cannot be found. Types and their string representations  were
 * mainly taken from RFC4120 and the mech_krb5 source.
 */

static const char *
k5_msgtype_lookup(const int type) {
	switch (type) {
		case 10: return ("KRB_AS_REQ(10)");
		case 11: return ("KRB_AS_REP(11)");
		case 12: return ("KRB_TGS_REQ(12)");
		case 13: return ("KRB_TGS_REP(13)");
		case 14: return ("KRB_AP_REQ(14)");
		case 15: return ("KRB_AP_REP(15)");
		case 16: return ("KRB_RESERVED(16)");
		case 17: return ("KRB_RESERVED(17)");
		case 20: return ("KRB_SAFE(20)");
		case 21: return ("KRB_PRIV(21)");
		case 22: return ("KRB_CRED(22)");
		case 30: return ("KRB_ERROR(30)");
		default: return (NULL);
	}
}

static const char *
k5_errtype_lookup(const int type) {
	switch (type) {
		case 0: return ("KDC_ERR_NONE(0)");
		case 1: return ("KDC_ERR_NAME_EXP(1)");
		case 2: return ("KDC_ERR_SERVICE_EXP(2)");
		case 3: return ("KDC_ERR_BAD_PVNO(3)");
		case 4: return ("KDC_ERR_C_OLD_MAST_KVNO(4)");
		case 5: return ("KDC_ERR_S_OLD_MAST_KVNO(5)");
		case 6: return ("KDC_ERR_C_PRINCIPAL_UNKNOWN(6)");
		case 7: return ("KDC_ERR_S_PRINCIPAL_UNKNOWN(7)");
		case 8: return ("KDC_ERR_PRINCIPAL_NOT_UNIQUE(8)");
		case 9: return ("KDC_ERR_NULL_KEY(9)");
		case 10: return ("KDC_ERR_CANNOT_POSTDATE(10)");
		case 11: return ("KDC_ERR_NEVER_VALID(11)");
		case 12: return ("KDC_ERR_POLICY(12)");
		case 13: return ("KDC_ERR_BADOPTION(13)");
		case 14: return ("KDC_ERR_ENCTYPE_NOSUPP(14)");
		case 15: return ("KDC_ERR_SUMTYPE_NOSUPP(15)");
		case 16: return ("KDC_ERR_PADATA_TYPE_NOSUPP(16)");
		case 17: return ("KDC_ERR_TRTYPE_NOSUPP(17)");
		case 18: return ("KDC_ERR_CLIENT_REVOKED(18)");
		case 19: return ("KDC_ERR_SERVICE_REVOKED(19)");
		case 20: return ("KDC_ERR_TGT_REVOKED(20)");
		case 21: return ("KDC_ERR_CLIENT_NOTYET(21)");
		case 22: return ("KDC_ERR_SERVICE_NOTYET(22)");
		case 23: return ("KDC_ERR_KEY_EXP(23)");
		case 24: return ("KDC_ERR_PREAUTH_FAILED(24)");
		case 25: return ("KDC_ERR_PREAUTH_REQUIRED(25)");
		case 26: return ("KDC_ERR_SERVER_NOMATCH(26)");
		case 27: return ("KDC_ERR_MUST_USE_USER2USER(27)");
		case 28: return ("KDC_ERR_PATH_NOT_ACCEPTED(28)");
		case 29: return ("KDC_ERR_SVC_UNAVAILABLE(29)");
		case 31: return ("KRB_AP_ERR_BAD_INTEGRITY(31)");
		case 32: return ("KRB_AP_ERR_TKT_EXPIRED(32)");
		case 33: return ("KRB_AP_ERR_TKT_NYV(33)");
		case 34: return ("KRB_AP_ERR_REPEAT(34)");
		case 35: return ("KRB_AP_ERR_NOT_US(35)");
		case 36: return ("KRB_AP_ERR_BADMATCH(36)");
		case 37: return ("KRB_AP_ERR_SKEW(37)");
		case 38: return ("KRB_AP_ERR_BADADDR(38)");
		case 39: return ("KRB_AP_ERR_BADVERSION(39)");
		case 40: return ("KRB_AP_ERR_MSG_TYPE(40)");
		case 41: return ("KRB_AP_ERR_MODIFIED(41)");
		case 42: return ("KRB_AP_ERR_BADORDER(42)");
		case 44: return ("KRB_AP_ERR_BADKEYVER(44)");
		case 45: return ("KRB_AP_ERR_NOKEY(45)");
		case 46: return ("KRB_AP_ERR_MUT_FAIL(46)");
		case 47: return ("KRB_AP_ERR_BADDIRECTION(47)");
		case 48: return ("KRB_AP_ERR_METHOD(48)");
		case 49: return ("KRB_AP_ERR_BADSEQ(49)");
		case 50: return ("KRB_AP_ERR_INAPP_CKSUM(50)");
		case 51: return ("KRB_AP_PATH_NOT_ACCEPTED(51)");
		case 52: return ("KRB_ERR_RESPONSE_TOO_BIG(52)");
		case 60: return ("KRB_ERR_GENERIC(60)");
		case 61: return ("KRB_ERR_FIELD_TOOLONG(61)");
		case 62: return ("KDC_ERR_CLIENT_NOT_TRUSTED(62)");
		case 63: return ("KDC_ERR_KDC_NOT_TRUSTED(63)");
		case 64: return ("KDC_ERR_INVALID_SIG(64)");
		case 65: return ("KDC_ERR_DH_KEY_PARAMETERS_NOT_ACCEPTED(65)");
		case 66: return ("KDC_ERR_CERTIFICATE_MISMATCH(66)");
		case 67: return ("KRB_AP_ERR_NO_TGT(67)");
		case 68: return ("KDC_ERR_WRONG_REALM(68)");
		case 69: return ("KRB_AP_ERR_USER_TO_USER_REQUIRED(69)");
		case 70: return ("KDC_ERR_CANT_VERIFY_CERTIFICATE(70)");
		case 71: return ("KDC_ERR_INVALID_CERTIFICATE(71)");
		case 72: return ("KDC_ERR_REVOKED_CERTIFICATE(72)");
		case 73: return ("KDC_ERR_REVOCATION_STATUS_UNKNOWN(73)");
		case 74: return ("KDC_ERR_REVOCATION_STATUS_UNAVAILABLE(74)");
		case 75: return ("KDC_ERR_CLIENT_NAME_MISMATCH(75)");
		case 76: return ("KDC_ERR_KDC_NAME_MISMATCH(76)");
		case 77: return ("KDC_ERR_INCONSISTENT_KEY_PURPOSE(77)");
		case 78: return ("KDC_ERR_DIGEST_IN_CERT_NOT_ACCEPTED(78)");
		case 79: return ("KDC_ERR_PA_CHECKSUM_MUST_BE_INCLUDED(79)");
		case 80: return (
		    "KDC_ERR_DIGEST_IN_SIGNED_DATA_NOT_ACCEPTED(80)");
		case 81: return (
		    "KDC_ERR_PUBLIC_KEY_ENCRYPTION_NOT_SUPPORTED(81)");
		default: return (NULL);
	}
}

static const char *
k5_patype_lookup(const krb5_preauthtype type) {
	switch (type) {
		case 0: return ("NONE(0)");
		case 1: return ("AP_REQ(1)");
		case 2: return ("ENC_TIMESTAMP(2)");
		case 3: return ("PW_SALT(3)");
		case 4: return ("ENC_ENCKEY(4)");
		case 5: return ("ENC_UNIX_TIME(5)");
		case 6: return ("ENC_SANDIA_SECURID(6)");
		case 7: return ("SESAME(7)");
		case 8: return ("OSF_DCE(8)");
		case 9: return ("CYBERSAFE_SECUREID(9)");
		case 10: return ("AFS3_SALT(10)");
		case 11: return ("ETYPE_INFO(11)");
		case 12: return ("SAM_CHALLENGE(12)");
		case 13: return ("SAM_RESPONSE(13)");
		case 14: return ("PK_AS_REQ_OLD(14)");
		case 15: return ("PK_AS_REP_OLD(15)");
		case 16: return ("PK_AS_REQ(16)");
		case 17: return ("PK_AS_REP(17)");
		case 19: return ("PK_ETYPE_INFO2(19)");
		case 25: return ("REFERRAL(25)");
		case 30: return ("SAM_CHALLENGE_2(30)");
		case 31: return ("SAM_RESPONSE_2(31)");
		case 128: return ("PAC_REQUEST(128)");
		case 129: return ("FOR_USER(129)");
		case 130: return ("S4U_X509_USER(130)");
		case 133: return ("FX_COOKIE(133)");
		case 136: return ("FX_FAST(136)");
		case 137: return ("FX_ERROR(137)");
		case 138: return ("ENCRYPTED_CHALLENGE(138)");
		case 147: return ("PKINIT_KX(147)");
		case 149: return ("REQ_ENC_PA_REP(149)");
		default: return (NULL);
	}
}

static const char *
k5_etype_lookup(const krb5_enctype type) {
	switch (type) {
		case 0x18 : return ("arcfour-hmac-md5-exp(0x18)");
		case 0x17 : return ("arcfour-hmac-md5(0x17)");
		case 0x12 : return ("aes256-cts-hmac-sha1-96(0x12)");
		case 0x11 : return ("aes128-cts-hmac-sha1-96(0x11)");
		case 0x10 : return ("des3-cbc-sha1(0x10)");
		case 0x8 : return ("des-hmac-sha1(0x8)");
		case 0x6 : return ("des3-cbc-raw(0x6)");
		case 0x5 : return ("des3-cbc-sha(0x5)");
		case 0x4 : return ("des-cbc-raw(0x4)");
		case 0x3 : return ("des-cbc-md5(0x3)");
		case 0x2 : return ("des-cbc-md4(0x2)");
		case 0x1 : return ("des-cbc-crc(0x1)");
		case 0x0 : return ("null(0x0)");
		default: return (NULL);
	}
}

static const char *
k5_cktype_lookup(const krb5_cksumtype type) {
	switch (type) {
		case -138 : return ("hmac-md5-arcfour(-138)");
		case 0x8003 : return ("gssapi(0x8003)");
		case 0x10 : return ("hmac-sha1-96-aes256(0x10)");
		case 0xf : return ("hmac-sha1-96-aes128(0xf)");
		case 0xc : return ("hmac-sha1-des3(0xc)");
		case 0x9 : return ("sha(0x9)");
		case 0x8 : return ("md5-des(0x8)");
		case 0x7 : return ("md5(0x7)");
		case 0x4 : return ("des-cbc(0x4)");
		case 0x3 : return ("md4-des(0x3)");
		case 0x2 : return ("md4(0x2)");
		case 0x1 : return ("crc32(0x1)");
		default: return (NULL);
	}
}

static const char *
k5_adtype_lookup(const krb5_authdatatype type) {
	switch (type) {
		case 0 : return ("NONE(0)");
		case 1 : return ("AD-IF-RELEVANT(1)");
		case 4 : return ("AD-KDCIssued(4)");
		case 5 : return ("AD-AND-OR(5)");
		case 8 : return ("AD-MANDATORY-FOR-KDC(8)");
		case 9 : return ("AD_INITIAL_VERIFIED_CAS(9)");
		case 64: return ("AD_OSF_DCE(64)");
		case 65: return ("AD_SESAME(65");
		case 71: return ("AD_FX_ARMOR(71)");
		case 128: return ("AD_WIN2K_PAC(128)");
		case 129: return ("AD_ETYPE_NEGOTIATION(129)");
		case 512: return ("AD_SIGNTICKET(512)");
		default: return (NULL);
	}
}

static const char *
k5_lrtype_lookup(const krb5_int32 type) {
	switch (type) {
		case 0 : return ("NONE(0)");
		case 1 : return ("ALL_LAST_TGT(1)");
		case -1 : return ("ONE_LAST_TGT(-1)");
		case 2 : return ("ALL_LAST_INITIAL(2)");
		case -2 : return ("ONE_LAST_INITIAL(-2)");
		case 3 : return ("ALL_LAST_TGT_ISSUED(3)");
		case -3 : return ("ONE_LAST_TGT_ISSUED(-3)");
		case 4 : return ("ALL_LAST_RENEWAL(4)");
		case -4 : return ("ONE_LAST_RENEWAL(-4)");
		case 5 : return ("ALL_LAST_REQ(5)");
		case -5 : return ("ONE_LAST_REQ(-5)");
		case 6 : return ("ALL_PW_EXPTIME(6)");
		case -6 : return ("ONE_PW_EXPTIME(-6)");
		case 7 : return ("ALL_ACCT_EXPTIME(7)");
		case -7 : return ("ONE_ACCT_EXPTIME(-7)");
		default: return (NULL);
	}
}

static const char *
k5_trtype_lookup(const krb5_int32 type) {
	switch (type) {
		case 0 : return ("(0)");
		case 1 : return ("DOMAIN-X500-COMPRESS(1)");
		default: return (NULL);
	}
}

static const char *
k5_flag_lookup(const unsigned int flag) {
	switch (flag) {
		case 0x00000000 : return ("");
		case 0x40000000 : return ("forwardable(1)");
		case 0x20000000 : return ("forwarded(2)");
		case 0x10000000 : return ("proxiable(3)");
		case 0x08000000 : return ("proxy(4)");
		case 0x04000000 : return ("may-postdate(5)");
		case 0x02000000 : return ("postdated(6)");
		case 0x01000000 : return ("invalid(7)");
		case 0x00800000 : return ("renewable(8)");
		case 0x00400000 : return ("initial(9)");
		case 0x00200000 : return ("pre-authent(10)");
		case 0x00100000 : return ("hw-authent(11)");
		case 0x00080000 : return ("transited-policy-checked(12)");
		case 0x00040000 : return ("ok-as-delegate(13)");
		case 0x00010000 : return ("canonicalize(15)");
		case 0x00000020 : return ("disable-transited-check(26)");
		case 0x00000010 : return ("renewable-ok(27)");
		case 0x00000008 : return ("enc-tkt-in-skey(28)");
		case 0x00000002 : return ("renew(30)");
		case 0x00000001 : return ("validate(31)");
		default: return (NULL);
	}
}

/*
 * *_to_str functions are similar to the *_lookup functions however the returned
 * string must be freed.  NULL may be returned due to a memory allocation
 * failure. The *to_str functions return a useful string when a *_lookup
 * function would return NULL.
 */

/*
 * A generic wrapper around *_lookup functions which returns a useful string
 * when a type cannot be found.
 * e.g.  "<unknown(999)>"
 * Takes a pointer to a lookup function which returns a string on sucess or NULL
 * if the type cannot be found.
 * Returned value must be freed.
 */
static char *
k5_type_to_str(const char *(*lookup)(const int), const int type) {
	char *ret = NULL;
	const char *str = (*lookup)(type);

	if (str == NULL)
		(void) asprintf(&ret, "<%s(%d)>",
		    dgettext(TEXT_DOMAIN, "unknown"), type);
	else
		ret = strdup(str);

	return (ret);
}

/*
 * Given a NULL terminated array ("arr") build up a string by calling "to_str"
 * for each element of the array. The returned string (like all *_to_str
 * functions) should be freed.
 * Returns NULL on memory allocation failure or empty array.
 */
static char *
k5_array_to_str(char *(*to_str)(const void *), const void **arr) {
	char *t, *str = NULL;
	unsigned int i;

	if (arr != NULL) {
		for (i = 0; arr[i] != NULL; i++) {
			t = (*to_str)(arr[i]);
			if (t != NULL) {
				if (str == NULL) {
					str = t;
				} else {
					char *tmp;
					(void) asprintf(&tmp, "%s %s", str, t);
					if (tmp != NULL) {
						free(str);
						str = tmp;
					}
					free(t);
					t = NULL;
				}
			}
		}
	}

	return (str);
}

/*
 * Kerberos flags are encoded in a single 32bit integer with each bit
 * representing a flag. Each possible flag is tested for by applying a mask
 * which is bit-shifted for each iteration.
 * Returns a string representation of Kerberos flags which should be freed. Can
 * return NULL on memory allocation error.
 */
static char *
k5_flags_to_str(const krb5_flags flags) {
	const char *t = NULL;
	char *tmp = NULL, *str = NULL;
	unsigned int i, mask = 1;

	/* Print out "flags" in hex with leading zeros */
	(void) asprintf(&str, "0x%.8x:", flags);
	if (str != NULL) {
		for (i = 0; i < sizeof (unsigned int) * 8; i++) {
			t = k5_flag_lookup(flags & mask);
			mask = mask << 1;

			/*
			 * k5_flag_lookup() returns "" when passed a zero
			 * indicating that there is no flag set at that bit.
			 * Continue on to the next flag.
			 */
			if (t != NULL && t[0] == '\0')
				continue;

			if (t != NULL)
				(void) asprintf(&tmp, "%s %s", str, t);
			else
				(void) asprintf(&tmp, "%s <%s(%d)>", str,
				    dgettext(TEXT_DOMAIN, "unknown"), i);

			/*
			 * Free the old string and make the memory pointed to by
			 * tmp the new string.
			 */
			if (tmp != NULL) {
				free(str);
				str = tmp;
			}
		}
	}

	return (str);
}

/*
 * Encryption types are stored in an array along with its size (unlike many
 * other arrays seen in krb5 which are generally NULL terminated).  Given an
 * array of encryption types ("enctypes") and a count ("n") return a string
 * representation.
 * The returned string should be freed. Returns NULL on memory allocation
 * failure or empty array
 */
static char *
k5_etypes_to_str(unsigned int n, const krb5_enctype *enctypes) {
	char *t, *str = NULL;
	unsigned int i;

	for (i = 0; i < n; i++) {
		t = k5_type_to_str(k5_etype_lookup, enctypes[i]);
		if (t != NULL) {
			if (str == NULL) {
				str = t;
			} else {
				char *tmp;
				(void) asprintf(&tmp, "%s %s", str, t);
				if (tmp != NULL) {
					free(str);
					str = tmp;
				}
				free(t);
				t = NULL;
			}
		}
	}

	return (str);
}

/*
 * Convert a krb5_data structure to string.
 * Returned string should be freed. Returns NULL on memory allocation failure or
 * NULL input.
 */
static char *
k5_data_to_str(const krb5_data *data) {
	char *str = NULL;

	if (data != NULL) {
		str = malloc(data->length + 1);
		if (str != NULL) {
			(void) memcpy(str, data->data, data->length);
			str[data->length] = '\0';
		}
	}

	return (str);
}

/*
 * Returns a string representation of a krb5_pa_data type. Currently only
 * returns type of krb5_pa_data. e.g. "ENC_TIMESTAMP(2)".
 * Takes a void pointer "p" as this function is can be passed to
 * k5_array_to_str().
 * Returned string should be freed. Returns NULL on memory allocation failure or
 * NULL input.
 */
static char *
k5_padata_to_str(const void *p) {
	const krb5_pa_data *pa = p;
	char *str = NULL;

	if (pa != NULL)
		str = k5_type_to_str(k5_patype_lookup, pa->pa_type);

	return (str);
}

/*
 * Returns a string representation of a krb5_authdata type. Currently only
 * returns type of krb5_authdata. e.g. "AD-IF-RELEVANT(1)".
 * Takes a void pointer "p" as this function is can be passed to
 * k5_array_to_str().
 * Returned string should be freed. Returns NULL on memory allocation failure or
 * NULL input.
 */
static char *
k5_authdata_to_str(const void *a) {
	const krb5_authdata *ad = a;
	char *str = NULL;

	if (ad != NULL)
		str = k5_type_to_str(k5_adtype_lookup, ad->ad_type);

	return (str);
}

/*
 * Returns a string representation of a krb5_last_req type. Returns type and
 * timestamp. e.g. "ALL_LAST_TGT(1):1283180754".
 * Takes a void pointer "p" as this function is can be passed to
 * k5_array_to_str().
 * Returned string should be freed. Returns NULL on memory allocation failure or
 * NULL input.
 */
static char *
k5_last_req_to_str(const void *l) {
	const krb5_last_req_entry *lr = l;
	char *str = NULL;

	if (lr != NULL) {
		char *tmp;
		tmp = k5_type_to_str(k5_lrtype_lookup, lr->lr_type);
		if (tmp != NULL) {
			(void) asprintf(&str, "%s:%u", tmp, lr->value);
			free(tmp);
		}
	}

	return (str);
}

/*
 * Returns a string representation of a krb5_transited type. For
 * KRB5_DOMAIN_X500_COMPRESS a string representation of the transited realms
 * will be returned. e.g. "DOMAIN-X500-COMPRESS(1):ACME.COM,MIT."
 * Takes a void pointer "p" as this function can be passed to
 * k5_array_to_str().
 * Returned string should be freed. Returns NULL on memory allocation failure or
 * NULL input.
 */
static char *
k5_transited_to_str(const void *t) {
	const krb5_transited *tr = t;
	char *str = NULL;

	if (tr != NULL) {
		if (tr->tr_type == KRB5_DOMAIN_X500_COMPRESS) {
			char *s1 = k5_type_to_str(
			    k5_trtype_lookup, (tr->tr_type));
			char *s2 = k5_data_to_str(&tr->tr_contents);

			(void) asprintf(&str, "%s:%s",
			    s1 != NULL ? s1 : "",
			    s2 != NULL ? s2 : "");

			free(s2);
			free(s1);
		} else {
			str = k5_type_to_str(k5_trtype_lookup, tr->tr_type);
		}
	}

	return (str);
}

/*
 * Returns a string representation of a krb5_address type. IPv6 and IPv4
 * addresses are supported. e.g.  "10.10.10.10"
 * Takes a void pointer "p" as this function can be passed to
 * k5_array_to_str().
 * Returned string should be freed. Returns NULL on memory allocation failure or
 * NULL input.
 */
static char *
k5_address_to_str(const void *a) {
	const krb5_address *addr = a;
	char *str = NULL;

	if (addr != NULL) {
		switch (addr->addrtype) {
			case ADDRTYPE_INET:
				str = malloc(INET_ADDRSTRLEN);
				if (str != NULL)
					(void) inet_ntop(AF_INET,
					    addr->contents, str,
					    INET_ADDRSTRLEN);
				break;

			case ADDRTYPE_INET6:
				str = malloc(INET6_ADDRSTRLEN);
				if (str != NULL)
					(void) inet_ntop(AF_INET6,
					    addr->contents, str,
					    INET6_ADDRSTRLEN);
				break;

			default:
				(void) asprintf(&str, "<%s(%d)>",
				    dgettext(TEXT_DOMAIN,
				    "unknown address type"),
				    addr->addrtype);
		}
	}

	return (str);
}

/*
 * Count the number of elements in a NULL terminated array.
 */
static int
k5_count_array(const void **ptr) {
	unsigned int i = 0;
	for (; ptr && ptr[i]; )
		i++;

	return (i);
}

/*
 * The following functions consist of "build" and "free" functions for each
 * argument passed from the Kerberos mech to DTrace.
 * These functions support the build-fire-free macros in kerberos_dtrace.h. The
 * k5_*info arguments are flat structures closely mimicking their *info DTrace
 * counter-parts. They are generally made up of strings and integers.
 */

k5_krbinfo_t *
k5_krbinfo_build(const krb5_data *data) {
	k5_krbinfo_t *ki = NULL;

	if (data != NULL) {
		ki = malloc(sizeof (k5_krbinfo_t));
		if (ki != NULL) {
			(void) memset(ki, 0, sizeof (k5_krbinfo_t));
			ki->version = 5;
			if (data->data != NULL) {
				ki->message_type =
				    k5_type_to_str(k5_msgtype_lookup,
				    data->data[0] & 0x1f);
			}
			ki->message_id = data->data;
			ki->message_length = data->length;
			ki->message = data->data;
		}
	}

	return (ki);
}

void
k5_krbinfo_free(k5_krbinfo_t *ki) {
	if (ki != NULL) {
		free(ki->message_type);
		free(ki);
	}
}

k5_kerrorinfo_t *
k5_kerrorinfo_build(const krb5_error *error) {
	k5_kerrorinfo_t *ke = NULL;

	if (error != NULL) {
		ke = malloc(sizeof (k5_kerrorinfo_t));
		if (ke != NULL) {
			(void) memset(ke, 0, sizeof (k5_kerrorinfo_t));
			ke->ctime = error->ctime;
			ke->cusec = error->cusec;
			ke->stime = error->stime;
			ke->susec = error->susec;
			ke->error_code = k5_type_to_str(
			    k5_errtype_lookup, error->error);
			(void) krb5_unparse_name_no_ctx(error->client,
			    &ke->client);
			(void) krb5_unparse_name_no_ctx(error->server,
			    &ke->server);
			ke->e_text = k5_data_to_str(&error->text);
			ke->e_data = NULL;

			/*
			 * When preauth is required we can treat e_data as a
			 * list of supported pre-authentication types.
			 */
			if (error->error == KDC_ERR_PREAUTH_REQUIRED &&
			    error->e_data.length > 0) {
				krb5_pa_data **pa = NULL;
				if (decode_krb5_padata_sequence(
				    &error->e_data, &pa) == 0) {
					ke->e_data = k5_array_to_str(
					    k5_padata_to_str,
					    (const void **)pa);
					krb5_free_pa_data_no_ctx(pa);
				}
			}
		}
	}

	return (ke);
}

void
k5_kerrorinfo_free(k5_kerrorinfo_t *ke) {
	if (ke != NULL) {
		free(ke->error_code);
		free(ke->e_data);
		free(ke->e_text);
		free(ke->server);
		free(ke->client);
		free(ke);
	}
}

k5_kdcreqinfo_t *
k5_kdcreqinfo_build(const krb5_kdc_req *req) {
	k5_kdcreqinfo_t *kr = NULL;
	if (req != NULL) {
		kr = malloc(sizeof (k5_kdcreqinfo_t));
		if (kr != NULL) {
			(void) memset(kr, 0, sizeof (k5_kdcreqinfo_t));
			kr->padata_types = k5_array_to_str(k5_padata_to_str,
			    (const void **)req->padata);
			kr->kdc_options = k5_flags_to_str(req->kdc_options);

			(void) krb5_unparse_name_no_ctx(req->client,
			    &kr->client);
			(void) krb5_unparse_name_no_ctx(req->server,
			    &kr->server);
			kr->from = req->from;
			kr->till = req->till;
			kr->rtime = req->rtime;
			kr->nonce = req->nonce;
			kr->etype = k5_etypes_to_str(req->nktypes, req->ktype);
			kr->addresses = k5_array_to_str(k5_address_to_str,
			    (const void **)req->addresses);
			kr->authorization_data =
			    k5_array_to_str(k5_authdata_to_str,
			    (const void **)req->unenc_authdata);
			kr->num_additional_tickets = k5_count_array(
			    (const void **)req->second_ticket);
		}
	}

	return (kr);
}

void
k5_kdcreqinfo_free(k5_kdcreqinfo_t *kr) {
	if (kr != NULL) {
		free(kr->authorization_data);
		free(kr->addresses);
		free(kr->etype);
		free(kr->server);
		free(kr->client);
		free(kr->kdc_options);
		free(kr->padata_types);
		free(kr);
	}
}

k5_kdcrepinfo_t *
k5_kdcrepinfo_build(const krb5_kdc_rep *rep,
    const krb5_enc_kdc_rep_part *encp) {

	k5_kdcrepinfo_t *kr = NULL;
	if (rep != NULL) {
		kr = malloc(sizeof (k5_kdcrepinfo_t));
		if (kr != NULL) {
			(void) memset(kr, 0, sizeof (k5_kdcrepinfo_t));
			kr->padata_types = k5_array_to_str(k5_padata_to_str,
			    (const void **)rep->padata);
			(void) krb5_unparse_name_no_ctx(rep->client,
			    &kr->client);
			kr->enc_part_kvno = rep->enc_part.kvno;
			kr->enc_part_etype = k5_type_to_str(k5_etype_lookup,
			    rep->enc_part.enctype);
		}
		if (encp != NULL) {
			if (encp->session != NULL) {
				kr->enc_key_type =
				    k5_type_to_str(k5_etype_lookup,
				    encp->session->enctype);
				kr->enc_key_length = encp->session->length;
				kr->enc_key_value = encp->session->contents;
			}
			kr->enc_last_req = k5_array_to_str(k5_last_req_to_str,
			    (const void **)encp->last_req);
			kr->enc_nonce = encp->nonce;
			kr->enc_key_expiration = encp->key_exp;
			kr->enc_flags = k5_flags_to_str(encp->flags);
			kr->enc_authtime = encp->times.authtime;
			kr->enc_starttime = encp->times.starttime;
			kr->enc_starttime = encp->times.endtime;
			kr->enc_renew_till = encp->times.renew_till;
			(void) krb5_unparse_name_no_ctx(encp->server,
			    &kr->enc_server);
			kr->enc_caddr = k5_array_to_str(k5_address_to_str,
			    (const void **)encp->caddrs);
		}
	}

	return (kr);
}

void
k5_kdcrepinfo_free(k5_kdcrepinfo_t *kr) {
	if (kr != NULL) {
		free(kr->enc_caddr);
		free(kr->enc_server);
		free(kr->enc_flags);
		free(kr->enc_last_req);
		free(kr->enc_key_type);
		free(kr->enc_part_etype);
		free(kr->client);
		free(kr->padata_types);
		free(kr);
	}
}

k5_kticketinfo_t *
k5_kticketinfo_build(const krb5_ticket *tkt) {
	k5_kticketinfo_t *kt = NULL;
	if (tkt != NULL) {
		kt = malloc(sizeof (k5_kticketinfo_t));
		if (kt != NULL) {
			(void) memset(kt, 0, sizeof (k5_kticketinfo_t));
			(void) krb5_unparse_name_no_ctx(tkt->server,
			    &kt->server);
			kt->enc_part_kvno = tkt->enc_part.kvno;
			kt->enc_part_etype = k5_type_to_str(k5_etype_lookup,
			    tkt->enc_part.enctype);
			if (tkt->enc_part2 != NULL) {
				krb5_enc_tkt_part *encp = tkt->enc_part2;

				kt->enc_flags = k5_flags_to_str(encp->flags);
				if (encp->session != NULL) {
					kt->enc_key_type = k5_type_to_str(
					    k5_etype_lookup,
					    encp->session->enctype);
					kt->enc_key_length =
					    encp->session->length;
					kt->enc_key_value =
					    encp->session->contents;
				}

				(void) krb5_unparse_name_no_ctx(encp->client,
				    &kt->enc_client);
				kt->enc_transited = k5_transited_to_str(
				    &encp->transited);
				kt->enc_transited_type = k5_type_to_str(
				    k5_trtype_lookup, encp->transited.tr_type);
				kt->enc_authtime = encp->times.authtime;
				kt->enc_starttime = encp->times.starttime;
				kt->enc_endtime = encp->times.endtime;
				kt->enc_renew_till = encp->times.renew_till;
				kt->enc_addresses = k5_array_to_str
				    (k5_address_to_str,
				    (const void **)encp->caddrs);
				kt->enc_authorization_data = k5_array_to_str
				    (k5_authdata_to_str,
				    (const void **)encp->authorization_data);
			}
		}
	}

	return (kt);
}

void
k5_kticketinfo_free(k5_kticketinfo_t *kt) {
	if (kt != NULL) {
		free(kt->enc_authorization_data);
		free(kt->enc_addresses);
		free(kt->enc_transited_type);
		free(kt->enc_transited);
		free(kt->enc_client);
		free(kt->enc_key_type);
		free(kt->enc_flags);
		free(kt->enc_part_etype);
		free(kt->server);
		free(kt);
	}
}


k5_kaprepinfo_t *
k5_kaprepinfo_build(const krb5_ap_rep *rep, const krb5_ap_rep_enc_part *encp) {
	k5_kaprepinfo_t *ka = NULL;

	if (rep != NULL) {
		ka = malloc(sizeof (k5_kaprepinfo_t));
		if (ka != NULL) {
			(void) memset(ka, 0, sizeof (k5_kaprepinfo_t));
			ka->enc_part_kvno = rep->enc_part.kvno;
			ka->enc_part_etype = k5_type_to_str(k5_etype_lookup,
			    rep->enc_part.enctype);
			if (encp != NULL) {
				ka->enc_ctime = encp->ctime;
				ka->enc_cusec = encp->cusec;

				if (encp->subkey != NULL) {
					ka->enc_subkey_type = k5_type_to_str(
					    k5_etype_lookup,
					    encp->subkey->enctype);
					ka->enc_subkey_length =
					    encp->subkey->length;
					ka->enc_subkey_value =
					    encp->subkey->contents;
				}
				ka->enc_seq_number = encp->seq_number;
			}
		}
	}

	return (ka);
}

void
k5_kaprepinfo_free(k5_kaprepinfo_t *ka) {
	if (ka != NULL) {
		free(ka->enc_subkey_type);
		free(ka->enc_part_etype);
		free(ka);
	}
}

k5_kapreqinfo_t *
k5_kapreqinfo_build(const krb5_ap_req *req) {
	k5_kapreqinfo_t *ka = NULL;

	if (req != NULL) {
		ka = malloc(sizeof (k5_kapreqinfo_t));
		if (ka != NULL) {
			(void) memset(ka, 0, sizeof (k5_kapreqinfo_t));

			ka->ap_options = k5_flags_to_str(req->ap_options);
			ka->authenticator_kvno = req->authenticator.kvno;
			ka->authenticator_etype =
			    k5_type_to_str(k5_etype_lookup,
			    req->authenticator.enctype);
		}
	}

	return (ka);
}

void
k5_kapreqinfo_free(k5_kapreqinfo_t *ka) {
	if (ka != NULL) {
		free(ka->authenticator_etype);
		free(ka->ap_options);
		free(ka);
	}
}

k5_kauthenticatorinfo_t *
k5_kauthenticatorinfo_build(const krb5_authenticator *auth) {
	k5_kauthenticatorinfo_t *ka = NULL;

	if (auth != NULL) {
		ka = malloc(sizeof (k5_kauthenticatorinfo_t));
		if (ka != NULL) {
			(void) memset(ka, 0, sizeof (k5_kauthenticatorinfo_t));
			(void) krb5_unparse_name_no_ctx(auth->client,
			    &ka->client);
			if (auth->checksum != NULL) {
				ka->cksum_type =
				    k5_type_to_str(k5_cktype_lookup,
				    auth->checksum->checksum_type);
				ka->cksum_length = auth->checksum->length;
				ka->cksum_value = auth->checksum->contents;
			}
			ka->cusec = auth->cusec;
			ka->ctime = auth->ctime;

			if (auth->subkey != NULL) {
				ka->subkey_type = k5_type_to_str(
				    k5_etype_lookup, auth->subkey->enctype);
				ka->subkey_length = auth->subkey->length;
				ka->subkey_value = auth->subkey->contents;
			}
			ka->seq_number = auth->seq_number;
			ka->authorization_data = k5_array_to_str(
			    k5_authdata_to_str,
			    (const void **)auth->authorization_data);
		}
	}

	return (ka);
}

void
k5_kauthenticatorinfo_free(k5_kauthenticatorinfo_t *ka) {
	if (ka != NULL) {
		free(ka->authorization_data);
		free(ka->subkey_type);
		free(ka->cksum_type);
		free(ka->client);
		free(ka);
	}
}

k5_ksafeinfo_t *
k5_ksafeinfo_build(const krb5_safe *safe) {
	k5_ksafeinfo_t *ks = NULL;

	if (safe != NULL) {
		ks = malloc(sizeof (k5_ksafeinfo_t));
		if (ks != NULL) {
			(void) memset(ks, 0, sizeof (k5_ksafeinfo_t));
			ks->user_data = safe->user_data.data;
			ks->user_data_length = safe->user_data.length;
			ks->timestamp = safe->timestamp;
			ks->usec = safe->usec;
			ks->seq_number = safe->seq_number;
			ks->s_address = k5_address_to_str(safe->s_address);
			ks->r_address = k5_address_to_str(safe->r_address);
			if (safe->checksum != NULL) {
				ks->cksum_type =
				    k5_type_to_str(k5_cktype_lookup,
				    safe->checksum->checksum_type);
				ks->cksum_length = safe->checksum->length;
				ks->cksum_value = safe->checksum->contents;
			}
		}
	}

	return (ks);
}

void
k5_ksafeinfo_free(k5_ksafeinfo_t *ks) {
	if (ks != NULL) {
		free(ks->cksum_type);
		free(ks->r_address);
		free(ks->s_address);
		free(ks);
	}
}

k5_kprivinfo_t *
k5_kprivinfo_build(const krb5_priv *priv, const krb5_priv_enc_part *encp) {
	k5_kprivinfo_t *kp = NULL;

	if (priv != NULL) {
		kp = malloc(sizeof (k5_kprivinfo_t));
		if (kp != NULL) {
			(void) memset(kp, 0, sizeof (k5_kprivinfo_t));
			kp->enc_part_kvno = priv->enc_part.kvno;
			kp->enc_part_etype = k5_type_to_str(k5_etype_lookup,
			    priv->enc_part.enctype);
			if (encp != NULL) {
				kp->enc_user_data = encp->user_data.data;
				kp->enc_user_data_length =
				    encp->user_data.length;
				kp->enc_timestamp = encp->timestamp;
				kp->enc_usec = encp->usec;
				kp->enc_seq_number = encp->seq_number;
				kp->enc_s_address =
				    k5_address_to_str(encp->s_address);
				kp->enc_r_address =
				    k5_address_to_str(encp->r_address);
			}
		}
	}

	return (kp);
}

void
k5_kprivinfo_free(k5_kprivinfo_t *kp) {
	if (kp != NULL) {
		free(kp->enc_r_address);
		free(kp->enc_s_address);
		free(kp->enc_part_etype);
		free(kp);
	}
}

k5_kcredinfo_t *
k5_kcredinfo_build(const krb5_cred *cred, const krb5_cred_enc_part *encp) {
	k5_kcredinfo_t *kc = NULL;

	if (cred != NULL) {
		kc = malloc(sizeof (k5_kcredinfo_t));
		if (kc != NULL) {
			(void) memset(kc, 0, sizeof (k5_kcredinfo_t));
			kc->enc_part_kvno = cred->enc_part.kvno;
			kc->enc_part_etype = k5_type_to_str(k5_etype_lookup,
			    cred->enc_part.enctype);
			kc->tickets =
			    k5_count_array((const void **)(cred->tickets));
			if (encp != NULL) {
				kc->enc_nonce = encp->nonce;
				kc->enc_timestamp = encp->timestamp;
				kc->enc_usec = encp->usec;
				kc->enc_s_address =
				    k5_address_to_str(encp->s_address);
				kc->enc_r_address =
				    k5_address_to_str(encp->r_address);
			}
		}
	}

	return (kc);
}

void
k5_kcredinfo_free(k5_kcredinfo_t *kc) {
	if (kc != NULL) {
		free(kc->enc_r_address);
		free(kc->enc_s_address);
		free(kc->enc_part_etype);
		free(kc);
	}
}

k5_kconninfo_t *
k5_kconninfo_build(const int fd) {
	k5_kconninfo_t *kc = malloc(sizeof (k5_kconninfo_t));

	if (kc != NULL) {
		struct sockaddr_storage s;
		socklen_t len = sizeof (struct sockaddr_storage);
		int t;

		(void) memset(kc, 0, sizeof (k5_kconninfo_t));

		if (getsockname(fd, (struct sockaddr *)&s, &len) == 0) {
			if (s.ss_family == AF_INET) {
				kc->protocol = strdup("ipv4");
				kc->local = malloc(INET_ADDRSTRLEN);
				if (kc->local != NULL)
					inet_ntop(s.ss_family,
					    &(ss2sin(&s)->sin_addr), kc->local,
					    INET_ADDRSTRLEN);

				kc->localport = htons(ss2sin(&s)->sin_port);

			} else if (s.ss_family == AF_INET6) {
				kc->protocol = strdup("ipv6");
				kc->local = malloc(INET6_ADDRSTRLEN);
				if (kc->local != NULL)
					inet_ntop(s.ss_family,
					    &(ss2sin6(&s)->sin6_addr),
					    kc->local, INET6_ADDRSTRLEN);
				kc->localport = htons(ss2sin6(&s)->sin6_port);

			} else
				(void) asprintf(&kc->protocol, "<%s(%d)>",
				    dgettext(TEXT_DOMAIN, "unknown"),
				    s.ss_family);
		}

		if (getpeername(fd, (struct sockaddr *)&s, &len) == 0) {
			if (s.ss_family == AF_INET) {
				kc->remote = malloc(INET_ADDRSTRLEN);
				if (kc->remote != NULL)
					inet_ntop(s.ss_family,
					    &(ss2sin(&s)->sin_addr), kc->remote,
					    INET_ADDRSTRLEN);
				kc->remoteport = htons(ss2sin(&s)->sin_port);
			} else if (s.ss_family == AF_INET6) {
				kc->remote = malloc(INET6_ADDRSTRLEN);
				if (kc->remote != NULL)
					inet_ntop(s.ss_family,
					    &(ss2sin6(&s)->sin6_addr),
					    kc->remote, INET6_ADDRSTRLEN);
				kc->remoteport = htons(ss2sin6(&s)->sin6_port);
			}
		}

		len = sizeof (t);
		if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &t, &len) == 0) {
			switch (t) {
				case SOCK_STREAM:
					kc->type = strdup("tcp");
					break;
				case SOCK_DGRAM:
					kc->type = strdup("udp");
					break;
				default:
					(void) asprintf(&kc->type, "<%s(%d)>",
					    dgettext(TEXT_DOMAIN, "unknown"),
					    t);
			}
		}
	}

	return (kc);
}

void
k5_kconninfo_free(k5_kconninfo_t *kc) {
	if (kc != NULL) {
		free(kc->type);
		free(kc->remote);
		free(kc->local);
		free(kc->protocol);
		free(kc);
	}
}

/*
 * Some probes should fire in multiple places. In order to ensure that each
 * probe is only listed once by DTrace these probes are put into their own
 * functions.
 */
void k5_trace_kdc_rep_read(const krb5_data *msg, const krb5_kdc_rep *dec_rep) {
	KERBEROS_PROBE_KRB_KDC_REP(READ, msg, dec_rep,
	    dec_rep == NULL ? NULL : dec_rep->enc_part2,
	    dec_rep == NULL ? NULL : dec_rep->ticket);
}

void k5_trace_kdc_req_read(const krb5_data *msg, const krb5_kdc_req *req) {
	KERBEROS_PROBE_KRB_KDC_REQ(READ, msg, req);
}

void k5_trace_message_send(const int fd, char *data,
    const unsigned int length) {
	KERBEROS_PROBE_KRB_MESSAGE(SEND, fd, data, length);
}

void k5_trace_message_recv(const int fd, char *data,
    const unsigned int length) {
	KERBEROS_PROBE_KRB_MESSAGE(RECV, fd, data, length);
}
