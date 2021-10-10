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
 * Copyright (c) 2001, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysmacros.h>
#include <netinet/in.h>
#include <net/pfkeyv2.h>
#include <time.h>
#include <stdio.h>

#include <ipsec_util.h>
#include "defs.h"
#include "readps.h"
#include <ike/isakmp_internal.h>

typedef struct valtab {
	int		ssh_val;
	uint32_t	ike_val;
} valtab_t;

static const valtab_t	states[] = {
	{ SSH_IKE_ST_START_SA_NEGOTIATION_I,	IKE_SA_STATE_INIT },
	{ SSH_IKE_ST_START_SA_NEGOTIATION_R,	IKE_SA_STATE_INIT },
	{ SSH_IKE_ST_MM_SA_I,			IKE_SA_STATE_SENT_SA },
	{ SSH_IKE_ST_MM_SA_R,			IKE_SA_STATE_SENT_SA },
	{ SSH_IKE_ST_AM_SA_I,			IKE_SA_STATE_SENT_SA },
	{ SSH_IKE_ST_AM_SA_R,			IKE_SA_STATE_SENT_SA },
	{ SSH_IKE_ST_MM_KE_I,			IKE_SA_STATE_SENT_KE },
	{ SSH_IKE_ST_MM_KE_R,			IKE_SA_STATE_SENT_KE },
	{ SSH_IKE_ST_MM_FINAL_I,		IKE_SA_STATE_SENT_LAST },
	{ SSH_IKE_ST_AM_FINAL_I,		IKE_SA_STATE_SENT_LAST },
	{ SSH_IKE_ST_MM_FINAL_R,		IKE_SA_STATE_DONE },
	{ SSH_IKE_ST_MM_DONE_I,			IKE_SA_STATE_DONE },
	{ SSH_IKE_ST_AM_DONE_R,			IKE_SA_STATE_DONE },
	{ SSH_IKE_ST_DONE,			IKE_SA_STATE_DONE },
	{ SSH_IKE_ST_DELETED,			IKE_SA_STATE_DELETED },
	{ SSH_IKE_ST_START_QM_I,		IKE_SA_STATE_INVALID },
	{ SSH_IKE_ST_START_QM_R,		IKE_SA_STATE_INVALID },
	{ SSH_IKE_ST_QM_HASH_SA_I,		IKE_SA_STATE_INVALID },
	{ SSH_IKE_ST_QM_HASH_SA_R,		IKE_SA_STATE_INVALID },
	{ SSH_IKE_ST_QM_HASH_I,			IKE_SA_STATE_INVALID },
	{ SSH_IKE_ST_QM_DONE_R,			IKE_SA_STATE_INVALID },
	{ SSH_IKE_ST_START_NGM_I,		IKE_SA_STATE_INVALID },
	{ SSH_IKE_ST_START_NGM_R,		IKE_SA_STATE_INVALID },
	{ SSH_IKE_ST_NGM_HASH_SA_I,		IKE_SA_STATE_INVALID },
	{ SSH_IKE_ST_NGM_HASH_SA_R,		IKE_SA_STATE_INVALID },
	{ SSH_IKE_ST_NGM_DONE_I,		IKE_SA_STATE_INVALID },
	{ SSH_IKE_ST_ANY,			IKE_SA_STATE_INVALID },
};

static const valtab_t	idtypes[] = {
	{ IPSEC_ID_IPV4_ADDR,		SADB_IDENTTYPE_RESERVED },
	{ IPSEC_ID_FQDN,		SADB_IDENTTYPE_FQDN },
	{ IPSEC_ID_USER_FQDN,		SADB_IDENTTYPE_USER_FQDN },
	{ IPSEC_ID_IPV4_ADDR_SUBNET,	SADB_IDENTTYPE_PREFIX },
	{ IPSEC_ID_IPV6_ADDR,		SADB_IDENTTYPE_RESERVED },
	{ IPSEC_ID_IPV6_ADDR_SUBNET,	SADB_IDENTTYPE_PREFIX },
	{ IPSEC_ID_IPV4_ADDR_RANGE,	SADB_X_IDENTTYPE_ADDR_RANGE },
	{ IPSEC_ID_IPV6_ADDR_RANGE,	SADB_X_IDENTTYPE_ADDR_RANGE },
	{ IPSEC_ID_DER_ASN1_DN,		SADB_X_IDENTTYPE_DN },
	{ IPSEC_ID_DER_ASN1_GN,		SADB_X_IDENTTYPE_GN },
	{ IPSEC_ID_KEY_ID,		SADB_X_IDENTTYPE_KEY_ID },
};

static const keywdtab_t	idstrtab[] = {
	{ IPSEC_ID_IPV4_ADDR,		"ip" },
	{ IPSEC_ID_FQDN,		"fqdn" },
	{ IPSEC_ID_USER_FQDN,		"user_fqdn" },
	{ IPSEC_ID_IPV4_ADDR_SUBNET,	"ipv4_prefix" },
	{ IPSEC_ID_IPV6_ADDR,		"ipv6" },
	{ IPSEC_ID_IPV6_ADDR_SUBNET,	"ipv6_prefix" },
	{ IPSEC_ID_IPV4_ADDR_RANGE,	"ipv4_range" },
	{ IPSEC_ID_IPV6_ADDR_RANGE,	"ipv6_range" },
	{ IPSEC_ID_DER_ASN1_DN,		"dn" },
	{ IPSEC_ID_DER_ASN1_GN,		"gn" },
	{ IPSEC_ID_KEY_ID,		"keyid" },
};

static const valtab_t	encrtab[] = {
	{ SSH_IKE_VALUES_ENCR_ALG_BLOWFISH_CBC,	SADB_EALG_BLOWFISH },
	{ SSH_IKE_VALUES_ENCR_ALG_AES_CBC,	SADB_EALG_AES },
	{ SSH_IKE_VALUES_ENCR_ALG_3DES_CBC,	SADB_EALG_3DESCBC },
	{ SSH_IKE_VALUES_ENCR_ALG_DES_CBC,	SADB_EALG_DESCBC },
};

static const keywdtab_t	encrstrtab[] = {
	{ SADB_EALG_BLOWFISH,	"blowfish-cbc" },
	{ SADB_EALG_3DESCBC,	"3des-cbc" },
	{ SADB_EALG_DESCBC,	"des-cbc" },
	{ SADB_EALG_AES,	"aes-cbc" },
};

static const valtab_t	authtab[] = {
	{ SSH_IKE_VALUES_HASH_ALG_MD5,		SADB_AALG_MD5HMAC },
	{ SSH_IKE_VALUES_HASH_ALG_SHA,		SADB_AALG_SHA1HMAC },
	{ SSH_IKE_VALUES_HASH_ALG_SHA2_256,	SADB_AALG_SHA256HMAC },
	{ SSH_IKE_VALUES_HASH_ALG_SHA2_384,	SADB_AALG_SHA384HMAC },
	{ SSH_IKE_VALUES_HASH_ALG_SHA2_512,	SADB_AALG_SHA512HMAC },
};

static const keywdtab_t	authstrtab[] = {
	{ SADB_AALG_MD5HMAC,	"md5" },
	{ SADB_AALG_SHA1HMAC,	"sha1" },
	{ SADB_AALG_SHA256HMAC,	"sha256" },
	{ SADB_AALG_SHA384HMAC,	"sha384" },
	{ SADB_AALG_SHA512HMAC,	"sha512" },
};

static const keywdtab_t dhstrtab[] = {
	{ SSH_IKE_VALUES_GRP_DESC_DEFAULT_MODP_1536, "1536-bit MODP (group 5)"},
	{ SSH_IKE_VALUES_GRP_DESC_DEFAULT_MODP_1024, "1024-bit MODP (group 2)"},
	{ SSH_IKE_VALUES_GRP_DESC_DEFAULT_MODP_768, "768-bit MODP (group 1)"},
	{ IKE_GRP_DESC_MODP_2048, "2048-bit MODP (group 14)"},
	{ IKE_GRP_DESC_MODP_3072, "3072-bit MODP (group 15)"},
	{ IKE_GRP_DESC_MODP_4096, "4096-bit MODP (group 16)"},
	{ IKE_GRP_DESC_MODP_6144, "6144-bit MODP (group 17)"},
	{ IKE_GRP_DESC_MODP_8192, "8192-bit MODP (group 18)"},
	{ IKE_GRP_DESC_ECP_256, "256-bit ECP (group 19)"},
	{ IKE_GRP_DESC_ECP_384, "384-bit ECP (group 20)"},
	{ IKE_GRP_DESC_ECP_521, "521-bit ECP (group 21)"},
	{ IKE_GRP_DESC_MODP_1024_160,
	    "1024-bit MODP, 160-bit subprime (group 22)"},
	{ IKE_GRP_DESC_MODP_2048_224,
	    "2048-bit MODP, 224-bit subprime (group 23)"},
	{ IKE_GRP_DESC_MODP_2048_256,
	    "2048-bit MODP, 256-bit subprime (group 24)"},
	{ IKE_GRP_DESC_ECP_192, "192-bit ECP (group 25)"},
	{ IKE_GRP_DESC_ECP_224, "224-bit ECP (group 26)"},
};

static const keywdtab_t	prfstrtab[] = {
	{ IKE_PRF_HMAC_MD5,		"hmac-md5" },
	{ IKE_PRF_HMAC_SHA1,		"hmac-sha1" },
	{ IKE_PRF_HMAC_SHA256,		"hmac-sha256" },
	{ IKE_PRF_HMAC_SHA384,		"hmac-sha384" },
	{ IKE_PRF_HMAC_SHA512,		"hmac-sha512" },
};


boolean_t
ike_group_supported(int group)
{
	const keywdtab_t *tp;

	for (tp = dhstrtab; tp < A_END(dhstrtab); tp++)
		if (group == tp->kw_tag)
			return (B_TRUE);

	PRTDBG((D_P1 | D_P2), ("Oakley Group %d - Not supported.", group));
	return (B_FALSE);
}

boolean_t
ike_hash_supported(int hash)
{
	const valtab_t *tp;

	/* Use "ssh values" because they correspond to IKEv1's DOI. */
	for (tp = authtab; tp < A_END(authtab); tp++)
		if (hash == tp->ssh_val)
			return (B_TRUE);

	return (B_FALSE);
}

int
ike_get_hash(int index, const keywdtab_t **ret)
{
	uint_t cnt = sizeof (authstrtab) / sizeof (authstrtab)[0];

	if (index >= cnt) {
		return (-1);
	} else {
		*ret = &authstrtab[index];
		if (index == cnt - 1)
			return (0);
		else
			return (1);
	}
}

boolean_t
ike_cipher_supported(int cipher)
{
	const valtab_t *tp;

	/* Use "ssh values" because they correspond to IKEv1's DOI. */
	for (tp = encrtab; tp < A_END(encrtab); tp++)
		if (cipher == tp->ssh_val)
			return (B_TRUE);

	return (B_FALSE);
}

int
ike_get_cipher(int index, const keywdtab_t **ret)
{
	uint_t cnt = sizeof (encrstrtab) / sizeof (encrstrtab)[0];

	if (index >= cnt) {
		return (-1);
	} else {
		*ret = &encrstrtab[index];
		if (index == cnt - 1)
			return (0);
		else
			return (1);
	}
}

uint16_t
sshencr_to_sadb(int sshval)
{
	const valtab_t	*tp;

	for (tp = encrtab; tp < A_END(encrtab); tp++) {
		if (sshval == tp->ssh_val)
			return (tp->ike_val);
	}
	return (SADB_EALG_NONE);
}

uint16_t
sadb_to_sshencr(int sshval)
{
	const valtab_t	*tp;

	for (tp = encrtab; tp < A_END(encrtab); tp++) {
		if (sshval == tp->ike_val)
			return (tp->ssh_val);
	}
	return (0);
}

static uint16_t
sshencrstr_to_sadb(const uchar_t *str)
{
	const keywdtab_t	*tp;

	for (tp = encrstrtab; tp < A_END(encrstrtab); tp++) {
		if (strcmp((char *)str, tp->kw_str) == 0)
			return (tp->kw_tag);
	}
	return (SADB_EALG_NONE);
}

char *
sshencr_to_string(int sshval)
{
	uint16_t	sadbval;
	const keywdtab_t	*tp;

	sadbval = sshencr_to_sadb(sshval);

	for (tp = encrstrtab; tp < A_END(encrstrtab); tp++) {
		if (sadbval == tp->kw_tag)
			return (tp->kw_str);
	}
	return ("");
}

uint16_t
get_ssh_encralg(SshIkeNegotiation nego)
{
	if (nego->sa == NULL)
		return (SADB_EALG_NONE);

	return (sshencrstr_to_sadb(nego->sa->encryption_algorithm_name));
}

uint16_t
sshauth_to_sadb(int sshval)
{
	const valtab_t	*tp;

	for (tp = authtab; tp < A_END(authtab); tp++) {
		if (sshval == tp->ssh_val)
			return (tp->ike_val);
	}
	return (SADB_AALG_NONE);
}

uint16_t
sadb_to_sshauth(int sshval)
{
	const valtab_t	*tp;

	for (tp = authtab; tp < A_END(authtab); tp++) {
		if (sshval == tp->ike_val)
			return (tp->ssh_val);
	}
	return (0);
}

static uint16_t
sshauthstr_to_sadb(const uchar_t *str)
{
	const keywdtab_t	*tp;

	for (tp = authstrtab; tp < A_END(authstrtab); tp++) {
		if (strcmp((char *)str, tp->kw_str) == 0)
			return (tp->kw_tag);
	}
	return (SADB_AALG_NONE);
}

char *
sshauth_to_string(int sshval)
{
	uint16_t	sadbval;
	const keywdtab_t	*tp;

	sadbval = sshauth_to_sadb(sshval);

	for (tp = authstrtab; tp < A_END(authstrtab); tp++) {
		if (sadbval == tp->kw_tag)
			return (tp->kw_str);
	}
	return ("");
}

uint16_t
get_ssh_authalg(SshIkeNegotiation nego)
{
	if (nego->sa == NULL)
		return (SADB_AALG_NONE);

	return (sshauthstr_to_sadb((uchar_t *)nego->sa->hash_algorithm_name));
}

static uint16_t
sshprfstr_to_const(const uchar_t *name)
{
	const keywdtab_t	*tp;

	for (tp = prfstrtab; tp < A_END(prfstrtab); tp++) {
		if (strcmp((char *)name, tp->kw_str) == 0)
			return (tp->kw_tag);
	}
	return (IKE_PRF_NONE);
}

uint16_t
get_ssh_prf(SshIkeNegotiation nego)
{
	if (nego->sa == NULL)
		return (IKE_PRF_NONE);

	return (sshprfstr_to_const(nego->sa->prf_algorithm_name));
}

uint16_t
get_ssh_cipherkeylen(SshIkeNegotiation nego)
{
	if (nego->sa == NULL)
		return (0);

	return (nego->sa->cipher_key_len * 8);
}

uint16_t
get_ssh_dhgroup(SshIkeNegotiation nego)
{
	if (nego->ike_ed == NULL || nego->ike_ed->group == NULL)
		return (0);

	return (nego->ike_ed->group->descriptor);
}

uint32_t
get_ssh_p1state(SshIkeNegotiation nego)
{
	const valtab_t	*tp;

	if (nego->notification_state ==
	    SSH_IKE_NOTIFICATION_STATE_ALREADY_SENT) {
		/*
		 * ed (exchange data) contains the structure element
		 * which has the SA state, but libike free's ed
		 * after the isakmp exchange is complete, so the
		 * only state available is ACTIVE. If the negotiation
		 * still points to the SA then check lock_flags
		 * to see if libike has marked this phase 1
		 * as expiring.
		 */
		if (nego->sa != NULL) {
			if (nego->sa->lock_flags) {
				return (IKE_SA_STATE_INVALID);
			}
		}
		return (IKE_SA_STATE_DONE);
	}

	for (tp = states; tp < A_END(states); tp++) {
		if (tp->ssh_val == nego->ed->current_state)
			return (tp->ike_val);
	}
	return (IKE_SA_STATE_INVALID);
}

uint32_t
sshidtype_to_sadb(int sshval)
{
	const valtab_t	*tp;

	for (tp = idtypes; tp < A_END(idtypes); tp++) {
		if (tp->ssh_val == sshval)
			return (tp->ike_val);
	}
	return (SADB_IDENTTYPE_RESERVED);
}

char *
sshidtype_to_string(int sshval)
{
	const keywdtab_t	*tp;

	for (tp = idstrtab; tp < A_END(idstrtab); tp++) {
		if (tp->kw_tag == sshval)
			return (tp->kw_str);
	}
	return ("");
}

uint32_t
sadb_to_sshidtype(int sadbval)
{
	const valtab_t	*tp;

	for (tp = idtypes; tp < A_END(idtypes); tp++) {
		if (tp->ike_val == sadbval)
			return (tp->ssh_val);
	}
	return (0);
}

uint32_t
get_ssh_max_kbytes(SshIkeNegotiation nego)
{
	if (nego->sa == NULL)
		return (0);

	return (nego->sa->kbyte_limit);
}

uint32_t
get_ssh_kbytes(SshIkeNegotiation nego)
{
	int	round;

	if (nego->sa == NULL)
		return (0);

	round = (nego->sa->byte_count % 1024) ? 1 : 0;
	return ((nego->sa->byte_count / 1024) + round);
}

uint16_t
get_ssh_skeyid_len(SshIkeNegotiation nego)
{
	if (nego->sa == NULL || !nego->sa->skeyid.initialized)
		return (0);

	return (nego->sa->skeyid.skeyid_size);
}

void
get_ssh_skeyid(SshIkeNegotiation nego, int len, uint8_t *p)
{
	int	cplen;

	if (nego->sa == NULL || !nego->sa->skeyid.initialized ||
	    nego->sa->skeyid.skeyid_size == 0)
		return;

	cplen = (len <= nego->sa->skeyid.skeyid_size) ?
	    len : nego->sa->skeyid.skeyid_size;

	(void) memcpy(p, nego->sa->skeyid.skeyid, cplen);
}

uint16_t
get_ssh_skeyid_d_len(SshIkeNegotiation nego)
{
	if (nego->sa == NULL || !nego->sa->skeyid.initialized)
		return (0);

	return (nego->sa->skeyid.skeyid_d_size);
}

void
get_ssh_skeyid_d(SshIkeNegotiation nego, int len, uint8_t *p)
{
	int	cplen;

	if (nego->sa == NULL || !nego->sa->skeyid.initialized ||
	    nego->sa->skeyid.skeyid_d_size == 0)
		return;

	cplen = (len <= nego->sa->skeyid.skeyid_d_size) ?
	    len : nego->sa->skeyid.skeyid_d_size;

	(void) memcpy(p, nego->sa->skeyid.skeyid_d, cplen);
}

uint16_t
get_ssh_skeyid_a_len(SshIkeNegotiation nego)
{
	if (nego->sa == NULL || !nego->sa->skeyid.initialized)
		return (0);

	return (nego->sa->skeyid.skeyid_a_size);
}

void
get_ssh_skeyid_a(SshIkeNegotiation nego, int len, uint8_t *p)
{
	int	cplen;

	if (nego->sa == NULL || !nego->sa->skeyid.initialized ||
	    nego->sa->skeyid.skeyid_a_size == 0)
		return;

	cplen = (len <= nego->sa->skeyid.skeyid_a_size) ?
	    len : nego->sa->skeyid.skeyid_a_size;

	(void) memcpy(p, nego->sa->skeyid.skeyid_a, cplen);
}

uint16_t
get_ssh_skeyid_e_len(SshIkeNegotiation nego)
{
	if (nego->sa == NULL || !nego->sa->skeyid.initialized)
		return (0);

	return (nego->sa->skeyid.skeyid_e_size);
}

void
get_ssh_skeyid_e(SshIkeNegotiation nego, int len, uint8_t *p)
{
	int	cplen;

	if (nego->sa == NULL || !nego->sa->skeyid.initialized ||
	    nego->sa->skeyid.skeyid_e_size == 0)
		return;

	cplen = (len <= nego->sa->skeyid.skeyid_e_size) ?
	    len : nego->sa->skeyid.skeyid_e_size;

	(void) memcpy(p, nego->sa->skeyid.skeyid_e, cplen);
}

uint16_t
get_ssh_encrkey_len(SshIkeNegotiation nego)
{
	if (nego->sa == NULL)
		return (0);

	return (nego->sa->cipher_key_len);
}

void
get_ssh_encrkey(SshIkeNegotiation nego, int len, uint8_t *p)
{
	int	cplen;

	if (nego->sa == NULL || nego->sa->cipher_key_len == 0)  {
		return;
	}

	cplen = (len <= nego->sa->cipher_key_len) ?
	    len : nego->sa->cipher_key_len;

	(void) memcpy(p, nego->sa->cipher_key, cplen);
}

uint16_t
get_ssh_iv_len(SshIkeNegotiation nego)
{
	if (nego->sa == NULL)
		return (0);

	return (nego->sa->cipher_iv_len);
}

void
get_ssh_iv(SshIkeNegotiation nego, int len, uint8_t *p)
{
	int	cplen;

	if (nego->sa == NULL || nego->sa->cipher_iv_len == 0)  {
		return;
	}

	cplen = (len <= nego->sa->cipher_iv_len) ?
	    len : nego->sa->cipher_iv_len;

	/*
	 * The cipher IV may be in one of two different places...
	 */
	if (nego->sa->cipher_iv != NULL)
		(void) memcpy(p, nego->sa->cipher_iv, cplen);
	else if (nego->ed->cipher_iv != NULL)
		(void) memcpy(p, nego->ed->cipher_iv, cplen);
}

SshIkePMPhaseI
get_ssh_pminfo(SshIkeNegotiation nego)
{
	return (nego->ike_pm_info);
}

/*
 * This function is called by the ssh library code when an
 * error occurs, allowing in.iked to track and report errors.
 */
void
ike_report_error(SshIkePMPhaseI pm_info, SshIkeNotifyMessageType type,
    Boolean decrypted, Boolean rx)
{
	phase1_t	*p1;

	PRTDBG((D_P1 | D_P2),
	    ("IKE error: type %u (%s), decrypted %d, received %d",
	    type, ssh_ike_error_code_to_string(type), decrypted, rx));

	if (pm_info == NULL) {
		PRTDBG((D_P1 | D_P2), ("Policy Manager phase 1 info not found!"
		    " (message type %d (%s))", type,
		    ssh_ike_error_code_to_string(type)));
		return;
	}

	if ((p1 = (phase1_t *)pm_info->policy_manager_data) == NULL) {
		PRTDBG((D_P1 | D_P2), ("Phase 1 is null!"));
		return;
	}

	switch (type) {
	case SSH_IKE_NOTIFY_MESSAGE_AUTHENTICATION_FAILED:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_HASH_INFORMATION:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_SIGNATURE:
		if (rx) {
			if (decrypted)
				p1->p1_errs.p1err_hash++;
			else
				p1->p1_errs.p1err_decrypt++;
		} else {
			p1->p1_errs.p1err_tx++;
		}
		break;

	case SSH_IKE_NOTIFY_MESSAGE_INVALID_PAYLOAD_TYPE:
	case SSH_IKE_NOTIFY_MESSAGE_DOI_NOT_SUPPORTED:
	case SSH_IKE_NOTIFY_MESSAGE_SITUATION_NOT_SUPPORTED:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_COOKIE:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_MAJOR_VERSION:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_MINOR_VERSION:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_EXCHANGE_TYPE:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_FLAGS:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_MESSAGE_ID:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_PROTOCOL_ID:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_SPI:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_TRANSFORM_ID:
	case SSH_IKE_NOTIFY_MESSAGE_ATTRIBUTES_NOT_SUPPORTED:
	case SSH_IKE_NOTIFY_MESSAGE_NO_PROPOSAL_CHOSEN:
	case SSH_IKE_NOTIFY_MESSAGE_BAD_PROPOSAL_SYNTAX:
	case SSH_IKE_NOTIFY_MESSAGE_PAYLOAD_MALFORMED:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_KEY_INFORMATION:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_ID_INFORMATION:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_CERT_ENCODING:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_CERTIFICATE:
	case SSH_IKE_NOTIFY_MESSAGE_CERT_TYPE_UNSUPPORTED:
	case SSH_IKE_NOTIFY_MESSAGE_INVALID_CERT_AUTHORITY:
	case SSH_IKE_NOTIFY_MESSAGE_ADDRESS_NOTIFICATION:
	case SSH_IKE_NOTIFY_MESSAGE_SA_LIFETIME:
	case SSH_IKE_NOTIFY_MESSAGE_CERTIFICATE_UNAVAILABLE:
	case SSH_IKE_NOTIFY_MESSAGE_UNSUPPORTED_EXCHANGE_TYPE:
	case SSH_IKE_NOTIFY_MESSAGE_UNEQUAL_PAYLOAD_LENGTHS:
	case SSH_IKE_NOTIFY_MESSAGE_NO_SA_ESTABLISHED:
	case SSH_IKE_NOTIFY_MESSAGE_NO_STATE_MATCHED:
	case SSH_IKE_NOTIFY_MESSAGE_EXCHANGE_DATA_MISSING:
	case SSH_IKE_NOTIFY_MESSAGE_TIMEOUT:
	case SSH_IKE_NOTIFY_MESSAGE_DELETED:
	case SSH_IKE_NOTIFY_MESSAGE_ABORTED:
	case SSH_IKE_NOTIFY_MESSAGE_UDP_HOST_UNREACHABLE:
	case SSH_IKE_NOTIFY_MESSAGE_UDP_PORT_UNREACHABLE:
		if (rx)
			p1->p1_errs.p1err_otherrx++;
		else
			p1->p1_errs.p1err_tx++;
		break;

	case SSH_IKE_NOTIFY_MESSAGE_RESERVED:
	case SSH_IKE_NOTIFY_MESSAGE_CONNECTED:
	case SSH_IKE_NOTIFY_MESSAGE_RESPONDER_LIFETIME:
	case SSH_IKE_NOTIFY_MESSAGE_REPLAY_STATUS:
	case SSH_IKE_NOTIFY_MESSAGE_INITIAL_CONTACT:
	case SSH_IKE_NOTIFY_MESSAGE_RETRY_LATER:
	case SSH_IKE_NOTIFY_MESSAGE_RETRY_NOW:
		break;
	default:
		PRTDBG(D_OP, ("Unrecognized error (%d) from IKE library.",
		    type));
	}
}
