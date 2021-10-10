/*

  hmac.c

  Author: Mika Kojo <mkojo@ssh.fi>
  Author: Pekka Nikander <pnr@tequila.nixu.fi>
  Author: Tomi Salo <ttsalo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created: Thu Jan  9 12:22:52 1997 [mkojo]

  Message authentication code calculation routines, using the HMAC
  structure.

  XXX Convert these to use SshHashDef structures.

  */

#include "sshincludes.h"
#include "sshcrypt.h"
#include "sshhash_i.h"
#include "sshmac_i.h"
#include "hmac.h"

#define SSH_DEBUG_MODULE "SshCryptHmac"

#ifdef SSHDIST_CRYPT_MD5
#include "ike/md5.h"
#endif /* SSHDIST_CRYPT_MD5 */
#ifdef SSHDIST_CRYPT_SHA
#include "ike/sha.h"
#endif /* SSHDIST_CRYPT_SHA */

/* Generic Hmac interface code. */
typedef struct
{
  unsigned char *ipad, *opad;
  const SshHashDefStruct *hash_def;
  void *hash_context;
} SshHmacCtx;

size_t
ssh_hmac_ctxsize(const SshHashDefStruct *hash_def)
{
  return
    sizeof(SshHmacCtx) +
    (*hash_def->ctxsize)() +
    hash_def->input_block_length * 2;
}

void
ssh_hmac_init(void *context, const unsigned char *key, size_t keylen,
              const SshHashDefStruct *hash_def)
{
  SshHmacCtx *created = context;
  unsigned int i;

  /* Compute positions in allocated space. */
  created->hash_context = (unsigned char *)created +
    sizeof(SshHmacCtx);
  created->ipad = (unsigned char *)created->hash_context +
    (*hash_def->ctxsize)();

  created->opad = created->ipad + hash_def->input_block_length;

  /* Clear pads. */
  memset(created->ipad, 0, hash_def->input_block_length * 2);

  /* Remember the hash function used to define this mac. */
  created->hash_def = hash_def;

  if (keylen > created->hash_def->input_block_length)
    {
      /* Do some hashing. */

      /* Compute the ipad. */
      (*created->hash_def->reset_context)(created->hash_context);
      (*created->hash_def->update)(created->hash_context, key, keylen);
      (*created->hash_def->final)(created->hash_context, created->ipad);

      memcpy(created->opad, created->ipad,
             created->hash_def->input_block_length);
    }
  else
    {
      memcpy(created->ipad, key, keylen);
      memcpy(created->opad, key, keylen);
    }

  for (i = 0; i < created->hash_def->input_block_length; i++)
    {
      created->ipad[i] ^= 0x36;
      created->opad[i] ^= 0x5c;
    }
}

/* Restart the Hmac operation. */
void ssh_hmac_start(void *context)
{
  SshHmacCtx *ctx = context;

  (*ctx->hash_def->reset_context)(ctx->hash_context);
  (*ctx->hash_def->update)(ctx->hash_context, ctx->ipad,
                           ctx->hash_def->input_block_length);
}

/* Update the Hmac context. */
void ssh_hmac_update(void *context, const unsigned char *buf,
                     size_t len)
{
  SshHmacCtx *ctx = context;
  (*ctx->hash_def->update)(ctx->hash_context, buf, len);
}

/* Finalize the digest. */
void ssh_hmac_final(void *context, unsigned char *digest)
{
  SshHmacCtx *ctx = context;

  (*ctx->hash_def->final)(ctx->hash_context, digest);
  (*ctx->hash_def->reset_context)(ctx->hash_context);
  (*ctx->hash_def->update)(ctx->hash_context, ctx->opad,
                           ctx->hash_def->input_block_length);
  (*ctx->hash_def->update)(ctx->hash_context, digest,
                           ctx->hash_def->digest_length);
  (*ctx->hash_def->final)(ctx->hash_context, digest);
}

/* Finalize 96 bits of the digest. */
void ssh_hmac_96_final(void *context, unsigned char *digest)
{
  SshHmacCtx *ctx = context;
  unsigned char buffer[SSH_MAX_HASH_DIGEST_LENGTH];

  (*ctx->hash_def->final)(ctx->hash_context, buffer);
  (*ctx->hash_def->reset_context)(ctx->hash_context);
  (*ctx->hash_def->update)(ctx->hash_context, ctx->opad,
                           ctx->hash_def->input_block_length);
  (*ctx->hash_def->update)(ctx->hash_context, buffer,
                           ctx->hash_def->digest_length);
  (*ctx->hash_def->final)(ctx->hash_context, buffer);
  memcpy(digest, buffer, 12);
}

/* Do everything with just one call. */
void ssh_hmac_of_buffer(void *context, const unsigned char *buf,
                        size_t len, unsigned char *digest)
{
  ssh_hmac_start(context);
  ssh_hmac_update(context, buf, len);
  ssh_hmac_final(context, digest);
}

void ssh_hmac_96_of_buffer(void *context, const unsigned char *buf,
                           size_t len, unsigned char *digest)
{
  ssh_hmac_start(context);
  ssh_hmac_update(context, buf, len);
  ssh_hmac_96_final(context, digest);
}

void ssh_hmac_zeroize(void *context)
{
  /* SshHmacCtx *ctx = context; */

  /* XXX need to make work */
  SSH_NOTREACHED;
}


/* XXX Specific Hmac interface code.  */

#ifdef SSHDIST_CRYPT_MD5

/* HMAC-MD5 */

#define MD5_INPUT_BLOCK_SIZE 64
#define MD5_OUTPUT_BLOCK_SIZE 16

typedef struct SshHmacMd5Rec
{
  unsigned char ipad[MD5_INPUT_BLOCK_SIZE];
  unsigned char opad[MD5_INPUT_BLOCK_SIZE];

  /* XXX FIXME! Remove the 1 and all such things with _the_ new version. XXX */
  unsigned char md5_context[1];
} SshHmacMd5;

size_t
ssh_hmac_md5_ctxsize()
{
  return sizeof(SshHmacMd5) + ssh_md5_ctxsize();
}

void
ssh_hmac_md5_init(void *context, const unsigned char *key, size_t keylen)
{
  SshHmacMd5 *created = (SshHmacMd5 *)context;
  int i;

  memset(created, 0, sizeof(*created) + ssh_md5_ctxsize());

  if (keylen > MD5_INPUT_BLOCK_SIZE)
    {
      ssh_md5_of_buffer(created->ipad, key, keylen);
      ssh_md5_of_buffer(created->opad, key, keylen);
    }
  else
    {
      memcpy(created->ipad, key, keylen);
      memcpy(created->opad, key, keylen);
    }

  for (i = 0; i < MD5_INPUT_BLOCK_SIZE; i++)
    {
      created->ipad[i] ^= 0x36;
      created->opad[i] ^= 0x5c;
    }
}

void ssh_hmac_md5_start(void *context)
{
  SshHmacMd5 *ctx = context;
  ssh_md5_reset_context(ctx->md5_context);
  ssh_md5_update(ctx->md5_context, ctx->ipad, MD5_INPUT_BLOCK_SIZE);
}

void ssh_hmac_md5_update(void *context, const unsigned char *buf,
                         size_t len)
{
  SshHmacMd5 *ctx = context;
  ssh_md5_update(ctx->md5_context, buf, len);
}

void ssh_hmac_md5_final(void *context, unsigned char *digest)
{
  SshHmacMd5 *ctx = context;

  ssh_md5_final(ctx->md5_context, digest);
  ssh_md5_reset_context(ctx->md5_context);
  ssh_md5_update(ctx->md5_context, ctx->opad, MD5_INPUT_BLOCK_SIZE);
  ssh_md5_update(ctx->md5_context, digest, MD5_OUTPUT_BLOCK_SIZE);
  ssh_md5_final(ctx->md5_context, digest);
}

void ssh_hmac_md5_96_final(void *context, unsigned char *digest)
{
  SshHmacMd5 *ctx = context;
  unsigned char buffer[16];

  ssh_md5_final(ctx->md5_context, buffer);
  ssh_md5_reset_context(ctx->md5_context);
  ssh_md5_update(ctx->md5_context, ctx->opad, MD5_INPUT_BLOCK_SIZE);
  ssh_md5_update(ctx->md5_context, buffer, MD5_OUTPUT_BLOCK_SIZE);
  ssh_md5_final(ctx->md5_context, buffer);
  memcpy(digest, buffer, 12);
}

void ssh_hmac_md5_of_buffer(void *context, const unsigned char *buf,
                            size_t len, unsigned char *digest)
{
  ssh_hmac_md5_start(context);
  ssh_hmac_md5_update(context, buf, len);
  ssh_hmac_md5_final(context, digest);
}

void ssh_hmac_md5_96_of_buffer(void *context, const unsigned char *buf,
                               size_t len, unsigned char *digest)
{
  ssh_hmac_md5_start(context);
  ssh_hmac_md5_update(context, buf, len);
  ssh_hmac_md5_96_final(context, digest);
}
#endif /* SSHDIST_CRYPT_MD5 */

#ifdef SSHDIST_CRYPT_SHA

#define SHA_INPUT_BLOCK_SIZE 64
#define SHA_OUTPUT_BLOCK_SIZE 20

/* HMAC-SHA */

typedef struct SshHmacShaRec
{
  unsigned char ipad[SHA_INPUT_BLOCK_SIZE];
  unsigned char opad[SHA_INPUT_BLOCK_SIZE];

  unsigned char sha_context[1];
} SshHmacSha;

size_t
ssh_hmac_sha_ctxsize()
{
  return sizeof(SshHmacSha) + ssh_sha_ctxsize();
}

void
ssh_hmac_sha_init(void *context, const unsigned char *key, size_t keylen)
{
  SshHmacSha *ctx = (SshHmacSha *)context;
  int i;

  memset(ctx, 0, sizeof(*ctx) + ssh_sha_ctxsize());

  if (keylen > SHA_INPUT_BLOCK_SIZE)
    {
      ssh_sha_of_buffer(ctx->ipad, key, keylen);
      ssh_sha_of_buffer(ctx->opad, key, keylen);
    }
  else
    {
      memcpy(ctx->ipad, key, keylen);
      memcpy(ctx->opad, key, keylen);
    }

  for (i = 0; i < SHA_INPUT_BLOCK_SIZE; i++)
    {
      ctx->ipad[i] ^= 0x36;
      ctx->opad[i] ^= 0x5c;
    }
}

void ssh_hmac_sha_start(void *context)
{
  SshHmacSha *ctx = context;

  ssh_sha_reset_context(ctx->sha_context);
  ssh_sha_update(ctx->sha_context, ctx->ipad, SHA_INPUT_BLOCK_SIZE);
}

void ssh_hmac_sha_update(void *context, const unsigned char *buf,
                         size_t len)
{
  SshHmacSha *ctx = context;
  ssh_sha_update(ctx->sha_context, buf, len);
}

void ssh_hmac_sha_final(void *context, unsigned char *digest)
{
  SshHmacSha *ctx = context;

  ssh_sha_final(ctx->sha_context, digest);
  ssh_sha_reset_context(ctx->sha_context);
  ssh_sha_update(ctx->sha_context, ctx->opad, SHA_INPUT_BLOCK_SIZE);
  ssh_sha_update(ctx->sha_context, digest, SHA_OUTPUT_BLOCK_SIZE);
  ssh_sha_final(ctx->sha_context, digest);
}

void ssh_hmac_sha_of_buffer(void *context, const unsigned char *buf,
                            size_t len, unsigned char *digest)
{
  ssh_hmac_sha_start(context);
  ssh_hmac_sha_update(context, buf, len);
  ssh_hmac_sha_final(context, digest);
}

void ssh_hmac_sha_96_final(void *context, unsigned char *digest)
{
  SshHmacSha *ctx = context;
  unsigned char buffer[20];

  ssh_sha_final(ctx->sha_context, buffer);
  ssh_sha_reset_context(ctx->sha_context);
  ssh_sha_update(ctx->sha_context, ctx->opad, SHA_INPUT_BLOCK_SIZE);
  ssh_sha_update(ctx->sha_context, buffer, SHA_OUTPUT_BLOCK_SIZE);
  ssh_sha_final(ctx->sha_context, buffer);

  memcpy(digest, buffer, 12);
}

void ssh_hmac_sha_96_of_buffer(void *context, const unsigned char *buf,
                               size_t len, unsigned char *digest)
{
  ssh_hmac_sha_start(context);
  ssh_hmac_sha_update(context, buf, len);
  ssh_hmac_sha_96_final(context, digest);
}

#endif /* SSHDIST_CRYPT_SHA */

/* hmac.c */
