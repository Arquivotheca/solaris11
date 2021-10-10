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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SMB_NTLMSSP_H
#define	_SMB_NTLMSSP_H

/*
 * NT LanMan Security Support Package (NTLMSSP)
 * Negotiation flags, etc.
 *
 * Reference: [MS-NLMP] NT LAN Manager (NTLM)
 *   Authentication Protocol Specification
 * http://msdn.microsoft.com/en-us/library/cc236621(PROT.10).aspx
 */

/*
 * NTLMSSP Negotiate Flags
 * [MS-NLMP] sec. 2.2.2.5
 */
#define	NTLMSSP_NEGOTIATE_UNICODE			0x00000001
#define	NTLMSSP_NEGOTIATE_OEM				0x00000002
#define	NTLMSSP_REQUEST_TARGET				0x00000004
/*	reserved 					0x00000008 */
#define	NTLMSSP_NEGOTIATE_SIGN				0x00000010
#define	NTLMSSP_NEGOTIATE_SEAL				0x00000020
#define	NTLMSSP_NEGOTIATE_DATAGRAM			0x00000040
#define	NTLMSSP_NEGOTIATE_LM_KEY			0x00000080
/*	reserved					0x00000100 */
#define	NTLMSSP_NEGOTIATE_NTLM				0x00000200
#define	NTLMSSP_NEGOTIATE_NT_ONLY			0x00000400
/*	old anonymous_session (ignored by servers)	0x00000800 */
#define	NTLMSSP_NEGOTIATE_OEM_DOMAIN_SUPPLIED		0x00001000
#define	NTLMSSP_NEGOTIATE_OEM_WORKSTATION_SUPPLIED	0x00002000
/*	reserved					0x00004000 */
#define	NTLMSSP_NEGOTIATE_ALWAYS_SIGN			0x00008000
#define	NTLMSSP_TARGET_TYPE_DOMAIN			0x00010000
#define	NTLMSSP_TARGET_TYPE_SERVER			0x00020000
#define	NTLMSSP_TARGET_TYPE_SHARE			0x00040000
#define	NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY	0x00080000
#define	NTLMSSP_NEGOTIATE_IDENTIFY			0x00100000
/*	reserved					0x00200000 */
#define	NTLMSSP_REQUEST_NON_NT_SESSION_KEY		0x00400000
#define	NTLMSSP_NEGOTIATE_TARGET_INFO			0x00800000
/*	reserved					0x01000000 */
#define	NTLMSSP_NEGOTIATE_VERSION			0x02000000
/*	reserved					0x04000000 */
/*	reserved					0x08000000 */
/*	reserved					0x10000000 */
#define	NTLMSSP_NEGOTIATE_128				0x20000000
#define	NTLMSSP_NEGOTIATE_KEY_EXCH			0x40000000
#define	NTLMSSP_NEGOTIATE_56				0x80000000

#endif /* _SMB_NTLMSSP_H */
