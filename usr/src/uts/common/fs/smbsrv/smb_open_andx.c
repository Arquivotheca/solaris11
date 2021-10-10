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
#include <smbsrv/smb_vops.h>

int smb_open_dsize_check = 0;

/*
 *  Client Request                     Description
 *  ================================== =================================
 *
 *  UCHAR WordCount;                   Count of parameter words = 15
 *  UCHAR AndXCommand;                 Secondary (X) command;  0xFF =
 *                                      none
 *  UCHAR AndXReserved;                Reserved (must be 0)
 *  USHORT AndXOffset;                 Offset to next command WordCount
 *  USHORT Flags;                      Additional information: bit set-
 *                                      0 - return additional info
 *                                      1 - exclusive oplock requested
 *                                      2 - batch oplock requested
 *  USHORT DesiredAccess;              File open mode
 *  USHORT SearchAttributes;
 *  USHORT FileAttributes;
 *  UTIME CreationTime;                Creation timestamp for file if it
 *                                      gets created
 *  USHORT OpenFunction;               Action to take if file exists
 *  ULONG AllocationSize;              Bytes to reserve on create or
 *                                      truncate
 *  ULONG Reserved[2];                 Must be 0
 *  USHORT ByteCount;                  Count of data bytes;    min = 1
 *  UCHAR BufferFormat                 0x04
 *  STRING FileName;
 *
 *  Server Response                    Description
 *  ================================== =================================
 *
 *  UCHAR WordCount;                   Count of parameter words = 15
 *  UCHAR AndXCommand;                 Secondary (X) command;  0xFF =
 *                                      none
 *  UCHAR AndXReserved;                Reserved (must be 0)
 *  USHORT AndXOffset;                 Offset to next command WordCount
 *  USHORT Fid;                        File handle
 *  USHORT FileAttributes;
 *  UTIME LastWriteTime;
 *  ULONG DataSize;                    Current file size
 *  USHORT GrantedAccess;              Access permissions actually
 *                                      allowed
 *  USHORT FileType;                   Type of file opened
 *  USHORT DeviceState;                State of the named pipe
 *  USHORT Action;                     Action taken
 *  ULONG ServerFid;                   Server unique file id
 *  USHORT Reserved;                   Reserved (must be 0)
 *  USHORT ByteCount;                  Count of data bytes = 0
 *
 * DesiredAccess describes the access the client desires for the file (see
 * section 3.6 -  Access Mode Encoding).
 *
 * OpenFunction specifies the action to be taken depending on whether or
 * not the file exists (see section 3.8 -  Open Function Encoding).  Action
 *
 * in the response specifies the action as a result of the Open request
 * (see section 3.9 -  Open Action Encoding).
 *
 * SearchAttributes indicates the attributes that the file must have to be
 * found while searching to see if it exists.  The encoding of this field
 * is described in the "File Attribute Encoding" section elsewhere in this
 * document.  If SearchAttributes is zero then only normal files are
 * returned.  If the system file, hidden or directory attributes are
 * specified then the search is inclusive -- both the specified type(s) of
 * files and normal files are returned.
 *
 * FileType returns the kind of resource actually opened:
 *
 *  Name                       Value  Description
 *  ========================== ====== ==================================
 *
 *  FileTypeDisk               0      Disk file or directory as defined
 *                                     in the attribute field
 *  FileTypeByteModePipe       1      Named pipe in byte mode
 *  FileTypeMessageModePipe    2      Named pipe in message mode
 *  FileTypePrinter            3      Spooled printer
 *  FileTypeUnknown            0xFFFF Unrecognized resource type
 *
 * If bit0 of Flags is clear, the FileAttributes, LastWriteTime, DataSize,
 * FileType, and DeviceState have indeterminate values in the response.
 *
 * This SMB can request an oplock on the opened file.  Oplocks are fully
 * described in the "Oplocks" section elsewhere in this document, and there
 * is also discussion of oplocks in the SMB_COM_LOCKING_ANDX SMB
 * description.  Bit1 and bit2 of the Flags field are used to request
 * oplocks during open.
 *
 * The following SMBs may follow SMB_COM_OPEN_ANDX:
 *
 *    SMB_COM_READ    SMB_COM_READ_ANDX
 *    SMB_COM_IOCTL
 */

/*
 * This message is sent to obtain a file handle for a data file.  This
 * returned Fid is used in subsequent client requests such as read, write,
 * close, etc.
 *
 * Client Request                     Description
 * ================================== =================================
 *
 * UCHAR WordCount;                   Count of parameter words = 2
 * USHORT DesiredAccess;              Mode - read/write/share
 * USHORT SearchAttributes;
 * USHORT ByteCount;                  Count of data bytes;    min = 2
 * UCHAR BufferFormat;                0x04
 * STRING FileName[];                 File name
 *
 * FileName is the fully qualified file name, relative to the root of the
 * share specified in the Tid field of the SMB header.  If Tid in the SMB
 * header refers to a print share, this SMB creates a new file which will
 * be spooled to the printer when closed.  In this case, FileName is
 * ignored.
 *
 * SearchAttributes specifies the type of file desired.  The encoding is
 * described in the "File Attribute Encoding" section.
 *
 * DesiredAccess controls the mode under which the file is opened, and the
 * file will be opened only if the client has the appropriate permissions.
 * The encoding of DesiredAccess is discussed in the section entitled
 * "Access Mode Encoding".
 *
 * Server Response                    Description
 * ================================== =================================
 *
 * UCHAR WordCount;                   Count of parameter words = 7
 * USHORT Fid;                        File handle
 * USHORT FileAttributes;             Attributes of opened file
 * UTIME LastWriteTime;               Time file was last written
 * ULONG DataSize;                    File size
 * USHORT GrantedAccess;              Access allowed
 * USHORT ByteCount;                  Count of data bytes = 0
 *
 * Fid is the handle value which should be used for subsequent file
 * operations.
 *
 * FileAttributes specifies the type of file obtained.  The encoding is
 * described in the "File Attribute Encoding" section.
 *
 * GrantedAccess indicates the access permissions actually allowed, and may
 * have one of the following values:
 *
 *    0  read-only
 *    1  write-only
 *    2 read/write
 *
 * File Handles (Fids) are scoped per client.  A Pid may reference any Fid
 * established by itself or any other Pid on the client (so far as the
 * server is concerned).  The actual accesses allowed through the Fid
 * depends on the open and deny modes specified when the file was opened
 * (see below).
 *
 * The MS-DOS compatibility mode of file open provides exclusion at the
 * client level.  A file open in compatibility mode may be opened (also in
 * compatibility mode) any number of times for any combination of reading
 * and writing (subject to the user's permissions) by any Pid on the same
 * client.  If the first client has the file open for writing, then the
 * file may not be opened in any way by any other client.  If the first
 * client has the file open only for reading, then other clients may open
 * the file, in compatibility mode, for reading..  The above
 * notwithstanding, if the filename has an extension of .EXE, .DLL, .SYM,
 * or .COM other clients are permitted to open the file regardless of
 * read/write open modes of other compatibility mode opens.  However, once
 * multiple clients have the file open for reading, no client is permitted
 * to open the file for writing and no other client may open the file in
 * any mode other than compatibility mode.
 *
 * The other file exclusion modes (Deny read/write, Deny write, Deny read,
 * Deny none) provide exclusion at the file level.  A file opened in any
 * "Deny" mode may be opened again only for the accesses allowed by the
 * Deny mode (subject to the user's permissions).  This is true regardless
 * of the identity of the second opener -a different client, a Pid from the
 * same client, or the Pid that already has the file open.  For example, if
 * a file is open in "Deny write" mode a second open may only obtain read
 * permission to the file.
 *
 * Although Fids are available to all Pids on a client, Pids other than the
 * owner may not have the full access rights specified in the open mode by
 * the Fid's creator.  If the open creating the Fid specified a deny mode,
 * then any Pid using the Fid, other than the creating Pid, will have only
 * those access rights determined by "anding" the open mode rights and the
 * deny mode rights, i.e., the deny mode is checked on all file accesses.
 * For example, if a file is opened for Read/Write in Deny write mode, then
 * other clients may only read the file and cannot write; if a file is
 * opened for Read in Deny read mode, then the other clients can neither
 * read nor write the file.
 */

smb_sdrc_t
smb_pre_open(smb_request_t *sr)
{
	struct open_param *op = &sr->arg.open;
	int rc;

	bzero(op, sizeof (sr->arg.open));

	rc = smbsr_decode_vwv(sr, "ww", &op->omode, &op->fqi.fq_sattr);
	if (rc == 0)
		rc = smbsr_decode_data(sr, "%S", sr, &op->fqi.fq_path.pn_path);

	DTRACE_SMB_2(op__Open__start, smb_request_t *, sr,
	    struct open_param *, op);

	return ((rc == 0) ? SDRC_SUCCESS : SDRC_ERROR);
}

void
smb_post_open(smb_request_t *sr)
{
	DTRACE_SMB_1(op__Open__done, smb_request_t *, sr);
}

smb_sdrc_t
smb_com_open(smb_request_t *sr)
{
	struct open_param *op = &sr->arg.open;
	smb_node_t *node;
	smb_attr_t attr;
	uint16_t file_attr;
	int rc;

	op->desired_access = smb_omode_to_amask(op->omode);
	op->share_access = smb_denymode_to_sharemode(op->omode,
	    op->fqi.fq_path.pn_path);
	op->crtime.tv_sec = op->crtime.tv_nsec = 0;
	op->create_disposition = FILE_OPEN;
	op->create_options = FILE_NON_DIRECTORY_FILE;
	if (op->omode & SMB_DA_WRITE_THROUGH)
		op->create_options |= FILE_WRITE_THROUGH;

	if (sr->sr_flags & SMB_FLAGS_OPLOCK) {
		if (sr->sr_flags & SMB_FLAGS_OPLOCK_NOTIFY_ANY)
			op->op_oplock_level = SMB_OPLOCK_BATCH;
		else
			op->op_oplock_level = SMB_OPLOCK_EXCLUSIVE;
	} else {
		op->op_oplock_level = SMB_OPLOCK_NONE;
	}
	op->op_oplock_levelII = B_FALSE;

	if (smb_common_open(sr) != NT_STATUS_SUCCESS)
		return (SDRC_ERROR);

	if (op->op_oplock_level == SMB_OPLOCK_NONE) {
		sr->sr_flags &=
		    ~(SMB_FLAGS_OPLOCK | SMB_FLAGS_OPLOCK_NOTIFY_ANY);
	}

	if (smb_open_dsize_check && op->dsize > UINT_MAX) {
		smb_errcode_seterror(0, ERRDOS, ERRbadaccess);
		return (SDRC_ERROR);
	}

	file_attr = op->dattr  & FILE_ATTRIBUTE_MASK;
	node = sr->fid_ofile->f_node;
	if (smb_node_getattr(sr, node, &attr) != 0) {
		smb_errcode_seterror(NT_STATUS_INTERNAL_ERROR,
		    ERRDOS, ERROR_INTERNAL_ERROR);
		return (SDRC_ERROR);
	}

	rc = smbsr_encode_result(sr, 7, 0, "bwwllww",
	    7,
	    sr->smb_fid,
	    file_attr,
	    smb_time_gmt_to_local(sr, attr.sa_vattr.va_mtime.tv_sec),
	    (uint32_t)op->dsize,
	    op->omode,
	    (uint16_t)0);	/* bcc */

	return ((rc == 0) ? SDRC_SUCCESS : SDRC_ERROR);
}

/*
 * smb_pre_open_andx
 * For compatibility with windows servers, the search attributes
 * specified in the request are ignored.
 *
 * Observed Windows behavior:
 * When a sharing mode is specified (i.e. not compatibility mode),
 * and the ofun specified is SMB_OFUN_OPEN_FAIL, the file is created
 * if it does not exist, regardless of whether SMB_OFUN_CREATE_CREATE
 * is specified.
 * For other ofun values, or in compatibility mode, the file will
 * only be created if SMB_OFUN_CREATE_CREATE is explicitly set in
 * the request.
 */
smb_sdrc_t
smb_pre_open_andx(smb_request_t *sr)
{
	struct open_param *op = &sr->arg.open;
	uint16_t flags;
	uint32_t creation_time;
	uint32_t open_action, share_mode;
	uint16_t file_attr, sattr;
	int rc;

	bzero(op, sizeof (sr->arg.open));

	rc = smbsr_decode_vwv(sr, "b.wwwwwlwll4.", &sr->andx_com,
	    &sr->andx_off, &flags, &op->omode, &sattr,
	    &file_attr, &creation_time, &op->ofun, &op->dsize, &op->timeo);

	if (rc == 0) {
		rc = smbsr_decode_data(sr, "%u", sr, &op->fqi.fq_path.pn_path);

		op->dattr = file_attr;

		if (flags & 2)
			op->op_oplock_level = SMB_OPLOCK_EXCLUSIVE;
		else if (flags & 4)
			op->op_oplock_level = SMB_OPLOCK_BATCH;
		else
			op->op_oplock_level = SMB_OPLOCK_NONE;

		if ((creation_time != 0) && (creation_time != UINT_MAX))
			op->crtime.tv_sec =
			    smb_time_local_to_gmt(sr, creation_time);
		op->crtime.tv_nsec = 0;

		/* implicit SMB_OFUN_CREATE_CREATE */
		open_action = op->ofun & SMB_OFUN_OPEN_MASK;
		share_mode = op->omode & SMB_DA_SHARE_MASK;
		if ((open_action == SMB_OFUN_OPEN_FAIL) &&
		    (share_mode != SMB_DA_SHARE_COMPATIBILITY)) {
			op->ofun |= SMB_OFUN_CREATE_CREATE;
		}

		op->create_disposition = smb_ofun_to_crdisposition(op->ofun);
	}

	DTRACE_SMB_2(op__OpenX__start, smb_request_t *, sr,
	    struct open_param *, op);

	return ((rc == 0) ? SDRC_SUCCESS : SDRC_ERROR);
}

void
smb_post_open_andx(smb_request_t *sr)
{
	DTRACE_SMB_1(op__OpenX__done, smb_request_t *, sr);
}

/*
 * smb_com_open_andx
 *
 * Observed Windows behavior:
 * If the file is successfully opened for EXECUTE, the granted
 * omode returned to the client is READ and subsequent attempts
 * to read the file via this fid (ofile) will succeed.
 *
 * The omode cannot be both READ and EXECUTE. Validate the open
 * for EXECUTE request and then explicitly add FILE_GENERIC_READ
 * to the ofile's granted_access and return an omode of READ.
 */
smb_sdrc_t
smb_com_open_andx(smb_request_t *sr)
{
	smb_arg_open_t	*op = &sr->arg.open;
	smb_node_t	*node;
	smb_ofile_t	*ofile;
	smb_attr_t	attr;
	int		rc;

	op->desired_access = smb_omode_to_amask(op->omode);
	op->share_access = smb_denymode_to_sharemode(op->omode,
	    op->fqi.fq_path.pn_path);

	if (op->create_disposition > FILE_MAXIMUM_DISPOSITION) {
		smb_errcode_seterror(0, ERRDOS, ERRbadaccess);
		return (SDRC_ERROR);
	}

	op->create_options = FILE_NON_DIRECTORY_FILE;
	if (op->omode & SMB_DA_WRITE_THROUGH)
		op->create_options |= FILE_WRITE_THROUGH;

	op->op_oplock_levelII = B_FALSE;

	if (smb_common_open(sr) != NT_STATUS_SUCCESS)
		return (SDRC_ERROR);

	if (smb_open_dsize_check && op->dsize > UINT_MAX) {
		smb_errcode_seterror(0, ERRDOS, ERRbadaccess);
		return (SDRC_ERROR);
	}

	if (op->op_oplock_level != SMB_OPLOCK_NONE)
		op->action_taken |= SMB_OACT_LOCK;
	else
		op->action_taken &= ~SMB_OACT_LOCK;

	ofile = sr->fid_ofile;
	switch (ofile->f_ftype) {
	case SMB_FTYPE_DISK:
	case SMB_FTYPE_PRINTER:
		node = ofile->f_node;
		if (smb_node_getattr(sr, node, &attr) != 0) {
			smb_errcode_seterror(NT_STATUS_INTERNAL_ERROR,
			    ERRDOS, ERROR_INTERNAL_ERROR);
			return (SDRC_ERROR);
		}
		break;

	case SMB_FTYPE_MESG_PIPE:
		bzero(&attr, sizeof (smb_attr_t));
		break;

	default:
		smb_errcode_seterror(NT_STATUS_INVALID_DEVICE_REQUEST,
		    ERRDOS, ERROR_INVALID_FUNCTION);
		return (SDRC_ERROR);
	}

	/* Open for execute grants read access */
	if ((op->omode & SMB_DA_ACCESS_MASK) == SMB_DA_ACCESS_EXECUTE) {
		op->omode = SMB_DA_ACCESS_READ;
		ofile->f_granted_access |= FILE_GENERIC_READ;
	}

	rc = smbsr_encode_result(sr, 15, 0,
	    "bb.wwwllwwwwl2.w",
	    15,
	    sr->andx_com,
	    VAR_BCC,
	    sr->smb_fid,
	    op->dattr & FILE_ATTRIBUTE_MASK,
	    smb_time_gmt_to_local(sr, attr.sa_vattr.va_mtime.tv_sec),
	    (uint32_t)op->dsize,
	    op->omode,
	    op->ftype,
	    op->devstate,
	    op->action_taken,
	    op->fileid,
	    0);

	return ((rc == 0) ? SDRC_SUCCESS : SDRC_ERROR);
}

smb_sdrc_t
smb_com_trans2_open2(smb_request_t *sr, smb_xa_t *xa)
{
	struct open_param *op = &sr->arg.open;
	uint32_t	creation_time;
	uint32_t	alloc_size;
	uint16_t	flags;
	uint16_t	file_attr;
	int		rc;

	bzero(op, sizeof (sr->arg.open));

	rc = smb_mbc_decodef(&xa->req_param_mb, "%wwwwlwl10.u",
	    sr, &flags, &op->omode, &op->fqi.fq_sattr, &file_attr,
	    &creation_time, &op->ofun, &alloc_size, &op->fqi.fq_path.pn_path);
	if (rc != 0)
		return (SDRC_ERROR);

	if ((creation_time != 0) && (creation_time != UINT_MAX))
		op->crtime.tv_sec = smb_time_local_to_gmt(sr, creation_time);
	op->crtime.tv_nsec = 0;

	op->dattr = file_attr;
	op->dsize = alloc_size;
	op->create_options = FILE_NON_DIRECTORY_FILE;

	op->desired_access = smb_omode_to_amask(op->omode);
	op->share_access = smb_denymode_to_sharemode(op->omode,
	    op->fqi.fq_path.pn_path);

	op->create_disposition = smb_ofun_to_crdisposition(op->ofun);
	if (op->create_disposition > FILE_MAXIMUM_DISPOSITION)
		op->create_disposition = FILE_CREATE;

	if (op->omode & SMB_DA_WRITE_THROUGH)
		op->create_options |= FILE_WRITE_THROUGH;

	if (sr->sr_flags & SMB_FLAGS_OPLOCK) {
		if (sr->sr_flags & SMB_FLAGS_OPLOCK_NOTIFY_ANY)
			op->op_oplock_level = SMB_OPLOCK_BATCH;
		else
			op->op_oplock_level = SMB_OPLOCK_EXCLUSIVE;
	} else {
		op->op_oplock_level = SMB_OPLOCK_NONE;
	}
	op->op_oplock_levelII = B_FALSE;

	if (smb_common_open(sr) != NT_STATUS_SUCCESS)
		return (SDRC_ERROR);

	if (op->op_oplock_level != SMB_OPLOCK_NONE)
		op->action_taken |= SMB_OACT_LOCK;
	else
		op->action_taken &= ~SMB_OACT_LOCK;

	if (STYPE_ISIPC(sr->tid_tree->t_res_type))
		op->dsize = 0;

	(void) smb_mbc_encodef(&xa->rep_param_mb, "wwllwwwwlwl",
	    sr->smb_fid,
	    op->dattr & FILE_ATTRIBUTE_MASK,
	    (uint32_t)0,	/* creation time */
	    (uint32_t)op->dsize,
	    op->omode,
	    op->ftype,
	    op->devstate,
	    op->action_taken,
	    op->fileid,
	    (uint16_t)0,	/* EA error offset */
	    (uint32_t)0);	/* EA list length */

	return (SDRC_SUCCESS);
}
