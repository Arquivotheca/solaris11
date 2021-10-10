/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#if defined(K_SOLARIS_PLATFORM) && !defined(SOLARIS10)
#include <stdlib.h>
#include <aes_impl.h>
#elif defined(K_LINUX_PLATFORM)
#include <string.h>	/* memcpy */
#include <openssl/aes.h>
#else
#include "rijndael.h"
#endif

#ifdef METAWARE
#include "sizet.h"
typedef unsigned char		uint8_t;
typedef unsigned short		uint16_t;
typedef unsigned int		uint32_t;
typedef unsigned long long	uint64_t;
#include <string.h>
#else
#ifndef WIN32
#include <strings.h>
#endif
#endif

#include "KMSAgentAESKeyWrap.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifdef WIN32
#define ovbcopy(x, y, z) memmove(y, x, z);
#else
#define ovbcopy(x, y, z) bcopy(x, y, z);
#endif

#ifdef METAWARE
#define bcopy(s1, s2, n)  memcpy(s2, s1, n)
#endif

/*
 * AES Key Wrap (see RFC 3394).
 */
static const uint8_t aes_key_wrap_iv[8] =
	{ 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6 };

void aes_key_wrap (const uint8_t *kek,
                   size_t kek_len,
                   const uint8_t *pt,
                   size_t len,
                   uint8_t *ct)
{
#if defined(K_SOLARIS_PLATFORM) && !defined(SOLARIS10)
	void *ks;
	size_t ks_size;
#elif defined(K_LINUX_PLATFORM)
	AES_KEY aes_key;
#else
	rijndael_ctx ctx;
#endif
	uint8_t *a, *r, ar[16], t, b[16];
	size_t i;
	int j;

	/*
	 * Only allow lengths for 't' values that fit within a byte.  This 
	 * covers all reasonable uses of AES Key Wrap
	 */
	if (len > (255 / 6)) {
		return;
	}

	/* allow ciphertext and plaintext to overlap (ct == pt) */
	ovbcopy(pt, ct + 8, len * 8);

	a = ct;
	memcpy(a, aes_key_wrap_iv, 8);	/* default IV */

#if defined(K_SOLARIS_PLATFORM) && !defined(SOLARIS10)
	ks = aes_alloc_keysched(&ks_size, 0);
	if (ks == NULL)
		return;
	aes_init_keysched(kek, kek_len * 8, ks);
#elif defined(K_LINUX_PLATFORM)
	AES_set_encrypt_key(kek, kek_len * 8, &aes_key);
#else
	rijndael_set_key_enc_only(&ctx, (uint8_t *)kek, kek_len * 8);
#endif

	for (j = 0, t = 1; j < 6; j++) {
		r = ct + 8;
		for (i = 0; i < len; i++, t++) {
			memcpy(ar, a, 8);
			memcpy(ar + 8, r, 8);
#if defined(K_SOLARIS_PLATFORM) && !defined(SOLARIS10)
			(void) aes_encrypt_block(ks, ar, b);
#elif defined(K_LINUX_PLATFORM)
			AES_encrypt(ar, b, &aes_key);
#else
			rijndael_encrypt(&ctx, ar, b);
#endif

			b[7] ^= t;
			memcpy(a, &b[0], 8);
			memcpy(r, &b[8], 8);

			r += 8;
		}
	}
#if defined(K_SOLARIS_PLATFORM) && !defined(SOLARIS10)
	free(ks);
#endif
}

int aes_key_unwrap (const uint8_t *kek,
                    size_t kek_len,
                    const uint8_t *ct,
                    uint8_t *pt,
                    size_t len)
{
#if defined(K_SOLARIS_PLATFORM) && !defined(SOLARIS10)
	void *ks;
	size_t ks_size;
#elif defined(K_LINUX_PLATFORM)
	AES_KEY aes_key;
#else
	rijndael_ctx ctx;
#endif
	uint8_t a[8], *r, b[16], t, ar[16];
	size_t i;
	int j;

	/*
	 * Only allow lengths for 't' values that fit within a byte.  This
	 * covers all reasonable uses of AES Key Wrap
	 */
	if (len > (255 / 6)) {
		return (-1);
	}

	memcpy(a, ct, 8);
	/* allow ciphertext and plaintext to overlap (ct == pt) */
	ovbcopy(ct + 8, pt, len * 8);
#if defined(K_SOLARIS_PLATFORM) && !defined(SOLARIS10)
	ks = aes_alloc_keysched(&ks_size, 0);
	if (ks == NULL)
		return (-1);
	aes_init_keysched(kek, kek_len * 8, ks);
#elif defined(K_LINUX_PLATFORM)
	AES_set_decrypt_key(kek, kek_len * 8, &aes_key);
#else
	rijndael_set_key(&ctx, (uint8_t *)kek, kek_len * 8);
#endif

	for (j = 0, t = 6 * len; j < 6; j++) {
		r = pt + (len - 1) * 8;
		for (i = 0; i < len; i++, t--) {
			memcpy(&ar[0], a, 8);
			ar[7] ^= t;
			memcpy(&ar[8], r, 8);
#if defined(K_SOLARIS_PLATFORM) && !defined(SOLARIS10)
			(void) aes_decrypt_block(ks, ar, b);
#elif defined(K_LINUX_PLATFORM)
			AES_decrypt(ar, b, &aes_key);
#else
			rijndael_decrypt(&ctx, ar, b);
#endif
			memcpy(a, b, 8);
			memcpy(r, b + 8, 8);
			r -= 8;
		}
	}
#if defined(K_SOLARIS_PLATFORM) && !defined(SOLARIS10)
	free(ks);
#endif

	return memcmp(a, aes_key_wrap_iv, 8) != 0;
}
