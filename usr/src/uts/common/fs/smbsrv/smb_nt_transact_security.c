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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <smbsrv/smb_kproto.h>

static void smb_encode_sd(struct smb_xa *, smb_sd_t *, uint32_t);
static void smb_encode_sacl(struct smb_xa *, smb_acl_t *);
static void smb_encode_dacl(struct smb_xa *, smb_acl_t *);
static smb_acl_t *smb_decode_acl(struct smb_xa *, uint32_t);

/*
 * smb_nt_transact_query_security_info
 *
 * This command allows the client to retrieve the security descriptor
 * on a file. The result of the call is returned to the client in the
 * Data part of the transaction response.
 *
 * Some clients specify a non-zero maximum data return size (mdrcnt)
 * for the SD and some specify zero. In either case, if the mdrcnt is
 * too small we need to return NT_STATUS_BUFFER_TOO_SMALL and a buffer
 * size hint. The client should then retry with the appropriate buffer
 * size.
 *
 *  Client Parameter Block             Description
 *  ================================== =================================
 *
 *  USHORT Fid;                        FID of target
 *  USHORT Reserved;                   MBZ
 *  ULONG secinfo;                     Fields of descriptor to set
 *
 *   Data Block Encoding                Description
 *   ================================== ==================================
 *
 *   Data[TotalDataCount]               Security Descriptor information
 */

smb_sdrc_t
smb_nt_transact_query_security_info(struct smb_request *sr, struct smb_xa *xa)
{
	smb_sd_t sd;
	uint32_t secinfo;
	uint32_t sdlen;
	uint32_t status;

	if (smb_mbc_decodef(&xa->req_param_mb, "w2.l",
	    &sr->smb_fid, &secinfo) != 0) {
		smb_errcode_seterror(NT_STATUS_INVALID_PARAMETER, 0, 0);
		return (SDRC_ERROR);
	}

	smbsr_lookup_file(sr);
	if (sr->fid_ofile == NULL) {
		smb_errcode_seterror(NT_STATUS_INVALID_HANDLE,
		    ERRDOS, ERRbadfid);
		return (SDRC_ERROR);
	}


	if ((sr->fid_ofile->f_node == NULL) ||
	    SMB_FTYPE_IS_PIPE(sr->fid_ofile->f_ftype)) {
		smb_errcode_seterror(NT_STATUS_ACCESS_DENIED,
		    ERRDOS, ERROR_ACCESS_DENIED);
		return (SDRC_ERROR);
	}

	sr->user_cr = smb_ofile_getcred(sr->fid_ofile);

	if (sr->tid_tree->t_acltype != ACE_T) {
		/*
		 * If target filesystem doesn't support ACE_T acls then
		 * don't process SACL
		 */
		secinfo &= ~SMB_SACL_SECINFO;
	}

	status = smb_sd_read(sr, &sd, secinfo);
	if (status != NT_STATUS_SUCCESS)
		return (SDRC_ERROR);

	sdlen = smb_sd_len(&sd, secinfo);
	if (sdlen == 0) {
		smb_sd_term(&sd);
		smb_errcode_seterror(NT_STATUS_INVALID_SECURITY_DESCR, 0, 0);
		return (SDRC_ERROR);
	}

	if (sdlen > xa->smb_mdrcnt) {
		/*
		 * The maximum data return count specified by the
		 * client is not big enough to hold the security
		 * descriptor. We have to return an error but we
		 * should provide a buffer size hint for the client.
		 */
		(void) smb_mbc_encodef(&xa->rep_param_mb, "l", sdlen);
		smb_errcode_set(NT_STATUS_BUFFER_TOO_SMALL, ERRDOS,
		    ERROR_INSUFFICIENT_BUFFER);
		smb_sd_term(&sd);
		return (SDRC_SUCCESS);
	}

	smb_encode_sd(xa, &sd, secinfo);
	(void) smb_mbc_encodef(&xa->rep_param_mb, "l", sdlen);
	smb_sd_term(&sd);
	return (SDRC_SUCCESS);
}

/*
 * smb_nt_transact_set_security_info
 *
 * This command allows the client to change the security descriptor on a
 * file. All we do here is decode the parameters and the data. The data
 * is passed directly to smb_nt_set_security_object, with the security
 * information describing the information to set. There are no response
 * parameters or data.
 *
 *   Client Parameter Block Encoding    Description
 *   ================================== ==================================
 *   USHORT Fid;                        FID of target
 *   USHORT Reserved;                   MBZ
 *   ULONG SecurityInformation;         Fields of SD that to set
 *
 *   Data Block Encoding                Description
 *   ================================== ==================================
 *   Data[TotalDataCount]               Security Descriptor information
 */
smb_sdrc_t
smb_nt_transact_set_security_info(struct smb_request *sr, struct smb_xa *xa)
{
	smb_sd_t sd;
	uint32_t secinfo;
	uint32_t status;

	if (smb_mbc_decodef(&xa->req_param_mb, "w2.l",
	    &sr->smb_fid, &secinfo) != 0) {
		smb_errcode_seterror(NT_STATUS_INVALID_PARAMETER, 0, 0);
		return (SDRC_ERROR);
	}

	smbsr_lookup_file(sr);
	if (sr->fid_ofile == NULL) {
		smb_errcode_seterror(NT_STATUS_INVALID_HANDLE,
		    ERRDOS, ERRbadfid);
		return (SDRC_ERROR);
	}

	if ((sr->fid_ofile->f_node == NULL) ||
	    SMB_FTYPE_IS_PIPE(sr->fid_ofile->f_ftype)) {
		smb_errcode_seterror(NT_STATUS_ACCESS_DENIED, 0, 0);
		return (SDRC_ERROR);
	}

	sr->user_cr = smb_ofile_getcred(sr->fid_ofile);

	if (SMB_TREE_IS_READONLY(sr)) {
		smb_errcode_seterror(NT_STATUS_MEDIA_WRITE_PROTECTED, 0, 0);
		return (SDRC_ERROR);
	}

	if (sr->tid_tree->t_acltype != ACE_T) {
		/*
		 * If target filesystem doesn't support ACE_T acls then
		 * don't process SACL
		 */
		secinfo &= ~SMB_SACL_SECINFO;
	}

	if ((secinfo & SMB_ALL_SECINFO) == 0) {
		return (NT_STATUS_SUCCESS);
	}

	status = smb_decode_sd(xa, &sd);
	if (status != NT_STATUS_SUCCESS) {
		smb_errcode_seterror(status, 0, 0);
		return (SDRC_ERROR);
	}

	if (((secinfo & SMB_OWNER_SECINFO) && (sd.sd_owner == NULL)) ||
	    ((secinfo & SMB_GROUP_SECINFO) && (sd.sd_group == NULL))) {
		smb_errcode_seterror(NT_STATUS_INVALID_PARAMETER, 0, 0);
		return (SDRC_ERROR);
	}

	if (!smb_node_is_system(sr->fid_ofile->f_node))
		status = smb_sd_write(sr, &sd, secinfo);

	smb_sd_term(&sd);
	if (status != NT_STATUS_SUCCESS)
		return (SDRC_ERROR);

	return (SDRC_SUCCESS);
}

/*
 * smb_encode_sd
 *
 * Encodes given security descriptor in the reply buffer.
 */
static void
smb_encode_sd(struct smb_xa *xa, smb_sd_t *sd, uint32_t secinfo)
{
	uint32_t offset = SMB_SD_HDRSIZE;

	/* encode header */
	(void) smb_mbc_encodef(&xa->rep_data_mb, "b.w",
	    sd->sd_revision, sd->sd_control | SE_SELF_RELATIVE);

	/* owner offset */
	if (secinfo & SMB_OWNER_SECINFO) {
		ASSERT(sd->sd_owner);
		(void) smb_mbc_encodef(&xa->rep_data_mb, "l", offset);
		offset += smb_sid_len(sd->sd_owner);
	} else {
		(void) smb_mbc_encodef(&xa->rep_data_mb, "l", 0);
	}

	/* group offset */
	if (secinfo & SMB_GROUP_SECINFO) {
		ASSERT(sd->sd_group);
		(void) smb_mbc_encodef(&xa->rep_data_mb, "l", offset);
		offset += smb_sid_len(sd->sd_group);
	} else {
		(void) smb_mbc_encodef(&xa->rep_data_mb, "l", 0);
	}

	/* SACL offset */
	if ((secinfo & SMB_SACL_SECINFO) && (sd->sd_sacl)) {
		(void) smb_mbc_encodef(&xa->rep_data_mb, "l", offset);
		offset += smb_acl_len(sd->sd_sacl);
	} else {
		(void) smb_mbc_encodef(&xa->rep_data_mb, "l", 0);
	}

	/* DACL offset */
	if ((secinfo & SMB_DACL_SECINFO) && (sd->sd_dacl))
		(void) smb_mbc_encodef(&xa->rep_data_mb, "l", offset);
	else
		(void) smb_mbc_encodef(&xa->rep_data_mb, "l", 0);

	if (secinfo & SMB_OWNER_SECINFO)
		smb_encode_sid(xa, sd->sd_owner);

	if (secinfo & SMB_GROUP_SECINFO)
		smb_encode_sid(xa, sd->sd_group);

	if (secinfo & SMB_SACL_SECINFO)
		smb_encode_sacl(xa, sd->sd_sacl);

	if (secinfo & SMB_DACL_SECINFO)
		smb_encode_dacl(xa, sd->sd_dacl);
}

/*
 * smb_encode_sid
 *
 * Encodes given SID in the reply buffer.
 */
void
smb_encode_sid(struct smb_xa *xa, smb_sid_t *sid)
{
	int i;

	(void) smb_mbc_encodef(&xa->rep_data_mb, "bb",
	    sid->sid_revision, sid->sid_subauthcnt);

	for (i = 0; i < NT_SID_AUTH_MAX; i++) {
		(void) smb_mbc_encodef(&xa->rep_data_mb, "b",
		    sid->sid_authority[i]);
	}

	for (i = 0; i < sid->sid_subauthcnt; i++) {
		(void) smb_mbc_encodef(&xa->rep_data_mb, "l",
		    sid->sid_subauth[i]);
	}
}

/*
 * smb_encode_sacl
 *
 * Encodes given SACL in the reply buffer.
 */
static void
smb_encode_sacl(struct smb_xa *xa, smb_acl_t *acl)
{
	smb_ace_t *ace;
	int i;

	if (acl == NULL)
		return;

	/* encode header */
	(void) smb_mbc_encodef(&xa->rep_data_mb, "b.ww2.", acl->sl_revision,
	    acl->sl_bsize, acl->sl_acecnt);

	for (i = 0, ace = acl->sl_aces; i < acl->sl_acecnt; i++, ace++) {
		(void) smb_mbc_encodef(&xa->rep_data_mb, "bbwl",
		    ace->se_hdr.se_type, ace->se_hdr.se_flags,
		    ace->se_hdr.se_bsize, ace->se_mask);

		smb_encode_sid(xa, ace->se_sid);
	}
}

/*
 * smb_encode_dacl
 *
 * Encodes given DACL in the reply buffer.
 */
static void
smb_encode_dacl(struct smb_xa *xa, smb_acl_t *acl)
{
	smb_ace_t *ace;

	if (acl == NULL)
		return;

	/* encode header */
	(void) smb_mbc_encodef(&xa->rep_data_mb, "b.ww2.", acl->sl_revision,
	    acl->sl_bsize, acl->sl_acecnt);

	ace = list_head(&acl->sl_sorted);
	while (ace) {
		(void) smb_mbc_encodef(&xa->rep_data_mb, "bbwl",
		    ace->se_hdr.se_type, ace->se_hdr.se_flags,
		    ace->se_hdr.se_bsize, ace->se_mask);

		smb_encode_sid(xa, ace->se_sid);
		ace = list_next(&acl->sl_sorted, ace);
	}
}

/*
 * smb_decode_sd
 *
 * Decodes the security descriptor in the request buffer
 * and set the fields of 'sd' appropraitely. Upon successful
 * return, caller must free allocated memories by calling
 * smb_sd_term().
 */
uint32_t
smb_decode_sd(struct smb_xa *xa, smb_sd_t *sd)
{
	struct mbuf_chain sdbuf;
	uint32_t owner_offs;
	uint32_t group_offs;
	uint32_t sacl_offs;
	uint32_t dacl_offs;

	smb_sd_init(sd, SECURITY_DESCRIPTOR_REVISION);

	(void) MBC_SHADOW_CHAIN(&sdbuf, &xa->req_data_mb,
	    xa->req_data_mb.chain_offset,
	    xa->req_data_mb.max_bytes - xa->req_data_mb.chain_offset);

	if (smb_mbc_decodef(&sdbuf, "b.wllll",
	    &sd->sd_revision, &sd->sd_control,
	    &owner_offs, &group_offs, &sacl_offs, &dacl_offs))
		goto decode_error;

	sd->sd_control &= ~SE_SELF_RELATIVE;

	if (owner_offs != 0) {
		if (owner_offs < SMB_SD_HDRSIZE)
			goto decode_error;

		sd->sd_owner = smb_decode_sid(xa, owner_offs);
		if (sd->sd_owner == NULL)
			goto decode_error;
	}

	if (group_offs != 0) {
		if (group_offs < SMB_SD_HDRSIZE)
			goto decode_error;

		sd->sd_group = smb_decode_sid(xa, group_offs);
		if (sd->sd_group == NULL)
			goto decode_error;
	}

	if (sacl_offs != 0) {
		if ((sd->sd_control & SE_SACL_PRESENT) == 0)
			goto decode_error;

		if (sacl_offs < SMB_SD_HDRSIZE)
			goto decode_error;

		sd->sd_sacl = smb_decode_acl(xa, sacl_offs);
		if (sd->sd_sacl == NULL)
			goto decode_error;
	}

	if (dacl_offs != 0) {
		if ((sd->sd_control & SE_DACL_PRESENT) == 0)
			goto decode_error;

		if (dacl_offs < SMB_SD_HDRSIZE)
			goto decode_error;

		sd->sd_dacl = smb_decode_acl(xa, dacl_offs);
		if (sd->sd_dacl == NULL)
			goto decode_error;
	}

	return (NT_STATUS_SUCCESS);

decode_error:
	smb_sd_term(sd);
	return (NT_STATUS_INVALID_SECURITY_DESCR);
}

/*
 * smb_decode_sid
 *
 * Allocates memory and decodes the SID in the request buffer
 * Upon successful return, caller must free the allocated memory
 * by calling smb_sid_free()
 */
smb_sid_t *
smb_decode_sid(struct smb_xa *xa, uint32_t offset)
{
	uint8_t revision;
	uint8_t subauth_cnt;
	struct mbuf_chain sidbuf;
	smb_sid_t *sid;
	int sidlen;
	int bytes_left;
	int i;

	offset += xa->req_data_mb.chain_offset;
	bytes_left = xa->req_data_mb.max_bytes - offset;
	if (bytes_left < sizeof (smb_sid_t))
		return (NULL);

	(void) MBC_SHADOW_CHAIN(&sidbuf, &xa->req_data_mb, offset, bytes_left);

	if (smb_mbc_decodef(&sidbuf, "bb", &revision, &subauth_cnt))
		return (NULL);

	sidlen = sizeof (smb_sid_t) - sizeof (uint32_t) +
	    (subauth_cnt * sizeof (uint32_t));
	sid = kmem_alloc(sidlen, KM_SLEEP);

	sid->sid_revision = revision;
	sid->sid_subauthcnt = subauth_cnt;

	for (i = 0; i < NT_SID_AUTH_MAX; i++) {
		if (smb_mbc_decodef(&sidbuf, "b", &sid->sid_authority[i]))
			goto decode_err;
	}

	for (i = 0; i < sid->sid_subauthcnt; i++) {
		if (smb_mbc_decodef(&sidbuf, "l", &sid->sid_subauth[i]))
			goto decode_err;
	}

	return (sid);

decode_err:
	kmem_free(sid, sidlen);
	return (NULL);
}

/*
 * smb_decode_acl
 *
 * Allocates memory and decodes the ACL in the request buffer
 * Upon successful return, caller must free the allocated memory
 * by calling smb_acl_free().
 */
static smb_acl_t *
smb_decode_acl(struct smb_xa *xa, uint32_t offset)
{
	struct mbuf_chain aclbuf;
	smb_acl_t *acl;
	smb_ace_t *ace;
	uint8_t revision;
	uint16_t size;
	uint16_t acecnt;
	int bytes_left;
	uint32_t sid_offs = offset;
	int sidlen;
	int i;

	offset += xa->req_data_mb.chain_offset;
	bytes_left = xa->req_data_mb.max_bytes - offset;
	if (bytes_left < SMB_ACL_HDRSIZE)
		return (NULL);

	(void) MBC_SHADOW_CHAIN(&aclbuf, &xa->req_data_mb, offset, bytes_left);

	if (smb_mbc_decodef(&aclbuf, "b.ww2.", &revision, &size, &acecnt))
		return (NULL);

	if (size == 0)
		return (NULL);

	acl = smb_acl_alloc(revision, size, acecnt);

	sid_offs += SMB_ACL_HDRSIZE;
	for (i = 0, ace = acl->sl_aces; i < acl->sl_acecnt; i++, ace++) {
		if (smb_mbc_decodef(&aclbuf, "bbwl",
		    &ace->se_hdr.se_type, &ace->se_hdr.se_flags,
		    &ace->se_hdr.se_bsize, &ace->se_mask))
			goto decode_error;

		sid_offs += SMB_ACE_HDRSIZE + sizeof (ace->se_mask);
		ace->se_sid = smb_decode_sid(xa, sid_offs);
		if (ace->se_sid == NULL)
			goto decode_error;
		/* This is SID length plus any paddings between ACEs */
		sidlen = ace->se_hdr.se_bsize -
		    (SMB_ACE_HDRSIZE + sizeof (ace->se_mask));
		aclbuf.chain_offset += sidlen;
		sid_offs += sidlen;
	}

	return (acl);

decode_error:
	smb_acl_free(acl);
	return (NULL);
}
