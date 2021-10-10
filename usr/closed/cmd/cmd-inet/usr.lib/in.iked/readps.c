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
 * Copyright (c) 2000, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>
#include <libintl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <net/pfkeyv2.h>

#include "readps.h"
#include <ipsec_util.h>

static FILE *fp;

#define	hd2num(hd) (((hd) >= '0' && (hd) <= '9') ? ((hd) - '0') : \
	(((hd) >= 'a' && (hd) <= 'f') ? ((hd) - 'a' + 10) : ((hd) - 'A' + 10)))

#define	ADDRBITS_V4	32	/* number of bits in IPv4 address */
#define	ADDRBITS_V6	128	/* number of bits in IPv6 address */


#define	CHR_CURLY_BRACE_OPEN		'{'
#define	CHR_CURLY_BRACE_CLOSE		'}'
#define	CHR_COMMENT_BEGIN		'#'
#define	CHR_COLON			':'
#define	CHR_SLASH			'/'
#define	CHR_BACKSLASH			'\\'
#define	CHR_DOUBLEQUOTE			'"'
#define	CHR_SPACE			' '
#define	CHR_NEWLINE			'\n'
#define	CHR_TAB				'\t'
#define	CHR_NULL			'\0'

#define	TRUE				1
#define	FALSE				0


/*
 * Error message strings
 */

static const char *err_arr [] = {
#define	ERR_LOCID_MISMATCH		0
	"Local Id value does not match the specified Localid type",
#define	ERR_REMID_MISMATCH		1
	"Remote Id does not match the remoteid type",
#define	ERR_MAINMODE_LOCID_NOTIP	2
	"Local Id can only be an IP address",
#define	ERR_MAINMODE_REMID_NOTIP	3
	"Remote Id can only be an IP address",
#define	ERR_ENTRY_NOBEGINBRACE		4
	"Syntax error - Entry does not start with an open brace",
#define	ERR_INVALID_FIELDNAME		5
	"Invalid attribute field name",
#define	ERR_REPEATED_FIELD		6
	"Field in an entry is repeated",
#define	ERR_INVALID_LOCALID_TYPE	7
	"Invalid Local Id type",
#define	ERR_MISSING_LOCALID		8
	"localid not defined",
#define	ERR_MISSING_REMID		9
	"remotedid not defined",
#define	ERR_INVALID_REMID_TYPE		10
	"Invalid Remote Id type",
#define	ERR_INVALID_IKE_MODE		11
	"Invalid Ike Mode",
#define	ERR_INVALID_BIT_SPECIFIER	12
	"Invalid Bit Specifier",
#define	ERR_BITLEN2BIG			13
	"Bit length too large",
#define	ERR_WARN_LOWBITS_TRUNC		14
	"Lower bits will be truncated",
#define	ERR_STRING_NOT_HEX		15
	"Invalid string format - hex string expected",
#define	ERR_BADADDR_PREFIXLEN_PART	16
	"Invalid prefix length format in address",
#define	ERR_BADADDR_SLASH_UNEXPECTED	17
	"Unxpected '/' in address string",
#define	ERR_BADADDR_TRY_AGAIN		18
	"Bad address string - try again later",
#define	ERR_BADADDR_ADDRESS		19
	"Bad address string",
#define	ERR_BADADDR_MISMATCH		20
	"Address versions do not match",
#define	ERR_BADADDR_MCAST    21
	"Multicast address not allowed",
#define	ERR_BADADDR_4MAPPED    22
	"V4 mapped in v6 address not allowed",
#define	ERR_BADADDR_4COMPAT    23
	"V4 compatible in v6 address not allowed",
#define	ERR_INVALID_ASCII_STRING	24
	"Syntax error in ASCII string or quotes",
#define	ERR_OUT_OF_MEMORY	25
	"Out of memory",
};


/*
 * Buffer to hold input line
 */
static char	linebuf[1024];


/*
 * Types of tokens
 */
#define	PS_TOK_BEGIN_CURLY_BRACE	1
#define	PS_TOK_END_CURLY_BRACE		2
#define	PS_TOK_FLD_TYPE			3
#define	PS_TOK_FLD_VALUE		4


/*
 * The prototype syntax of this file used, e.g. "localid" and "localidtype".
 * The aliases support transitioning to using same keywords as the ike/config
 * parser, e.g. "local_id" and "local_id_type".
 *
 * Also, {local,remote}_addr are aliases for {local,remote}_id for now,
 * eventually they should be handled differently.
 */
static const keywdtab_t fldstab[] = {
	{ PS_FLD_LOCID,		"localid" },
	{ PS_FLD_LOCID,		"local_id" },
	{ PS_FLD_LOCID,		"local_addr" },
	{ PS_FLD_LOCID_TYPE,	"localidtype" },
	{ PS_FLD_LOCID_TYPE,	"local_id_type" },
	{ PS_FLD_REMID,		"remoteid" },
	{ PS_FLD_REMID,		"remote_id" },
	{ PS_FLD_REMID,		"remote_addr" },
	{ PS_FLD_REMID_TYPE,	"remoteidtype" },
	{ PS_FLD_REMID_TYPE,	"remote_id_type" },
	{ PS_FLD_IKE_MODE,	"ike_mode" },
	{ PS_FLD_IKE_MODE,	"mode" },
	{ PS_FLD_KEY,		"key" },
};

static const keywdtab_t idstab[] = {
	{ PS_ID_IP,			"IP" },
	{ PS_ID_IP4,			"IPv4" },
	{ PS_ID_IP6,			"IPv6" },
	{ PS_ID_IP,			"IP_SUBNET" },
	{ PS_ID_IP4,			"IPv4_SUBNET" },
	{ PS_ID_IP6,			"IPv6_SUBNET" },
};

static const keywdtab_t ikmstab[] = {
	{ PS_IKM_MAIN,			"main" },
};

/*
 * Head and tail of linked list of entry data structures
 */
static preshared_entry_t *ps_head, *ps_tail;

/*
 * Global counters for use in reporting approximate error location
 * in config file
 */
static int err_line_number;
static int err_entry_number;

/*
 * Function prototypes
 */
static int getidtype(char *);
static int getfldtype(char *);
static int getikmtype(char *);
static int postprocess_entry(preshared_entry_t *, char **);
static char *get_next_token(char **);
static char *readnextline(FILE *);
static preshared_entry_t *getnextentry(FILE *, char **);
static uint8_t *parsekey(char *, uint_t *, uint_t *, char **);
/* Note: in_get{prefilen,addr}, in6_getaddr stolen from ifconfig.c */
static int in_getprefixlen(char *, boolean_t, int);
static int in_getaddr(char *, struct sockaddr_storage *, int *, char **);
static int in6_getaddr(char *, struct sockaddr_storage *, int *, char **);
static boolean_t check_if_v6(char *);


/*
 * Functions
 */

/*
 * Check for same preshared entry.
 */
static boolean_t
same_psent(preshared_entry_t *ptr, preshared_entry_t *ps)
{
	struct sockaddr_in	*ptr_v4, *ps_v4;
	struct sockaddr_in6	*ptr_v6, *ps_v6;

	/*
	 * Check for duplicate entries.  Preshared type
	 * of IPv4/IPv6 has a NULL locid/remid string and
	 * has the sockaddr_storages structures populated.
	 * All others store themselves in the aforementioned
	 * strings.
	 */

	/* Local and Remote ID types must match */
	if ((ptr->pe_locidtype != ps->pe_locidtype) ||
	    (ptr->pe_remidtype != ps->pe_remidtype))
		return (B_FALSE);

	/* Check local ids */
	switch (ptr->pe_locidtype) {
	case PS_ID_IP:
	case PS_ID_IP4:
	case PS_ID_SUBNET4:
		ptr_v4 = (struct sockaddr_in *)&ptr->pe_locid_sa;
		ps_v4 = (struct sockaddr_in *)&ps->pe_locid_sa;
		if ((uint32_t)ptr_v4->sin_addr.s_addr !=
		    (uint32_t)ps_v4->sin_addr.s_addr)
			return (B_FALSE);
		/*
		 * Prefix length either same number or same error
		 * PS_PLEN_NO_PREFIX for non subnet
		 */
		if (&ptr->pe_locid_plen != &ps->pe_locid_plen)
			return (B_FALSE);
		break;
	case PS_ID_IP6:
	case PS_ID_SUBNET6:
		ptr_v6 = (struct sockaddr_in6 *)&ptr->pe_locid_sa;
		ps_v6 = (struct sockaddr_in6 *)&ps->pe_locid_sa;
		if (!(IN6_ARE_ADDR_EQUAL(&ptr_v6->sin6_addr,
		    &ps_v6->sin6_addr)))
			return (B_FALSE);
		/*
		 * Prefix length either same number or same error
		 * PS_PLEN_NO_PREFIX for non subnet
		 */
		if (&ptr->pe_locid_plen != &ps->pe_locid_plen)
			return (B_FALSE);
		break;
	default:
		if (strcmp(ptr->pe_locid, ps->pe_locid) != 0)
				return (B_FALSE);
	}

	/* Check remote ids */
	switch (ptr->pe_remidtype) {
	case PS_ID_IP:
	case PS_ID_IP4:
	case PS_ID_SUBNET4:
		ptr_v4 = (struct sockaddr_in *)&ptr->pe_remid_sa;
		ps_v4 = (struct sockaddr_in *)&ps->pe_remid_sa;
		if ((uint32_t)ptr_v4->sin_addr.s_addr !=
		    (uint32_t)ps_v4->sin_addr.s_addr)
			return (B_FALSE);
		/*
		 * Prefix length either same number or same error
		 * PS_PLEN_NO_PREFIX for non subnet
		 */
		if (&ptr->pe_remid_plen != &ps->pe_remid_plen)
			return (B_FALSE);
		break;
	case PS_ID_IP6:
	case PS_ID_SUBNET6:
		ptr_v6 = (struct sockaddr_in6 *)&ptr->pe_remid_sa;
		ps_v6 = (struct sockaddr_in6 *)&ps->pe_remid_sa;
		if (!(IN6_ARE_ADDR_EQUAL(&ptr_v6->sin6_addr,
		    &ps_v6->sin6_addr)))
			return (B_FALSE);
		/*
		 * Prefix length either same number or same error
		 * PS_PLEN_NO_PREFIX for non subnet
		 */
		if (&ptr->pe_remid_plen != &ps->pe_remid_plen)
			return (B_FALSE);
		break;
	default:
		if (strcmp(ptr->pe_remid, ps->pe_remid) != 0)
				return (B_FALSE);
	}
	return (B_TRUE);
}

/*
 * Check for duplicate preshared entry.
 */
static boolean_t
has_dup(preshared_entry_t *ps, preshared_entry_t **head)
{
	preshared_entry_t *ptr;

	if (ps == NULL) /* no op */
		return (B_FALSE);

	ptr = *head;

	while (ptr != NULL) {
		if (same_psent(ptr, ps))
			return (B_TRUE);
		ptr = ptr->pe_next;
	}
	return (B_FALSE);
}

/*
 * Append entries to list.
 */
static boolean_t
append_to_list(preshared_entry_t *ps, preshared_entry_t **head,
    preshared_entry_t **tail)
{
	if (ps == NULL)		/* no op */
		return (B_TRUE);
	if (has_dup(ps, head)) {
		(void) fprintf(stderr,
		    gettext("Ignoring duplicate preshared entry.\n"));
		free(ps->pe_locid);
		free(ps->pe_remid);
		free(ps->pe_keybuf);
		free(ps);
		return (B_FALSE);
	}
	ps->pe_next = NULL;	/* will be last entry */
	if (*head == NULL) {
		/* list initialization  */
		*head = *tail = ps;
	} else {
		(*tail)->pe_next = ps;
		*tail = (*tail)->pe_next;
	}
	return (B_TRUE);
}

/*
 * Appends preshared entry to list
 */
boolean_t
append_preshared_entry(preshared_entry_t *ps)
{
	return (append_to_list(ps, &ps_head, &ps_tail));
}

/*
 * Frees preshared list
 */
static void
free_preshared_list(preshared_entry_t **head, preshared_entry_t **tail)
{
	preshared_entry_t	*ps;

	while (*head != NULL) {
		/* Zero the old list in case of reloading. */
		ps = *head;
		if (ps == *tail)
			*tail = NULL;
		*head = ps->pe_next;
		free(ps->pe_locid);
		free(ps->pe_remid);
		free(ps->pe_keybuf);
		free(ps);
	}
}

static char *
getidstr(int val)
{
	const keywdtab_t *idt;

	for (idt = idstab; idt < A_END(idstab); idt++) {
		if (val == idt->kw_tag)
			return (idt->kw_str);
	}
	return (NULL);		/* not found */
}

static int
getidtype(char *valp)
{
	const keywdtab_t *idt;

	for (idt = idstab; idt < A_END(idstab); idt++) {
		if (strcasecmp(valp, idt->kw_str) == 0)
			return (idt->kw_tag);
	}
	return (-1);		/* not found */
}

static int
getfldtype(char *valp)
{
	const keywdtab_t *fldt;

	for (fldt = fldstab; fldt < A_END(fldstab); fldt++) {
		if (strcasecmp(valp, fldt->kw_str) == 0)
			return (fldt->kw_tag);
	}
	return (-1);		/* not found */
}

static char *
getikmstr(int val)
{
	const keywdtab_t *idt;

	for (idt = ikmstab; idt < A_END(ikmstab); idt++) {
		if (val == idt->kw_tag)
			return (idt->kw_str);
	}
	return (NULL);		/* not found */
}

static int
getikmtype(char *valp)
{
	const keywdtab_t *ikmt;

	for (ikmt = ikmstab; ikmt < A_END(ikmstab); ikmt++) {
		if (strcasecmp(valp, ikmt->kw_str) == 0)
			return (ikmt->kw_tag);
	}
	return (-1);		/* not found */

}

/*
 * unlike the other get*str functions, this one mallocs the returned
 * string, so the caller will need to free it.
 */
char *
getkeystr(uint8_t *key, uint_t bytes, uint_t bits)
{
	uint8_t	*sp;
	char	*dp, *buf;
	uint_t	len;

	/* assume two digits per byte, and no more than 4 digits of bitlen */
	len = (bytes < 1) + 6;
	if ((buf = malloc(len)) == NULL)
		return (NULL);

	sp = key;
	dp = buf;
	while (bytes-- != 0) {
		(void) sprintf(dp, "%02x", *sp++);
		dp += 2;
	}
	(void) sprintf(dp, "/%d", bits);

	return (buf);
}

/*
 * Parsing for hex key values.
 * Return value:
 *	Pointer to allocated buffer containing the key
 *	Parameter, "len" contains length of key buffer on successful return
 *	Parameter, "lbits" contains length of key in bits in the key buffer.
 *	Note: originally taken from ipseckey.c and adapted for use here
 */
static uint8_t *
parsehexkey(char *input, uint_t *keybuflen, uint_t *lbits, char **errp)
{
	uint8_t *keyp, *keybufp;
	uint_t i, hexlen = 0, bits, alloclen;

	for (i = 0; input[i] != CHR_NULL && input[i] != CHR_SLASH; i++)
		hexlen++;

	if (input[i] == CHR_NULL) {
		bits = 0;
	} else {
		/* Have /nn. */
		input[i] = CHR_NULL;
		if (sscanf((input + i + 1), "%u", &bits) != 1) {
			*errp = (char *)err_arr[ERR_INVALID_BIT_SPECIFIER];
			return (NULL);
		}

		/* hexlen in nibbles */
		if (((bits + 3) >> 2) > hexlen) {
			*errp = (char *)err_arr[ERR_BITLEN2BIG];
			return (NULL);
		}

		/*
		 * Adjust hexlen down if user gave us too small of a bit
		 * count.
		 */
		if ((hexlen << 2) > bits + 3) {
			/*
			 * NOTE: Callers don't necessarily handle warnings
			 * a successful return.
			 */
			*errp = (char *)err_arr[ERR_WARN_LOWBITS_TRUNC];
			hexlen = (bits + 3) >> 2;
			input[hexlen] = CHR_NULL;
		}
	}

	/*
	 * Allocate.  Remember, hexlen is in nibbles.
	 */

	alloclen = (hexlen/2 + (hexlen & 0x1));
	keyp = malloc(alloclen);

	if (keyp == NULL) {
		*errp = strerror(errno);
		return (NULL);
	}

	keybufp = keyp;
	*keybuflen = alloclen;
	if (bits == 0)
		*lbits = (hexlen + (hexlen & 0x1)) << 2;
	else
		*lbits = bits;

	/*
	 * Read in nibbles.  Read in odd-numbered as shifted high.
	 * (e.g. 123 becomes 0x1230).
	 */

	for (i = 0; input[i] != CHR_NULL; i += 2) {
		boolean_t second = (input[i + 1] != CHR_NULL);

		if (!isxdigit(input[i]) ||
		    (!isxdigit(input[i + 1]) && second)) {
			free(keybufp); /* free allocated memory on error */
			*errp = (char *)err_arr[ERR_STRING_NOT_HEX];
			return (NULL);
		}
		*keyp = (hd2num(input[i]) << 4);
		if (second)
			*keyp |= hd2num(input[i + 1]);
		else
			break;	/* out of for loop. */
		keyp++;
	}

	/* bzero the remaining bits if we're a non-octet amount. */
	if (bits & 0x7)
		*((input[i] == CHR_NULL) ? keyp - 1 : keyp) &=
		    0xff << (8 - (bits & 0x7));
	return (keybufp);
}
/*
 * Parsing for ASCII key values.
 * Return value:
 *	Pointer to allocated buffer containing the key
 *	Parameter, "len" contains length of key buffer on successful return
 *	Parameter, "lbits" contains length of key in bits in the key buffer.
 */
static uint8_t *
parseasciikey(char *input, uint_t *keybuflen, uint_t *lbits, char **errp)
{
	uint8_t *keyp, *keybufp, *tmp;
	uint_t i, asciilen = 0, num_escapes = 0;

	/* Make sure the first and last characters are '"', unescaped */

	if (input[0] != CHR_DOUBLEQUOTE)
		goto invalid_ascii_string;

	/*
	 * Set pointer past first quote, then find the end,
	 * keeping track of trailing escape characters
	 */
	input++;

	for (i = 0; input[i] != CHR_NULL; i++) {
		switch (input[i]) {
			case CHR_BACKSLASH:
			{
				num_escapes++;
				break;
			}
			case CHR_DOUBLEQUOTE:
			{
				/*
				 * If the token ends next round, don't
				 * reset the escape count, we are done.
				 * Otherwise treat it normally.
				 */
				if (input [i + 1] != CHR_NULL)
					num_escapes = 0;
			}
			default:
			{
				num_escapes = 0;
			}
		}
		asciilen++;
	}
	/*
	 * Length without trailing \0 and leading quote lopped off
	 * from before must be at least 3 for a well formed key
	 */
	if (--asciilen < 3)
		goto invalid_ascii_string;
	/*
	 * Make sure the last quote exists and is not escaped,
	 * but also watch for valid "this string\\" syntax.
	 */
	if (input[asciilen] != CHR_DOUBLEQUOTE)
		goto invalid_ascii_string;

	/* Make sure we're not escaping the last double quote */
	if (num_escapes % 2 != 0)
		goto invalid_ascii_string;

	input[asciilen] = CHR_NULL;

	/* Now we have something well-formed in quotes and length set */

	/*
	 * Allocate.  If we have backslashes, we can shrink later
	 */

	keyp = malloc(asciilen);
	if (keyp == NULL) {
		*errp = strerror(errno);
		return (NULL);
	}

	keybufp = keyp;
	num_escapes = 0;
	/*
	 * Read in bytes, but be aware of next byte for escape purposes
	 */

	for (i = 0; input[i] != CHR_NULL; i++) {
		boolean_t second = (input[i + 1] != CHR_NULL);

		if (!isascii(input[i]) ||
		    (second && !isascii(input[i + 1]))) {
			goto free_key_buffer;
		}
		/*
		 * We have already sanitized out the first and
		 * last quote.  Don't allow unescaped quotes in the
		 * body of the key.
		 */
		if ((input[i] == CHR_DOUBLEQUOTE) && (num_escapes % 2 == 0))
			goto invalid_ascii_string;
		/*
		 * Consider backslash an escape character for
		 * itself and double quotes, but not for anything else
		 */
		if (second && (input[i] == CHR_BACKSLASH)) {
			num_escapes++;
			if ((num_escapes % 2 != 0) &&
			    (input[i + 1] == CHR_DOUBLEQUOTE ||
			    input[i + 1] == CHR_BACKSLASH)) {
				*keyp = input[i + 1];
				i++;
				keyp++;
				asciilen--;
				continue; /* for loop */
			}
		} else {
			num_escapes = 0;
		}
		*keyp = input[i];
		keyp++;
	}
	/* Shrink to fit */
	*keybuflen = asciilen;
	*lbits = asciilen << 3;
	tmp = realloc(keybufp, asciilen);

	if (tmp == NULL) {
		free(keybufp);
		*errp = (char *)err_arr[ERR_OUT_OF_MEMORY];
		return (NULL);
	}
	keybufp = tmp;
	return (keybufp);

free_key_buffer:
	free(keybufp);
invalid_ascii_string:
	*errp = (char *)err_arr[ERR_INVALID_ASCII_STRING];
	return (NULL);
}

static uint8_t *
parsekey(char *input, uint_t *keybuflen, uint_t *lbits, char **errp)
{
	/* Determine if this is an ASCII key or hex key */
	if (input[0] == CHR_DOUBLEQUOTE) {
		return (parseasciikey(input, keybuflen, lbits, errp));
	} else {
		return (parsehexkey(input, keybuflen, lbits, errp));
	}
}

static boolean_t
check_if_v6(char *abuf)
{
	char *cp;
	for (cp = abuf; *cp != CHR_NULL; cp++) {
		if (*cp == CHR_COLON)
			return (B_TRUE);
	}
	return (B_FALSE);
}

static boolean_t
valid_ip6_address(struct sockaddr_in6 *addr, char **errmsgp)
{

		/* is it mcast */
		if (IN6_IS_ADDR_MULTICAST(&addr->sin6_addr)) {
			*errmsgp = (char *)err_arr[ERR_BADADDR_MCAST];
			return (B_FALSE);
		}
		/* is it v4 mapped */
		if (IN6_IS_ADDR_V4MAPPED(&addr->sin6_addr)) {
			*errmsgp = (char *)err_arr[ERR_BADADDR_4MAPPED];
			return (B_FALSE);
		}
		/* is it v4 compat */
		if (IN6_IS_ADDR_V4COMPAT(&addr->sin6_addr)) {
			*errmsgp = (char *)err_arr[ERR_BADADDR_4COMPAT];
			return (B_FALSE);
		}

		return (B_TRUE);
}

/*
 * Post process preshared key entry.
 */
static int
postprocess_entry(preshared_entry_t *ps, char **errmsgp)
{
	/* ID types default to IP. */
	if ((ps->pe_flds_mask & PS_FLD_LOCID_TYPE) == 0) {
		ps->pe_flds_mask |= PS_FLD_LOCID_TYPE;
		ps->pe_locidtype = PS_ID_IP;
	}
	if ((ps->pe_flds_mask & PS_FLD_REMID_TYPE) == 0) {
		ps->pe_flds_mask |= PS_FLD_REMID_TYPE;
		ps->pe_remidtype = PS_ID_IP;
	}

	/*
	 * Verify that all mandatory fields are there.
	 * mandatory:  local_id and remote_id (or local_addr and remote_addr),
	 * mode, and key.
	 */

	/*
	 * Note: verify that semantic relationships among fields is fine
	 * Since we only support main mode, we force the mode to be main.
	 */

	ps->pe_ike_mode = PS_IKM_MAIN;
	if ((ps->pe_locidtype != PS_ID_IP) &&
	    (ps->pe_locidtype != PS_ID_IP4) &&
	    (ps->pe_locidtype != PS_ID_IP6)) {
		*errmsgp = (char *)err_arr[ERR_MAINMODE_LOCID_NOTIP];
		return (-1);
	}
	if ((ps->pe_remidtype != PS_ID_IP) &&
	    (ps->pe_remidtype != PS_ID_IP4) &&
	    (ps->pe_remidtype != PS_ID_IP6)) {
		*errmsgp = (char *)err_arr[ERR_MAINMODE_REMID_NOTIP];
		return (-1);
	}

	/*
	 * Parse "id" values now and verify that they match the
	 * "idtype" associated with them.
	 * NOTE: Real work is TBD
	 */
	if (ps->pe_flds_mask & PS_FLD_LOCID_TYPE) {
		int retval;

		if (ps->pe_locid == NULL) {
			*errmsgp = (char *)err_arr[ERR_MISSING_LOCALID];
			return (-1);
		}

		switch (ps->pe_locidtype) {
		case PS_ID_IP:
		{
			boolean_t isv6 = check_if_v6(ps->pe_locid);
			if (isv6) {
				ps->pe_locidtype = PS_ID_IP6;
				retval = in6_getaddr(ps->pe_locid,
				    &ps->pe_locid_sa, &ps->pe_locid_plen,
				    errmsgp);
			} else {
				ps->pe_locidtype = PS_ID_IP4;
				retval = in_getaddr(ps->pe_locid,
				    &ps->pe_locid_sa, &ps->pe_locid_plen,
				    errmsgp);
			}
			if (retval < 0)
				return (-1);
		}
		break;
		case PS_ID_IP4:
		case PS_ID_SUBNET4:
			retval = in_getaddr(ps->pe_locid, &ps->pe_locid_sa,
			    &ps->pe_locid_plen, errmsgp);
			if (retval < 0)
				return (-1);
			break;
		case PS_ID_IP6:
		case PS_ID_SUBNET6:
			retval = in6_getaddr(ps->pe_locid, &ps->pe_locid_sa,
			    &ps->pe_locid_plen, errmsgp);
			if (retval < 0)
				return (-1);
			break;
		case PS_ID_SUBNET:
		{
			boolean_t isv6 = check_if_v6(ps->pe_locid);
			if (isv6) {
				ps->pe_locidtype = PS_ID_SUBNET6;
				retval = in6_getaddr(ps->pe_locid,
				    &ps->pe_locid_sa, &ps->pe_locid_plen,
				    errmsgp);
			} else {
				ps->pe_locidtype = PS_ID_SUBNET4;
				retval = in_getaddr(ps->pe_locid,
				    &ps->pe_locid_sa, &ps->pe_locid_plen,
				    errmsgp);
			}
			if (retval < 0)
				return (-1);
		}
		break;
		case PS_ID_RANGE:
			break;
		case PS_ID_RANGE4:
			break;
		case PS_ID_RANGE6:
			break;
		case PS_ID_ASN1DN:
			break;
		case PS_ID_ASN1GN:
			break;
		case PS_ID_KEYID:
			break;
		case PS_ID_FQDN:
			break;
		case PS_ID_USER_FQDN:
			break;
		}
	}
	if (ps->pe_flds_mask & PS_FLD_REMID_TYPE) {
		int retval;

		if (ps->pe_remid == NULL) {
			*errmsgp = (char *)err_arr[ERR_MISSING_REMID];
			return (-1);
		}
		switch (ps->pe_remidtype) {
		case PS_ID_IP:
		{
			boolean_t isv6 = check_if_v6(ps->pe_remid);
			if (isv6) {
				ps->pe_remidtype = PS_ID_IP6;
				retval = in6_getaddr(ps->pe_remid,
				    &ps->pe_remid_sa, &ps->pe_remid_plen,
				    errmsgp);
			} else {
				ps->pe_remidtype = PS_ID_IP4;
				retval = in_getaddr(ps->pe_remid,
				    &ps->pe_remid_sa, &ps->pe_remid_plen,
				    errmsgp);
			}
			if (retval < 0)
				return (-1);
		}
		break;
		case PS_ID_IP4:
		case PS_ID_SUBNET4:
			retval = in_getaddr(ps->pe_remid, &ps->pe_remid_sa,
			    &ps->pe_remid_plen, errmsgp);
			if (retval < 0)
				return (-1);
			break;
		case PS_ID_IP6:
		case PS_ID_SUBNET6:
			retval = in6_getaddr(ps->pe_remid, &ps->pe_remid_sa,
			    &ps->pe_remid_plen, errmsgp);
			if (retval < 0)
				return (-1);
			break;
		case PS_ID_SUBNET:
		{
			boolean_t isv6 = check_if_v6(ps->pe_remid);
			if (isv6) {
				ps->pe_remidtype = PS_ID_SUBNET6;
				retval = in6_getaddr(ps->pe_remid,
				    &ps->pe_remid_sa, &ps->pe_remid_plen,
				    errmsgp);
			} else {
				ps->pe_remidtype = PS_ID_SUBNET4;
				retval = in_getaddr(ps->pe_remid,
				    &ps->pe_remid_sa, &ps->pe_remid_plen,
				    errmsgp);
			}
			if (retval < 0)
				return (-1);
		}
		break;
		case PS_ID_RANGE:
			break;
		case PS_ID_RANGE4:
			break;
		case PS_ID_RANGE6:
			break;
		case PS_ID_ASN1DN:
			break;
		case PS_ID_ASN1GN:
			break;
		case PS_ID_KEYID:
			break;
		case PS_ID_FQDN:
			break;
		case PS_ID_USER_FQDN:
			break;
		}
	}

	/*
	 * Reality check address families
	 * Remote and local address families must match
	 */
	if (check_if_v6(ps->pe_locid) != check_if_v6(ps->pe_remid)) {
		*errmsgp = (char *)err_arr[ERR_BADADDR_MISMATCH];
		return (-1);
	}

	/* v6 */
	if (ps->pe_remidtype == PS_ID_IP6 ||
	    ps->pe_remidtype == PS_ID_SUBNET6) {
		struct sockaddr_in6 *local =
		    (struct sockaddr_in6 *)(&ps->pe_locid_sa);
		struct sockaddr_in6 *remote =
		    (struct sockaddr_in6 *)(&ps->pe_remid_sa);

		if (!valid_ip6_address(local, errmsgp) ||
		    !valid_ip6_address(remote, errmsgp)) {
			return (-1);
		}
	} else {
		/* v4 */
		struct sockaddr_in *local =
		    (struct sockaddr_in *)(&ps->pe_locid_sa);
		struct sockaddr_in *remote =
		    (struct sockaddr_in *)(&ps->pe_remid_sa);

		/* are either mcast */
		if (IN_MULTICAST(ntohl(local->sin_addr.s_addr)) ||
		    IN_MULTICAST(ntohl(remote->sin_addr.s_addr))) {
			*errmsgp = (char *)err_arr[ERR_BADADDR_MCAST];
			return (-1);
		}
	}

	return (0);		/* OK return */
}

/*
 * Note: Code stolen from ifconfig.c and adapted.
 *
 * If "slash" is zero this parses the whole string as
 * an integer. With "slash" non zero it parses the tail part as an integer.
 *
 * If it is not a valid integer this returns BAD_ADDR.
 * If there is /<n> present this returns PS_PLEN_NO_PREFIX.
 */
static int
in_getprefixlen(char *addr, boolean_t slash, int max_plen)
{
	int prefixlen;
	char *str, *end;

	if (slash) {
		str = strchr(addr, CHR_SLASH);
		if (str == NULL)
			return (PS_PLEN_NO_PREFIX);
		str++;
	} else
		str = addr;

	prefixlen = strtol(str, &end, 10);
	if (prefixlen < 0)
		return (PS_PLEN_BAD_ADDR);
	if (str == end)
		return (PS_PLEN_BAD_ADDR);
	if (max_plen != 0 && max_plen < prefixlen)
		return (PS_PLEN_BAD_ADDR);
	return (prefixlen);
}


/*
 * Note: code stolen from ifconfig.c and adapted
 *
 * If the last argument is non-NULL allow a <addr>/<n> syntax and
 * pass out <n> in *plenp.
 * If <n> doesn't parse return PS_PLEN_BAD_ADDR as *plenp.
 * If no /<n> is present return PS_PLEN_NO_PREFIX as *plenp.
 */
static int
in_getaddr(char *s, struct sockaddr_storage *saddr, int *plenp, char **errp)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)saddr;
	struct hostent *hp;
	struct netent *np;
	char str[BUFSIZ];
	int error_num;

	(void) strncpy(str, s, sizeof (str));

	/*
	 * Look for '/'<n> (CHR_SLASH)is plenp
	 */
	if (plenp != NULL) {
		char *cp;

		*plenp = in_getprefixlen(str, B_TRUE, ADDRBITS_V4);
		if (*plenp == PS_PLEN_BAD_ADDR) {
			*errp = (char *)err_arr[ERR_BADADDR_PREFIXLEN_PART];
			return (-1);
		}
		cp = strchr(str, CHR_SLASH);
		if (cp != NULL)
			*cp = CHR_NULL;
	} else if (strchr(str, CHR_SLASH) != NULL) {
		*errp = (char *)err_arr[ERR_BADADDR_SLASH_UNEXPECTED];
		return (-1);
	}

	(void) memset(sin, 0, sizeof (*sin));

	/*
	 *	Try to catch attempts to set the broadcast address to all 1's.
	 */
	if (strcmp(str, "255.255.255.255") == 0 ||
	    (strtoul(str, (char **)NULL, 0) == 0xffffffffUL)) {
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = 0xffffffff;
		return (0);
	}

	hp = getipnodebyname(str, AF_INET, 0, &error_num);
	if (hp) {
		sin->sin_family = hp->h_addrtype;
		(void) memcpy(&sin->sin_addr, hp->h_addr, hp->h_length);
		freehostent(hp);
		return (0);
	}
	np = getnetbyname(str);
	if (np) {
		sin->sin_family = np->n_addrtype;
		sin->sin_addr = inet_makeaddr(np->n_net, INADDR_ANY);
		return (0);
	}
	if (error_num == TRY_AGAIN) {
		*errp = (char *)err_arr[ERR_BADADDR_TRY_AGAIN];
	} else {
		*errp = (char *)err_arr[ERR_BADADDR_ADDRESS];
	}
	return (-1);
}

/*
 * Note: Code stolen from ifconfig.c and adapted.
 *
 * If the third argument is non-NULL allow a <addr>/<n> syntax and
 * pass out <n> in *plenp.
 * If <n> doesn't parse return PS_PLEN_BAD_ADDR as *plenp.
 * If no /<n> is present return PS_PLEN_NO_PREFIX as *plenp.
 */
static int
in6_getaddr(char *s, struct sockaddr_storage *saddr, int *plenp, char **errp)
{
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)saddr;
	struct hostent *hp;
	char str[BUFSIZ];
	int error_num;

	(void) strncpy(str, s, sizeof (str));

	/*
	 * Look for '/'<n> (CHR_SLASH) is plenp
	 */
	if (plenp != NULL) {
		char *cp;

		*plenp = in_getprefixlen(str, B_TRUE, ADDRBITS_V6);
		if (*plenp == PS_PLEN_BAD_ADDR) {
			*errp = (char *)err_arr[ERR_BADADDR_PREFIXLEN_PART];
			return (-1);
		}
		cp = strchr(str, CHR_SLASH);
		if (cp != NULL)
			*cp = CHR_NULL;
	} else if (strchr(str, CHR_SLASH) != NULL) {
		*errp = (char *)err_arr[ERR_BADADDR_SLASH_UNEXPECTED];
		return (-1);
	}

	(void) memset(sin6, 0, sizeof (*sin6));

	hp = getipnodebyname(str, AF_INET6, 0, &error_num);
	if (hp) {
		sin6->sin6_family = hp->h_addrtype;
		(void) memcpy(&sin6->sin6_addr, hp->h_addr, hp->h_length);
		freehostent(hp);
		return (0);
	}
	if (error_num == TRY_AGAIN) {
		*errp = (char *)err_arr[ERR_BADADDR_TRY_AGAIN];
	} else {
		*errp = (char *)err_arr[ERR_BADADDR_ADDRESS];
	}
	return (-1);
}

/*
 * char *get_next_token(cpp)
 *	cpp - on input should point within a null terminated buffer.
 *	return value - on output will point to past spaces to first
 *		non-space character that starts a token
 *	cpp - on output will point past non-space token string pointed
 *		by return value.
 *	Side-effects: modifies a potential space character after token
 *		to null to form a null terminated token unless a
 *		double-quote has been seen in the token
 */
static char *
get_next_token(char **cpp)
{
	char *cp = *cpp;
	char *startcp, *endcp;
	boolean_t quote_seen = B_FALSE;
	int num_escapes = 0;

	/* skip leading whitespaces */
	while (*cp == CHR_SPACE || *cp == CHR_TAB || *cp == CHR_NEWLINE)
		cp++;

	if (*cp == CHR_NULL) {	/* end of string */
		*cpp = NULL;
		return (NULL);
	}

	startcp = endcp = cp;
	while (*endcp != CHR_NULL &&
	    ((*endcp != CHR_SPACE && *endcp != CHR_TAB &&
	    *endcp != CHR_NEWLINE) || quote_seen)) {
		/*
		 * Within quotes, ignore spaces and tabs as delimiters
		 * and take note of start and end quotes, ignoring
		 * backslash escaped characters.
		 *
		 * There is a catch here.  If multiple backslashes
		 * immediately precede the quote, they can cancel each
		 * other out.  An even number of sequential backslashes
		 * is not an escape for the end quote.
		 */
		if (quote_seen) {
			if (*endcp == CHR_BACKSLASH)
				num_escapes++;
			else if (*endcp != CHR_DOUBLEQUOTE ||
			    (num_escapes % 2 == 0))
			num_escapes = 0;
		}

		if (*endcp == CHR_DOUBLEQUOTE &&
		    ((endcp == startcp) ||
		    ((endcp > startcp) && (num_escapes % 2 == 0))))
			quote_seen = !quote_seen;
		endcp++;
	}
	if (*endcp != CHR_NULL)
		*endcp++ = CHR_NULL;
	*cpp = endcp;
	return (startcp);
}

static char *
readnextline(FILE *fp)
{
	char *cp;

	while ((cp = fgets(linebuf, sizeof (linebuf), fp)) != NULL) {
		err_line_number++; /* NOTE approx - if long lines */
		if (*cp == CHR_NEWLINE || *cp == CHR_COMMENT_BEGIN)
			continue;
		break;
	}
	if (cp == NULL)
		return (NULL);
	return (linebuf);
}


/*
 * static preshared_entry_t *getnextentry(fp, errmpp)
 *	fp
 *		file pointer to open config file.
 *	errmpp
 *		pointer to pointer to buffer containing error message
 *	return value
 *		NULL when EOF is reached
 *		pointer to valid entry otherwise
 *
 *	Function scans open input file to get the next entry read from
 *	the file and instantiated in the preshared_entry_t data structure.
 *	In case of invalid syntax entries the function exits and does
 *	not return and process exits.
 */
static preshared_entry_t *
getnextentry(FILE *fp, char **errmpp)
{
	preshared_entry_t *ps;
	char *cp;		/* pointer to scan input */
	char *tp;
	char *errmsg = NULL;
	int cur_fld_type = -1;	/* "uninitialized" */
	int entry_done = FALSE;
	int next_token_expected = PS_TOK_BEGIN_CURLY_BRACE;

	*errmpp = NULL;

	ps = calloc(1, sizeof (preshared_entry_t));	/* Auto-zeroes... */
	if (ps == NULL) {
		errmsg = strerror(errno);
		goto fatal_error;
	}
nextline:
	/* get next line  */

	/* skip comment and null lines */
	while (((cp = readnextline(fp)) != NULL) &&
	    (*cp == CHR_COMMENT_BEGIN || *cp == CHR_NULL))
		;

	if (cp == NULL) {
		/* EOF  */
		free(ps);
		return (NULL);
	}

	for (;;) {
		if (entry_done == TRUE)
			break;
		tp = get_next_token(&cp);
		if (tp == NULL)
			goto nextline;
process_newtoken:
		/* skip rest of line if comment token */
		if (*tp == CHR_COMMENT_BEGIN)
			goto nextline;

		switch (next_token_expected) {

		case PS_TOK_BEGIN_CURLY_BRACE:
			if (*tp != CHR_CURLY_BRACE_OPEN) {
				errmsg = (char *)err_arr[
				    ERR_ENTRY_NOBEGINBRACE];
				goto bad_syntax;
			}
			err_entry_number++;
			next_token_expected = PS_TOK_FLD_TYPE;

			/*
			 * Note: We skip tokens assuming whitespace
			 * termination but relax that for this token.
			 *
			 * Reset scan pointer past this token (one character)
			 * so pointer does not miss any tokens that may
			 * be glued to this one with no whitespaces.
			 */
			tp++;	/* past brace */
			if (*tp == CHR_NULL) {
				/* past whitespace terminated brace */
				tp++;
			}
			cp = tp;
			break;

		case PS_TOK_END_CURLY_BRACE:
			/*
			 * Do sanity checks on entry and other
			 * post-processing
			 */
			if (postprocess_entry(ps, &errmsg) < 0)
				goto bad_semantics;
			entry_done = TRUE;
			break;

		case PS_TOK_FLD_TYPE:
			/*
			 * Note: could be end-of-entry too...
			 * here we assume the closing brace is
			 * preceded by a whitespace. We gave some latitude to
			 * opening brace, but not to the closing brace
			 */
			if (*tp == CHR_CURLY_BRACE_CLOSE) {
				/*
				 * End of fields, reset expected token,
				 * Also reset scan token so this
				 */
				next_token_expected = PS_TOK_END_CURLY_BRACE;
				goto process_newtoken;
			}

			/* initialize cur_fld_type */
			if ((cur_fld_type = getfldtype(tp)) < 0) {
				errmsg = (char *)err_arr[ERR_INVALID_FIELDNAME];
				goto bad_syntax;
			}

			if ((ps->pe_flds_mask & cur_fld_type) != 0) {
				errmsg = (char *)err_arr[ERR_REPEATED_FIELD];
				goto bad_syntax;
			}

			ps->pe_flds_mask |= cur_fld_type;

			next_token_expected = PS_TOK_FLD_VALUE;
			break;

		case PS_TOK_FLD_VALUE:

			/*
			 * Note: we assume all value fields do
			 * not embed any whitespace even for complicated
			 * value syntax such as for "range". Is that a
			 * "reasonable" assumption ?
			 *
			 * Note++: All things being strdup()'d below need
			 * to be parsed and put into context dependent data
			 * structures at some point (after entry is read or
			 * right here).
			 */
			switch (cur_fld_type) {
			case PS_FLD_LOCID_TYPE:
				if ((ps->pe_locidtype = getidtype(tp)) < 0) {
					errmsg = (char *)err_arr[
					    ERR_INVALID_LOCALID_TYPE];
					goto bad_syntax;
				}
				break;
			case PS_FLD_LOCID:
				ps->pe_locid = strdup(tp);
				if (ps->pe_locid == NULL) {
					errmsg = strerror(errno);
					goto fatal_error;
				}
				break;
			case PS_FLD_REMID_TYPE:
				if ((ps->pe_remidtype = getidtype(tp)) < 0) {
					errmsg = (char *)err_arr[
					    ERR_INVALID_REMID_TYPE];
					goto bad_syntax;
				}
				break;
			case PS_FLD_REMID:
				ps->pe_remid = strdup(tp);
				if (ps->pe_remid == NULL) {
					errmsg = strerror(errno);
					goto fatal_error;
				}
				break;
			case PS_FLD_IKE_MODE:
				if ((ps->pe_ike_mode = getikmtype(tp)) < 0) {
					errmsg = (char *)err_arr[
					    ERR_INVALID_IKE_MODE];
					goto bad_syntax;
				}
				break;
			case PS_FLD_KEY:
				ps->pe_keybuf = parsekey(tp,
				    &ps->pe_keybuf_bytes,
				    &ps->pe_keybuf_lbits, &errmsg);
				if (ps->pe_keybuf == NULL)
					goto fatal_error;
				else if (errmsg != NULL) {
					/*
					 * Utter warning on otherwise okay
					 * input.
					 */
					(void) fprintf(stderr,
					    gettext("read_preshared: %s\n"),
					    errmsg);
				}
				break;
			default:
				/* Should never happen - assert ? */
				errmsg = (char *)err_arr[
				    ERR_INVALID_FIELDNAME];
				goto fatal_error;
			}

			next_token_expected = PS_TOK_FLD_TYPE;
			break;
		}
	}
	return (ps);
	/*
	 * TBD any differentiation in these errors ?
	 */
bad_semantics:
bad_syntax:
fatal_error:
	free(ps);
	*errmpp = errmsg;
	return (NULL);
}


/*
 * Load any preshared keys.
 * Boolean parameter 'replace' determines whether the newly-read entries
 * will replace the existing list, or be appended to it.
 * On success, returns a linked list of preshared entries, and sets the
 * error message pointer *errmp to NULL.
 * On failure, returns NULL and sets *errmp to an error message string.
 */
char *
preshared_load(const char *ps_filename, int ps_fd, boolean_t replace)
{
	preshared_entry_t *ps, *tmp_head = NULL, *tmp_tail = NULL;
	char *errmp;
	boolean_t dupfound = B_FALSE;

	/* TODO more paranoia/permissions checks on file needed */

	if (ps_filename == NULL)
		fp = fdopen(ps_fd, "r");
	else
		fp = fopen(ps_filename, "r");
	if (fp == NULL) {
		if (errno == ENOENT)
			return (NULL);	/* no file == empty file */
		return (strerror(errno));
	}

	while ((ps = getnextentry(fp, &errmp)) != NULL) {
		/* process the new entry */

		/*
		 * For now, create a list of just the new entries
		 */
		if (!replace) {
			if (has_dup(ps, &ps_head)) {
				/* Skip duplicates in master list */
				free(ps->pe_locid);
				free(ps->pe_remid);
				free(ps->pe_keybuf);
				free(ps);
				dupfound = B_TRUE;
				continue;
			}
		}
		if (!append_to_list(ps, &tmp_head, &tmp_tail)) {
			/* Duplicate found within new list */
			dupfound = B_TRUE;
		}
	}

	(void) fclose(fp);

	if (errmp == NULL) {
		/*
		 * we read the list in successfully; either
		 * replace or append to global list now.
		 */
		if (replace || ps_tail == NULL) {
			free_preshared_list(&ps_head, &ps_tail);
			ps_head = tmp_head;
			ps_tail = tmp_tail;
		} else {
			ps_tail->pe_next = tmp_head;
			ps_tail = tmp_tail;
		}
	} else {
		/*
		 * had problems reading new list; free it
		 */
		free_preshared_list(&tmp_head, &tmp_tail);
	}

	if (dupfound && (errmp == NULL)) {
		/* Hack to key off of later, not user visible */
		return ("DUP");
	}

	return (errmp);
}

/*
 * Returns 0 if requested entry not found; 1 if deleted successfully.
 */
int
delete_ps(preshared_entry_t *delp)
{
	preshared_entry_t	*curp, *prevp;

	if (ps_head == NULL)
		return (0);

	if (delp == ps_head) {
		/* CORNER CASE: Deletion of last entry */
		if (ps_head == ps_tail)
			ps_tail = NULL;
		ps_head = delp->pe_next;
		free(delp);
		return (1);
	}

	for (prevp = ps_head, curp = ps_head->pe_next; curp != NULL;
	    prevp = curp, curp = curp->pe_next) {
		if (curp == delp)
			break;
	}
	if (curp == NULL)
		return (0);

	prevp->pe_next = curp->pe_next;
	if (curp == ps_tail)
		ps_tail = prevp;
	/* Exploit that memset returns its first argument. */
	free(memset(curp, 0, sizeof (*curp)));
	return (1);
}

/*
 * return -1 on error, number of preshared entries written on success
 */
int
write_preshared(int fd, char **errmp)
{
	preshared_entry_t	*pep;
	FILE	*ofile;
	char	*keyp;
	int	written = 0;

	if ((ofile = fdopen(fd, "w+")) == NULL) {
		*errmp = strerror(errno);
		return (-1);
	}

	for (pep = ps_head; pep != NULL; pep = pep->pe_next) {
		const char *mdstr;

		if ((keyp = getkeystr(pep->pe_keybuf, pep->pe_keybuf_bytes,
		    pep->pe_keybuf_lbits)) == NULL) {
			*errmp = strerror(errno);
			return (-1);
		}

		if (pep->pe_locid != NULL) {
			(void) fprintf(ofile, "{\n"
			    "\tlocalidtype    %s\n\tlocalid        %s\n",
			    getidstr(pep->pe_locidtype), pep->pe_locid);
		} else {
			(void) fprintf(ofile,
			    "{\n\tlocalidtype    %s\n\tlocalid        ",
			    getidstr(pep->pe_locidtype));
			(void) dump_sockaddr(
			    (struct sockaddr *)&pep->pe_locid_sa,
			    (pep->pe_locid_plen > 0) ? pep->pe_locid_plen : 0,
			    B_TRUE, ofile, B_FALSE);
			(void) fprintf(ofile, "\n");
		}

		if (pep->pe_remid != NULL) {
			(void) fprintf(ofile,
			    "\tremoteidtype   %s\n\tremoteid       %s\n",
			    getidstr(pep->pe_remidtype), pep->pe_remid);
		} else {
			(void) fprintf(ofile,
			    "\tremoteidtype   %s\n\tremoteid       ",
			    getidstr(pep->pe_remidtype));
			(void) dump_sockaddr(
			    (struct sockaddr *)&pep->pe_remid_sa,
			    (pep->pe_remid_plen > 0) ? pep->pe_remid_plen : 0,
			    B_TRUE, ofile, B_FALSE);
			(void) fprintf(ofile, "\n");
		}

		mdstr = getikmstr(pep->pe_ike_mode);
		if (mdstr != NULL)
			(void) fprintf(ofile, "\tike_mode       %s\n", mdstr);

		(void) fprintf(ofile, "\tkey            %s\n}\n", keyp);
		free(memset(keyp, 0, strlen(keyp)));
		written++;
	}
	(void) fclose(ofile);

	return (written);
}

/*
 * Convert a PS_ID_* to an SADB_IDENTTYPE_* constant.
 * Returns SADB_IDENTTYPE_RESERVED on failure.
 */
int
psid2sadb(int psid)
{
	switch (psid) {
	case PS_ID_IP:
	case PS_ID_IP4:
	case PS_ID_IP6:
		return (SADB_IDENTTYPE_RESERVED);
	case PS_ID_SUBNET:
	case PS_ID_SUBNET4:
	case PS_ID_SUBNET6:
		return (SADB_IDENTTYPE_PREFIX);
	case PS_ID_RANGE:
	case PS_ID_RANGE4:
	case PS_ID_RANGE6:
		return (SADB_X_IDENTTYPE_ADDR_RANGE);
	case PS_ID_ASN1DN:
		return (SADB_X_IDENTTYPE_DN);
	case PS_ID_ASN1GN:
		return (SADB_X_IDENTTYPE_GN);
	case PS_ID_KEYID:
		return (SADB_X_IDENTTYPE_KEY_ID);
	case PS_ID_FQDN:
		return (SADB_IDENTTYPE_FQDN);
	case PS_ID_USER_FQDN:
		return (SADB_IDENTTYPE_USER_FQDN);
	}
	return (SADB_IDENTTYPE_RESERVED);
}

int
sadb2psid(int sadb)
{
	switch (sadb) {
	case SADB_IDENTTYPE_PREFIX:
		return (PS_ID_SUBNET);
	case SADB_X_IDENTTYPE_ADDR_RANGE:
		return (PS_ID_RANGE);
	case SADB_X_IDENTTYPE_DN:
		return (PS_ID_ASN1DN);
	case SADB_X_IDENTTYPE_GN:
		return (PS_ID_ASN1GN);
	case SADB_X_IDENTTYPE_KEY_ID:
		return (PS_ID_KEYID);
	case SADB_IDENTTYPE_FQDN:
		return (PS_ID_FQDN);
	case SADB_IDENTTYPE_USER_FQDN:
		return (PS_ID_USER_FQDN);
	}
	return (0);
}

/*
 * Lookup preshared entry by ident
 */
preshared_entry_t *
lookup_ps_by_ident(sadb_ident_t *local, sadb_ident_t *remote)
{
	int	ltype, rtype;
	char	*lid, *rid;
	preshared_entry_t	*ps;

	if (local == NULL || remote == NULL)
		return (NULL);

	lid = (char *)(local + 1);
	rid = (char *)(remote + 1);

	for (ps = ps_head; ps != NULL; ps = ps->pe_next) {

		/*
		 * Have to convert for each preshared (rather than converting
		 * the passed-in sadb ids just once to preshared-style)
		 * because there are multiple PS_ID_* for each SADB_IDENT_*.
		 */
		ltype = psid2sadb(ps->pe_locidtype);
		rtype = psid2sadb(ps->pe_remidtype);

		if ((local->sadb_ident_type != ltype) ||
		    (remote->sadb_ident_type != rtype))
			continue;

		if (ps->pe_locidtype == PS_ID_FQDN ||
		    ps->pe_locidtype == PS_ID_USER_FQDN) {
			/*
			 * Check for case insensitive match.
			 */
			if ((strcasecmp(ps->pe_locid, lid) == 0) &&
			    (strcasecmp(ps->pe_remid, rid) == 0))
				break;
		} else {
			/*
			 * Otherwise just go for an exact match.
			 */
			if ((strcmp(ps->pe_locid, lid) == 0) &&
			    (strcmp(ps->pe_remid, rid) == 0))
				break;
		}
	}
	return (ps);
}

/*
 * Stolen from kernel spd.c - maybe this should go in libipsecutil
 *
 * sleazy prefix-length-based compare.
 */
boolean_t
ip_addr_match(uint8_t *addr1, int pfxlen, in6_addr_t *addr2p)
{
	int offset = pfxlen>>3;
	int bitsleft = pfxlen & 7;
	uint8_t *addr2 = (uint8_t *)addr2p;

	/*
	 * and there was much evil..
	 * XXX should inline-expand the bcmp here and do this 32 bits
	 * or 64 bits at a time..
	 */
	return ((bcmp(addr1, addr2, offset) == 0) &&
	    ((bitsleft == 0) ||
	    (((addr1[offset] ^ addr2[offset]) & (0xff<<(8-bitsleft))) == 0)));
}

/*
 * Lookup preshared entry by v4 or v4 mapped v6 address
 */
preshared_entry_t *
lookup_ps_by_in_addr(struct in_addr *local, struct in_addr *remote)
{
	struct in_addr *inp;
	struct in_addr in;
	struct sockaddr_in	*loc_sin, *rem_sin;
	struct sockaddr_in6	*loc_sin6, *rem_sin6;
	preshared_entry_t *ps;
	boolean_t loc_is_v6, rem_is_v6;
	int pfxlen;

	if (local == NULL || remote == NULL)
		return (NULL);

	bzero(&in, sizeof (struct in_addr));

	/*
	 * Also looks up IPv6 id type entries for mapped addresses
	 */

	for (ps = ps_head; ps != NULL; ps = ps->pe_next) {

		/* filter on relevant id type first */
		if ((ps->pe_locidtype != PS_ID_IP4 &&
		    ps->pe_locidtype != PS_ID_IP6 &&
		    ps->pe_locidtype != PS_ID_IP &&
		    ps->pe_locidtype != PS_ID_SUBNET4 &&
		    ps->pe_locidtype != PS_ID_SUBNET6 &&
		    ps->pe_locidtype != PS_ID_SUBNET) ||
		    (ps->pe_remidtype != PS_ID_IP4 &&
		    ps->pe_remidtype != PS_ID_IP6 &&
		    ps->pe_remidtype != PS_ID_IP &&
		    ps->pe_remidtype != PS_ID_SUBNET4 &&
		    ps->pe_remidtype != PS_ID_SUBNET6 &&
		    ps->pe_remidtype != PS_ID_SUBNET))
			continue; /* local and/or remote id types invalid */

		/* establish format of address */
		if ((ps->pe_locidtype == PS_ID_IP6 ||
		    ps->pe_locidtype == PS_ID_SUBNET6) ||
		    ((ps->pe_locidtype == PS_ID_IP ||
		    ps->pe_locidtype == PS_ID_SUBNET) &&
		    check_if_v6(ps->pe_locid)))
			loc_is_v6 = B_TRUE;
		else
			loc_is_v6 = B_FALSE;
		if ((ps->pe_remidtype == PS_ID_IP6 ||
		    ps->pe_remidtype == PS_ID_SUBNET6) ||
		    ((ps->pe_locidtype == PS_ID_IP ||
		    ps->pe_locidtype == PS_ID_SUBNET) &&
		    check_if_v6(ps->pe_remid)))
			rem_is_v6 = B_TRUE;
		else
			rem_is_v6 = B_FALSE;

		loc_sin = (struct sockaddr_in *)&ps->pe_locid_sa;
		rem_sin = (struct sockaddr_in *)&ps->pe_remid_sa;
		loc_sin6 = (struct sockaddr_in6 *)&ps->pe_locid_sa;
		rem_sin6 = (struct sockaddr_in6 *)&ps->pe_remid_sa;

		/* if v6 format, it is only interesting if v4mapped */
		if (loc_is_v6 && !IN6_IS_ADDR_V4MAPPED(&loc_sin6->sin6_addr))
			continue;	/* not v4 mapped v6 address */
		if (rem_is_v6 && !IN6_IS_ADDR_V4MAPPED(&rem_sin6->sin6_addr))
			continue;	/* not v4 mapped v6 address */


		/* match localid */
		if (loc_is_v6) {
			IN6_V4MAPPED_TO_INADDR(&loc_sin6->sin6_addr, &in);
			inp = &in;
		} else {
			inp = &loc_sin->sin_addr;
		}

		if (ps->pe_locid_plen < 0)
			pfxlen = ADDRBITS_V4;
		else
			pfxlen = ps->pe_locid_plen;
		if (!ip_addr_match((uint8_t *)&inp->s_addr, pfxlen,
		    (in6_addr_t *)&local->s_addr))
			continue; /* local id mismatched */

		/* match remote id */
		if (rem_is_v6) {
			IN6_V4MAPPED_TO_INADDR(&rem_sin6->sin6_addr, &in);
			inp = &in;
		} else {
			inp = &rem_sin->sin_addr;
		}

		if (ps->pe_remid_plen < 0)
			pfxlen = ADDRBITS_V4;
		else
			pfxlen = ps->pe_remid_plen;
		if (!ip_addr_match((uint8_t *)&inp->s_addr, pfxlen,
		    (in6_addr_t *)&remote->s_addr))
			continue; /* remote id mismatched */

		/* match found for both local and remote id */
		break; /* out of loop */
	}
	return (ps); /* NULL if loop expires on its own */
}

/*
 * Lookup preshared entry by v6 address
 */
preshared_entry_t *
lookup_ps_by_in6_addr(struct in6_addr *local, struct in6_addr *remote)
{
	preshared_entry_t *ps;
	struct sockaddr_in6	*loc_sin6, *rem_sin6;
	int pfxlen;

	if (local == NULL || remote == NULL)
		return (NULL);

	for (ps = ps_head; ps != NULL; ps = ps->pe_next) {

		/* filter on relevant id type first */
		if ((ps->pe_locidtype != PS_ID_IP6 &&
		    ps->pe_locidtype != PS_ID_IP &&
		    ps->pe_locidtype != PS_ID_SUBNET &&
		    ps->pe_locidtype != PS_ID_SUBNET6) ||
		    (ps->pe_remidtype != PS_ID_IP6 &&
		    ps->pe_remidtype != PS_ID_IP &&
		    ps->pe_remidtype != PS_ID_SUBNET &&
		    ps->pe_remidtype != PS_ID_SUBNET6))
			continue; /* local and/or remote id types invalid */

		/*
		 * if PS_ID_IP or PS_ID_SUBNET, it is
		 * only interesting if v6 format
		 */
		if ((ps->pe_locidtype == PS_ID_IP ||
		    ps->pe_locidtype == PS_ID_SUBNET) &&
		    !check_if_v6(ps->pe_locid))
			continue; /* not v6 address */
		if ((ps->pe_remidtype == PS_ID_IP ||
		    ps->pe_remidtype == PS_ID_SUBNET) &&
		    !check_if_v6(ps->pe_remid))
			continue; /* not v6 address */

		loc_sin6 = (struct sockaddr_in6 *)&ps->pe_locid_sa;
		rem_sin6 = (struct sockaddr_in6 *)&ps->pe_remid_sa;

		if (ps->pe_locid_plen < 0)
			pfxlen = ADDRBITS_V6;
		else
			pfxlen = ps->pe_locid_plen;
		if (!ip_addr_match((uint8_t *)&loc_sin6->sin6_addr, pfxlen,
		    (in6_addr_t *)local))
			continue; /* local id mismatched */

		if (ps->pe_remid_plen < 0)
			pfxlen = ADDRBITS_V6;
		else
			pfxlen = ps->pe_remid_plen;
		if (!ip_addr_match((uint8_t *)&rem_sin6->sin6_addr, pfxlen,
		    (in6_addr_t *)remote))
			continue; /* remote id mismatched */
		/* match found for both local and remote id */
		break; /* out of loop */
	}
	return (ps); /* NULL if loop expires on its own */
}

/*
 * Lookup nth preshared key entry
 */
preshared_entry_t *
lookup_nth_ps(int n)
{
	preshared_entry_t	*ps;
	int			cnt;

	for (cnt = 0, ps = ps_head; (cnt < n) && (ps != NULL);
	    cnt++, ps = ps->pe_next)
		;
	return (ps);
}

/*
 * Random Notes:
 * (a)
 * Maybe a better syntax for fields is following which eliminates
 * semantic relationships between two different fields (id and idtype) ???
 * E.g.
 * {
 *	...
 *	localid	ipv4		1.2.3.4
 *	remoteid rangev4	1.2.3.4-5.6.7.8
 *	...
 *  }
 *  Current design causes us to delay certain verifications into a
 *  postprocess_XXX() routine which otherwise could be done while
 *  parsing if syntax was as above.
 *
 * (b)
 *	Current error handling has a generic design that prints only
 *	error message, entry number and line number. Other options are
 *	possible (e.g. parsekey() "original" code from danmcd prints
 *	error messages specific to the error context which have more
 *	detailed context). Perhaps we should change to direct printing
 *	of specific error messages globally later, but for now stick
 *	to the simple model coded above.
 * (c) The getnexttoken() part can be improved by passing it some context
 *	such as "expected token" and maybe the fieldname. This *may* help
 *	allow a more flexible syntax for complex tokens such as ranges.
 *	which cannot currently embed spaces but it would be nice to allow
 *	them to do that and be delimited with a context sensitive delimiter.
 * (d) Needs detection of duplicates in the database before inserting new
 *	entry.
 * (e) Can be enhanced to do hashes and better structures than a linked list
 *	of entries.
 */
