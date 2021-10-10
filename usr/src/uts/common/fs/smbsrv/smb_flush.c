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
 * The flush SMB is sent to ensure all data and allocation information
 * for the corresponding file has been written to stable storage. This
 * is a synchronous request. The response should not be sent until the
 * writes are complete.
 *
 * The SmbFlush request is described in CIFS/1.0 1996 Section 3.9.14.
 *
 * CIFS/1.0 June 13, 1996
 * Heizer, et al
 * draft-heizer-cifs-v1-spec-00.txt
 */

#include <smbsrv/smb_kproto.h>
#include <smbsrv/smb_fsops.h>


static void smb_flush_file(struct smb_request *sr, struct smb_ofile *ofile);

/*
 * smb_com_flush
 *
 * Flush any cached data for a specified file, or for all files that
 * this client has open, to stable storage. If the fid is valid (i.e.
 * not 0xFFFF), we flush only that file. Otherwise we flush all files
 * associated with this client.
 *
 * We need to protect the list because there's a good chance we'll
 * block during the flush operation.
 */
smb_sdrc_t
smb_pre_flush(smb_request_t *sr)
{
	int rc;

	rc = smbsr_decode_vwv(sr, "w", &sr->smb_fid);

	DTRACE_SMB_1(op__Flush__start, smb_request_t *, sr);

	return ((rc == 0) ? SDRC_SUCCESS : SDRC_ERROR);
}

void
smb_post_flush(smb_request_t *sr)
{
	DTRACE_SMB_1(op__Flush__done, smb_request_t *, sr);
}

smb_sdrc_t
smb_com_flush(smb_request_t *sr)
{
	smb_ofile_t	*file;
	smb_llist_t	*flist;
	int		rc;

	if (smb_flush_required == 0) {
		rc = smbsr_encode_empty_result(sr);
		return ((rc == 0) ? SDRC_SUCCESS : SDRC_ERROR);
	}

	if (sr->smb_fid != 0xffff) {
		smbsr_lookup_file(sr);
		if (sr->fid_ofile == NULL) {
			smb_errcode_seterror(NT_STATUS_INVALID_HANDLE,
			    ERRDOS, ERRbadfid);
			return (SDRC_ERROR);
		}

		smb_flush_file(sr, sr->fid_ofile);
	} else {
		flist = &sr->tid_tree->t_ofile_list;
		smb_llist_enter(flist, RW_READER);
		file = smb_llist_head(flist);
		while (file) {
			mutex_enter(&file->f_mutex);
			smb_flush_file(sr, file);
			mutex_exit(&file->f_mutex);
			file = smb_llist_next(flist, file);
		}
		smb_llist_exit(flist);
	}

	rc = smbsr_encode_empty_result(sr);
	return ((rc == 0) ? SDRC_SUCCESS : SDRC_ERROR);
}


/*
 * smb_flush_file
 *
 * If writes on this file are not synchronous, flush it using the NFSv3
 * commit interface.
 */
static void
smb_flush_file(struct smb_request *sr, struct smb_ofile *ofile)
{
	sr->user_cr = smb_ofile_getcred(ofile);

	if ((ofile->f_node->flags & NODE_FLAGS_WRITE_THROUGH) == 0)
		(void) smb_fsop_commit(sr, sr->user_cr, ofile->f_node);
}
