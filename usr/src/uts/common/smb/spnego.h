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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (C) 2002 Microsoft Corporation
 * All rights reserved.
 *
 * THIS CODE AND INFORMATION IS PROVIDED "AS IS"
 * WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED
 * OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE IMPLIED WARRANTIES OF MERCHANTIBILITY
 * AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _SMB_SPNEGO_H
#define	_SMB_SPNEGO_H

/*
 * Definitions required to create and interpret SPNEGO tokens
 * so that Kerberos GSS tokens can be packaged/unpackaged.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Users of SPNEGO Token Handler API will request these as well as free them.
 */
typedef void *SPNEGO_TOKEN_HANDLE;

/*
 * Defines the element types that are found in each of the tokens.
 */
typedef enum spnego_element_type {
	spnego_element_min,  /* Lower bound */

	spnego_init_mechtypes,	/* Init token elements */
	spnego_init_reqFlags,
	spnego_init_mechToken,
	spnego_init_mechListMIC,

	spnego_targ_negResult,	/* Targ token elements */
	spnego_targ_supportedMech,
	spnego_targ_responseToken,
	spnego_targ_mechListMIC,

	spnego_element_max	/* Upper bound */
} SPNEGO_ELEMENT_TYPE;

/*
 * Token Element Availability.  Elements in both token types are optional.
 * Since there are only 4 elements in each Token, we will allocate space
 * to hold the information, but we need a way to indicate whether or not
 * an element is available
 */
#define	SPNEGO_TOKEN_ELEMENT_UNAVAILABLE	0
#define	SPNEGO_TOKEN_ELEMENT_AVAILABLE		1

/*
 * Token type values.  SPNEGO has 2 token types:
 * NegTokenInit and NegTokenTarg
 */
#define	SPNEGO_TOKEN_INIT			0
#define	SPNEGO_TOKEN_TARG			1

/*
 * GSS Mechanism OID enumeration.  We only really handle 3 different OIDs.
 * These are stored in an array structure defined in the parsing code.
 */
typedef enum spnego_mech_oid {
	/* Init token elements */
	spnego_mech_oid_Kerberos_V5_Legacy, /* Really V5 but OID off by 1 bit */
	spnego_mech_oid_Kerberos_V5,
	spnego_mech_oid_Spnego,
	spnego_mech_oid_NTLMSSP,
	spnego_mech_oid_NotUsed = -1
} SPNEGO_MECH_OID;

/*
 * Defines the negResult values.
 */
typedef enum spnego_negResult {
	spnego_negresult_success,
	spnego_negresult_incomplete,
	spnego_negresult_rejected,
	spnego_negresult_NotUsed = -1
} SPNEGO_NEGRESULT;

/*
 * Context Flags in NegTokenInit
 *
 * ContextFlags values MUST be zero or a combination of the values below.
 * CONTEXT_MASK is a mask (combination of the flags) that can be used to
 * retrieve valid values.
 */
#define	SPNEGO_NEGINIT_CONTEXT_DELEG_FLAG	0x80
#define	SPNEGO_NEGINIT_CONTEXT_MUTUAL_FLAG	0x40
#define	SPNEGO_NEGINIT_CONTEXT_REPLAY_FLAG	0x20
#define	SPNEGO_NEGINIT_CONTEXT_SEQUENCE_FLAG	0x10
#define	SPNEGO_NEGINIT_CONTEXT_ANON_FLAG	0x8
#define	SPNEGO_NEGINIT_CONTEXT_CONF_FLAG	0x4
#define	SPNEGO_NEGINIT_CONTEXT_INTEG_FLAG	0x2
#define	SPNEGO_NEGINIT_CONTEXT_MASK		0xFE

/*
 * SPNEGO API return codes.
 */
#define	SPNEGO_E_SUCCESS			0
#define	SPNEGO_E_INVALID_TOKEN			-1
#define	SPNEGO_E_INVALID_LENGTH			-2
#define	SPNEGO_E_PARSE_FAILED			-3
#define	SPNEGO_E_NOT_FOUND			-4
#define	SPNEGO_E_ELEMENT_UNAVAILABLE		-5
#define	SPNEGO_E_OUT_OF_MEMORY			-6
#define	SPNEGO_E_NOT_IMPLEMENTED		-7
#define	SPNEGO_E_INVALID_PARAMETER		-8
#define	SPNEGO_E_UNEXPECTED_OID			-9
#define	SPNEGO_E_TOKEN_NOT_FOUND		-10
#define	SPNEGO_E_UNEXPECTED_TYPE		-11
#define	SPNEGO_E_BUFFER_TOO_SMALL		-12
#define	SPNEGO_E_INVALID_ELEMENT		-13

/* Frees opaque data */
void spnegoFreeData(SPNEGO_TOKEN_HANDLE);

/* Initializes SPNEGO_TOKEN structure from DER encoded binary data */
int spnegoInitFromBinary(unsigned char *, unsigned long, SPNEGO_TOKEN_HANDLE *);

/* Initialize SPNEGO_TOKEN for a NegTokenInit type */
int spnegoCreateNegTokenInit(SPNEGO_MECH_OID, unsigned char, unsigned char *,
    unsigned long, unsigned char *, unsigned long, SPNEGO_TOKEN_HANDLE *);

/* Initialize SPNEGO_TOKEN structure for a NegTokenTarg type */
int spnegoCreateNegTokenTarg(SPNEGO_MECH_OID, SPNEGO_NEGRESULT,
    unsigned char *, unsigned long, unsigned char *, unsigned long,
    SPNEGO_TOKEN_HANDLE *);

/* Copy binary representation of SPNEGO Data into user supplied buffer */
int spnegoTokenGetBinary(SPNEGO_TOKEN_HANDLE, unsigned char *, unsigned long *);

/* Returns SPNEGO Token Type */
int spnegoGetTokenType(SPNEGO_TOKEN_HANDLE, int *);

/* Returns the Initial Mech Type in the MechList element in the NegInitToken */
int spnegoIsMechTypeAvailable(SPNEGO_TOKEN_HANDLE, SPNEGO_MECH_OID, int *);

/*
 * Returns the value from the context flags element in the NegInitToken
 * as an unsigned long.
 */
int spnegoGetContextFlags(SPNEGO_TOKEN_HANDLE, unsigned char *);

/*
 * Reading a Response Token
 *
 * Returns the value from the negResult element
 * (Status code of GSS call - 0,1,2)
 */
int spnegoGetNegotiationResult(SPNEGO_TOKEN_HANDLE, SPNEGO_NEGRESULT *);

/* Returns the Supported Mech Type from the NegTokenTarg */
int spnegoGetSupportedMechType(SPNEGO_TOKEN_HANDLE, SPNEGO_MECH_OID *);

/*
 * Reading either Token Type
 *
 * Returns the actual Mechanism data from the token (this is what is passed
 * into GSS-API functions
 */
int spnegoGetMechToken(SPNEGO_TOKEN_HANDLE, unsigned char *, unsigned long *);

/* Returns the Message Integrity BLOB in the token */
int spnegoGetMechListMIC(SPNEGO_TOKEN_HANDLE, unsigned char *, unsigned long *);

#ifdef __cplusplus
}
#endif

#endif /* _SMB_SPNEGO_H */
