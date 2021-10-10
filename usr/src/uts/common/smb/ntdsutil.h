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

#ifndef _SMB_NTDSUTIL_H
#define	_SMB_NTDSUTIL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Active Directory DC, domain and forest functional levels [MS-ADTS].
 */
#define	DS_BEHAVIOR_WIN2000			0
#define	DS_BEHAVIOR_WIN2003_WITH_MIXED_DOMAINS	1
#define	DS_BEHAVIOR_WIN2003			2
#define	DS_BEHAVIOR_WIN2008			3
#define	DS_BEHAVIOR_WIN2008R2			4

/*
 * rootDSE attributes indicating the functional level.
 * These attributes are not set on Windows 2000.
 */
#define	DS_ATTR_DCLEVEL				"domainControllerFunctionality"
#define	DS_ATTR_DOMAINLEVEL			"domainFunctionality"
#define	DS_ATTR_FORESTLEVEL			"forestFunctionality"

/*
 * Trusted Domain Object attributes
 *
 * msDs-supportedEncryptionTypes (Windows Server 2008 only)
 * This attribute defines the encryption types supported by the system.
 * Encryption Types:
 *  - DES cbc mode with CRC-32
 *  - DES cbc mode with RSA-MD5
 *  - ArcFour with HMAC/md5
 *  - AES-128
 *  - AES-256
 */
#define	DS_ATTR_ENCTYPES			"msDs-supportedEncryptionTypes"
#define	KERB_ENCTYPE_DES_CBC_CRC		0x00000001
#define	KERB_ENCTYPE_DES_CBC_MD5		0x00000002
#define	KERB_ENCTYPE_RC4_HMAC_MD5		0x00000004
#define	KERB_ENCTYPE_AES128_CTS_HMAC_SHA1_96	0x00000008
#define	KERB_ENCTYPE_AES256_CTS_HMAC_SHA1_96	0x00000010

/*
 * Does AD DS support AES?
 */
#define	DS_SUPPORT_AES(dclevel)		((dclevel) >= DS_BEHAVIOR_WIN2008)

#ifdef __cplusplus
}
#endif

#endif /* _SMB_NTDSUTIL_H */
