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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#pragma	D depends_on library ip.d
#pragma	D depends_on library net.d
#pragma	D depends_on module genunix
#pragma	D depends_on module smbsrv

typedef struct smbopinfo {
	cred_t		*soi_cred;	/* credentials for operation */
	string		soi_curpath;	/* current file handle path (if any) */
	uint64_t	soi_sid;
	uint32_t	soi_pid;
	uint32_t	soi_status;
	uint16_t	soi_tid;
	uint16_t	soi_uid;
	uint16_t	soi_mid;
	uint16_t	soi_flags2;
	uint8_t		soi_flags;
} smbopinfo_t;

typedef struct smbReadArgs {
	off_t		soa_offset;
	uint_t		soa_count;	
} smbReadArgs_t;

typedef struct smbWriteArgs {
	off_t		soa_offset;
	uint_t		soa_count;
} smbWriteArgs_t;

#pragma D binding "1.6" translator
translator conninfo_t < smb_request_t *P > {
	ci_protocol = P->session->ipaddr.a_family == AF_INET ? "ipv4" : "ipv6";
	ci_local = P->session->local_ipaddr.a_family == AF_INET ? 
	    inet_ntoa(&P->session->local_ipaddr.au_addr.au_ipv4) :
	    inet_ntoa6(&P->session->local_ipaddr.au_addr.au_ipv6);
	ci_remote = P->session->ipaddr.a_family == AF_INET ? 
	    inet_ntoa(&P->session->ipaddr.au_addr.au_ipv4) :
	    inet_ntoa6(&P->session->ipaddr.au_addr.au_ipv6);
};

#pragma D binding "1.6" translator
translator smbopinfo_t < smb_request_t *P > {
	soi_cred = P->user_cr;
	soi_curpath = (P->fid_ofile == NULL || P->fid_ofile->f_node == NULL ||
	    P->fid_ofile->f_node->vp->v_path == NULL) ? "<unknown>" :
	    P->fid_ofile->f_node->vp->v_path;
	soi_sid = P->session->s_kid;
	soi_pid = (P->sr_header.hd_pidhigh << 16) + P->sr_header.hd_pidlow;
	soi_status = P->sr_header.hd_status;
	soi_tid = P->sr_header.hd_tid;
	soi_uid = P->sr_header.hd_uid;
	soi_mid = P->sr_header.hd_mid;
	soi_flags2 = P->sr_header.hd_flags2;
	soi_flags= P->sr_header.hd_flags;
};

#pragma D binding "1.6" translator
translator smbReadArgs_t < smb_rw_param_t *P > {
	soa_offset = P->rw_offset;
	soa_count = P->rw_count;
};

#pragma D binding "1.6" translator
translator smbWriteArgs_t < smb_rw_param_t *P > {
	soa_offset = P->rw_offset;
	soa_count = P->rw_count;
};
