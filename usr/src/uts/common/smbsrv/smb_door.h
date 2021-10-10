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

#ifndef _SMBSRV_SMB_DOOR_H
#define	_SMBSRV_SMB_DOOR_H

#include <sys/door.h>
#include <sys/paths.h>
#include <smb/wintypes.h>
#include <smbsrv/smb_xdr.h>
#include <smbsrv/smb_token.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	SMBD_DOOR_NAME			_PATH_SYSVOL "/smbd_door"

#define	SMB_DOOR_CALL_RETRIES		3

/*
 * Opcodes for smbd door.
 *
 * SMB_DR_NULL is the equivalent of the NULL RPC.  It ensures that an
 * opcode of zero is not misinterpreted as an operational door call
 * and it is available as a test interface.
 *
 * SMB_DR_ASYNC_RESPONSE delivers the response part of an asynchronous
 * request and must be processed as a synchronous request.
 */
typedef enum smb_dopcode {
	SMB_DR_NULL = 0,
	SMB_DR_ASYNC_RESPONSE,
	SMB_DR_USER_AUTH,
	SMB_DR_USER_LOGOFF,
	SMB_DR_LOOKUP_SID,
	SMB_DR_LOOKUP_NAME,
	SMB_DR_LOCATE_DC,
	SMB_DR_JOIN,
	SMB_DR_GET_DCINFO,
	SMB_DR_GET_FQDN,
	SMB_DR_VSS_GET_COUNT,
	SMB_DR_VSS_GET_SNAPSHOTS,
	SMB_DR_VSS_MAP_GMTTOKEN,
	SMB_DR_ADS_FIND_HOST,
	SMB_DR_QUOTA_QUERY,
	SMB_DR_QUOTA_SET,
	SMB_DR_DFS_GET_REFERRALS,
	SMB_DR_SHR_HOSTACCESS,
	SMB_DR_SHR_EXEC,
	SMB_DR_SHR_NOTIFY,
	SMB_DR_SHR_PUBLISH_ADMIN,
	SMB_DR_SPOOLDOC,
	SMB_DR_SESSION_CREATE,
	SMB_DR_SESSION_DESTROY,
	SMB_DR_GET_DOMAINS_INFO
} smb_dopcode_t;

struct smb_event;

typedef struct smb_doorarg {
	smb_doorhdr_t		da_req_hdr;
	smb_doorhdr_t		da_rsp_hdr;
	door_arg_t		da_arg;
	xdrproc_t		da_req_xdr;
	xdrproc_t		da_rsp_xdr;
	void			*da_req_data;
	void			*da_rsp_data;
	smb_dopcode_t		da_opcode;
	const char		*da_opname;
	struct smb_event	*da_event;
	uint32_t		da_flags;
} smb_doorarg_t;

/*
 * Door call return codes.
 */
#define	SMB_DOP_SUCCESS			0
#define	SMB_DOP_NOT_CALLED		1
#define	SMB_DOP_DECODE_ERROR		2
#define	SMB_DOP_ENCODE_ERROR		3
#define	SMB_DOP_EMPTYBUF		4

#ifndef _KERNEL
char *smb_common_encode(void *, xdrproc_t, size_t *);
int smb_common_decode(char *, size_t, xdrproc_t, void *);
char *smb_string_encode(char *, size_t *);
int smb_string_decode(smb_string_t *, char *, size_t);
#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif	/* _SMBSRV_SMB_DOOR_H */
