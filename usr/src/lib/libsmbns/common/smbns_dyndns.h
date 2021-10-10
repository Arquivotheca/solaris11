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

#ifndef _SMBSRV_DYNDNS_H
#define	_SMBSRV_DYNDNS_H

#include <smbsrv/libsmbns.h>

/*
 * Header section format:
 *
 * The header contains the following fields:
 *
 *                                     1  1  1  1  1  1
 *       0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
 *     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *     |                      ID                       |
 *     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *     |QR|   Opcode  |AA|TC|RD|RA|   Z    |   RCODE   |
 *     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *     |                    QDCOUNT                    |
 *     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *     |                    ANCOUNT                    |
 *     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *     |                    NSCOUNT                    |
 *     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *     |                    ARCOUNT                    |
 *     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * where:
 *
 * ID              A 16 bit identifier assigned by the program that
 *                 generates any kind of query.  This identifier is copied
 *                 the corresponding reply and can be used by the requester
 *                 to match up replies to outstanding queries.
 *
 * QR              A one bit field that specifies whether this message is a
 *                 query (0), or a response (1).
 *
 * OPCODE          A four bit field that specifies kind of query in this
 *                 message.  This value is set by the originator of a query
 *                 and copied into the response.  The values are:
 *
 *                 0               a standard query (QUERY)
 *
 *                 1               an inverse query (IQUERY)
 *
 *                 2               a server status request (STATUS)
 *
 *                 3-15            reserved for future use
 *
 * AA              Authoritative Answer - this bit is valid in responses,
 *                 and specifies that the responding name server is an
 *                 authority for the domain name in question section.
 *
 *                 Note that the contents of the answer section may have
 *                 multiple owner names because of aliases.  The AA bit
 *
 *                 corresponds to the name which matches the query name, or
 *                 the first owner name in the answer section.
 *
 * TC              TrunCation - specifies that this message was truncated
 *                 due to length greater than that permitted on the
 *                 transmission channel.
 *
 * RD              Recursion Desired - this bit may be set in a query and
 *                 is copied into the response.  If RD is set, it directs
 *                 the name server to pursue the query recursively.
 *                 Recursive query support is optional.
 *
 * RA              Recursion Available - this be is set or cleared in a
 *                 response, and denotes whether recursive query support is
 *                 available in the name server.
 *
 * Z               Reserved for future use.  Must be zero in all queries
 *                 and responses.
 *
 * RCODE           Response code - this 4 bit field is set as part of
 *                 responses.  The values have the following
 *                 interpretation:
 *
 *                 0               No error condition
 *
 *                 1               Format error - The name server was
 *                                 unable to interpret the query.
 *
 *                 2               Server failure - The name server was
 *                                 unable to process this query due to a
 *                                 problem with the name server.
 *
 *                 3               Name Error - Meaningful only for
 *                                 responses from an authoritative name
 *                                 server, this code signifies that the
 *                                 domain name referenced in the query does
 *                                 not exist.
 *
 *                 4               Not Implemented - The name server does
 *                                 not support the requested kind of query.
 *
 *                 5               Refused - The name server refuses to
 *                                 perform the specified operation for
 *                                 policy reasons.  For example, a name
 *                                 server may not wish to provide the
 *                                 information to the particular requester,
 *                                 or a name server may not wish to perform
 *                                 a particular operation (e.g., zone
 *
 *                                 transfer) for particular data.
 *
 *                 6-15            Reserved for future use.
 *
 * QDCOUNT         an unsigned 16 bit integer specifying the number of
 *                 entries in the question section.
 *
 * ANCOUNT         an unsigned 16 bit integer specifying the number of
 *                 resource records in the answer section.
 *
 * NSCOUNT         an unsigned 16 bit integer specifying the number of name
 *                 server resource records in the authority records
 *                 section.
 *
 * ARCOUNT         an unsigned 16 bit integer specifying the number of
 *                 resource records in the additional records section.
 */

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DNS Update semantics, per RFC 2136 section 2.5 */
typedef enum dyndns_update_op {
	DYNDNS_UPDATE_ADD = 1,		/* Add RRs to an RRset */
	DYNDNS_UPDATE_DEL_ALL = 2,	/* Delete an RRset */
	DYNDNS_UPDATE_DEL_CLEAR = 3,	/* Delete all RRsets from a name */
	DYNDNS_UPDATE_DEL_ONE = 4	/* Delete an RR from an RRset */
} dyndns_update_op_t;

typedef enum dyndns_zone_dir {
	DYNDNS_ZONE_REV = 0,		/* Update reverse lookup zone */
	DYNDNS_ZONE_FWD			/* Update forward lookup zone */
} dyndns_zone_dir_t;

typedef enum dyndns_check_opt {
	DYNDNS_CHECK_NONE = 0,		/* Don't check DNS for entry */
	DYNDNS_CHECK_EXIST		/* Check DNS for entry */
} dyndns_check_opt_t;

/* DNS TKEY modes, standard values, per RFC 2930 section 2.5 */
typedef enum dyndns_tkey_mode {
	DYNDNS_TKEY_MODE_SERVER = 1,	/* server assignment (optional) */
	DYNDNS_TKEY_MODE_DH = 2,	/* Diffie-Hellman exchange (required) */
	DYNDNS_TKEY_MODE_GSS = 3,	/* GSS-API negotiation (optional) */
	DYNDNS_TKEY_MODE_RESOLVER = 4,	/* resolver assignment (optional) */
	DYNDNS_TKEY_MODE_KEYDEL = 5	/* key deletion (required) */
} dyndns_tkey_mode_t;

/* DNS TKEY RDATA fields, per RFC 2930 section 2 */
typedef struct dyndns_tkey_rdata {
	const char	*tk_alg_name;
	uint32_t	tk_incept_time;
	uint32_t	tk_expire_time;
	uint16_t	tk_mode;
	uint16_t	tk_error;
	uint16_t	tk_key_size;
	const uchar_t	*tk_key_data;
	uint16_t	tk_other_size;
	const uchar_t	*tk_other_data;
} dyndns_tkey_rdata_t;

/* Fixed offsets in TKEY RDATA, after algorithm name, per RFC 2930 section 2 */
#define	DYNDNS_TKEY_OFFSET_INCEPTION	0
#define	DYNDNS_TKEY_OFFSET_EXPIRATION	4
#define	DYNDNS_TKEY_OFFSET_MODE		8
#define	DYNDNS_TKEY_OFFSET_ERROR	10
#define	DYNDNS_TKEY_OFFSET_KEYSIZE	12
#define	DYNDNS_TKEY_OFFSET_KEYDATA	14

/* DNS TSIG RDATA fields, per RFC 2845 section 2.3 */
typedef struct dyndns_tsig_rdata {
	const char	*ts_alg_name;
	uint64_t	ts_sign_time;	/* encoded as 48 bits on wire */
	uint16_t	ts_fudge_time;
	uint16_t	ts_mac_size;
	const uchar_t	*ts_mac_data;
	uint16_t	ts_orig_id;
	uint16_t	ts_error;
	uint16_t	ts_other_size;
	const uchar_t	*ts_other_data;
} dyndns_tsig_rdata_t;

typedef enum dyndns_digest_data {
	DYNDNS_DIGEST_UNSIGNED = 0,	/* digest includes unsigned data */
	DYNDNS_DIGEST_SIGNED		/* digest includes signed data */
} dyndns_digest_data_t;

/* Flags to indicate whether to attempt non-secure or secure updates */
#define	DYNDNS_SECURITY_NONE		0x00000001U
#define	DYNDNS_SECURITY_GSS		0x00000002U
#define	DYNDNS_SECURITY_ALL		0xffffffffU

int dyndns_update_nameaddr(dyndns_update_op_t, dyndns_zone_dir_t,
    const char *, int, const void *, uint32_t, dyndns_check_opt_t,
    const char *);

#ifdef __cplusplus
}
#endif

#endif /* _SMBSRV_DYNDNS_H */
