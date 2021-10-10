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

#include <smbsrv/smb_kproto.h>

/*
 * The echo request is used to test the connection to the server,
 * and to see if the server is still responding.  The tid is ignored,
 * so this request may be sent to the server even if there are no
 * tree connections to the server.
 *
 * Each response echoes the data sent, though ByteCount may indicate
 * no data. If echo-count is zero, no response is sent.
 */
smb_sdrc_t
smb_pre_echo(smb_request_t *sr)
{
	DTRACE_SMB_1(op__Echo__start, smb_request_t *, sr);
	return (SDRC_SUCCESS);
}

void
smb_post_echo(smb_request_t *sr)
{
	DTRACE_SMB_1(op__Echo__done, smb_request_t *, sr);
}

smb_sdrc_t
smb_com_echo(struct smb_request *sr)
{
	unsigned short necho;
	unsigned short nbytes;
	unsigned short i;
	struct mbuf_chain reply;
	char *data;

	if (smbsr_decode_vwv(sr, "w", &necho) != 0)
		return (SDRC_ERROR);

	nbytes = sr->smb_bcc;
	data = smb_srm_zalloc(sr, nbytes);

	if (smb_mbc_decodef(&sr->smb_data, "#c", nbytes, data))
		return (SDRC_ERROR);

	for (i = 1; i <= necho; ++i) {
		MBC_INIT(&reply, SMB_HEADER_ED_LEN + 10 + nbytes);

		(void) smb_mbc_encodef(&reply, "H", &sr->sr_header);
		(void) smb_mbc_encodef(&reply, "bww#c", 1, i,
		    nbytes, nbytes, data);

		if (sr->session->signing.flags & SMB_SIGNING_ENABLED)
			smb_sign_reply(sr, &reply);

		(void) smb_session_send(sr->session, 0, &reply);
	}

	return (SDRC_NO_REPLY);
}
