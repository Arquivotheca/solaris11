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
 * Copyright (c) 2001, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_IKED_READPS_H
#define	_IKED_READPS_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <netinet/in.h>
#include <net/pfkeyv2.h>

typedef struct preshared_entry_s {
	struct preshared_entry_s	*pe_next;
	int				pe_flds_mask;
	int				pe_locidtype;
	char				*pe_locid;
	int				pe_remidtype;
	char				*pe_remid;
	int				pe_ike_mode;
	uint8_t				*pe_keybuf;
	uint_t				pe_keybuf_bytes;
	uint_t				pe_keybuf_lbits;
	struct sockaddr_storage		pe_locid_sa;
	struct sockaddr_storage		pe_remid_sa;
	int				pe_locid_plen;
	int				pe_remid_plen;
} preshared_entry_t;

/*
 * Types of Fields
 * Note: used in pe_flds_mask; values bitwise distinct, not just unique
 */
#define	PS_FLD_COMMENT			0x01
#define	PS_FLD_LOCID			0x02
#define	PS_FLD_LOCID_TYPE		0x04
#define	PS_FLD_REMID			0x08
#define	PS_FLD_REMID_TYPE		0x10
#define	PS_FLD_IKE_MODE			0x20
#define	PS_FLD_KEY			0x40

/*
 * Type of Remote/Local Ids
 * Note: used in pe_locidtype and pe_remidtype fields.
 */
#define	PS_ID_IP			1
#define	PS_ID_IP4			2
#define	PS_ID_IP6			3
#define	PS_ID_SUBNET			4
#define	PS_ID_SUBNET4			5
#define	PS_ID_SUBNET6			6
#define	PS_ID_RANGE4			7
#define	PS_ID_RANGE6			8
#define	PS_ID_ASN1DN			9
#define	PS_ID_ASN1GN			10
#define	PS_ID_KEYID			11
#define	PS_ID_FQDN			12
#define	PS_ID_USER_FQDN			13
#define	PS_ID_RANGE			14


/*
 * Types of IKE Modes
 * Note: used in pe_ike_mode field.
 */
#define	PS_IKM_MAIN			1
#define	PS_IKM_AGGRESSIVE		2
#define	PS_IKM_BOTH			3

/*
 * Prefix length "special values"
 * Note: used in pe_locid_plen and pe_remid_plen fields
 */
#define	PS_PLEN_BAD_ADDR	-1	/* prefix is invalid */
#define	PS_PLEN_NO_PREFIX	-2	/* no prefix was found */

/*
 * Interface function prototypes
 *
 */

/*
 * char *preshared_load()
 *	args : char *ps_filename:	config file name
 *	args : int  fd:			config file descriptor; will
 *					be used if ps_filename is NULL.
 *	args : boolean_t replace:	true => replace existing list;
 *					false => append to existing list
 * Return value
 *	- NULL on success; pointer to error string on error.
 *
 *	Also on error, globals err_line_number/err_entry_number point
 *	to approximate location of error.
 */
extern char *preshared_load(const char *, int, boolean_t);

/*
 * Append the given preshared_entry_t to the global list
 */
extern boolean_t append_preshared_entry(preshared_entry_t *);

/*
 * psid2sadb(): convert PS_ID_* types to SADB_[X_]IDENTTYPE_* types
 */
extern int psid2sadb(int);

/*
 * Look up preshared entries by in_addr (IPv4)
 *	- first arg localid
 *	- second arg remoteid
 */
extern preshared_entry_t *
	lookup_ps_by_in_addr(struct in_addr *, struct in_addr *);

/*
 * Look up preshared entries by in_addr (IPv6)
 *	- first arg localid
 *	- second arg remoteid
 */
extern preshared_entry_t *
	lookup_ps_by_in6_addr(struct in6_addr *, struct in6_addr *);

/*
 * Look up preshared entries by identity
 *	- first arg localid
 *	- second arg remoteid
 */
extern preshared_entry_t *
	lookup_ps_by_ident(sadb_ident_t *, sadb_ident_t *);

/*
 * Look up the nth preshared entry in our list
 *	- first arg n
 */
extern preshared_entry_t *
	lookup_nth_ps(int);

/*
 * Delete an entry from the list of preshareds.
 *	- first arg points to the entry to be deleted
 *	- returns 1 if entry successfully deleted; 0 if entry not found
 */
extern int delete_ps(preshared_entry_t *);

/*
 * Write the preshared entries out to a specified file
 *	args : int  fd:			config file descriptor
 *	args : char **errmp:		errpr message string
 * Return value
 *	- if the list was written successfully, returns the number
 *	  of entries that were written; returns -1 on error.
 *	  Also on error, errmp contains pointer to error message
 *	  string and globals err_line_number/err_entry_number point
 *	  to approximate location of error.
 */
extern int write_preshared(int, char **);

#ifdef	__cplusplus
}
#endif

#endif	/* _IKED_READPS_H */
