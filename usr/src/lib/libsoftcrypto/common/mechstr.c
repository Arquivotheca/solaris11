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
 *
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 *
 */

#include "libsoftcrypto.h"

#define	MAXSTRLEN 1024

typedef struct {
	int num;
	const char *str;
} mechstr_t;

static const mechstr_t mechstrlist[] = {
	{CRYPTO_AES_ECB, "CRYPTO_AES_ECB"},
	{CRYPTO_AES_CBC, "CRYPTO_AES_CBC"},
	{CRYPTO_AES_CTR, "CRYPTO_AES_CTR"},
	{CRYPTO_AES_CCM, "CRYPTO_AES_CCM"},
	{CRYPTO_AES_GCM, "CRYPTO_AES_GCM"},
	{CRYPTO_AES_CFB128, "CRYPTO_AES_CFB128"},
	{CRYPTO_RSA_PKCS, "CRYPTO_RSA_PKCS"},
	{CRYPTO_RSA_X_509, "CRYPTO_RSA_X_509"},
	{CRYPTO_MD5_RSA_PKCS, "CRYPTO_MD5_RSA_PKCS"},
	{CRYPTO_SHA1_RSA_PKCS, "CRYPTO_SHA1_RSA_PKCS"},
	{CRYPTO_SHA256_RSA_PKCS, "CRYPTO_SHA256_RSA_PKCS"},
	{CRYPTO_SHA384_RSA_PKCS, "CRYPTO_SHA384_RSA_PKCS"},
	{CRYPTO_SHA512_RSA_PKCS, "CRYPTO_SHA512_RSA_PKCS"}
};

/* bsearch compare function */
static int
mech_comp(const void *mech1, const void *mech2) {
	return (((mechstr_t *)mech1)->num - ((mechstr_t *)mech2)->num);
}

/* Returns the string value of the mechanism for a given mechanism number */
const char *
ucrypto_id2mech(ucrypto_mech_t mech_type)
{
	mechstr_t target;
	mechstr_t *result = NULL;

	target.num = mech_type;
	target.str = NULL;

	result = (mechstr_t *)bsearch((void *)&target, (void *)mechstrlist,
	    (sizeof (mechstrlist) / sizeof (mechstr_t)), sizeof (mechstr_t),
	    mech_comp);
	if (result != NULL)
		return (result->str);

	return (NULL);
}

/*
 * Return a deliminated string of supported mechanisms with their
 * value number specified in ucrypto_mech_t.  See libsoftcrypto.h for
 * details on format
 *
 */
int
ucrypto_get_mechlist(char *s)
{
	int i = 0, pos;
	static const int max = sizeof (mechstrlist) / sizeof (mechstr_t);
	char str[MAXSTRLEN];

	if (s == NULL)
		s = &(str[0]);

	pos = sprintf(s, "%d:", max);
	for (i = 0; i < max; i++)
		pos += sprintf(s + pos, "%s,%d;", mechstrlist[i].str,
		    mechstrlist[i].num);

	(void) sprintf(s + pos, "\0");
	pos++;  /* Need to count the NULL */

	return (pos);
}

/*
 * With a given mechanism string value, match it with the supported
 * mechanism and return the mechanism number, if found; otherwise, it
 * returns an undefined mech number.
 */
ucrypto_mech_t
ucrypto_mech2id(const char *str)
{
	static const int max = sizeof (mechstrlist) / sizeof (mechstr_t);
	int i;

	if (str == NULL)
		return (0);

	for (i = 0; i < max; i++) {
		if (strcmp(mechstrlist[i].str, str) == 0)
			return (mechstrlist[i].num);
	}

	return (0);
}
