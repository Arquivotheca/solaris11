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

/*
 * This command is used to create or open a file or directory, when EAs
 * or an SD must be applied to the file. The functionality is similar
 * to SmbNtCreateAndx with the option to supply extended attributes or
 * a security descriptor.
 *
 * Note: we don't decode the extended attributes because we don't
 * support them at this time.
 */

#include <smbsrv/smb_kproto.h>
#include <smbsrv/smb_fsops.h>

/*
 * smb_nt_transact_create
 *
 * This command is used to create or open a file or directory, when EAs
 * or an SD must be applied to the file. The request parameter block
 * encoding, data block encoding and output parameter block encoding are
 * described in CIFS section 4.2.2.
 *
 * The format of the command is SmbNtTransact but it is basically the same
 * as SmbNtCreateAndx with the option to supply extended attributes or a
 * security descriptor. For information not defined in CIFS section 4.2.2
 * see section 4.2.1 (NT_CREATE_ANDX).
 */
smb_sdrc_t
smb_pre_nt_transact_create(smb_request_t *sr, smb_xa_t *xa)
{
	struct open_param *op = &sr->arg.open;
	uint8_t SecurityFlags;
	uint32_t EaLength;
	uint32_t ImpersonationLevel;
	uint32_t NameLength;
	uint32_t sd_len;
	uint32_t status;
	smb_sd_t sd;
	int rc;

	bzero(op, sizeof (sr->arg.open));

	rc = smb_mbc_decodef(&xa->req_param_mb, "%lllqllllllllb",
	    sr,
	    &op->nt_flags,
	    &op->rootdirfid,
	    &op->desired_access,
	    &op->dsize,
	    &op->dattr,
	    &op->share_access,
	    &op->create_disposition,
	    &op->create_options,
	    &sd_len,
	    &EaLength,
	    &NameLength,
	    &ImpersonationLevel,
	    &SecurityFlags);

	if (rc == 0) {
		if (NameLength == 0) {
			op->fqi.fq_path.pn_path = "\\";
		} else if (NameLength >= MAXPATHLEN) {
			smb_errcode_seterror(NT_STATUS_OBJECT_PATH_NOT_FOUND,
			    ERRDOS, ERROR_PATH_NOT_FOUND);
			rc = -1;
		} else {
			rc = smb_mbc_decodef(&xa->req_param_mb, "%#u",
			    sr, NameLength, &op->fqi.fq_path.pn_path);
		}
	}

	op->op_oplock_level = SMB_OPLOCK_NONE;
	if (op->nt_flags & NT_CREATE_FLAG_REQUEST_OPLOCK) {
		if (op->nt_flags & NT_CREATE_FLAG_REQUEST_OPBATCH)
			op->op_oplock_level = SMB_OPLOCK_BATCH;
		else
			op->op_oplock_level = SMB_OPLOCK_EXCLUSIVE;
	}

	if (sd_len) {
		status = smb_decode_sd(xa, &sd);
		if (status != NT_STATUS_SUCCESS) {
			smb_errcode_seterror(status, 0, 0);
			return (SDRC_ERROR);
		}
		op->sd = kmem_alloc(sizeof (smb_sd_t), KM_SLEEP);
		*op->sd = sd;
	} else {
		op->sd = NULL;
	}

	DTRACE_SMB_2(op__NtTransactCreate__start, smb_request_t *, sr,
	    struct open_param *, op);

	return ((rc == 0) ? SDRC_SUCCESS : SDRC_ERROR);
}

void
smb_post_nt_transact_create(smb_request_t *sr, smb_xa_t *xa)
{
	smb_sd_t *sd = sr->arg.open.sd;

	DTRACE_SMB_2(op__NtTransactCreate__done, smb_request_t *, sr,
	    smb_xa_t *, xa);

	if (sd) {
		smb_sd_term(sd);
		kmem_free(sd, sizeof (smb_sd_t));
	}

	if (sr->arg.open.dir != NULL)
		smb_ofile_release(sr->arg.open.dir);
}

smb_sdrc_t
smb_nt_transact_create(smb_request_t *sr, smb_xa_t *xa)
{
	smb_arg_open_t	*op = &sr->arg.open;
	uint8_t		dirflag = 0;
	smb_attr_t	attr;
	smb_node_t	*node;
	smb_ofile_t	*ofile;
	int		rc;

	if ((op->create_options & FILE_DELETE_ON_CLOSE) &&
	    !(op->desired_access & STANDARD_DELETE)) {
		smb_errcode_seterror(NT_STATUS_INVALID_PARAMETER,
		    ERRDOS, ERRbadaccess);
		return (SDRC_ERROR);
	}

	if (op->create_disposition > FILE_MAXIMUM_DISPOSITION) {
		smb_errcode_seterror(NT_STATUS_INVALID_PARAMETER,
		    ERRDOS, ERRbadaccess);
		return (SDRC_ERROR);
	}

	if (op->dattr & FILE_FLAG_WRITE_THROUGH)
		op->create_options |= FILE_WRITE_THROUGH;

	if (op->dattr & FILE_FLAG_DELETE_ON_CLOSE)
		op->create_options |= FILE_DELETE_ON_CLOSE;

	if (op->dattr & FILE_FLAG_BACKUP_SEMANTICS)
		op->create_options |= FILE_OPEN_FOR_BACKUP_INTENT;

	if (op->create_options & FILE_OPEN_FOR_BACKUP_INTENT)
		sr->user_cr = smb_user_getprivcred(sr->uid_user);

	if (op->rootdirfid == 0) {
		op->fqi.fq_dnode = sr->tid_tree->t_snode;
	} else {
		op->dir = smb_ofile_lookup_by_fid(sr->tid_tree,
		    (uint16_t)op->rootdirfid);
		if (op->dir == NULL) {
			smb_errcode_seterror(NT_STATUS_INVALID_HANDLE,
			    ERRDOS, ERRbadfid);
			return (SDRC_ERROR);
		}
		op->fqi.fq_dnode = op->dir->f_node;
	}

	op->op_oplock_levelII = B_TRUE;

	if (smb_common_open(sr) != NT_STATUS_SUCCESS)
		return (SDRC_ERROR);

	ofile = sr->fid_ofile;
	switch (ofile->f_ftype) {
	case SMB_FTYPE_DISK:
	case SMB_FTYPE_PRINTER:
		if (op->create_options & FILE_DELETE_ON_CLOSE)
			smb_ofile_set_delete_on_close(ofile);

		node = ofile->f_node;
		if (smb_node_is_dir(node))
			dirflag = 1;
		if (smb_node_getattr(sr, node, &attr) != 0) {
			smb_errcode_seterror(NT_STATUS_INTERNAL_ERROR,
			    ERRDOS, ERROR_INTERNAL_ERROR);
			return (SDRC_ERROR);
		}
		break;

	case SMB_FTYPE_MESG_PIPE:
		bzero(&attr, sizeof (smb_attr_t));
		attr.sa_allocsz = 0x1000LL;
		break;

	default:
		smb_errcode_seterror(NT_STATUS_INVALID_DEVICE_REQUEST,
		    ERRDOS, ERROR_INVALID_FUNCTION);
		return (SDRC_ERROR);
	}

	rc = smb_mbc_encodef(&xa->rep_param_mb, "b.wllTTTTlqqwwb",
	    op->op_oplock_level,
	    sr->smb_fid,
	    op->action_taken,
	    0,	/* EaErrorOffset */
	    &attr.sa_crtime,
	    &attr.sa_vattr.va_atime,
	    &attr.sa_vattr.va_mtime,
	    &attr.sa_vattr.va_ctime,
	    op->dattr & FILE_ATTRIBUTE_MASK,
	    attr.sa_allocsz,
	    attr.sa_vattr.va_size,
	    op->ftype,
	    op->devstate,
	    dirflag);

	return ((rc == 0) ? SDRC_SUCCESS : SDRC_ERROR);
}
