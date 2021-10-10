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

#include <sys/types.h>
#include <sys/stream.h>
#include <net/pfkeyv2.h>
#include <inet/common.h>
#include <inet/ip.h>
#include <inet/ip_impl.h>
#include <sys/strsun.h>
#include <inet/ipsec_info.h>
#include <inet/sadb.h>
#include <inet/ipsec_impl.h>
#include <inet/ipsecah.h>
#include <inet/ipsecesp.h>

/* For encryption, the first mblk_t points to the esph_t, skip over this. */
#define	ESPPTR_INIT(out, mp, data_off, data_mp, esph_mp, esph_ptr,	\
    esph_off, ivlen) {							\
	if (out) {							\
		data_off = 0;						\
		esph_mp = mp->b_cont;					\
		data_mp = esph_mp->b_cont;				\
	} else {							\
		data_off = esph_off + sizeof (esph_t) +	ivlen;		\
		data_mp = esph_mp = mp;					\
	}								\
	esph_ptr = (esph_t *)(esph_mp->b_rptr + esph_off);		\
}
/*
 * Create the nonce, which is made up of the salt and the IV.
 * Copy the salt from the SA and the IV from the packet.
 * For inbound packets we copy the IV from the packet because it
 * was set by the sending system, for outbound packets we copy the IV
 * from the packet because the IV in the SA may be changed by another
 * thread, the IV in the packet was created while holding a mutex.
 */
#define	INIT_NONCE(assoc, nonce, iv_ptr) {				\
	bcopy(assoc->ipsa_nonce, nonce, assoc->ipsa_saltlen);		\
	nonce += assoc->ipsa_saltlen;					\
	bcopy(iv_ptr, nonce, assoc->ipsa_iv_len);			\
}
/*
 * For combined mode ciphers, the ciphertext is the same
 * size as the clear text, the ICV should follow the
 * ciphertext. To convince the kcf to allow in-line
 * encryption, with an ICV, use ipsec_out_crypto_mac
 * to point to the same buffer as the data. The calling
 * function need to ensure the buffer is large enough to
 * include the ICV.
 */
#define	CM_INIT_MAC(ic, length) {					\
	bcopy(&ic->ic_crypto_data, &ic->ic_crypto_mac,			\
	    sizeof (crypto_data_t));					\
	ic->ic_crypto_mac.cd_length = length;				\
	ic->ic_mac = &ic->ic_crypto_mac;				\
}

void
ccm_params_init(ipsa_t *assoc, uint_t data_len, ipsec_crypto_t *ic,
    mblk_t *mp, uint_t esph_offset, boolean_t outbound)
{
	uchar_t *nonce;
	mblk_t *data_mp, *esph_mp;
	uint_t encr_offset;
	esph_t *esph_ptr;
	uchar_t *iv_ptr;
	crypto_mechanism_t *combined_mech;
	CK_AES_CCM_PARAMS *params;
	ipsa_cm_mech_t *cm_mech = &ic->ic_cmm;

	combined_mech = (crypto_mechanism_t *)cm_mech;
	params = (CK_AES_CCM_PARAMS *)(combined_mech + 1);
	nonce = (uchar_t *)(params + 1);

	ESPPTR_INIT(outbound, mp, encr_offset, data_mp, esph_mp, esph_ptr,
	    esph_offset, assoc->ipsa_iv_len);

	iv_ptr = (uchar_t *)(esph_ptr + 1);

	params->ulMACSize = assoc->ipsa_mac_len;
	params->ulNonceSize = assoc->ipsa_nonce_len;
	params->ulAuthDataSize = sizeof (esph_t);
	params->ulDataSize = data_len;
	params->nonce = nonce;
	params->authData = (uchar_t *)esph_ptr;

	cm_mech->combined_mech.cm_type = assoc->ipsa_emech.cm_type;
	cm_mech->combined_mech.cm_param_len = sizeof (CK_AES_CCM_PARAMS);
	cm_mech->combined_mech.cm_param = (caddr_t)params;

	INIT_NONCE(assoc, nonce, iv_ptr);

	/* Initialise the crypto_data_t. */
	ESP_INIT_CRYPTO_DATA(&ic->ic_crypto_data, data_mp, encr_offset,
	    data_len);

	if (!outbound) {
		ic->ic_espstart = esph_offset;
		ic->ic_processed_len = data_len + assoc->ipsa_iv_len;
	}

	CM_INIT_MAC(ic, data_len + assoc->ipsa_mac_len);
}

/* ARGSUSED */
void
cbc_params_init(ipsa_t *assoc, uint_t data_len, ipsec_crypto_t *ic,
    mblk_t *mp, uint_t esph_offset, boolean_t outbound)
{
	mblk_t *data_mp, *esph_mp;
	uint_t encr_offset;
	esph_t *esph_ptr;
	boolean_t dual_mode;
	char *iv_ptr;
	ipsa_cm_mech_t *cm_mech = &ic->ic_cmm;

	dual_mode = ((assoc->ipsa_encr_alg != SADB_EALG_NULL) &&
	    (assoc->ipsa_auth_alg != SADB_AALG_NONE));

	/* The output buffer is the same as the input buffer (inline). */
	ic->ic_mac = NULL;

	cm_mech->combined_mech.cm_type = assoc->ipsa_emech.cm_type;
	cm_mech->combined_mech.cm_param_len = 0;
	cm_mech->combined_mech.cm_param = NULL;

	ESPPTR_INIT(outbound, mp, encr_offset, data_mp, esph_mp, esph_ptr,
	    esph_offset, assoc->ipsa_iv_len);

	/* The Initialization Vector follows the ESP header. */
	iv_ptr = (char *)(esph_ptr + 1);

	/*
	 * The KcF dual mode providers provide encryption and authentication
	 * in a single operation. The ESP_INIT_CRYPTO_DUAL_DATA macro
	 * Initializes the crypto_dual_data_t structure, which has offsets to
	 * the data to be authenticated and [en,de]crypted as well as the
	 * lengths of the authentication and [en,de]crypted data.
	 *
	 * Authenticate from ESP header through IV and payload. [En,De]crypt
	 * payload starting after the IV. The ICV is not part of the data
	 * authenticated.
	 *
	 * Outbound:
	 * Skip the mblk_t that contains ESP header and IV.
	 *
	 * Inbound:
	 * The ESP header, IV and payload are in a single mblk_t (which may be
	 * continued if the payload is large enough), the encryptio and
	 * authentication offsets are calculated from the mblk_t read pointer.
	 */
	if (dual_mode) {
		if (outbound) {
			ESP_INIT_CRYPTO_DUAL_DATA(&ic->ic_crypto_dual_data,
			    esph_mp, MBLKL(esph_mp), data_len, esph_offset,
			    data_len + assoc->ipsa_iv_len + sizeof (esph_t));
		} else {
			ESP_INIT_CRYPTO_DUAL_DATA(&ic->ic_crypto_dual_data,
			    esph_mp, esph_offset,
			    MBLKL(esph_mp) - esph_offset - assoc->ipsa_mac_len,
			    esph_offset + sizeof (esph_t) + assoc->ipsa_iv_len,
			    data_len - assoc->ipsa_mac_len);
		}
		/* specify IV */
		ic->ic_crypto_dual_data.dd_miscdata = iv_ptr;

	} else {
		/* Initialise the crypto_data_t. */
		ESP_INIT_CRYPTO_DATA(&ic->ic_crypto_data, data_mp,
		    encr_offset, data_len);
		ic->ic_crypto_data.cd_miscdata = (caddr_t)iv_ptr;
	}
	if (!outbound) {
		ic->ic_espstart = esph_offset;
		ic->ic_processed_len = data_len + assoc->ipsa_iv_len;
	}
}

/* ARGSUSED */
void
gcm_params_init(ipsa_t *assoc, uint_t data_len, ipsec_crypto_t *ic,
    mblk_t *mp, uint_t esph_offset, boolean_t outbound)
{
	mblk_t *data_mp, *esph_mp;
	uchar_t *nonce;
	esph_t *esph_ptr;
	uchar_t *iv_ptr;
	uint_t encr_offset;
	crypto_mechanism_t *combined_mech;
	CK_AES_GCM_PARAMS *params;
	ipsa_cm_mech_t *cm_mech = &ic->ic_cmm;

	combined_mech = (crypto_mechanism_t *)cm_mech;
	params = (CK_AES_GCM_PARAMS *)(combined_mech + 1);
	nonce = (uchar_t *)(params + 1);

	ESPPTR_INIT(outbound, mp, encr_offset, data_mp, esph_mp, esph_ptr,
	    esph_offset, assoc->ipsa_iv_len);

	iv_ptr = (uchar_t *)(esph_ptr + 1);

	params->pIv = nonce;
	params->ulIvLen = assoc->ipsa_nonce_len;
	params->ulIvBits = SADB_8TO1(assoc->ipsa_nonce_len);
	params->pAAD = (uchar_t *)esph_ptr;
	params->ulAADLen = sizeof (esph_t);
	params->ulTagBits = SADB_8TO1(assoc->ipsa_mac_len);

	cm_mech->combined_mech.cm_type = assoc->ipsa_emech.cm_type;
	cm_mech->combined_mech.cm_param_len = sizeof (CK_AES_GCM_PARAMS);
	cm_mech->combined_mech.cm_param = (caddr_t)params;

	INIT_NONCE(assoc, nonce, iv_ptr);

	ESP_INIT_CRYPTO_DATA(&ic->ic_crypto_data, data_mp, encr_offset,
	    data_len);

	if (!outbound) {
		ic->ic_espstart = esph_offset;
		ic->ic_processed_len = data_len + assoc->ipsa_iv_len;
	}

	CM_INIT_MAC(ic, data_len + assoc->ipsa_mac_len);
}

/* ARGSUSED */
void
ah_gmac_params_init(ipsa_t *assoc, uint_t data_len, ipsec_crypto_t *ic,
    mblk_t *mp, uint_t header_offset, boolean_t outbound)
{
	uchar_t *nonce;
	uchar_t *iv_ptr;
	crypto_mechanism_t *combined_mech;
	CK_AES_GMAC_PARAMS *params;
	ipsa_cm_mech_t *cm_mech = &ic->ic_cmm;

	combined_mech = (crypto_mechanism_t *)cm_mech;
	params = (CK_AES_GMAC_PARAMS *)(combined_mech + 1);
	nonce = (uchar_t *)(params + 1);

	/*
	 * The calling function in "ah" will have already ensured that the
	 * data to be authenticated (AAD) is in a single contiguous buffer.
	 * See comments in ah_submit_req_inbound().
	 *
	 * When GMAC is called via crypto_mac()/crypto_mac_verify() the
	 * underlying Kcf implementation is different to that used by ESP.
	 * The CK_AES_GMAC_PARAMS is morphed into a CK_AES_GCM_PARAMS and
	 * the AAD is discarded, for this reason the pointer to the AAD is
	 * passed in via crypto_data.
	 */
	ic->ic_crypto_data.cd_format = CRYPTO_DATA_RAW;
	ic->ic_crypto_data.cd_raw.iov_base = (char *)ic->ic_AAD->b_rptr;
	ic->ic_crypto_data.cd_raw.iov_len = data_len;
	ic->ic_crypto_data.cd_offset = 0;
	ic->ic_crypto_data.cd_length = data_len;

	params->pIv = nonce;
	params->pAAD = (uchar_t *)ic->ic_AAD->b_rptr;
	params->ulAADLen = data_len;

	/*
	 * The Initialisation Vector (IV) needs to be copied into the
	 * CK_AES_GMAC_PARAMS structure. The AAD is a contiguous buffer copy
	 * of the original mblk_t chain, the IV follows the IP and ah_t headers
	 * in this buffer. The length of the IP header may vary (IP options) so
	 * the easiest way to calculate the offset to IV is to look at the
	 * original mblk_t.
	 */
	iv_ptr = params->pAAD + MBLKL(mp) - assoc->ipsa_iv_len -
	    assoc->ipsa_mac_len;

	cm_mech->combined_mech.cm_type = assoc->ipsa_amech.cm_type;
	cm_mech->combined_mech.cm_param_len = sizeof (CK_AES_GMAC_PARAMS);
	cm_mech->combined_mech.cm_param = (caddr_t)params;

	INIT_NONCE(assoc, nonce, iv_ptr);

	/*
	 * The GMAC for AH is calculated over the whole of the IP packet
	 * including the ah_t header and the IV and ICV fields. The IV and ICV
	 * fields are zero'd out before making this calculation (remember this
	 * is a copy of the real data. The ICV was zero'd out when the pseudo
	 * IP header was created, zero out the IV now its been copied to pIv.
	 */
	bzero(iv_ptr, assoc->ipsa_iv_len);
}

void
esp_gmac_params_init(ipsa_t *assoc, uint_t data_len, ipsec_crypto_t *ic,
    mblk_t *mp, uint_t esph_offset, boolean_t outbound)
{
	uchar_t *nonce;
	mblk_t  *esph_mp;
	mblk_t *data_mp;
	mblk_t *last_mp;
	esph_t *esph_ptr;
	uint_t offset_len;
	uchar_t *iv_ptr;
	char *icv_ptr;
	crypto_mechanism_t *combined_mech;
	CK_AES_GMAC_PARAMS *params;
	uchar_t *AAD;
	ipsa_cm_mech_t *cm_mech = &ic->ic_cmm;

	combined_mech = (crypto_mechanism_t *)cm_mech;
	params = (CK_AES_GMAC_PARAMS *)(combined_mech + 1);
	nonce = (uchar_t *)(params + 1);

	ESPPTR_INIT(outbound, mp, offset_len, data_mp, esph_mp, esph_ptr,
	    esph_offset, assoc->ipsa_iv_len);

	iv_ptr = (uchar_t *)esph_ptr + sizeof (esph_t);

	/*
	 * AES-GMAC authenticates from the beginning of the esph_t, through
	 * the IV and the ULP data, CK_AES_GMAC_PARAMS->AAD points to the
	 * start of the esph_t.
	 *
	 * The implementation of AES-GMAC requires that the AAD be in a
	 * contiguous buffer, if that is not the case (outbound packets
	 * are a mblk_t chain), then use msgpullup() to copy the data. The
	 * original data is not used to calculate the GMAC, this operation
	 * is performed on the copy. For ESP, the ICV is written at the end
	 * of the buffer containing the original ULP data.
	 */

	if (mp->b_cont == NULL) {
		AAD = (uchar_t *)(mp->b_rptr) + esph_offset;
		icv_ptr = (char *)(data_mp->b_wptr - (char)assoc->ipsa_mac_len);
	} else {
		ic->ic_AAD = msgpullup(mp->b_cont, -1);
		if (ic->ic_AAD == NULL) {
			/*
			 * If msgpullup() fails, setting params to NULL will
			 * cause the KcF to fail in a controlled way.
			 */
			params = NULL;
			goto fail;
		}
		AAD = (uchar_t *)(ic->ic_AAD->b_rptr) + esph_offset;
		for (last_mp = mp->b_cont; last_mp->b_cont != NULL;
		    last_mp = last_mp->b_cont)
			;
		icv_ptr = (char *)(last_mp->b_wptr - (char)assoc->ipsa_mac_len);
	}
	/*
	 * The length of the crypto_data is set to zero, this is because the
	 * actual data used to generate the ICV (IE: AAD) is passed in via
	 * the CK_AES_GMAC_PARAMS structure.
	 *
	 * Don't use ESP_INIT_CRYPTO_DATA it does not DTRT.
	 */

	ic->ic_mac = &ic->ic_crypto_mac;
	ic->ic_crypto_data.cd_format = CRYPTO_DATA_RAW;
	ic->ic_crypto_data.cd_offset = 0;
	ic->ic_crypto_data.cd_length = 0;
	bcopy(&ic->ic_crypto_data, &ic->ic_crypto_mac, sizeof (crypto_data_t));

	params->pIv = nonce;
	params->pAAD = AAD;
	params->ulAADLen = data_len;

	if (outbound) {
		ic->ic_crypto_data.cd_raw.iov_base = (char *)data_mp->b_rptr;
		ic->ic_crypto_mac.cd_raw.iov_base = icv_ptr;
		ic->ic_crypto_mac.cd_raw.iov_len = assoc->ipsa_mac_len;
		ic->ic_crypto_mac.cd_length = assoc->ipsa_mac_len;
		params->ulAADLen += assoc->ipsa_iv_len + sizeof (esph_t);
	} else {
		/* For inbound packets, data_len includes the ICV. */
		ic->ic_crypto_data.cd_raw.iov_base = (char *)mp->b_rptr +
		    offset_len + data_len - assoc->ipsa_mac_len;
		ic->ic_crypto_mac.cd_raw.iov_base =
		    ic->ic_crypto_data.cd_raw.iov_base;
		ic->ic_crypto_data.cd_raw.iov_len = MBLKL(mp);
		ic->ic_crypto_data.cd_length = assoc->ipsa_mac_len;
		ic->ic_crypto_mac.cd_offset = esph_offset;
	}

	if (!outbound) {
		ic->ic_espstart = esph_offset;
		ic->ic_processed_len = data_len + assoc->ipsa_mac_len;
	}

	cm_mech->combined_mech.cm_type = assoc->ipsa_emech.cm_type;
	cm_mech->combined_mech.cm_param_len = sizeof (CK_AES_GMAC_PARAMS);
fail:
	cm_mech->combined_mech.cm_param = (caddr_t)params;

	INIT_NONCE(assoc, nonce, iv_ptr);
}
