/*

  sha256.c

  Author: Mika Kojo <mkojo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Fri Dec  8 21:17:43 2000.

  */

/* This is partially based on code by Antti Huima for the SHA-1. */

#include "sshincludes.h"
#include "sshcrypt.h"
#include "sshhash_i.h"
#include "sha.h"
#include "sshgetput.h"

#include "sha256.h"

/* Define SHA-256 in transparent way. */
const SshHashDefStruct ssh_hash_sha256_def =
{
  /* Name of the hash function. */
  "sha256",
  /* Certification status */
  0,
  /* ASN.1 Object identifier (not defined) */
  NULL,
  /* ISO/IEC dedicated hash identifier. */
  0,
  /* Digest size. */
  32,
  /* Input block length. */
  64,
  /* Context size */
  ssh_sha256_ctxsize,
  /* Reset function, between long usage of one context. */
  ssh_sha256_reset_context,
  /* Update function */
  ssh_sha256_update,
  /* Final */
  ssh_sha256_final,
  /* No ASN1. */
  NULL, NULL
};

/* Define SHA-256 in transparent way. */
const SshHashDefStruct ssh_hash_sha256_96_def =
{
  /* Name of the hash function. */
  "sha256-96",
  /* Certification status */
  0,
  /* ASN.1 Object identifier (not defined) */
  NULL,
  /* ISO/IEC dedicated hash identifier. */
  0, /* None */
  /* Digest size. */
  12,
  /* Input block length. */
  64,
  /* Context size */
  ssh_sha256_ctxsize,
  /* Reset function, between long usage of one context. */
  ssh_sha256_reset_context,
  /* Update function */
  ssh_sha256_update,
  /* Final */
  ssh_sha256_96_final,
  /* No ASN1. */
  NULL, NULL
};

/* Define SHA-256 in transparent way. */
const SshHashDefStruct ssh_hash_sha256_80_def =
{
  /* Name of the hash function. */
  "sha256-80",
  /* Certification status */
  0,
  /* ASN.1 Object identifier (not defined) */
  NULL,
  /* ISO/IEC dedicated hash identifier. */
  0, /* None */
  /* Digest size. */
  10,
  /* Input block length. */
  64,
  /* Context size */
  ssh_sha256_ctxsize,
  /* Reset function, between long usage of one context. */
  ssh_sha256_reset_context,
  /* Update function */
  ssh_sha256_update,
  /* Final */
  ssh_sha256_80_final,
  /* No ASN1. */
  NULL, NULL
};

#ifndef SUNWIPSEC

typedef struct
{
  SshUInt32 H[8];
  unsigned char in[64];
  SshUInt32 total_length[2];
} SshSHA256Context;

/* Right shift and rotate. */
#define ROT32(x,s)   ((((x) >> s) | ((x) << (32 - s))) & 0xffffffff)
#define SHIFT32(x,s) ((x) >> s)

/* These can be optimized---but lets do it when everything works. */
#define CH(x,y,z)  (((x) & (y)) ^ ((~(x)) & (z)))
#define MAJ(x,y,z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define BIG_SIGMA0(x)  (ROT32(x,2) ^ ROT32(x,13) ^ ROT32(x, 22))
#define BIG_SIGMA1(x)  (ROT32(x,6) ^ ROT32(x,11) ^ ROT32(x,25))
#define SMALL_SIGMA0(x) (ROT32(x,7) ^ ROT32(x,18) ^ SHIFT32(x,3))
#define SMALL_SIGMA1(x) (ROT32(x,17) ^ ROT32(x,19) ^ SHIFT32(x,10))

/* We assume that the compiler does a good job in inlining these. Any
   decent compiler should be able to observe that these are constant
   data and could thus move the values inline the code. Obviously
   this might not be the case here as one has a lot of places where
   these are requested. */
static const SshUInt32 table_h[8] = {
  0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f,
  0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
static const SshUInt32 table_c[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
  0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
  0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
  0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
  0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
  0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void ssh_sha256_reset_context(void *c)
{
  SshSHA256Context *ctx = c;
  unsigned int i;

  for (i = 0; i < 8; i++)
    ctx->H[i] = table_h[i];

  ctx->total_length[0] = 0;
  ctx->total_length[1] = 0;
}

size_t ssh_sha256_ctxsize()
{
  return sizeof(SshSHA256Context);
}

/* Single round of the SHA-256 compression function. Observe that we
   avoid copying material by renaming the variables. */
#define ROUND(a,b,c,d,e,f,g,h,j) \
do { \
  SshUInt32 T1,T2; \
  T1 = h + BIG_SIGMA1(e) + CH(e,f,g) + table_c[j] + W[j]; \
  T2 = BIG_SIGMA0(a) + MAJ(a,b,c); \
  d += T1; h = T1 + T2; \
} while(0)


static void sha256_transform(SshSHA256Context *ctx,
                             const unsigned char *block)
{
  SshUInt32 W[64];
  SshUInt32 i;
  SshUInt32 a,b,c,d,e,f,g,h;

  /* Naive implementation. */

  /* Key scheduling. */

  for (i = 0; i < 16; i++, block += 4)
    W[i] = SSH_GET_32BIT(block);
  for (i = 16; i < 64; i++)
    W[i] = SMALL_SIGMA1(W[i-2]) + W[i-7] + SMALL_SIGMA0(W[i-15]) + W[i-16];

  /* Now the actual engine. */

  /* Copy the internal state to local registers. */
  a = ctx->H[0];
  b = ctx->H[1];
  c = ctx->H[2];
  d = ctx->H[3];
  e = ctx->H[4];
  f = ctx->H[5];
  g = ctx->H[6];
  h = ctx->H[7];

  /* Fully expanded compression loop. */
#define BLOCK(j) \
  ROUND(a,b,c,d,e,f,g,h,j*8+0); \
  ROUND(h,a,b,c,d,e,f,g,j*8+1); \
  ROUND(g,h,a,b,c,d,e,f,j*8+2); \
  ROUND(f,g,h,a,b,c,d,e,j*8+3); \
  ROUND(e,f,g,h,a,b,c,d,j*8+4); \
  ROUND(d,e,f,g,h,a,b,c,j*8+5); \
  ROUND(c,d,e,f,g,h,a,b,j*8+6); \
  ROUND(b,c,d,e,f,g,h,a,j*8+7);

  BLOCK(0); BLOCK(1); BLOCK(2); BLOCK(3);
  BLOCK(4); BLOCK(5); BLOCK(6); BLOCK(7);

  /* Update the internal state. */
  ctx->H[0] = a + ctx->H[0];
  ctx->H[1] = b + ctx->H[1];
  ctx->H[2] = c + ctx->H[2];
  ctx->H[3] = d + ctx->H[3];
  ctx->H[4] = e + ctx->H[4];
  ctx->H[5] = f + ctx->H[5];
  ctx->H[6] = g + ctx->H[6];
  ctx->H[7] = h + ctx->H[7];
}

/* The rest is basically equivalent to the SHA-1 implementation. */

void ssh_sha256_update(void *c, const unsigned char *buf, size_t len)
{
  SshSHA256Context *context = c;
  unsigned int to_copy = 0;
  unsigned int in_buffer;

  SshUInt32 old_length = context->total_length[0];

  in_buffer = old_length % 64;

  context->total_length[0] += len;
  context->total_length[0] &= 0xFFFFFFFFUL;

  if (context->total_length[0] < old_length) /* carry */
    context->total_length[1]++;

  while (len > 0)
    {
      if (in_buffer == 0 && len >= 64)
        {
          sha256_transform(context, buf);
          buf += 64;
          len -= 64;
          continue;
        }

      /* do copy? */
      to_copy = 64 - in_buffer;
      if (to_copy > 0)
        {
          if (to_copy > len)
            to_copy = len;
          memcpy(&context->in[in_buffer],
                 buf, to_copy);
          buf += to_copy;
          len -= to_copy;
          in_buffer += to_copy;
          if (in_buffer == 64)
            {
              sha256_transform(context, context->in);
              in_buffer = 0;
            }
        }
    }
}

void ssh_sha256_final(void *c, unsigned char *digest)
{
  SshSHA256Context *context = c;
  int padding, i;
  unsigned char temp = 0x80;
  unsigned int in_buffer;
  SshUInt32 total_low, total_high;

  total_low = context->total_length[0];
  total_high = context->total_length[1];

  ssh_sha256_update(context, &temp, 1);

  in_buffer = context->total_length[0] % 64;
  padding = (64 - (in_buffer + 9) % 64) % 64;

  if (in_buffer > 56)
    {
      memset(&context->in[in_buffer], 0, 64 - in_buffer);
      padding -= (64 - in_buffer);
      sha256_transform(context, context->in);
      in_buffer = 0;
    }

  /* Change the byte count to bit count. */
  total_high <<= 3;
  total_high += (total_low >> 29);
  total_low <<= 3;

  SSH_PUT_32BIT(context->in + 56, total_high);
  SSH_PUT_32BIT(context->in + 60, total_low);

  if ((64 - in_buffer - 8) > 0)
    {
      memset(&context->in[in_buffer],
             0, 64 - in_buffer - 8);
    }

  sha256_transform(context, context->in);

  /* Copy the internal state to the digest output. */
  for (i = 0; i < 8; i++)
    {
      SSH_PUT_32BIT(digest + i*4, context->H[i]);
    }

  memset(context, 0, sizeof(SshSHA256Context));
}

void ssh_sha256_of_buffer(unsigned char digest[32],
                          const unsigned char *buf, size_t len)
{
  SshSHA256Context context;
  ssh_sha256_reset_context(&context);
  ssh_sha256_update(&context, buf, len);
  ssh_sha256_final(&context, digest);
}

/* Extra routines. */
void ssh_sha256_96_final(void *c, unsigned char *digest)
{
  unsigned char tmp_digest[32];
  ssh_sha256_final(c, tmp_digest);
  memcpy(digest, tmp_digest, 12);
}

void ssh_sha256_96_of_buffer(unsigned char digest[12],
                             const unsigned char *buf, size_t len)
{
  SshSHA256Context context;
  ssh_sha256_reset_context(&context);
  ssh_sha256_update(&context, buf, len);
  ssh_sha256_96_final(&context, digest);
}

void ssh_sha256_80_final(void *c, unsigned char *digest)
{
  unsigned char tmp_digest[32];
  ssh_sha256_final(c, tmp_digest);
  memcpy(digest, tmp_digest, 10);
}

void ssh_sha256_80_of_buffer(unsigned char digest[10],
                             const unsigned char *buf, size_t len)
{
  SshSHA256Context context;
  ssh_sha256_reset_context(&context);
  ssh_sha256_update(&context, buf, len);
  ssh_sha256_80_final(&context, digest);
}

/* End. */

#else /* SUNWIPSEC */

#include <sha2.h>

void
ssh_sha256_reset_context(void *c)
{
	SHA256Init((SHA256_CTX *)c);
}

size_t
ssh_sha256_ctxsize(void)
{
	return (sizeof (SHA2_CTX));
}

void
ssh_sha256_update(void *c, const unsigned char *buf, size_t len)
{
	SHA256Update((SHA256_CTX *)c, buf, len);
}

void
ssh_sha256_final(void *c, unsigned char *digest)
{
	SHA256Final((void *)digest, (SHA256_CTX *)c);
}

void
ssh_sha256_96_final(void *c, unsigned char *digest)
{
	uint8_t tmp_digest[SHA256_DIGEST_LENGTH];
	SHA256Final((void *)digest, (SHA256_CTX *)c);
	memcpy(digest, tmp_digest, 12);
}

void
ssh_sha256_80_final(void *c, unsigned char *digest)
{
	uint8_t tmp_digest[SHA256_DIGEST_LENGTH];
	SHA256Final((void *)digest, (SHA256_CTX *)c);
	memcpy(digest, tmp_digest, 10);
}

/* _of_buffer() for 256 only, to keep the rest of libike happy. */

void
ssh_sha256_of_buffer(uint8_t digest[SHA256_DIGEST_LENGTH], const uint8_t *buf,
    size_t len)
{
	SHA256_CTX ctx;

	SHA256Init(&ctx);
	SHA256Update(&ctx, buf, len);
	SHA256Final(digest, &ctx);
}

void
ssh_sha256_96_of_buffer(uint8_t digest[12], const uint8_t *buf, size_t len)
{
	uint8_t tmp_digest[SHA256_DIGEST_LENGTH];

	ssh_sha256_of_buffer(tmp_digest, buf, len);
	memcpy(digest, tmp_digest, 12);
}

void
ssh_sha256_80_of_buffer(uint8_t digest[10], const uint8_t *buf, size_t len)
{
	uint8_t tmp_digest[SHA256_DIGEST_LENGTH];

	ssh_sha256_of_buffer(tmp_digest, buf, len);
	memcpy(digest, tmp_digest, 10);
}

size_t
ssh_sha384_ctxsize(void)
{
	return (sizeof (SHA2_CTX));
}

void
ssh_sha384_reset_context(void *c)
{
	SHA384Init((SHA384_CTX *)c);
}

void
ssh_sha384_update(void *c, const unsigned char *buf, size_t len)
{
	SHA384Update((SHA384_CTX *)c, buf, len);
}

void
ssh_sha384_final(void *c, unsigned char *digest)
{
	SHA384Final((void *)digest, (SHA384_CTX *)c);
}

size_t
ssh_sha512_ctxsize(void)
{
	return (sizeof (SHA2_CTX));
}

void
ssh_sha512_reset_context(void *c)
{
	SHA512Init((SHA512_CTX *)c);
}

void
ssh_sha512_update(void *c, const unsigned char *buf, size_t len)
{
	SHA512Update((SHA512_CTX *)c, buf, len);
}

void
ssh_sha512_final(void *c, unsigned char *digest)
{
	SHA512Final((void *)digest, (SHA512_CTX *)c);
}

/* Define SHA-384 in transparent way. */
const SshHashDefStruct ssh_hash_sha384_def =
{
  /* Name of the hash function. */
  "sha384",
  /* Certification status */
  0,
  /* ASN.1 Object identifier (not defined) */
  NULL,
  /* ISO/IEC dedicated hash identifier. */
  0,
  /* Digest size. */
  48,
  /* Input block length. */
  128,
  /* Context size */
  ssh_sha384_ctxsize,
  /* Reset function, between long usage of one context. */
  ssh_sha384_reset_context,
  /* Update function */
  ssh_sha384_update,
  /* Final */
  ssh_sha384_final,
  /* No ASN1. */
  NULL, NULL
};

/* Define SHA-512 in transparent way. */
const SshHashDefStruct ssh_hash_sha512_def =
{
  /* Name of the hash function. */
  "sha512",
  /* Certification status */
  0,
  /* ASN.1 Object identifier (not defined) */
  NULL,
  /* ISO/IEC dedicated hash identifier. */
  0,
  /* Digest size. */
  64,
  /* Input block length. */
  128,
  /* Context size */
  ssh_sha512_ctxsize,
  /* Reset function, between long usage of one context. */
  ssh_sha512_reset_context,
  /* Update function */
  ssh_sha512_update,
  /* Final */
  ssh_sha512_final,
  /* No ASN1. */
  NULL, NULL
};
#endif
