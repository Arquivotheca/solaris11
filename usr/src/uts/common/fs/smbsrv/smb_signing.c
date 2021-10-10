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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * These routines provide the SMB MAC signing for the SMB server.
 * The routines calculate the signature of a SMB message in an mbuf chain.
 *
 * The following table describes the client server signing relationship.
 * These rules are implemented in smb_session_setup.c, prior to calling
 * smb_sign_init().
 *			| Client	| Client      | Client
 * 			| Required	| Enabled     | Disabled
 * ---------------------+---------------+------------ +--------------
 * Server Required	| Signed	| Signed      | Fail
 * ---------------------+---------------+-------------+--------------
 * Server Enabled	| Signed	| Signed      | Not Signed
 * ---------------------+---------------+-------------+--------------
 * Server Disabled	| Signed	| Not Signed  | Not Signed
 */

#include <sys/uio.h>
#include <smbsrv/smb_kproto.h>
#include <smbsrv/msgbuf.h>
#include <sys/crypto/api.h>
#include <sys/md5.h>

#define	SMB_SIG_SIZE	8
#define	SMB_SIG_OFFS	14

static int smb_sign_calc(mbuf_chain_t *, smb_sign_t *, uint32_t, uint8_t *);

/* This holds the MD5 mechanism */
static	crypto_mechanism_t crypto_mech = {CRYPTO_MECHANISM_INVALID, 0, 0};

/*
 * smb_sign_init
 *
 * Intializes MAC key based on the user session key and
 * NTLM response and store it in the signing structure.
 */
void
smb_sign_init(smb_request_t *sr, smb_session_key_t *session_key,
	char *resp, int resp_len)
{
	smb_sign_t *sign = &sr->session->signing;

	/*
	 * Initialise the crypto mechanism to MD5 if it not
	 * already initialised.
	 */
	if (crypto_mech.cm_type ==  CRYPTO_MECHANISM_INVALID) {
		crypto_mech.cm_type = crypto_mech2id(SUN_CKM_MD5);
		if (crypto_mech.cm_type == CRYPTO_MECHANISM_INVALID) {
			/*
			 * There is no MD5 crypto mechanism
			 * so turn off signing
			 */
			sr->sr_cfg->skc_signing_enable = 0;
			sr->session->secmode &=
			    (~NEGOTIATE_SECURITY_SIGNATURES_ENABLED);
			sign->flags &= ~SMB_SIGNING_SUPPORTED;
			cmn_err(CE_WARN,
			    "SmbSignInit: signing disabled (no MD5)");
			return;
		}
	}

	/* MAC key = concat (SessKey, NTLMResponse) */
	bcopy(session_key->val, sign->mackey, session_key->len);
	sign->mackey_len = session_key->len;

	if (resp != NULL) {
		bcopy(resp, sign->mackey + session_key->len, resp_len);
		sign->mackey_len += resp_len;
	}

	sr->session->signing.seqnum = 0;
	sr->sr_seqnum = 2;
	sr->reply_seqnum = 1;
	sign->flags |= SMB_SIGNING_ENABLED;
}

/*
 * smb_sign_calc
 *
 * Calculates MAC signature for the given buffer and returns
 * it in the mac_sign parameter.
 *
 * The sequence number is placed in the first four bytes of the signature
 * field of the signature and the other 4 bytes are zeroed.
 * The signature is the first 8 bytes of the MD5 result of the
 * concatenated MAC key and the SMB message.
 *
 * MACsig = head(MD5(concat(MACKey, SMBMsg)), 8)
 *
 * where
 *	MACKey = concat( UserSessionKey, NTLMResp )
 * and
 *	SMBMsg is the SMB message containing the sequence number.
 *
 * Return 0 if success else -1
 *
 */
static int
smb_sign_calc(mbuf_chain_t *mbc, smb_sign_t *sign, uint32_t seqnum,
    uint8_t *mac_sign)
{
	uint32_t seq_buf[2] = {0, 0};
	uint8_t mac[MD5_DIGEST_LENGTH];
	mbuf_t *mbuf = mbc->chain;
	int offset = mbc->chain_offset;
	int size;
	int status;

	crypto_data_t data;
	crypto_data_t digest;
	crypto_context_t crypto_ctx;

	data.cd_format = CRYPTO_DATA_RAW;
	data.cd_offset = 0;
	data.cd_length = (size_t)-1;
	data.cd_miscdata = 0;

	digest.cd_format = CRYPTO_DATA_RAW;
	digest.cd_offset = 0;
	digest.cd_length = (size_t)-1;
	digest.cd_miscdata = 0;
	digest.cd_raw.iov_base = (char *)mac;
	digest.cd_raw.iov_len = sizeof (mac);

	status = crypto_digest_init(&crypto_mech, &crypto_ctx, 0);
	if (status != CRYPTO_SUCCESS)
		goto error;

	/*
	 * Put the sequence number into the first 4 bytes
	 * of the signature field in little endian format.
	 * We are using a buffer to represent the signature
	 * rather than modifying the SMB message.
	 */
#ifdef __sparc
	{
		uint32_t temp;
		((uint8_t *)&temp)[0] = ((uint8_t *)&seqnum)[3];
		((uint8_t *)&temp)[1] = ((uint8_t *)&seqnum)[2];
		((uint8_t *)&temp)[2] = ((uint8_t *)&seqnum)[1];
		((uint8_t *)&temp)[3] = ((uint8_t *)&seqnum)[0];

		seq_buf[0] = temp;
	}
#else
	seq_buf[0] = seqnum;
#endif

	/* Digest the MACKey */
	data.cd_raw.iov_base = (char *)sign->mackey;
	data.cd_raw.iov_len = sign->mackey_len;
	data.cd_length = sign->mackey_len;
	status = crypto_digest_update(crypto_ctx, &data, 0);
	if (status != CRYPTO_SUCCESS)
		goto error;

	/* Find start of data in chain */
	while (offset >= mbuf->m_len) {
		offset -= mbuf->m_len;
		mbuf = mbuf->m_next;
	}

	/* Digest the SMB packet up to the signature field */
	size = SMB_SIG_OFFS;
	while (size >= mbuf->m_len - offset) {
		data.cd_raw.iov_base = &mbuf->m_data[offset];
		data.cd_raw.iov_len = mbuf->m_len - offset;
		data.cd_length = mbuf->m_len - offset;
		status = crypto_digest_update(crypto_ctx, &data, 0);
		if (status != CRYPTO_SUCCESS)
			goto error;

		size -= mbuf->m_len - offset;
		mbuf = mbuf->m_next;
		offset = 0;
	}
	if (size > 0) {
		data.cd_raw.iov_base = &mbuf->m_data[offset];
		data.cd_raw.iov_len = size;
		data.cd_length = size;
		status = crypto_digest_update(crypto_ctx, &data, 0);
		if (status != CRYPTO_SUCCESS)
			goto error;
		offset += size;
	}

	/*
	 * Digest in the seq_buf instead of the signature
	 * which has the sequence number
	 */

	data.cd_raw.iov_base = (char *)seq_buf;
	data.cd_raw.iov_len = SMB_SIG_SIZE;
	data.cd_length = SMB_SIG_SIZE;
	status = crypto_digest_update(crypto_ctx, &data, 0);
	if (status != CRYPTO_SUCCESS)
		goto error;

	/* Find the end of the signature field  */
	offset += SMB_SIG_SIZE;
	while (offset >= mbuf->m_len) {
		offset -= mbuf->m_len;
		mbuf = mbuf->m_next;
	}
	/* Digest the rest of the SMB packet */
	while (mbuf) {
		data.cd_raw.iov_base = &mbuf->m_data[offset];
		data.cd_raw.iov_len = mbuf->m_len - offset;
		data.cd_length = mbuf->m_len - offset;
		status = crypto_digest_update(crypto_ctx, &data, 0);
		if (status != CRYPTO_SUCCESS)
			goto error;
		mbuf = mbuf->m_next;
		offset = 0;
	}
	digest.cd_length = MD5_DIGEST_LENGTH;
	status = crypto_digest_final(crypto_ctx, &digest, 0);
	if (status != CRYPTO_SUCCESS)
		goto error;
	bcopy(mac, mac_sign, SMB_SIG_SIZE);
	return (0);
error:
	cmn_err(CE_WARN, "SmbSignCalc: crypto error %d", status);
	return (-1);

}


/*
 * smb_sign_check_request
 *
 * Calculates MAC signature for the request mbuf chain
 * using the next expected sequence number and compares
 * it to the given signature.
 *
 * Note it does not check the signature for secondary transactions
 * as their sequence number is the same as the original request.
 *
 * Return 0 if the signature verifies, otherwise, returns -1;
 *
 */
int
smb_sign_check_request(smb_request_t *sr)
{
	smb_sign_t	*sign = &sr->session->signing;
	uint8_t		mac_sig[SMB_SIG_SIZE];
	int		rtn = 0;

	/*
	 * Don't check secondary transactions - we don't know the sequence
	 * number.
	 */
	if (sr->sr_com_current == SMB_COM_TRANSACTION_SECONDARY ||
	    sr->sr_com_current == SMB_COM_TRANSACTION2_SECONDARY ||
	    sr->sr_com_current == SMB_COM_NT_TRANSACT_SECONDARY)
		return (0);

	/* calculate mac signature */
	rtn = smb_sign_calc(&sr->sr_command_dup, sign, sr->sr_seqnum, mac_sig);
	if (rtn != 0)
		return (-1);

	/* compare the signatures */
	if (bcmp(mac_sig, sr->sr_signature, SMB_SIG_SIZE) != 0) {
		DTRACE_PROBE2(smb__signing__req, smb_request_t, sr,
		    smb_sign_t *, sr->sr_signature);
		cmn_err(CE_NOTE, "smb_sign_check_request: bad signature");
		return (-1);
	}

	return (0);
}

/*
 * smb_sign_check_secondary
 *
 * Calculates MAC signature for the secondary transaction mbuf chain
 * and compares it to the given signature.
 * Return 0 if the signature verifies, otherwise, returns -1;
 *
 */
int
smb_sign_check_secondary(smb_request_t *sr, uint_t reply_seqnum)
{
	uint8_t		mac_sig[SMB_SIG_SIZE];
	smb_sign_t	*sign = &sr->session->signing;
	int		rtn = 0;

	/* calculate mac signature */
	if (smb_sign_calc(&sr->sr_command_dup, sign, reply_seqnum - 1,
	    mac_sig) != 0)
		return (-1);

	/* compare the signatures */
	if (bcmp(mac_sig, sr->sr_signature, SMB_SIG_SIZE) != 0) {
		cmn_err(CE_WARN, "smb_sign_check_secondary: bad signature");
		rtn = -1;
	}
	/* Save the reply sequence number */
	sr->reply_seqnum = reply_seqnum;

	return (rtn);
}

/*
 * smb_sign_reply
 *
 * Calculates MAC signature for the given mbuf chain,
 * and write it to the signature field in the mbuf.
 *
 */
void
smb_sign_reply(smb_request_t *sr, mbuf_chain_t *reply)
{
	mbuf_chain_t resp;
	smb_sign_t *sign = &sr->session->signing;
	uint8_t signature[SMB_SIG_SIZE];
	mbuf_t *mbuf;
	int size = SMB_SIG_SIZE;
	uint8_t *sig_ptr = signature;
	int offset = 0;

	if (reply)
		resp = *reply;
	else
		resp = sr->reply;

	/* Reset offset to start of reply */
	resp.chain_offset = 0;
	mbuf = resp.chain;

	/*
	 * Calculate MAC signature
	 */
	if (smb_sign_calc(&resp, sign, sr->reply_seqnum, signature) != 0) {
		cmn_err(CE_WARN, "smb_sign_reply: error in smb_sign_calc");
		return;
	}

	/*
	 * Put signature in the response
	 *
	 * First find start of signature in chain (offset + signature offset)
	 */
	offset += SMB_SIG_OFFS;
	while (offset >= mbuf->m_len) {
		offset -= mbuf->m_len;
		mbuf = mbuf->m_next;
	}

	while (size >= mbuf->m_len - offset) {
		(void) memcpy(&mbuf->m_data[offset],
		    sig_ptr, mbuf->m_len - offset);
		offset = 0;
		sig_ptr += mbuf->m_len - offset;
		size -= mbuf->m_len - offset;
		mbuf = mbuf->m_next;
	}
	if (size > 0) {
		(void) memcpy(&mbuf->m_data[offset], sig_ptr, size);
	}
}
