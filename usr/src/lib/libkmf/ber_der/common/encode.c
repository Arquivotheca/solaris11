/*
 * -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

/*
 * Copyright (c) 1990 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <inttypes.h>

#include <ber_der.h>
#include "kmfber_int.h"

/* the following constants are used in kmfber_calc_lenlen */

#define	LENMASK1	0xFF
#define	LENMASK2 	0xFFFF
#define	LENMASK3	0xFFFFFF
#define	LENMASK4	0xFFFFFFFF
#define	_MASK		0x80

int
kmfber_calc_taglen(ber_tag_t tag)
{
	int		i;
	ber_int_t	mask;

	/* find the first non-all-zero byte in the tag */
	for (i = sizeof (ber_int_t) - 1; i > 0; i--) {
		mask = (LENMASK3 << (i * 8));
		/* not all zero */
		if (tag & mask)
			break;
	}

	return (i + 1);
}

static int
ber_put_tag(BerElement	*ber, ber_tag_t tag, int nosos)
{
	ber_int_t	taglen;
	ber_tag_t	ntag;

	taglen = kmfber_calc_taglen(tag);

	ntag = htonl(tag);

	return (kmfber_write(ber,
	    ((char *) &ntag) + sizeof (ber_int_t) - taglen,
	    taglen, nosos));
}

int
kmfber_calc_lenlen(ber_int_t len)
{
	/*
	 * short len if it's less than 128 - one byte giving the len,
	 * with bit 8 0.
	 */

	if (len <= 0x7F)
		return (1);

	/*
	 * long len otherwise - one byte with bit 8 set, giving the
	 * length of the length, followed by the length itself.
	 */

	if (len <= LENMASK1)
		return (2);
	if (len <= LENMASK2)
		return (3);
	if (len <= LENMASK3)
		return (4);

	return (5);
}

int
kmfber_put_len(BerElement *ber, ber_int_t len, int nosos)
{
	int		i;
	char		lenlen;
	ber_int_t	mask, netlen;

	/*
	 * short len if it's less than 128 - one byte giving the len,
	 * with bit 8 0.
	 */
	if (len <= 127) {
		netlen = htonl(len);
		return (kmfber_write(ber,
		    (char *)&netlen + sizeof (ber_int_t) - 1,
		    1, nosos));
	}

	/*
	 * long len otherwise - one byte with bit 8 set, giving the
	 * length of the length, followed by the length itself.
	 */

	/* find the first non-all-zero byte */
	for (i = sizeof (ber_int_t) - 1; i > 0; i--) {
		mask = (LENMASK1 << (i * 8));
		/* not all zero */
		if (len & mask)
			break;
	}
	lenlen = ++i;
	if (lenlen > 4)
		return (-1);
	lenlen |= 0x80;

	/* write the length of the length */
	if (kmfber_write(ber, &lenlen, 1, nosos) != 1)
		return (-1);

	/* write the length itself */
	netlen = htonl(len);
	if (kmfber_write(ber,
	    (char *) &netlen + (sizeof (ber_int_t) - i), i, nosos) != i)
		return (-1);

	return (i + 1);
}

static int
ber_put_int_or_enum(BerElement *ber, ber_int_t num, ber_tag_t tag)
{
	int		i, sign;
	ber_int_t	len, lenlen, taglen, netnum, mask;

	sign = (num < 0);

	/*
	 * high bit is set - look for first non-all-one byte
	 * high bit is clear - look for first non-all-zero byte
	 */
	for (i = sizeof (ber_int_t) - 1; i > 0; i--) {
		mask = (LENMASK1 << (i * 8));

		if (sign) {
			/* not all ones */
			if ((num & mask) != mask)
				break;
		} else {
			/* not all zero */
			if (num & mask)
				break;
		}
	}

	/*
	 * we now have the "leading byte".  if the high bit on this
	 * byte matches the sign bit, we need to "back up" a byte.
	 */
	mask = (num & (_MASK << (i * 8)));
	if ((mask && !sign) || (sign && !mask))
		i++;

	len = i + 1;

	if ((taglen = ber_put_tag(ber, tag, 0)) == -1)
		return (-1);

	if ((lenlen = kmfber_put_len(ber, len, 0)) == -1)
		return (-1);
	i++;
	netnum = htonl(num);
	if (kmfber_write(ber,
	    (char *) &netnum + (sizeof (ber_int_t) - i), i, 0) == i)
		/* length of tag + length + contents */
		return (taglen + lenlen + i);

	return (-1);
}

static int
kmfber_put_enum(BerElement *ber, ber_int_t num, ber_tag_t tag)
{
	if (tag == KMFBER_DEFAULT)
		tag = BER_ENUMERATED;

	return (ber_put_int_or_enum(ber, num, tag));
}

int
ber_put_int(BerElement *ber, ber_int_t num, ber_tag_t tag)
{
	if (tag == KMFBER_DEFAULT)
		tag = BER_INTEGER;

	return (ber_put_int_or_enum(ber, num, tag));
}

int
ber_put_oid(BerElement *ber, struct berval *oid, ber_tag_t tag)
{
	ber_int_t taglen, lenlen, rc, len;

	if (tag == KMFBER_DEFAULT)
		tag = 0x06; 	/* TODO: Add new OID constant to header */

	if ((taglen = ber_put_tag(ber, tag, 0)) == -1)
		return (-1);

	len = (ber_int_t)oid->bv_len;
	if ((lenlen = kmfber_put_len(ber, len, 0)) == -1 ||
	    kmfber_write(ber, oid->bv_val, oid->bv_len, 0) !=
	    (ber_int_t)oid->bv_len) {
		rc = -1;
	} else {
		/* return length of tag + length + contents */
		rc = taglen + lenlen + oid->bv_len;
	}
	return (rc);
}

int
ber_put_big_int(BerElement *ber, ber_tag_t tag, char *data,
	ber_len_t len)
{
	ber_int_t taglen, lenlen, ilen, rc;
	char zero = 0x00;

	if (tag == KMFBER_DEFAULT)
		tag = BER_INTEGER;

	if ((taglen = ber_put_tag(ber, tag, 0)) == -1)
		return (-1);

	/* Add a leading 0 if the high order bit is set */
	if (data[0] & 0x80)
		len++;

	ilen = (ber_int_t)len;
	if ((lenlen = kmfber_put_len(ber, ilen, 0)) == -1)
		return (-1);

	/* add leading 0 if hi bit set */
	if ((data[0] & 0x80) && kmfber_write(ber, &zero, 1, 0) != 1)
		return (-1);

	/* Adjust the length of the write if hi-order bit is set */
	if (data[0] & 0x80)
		ilen = len - 1;
	if (kmfber_write(ber, data, ilen, 0) != (ber_int_t)ilen) {
		return (-1);
	} else {
		/* return length of tag + length + contents */
		rc = taglen + lenlen + len;
	}
	return (rc);
}

static int
kmfber_put_ostring(BerElement *ber, char *str, ber_len_t len,
	ber_tag_t tag)
{
	ber_int_t	taglen, lenlen, ilen, rc;
#ifdef STR_TRANSLATION
	int	free_str;
#endif /* STR_TRANSLATION */

	if (tag == KMFBER_DEFAULT)
		tag = BER_OCTET_STRING;

	if ((taglen = ber_put_tag(ber, tag, 0)) == -1)
		return (-1);

#ifdef STR_TRANSLATION
	if (len > 0 && (ber->ber_options & KMFBER_OPT_TRANSLATE_STRINGS) != 0 &&
	    ber->ber_encode_translate_proc != NULL) {
		if ((*(ber->ber_encode_translate_proc))(&str, &len, 0)
		    != 0) {
			return (-1);
		}
		free_str = 1;
	} else {
		free_str = 0;
	}
#endif /* STR_TRANSLATION */

	/*
	 *  Note:  below is a spot where we limit ber_write
	 *	to signed long (instead of unsigned long)
	 */
	ilen = (ber_int_t)len;
	if ((lenlen = kmfber_put_len(ber, ilen, 0)) == -1 ||
	    kmfber_write(ber, str, len, 0) != (ber_int_t)len) {
		rc = -1;
	} else {
		/* return length of tag + length + contents */
		rc = taglen + lenlen + len;
	}

#ifdef STR_TRANSLATION
	if (free_str) {
		free(str);
	}
#endif /* STR_TRANSLATION */

	return (rc);
}

static int
kmfber_put_string(BerElement *ber, char *str, ber_tag_t tag)
{
	return (kmfber_put_ostring(ber, str, (ber_len_t)strlen(str), tag));
}

static int
kmfber_put_bitstring(BerElement *ber, char *str,
	ber_len_t blen /* in bits */, ber_tag_t tag)
{
	ber_int_t	taglen, lenlen, len;
	unsigned char	unusedbits;

	if (tag == KMFBER_DEFAULT)
		tag = BER_BIT_STRING;

	if ((taglen = ber_put_tag(ber, tag, 0)) == -1)
		return (-1);

	len = (blen + 7) / 8;
	unusedbits = (unsigned char) (len * 8 - blen);
	if ((lenlen = kmfber_put_len(ber, len + 1, 0)) == -1)
		return (-1);

	if (kmfber_write(ber, (char *)&unusedbits, 1, 0) != 1)
		return (-1);

	if (kmfber_write(ber, str, len, 0) != len)
		return (-1);

	/* return length of tag + length + unused bit count + contents */
	return (taglen + 1 + lenlen + len);
}

static int
kmfber_put_null(BerElement *ber, ber_tag_t tag)
{
	int	taglen;

	if (tag == KMFBER_DEFAULT)
		tag = BER_NULL;

	if ((taglen = ber_put_tag(ber, tag, 0)) == -1)
		return (-1);

	if (kmfber_put_len(ber, 0, 0) != 1)
		return (-1);

	return (taglen + 1);
}

static int
kmfber_put_boolean(BerElement *ber, int boolval, ber_tag_t tag)
{
	int		taglen;
	unsigned char	trueval = 0xff;
	unsigned char	falseval = 0x00;

	if (tag == KMFBER_DEFAULT)
		tag = BER_BOOLEAN;

	if ((taglen = ber_put_tag(ber, tag, 0)) == -1)
		return (-1);

	if (kmfber_put_len(ber, 1, 0) != 1)
		return (-1);

	if (kmfber_write(ber, (char *)(boolval ? &trueval : &falseval), 1, 0)
	    != 1)
		return (-1);

	return (taglen + 2);
}

#define	FOUR_BYTE_LEN	5


/*
 * The idea here is roughly this: we maintain a stack of these Seqorset
 * structures. This is pushed when we see the beginning of a new set or
 * sequence. It is popped when we see the end of a set or sequence.
 * Since we don't want to malloc and free these structures all the time,
 * we pre-allocate a small set of them within the ber element structure.
 * thus we need to spot when we've overflowed this stack and fall back to
 * malloc'ing instead.
 */
static int
ber_start_seqorset(BerElement *ber, ber_tag_t tag)
{
	Seqorset	*new_sos;

	/* can we fit into the local stack ? */
	if (ber->ber_sos_stack_posn < SOS_STACK_SIZE) {
		/* yes */
		new_sos = &ber->ber_sos_stack[ber->ber_sos_stack_posn];
	} else {
		/* no */
		if ((new_sos = (Seqorset *)malloc(sizeof (Seqorset)))
		    == NULLSEQORSET) {
			return (-1);
		}
	}
	ber->ber_sos_stack_posn++;

	if (ber->ber_sos == NULLSEQORSET)
		new_sos->sos_first = ber->ber_ptr;
	else
		new_sos->sos_first = ber->ber_sos->sos_ptr;

	/* Set aside room for a 4 byte length field */
	new_sos->sos_ptr = new_sos->sos_first + kmfber_calc_taglen(tag) +
	    FOUR_BYTE_LEN;
	new_sos->sos_tag = tag;

	new_sos->sos_next = ber->ber_sos;
	new_sos->sos_clen = 0;

	ber->ber_sos = new_sos;
	if (ber->ber_sos->sos_ptr > ber->ber_end) {
		if (kmfber_realloc(ber, ber->ber_sos->sos_ptr -
		    ber->ber_end) != 0)
			return (-1);
	}
	return (0);
}

static int
kmfber_start_seq(BerElement *ber, ber_tag_t tag)
{
	if (tag == KMFBER_DEFAULT)
		tag = BER_CONSTRUCTED_SEQUENCE;

	return (ber_start_seqorset(ber, tag));
}

static int
kmfber_start_set(BerElement *ber, ber_tag_t tag)
{
	if (tag == KMFBER_DEFAULT)
		tag = BER_CONSTRUCTED_SET;

	return (ber_start_seqorset(ber, tag));
}

static int
ber_put_seqorset(BerElement *ber)
{
	ber_int_t	netlen, len, taglen, lenlen;
	unsigned char	ltag = 0x80 + FOUR_BYTE_LEN - 1;
	Seqorset	*next;
	Seqorset	**sos = &ber->ber_sos;

	/*
	 * If this is the toplevel sequence or set, we need to actually
	 * write the stuff out.  Otherwise, it's already been put in
	 * the appropriate buffer and will be written when the toplevel
	 * one is written.  In this case all we need to do is update the
	 * length and tag.
	 */

	len = (*sos)->sos_clen;
	netlen = (ber_len_t)htonl(len);

	if (ber->ber_options & KMFBER_OPT_USE_DER) {
		lenlen = kmfber_calc_lenlen(len);
	} else {
		lenlen = FOUR_BYTE_LEN;
	}

	if ((next = (*sos)->sos_next) == NULLSEQORSET) {
		/* write the tag */
		if ((taglen = ber_put_tag(ber, (*sos)->sos_tag, 1)) == -1)
			return (-1);

		if (ber->ber_options & KMFBER_OPT_USE_DER) {
			/* Write the length in the minimum # of octets */
			if (kmfber_put_len(ber, len, 1) == -1)
				return (-1);

			if (lenlen != FOUR_BYTE_LEN) {
				/*
				 * We set aside FOUR_BYTE_LEN bytes for
				 * the length field.  Move the data if
				 * we don't actually need that much
				 */
				(void) memmove((*sos)->sos_first + taglen +
				    lenlen, (*sos)->sos_first + taglen +
				    FOUR_BYTE_LEN, len);
			}
		} else {
			/* Fill FOUR_BYTE_LEN bytes for length field */
			/* one byte of length length */
			if (kmfber_write(ber, (char *)&ltag, 1, 1) != 1)
				return (-1);

			/* the length itself */
			if (kmfber_write(ber,
			    (char *)&netlen + sizeof (ber_int_t)
			    - (FOUR_BYTE_LEN - 1), FOUR_BYTE_LEN - 1, 1) !=
			    FOUR_BYTE_LEN - 1)
				return (-1);
		}
		/* The ber_ptr is at the set/seq start - move it to the end */
		ber->ber_ptr += len;
	} else {
		ber_tag_t	ntag;

		/* the tag */
		taglen = kmfber_calc_taglen((*sos)->sos_tag);
		ntag = htonl((*sos)->sos_tag);
		(void) memmove((*sos)->sos_first, (char *)&ntag +
		    sizeof (ber_int_t) - taglen, taglen);

		if (ber->ber_options & KMFBER_OPT_USE_DER) {
			ltag = (lenlen == 1) ? (unsigned char)len :
			    (unsigned char) (0x80 + (lenlen - 1));
		}

		/* one byte of length length */
		(void) memmove((*sos)->sos_first + 1, &ltag, 1);

		if (ber->ber_options & KMFBER_OPT_USE_DER) {
			if (lenlen > 1) {
				/* Write the length itself */
				(void) memmove((*sos)->sos_first + 2,
				    (char *)&netlen + sizeof (ber_uint_t) -
				    (lenlen - 1),
				    lenlen - 1);
			}
			if (lenlen != FOUR_BYTE_LEN) {
				/*
				 * We set aside FOUR_BYTE_LEN bytes for
				 * the length field.  Move the data if
				 * we don't actually need that much
				 */
				(void) memmove((*sos)->sos_first + taglen +
				    lenlen, (*sos)->sos_first + taglen +
				    FOUR_BYTE_LEN, len);
			}
		} else {
			/* the length itself */
			(void) memmove((*sos)->sos_first + taglen + 1,
			    (char *) &netlen + sizeof (ber_int_t) -
			    (FOUR_BYTE_LEN - 1), FOUR_BYTE_LEN - 1);
		}

		next->sos_clen += (taglen + lenlen + len);
		next->sos_ptr += (taglen + lenlen + len);
	}

	/* we're done with this seqorset, so free it up */
	/* was this one from the local stack ? */
	if (ber->ber_sos_stack_posn > SOS_STACK_SIZE) {
		free((char *)(*sos));
	}
	ber->ber_sos_stack_posn--;
	*sos = next;

	return (taglen + lenlen + len);
}

/* VARARGS */
int
kmfber_printf(BerElement *ber, const char *fmt, ...)
{
	va_list		ap;
	char		*s, **ss;
	struct berval	**bv, *oid;
	int		rc, i, t;
	ber_int_t	len;

	va_start(ap, fmt);

#ifdef KMFBER_DEBUG
	if (lber_debug & 64) {
		char msg[80];
		sprintf(msg, "kmfber_printf fmt (%s)\n", fmt);
		ber_err_print(msg);
	}
#endif

	for (rc = 0; *fmt && rc != -1; fmt++) {
		switch (*fmt) {
		case 'b':	/* boolean */
			i = va_arg(ap, int);
			rc = kmfber_put_boolean(ber, i, ber->ber_tag);
			break;

		case 'i':	/* int */
			i = va_arg(ap, int);
			rc = ber_put_int(ber, (ber_int_t)i, ber->ber_tag);
			break;

		case 'D':	/* Object ID */
			if ((oid = va_arg(ap, struct berval *)) == NULL)
				break;
			rc = ber_put_oid(ber, oid, ber->ber_tag);
			break;
		case 'I':	/* int */
			s = va_arg(ap, char *);
			len = va_arg(ap, ber_int_t);
			rc = ber_put_big_int(ber, ber->ber_tag, s, len);
			break;

		case 'e':	/* enumeration */
			i = va_arg(ap, int);
			rc = kmfber_put_enum(ber, (ber_int_t)i, ber->ber_tag);
			break;

		case 'l':
			t = va_arg(ap, int);
			rc = kmfber_put_len(ber, t, 0);
			break;
		case 'n':	/* null */
			rc = kmfber_put_null(ber, ber->ber_tag);
			break;

		case 'o':	/* octet string (non-null terminated) */
			s = va_arg(ap, char *);
			len = va_arg(ap, int);
			rc = kmfber_put_ostring(ber, s, len, ber->ber_tag);
			break;

		case 's':	/* string */
			s = va_arg(ap, char *);
			rc = kmfber_put_string(ber, s, ber->ber_tag);
			break;

		case 'B':	/* bit string */
			s = va_arg(ap, char *);
			len = va_arg(ap, int);	/* in bits */
			rc = kmfber_put_bitstring(ber, s, len, ber->ber_tag);
			break;

		case 't':	/* tag for the next element */
			ber->ber_tag = va_arg(ap, ber_tag_t);
			ber->ber_usertag = 1;
			break;

		case 'T': /* Write an explicit tag, but don't change current */
			t = va_arg(ap, int);
			rc = ber_put_tag(ber, t, 0);
			break;

		case 'v':	/* vector of strings */
			if ((ss = va_arg(ap, char **)) == NULL)
				break;
			for (i = 0; ss[i] != NULL; i++) {
				if ((rc = kmfber_put_string(ber, ss[i],
				    ber->ber_tag)) == -1)
					break;
			}
			break;

		case 'V':	/* sequences of strings + lengths */
			if ((bv = va_arg(ap, struct berval **)) == NULL)
				break;
			for (i = 0; bv[i] != NULL; i++) {
				if ((rc = kmfber_put_ostring(ber, bv[i]->bv_val,
				    bv[i]->bv_len, ber->ber_tag)) == -1)
					break;
			}
			break;

		case '{':	/* begin sequence */
			rc = kmfber_start_seq(ber, ber->ber_tag);
			break;

		case '}':	/* end sequence */
			rc = ber_put_seqorset(ber);
			break;

		case '[':	/* begin set */
			rc = kmfber_start_set(ber, ber->ber_tag);
			break;

		case ']':	/* end set */
			rc = ber_put_seqorset(ber);
			break;

		default: {
#ifdef KMFBER_DEBUG
				char msg[80];
				sprintf(msg, "unknown fmt %c\n", *fmt);
				ber_err_print(msg);
#endif
				rc = -1;
				break;
			}
		}

		if (ber->ber_usertag == 0)
			ber->ber_tag = KMFBER_DEFAULT;
		else
			ber->ber_usertag = 0;
	}

	va_end(ap);

	return (rc);
}
